# UltraCanvas Element Plugin System — Plan & Proposal

Status: **Proposal — not yet implemented.** Plan for a real, on-demand
plugin system covering **charts, diagrams, tools and widgets**: elements
become installable, dynamically loadable modules instead of compile-time
members of the core library.

Author: UltraCanvas Framework
Last Modified: 2026-07-30

---

## 1. Where we are today

The framework currently has **three different degrees of "plugin"**, and
only one of them is dynamic:

| Mechanism | What it covers | Dynamic? |
|---|---|---|
| `Plugins/Charts`, `Plugins/Diagrams`, … source folders compiled into `libultracanvas.a` | All ~40 chart/diagram elements, widgets | No — organizational only. Link-time selection: an app that never references `CreateKanbanBoardElement` doesn't carry its code, but nothing can be added after build |
| In-process registries: `UltraCanvasGraphicsPluginRegistry` (`IGraphicsPlugin`, by file extension), FileLoader facade | File-format handlers | No — handlers must be registered by code already linked in |
| **UltraNet protocol plug-ins** (`Plugins/UltraNet/{mqtt,amqp,ssh,grpc,…}`), LaTeX module | Tier 2/3 protocols, LaTeX rendering | **Yes** — CMake `MODULE` DSOs discovered in a plugin directory, loaded with `dlopen`/`LoadLibraryA`, entered through a versioned C entry point |

The UltraNet system (`include/UltraNet/UltraNetPlugins.h`,
`core/UltraNet/UltraNetPlugins.cpp`) is the proven in-repo precedent and
already solved the hard parts:

- **v2 entry contract**: the DSO exports one C symbol,
  `UltraNet_PluginInit(const UltraNetPluginHost* host)`; the host passes a
  function-pointer table (`abiVersion`, `RegisterPlugin`) so the plug-in
  never needs to resolve host symbols — which is what makes Windows work
  (no `RTLD_GLOBAL`/`-rdynamic` dependence).
- **ABI version handshake** (`ULTRANET_PLUGIN_HOST_ABI_VERSION`).
- **Export macro** per platform (`__declspec(dllexport)` /
  `visibility("default")`).
- **Directory discovery** accepting `.so`/`.dylib`/`.dll`, idempotent
  `RefreshPlugins()`, settable plugin directory.

**The plan in one sentence: lift this contract out of UltraNet into a
framework-level element plugin system, give every chart/diagram/tool/widget
a registrable descriptor, and make the core library's bundled elements just
one (default-on) provider among equals.**

---

## 2. Goals and non-goals

Goals:

1. **On-demand loading** — an application (or ULTRA OS) can ship without
   any chart/diagram/tool code and gain them by dropping a DSO + manifest
   into a plugin directory; elements load at first use, not at startup.
2. **One registry, four categories** — charts, diagrams, widgets and tools
   register through the same host contract; a single DSO may register
   several elements plus their file formats and text-definition handlers.
3. **Create-by-name** — `CreateElement("kanban-board", …)` works for
   bundled and plugged elements alike. This is also the missing enabler
   for UI-from-file (the template system) and for a generic "render this
   Mermaid/diagram text" API.
4. **Zero breakage** — every existing element keeps compiling into the
   core library by default; existing factory functions
   (`CreateGanttChartElement(...)` etc.) keep working unchanged.
5. **Same-toolchain C++ plugins** — like UltraNet, plugins pass C++
   interfaces (`shared_ptr`) across the boundary and are built with the
   framework's toolchain. ULTRA OS controls its toolchain, so this is an
   acceptable, hugely simpler contract than a pure C ABI.

Non-goals (documented, revisitable — see §9):

- Out-of-process/sandboxed plugins, crash isolation.
- Cross-compiler binary compatibility (MSVC plugin into GCC host).
- Hot *unloading* while elements are alive (see §6.4).
- WASM dynamic loading (Emscripten side modules) — the registry works on
  WASM, DSO loading compiles out.

---

## 3. Architecture

New units (names follow the UltraNet precedent):

```
include/UltraCanvasElementPlugins.h      # descriptors, registry, host ABI
core/UltraCanvasElementPlugins.cpp       # registry + DSO loader
include/UltraCanvasPluginLoader.h        # dlopen/LoadLibrary/manifest shared
core/UltraCanvasPluginLoader.cpp         #   helpers, lifted from UltraNetPlugins.cpp
```

### 3.1 Element descriptors

Every pluggable thing is described by a descriptor registered under a
stable, kebab-case **type name**:

```cpp
enum class UCPluginCategory { Chart, Diagram, Widget, Tool, FileFormat };

struct UCElementDescriptor {
    std::string typeName;          // "kanban-board", "gantt-chart", "cfd"
    UCPluginCategory category = UCPluginCategory::Widget;
    std::string displayName;       // "Kanban Board"
    std::string description;       // One line for pickers/docs
    std::string version;           // Plugin-defined
    std::string docPath;           // "Docs/UltraCanvas/UltraCanvasKanbanBoard.md"

    // Mandatory: plain factory, the shape every element already has.
    std::function<std::shared_ptr<UltraCanvasUIElement>(
            const std::string& id, int x, int y, int w, int h)> create;

    // Optional capabilities — set what applies:
    std::function<std::shared_ptr<UltraCanvasUIElement>(
            const std::string& id, int x, int y, int w, int h,
            const std::string& textDefinition,
            std::string* error)> createFromText;   // Mermaid "kanban", "gantt"…
    std::vector<std::string> textKeywords;         // First word of definitions
    std::vector<std::string> fileExtensions;       // Opens these via FileLoader
};
```

Tools (OCR, vectorizer, QR/barcode workbenches — the demo's Tools
category) are elements too from the registry's point of view: their
`create` returns the tool's panel/workbench element; an extra optional
`invoke` hook covers headless operations (e.g. "OCR this image, give me
text") without instantiating UI.

### 3.2 The registry

```cpp
class UltraCanvasElementRegistry {
public:
    static void Register(const UCElementDescriptor& d);      // idempotent by typeName
    static void Unregister(const std::string& typeName);

    static std::shared_ptr<UltraCanvasUIElement> Create(
            const std::string& typeName, const std::string& id,
            int x, int y, int w, int h);                     // triggers lazy DSO load
    static std::shared_ptr<UltraCanvasUIElement> CreateFromText(
            const std::string& textDefinition, /* keyword-dispatched */ ...);

    static const UCElementDescriptor* Find(const std::string& typeName);
    static std::vector<UCElementDescriptor> List(UCPluginCategory category);
    static std::vector<UCElementDescriptor> ListAll();       // bundled + plugged
};
```

Three providers feed it, through one `Register` call:

1. **Bundled elements** (default): the core library keeps compiling all
   current elements. Each element family gains a tiny glue file
   (`RegisterKanbanElements()`, `RegisterGanttElements()`, …) invoked from
   a generated `UltraCanvas_RegisterBundledElements()` during
   `UltraCanvasApplication` init. Explicit calls — not static
   initializers — because static-archive members with only
   self-registration objects get dropped by the linker.
2. **App-side opt-in**: an app that builds a lean core
   (`ULTRACANVAS_BUNDLED_ELEMENTS=OFF`, §5) links only the element
   libraries it wants and calls their `Register…()` functions itself.
3. **Dynamic DSOs** — the new part, below.

### 3.3 The DSO contract (generalized UltraNet v2)

```cpp
constexpr int ULTRACANVAS_PLUGIN_HOST_ABI_VERSION = 1;

struct UltraCanvasPluginHost {
    int abiVersion;                 // Entry-contract version (C level)
    const char* frameworkVersion;   // e.g. "0.3.19"
    const char* frameworkAbiTag;    // Toolchain/stdlib tag, see below
    void (*RegisterElement)(const UCElementDescriptor*);
    void (*RegisterGraphicsPlugin)(std::shared_ptr<IGraphicsPlugin>);
    void (*Log)(int level, const char* message);
};

// The one exported symbol. Returns false to refuse loading (mismatch).
extern "C" ULTRACANVAS_PLUGIN_EXPORT
bool UltraCanvas_PluginInit(const UltraCanvasPluginHost* host);
```

Design points, each inherited from or improving on the UltraNet version:

- **Host vtable, not symbol lookup** → works on Windows without
  `-rdynamic`, and lets one loader serve app-embedded and
  framework-shared-library hosts alike.
- **Two-layer versioning.** `abiVersion` guards the C entry contract
  (append-only vtable growth). `frameworkAbiTag` — a string like
  `"gcc-libstdc++-cxx20-uc1"` baked at build time — guards the *C++*
  boundary: the plugin compares it to its own and returns `false` on
  mismatch instead of crashing later. UltraNet has no such guard; this is
  the main hardening the generalization adds.
- **`bool` return + refusal path** so a mismatched or unhappy plugin is
  skipped with a log line, never half-registered.
- **No exceptions across the boundary**: the host wraps the init call;
  the macro-provided plugin skeleton wraps descriptor callbacks.
- One DSO may register *many* descriptors (e.g. `ultracanvas-kanban.so`
  registers `kanban-board` **and** `cumulative-flow-chart`).

### 3.4 Discovery, manifests, and lazy loading

Next to each DSO sits a small manifest, `<name>.ucplugin.json` (parsed
with `UltraCanvasJSON`):

```json
{
  "name": "ultracanvas-kanban",
  "version": "1.0.0",
  "minFramework": "0.3.19",
  "abiTag": "gcc-libstdc++-cxx20-uc1",
  "provides": [
    {"type": "kanban-board", "category": "diagram",
     "displayName": "Kanban Board", "textKeywords": ["kanban"]},
    {"type": "cumulative-flow-chart", "category": "chart",
     "displayName": "Cumulative Flow Diagram"}
  ]
}
```

The manifest is what makes the system genuinely **on-demand**:

- At startup the registry scans plugin directories and indexes
  **manifests only** — no DSO is opened. Cost: a few JSON parses.
- Pickers/UIs can list every available element (bundled + installed)
  from the index.
- The DSO is `dlopen`ed on the **first `Create()` of one of its types**
  (or first `CreateFromText` hitting one of its keywords, or first file
  open matching its extensions). After init, the descriptor registered by
  the DSO replaces the manifest stub.
- A DSO without a manifest still works (opened eagerly during refresh,
  like UltraNet does today) — manifests are an optimization, not a gate.

Plugin directories, in load order (later wins on name conflicts):
system (`<prefix>/lib/ultracanvas/plugins`), user
(`~/.local/share/ultracanvas/plugins` / platform equivalent), app-provided
(`UltraCanvas_AddPluginDirectory`), `ULTRACANVAS_PLUGIN_PATH` env var.
Never the current working directory (§7).

---

## 4. What plugs in, per category

| Category | Interface surface | First candidates |
|---|---|---|
| **Charts** | `create` (+ optional CSV/`IChartDataSource` hookup later) | CumulativeFlowChart, contour family (pulls heavy marching-squares/3D code out of lean builds) |
| **Diagrams** | `create` + `createFromText`/`textKeywords` — gives the framework a single `RenderDiagramText(text)` API that dispatches "kanban"→board, future "flowchart"→flowchart… | KanbanBoard (pilot; its Mermaid loader exists), FlowChart, Sankey |
| **Widgets** | `create`; create-by-name feeds `UltraCanvasTemplate`/future UI-from-file | Spreadsheet, EBookViewer, MediaViewer — the heavyweight widgets |
| **Tools** | `create` (workbench panel) + optional headless `invoke` | OCR (Docs list it as "Investigation & Proposal" — specify it as a plugin from day one), Vectorizer, QR/Barcode |
| **File formats** | existing `IGraphicsPlugin` via `host->RegisterGraphicsPlugin` — folded into the same DSOs | CDR, XAR (already separate CMake targets), LaTeX (already a dlopen'ed MODULE — port it to the common loader) |

---

## 5. Build & packaging

New CMake helper, mirroring how UltraNet plugin targets are declared:

```cmake
ultracanvas_add_element_plugin(
    NAME ultracanvas-kanban
    SOURCES Plugins/Charts/UltraCanvasKanbanBoard.cpp
            Plugins/Charts/UltraCanvasCumulativeFlowChart.cpp
    MANIFEST Plugins/Charts/kanban.ucplugin.json)
```

producing a CMake `MODULE` target (the LaTeX precedent: `.so` on both
Linux and macOS — the loader already accepts `.so`/`.dylib`/`.dll`),
installing DSO + manifest to the plugin directory, and stamping the
manifest's `abiTag`/`minFramework` at configure time.

Two build modes from the same sources — **an element's `.cpp` never
changes between them**, only the glue file differs:

- `ULTRACANVAS_BUNDLED_ELEMENTS=ON` (default): today's behaviour, all
  elements in the core lib, `Register…()` called at init. No packaging
  change for existing users.
- `ULTRACANVAS_BUNDLED_ELEMENTS=OFF` + a plugin list: lean core; selected
  element families built as plugin DSOs. ULTRA OS uses this mode.

Per-plugin third-party licensing recorded in `THIRD_PARTY_LICENSES.md`
and `master_dependencies.yaml` as usual (house rule 4) — a plugin carrying
a vendored library (e.g. contour's KissFFT sibling) keeps that record even
when the code leaves the core build.

---

## 6. Loader semantics & lifetime

1. **Idempotent refresh** (UltraNet pattern): loaded paths are remembered;
   `RefreshPlugins()` can run any time to pick up newly installed DSOs.
2. **Load failure is a log line, not a crash**: missing symbol, `false`
   from init, ABI mismatch → skip and continue; the manifest stub is
   marked unavailable so `Create` returns `nullptr` with a reason
   retrievable via `GetLastPluginError()`.
3. **Name conflicts**: last directory wins (user overrides system);
   re-registering an already-live typeName is ignored with a warning
   (matches both existing registries' duplicate policy).
4. **No unloading in v1.** Elements are `shared_ptr`s whose vtables live
   in the DSO; unloading with any instance (or any registered
   `std::function`) alive is undefined behaviour. `dlclose` is simply
   never called (the OS reclaims at exit). Refcounted unload is future
   work (§9) and the API reserves `UnloadPlugin` returning
   `NotSupported` so callers are honest about it today.

---

## 7. Security & robustness

- Load only from the fixed directory list (§3.4); never CWD, never
  paths from documents. A document can *name* an element type; it can
  never cause a DSO path to be loaded.
- Manifests are parsed with the strict `UltraCanvasJSON` defaults
  (depth-limited, RFC-conformant).
- ABI tag + framework version checked **before** any C++ object crosses
  the boundary.
- Plugin init runs behind a try/catch and a time-bounded log; a plugin
  that throws is refused.
- DSOs are in-process: this is a *compatibility and hygiene* boundary,
  not a security sandbox — stated plainly in the docs. Untrusted-plugin
  sandboxing is out of scope (§9).

---

## 8. Delivery phases

**P1 — Registry + bundled providers (no behaviour change).**
`UltraCanvasElementPlugins.{h,cpp}`, descriptors, `Register…()` glue for
all existing chart/diagram/tool/widget families, generated
`RegisterBundledElements()`, create-by-name + `List()` working, demo app
gains a "Plugins" page listing the registry (category, name, version,
origin bundled/DSO). Headless unit tests for the registry.

**P2 — The DSO loader.** Lift `PluginOpen`/suffix/discovery helpers out
of `UltraNetPlugins.cpp` into `UltraCanvasPluginLoader`, port UltraNet to
the shared helpers (behaviour-identical), implement the
`UltraCanvas_PluginInit` host contract + manifest scan + lazy load.
**Pilot: `ultracanvas-kanban` DSO** (KanbanBoard + CumulativeFlowChart —
newest, self-contained, has `createFromText`), built and loaded in CI; a
lean-build demo proves an app with `BUNDLED_ELEMENTS=OFF` renders a board
loaded from the DSO.

**P3 — Migration & fold-in.** `ultracanvas_add_element_plugin` for the
remaining families (each chart/diagram family becomes *buildable* as a
plugin; bundled stays the default); port LaTeX and CDR/XAR to the common
contract; route `RegisterGraphicsPlugin` through the host vtable; tools
(OCR spec'd as plugin-first, Vectorizer, QR/Barcode).

**P4 — Consumers.** `RenderDiagramText()` keyword dispatch;
template/UI-from-file integration using create-by-name; FileLoader
consults the registry for extension-mapped elements; ULTRA OS packaging
(plugin install directory conventions, versioned filenames).

Each phase is releasable on its own; after P1 nothing observable changes
for existing apps, after P2 the system is real end-to-end.

---

## 9. Open questions

1. **Unload support** — worth the refcounting complexity, or is
   process-lifetime residency (the browser-extension model) acceptable on
   ULTRA OS? Recommendation: residency, revisit only with a concrete
   memory-pressure case.
2. **Pure C ABI** for cross-compiler plugins — large surface cost
   (every descriptor callback becomes a C shim). Recommendation: defer;
   the `frameworkAbiTag` handshake keeps mismatches safe-failing.
3. **WASM** — Emscripten `SIDE_MODULE`s could map onto the same contract
   later; P1's registry already gives WASM builds create-by-name over
   bundled elements.
4. **Signing / trust** for ULTRA OS plugin distribution — belongs to the
   OS packaging layer, not the loader; the loader's contribution is the
   fixed-directory policy.
5. **Per-plugin resources** (icons, translations, demo pages) — probably
   a `resources/` subfolder next to the DSO, resolved via the manifest;
   needs a small VirtualFS hook. Decide in P3.

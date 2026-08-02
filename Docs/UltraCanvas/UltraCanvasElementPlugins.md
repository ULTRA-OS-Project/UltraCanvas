# UltraCanvasElementPlugins Documentation

## Overview

`UltraCanvasElementRegistry` is the framework-wide element plugin system: every
pluggable thing — chart, diagram, widget, tool — is described by a
`UCElementDescriptor` registered under a stable kebab-case type name, whether it
is compiled into the application or loaded on demand from a plugin DSO. It
implements the registry (P1) and loader core (P2) of
[`UltraCanvasElementPluginSystemProposal.md`](UltraCanvasElementPluginSystemProposal.md);
manifest-indexed selective loading and the UltraNet port onto the shared loader
helpers are still to come.

- Registry + loader: `include/UltraCanvasElementPlugins.h`,
  `core/UltraCanvasElementPlugins.cpp`
- DSO helpers (shared, platform-neutral): `include/UltraCanvasPluginLoader.h`,
  `core/UltraCanvasPluginLoader.cpp`
- Named-property surface: `include/UltraCanvasElementProperties.h`
- Tests: `Tests/ElementPluginTest.cpp` (`ctest -R ElementPluginTest`) — includes
  a real end-to-end DSO load of two test plugins built as MODULE libraries

**Version:** 1.0.0
**Last Modified:** 2026-08-01
**Author:** UltraCanvas Framework
**Namespace:** `UltraCanvas`

## Registering an element

```cpp
#include "UltraCanvasElementPlugins.h"

UCElementDescriptor d;
d.typeName    = "kanban-board";                 // stable, kebab-case
d.category    = UCPluginCategory::Diagram;      // Chart | Diagram | Widget | Tool | FileFormat
d.displayName = "Kanban Board";
d.description = "Card board with WIP limits";
d.docPath     = "Docs/UltraCanvas/UltraCanvasKanbanBoard.md";
d.create      = [](const std::string& id, int x, int y, int w, int h) {
    return std::static_pointer_cast<UltraCanvasUIElement>(
        CreateKanbanBoardElement(id, x, y, w, h));
};
// Optional capabilities:
d.textKeywords   = {"kanban"};                  // dispatch for CreateFromText
d.createFromText = ...;                         // Mermaid-style text loader
d.propertyKeys   = {"columns", "wipLimit"};     // IConfigurableElement keys
UltraCanvasElementRegistry::Register(d);
```

Registration is idempotent by type name: re-registering a live name is ignored
with a warning. A descriptor without a `create` factory is rejected.

**Never register through a file-scope static.** A self-registering static
forces the linker to keep every element's object file in every binary. Register
from explicit `Register…()` functions (bundled elements), or from a plugin's
init (below).

## Creating by name

```cpp
auto board = UltraCanvasElementRegistry::Create("kanban-board", "b1", 20, 20, 800, 500);

// Keyword-dispatched text creation - the first word selects the element:
std::string error;
auto fromText = UltraCanvasElementRegistry::CreateFromText(
    "kanban\n  column: Todo\n", "b2", 0, 0, 800, 500, &error);

// Discovery for pickers:
for (const UCElementDescriptor& d : UltraCanvasElementRegistry::List(UCPluginCategory::Chart))
    printf("%s - %s\n", d.typeName.c_str(), d.displayName.c_str());
```

A miss on `Create`/`CreateFromText` triggers an on-demand refresh of the plugin
directories (or the custom resolver installed with `SetMissingTypeResolver`) and
retries once. A miss that stays a miss returns `nullptr` and explains itself in
`UltraCanvas_GetLastPluginError()`.

## Configuring a by-name element

`Create` returns the element base class, which has none of the element's typed
API. Three paths, in order of preference:

1. Typed factory (`CreateKanbanBoardElement(...)`) — full API, requires the
   element compiled into the application.
2. `std::dynamic_pointer_cast<UltraCanvasKanbanBoardElement>` — full API,
   requires the header and a plugin built with the same toolchain.
3. **Named properties** — works across any module boundary:

```cpp
#include "UltraCanvasElementProperties.h"

if (auto cfg = std::dynamic_pointer_cast<IConfigurableElement>(board)) {
    cfg->SetProperty("columns", int64_t{4});         // false = unknown key/bad value
    cfg->SetProperty("title", std::string("Sprint"));
}
```

Elements implement `IConfigurableElement`, usually by embedding a
`UCPropertyBag` (define keys with defaults; undefined keys are rejected).
Descriptors advertise their keys in `propertyKeys`.

## Writing a plugin DSO

```cpp
// myplugin.cpp - built as a CMake MODULE library
#include "UltraCanvasElementPlugins.h"

static bool Init(const UltraCanvas::UltraCanvasPluginHost* host) {
    UltraCanvas::UCElementDescriptor d;
    /* ... fill as above ... */
    host->RegisterElement(&d);       // never resolve host symbols directly
    return true;                     // false = refuse (missing backend, ...)
}
ULTRACANVAS_DEFINE_ELEMENT_PLUGIN(Init)
```

The macro defines the one exported symbol, `UltraCanvas_PluginInit`, with the
handshake already in place: it verifies the host's `abiVersion`, compares the
host's `frameworkAbiTag` (compiler + standard library + C++ standard +
descriptor version, e.g. `gcc-libstdc++-cxx20-uc1`) against the plugin's own,
and fences exceptions. A mismatched or unhappy plugin is skipped with a log
line — never half-registered. One DSO may register many descriptors.

## Loader behaviour

```cpp
UltraCanvas_AddPluginDirectory("/usr/lib/ultracanvas/plugins");
int loaded = UltraCanvas_RefreshElementPlugins();   // explicit; usually implicit via Create
```

- Search is restricted to the configured directories plus
  `ULTRACANVAS_PLUGIN_PATH` (`:`-separated; `;` on Windows) — never the working
  directory, never paths from documents.
- Accepted files: `.so` / `.dylib` (POSIX), `.dll` (Windows).
- Refresh is idempotent (canonical-path keyed); plugins that refused are
  remembered and not reopened.
- Load failures are log lines and `UltraCanvas_GetLastPluginError()`, never
  fatal.
- **No unloading**: handles stay open for the process lifetime, because element
  vtables live in the DSO.
- POSIX loads use `RTLD_GLOBAL` so `dynamic_cast` works across module
  boundaries (path 2 above).
- **Static-core hosts must call `UltraCanvas_RegisterSelectedCharts()` at
  startup** (harmless no-op body when nothing is selected): besides registering
  the compile-time chart selection, its generated TU anchors the chart engine's
  objects into the executable. A static archive only keeps referenced members,
  so without the anchor the linker strips the engine and a runtime chart module
  fails at `dlopen` with an undefined base-class symbol. Shared-core builds
  need no anchor - modules link the DLL/so directly.

## Best Practices

1. Type names are API — kebab-case, never renamed once shipped.
2. Register explicitly, never from static initialisers.
3. Refuse (`return false`) before registering anything; never after.
4. Advertise `propertyKeys` so document-driven callers can configure blind.
5. Plugins must be built with the framework's toolchain — the ABI tag enforces
   this at load time rather than at crash time.

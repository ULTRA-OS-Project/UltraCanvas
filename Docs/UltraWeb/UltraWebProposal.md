# UltraWeb — WebAssembly Apps on UltraCanvas, Then the Web

**Status:** Proposal — nothing implemented yet; Phase 1 (the WebAssembly
app host, §4) is next. This document is the feasibility study, the
architecture and the phase plan.
**Author:** UltraCanvas Framework / ULTRA OS
**Last Modified:** 2026-10-06

**Decision (2026-10-06).** UltraWeb starts as a **host for WebAssembly
applications written against UltraCanvas**: a URL loads a `.wasm` module,
and the module builds its UI out of real UltraCanvas elements that the
browser owns. The HTML reader browser of the first draft (2026-10-01) comes
next. The end goal is that **React and Angular single-page apps run**,
through QuickJS compiled to WebAssembly running as one more module on the
same runtime. That end goal was **measured before the path was chosen**
(§13). It is reachable. The JavaScript engine is not what limits it; the
native DOM work is (§7.3).

**Short answer:**

- **A WebAssembly app host can be built from existing parts.** wasmtime runs
  QuickJS's C code at 1.2× the time of the same code compiled natively.
  It runs C++ code that uses exceptions, which the framework needs. A call
  from guest code into the host costs 7 ns, or 52 ns when it carries a
  string, through wasmtime's unchecked host-function API. So a generic
  element ABI, one call per property set, is cheap enough (§4, §13.4).
- **React 19.3 and Angular 22.2 production builds run unmodified on
  QuickJS-ng**, both natively and compiled to WebAssembly under wasmtime and
  WAMR. Angular was tested both zoneless and with zone.js. All three pass
  every DOM check of the js-framework-benchmark table: create, update,
  select, swap, remove, 10,000 rows (§13.1).
- **Speed:** QuickJS inside wasmtime runs this workload 10–15× slower than
  V8 with its JIT (geometric mean over the operations), and 2.6–3.2× slower
  than V8's own interpreter.
  - A click that updates, selects or removes rows in a 1,000-row table
    takes 10–33 ms in React (106 ms for a swap) and 10–11 ms in Angular.
    That is usable for interactive apps.
  - Rendering 1,000 new rows takes 0.55–0.72 s and 10,000 rows takes
    6–17 s.
    That is slow for big first renders. Part of each figure is the
    JavaScript test DOM, which UltraWeb replaces with a native one
    (§13.2).
- **The long pole is native.** It needs four things:
  - a live, mutable DOM;
  - incremental DOM → element patching;
  - synchronous layout queries;
  - flex/grid mapping.

  Today's `ElementBuilder` is strictly one-shot. It folds inline content
  into Pango-markup labels and keeps no node → element map. Patching it is a
  redesign, not an extension (§7.3).

| Capability | Phase | Basis |
|---|---|---|
| Load a `.wasm` app from a file or URL and run it sandboxed | 1 | wasmtime behind `UltraCanvasWasmHost`, §4.1 |
| App UI from UltraCanvas elements through the element ABI | 1 | element ABI v1 and guest SDK, §4.2 |
| fetch, timers, storage, clipboard for apps; per-app permissions | 1 | UltraNet and new host services, §4.3 |
| Fetch and render HTML + CSS pages; links, images, history, downloads | 2 | HTMLReader + CSSLayout + `HTML::PageLoader`, §5 |
| Flex, grid, `position`, viewport units, `calc()` | 3 | CSSLayout has the layouts; the resolver does not map them, §6.2 |
| Live DOM, incremental patching, layout queries, DOM events | 4 | new, §7.3 |
| JavaScript: React and Angular apps | 5 | QuickJS-ng in WebAssembly plus DOM bindings, §7 |
| System web view for pages nothing else can show | optional | §8 |

---

## 1. What this is, and what it is not

UltraWeb is an **application** (`Apps/UltraWeb`) plus reusable framework
pieces:

- **`UltraCanvasWasmHost`** wraps the WebAssembly runtime: loading modules,
  instance limits, imports and exports. It follows the wrapped-engines rule,
  so no wasmtime type appears in a public header and the runtime can be
  swapped (§4.1).
- **The element ABI and the guest SDK** are the contract between a guest
  module and the host's element tree, and the headers app authors compile
  against (§4.2).
- **`HTML::PageLoader` and `UltraCanvasWebView`** turn a URL into a page of
  UltraCanvas elements (Phase 2, §5).
- **The web-compat module** (Phase 5) is QuickJS-ng plus DOM bindings,
  compiled to WebAssembly. The host loads it for pages with scripts (§7).

It is **not** a wrapper around a system browser engine (§8). And it is
**not** the Emscripten port in `UltraCanvas/OS/WASM`. That port runs
UltraCanvas *inside* a browser; UltraWeb *is* the browser (§2.1).

---

## 2. What already exists

### 2.1 The WebAssembly port, and why UltraWeb cannot simply run its output

`UltraCanvas/OS/WASM` builds the framework with Emscripten for Chromium.
Everything is compiled in: the framework, cairo, pango, glib and vips.
Frames are presented with `putImageData`, input comes from Emscripten's
HTML5 callbacks, and threads are Web Workers. A module built that way
imports Emscripten's JavaScript glue: `EM_ASM` blocks are JavaScript
source run by the page, and the browser APIs sit behind them. So it runs
only where a browser's JavaScript engine and DOM exist.

UltraWeb embeds a plain WebAssembly runtime instead. Its guests are built
for `wasm32-wasip1` with wasi-sdk, against UltraWeb's own imports (§4).

What carries over:

- The experience of cross-compiling the dependency stack to WebAssembly:
  `build-wasm-sysroot.sh`, and the pango patch for mismatched indirect
  calls. Every runtime checks call signatures strictly, not only the
  browser.
- The rule that `Run()` does not block.
- One reuse worth keeping open: the Emscripten build could implement
  UltraWeb's imports inside an ordinary browser. The same guest module
  would then also run on the open web (§14, question 5).

### 2.2 HTMLReader, CSSLayout, UltraNet

The HTMLReader module (`UltraCanvas/{include,core}/HTMLReader/`, about
5,000 lines) is the core of the Phase 2 browser. It does not lay pages out
itself. It builds an element tree, and the CSSLayout engine lays that out,
so there is no second layout engine to write.

| Piece | File | What it gives the browser |
|---|---|---|
| Parser | `HTMLParser.h` | Tolerant HTML/XHTML: unclosed `<p>`/`<li>`, void elements, CDATA, entities, raw-text `<script>`/`<style>`. Not the HTML5 tree-construction algorithm (§6.1). |
| DOM | `HTMLDocument.h` | `Node` with `children` and a `parent` pointer, `GetElementById`, `<title>`, `<meta>`, `<style>` blocks and `<link rel=stylesheet>` hrefs collected. Only `SetAttribute` mutates (§7.3). |
| CSS | `CSSStyleSheet.h`, `HTMLStyleResolver.h` | Cascade with specificity and source order, inline `style=""`, user-agent defaults, `@media` min/max-width, `margin: auto`, `max-width`, floats, background images, border radius, `border-collapse`, `white-space`, `letter-spacing`; structural pseudo-classes and attribute selectors (§6.3). |
| Builder | `HTMLElementBuilder.h` | Containers for blocks, `UltraCanvasLabel` runs with Pango markup for inline text, images flowing in text, content flowing around floats, tables on the CSSLayout table engine, inline-block boxes, a map of `id`/`name` anchors. |
| Hooks | `HTML::BuildOptions` | `resourceLoader` (images and linked stylesheets as bytes), `onLinkActivated` (raw href of the clicked link), `viewportWidth` (for `@media`), `userCss`. |
| Network | `UltraNet/UltraNetHttp.h` | Sync and async requests, streaming chunks, progress, redirects (`maxRedirects = 10`), TLS verification on, `finalUrl` after redirects. |
| Sessions | `UltraNet/UltraNetCookies.h` | `UltraNet_CreateSession`, session GET/POST, cookie load/save. |
| URLs | `UltraNet/UltraNetUrl.h` | Parse, build, encode, query strings. **No relative-reference resolution** (§5.1). |
| UI | `UltraCanvasTabbedContainer`, `UltraCanvasTextInput`, `UltraCanvasMenu`, `UltraCanvasDropdown`, `UltraCanvasCheckbox`, `UltraCanvasRadio` | The browser frame and real form controls. |
| SVG | `Plugins/SVG/UltraCanvasSVGPlugin.cpp` | `<img src=*.svg>` and inline `<svg>` (§6.4). |
| Packages | `VirtualFS/VirtualFSCompression.h` | Zstd/Brotli/LZ4 buffers for `.ucpkg` app packages (§4.3). |

Two consumers prove the reader on real-world markup today: the eBook
engines (EPUB, FB2, MOBI) and UltraMail's HTML message view.

---

## 3. Architecture

```
 Apps/UltraWeb ── window, tabs, address bar, permissions, console
 ──────────────────────────────────────────────────────────────────────────────
 each tab holds one of:
   WASM app       guest .wasm ──(element ABI)──► host element tree
   HTML page      PageLoader ► HTML::Document ► ElementBuilder ► element tree
   scripted page  web-compat .wasm (QuickJS + DOM bindings)
                    ──(DOM ABI)──► live DOM ► patcher ► element tree
 ──────────────────────────────────────────────────────────────────────────────
 UltraCanvasWasmHost   wasmtime behind an UltraCanvas API: modules, instances,
                       memory and CPU limits, imports and exports
 UltraNet (fetch, cookies) · HTMLReader · CSSLayout · UltraCanvas elements
```

The property that makes this one design rather than three is that there is
**one runtime and one sandbox, with two ABIs that both end in UltraCanvas
elements**:

- The element ABI serves native WebAssembly apps.
- The DOM ABI serves the JavaScript module. It uses the same verbs —
  create, insert, set, listen, bounds — with HTML node kinds (§7.4).

The JavaScript engine is therefore **not a host component but a guest**.
That brings three things:

- It can be upgraded, swapped for another engine, or modified (§7.5)
  without rebuilding UltraWeb.
- It runs under the same memory and CPU limits as any app.
- A bug in it is contained by the sandbox.

---

## 4. The WebAssembly application host (Phase 1)

### 4.1 Runtime

Both candidates were measured on QuickJS running React and Angular (§13):

| | wasmtime 49 | WAMR 2.4.5 |
|---|---|---|
| Time against the same C compiled natively (QuickJS running the three apps, geometric mean) | **1.17–1.25×** (Cranelift) | 1.6–1.9× fast JIT (x86-64 only); 13–15× interpreter |
| C++ exceptions (wasi-sdk 34, `-fwasm-exceptions`) | **runs** (standard exception handling) | does not load, in any mode |
| Language, build | Rust; prebuilt C-API libraries per platform | C, CMake |
| Prebuilt C API (v49.0.2) | Linux and macOS (x86-64, arm64), Android (x86-64, arm64), Windows MSVC (x86-64, arm64), Windows GNU MinGW (x86-64). Not the `gnullvm` ABI the Windows CI builds use. | builds from source everywhere, plus FreeBSD and RTOSes |
| Guest → host call: empty / passing a 16-byte string | 7 / 52 ns with the unchecked API; 82 / 162 ns with the checked one | 2.9 / 41 ns fast JIT; 46 / 98 ns interpreter |

**wasmtime first**, behind `UltraCanvasWasmHost`, for two reasons:

- **Speed.**
- **C++ exceptions.** `UltraCanvas/core` has 61 `try` blocks in 27 files.
  A guest SDK built without exceptions would abort wherever the framework
  catches.

The build takes wasmtime's **prebuilt C-API release** through
`FetchContent`, the same way the Vectorizer plugin already fetches its Rust
crate, so no Rust toolchain is needed where a prebuilt library exists. The
library is Apache-2.0-WITH-LLVM-exception. The Windows CI builds use MSYS2
CLANG64/CLANGARM64 (the `gnullvm` ABI), for which there is no prebuilt
library. There, wasmtime's `c-api` crate has to be built with Corrosion, as
the Vectorizer builds its crate for that ABI (§14, question 2).

**WAMR stays the fallback** for targets wasmtime does not reach (ULTRA OS on
small devices, BSD); that is what the wrapper is for.

### 4.2 The element ABI and the guest SDK

A guest talks to elements the host owns, through a small, generic,
handle-based tree protocol. It does not get one import per element method:

```c
// imports, module "ultracanvas"; strings cross as (pointer, length) into guest memory
uint32_t uc_create(const char* kind, uint32_t kindLen);   // "Container", "Label", "Button", "TextInput", "Checkbox" → handle
void     uc_release(uint32_t handle);
int32_t  uc_insert(uint32_t parent, uint32_t child, uint32_t before);   // before 0 = append
int32_t  uc_remove(uint32_t child);
int32_t  uc_set_text(uint32_t handle, uint32_t prop, const char* value, uint32_t len);
int32_t  uc_set_number(uint32_t handle, uint32_t prop, double value);
int32_t  uc_get_text(uint32_t handle, uint32_t prop, char* out, uint32_t cap);   // returns the full length
double   uc_get_number(uint32_t handle, uint32_t prop);
int32_t  uc_listen(uint32_t handle, uint32_t eventMask);
int32_t  uc_bounds(uint32_t handle, float* outXYWH);   // as of the last layout pass; a synchronous flush is Phase 4 (§7.3)
// exports the guest provides
void     uc_main(void);                                // build the UI, then return
void     uc_event(uint32_t handle, uint32_t event, int32_t detail);   // host → guest, UI thread
```

- **Why handles and generic setters**, rather than one import per
  element method:
  - The host keeps ownership, so a guest cannot corrupt an element.
  - An element joins the ABI by adding its kind and properties to one
    table.
  - The same verbs are what DOM bindings need (§7.4), so Phase 5 adds
    node kinds, not a second protocol.
- **Property, kind and event ids.** v1 keeps them in one header that the
  host and every guest include, `UltraWeb/guest/ultraweb.h`, so the two
  sides cannot disagree. Later the table is generated from the element
  headers, and the generator also emits C++ proxies named like the
  framework classes (`UltraCanvasButton`, `SetText`, `onClick`). An app
  written against the catalogue then compiles for the desktop and for
  UltraWeb from one source, for the elements the table covers. The ABI is
  versioned, and a module declares the version it was built for.
- **Cost.** A guest → host call through wasmtime's unchecked API costs
  7 ns, or 52 ns when it carries a string (§13.4). React creating 1,000
  rows makes about 30,000 DOM calls and Angular about 44,000. That is
  0.2–2.3 ms of crossing beside 0.5 s of script. So v1 has no batching
  layer, and the host registers its imports with the unchecked API: the
  checked one costs 10× more per call.
- **Custom drawing** (later): a `Canvas` kind takes a buffer of drawing
  commands mapped onto `IRenderContext`. Guests can then draw charts and
  games without bundling Cairo.
- **Not chosen for v1: the full framework inside the guest.** In this
  model the guest contains UltraCanvas, Cairo and Pango and presents
  pixels, as OS/WASM does in a browser. It would reuse everything, but:
  - every app ships its own multi-megabyte Cairo, Pango and fonts;
  - text renders without the host's theme, IME or accessibility;
  - every tab duplicates the framework in memory.

  It stays a later option for porting existing apps unchanged.

### 4.3 Host services, loading, limits

- **Event loop.** Guest code runs on the UI thread, as page script does in
  every browser. The host calls the exported `uc_main` once, then
  `uc_event` for events and timers. Completions from other threads (fetch)
  come back through `PostToUIThread`.
- **Imports beyond elements:**
  - `uc_fetch`: UltraNet, same-origin and CORS rules, TLS verification on.
  - Timers.
  - Text clipboard, gated by a user gesture.
  - Per-origin key-value storage in the profile.
  - A console that goes to UltraWeb's log.

  WASI preview 1 provides clocks, randomness and stdout/stderr only.
  There are **no preopened directories**, so a guest has no file access.
- **Loading.** A URL answers either `application/wasm`, or an HTML page
  carrying `<link rel="ultraweb-app" href="app.json">`. The second form
  lets a site serve a normal page to other browsers and the native app to
  UltraWeb.
  - The manifest gives the name, version, entry module, ABI version,
    requested permissions and a content hash.
  - Multi-file apps travel as `.ucpkg` (Zstd sections, VirtualFS).
  - Compiled code is cached by hash (wasmtime serialised modules), so a
    revisit skips compilation.
- **Limits:**
  - a memory cap per instance;
  - wasmtime epoch interruption for code that does not return to the host
    (an infinite loop in `uc_event`);
  - fuel for tests.

  A guest that traps or exceeds a limit is torn down, and its tab shows
  the error.

### 4.4 Building a guest

```bash
SYSROOT=<wasi-sdk>/share/wasi-sysroot
<wasi-sdk>/bin/clang++ --target=wasm32-wasip1 --sysroot=$SYSROOT -O2 \
    -fwasm-exceptions -mllvm -wasm-use-legacy-eh=false -L$SYSROOT/lib/wasm32-wasip1/eh -lunwind \
    -mexec-model=reactor -I UltraWeb/guest app.cpp -o app.wasm
```

- `-mexec-model=reactor` builds a *reactor*: a module with no `main`.
  The host calls its exports, and its state stays alive between calls.
- The exception flags are the ones verified to run on wasmtime (§13.5).
  A guest that never throws can drop them.

---

## 5. The HTML reader (Phase 2)

### 5.1 `HTML::PageLoader`

```cpp
namespace UltraCanvas::HTML {

struct FetchResult {
    int status = 0;
    std::string finalUrl;          // after redirects; becomes the base URL
    std::string contentType;       // "text/html; charset=..."
    std::vector<uint8_t> body;
    std::string error;             // empty on success
};

// Injected so the loader has no UltraNet dependency and tests run offline.
using Fetcher = std::function<void(const std::string& url,
                                   std::function<void(FetchResult)> done)>;

struct LoadedPage {
    std::string url;               // final URL
    std::string baseUrl;           // url, or <base href> resolved against it
    Document document;             // parsed, linked stylesheets inlined
    std::vector<std::string> warnings;
};

class PageLoader {
public:
    explicit PageLoader(Fetcher fetcher);
    int Load(const std::string& url, std::function<void(LoadedPage)> onReady);   // returns a navigation id
    void Cancel(int navigationId);
    // For BuildOptions::resourceLoader: resolved against the page base,
    // answered from the cache, empty (and queued for fetch) on a miss.
    std::vector<uint8_t> GetResource(const std::string& baseUrl, const std::string& href);
    std::function<void()> onResourcesArrived;
};

}
```

What it adds that nothing has today:

- **URL resolution:** RFC 3986 §5 relative references, as
  `UltraNet_ResolveUrl(base, reference)` next to `UltraNet_ParseUrl`.
- **Charset detection,** in this order:
  1. a byte-order mark;
  2. the HTTP `Content-Type` charset;
  3. `<meta charset>` in the first 1024 bytes;
  4. UTF-8, falling back to Windows-1252.

  UltraNetMime's iconv conversion becomes a public
  `UltraNet_ConvertToUtf8(bytes, charset)` rather than being written a
  second time.
- **Stylesheets:**
  - linked stylesheets are fetched and inlined in document order;
  - `@import` is followed to a depth of 4;
  - `url()` resolves against the stylesheet's own URL.
- **Cache:** an in-memory LRU of subresources, 64 MB, honouring
  `Cache-Control: no-store`.
- **Content types:**
  - `text/html` and `application/xhtml+xml` are pages;
  - `text/plain` is wrapped in `<pre>`;
  - an image becomes a one-image page;
  - `application/wasm` is a WebAssembly app (§4);
  - anything else is a download.
- **`file://`** goes through `OpenFileUtf8`/`PathFromUtf8`, per the UTF-8
  path rule.

### 5.2 `UltraCanvasWebView`

A container element that owns one page:

- `Navigate`, `Reload`, `Stop` and back/forward, with the scroll offset
  restored.
- `#fragment` jumps, and zoom.
- A rebuild on width changes that cross a breakpoint a loaded stylesheet
  actually uses, debounced to 200 ms.
- Error pages built from a built-in template.
- Callbacks for URL, title, load state, hovered link, external schemes and
  downloads.

The web view is the one place link activation is interpreted, so UltraMail
and the eBook viewer can later delegate to it.

### 5.3 The browser frame

- Toolbar: back, forward, reload/stop, an address bar
  (`UltraCanvasTextInput`), and a menu. Text that is not a URL goes to a
  search URL template.
- Tabs on `UltraCanvasTabbedContainer`.
- A status bar.
- Keyboard: Ctrl+L, Ctrl+T, Ctrl+W, Ctrl+R / F5, Alt+Left / Alt+Right,
  Ctrl+F, and Ctrl+plus / minus / 0.
- Downloads through `UltraNet_HttpDownloadFile`.
- History and bookmarks as JSON (`UltraCanvasJSON`) in the settings
  directory.

Phase 1 ships the window, the address bar and the status bar first.

---

## 6. Gaps in the HTMLReader, and what to do about each

Ordered by how much of the web each one costs today. Checked against the
code on 2026-10-06.

### 6.1 Parser

The parser is tolerant but not spec-conformant. Real sites rely on HTML5
tree construction for:

- misnested formatting (`<b><p>x</b></p>`);
- an implied `<tbody>`;
- `<table>` foster-parenting;
- `<template>`.

Plan: add the cheap, high-yield rules — implied `tbody`/`tr`, auto-closing
`<p>`, the adoption-agency case for `<a>`/`<b>`/`<i>` — and check them
against fixtures. A vendored HTML5 parser (lexbor, gumbo) behind
`HTML::Parser` is the alternative if the fixtures demand it.

`<noscript>` is rendered until Phase 5 brings a script engine; `<script>`
and `<template>` are hidden; `<iframe>` becomes a link box.

### 6.2 Layout properties (the biggest gap for modern pages)

- **Flex and grid render as blocks.** `HTMLStyleResolver.cpp:842` maps
  `display: flex` and `display: grid` to `Block`, and `inline-flex` /
  `inline-grid` to `InlineBlock` (:849-850).
- **Positioning is not parsed:** `position`, `top`/`left` and `z-index`.
  The builder uses absolute positioning only for background images.
- **Floats work** (added since the first draft).

The CSSLayout engine already implements flex (grow, shrink, basis, wrap,
gap, justify, align), a subset of grid, and absolute, fixed and relative
positioning. So this is mapping work, not new layout code:

| CSS | Maps to |
|---|---|
| `display: flex / inline-flex`, `flex-*`, `justify-content`, `align-*`, `gap`, `order` | `layout.SetFlexRow/Column`, `layoutItem.SetFlexGrow/Shrink/Basis/AlignSelf` |
| `display: grid`, `grid-template-*` (px, %, `fr`, `repeat()`, `minmax()`), `grid-column/row` | CSSLayout grid (no named areas, dense packing or subgrid yet) |
| `position: absolute / relative / fixed / sticky`, `top/right/bottom/left`, `z-index` | absolute layout; `fixed` relative to the view; `sticky` as `relative` at first |
| `vw`, `vh`, `vmin`, `vmax`, `calc()`, `min()`, `max()`, `clamp()` | resolver length evaluation with the viewport size |
| `overflow`, `visibility`, `opacity`, `box-shadow`, `text-transform`, `text-overflow` | element properties that exist or are small |

Flex and grid items must become direct children of the flex or grid
container (with anonymous items for loose text). That is the one
structural change to the builder in this phase.

### 6.3 Selectors

| Status | Selectors |
|---|---|
| Supported | `:first-child`, `:last-child`, `:only-child`, `:first-of-type`, `:nth-child()`, `:nth-last-child()`, `:nth-of-type()`, `:empty`, `:root`, attribute selectors |
| Missing | `:not()`, `:is()`, `:where()`, `:has()` |
| Dropped | `:hover`, `:focus`, `:active` (a static render is never hovered) |
| Approximated | the child combinator `>`, treated as a descendant combinator (`CSSStyleSheet.h:94`) |

Live pages need real `>` matching and `:hover`/`:focus`, re-resolving only
the affected subtree when the pointer or focus changes.

### 6.4 Media

- SVG images and inline SVG through the SVG plugin.
- `srcset` / `<picture>`.
- Animated GIF/WebP through `UCImageAnimationController`.
- `<video>` / `<audio>` show a poster plus a link to UltraViewer.
- `@font-face` is ignored until a later phase.

### 6.5 Forms

`ElementBuilder::BuildFormControl` draws controls for display only. A
browser needs the framework's real elements (`UltraCanvasTextInput`,
`UltraCanvasButton`, `UltraCanvasDropdown`, `UltraCanvasCheckbox`,
`UltraCanvasRadio`), so that caret, selection, clipboard, IME and focus
work.

A `FormController` per `<form>` posts
`application/x-www-form-urlencoded` or multipart data through the page
loader. The display-only path stays for UltraMail, behind a `BuildOptions`
flag.

### 6.6 Performance

The whole tree is rebuilt on every load, every image batch and every
breakpoint change. That is fine for articles. What would make a long front
page fast, in order:

1. coalesce rebuilds;
2. patch images in place;
3. build lazily below the fold.

Style resolution is O(elements × rules), with no rule index. Measured with
the parser and resolver compiled on their own (-O2):
- 16,000 elements and 300 rules: 100 ms;
- 64,000 elements and 1,000 rules: 1.4 s.

A rule index (by id, class and tag) is the first fix (§7.3).

---

## 7. JavaScript: React and Angular (Phases 4–5)

### 7.1 What was verified

React 19.3, Angular 22.2 zoneless and Angular 22.2 with zone.js ran as
unmodified production builds on QuickJS-ng 0.17 (§13). They ran natively,
and compiled to `wasm32-wasip1` under wasmtime, WAMR's fast JIT and WAMR's
interpreter. Every run passed every DOM check. Neither the engine nor the
frameworks needed a patch. The frameworks needed only what a browser
provides: a DOM, timers and an event loop.

### 7.2 The engine: QuickJS-ng as a guest module

- **Language:** QuickJS-ng covers the modern language through ES2025.
  Probed directly: iterator helpers, `Set` methods, `Promise.withResolvers`,
  RegExp `v` flag, `Array.fromAsync`, `Float16Array`, explicit resource
  management, `WeakRef`, `FinalizationRegistry`.
- **Licence and size:** MIT, C. It compiles to WebAssembly unchanged:
  `qjs` with its libc and REPL is 1.6 MB as `.wasm`.
- **Not in the engine (host-side by design):** timers, `fetch` / `XMLHttpRequest`, `URL`,
  `TextEncoder`/`TextDecoder`, `structuredClone`, `MessageChannel`, `Intl`
  and `WebAssembly`.
  - The module provides `URL`, `TextEncoder` and `structuredClone` as small
    C or JS shims.
  - The host provides timers, the event loop and fetch through imports.
  - `Intl` is the one sizeable gap: it means ICU on the host side, or
    ICU4X inside the module. Angular's own pipes carry their locale data,
    but `toLocaleString` and much third-party code use `Intl`.
- **Feature probes:** React looks up `MessageChannel` (its scheduler
  prefers it to `setTimeout`), `setImmediate`, `reportError`, and the
  `AnimationEvent`, `TransitionEvent` and `CompositionEvent` constructors.
  zone.js and Angular look up their own list (§13.3).

### 7.3 The native work — the long pole

Checked against the code on 2026-10-06:

| Capability | Today | What it takes |
|---|---|---|
| **Live, mutable DOM** | PARTIAL. `Node` has `children` (`shared_ptr`) and a raw `parent` (`HTMLDocument.h:37-43`). The only mutator is `SetAttribute`. Nothing notifies of changes. Whitespace-only text nodes are dropped. | Insert, remove and replace; mutation records; whitespace text nodes; `querySelector` (reusing `SelectorMatches`). Rough estimate: 2–3 engineer-weeks. |
| **Incremental DOM → element patching** | MISSING. `ElementBuilder` is one-shot: `BuildResult` keeps only an anchors map (`HTMLElementBuilder.h:73-82`), and callers discard the builder. Inline runs are flattened into one Pango-markup `UltraCanvasLabel`. Margins add spacer containers, inline boxes add "line" rows, and floats wrap the siblings that follow. All of these depend on neighbours. | A per-page builder and resolver kept alive, a node → element map, a dirty-block scheduler, insert-at-index on `UltraCanvasContainer` (append only today), a rule index. Most likely **a box tree between the DOM and the widgets**: a redesign of `HTMLElementBuilder`, not an extension. Rough estimate: 8–12 weeks, plus 4–6 weeks for flex, grid and position (§6.2). |
| **Synchronous layout queries** (`getBoundingClientRect`, `offsetWidth`) | PARTIAL. `Measure` and `Arrange` are public and `GetBoundsInWindow` exists. But `Element::Arrange` (`Element.cpp:282`) re-arranges every child with no "unchanged" short-circuit. `UltraCanvasUIElement::Arrange` adds damage on every pass (`UltraCanvasUIElement.cpp:361-386`). `InvalidateLayout` always bubbles to the root. Inline elements have no box of their own. | `FlushLayout()` without painting; skip arranging clean subtrees; add damage only when bounds change; rectangles for inline ranges from Pango. Rough estimate: 2–4 weeks. |
| **DOM events** | PARTIAL. `FindElementAtPoint` returns the deepest element. Bubbling exists for keyboard, wheel and drag events, but not for plain mouse events. There is no capture phase and no `preventDefault`. | Capture/target/bubble along `Node::parent`, built on an element → node map; a microtask checkpoint after every task. |
| **Bindings** | — | WebIDL-generated QuickJS bindings, in the module, calling the DOM ABI. The measured surface comes first (§13.3). Rough estimate: 10–16 weeks with timers, fetch, history, storage and events. |

The effort figures are estimates from reading the code, not measurements.

The **biggest risk** is the second row. React and Angular change a few
nodes at a time and expect a frame within 16 ms. Today one changed text
node costs four things:
- re-resolving the block's style against every rule;
- reshaping the block's Pango markup;
- a full arrange;
- a full repaint.

The box tree is what removes that cost. It is also what gives inline
elements boxes for events and for `getClientRects`.

### 7.4 Where the DOM lives: in the host

The DOM belongs in the **host**, as native `HTML::Node`s behind the DOM
ABI, not as a JavaScript DOM inside the module. Two reasons:

- **Speed.** The measurements used a JavaScript DOM (linkedom) inside the
  engine. On QuickJS, 57% of React's create-1,000-rows time and 57–62% of
  Angular's was spent inside that DOM (§13.2). In the host, that work
  becomes 30,000–44,000 calls at 7–52 ns each, plus native tree
  operations.
- **Layout needs it anyway.** Layout, style and hit-testing need the DOM
  natively. Keeping a second copy in the module and streaming mutations
  across (the WorkerDOM design) also makes `getBoundingClientRect`
  asynchronous, which breaks real component libraries.

An in-process sandbox keeps those calls **synchronous**. That is the
advantage of this design over worker-based ones.

### 7.5 Modifying QuickJS: what a fork can and cannot buy

QuickJS-ng is MIT and small enough to own a fork of.

- **What a fork can buy:**
  - **Interpreter speed, up to about 2.5×.** V8 with its JIT disabled is
    2.2–2.6× faster than QuickJS on this workload. That is a realistic
    ceiling for interpreter work: inline caches, faster property access
    and shapes, cheaper calls into host functions.
  - **Bytecode caching:** loading cached bytecode is 13–20× faster than
    compiling (§13.6). Cache it per script hash in the profile.
  - **Built-ins:** `Intl` (through ICU4X), `structuredClone`, and fast
    paths for DOM wrapper objects (QuickJS already has class ids and
    opaque pointers).
  - **Memory:** a memory limit enforced inside the engine as well as by
    the runtime. GC tuning only if real apps show pauses: React's slow
    10,000-row case was not the cycle collector (§13.1).
- **What it cannot buy: a JIT inside the sandbox.**
  - WebAssembly code cannot generate and run machine code. A guest JIT
    would have to emit WebAssembly and ask the host to compile it, which
    is a research project.
  - So the 4–5× between V8's interpreter and V8 with its JIT is out of
    reach on this path. A system web view (§8) is the answer for the few
    sites that need that speed.
- **Cost of a fork:** tracking upstream. Keep patches small and send them
  upstream.

### 7.6 What real React and Angular apps need beyond the benchmark

- Routers: the history API, `popstate`, `location`.
- `fetch` / `XMLHttpRequest`, `localStorage`.
- `getBoundingClientRect`, `ResizeObserver`, `IntersectionObserver`,
  `getComputedStyle` and `MutationObserver` (component libraries such as
  Angular CDK and MUI).
- Focus management, CSS transition and animation events, `matchMedia`,
  `Intl`.
- Flex and grid in the CSS they ship (§6.2).

---

## 8. Alternative considered: wrap a system web view

WebKitGTK, WebView2 and WKWebView give full compatibility in weeks. It was
rejected as the primary path for three reasons:

- The page is an opaque native surface: no UltraCanvas elements, no reuse
  in UltraMail or the eBook viewer, no ULTRA OS or WebAssembly target.
- It means three engines with three behaviours and three dependency
  stories.
- It builds nothing the framework keeps.

It remains a sensible later fallback behind the same tab API: "open in
full engine" for the pages the native path cannot show.

---

## 9. Security and privacy

- **Sandbox.**
  - A guest can reach only the imports the host gives it. WASI has no
    preopened directories, so there is no file access.
  - Memory is capped per instance, and code that never returns is
    interrupted.
  - Permissions (clipboard, storage size, network beyond the app's
    origin) are granted per origin and shown in the UI.
- **TLS verification** is on, always. A TLS failure shows an error page
  with no "proceed anyway" in Phase 1–2.
- **Hostile input.** The parser, CSS parser, image decoders and
  WebAssembly validation all take hostile input. Fuzz targets for
  `HTML::Parser` and `CSSStyleSheet` are part of Phase 2. wasmtime
  validates every module before compiling it.
- **Resource limits:**
  - documents up to 16 MB;
  - a cap on subresources per page;
  - `@import` depth 4;
  - 10 redirects;
  - an element-count cap.
- **Mixed content:** an `https` page or app does not load `http`
  subresources.
- **Schemes:** `http`, `https`, `file` (address bar only), `data:` for
  images, and `about:`. Anything else goes to the operating system after
  confirmation.
- **Cookies** are per profile and first-party by default. They are off
  until forms or scripts need them.
- **Downloads** never auto-open, and their names are sanitised.

---

## 10. Phases

### Phase 1 — WebAssembly app host

- `UltraCanvasWasmHost`: wasmtime through its C API, module validation,
  instances, memory limits, epoch interruption, traps reported as errors.
- Element ABI v1: Container, Label, Button, TextInput, Checkbox; text and
  number properties; click, change and toggle events; `uc_bounds`.
- Guest header `UltraWeb/guest/ultraweb.h` and a sample guest.
- `Apps/UltraWeb`:
  - an address bar that opens `.wasm` from a file path, `file://` or
    `https://`;
  - a status bar;
  - an error view;
  - the version in the title.
- Then: `uc_fetch`, timers, storage, clipboard, manifests, compiled-module
  cache, `.ucpkg`, a permissions UI, C++ proxies generated from the
  element headers.

**Done when:** a C++ app built with wasi-sdk against the guest SDK (a form,
a list of 10,000 rows, a fetch from its origin) loads from an `https` URL
and runs on Linux, Windows and macOS, and a trapping or looping guest
cannot hang or crash UltraWeb.

### Phase 2 — HTML reader browser

- `UltraNet_ResolveUrl` and `UltraNet_ConvertToUtf8`.
- `HTML::PageLoader`.
- `UltraCanvasWebView`: history, fragments, error pages.
- `<noscript>` rendered.
- Tabs, downloads, history and bookmarks.
- Fuzz targets.

**Done when:** Wikipedia articles, the repository's `Docs/` read from
`file://`, blogs and documentation sites render readably, and links,
images and back/forward work on all three desktop platforms.

### Phase 3 — modern layout

- Flex, grid, position, viewport units and `calc()` mapped onto CSSLayout
  (§6.2).
- The missing selectors, a real `>`, and `:hover` / `:focus` (§6.3).
- SVG and `srcset`.
- HTML5 parser rules.
- A rule index in the style resolver.

**Done when:** reference pages built on flex and grid lay out recognisably
close to a mainstream browser, checked with screenshot tests.

### Phase 4 — live DOM

The §7.3 work:
- a mutation API and mutation records;
- the box tree and incremental patching;
- `FlushLayout` and layout queries;
- DOM events with capture and bubble;
- per-subtree style invalidation.

It is exercised from C++ tests and from Phase 1 guests through the DOM ABI
before any JavaScript exists.

**Done when:** a page mutated node by node from a test reaches the same
element tree as a fresh build of the final DOM, and a single text change
on a 10,000-node page re-lays and repaints within a 16 ms frame.

### Phase 5 — JavaScript: React and Angular

- QuickJS-ng as a guest module.
- WebIDL bindings over the DOM ABI.
- Timers and a microtask checkpoint.
- `fetch` / `XMLHttpRequest`, the history API, `localStorage`, `URL`,
  `TextEncoder`, `structuredClone`, `MessageChannel`.
- `Intl` (§7.2).
- A bytecode cache.
- The interpreter work from §7.5 if the measurements of real apps call
  for it.

**Done when:** these apps work in UltraWeb:
- the React and Angular TodoMVC and RealWorld ("Conduit") apps;
- the js-framework-benchmark table of §13, against the native DOM;
- an Angular Material and a MUI demo page.

### Optional

A system web view fallback (§8), decided on evidence of which sites users
actually need.

---

## 11. Testing

- **Offline by construction.** The page loader takes a `Fetcher`, and
  guests take their imports from the host, so tests pass fixtures and
  never touch the network.
- **Host:**
  - load, instantiate and trap handling;
  - memory limit and epoch interruption;
  - every ABI call with bad handles and out-of-range pointers, which must
    return an error and must not crash.

  Guest modules are written as WAT text inside the tests, so CI needs no
  wasi-sdk.
- **Unit tests:** URL resolution (the RFC 3986 §5.4 examples), charset
  order, stylesheet order, history and fragments, cancellation.
- **Layout:** the `Tests/HTMLTableLayoutTest.cpp` pattern, extended to flex,
  grid and position.
- **Live DOM:** "mutate step by step" against "build fresh", compared
  element by element.
- **JavaScript:** the harness in [`Feasibility/`](Feasibility/README.md)
  becomes a regression suite once the native DOM replaces linkedom.
- **Fuzzing:** the parser, the CSS parser and the ABI entry points.
- **Regression for the other consumers:** the eBook and UltraMail tests
  stay green after every HTMLReader change.

---

## 12. Risks

| Risk | Mitigation |
|---|---|
| Incremental patching cannot reach frame budget on the current builder | Plan the box tree from the start of Phase 4 (§7.3); measure with the live-DOM tests before bindings exist. |
| Users expect a full browser and meet script-only sites before Phase 5 | Say so in the UI: a "this page needs JavaScript" notice when `<noscript>` or an empty body plus scripts is seen, with "open in system browser". |
| QuickJS is too slow for heavy apps | Measured: fine for interactions, slow for 1,000+-row first renders (§13). Bytecode cache, interpreter work (§7.5), and the system web view for the rest. |
| wasmtime unavailable on a target (BSD, small ULTRA OS devices) | `UltraCanvasWasmHost` keeps the runtime swappable; WAMR is the measured fallback (C++ exceptions not yet). |
| Flex/grid mapping changes how existing mail and eBooks render | Gate it behind `ResolverOptions` until the eBook and UltraMail suites are reviewed. |
| ABI churn breaks published apps | Versioned ABI; a module declares its version; the host keeps the previous version's table. |
| Hostile input | Validation, resource limits, fuzzing, the sandbox. |

---

## 13. Feasibility measurements

Measured on 2026-10-06 with the harness in
[`Feasibility/`](Feasibility/README.md), on one core of a 4-core x86-64
cloud container. The full tables are in
[`Feasibility/results.md`](Feasibility/results.md). The DOM in every run
is linkedom, a JavaScript DOM, and no layout or paint is included (see the
caveats there).

### 13.1 Script time per operation (median ms)

| App | Operation | V8 JIT | V8 no JIT | QuickJS native | QuickJS-in-WASM wasmtime | … WAMR fast JIT | … WAMR interp. |
|---|---|---:|---:|---:|---:|---:|---:|
| React 19.3 | create1k | 50.2 | 214 | 526 | 721 | 1348 | 8225 |
|  | update10th | 2.3 | 8.6 | 28.2 | 33.0 | 46.1 | 417 |
|  | select | 0.7 | 3.2 | 9.3 | 11.2 | 14.5 | 127 |
|  | swap | 3.1 | 42.9 | 76.0 | 106 | 137 | 1382 |
|  | remove | 1.0 | 3.4 | 9.4 | 10.3 | 13.6 | 127 |
|  | create10k | 745 | 5729 | 14003 | 16795 | 23259 | 195293 |
| | **slowdown vs V8 JIT** | 1.0× | 3.8× | 8.4× | 9.8× | 13.8× | 112.0× |
| Angular 22.2 zoneless | create1k | 41.7 | 164 | 382 | 548 | 914 | 6250 |
|  | update10th | 0.5 | 4.3 | 8.6 | 9.7 | 14.0 | 142 |
|  | select | 0.4 | 4.2 | 8.1 | 9.8 | 13.3 | 140 |
|  | swap | 0.3 | 4.2 | 8.1 | 9.5 | 13.8 | 144 |
|  | remove | 0.4 | 4.4 | 9.1 | 10.4 | 16.8 | 161 |
|  | create10k | 449 | 1621 | 4344 | 6065 | 9774 | 59716 |
| | **slowdown vs V8 JIT** | 1.0× | 4.9× | 12.4× | 14.8× | 23.7× | 175.0× |
| Angular 22.2 zone.js | create1k | 40.2 | 189 | 417 | 617 | 995 | 6417 |
|  | update10th | 0.6 | 4.2 | 9.1 | 10.9 | 14.1 | 146 |
|  | select | 0.4 | 3.8 | 8.3 | 9.7 | 13.4 | 144 |
|  | swap | 0.4 | 3.9 | 8.9 | 9.9 | 13.9 | 152 |
|  | remove | 0.4 | 4.3 | 9.5 | 11.1 | 16.6 | 187 |
|  | create10k | 498 | 1918 | 4855 | 6471 | 10461 | 66830 |
| | **slowdown vs V8 JIT** | 1.0× | 4.5× | 11.5× | 14.4× | 22.3× | 175.5× |

Every run of every app on every engine passed every DOM check: row
counts, labels, the selected class and the swapped ids.

Reading the table:
- **Small updates are interactive.** Select, swap, remove and update cost
  milliseconds on QuickJS, natively and in the sandbox. React's swap is the
  exception at about 0.1 s.
- **Big first renders are the slow case:** 1,000 rows in 0.4–0.7 s, and
  10,000 rows in 4–17 s.
- **The sandbox costs little.** wasmtime adds 17–25% to native QuickJS.
  WAMR's fast JIT adds 64–94%, and its interpreter is 13–15× native.
- **One case grows faster than linearly.** React's 10,000-row render takes
  15× its 1,000-row time on V8 with its JIT, 27× on V8 without it, and
  17–27× on QuickJS, while Angular stays at 10–12×.
  - It is not QuickJS's cycle collector: a QuickJS built with cycle
    collection disabled after the first pass was 7% faster on that
    operation, not 3×.
  - The pattern is React's on every engine. It is most likely its
    reconciliation of a 10,000-child list against the test DOM, to be
    profiled against the native DOM.

### 13.2 How much of it is the JavaScript test DOM

| App | Engine | create 1,000 rows: total / inside the DOM | create 10,000 rows: total / inside the DOM | DOM calls for 1,000 rows |
|---|---|---|---|---:|
| React 19.3 | V8 JIT | 99.4 / 66.3 ms (67%) | 790 / 471 ms (60%) | 30,025 |
| React 19.3 | QuickJS native | 565 / 323 ms (57%) | 14116 / 3485 ms (25%) | 30,025 |
| Angular 22.2 zoneless | V8 JIT | 97.6 / 60.2 ms (62%) | 676 / 509 ms (75%) | 44,002 |
| Angular 22.2 zoneless | QuickJS native | 453 / 280 ms (62%) | 5443 / 3390 ms (62%) | 44,002 |
| Angular 22.2 zone.js | V8 JIT | 107 / 64.9 ms (61%) | 584 / 421 ms (72%) | 44,002 |
| Angular 22.2 zone.js | QuickJS native | 458 / 264 ms (57%) | 5574 / 3431 ms (62%) | 44,002 |

The recording runs wrap every DOM member and time only the calls
framework code makes, not linkedom's calls into itself, and not the
listeners `dispatchEvent` runs. More than half of the time to create rows
is the test DOM. With the DOM native, that share becomes host calls
(§13.4) plus C++ tree work.

### 13.3 Browser API surface the frameworks used

Recorded per call while each app ran the whole workload. Full list with
counts: [`Feasibility/results.md`](Feasibility/results.md).

| | React 19.3 | Angular 22.2 (zoneless and zone.js) |
|---|---|---|
| Tree | `createElement`, `appendChild`, `insertBefore`, `removeChild`, `firstChild`, `lastChild`, `textContent =` | `createElement`, `createTextNode`, `createComment`, `appendChild`, `insertBefore`, `remove()`, `nodeValue =` |
| Attributes | `setAttribute`, `className`, `style`, `nodeName`, `namespaceURI`, `tagName` | `setAttribute`, `className =`, `classList`, `getAttributeNode`, `tagName` |
| Events | `addEventListener` on the root only (delegation), `onclick =` on every element (a Safari workaround), `window.event` | `addEventListener` / `removeEventListener` on every element |
| Document | `body`, `defaultView`, `getRootNode`, `contentEditable` | `querySelector` (finding `<app-root>`), `documentElement`, `head`, `body`, `getRootNode`, `Attr.value =` |
| Timers | `setTimeout`, `clearTimeout`, `requestAnimationFrame` | the same, plus `setInterval`, `cancelAnimationFrame` (zone.js patches all of them) |
| Feature probes | `MessageChannel`, `setImmediate`, `reportError`, `AnimationEvent`, `TransitionEvent`, `CompositionEvent` | `$localize`, `Zone`; zone.js also looks at `MutationObserver`, `IntersectionObserver`, `FileReader` and `WorkerGlobalScope` to decide what to patch |

The full list also has the test driver's own calls (`getElementById`,
`querySelector`/`querySelectorAll`, reading `textContent` and `className`,
`dispatchEvent`, the `Event` constructor). They are left out of this table.
Without them, the two frameworks need **about 35 DOM members** to run the
benchmark. That is the starting point for the WebIDL binding list; real apps
add the members listed in §7.6.

### 13.4 Guest → host call cost

| Runtime and host-function API | Empty call | Call passing a 16-byte string |
|---|---:|---:|
| wasmtime, `wasmtime_linker_define_func_unchecked` | 7.3 ns | 51.9 ns |
| wasmtime, `wasmtime_linker_define_func` | 82.2 ns | 161.5 ns |
| WAMR fast JIT, native library | 2.9 ns | 40.9 ns |
| WAMR interpreter, native library | 45.6 ns | 97.9 ns |

The guest calls the import ten million times in a loop (`Feasibility/hostcall/`).
The string variant copies the bytes out of guest memory, as `uc_set_text`
does. wasmtime's checked API converts every argument to a `wasmtime_val_t`
and back. The unchecked one hands over the raw slots, which is why the
element bridge uses it.

### 13.5 C++ exceptions in a guest

A C++ program using `std::stoi` (which throws) and its own
`throw std::runtime_error` was built with wasi-sdk 34
(`-fwasm-exceptions -mllvm -wasm-use-legacy-eh=false`, linked against the
`eh` sysroot's `libunwind`):

- **wasmtime 49** (`-W exceptions=y`) caught both exceptions.
- **WAMR 2.4.5** refused to load the module in fast-JIT,
  fast-interpreter and classic-interpreter builds, the last with
  `WAMR_BUILD_EXCE_HANDLING=1`.
- The legacy encoding (clang's default) loads in neither runtime.

### 13.6 Parsing and the bytecode cache

| Bundle | Size | QuickJS native: compile / load bytecode | QuickJS-in-WASM (wasmtime): compile / load bytecode |
|---|---:|---:|---:|
| `react.js` | 221 KB | 22.0 / 1.3 ms (17×) | 32.2 / 1.6 ms (20×) |
| `ng-zoneless.js` | 110 KB | 12.9 / 1.0 ms (13×) | 20.6 / 1.2 ms (17×) |
| `typescript.js` | 8,930 KB | 404 / 28.4 ms (14×) | 575 / 34.7 ms (17×) |

The bundles compile at 5–22 MB/s, and minified code is slower per byte.
A 2 MB single-page app therefore costs 0.1–0.4 s to compile on its first
visit, and a tenth of that from a bytecode cache.

---

## 14. Open questions

1. Where does the guest SDK live? `UltraWeb/guest/` as a top-level module
   next to `UltraNet/` is the proposal.
2. Is wasmtime's prebuilt C-API library acceptable on every CI platform,
   or must it be built from source with Corrosion, as the Vectorizer
   plugin builds its crate?
3. Does `Intl` come from ICU in the host or ICU4X in the module?
4. What are the default search provider and the ULTRA OS start page?
5. Should the Emscripten build (`OS/WASM`) implement the element ABI in
   ordinary browsers, so UltraWeb apps also run on the open web?
6. Profile storage: plain files in the settings directory, or UltraVault
   for cookies and app storage?

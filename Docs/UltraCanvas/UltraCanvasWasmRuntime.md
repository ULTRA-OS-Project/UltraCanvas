# UltraCanvas Runtime for Browsers

**Status:** Prototype, working in Chromium. Firefox not yet tried.
**Last Modified:** 2026-11-10
**Author:** UltraCanvas Framework / ULTRA OS
**Reads with:** [`UltraCanvasWebAssembly.md`](UltraCanvasWebAssembly.md)
(UltraCanvas as an Emscripten page, the platform this builds on),
[`UltraCanvas/OS/WASM/README.md`](../../UltraCanvas/OS/WASM/README.md) (the
sysroot and the flags), [`Docs/UltraWeb/UltraWebInTheBrowser.md`](../UltraWeb/UltraWebInTheBrowser.md)
(the small UltraWeb app interface in the browser).

An UltraCanvas application for the browser is normally one Emscripten
page with the whole framework linked in: cairo, pango, glib, the fonts and
UltraCanvas, several megabytes for every app on every site. The UltraCanvas
runtime splits that in two:

- **The runtime** is UltraCanvas, linked once into an Emscripten *main
  module* that exports the whole C++ API. A browser extension installs it
  once; a site can also serve it itself.
- **An app** is a *side module*: the app's own code only, compiled against
  the UltraCanvas headers with the ordinary C++ API - `CreateWindow`,
  `UltraCanvasButton`, its own `UltraCanvasUIElement` subclasses - and its
  ordinary `main()`. The runtime loads it with `dlopen` and calls that
  `main()`.

## Contents

- [What an app looks like](#what-an-app-looks-like)
- [Building](#building)
- [Trying it](#trying-it)
- [How it works](#how-it-works)
  - [The main module](#the-main-module)
  - [The extension](#the-extension)
  - [The loader](#the-loader)
- [What a visitor downloads](#what-a-visitor-downloads)
- [Tests](#tests)
- [Limits of the prototype](#limits-of-the-prototype)
- [What would make it a product](#what-would-make-it-a-product)

## What an app looks like

Any UltraCanvas `main()`, unchanged:

```cpp
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasWindow.h"

using namespace UltraCanvas;

int main(int, char* argv[]) {
    static UltraCanvasApplication app;   // static: Run() never returns in a browser
    if (!app.Initialize(argv[0])) return 1;
    WindowConfig config;
    config.title = "Hello";
    auto window = CreateWindow(config);
    auto button = CreateButton("Count", 20, 90, 200, 40, "Count");
    button->SetOnClick([]() { /* ... */ });
    window->AddChild(button);
    window->Show();
    app.Run();
    return 0;
}
```

The sample, [`UltraCanvas/OS/WASM/runtime/examples/hello-app.cpp`](../../UltraCanvas/OS/WASM/runtime/examples/hello-app.cpp),
adds a checkbox and an element of its own - a subclass of
`UltraCanvasUIElement` that draws itself and takes clicks - so a factory,
`shared_ptr`, `std::function` callbacks and virtual overrides all cross
from the app into the runtime and back.

## Building

The runtime needs a single-threaded, position-independent wasm sysroot:
Emscripten's dynamic linking requires every object in the main module to be
compiled with `-fPIC`, cairo and pango included.

```bash
source /path/to/emsdk/emsdk_env.sh
WASM_THREADS=0 WASM_PIC=1 UltraCanvas/OS/WASM/build-wasm-sysroot.sh /path/to/wasm-sysroot-pic
export PKG_CONFIG_PATH=/path/to/wasm-sysroot-pic/lib/pkgconfig:/path/to/wasm-sysroot-pic/share/pkgconfig
export PKG_CONFIG_LIBDIR=$PKG_CONFIG_PATH
emcmake cmake -S UltraCanvas/OS/WASM/runtime -B build-ucr -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-ucr
```

From the repository root it is the same with `-DULTRACANVAS_WASM_THREADS=OFF
-DULTRACANVAS_WASM_DYNAMIC=ON`, which compiles the whole build `-fPIC` and
adds `UltraCanvas/OS/WASM/runtime`.

An app of your own is one CMake call, from
[`cmake/UltraCanvasWasmApp.cmake`](../../cmake/UltraCanvasWasmApp.cmake):

```cmake
include(cmake/UltraCanvasWasmApp.cmake)
ultracanvas_wasm_app(my-app main.cpp MyWindow.cpp)   # -> my-app.wasm
```

It compiles the sources with the library's include directories, definitions
and options, and links them as a side module (`-sSIDE_MODULE=1`), not with
the library.

## Trying it

The build directory holds:

| Path | What it is |
|---|---|
| `ultracanvas-extension/` | the extension: in Chrome, *Extensions → Developer mode → Load unpacked*; in Firefox, *about:debugging → Load Temporary Add-on* (pick `manifest.json`) |
| `site/` | a sample site: `index.html` (runtime from the extension, else from `site/runtime/`), `extension-only.html` (needs the extension), the loader and `hello-app.wasm` |

Serve `site/` with any static server (`python3 -m http.server -d build-ucr/site 8080`)
and open `http://localhost:8080/`.

## How it works

```
 page (the site's origin)
 ├── ultracanvas-loader.js        the site's: finds the runtime, fetches the app
 ├── ultracanvas-runtime.js/.wasm  UltraCanvas + cairo + pango + glib, MAIN_MODULE
 │     └── dlopen("/app/app.wasm") the app, a SIDE_MODULE, linked against it
 │           └── main()            the app's own, as on the desktop
 └── <canvas>                      what UltraCanvas draws, as on any Emscripten page
```

### The main module

[`UltraCanvas/OS/WASM/runtime/UltraCanvasWasmRuntime.cpp`](../../UltraCanvas/OS/WASM/runtime/UltraCanvasWasmRuntime.cpp)
is a `main()` that only tells the page it is ready, and
`uc_runtime_start_app(path)`, which `dlopen`s the app, finds its `main()`
(`__main_argc_argv` in WebAssembly) and calls it. The app's `Run()` starts
the browser's main loop, so the call ends in Emscripten's `unwind`, which the
loader expects.

The link (`-sMAIN_MODULE=1`) takes the UltraCanvas archive whole, since an
app may use any of it, and exports every symbol, the C and C++ standard
libraries' included. The C++ exception personality routine is exported by
name: nothing in the main module refers to it, and without it an app that
catches an exception fails to load.

What crosses the module boundary, as checked: with UltraCanvas, the
sample app's factory calls and `shared_ptr`s, `std::function` callbacks
called by the runtime, an `UltraCanvasUIElement` subclass whose `Render`
and `OnEvent` overrides the runtime calls, and a C++ exception thrown in
the runtime's C++ library and caught in the app; on a small library first,
also `dynamic_cast` of a runtime object in the app.

One link detail: glib defines `g_strcmp0`, which pango uses, in its test
utilities, and they refer to `g_io_channel_unix_new`, which glib's browser
build lacks. An ordinary link never notices; a main module exports what it
links, so the runtime defines that one function as a stub that stops with a
message if it is ever called.

### The extension

[`UltraCanvas/OS/WASM/runtime/extension/`](../../UltraCanvas/OS/WASM/runtime/extension/):
a Manifest V3 extension that carries the runtime as web-accessible files and
one content script, which puts the runtime's address and version on
`<html data-ultracanvas-runtime data-ultracanvas-runtime-version>` before
the page's scripts run. The extension never fetches or runs a site's code:
the page loads the runtime from the extension's address and starts its app
itself, which keeps the extension within the stores' rule against remotely
hosted code.

### The loader

[`UltraCanvas/OS/WASM/runtime/site/ultracanvas-loader.js`](../../UltraCanvas/OS/WASM/runtime/site/ultracanvas-loader.js):

```html
<script src="ultracanvas-loader.js" data-app="my-app.wasm" data-runtime="runtime/"
        data-runtime-version="0.9.523"></script>
```

It takes the extension's runtime when it is there - and, with
`data-runtime-version`, only when it is the version the app was built
against - and the site's `data-runtime` copy otherwise. It fetches the app
while the runtime starts, writes it into the virtual file system and calls
`uc_runtime_start_app`. `<html>` then carries
`data-ultracanvas-runtime-source` (`extension` or `site`) and
`data-ultracanvas-state` (`running` or `failed`).

## What a visitor downloads

Measured by the end-to-end test and `gzip -9` (Release, single-threaded,
every bundled font), for the sample app:

| | Once per browser | Each app, each site |
|---|---|---|
| **The runtime with the extension** | 9.9 MB, installed with the extension: `.wasm` 7.6 MB, fonts in `.data` 2.2 MB, `.js` 0.13 MB (31.6 MB unpacked) | **8.5 KB**: the app (27 KB unpacked), plus the 4 KB loader |
| The runtime from the site, no extension | - | 9.9 MB on the first visit to each site, and the app |
| The same app as one ordinary Emscripten page | - | 4.0 MB: `.wasm` 1.8 MB, `.data` 2.2 MB |

The runtime is about two and a half ordinary pages, so the extension pays
for itself from the third UltraCanvas app or site a visitor opens; after
that each app costs its own code only.

The runtime is larger than one page because it keeps and exports
everything: the whole UltraCanvas archive, and with `-sMAIN_MODULE=1` every
symbol of cairo, pango, glib, harfbuzz and the C and C++ libraries too.
Exporting only the UltraCanvas API and what apps need of the standard
library (`-sMAIN_MODULE=2` with a list) is the first thing to measure next.

## Tests

`UltraCanvas/OS/WASM/runtime/test/runtime-test.mjs`, in Chromium through
Playwright:

```bash
node UltraCanvas/OS/WASM/runtime/test/runtime-test.mjs build-ucr
```

- the sample site without the extension: the runtime comes from the site,
  and the app's button, checkbox and own element answer real clicks;
- the extension-only page without the extension: it asks for it;
- both pages with the extension: the runtime comes from it, the app answers
  clicks, and the site sends no runtime.

Branded Google Chrome no longer loads unpacked extensions from the command
line (Chrome 137), so the test runs Playwright's Chromium.

## Limits of the prototype

- **An app is bound to its runtime's build.** The side module refers to the
  library's symbols by name and to its classes' layout as compiled, so an
  app runs on the UltraCanvas version, Emscripten version and flags it was
  built with. `data-runtime-version` makes the loader fall back to the
  site's copy when the extension's differs; a stable binary interface
  across releases does not exist yet.
- **Single-threaded.** Dynamic linking and Emscripten's threads do not
  combine in browsers yet, and a page cannot start Web Workers from an
  extension's address either. The single-threaded build has no libvips
  (UltraCanvasWebAssembly.md).
- **The core only.** The main module carries the UltraCanvas library whole,
  but not the optional plugin libraries (Vector, Models): an app that calls
  into them fails to load with the missing symbol named.
- **One app per page, in the window it creates.** As on any UltraCanvas
  page.
- **Resources.** The runtime carries the bundled fonts; an app's own files
  (images, data) are its own to fetch.
- **Firefox and Safari** are untried.

## What would make it a product

1. **A versioned binary interface**: the runtime's version in the app
   module, checked at load, and a promise of which releases keep it.
2. **The demo application as a side module**, with its resources, as the
   first large app.
3. **Plugins**: the chart, vector and model plugins in the main module, or
   loaded as side modules of their own.
4. **One extension** for UltraCanvas apps and UltraWeb apps
   (Docs/UltraWeb/UltraWebInTheBrowser.md), both on this runtime.
5. **Store packaging**, and a narrower content script.

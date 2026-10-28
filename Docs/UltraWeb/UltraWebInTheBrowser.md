# UltraWeb apps in Chrome and Firefox

**Status:** Prototype, working in Chromium. Firefox not yet tried.
**Last Modified:** 2026-10-27
**Author:** UltraCanvas Framework / ULTRA OS
**Reads with:** [`UltraWeb.md`](UltraWeb.md) (the browser and the element
ABI), [`UltraWebProposal.md`](UltraWebProposal.md) §2.1 and §14 question 5,
[`UltraCanvasWebAssembly.md`](../UltraCanvas/UltraCanvasWebAssembly.md) (the
Emscripten port this builds on).

The question (open question 5 of the proposal): **can an UltraWeb app - the
small `app.wasm` that holds only its own code and asks UltraWeb for its
elements - also run in an ordinary browser, with UltraCanvas installed once
instead of downloaded with every app?**

Yes. The UltraWeb runtime is UltraCanvas compiled with Emscripten, plus
UltraWeb's own element ABI code, unchanged. It runs the same `app.wasm` the
UltraWeb browser runs, on the browser's WebAssembly engine. It comes either
from a browser extension, once, or from the site, for a visitor who does
not have the extension.

## Contents

- [The three ways an app reaches the screen](#the-three-ways-an-app-reaches-the-screen)
- [Trying it](#trying-it)
- [How it works](#how-it-works)
  - [The runtime](#the-runtime)
  - [The browser engine behind WasmHost](#the-browser-engine-behind-wasmhost)
  - [The services](#the-services)
  - [The extension](#the-extension)
  - [The loader](#the-loader)
- [What a visitor downloads](#what-a-visitor-downloads)
- [Tests](#tests)
- [Limits of the prototype](#limits-of-the-prototype)
- [What would make it a product](#what-would-make-it-a-product)

## The three ways an app reaches the screen

| | Where UltraCanvas is | What the site sends | Status |
|---|---|---|---|
| **Emscripten page** (`UltraCanvas/OS/WASM`) | linked into the app | the app with the whole framework, cairo, pango, glib, libvips and fonts | works |
| **Browser + UltraWeb runtime** (this page) | in the runtime: the extension, or the site's copy | the app only (with the extension) | prototype |
| **UltraWeb** | the browser's native code | the app only | works |

The second and third run **the same `app.wasm`**: an app written against
[`UltraWeb/guest/ultraweb.h`](../../UltraWeb/guest/ultraweb.h) runs in
UltraWeb natively and in Chrome with the extension, unchanged.

## Trying it

Build the runtime against a single-threaded wasm sysroot
(`UltraCanvas/OS/WASM/README.md`, `WASM_THREADS=0`):

```bash
source /path/to/emsdk/emsdk_env.sh
export PKG_CONFIG_PATH=/path/to/wasm-sysroot-st/lib/pkgconfig:/path/to/wasm-sysroot-st/share/pkgconfig
export PKG_CONFIG_LIBDIR=$PKG_CONFIG_PATH
emcmake cmake -S Apps/UltraWeb/browser -B build-uwr -G Ninja -DCMAKE_BUILD_TYPE=Release -DULTRACANVAS_WASM_THREADS=OFF
cmake --build build-uwr
```

From the repository root under emcmake it is built too
(`BUILD_ULTRAWEB_RUNTIME`, on by default), in `build-wasm/Apps/UltraWeb/browser`.
The build directory then holds:

| Path | What it is |
|---|---|
| `ultraweb-extension/` | the extension: in Chrome, *Extensions → Developer mode → Load unpacked*; in Firefox, *about:debugging → Load Temporary Add-on* (pick `manifest.json`) |
| `site/` | a sample site: `index.html` (runtime from the extension, else from `site/runtime/`), `extension-only.html` (needs the extension), the loader, `hello.wasm`, `hello.txt` |

Serve `site/` with any static server (`python3 -m http.server -d build-uwr/site 8080`)
and open `http://localhost:8080/`. No cross-origin isolation headers are
needed: the runtime is single-threaded.

The sample app, [`Apps/UltraWeb/browser/examples/hello.c`](../../Apps/UltraWeb/browser/examples/hello.c),
uses every part of the ABI: buttons, a text input, a checkbox, a counter kept
in storage across visits, a timer, a fetch of `hello.txt`, a clipboard copy.
It is freestanding C (no libc), so the clang inside emsdk builds it with the
plain WebAssembly target; wasi-sdk builds it too. The same file runs in
UltraWeb: `UltraWeb build-uwr/site/hello.wasm`.

## How it works

```
 page (the site's origin)
 ├── ultraweb-loader.js          the site's: finds the runtime, fetches app.wasm
 ├── ultraweb-runtime.js/.wasm   UltraCanvas + UltraWeb's element ABI (Emscripten)
 │     └── UltraWebGuest          Apps/UltraWeb/host, unchanged
 │           └── WasmHost          browser engine: OS/WASM/UltraCanvasWASMWasmHost.cpp
 │                 └── app.wasm    a second WebAssembly instance, the app
 └── <canvas>                     what the runtime draws, as any Emscripten page
```

### The runtime

[`Apps/UltraWeb/browser/UltraWebRuntime.cpp`](../../Apps/UltraWeb/browser/UltraWebRuntime.cpp)
opens one window filling the page, with an app area that is
`UC_ROOT_HANDLE`, as an UltraWeb tab does. It compiles UltraWeb's
`host/UltraWebGuest.cpp`, `host/UltraWebFetch.cpp` (the fetch rules) and
`host/UltraWebStorage.cpp` as they are, so the ABI's rules (handles, limits,
which events reach the app, when the clipboard may be written) are the
desktop's to the letter. The UltraNet network service of
`UltraWebFetch.cpp` moved to `host/UltraWebNetworkFetch.cpp` for this, since
the browser build has no UltraNet.

### The browser engine behind WasmHost

`UltraCanvasWasmHost` already hid its engine (wasmtime) behind its header,
so the runtime gives it a second one rather than a second bridge:
[`UltraCanvas/OS/WASM/UltraCanvasWASMWasmHost.cpp`](../../UltraCanvas/OS/WASM/UltraCanvasWASMWasmHost.cpp),
the `WasmHost` library's engine under Emscripten.

- **Compiling.** `new WebAssembly.Module`, synchronously, so `Create()`
  keeps its contract. Chromium allows that on the main thread up to 8 MB;
  an app is a few kilobytes.
- **Imports.** Each `WasmImport` becomes a JavaScript function that writes
  its arguments into a scratch area of the runtime's heap and calls back
  into C++ (`uc_wasmhost_dispatch`). The app's memory is reached through
  its exported `memory`, bounds-checked as on wasmtime.
- **Signatures.** A browser does not tell script a function's signature,
  so the engine reads the module's type, import, function and export
  sections itself. It refuses an import the host does not offer and one
  whose signature differs from the `WasmImport`, as wasmtime's linker does.
- **Traps.** A host function's `Trap()` is thrown into the app as a
  `WebAssembly.RuntimeError` and comes out of the `Call()` that entered it.
  The instance is then unusable, as on wasmtime.
- **WASI.** The same small grant as on wasmtime: clocks, random bytes,
  stdout and stderr (to the console), no arguments, no environment, no
  files. `proc_exit` ends the call with "the module exited (status N)".
- **What the browser cannot give.** `callTimeoutMs` is not enforced: an app
  that never returns hangs the tab until the browser offers to stop it. The
  memory cap is checked after each call, not at each `memory.grow`.
  WebAssembly text is refused; serve the binary.

### The services

| Service | In UltraWeb | In the runtime |
|---|---|---|
| Timers | the application's timers | the same (`StartTimer`) |
| Fetch | UltraNet, under `FetchRules` | the page's `fetch()` without credentials, under the same `FetchRules`. The browser applies CORS itself; a cross-origin answer that reached script is passed to the rules as allowed for this origin, since the browser hides that header from script. |
| Storage | a JSON file per origin in the settings folder | the same file, in an IndexedDB-backed directory of the page's origin (synced every 2 s, and when the page is hidden or closed) |
| Clipboard | the system clipboard | `navigator.clipboard`, through the backend's `SetClipboardText` |

The page's origin is the app's origin: an app loaded from
`https://x.org/app/hello.wasm` fetches and stores as `https://x.org`.

URL parsing, which the rules and the storage partitions need, is
UltraNet's `UltraNetUrl.h`; in the browser build it is implemented over the
browser's WHATWG URL parser
([`UltraCanvas/OS/WASM/UltraCanvasWASMNetUrl.cpp`](../../UltraCanvas/OS/WASM/UltraCanvasWASMNetUrl.cpp)).

### The extension

[`Apps/UltraWeb/browser/extension/`](../../Apps/UltraWeb/browser/extension/):
a Manifest V3 extension that carries the runtime as web-accessible files
and one content script of one line. The script runs before the page's own
scripts and puts the runtime's address on `<html data-ultraweb-runtime>`.

The extension **never fetches or runs a site's code**: the page loads the
runtime from the extension's address and runs its app itself. That keeps
the extension within the stores' rule against remotely hosted code, which
an extension that loaded `app.wasm` into its own context would break.

The cost is a permission: a content script on every http(s) page reads to
the user as "read and change your data on all websites", although the
script only writes one attribute. Narrowing it is the first item under
[What would make it a product](#what-would-make-it-a-product).

### The loader

[`Apps/UltraWeb/browser/site/ultraweb-loader.js`](../../Apps/UltraWeb/browser/site/ultraweb-loader.js),
served by the site:

```html
<script src="ultraweb-loader.js" data-app="hello.wasm" data-runtime="runtime/"></script>
```

It takes the extension's runtime when the attribute is there and the
site's `data-runtime` otherwise; without either, it says the page needs the
extension. It fetches the app while the runtime starts and hands its bytes
to `uw_runtime_start`. `<html>` then carries
`data-ultraweb-runtime-source` (`extension` or `site`) and
`data-ultraweb-state` (`running` or `failed`).

## What a visitor downloads

Measured by the end-to-end test below (Release, single-threaded, every
bundled font; gzip -9 as a server would compress it):

| Visit | The site sends | gzip |
|---|---|---|
| No extension, the runtime from the site (`index.html`) | 8,979 KiB: the runtime (`.wasm` 4,556 KiB, fonts in `.data` 4,276 KiB, `.js` 139 KiB), the page, the loader, the app | 3.9 MB |
| With the extension, any page | **8.4 KiB**: the page, the loader, the 4.3 KiB app and its `hello.txt` | 4 KB |
| The extension itself, once | the same runtime, 8.9 MB unpacked | 3.9 MB zipped |

The framework is the bulk either way; what changes is who downloads it and
how often. A site with ten apps, or ten sites with an app each, send it
once with the extension and ten times without it: the browser's HTTP cache
does not share the site's copy between sites, since it is kept per site.
The fonts are half of it: all fourteen bundled faces are preloaded.

## Tests

- `wasmhost-browser-test` (`Apps/UltraWeb/browser/test/wasmhost-browser-test.cpp`),
  run with `node wasmhost-browser-test.js` in the build directory: the
  browser engine alone - every value type both ways, guest memory, traps
  from either side, refused imports, WASI, the memory cap.
- `runtime-test.mjs` (`Apps/UltraWeb/browser/test/`), in Chromium through
  Playwright: the sample site without the extension (clicks, checkbox,
  timer, fetch, storage across a reload), the extension-only page without
  the extension (it asks for it), and both pages with the extension (the
  site sends no runtime).

  ```bash
  node Apps/UltraWeb/browser/test/runtime-test.mjs build-uwr
  ```

  Branded Google Chrome no longer loads unpacked extensions from the
  command line (Chrome 137), so the test runs Playwright's Chromium.

The `wasm-build.yml` workflow runs both in its single-threaded variant.

## Limits of the prototype

- **One app per page, filling it.** The runtime's window is the page's
  viewport, as an Emscripten page's is. An app in a part of a page needs
  the backend to draw into a given `<canvas>`.
- **Single-threaded only.** A page cannot start Web Workers from an
  extension's address, and a threaded runtime would need cross-origin
  isolation headers on every site. The single-threaded build has no libvips,
  which the element ABI does not use yet.
- **No time limit** on an app's call, and the memory cap after the call
  (see above).
- **The app comes from the page's origin.** In a browser the page's origin
  is what fetch and storage really run under, and the runtime takes the
  app's address as the app's origin, as UltraWeb does; the two agree only
  when the page and its `app.wasm` share an origin.
- **The extension can be seen.** Any page can read
  `data-ultraweb-runtime`, so a site can tell that a visitor has it - one
  more bit for fingerprinting, as with any extension that announces itself.
- **Firefox and Safari** are untried; the backend has been exercised in
  Chromium only.
- **No integrity check.** The site's runtime and the extension's are
  trusted as they are; a manifest naming the runtime version and hash
  (`<link rel="ultraweb-app" href="app.json">`, proposal §4.3) would let a
  page ask for a matching one.

## What would make it a product

1. **A narrower content script.** Announce the runtime without touching
   every page: for example, match only pages that opt in by a path
   (`/ultraweb/*`), or answer a `postMessage` from the loader instead of
   writing the DOM.
2. **Versioning.** The loader should say which ABI version the app needs
   and fall back to the site's runtime when the extension's is older.
3. **Store packaging**: a signed Chrome Web Store and addons.mozilla.org
   build of `ultraweb-extension/`.
4. **An app in part of a page**, for apps embedded in an ordinary site.

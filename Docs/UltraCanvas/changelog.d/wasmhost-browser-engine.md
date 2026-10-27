- **WasmHost runs in a browser.** Under Emscripten the `WasmHost` library
  now has an engine: the browser's own WebAssembly
  (`UltraCanvas/OS/WASM/UltraCanvasWASMWasmHost.cpp`), behind the same
  `UltraCanvasWasmHost.h`. It compiles a guest synchronously, reads its
  signatures from the module's own sections (a browser does not show them
  to script), refuses unknown and mismatched imports as wasmtime does,
  throws a host `Trap()` into the guest, grants the same WASI subset, and
  checks the memory cap after each call. A per-call time limit cannot be
  enforced in a browser, and WebAssembly text is refused. Tested under Node
  by `Apps/UltraWeb/browser/test/wasmhost-browser-test.cpp`. It is what runs
  UltraWeb apps in Chrome (`Docs/UltraWeb/UltraWebInTheBrowser.md`).
- **`UltraNetUrl.h` in the browser.** UltraNet is compiled out of a
  WebAssembly build; its URL helpers (`UltraNet_ParseUrl`,
  `UltraNet_ResolveUrl`, `UltraNet_BuildUrl`, the percent coding and query
  strings) now have a browser implementation over the WHATWG URL parser
  (`UltraCanvas/OS/WASM/UltraCanvasWASMNetUrl.cpp`).
- `wasm-build.yml` builds the UltraWeb runtime with the rest and, in the
  single-threaded variant, runs the WasmHost browser test and the runtime's
  end-to-end test in Chromium.

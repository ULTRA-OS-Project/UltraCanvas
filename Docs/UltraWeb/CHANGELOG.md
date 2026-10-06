#### 2026-10-06 *0.1.0*
- **UltraWeb runs WebAssembly apps.** The first version of the ULTRA OS
  browser loads a `.wasm` (or WebAssembly text) app from `about:demo`, a file
  path, a `file://` URL or an `https://` URL, and runs it in a sandbox: the
  app builds its UI out of real UltraCanvas elements - containers, labels,
  buttons, text inputs, checkboxes - through the element ABI in
  `UltraWeb/guest/ultraweb.h`, and holds only handles to them. An app that
  traps or runs one call for longer than two seconds is stopped and its error
  shown, and its memory cannot grow past 256 MB, so it can neither crash nor
  freeze the browser. `about:demo` is
  built in, and `UltraWeb --check <address>` starts an app without a window.
  HTML pages and JavaScript come in later phases
  (`Docs/UltraWeb/UltraWebProposal.md`). Runs on Linux in this version; the
  framework's WasmHost module, on wasmtime 49, is what it is built on (see the
  framework changelog).

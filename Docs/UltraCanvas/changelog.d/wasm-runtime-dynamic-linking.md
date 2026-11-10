- **The UltraCanvas runtime for browsers (prototype): apps keep the whole
  C++ API and ship only their own code.** UltraCanvas, cairo, pango, glib
  and the fonts are linked once into an Emscripten main module that exports
  the C++ API (`UltraCanvas/OS/WASM/runtime`). An app is a side module built
  with `ultracanvas_wasm_app()` (`cmake/UltraCanvasWasmApp.cmake`) from an
  ordinary UltraCanvas `main()`; the runtime loads it with `dlopen` and calls
  that `main()`. A Manifest V3 extension installs the runtime once, so a site
  sends only its app; `ultracanvas-loader.js` takes the extension's runtime,
  or the site's own copy when the extension is missing or of another version.
  Tested end to end in Chromium (`UltraCanvas/OS/WASM/runtime/test/runtime-test.mjs`).
  See `Docs/UltraCanvas/UltraCanvasWasmRuntime.md`.
- `build-wasm-sysroot.sh` takes `WASM_PIC=1` for a position-independent
  sysroot, and the root build `ULTRACANVAS_WASM_DYNAMIC` (single-threaded),
  which compiles everything `-fPIC` and adds the runtime.
- `wasm-build.yml` gains a `dynamic` variant: the -fPIC build against a
  -fPIC sysroot, with the runtime's Chromium test.

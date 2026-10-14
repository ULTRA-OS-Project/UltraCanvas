- **The WebAssembly workflow runs on pull requests.** `wasm-build.yml` was
  manual only, so a change to the port, the sysroot script, the Cairo image
  code or the demo reached `main` untested in the browser. It now runs on a
  pull request, and on a push to `main`, that touches one of those paths
  (`.github/workflows/wasm-build.yml`, `UltraCanvas/OS/WASM/`,
  `cmake/UltraCanvasWasmSysroot.cmake`, the two `CMakeLists.txt`,
  `UltraCanvas/libspecific/Cairo/`, `UltraCanvas/third_party/stb/`,
  `Apps/DemoApp/`); a manual dispatch still takes an emsdk version.

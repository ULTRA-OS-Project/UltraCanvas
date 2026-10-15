- **WasmHost runs WebAssembly on Windows.** The MSYS2 CLANG64 / CLANGARM64
  builds had no engine, because wasmtime publishes no library for their
  `gnullvm` ABI. `cmake/UltraCanvasWasmtime.cmake` (1.2.0) now builds
  wasmtime's C API from its source release there, offline, with the native
  Rust toolchain (`mingw-w64-clang-*-rust`, which the Vectorizer plugin
  already needs).
  - The source release is hash-checked like the prebuilt archives.
  - Only the features WasmHost uses are built: Cranelift, WebAssembly
    text, WASI, parallel compilation, and the GC behind exceptions.
  - `ULTRACANVAS_ENABLE_WASM_HOST` is now on by default for MinGW builds.
    Without a Rust toolchain the build carries on without an engine and
    says why, and the no-engine message names the package to install.
  - `ULTRACANVAS_WASMTIME_FROM_SOURCE=ON` takes the same path on any
    platform.

- **A WebAssembly host for applications: the WasmHost module.**
  `WasmHost/UltraCanvasWasmHost.h` compiles a module (binary, or WebAssembly
  text), links host functions an application offers, instantiates it with
  WASI preview 1 and nothing granted (no files, no environment), and calls its
  exports. Every call into a guest has a time limit (default 2 s, epoch
  interruption) and every instance a memory cap (default 256 MB), and every
  way a guest can fail - a trap, a C++ exception thrown by a host function,
  a runaway loop, bad input - ends in a `WasmStatus`, never a crash or a hang.
  The engine is wasmtime 49.0.2 through its prebuilt C API, downloaded and
  hash-checked by `cmake/UltraCanvasWasmtime.cmake` (or taken from
  `ULTRACANVAS_WASMTIME_DIR`) and private to the module, so no wasmtime type
  reaches a caller. On by default on Linux (`ULTRACANVAS_ENABLE_WASM_HOST`);
  elsewhere the module builds without an engine and says so. UltraWeb is its
  first user; `Tests/WasmHostTest.cpp` covers it with guests written inline
  as WebAssembly text.

- **UltraWeb links on Ubuntu 24.04.** wasmtime's prebuilt library carries
  its own zstd with hidden symbols, and on Ubuntu 24.04 `libvips-dev` pulls
  in `libarchive-dev`, so VirtualFS links the system libarchive, which uses
  the system zstd. GNU ld then refused `UltraWeb` and `UltraWebGuestTest`:
  "hidden symbol `ZSTD_freeCStream' ... is referenced by DSO". CI's Ubuntu
  22.04 has no libarchive and never saw it; a 24.04 machine and every cloud
  session did. Where the system zstd is 1.5 or newer, `WasmHost` now links it
  ahead of the wasmtime archive (`ULTRACANVAS_WASMTIME_PRELINK`,
  `cmake/UltraCanvasWasmtime.cmake`), so wasmtime binds to it and the
  archive's copy is never pulled in; with an older zstd or none, nothing
  changes. `WasmHostTest` and `UltraWebGuestTest` pass against it.

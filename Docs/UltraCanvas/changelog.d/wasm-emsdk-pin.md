- **CI: the WebAssembly build names its Emscripten version.** The workflow
  installed `latest` unless told otherwise, so the compiler moved to every
  new emsdk release, a new major one included, while the cached sysroot,
  keyed on the word `latest`, stayed built by whichever release came first.
  It installs 6.0.11 now, the newest release of the 6.0 line the backend was
  validated with, and `UltraCanvas/OS/WASM/README.md` names the same
  version; a run can still ask for another one.
- **CI: the WebAssembly build's glib gets the meson it needs.** The
  workflow installed meson from Ubuntu 24.04 (1.3.2), and the sysroot's glib
  (`wasm-vips-2.89.3`) refuses anything older than 1.4, so the first run of
  the workflow stopped in `meson setup` for glib. It installs meson 1.12.1
  from PyPI now, and `build-wasm-sysroot.sh` names the 1.4 floor among its
  requirements.
- **CI: the WebAssembly demo finds the sysroot's libraries.** CMake runs
  the host's `pkg-config`, and `emcmake` does not pass it
  `EM_PKG_CONFIG_PATH`, so the demo's configure step looked for cairo on the
  host and failed after the whole sysroot had built. The step puts the
  sysroot on `PKG_CONFIG_PATH` and `PKG_CONFIG_LIBDIR`, as
  `build-wasm-sysroot.sh` already does, and the README and the demo's
  `CMakeLists.txt` give the same two exports. The sysroot is cached as soon
  as it is built rather than only when the whole job passes, so a failure
  in the demo no longer costs the next run a 20-minute rebuild.

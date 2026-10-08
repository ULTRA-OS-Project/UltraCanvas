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

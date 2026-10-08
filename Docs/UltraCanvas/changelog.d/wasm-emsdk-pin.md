- **CI: the WebAssembly build names its Emscripten version.** The workflow
  installed `latest` unless told otherwise, so the compiler moved to every
  new emsdk release, a new major one included, while the cached sysroot,
  keyed on the word `latest`, stayed built by whichever release came first.
  It installs 6.0.11 now, the newest release of the 6.0 line the backend was
  validated with, and `UltraCanvas/OS/WASM/README.md` names the same
  version; a run can still ask for another one.

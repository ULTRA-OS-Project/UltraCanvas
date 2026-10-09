- **CI: the caches move off Node.js 20.** `actions/cache@v4` (the ccache and
  macOS vcpkg caches in the Build workflow, the sysroot cache in the
  WebAssembly one) targets Node.js 20, which GitHub's runners no longer run:
  every Build leg ended with "Node.js 20 is deprecated … being forced to run
  on Node.js 24: actions/cache@v4". It is `actions/cache@v5` now, the Node.js
  24 release; the inputs and outputs the workflows use are unchanged. The
  WebAssembly workflow's `actions/upload-artifact@v4` moves to v6, the version
  the Build workflow already uses.
- **CI: the WebAssembly workflow installs Emscripten with emsdk itself.**
  `mymindstorm/setup-emsdk@v14` targets Node.js 20 too, and every run of
  the workflow carried the same warning for it. The step now clones emsdk
  and runs `emsdk install` and `emsdk activate` for the requested version,
  then hands the later steps the same `PATH`, `EMSDK` and `EMSDK_NODE` the
  action did. The toolchain is no longer cached; it downloads in a minute or
  two on a job that only runs when started by hand.

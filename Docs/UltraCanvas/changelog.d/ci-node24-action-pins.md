- **CI: the caches move off Node.js 20.** `actions/cache@v4` (the ccache and
  macOS vcpkg caches in the Build workflow, the sysroot cache in the
  WebAssembly one) targets Node.js 20, which GitHub's runners no longer run:
  every Build leg ended with "Node.js 20 is deprecated … being forced to run
  on Node.js 24: actions/cache@v4". It is `actions/cache@v5` now, the Node.js
  24 release; the inputs and outputs the workflows use are unchanged. The
  WebAssembly workflow's `actions/upload-artifact@v4` moves to v6, the version
  the Build workflow already uses.

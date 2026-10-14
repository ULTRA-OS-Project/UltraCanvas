- **The single-threaded WebAssembly build runs in current browsers again.**
  It stopped on its first request for random bytes with
  `Failed to execute 'getRandomValues' on 'Crypto': The provided
  ArrayBufferView value must not be resizable` (the threaded variants were
  unaffected). The core's link options passed `-sGROWABLE_ARRAYBUFFERS=1`
  to every WebAssembly build; in a browser with resizable WebAssembly
  memory that makes a single-threaded program's heap a resizable
  ArrayBuffer, and Emscripten hands such a heap to Web Crypto directly,
  where it copies shared memory through a temporary buffer first. The
  option now goes with `-pthread` only, where it earns its place (growth
  without a check on every JavaScript-side heap access); a single-threaded
  program grows its memory the ordinary way.

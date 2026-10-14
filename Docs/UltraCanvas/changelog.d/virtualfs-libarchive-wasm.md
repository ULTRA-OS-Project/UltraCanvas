- VirtualFS no longer warns about a missing libarchive in a WebAssembly
  configure: the wasm sysroot never holds it, so the message is a status
  line there and a warning everywhere else.

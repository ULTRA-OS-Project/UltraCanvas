- **The thread-less WebAssembly sysroot's `.pc` files no longer carry
  `-pthread`.** glib's pkg-config file, and through it fontconfig's,
  harfbuzz's, pixman's and libvips', added `-pthread` to the flags of every
  consumer even in a sysroot built with `WASM_THREADS=0`, so a program
  configured without threads was compiled with `__EMSCRIPTEN_PTHREADS__`
  set - which is how pango's no-threads guard first missed. The sysroot
  script now scrubs the flag from every installed `.pc` at the start of each
  stage and at the end, so what `pkg-config` reports matches the sysroot.
  Pango's no-threads patch still reads its explicit `PANGO_FC_NO_THREAD`
  switch.

- **The single-threaded WebAssembly build runs.** The workflow's
  `single-thread` variant had never run in a browser: every page ended on
  `GLib-ERROR (g_system_thread_new): error 'Not supported' during
  'pthread_create'` at its first text, which the new smoke step was the
  first to see. Two causes. pango hands fontconfig's work (FcInit,
  matching, sorting) to a worker thread it starts with `g_thread_new`, a
  program built without `-pthread` cannot start one (Emscripten answers
  ENOTSUP) and GLib treats that answer as a programming error; a sysroot
  patch (`pango-1.52-wasm-no-threads.patch`) runs those jobs on the
  caller's thread when there are no threads, and the threaded build keeps
  its worker. And libvips evaluates every image on a worker thread of its
  own (wasm-vips itself requires threads), so the library's CMake now
  forces `ULTRACANVAS_ENABLE_VIPS` off when `ULTRACANVAS_WASM_THREADS` is
  off, with a status line saying why: such a program reads PNG through
  cairo's reader and SVG through the Vector plugin, and the workflow's
  variant says so.

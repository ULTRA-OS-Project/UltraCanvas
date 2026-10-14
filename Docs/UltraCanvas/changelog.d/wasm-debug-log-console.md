- **`debugOutput` reaches the browser console in the WebAssembly build.** The
  diagnostic stream was off in a release build unless `ULTRACANVAS_DEBUG_LOG`
  said otherwise, and a `.wasm` inherits no environment to say it in, so the
  demo's "Error creating example for ..." line, written when a page's builder
  throws, went nowhere. Under Emscripten the sink is now on by default and
  writes to stderr, which the browser shows in its developer console; a page
  turns it off with `Module.ENV.ULTRACANVAS_DEBUG_LOG = "0"` in a `preRun`
  hook. Desktop and Android defaults are unchanged.

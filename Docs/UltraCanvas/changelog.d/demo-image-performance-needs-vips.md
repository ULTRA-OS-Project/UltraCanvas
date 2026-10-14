- The demo's *Image Performance Test* page is built and registered only with
  libvips, whose codecs it benchmarks through `vips::VImage` directly: a
  build without libvips (the WebAssembly workflow's no-vips variant) failed
  to compile it. It joins the bitmap formats and Pixel FX pages behind the
  same guard.

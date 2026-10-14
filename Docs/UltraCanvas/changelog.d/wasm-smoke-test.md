- **The WebAssembly workflow opens its pages in a browser.** `wasm-build.yml`
  built five variants and ran none, so a page that came up empty - the
  JPEG page, until the sysroot learnt JPEG - was found by a person after the
  download. Each variant's job now serves its collected pages with
  `serve.py` and opens them in the runner's Google Chrome, headless, with
  the new `UltraCanvas/OS/WASM/demo/smoke.mjs`: the demo's start page and a
  handful of its pages (SVG, pie charts and, where libvips is in, the PNG,
  JPEG, TIFF, WebP, GIF and BMP pages), plus the two validation apps. A page
  must present a frame and keep a console free of "Error creating
  example", uncaught errors and Emscripten aborts, or the job fails; the
  screenshots and console logs are the `wasm-smoke-<variant>` artifact.
  The script runs against a local build too (the WebAssembly README).

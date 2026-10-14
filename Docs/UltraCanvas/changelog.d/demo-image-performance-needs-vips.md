- **A build without libvips links again** (the WebAssembly workflow's no-vips
  variant, run 16 of `wasm-build.yml`): the demo's bitmap format pages and
  its *Image Performance Test* are registered only with libvips, whose
  codecs they show and benchmark, matching the guard their sources already
  had in CMake; `ShowInMediaViewer`, which the vector and 3D pages call too,
  moved from the libvips-only `UltraCanvasBitmapFormatDemo.cpp` to
  `UltraCanvasMediaViewerExamples.cpp`, built in every configuration; and
  the QR code element's `ScanQRCodeFile` and picture export, which read and
  write through libvips, now say so and fail cleanly without it instead of
  leaving the core library with unresolved libvips symbols. The element,
  the pixel-buffer scan and the decoder are unchanged. The OCR and
  Vectorizer plugins, whose own CMake files require libvips (one hands
  libvips images to tesseract, the other loads the raster it traces through
  PixelFX), are skipped with a status line when the core is configured with
  `ULTRACANVAS_ENABLE_VIPS=OFF`, instead of linking against a PixelFX the
  core has not got; the demo registers its Vectorizer page only with the
  plugin, as before.

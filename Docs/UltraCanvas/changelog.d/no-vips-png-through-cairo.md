- **A build without libvips reads PNG.** `UCImage` in a build configured
  with `ULTRACANVAS_ENABLE_VIPS=OFF` read SVG (through the Vector plugin)
  and nothing else, so the demo's own chrome - the tree's folder and status
  icons, the page buttons, the About logo - came up as grey squares in the
  WebAssembly `no-vips` and `single-thread` variants. PNG now goes through
  cairo's own reader (`cairo_image_surface_create_from_png_stream`), from a
  file or from bytes: the size comes from the header at load, the pixels
  are decoded when a pixmap is first asked for, the fit modes mean what the
  libvips thumbnail path means them to, and `HasTransparency` answers from
  the header's colour type. Other raster formats still report that the
  build has no decoder for them.

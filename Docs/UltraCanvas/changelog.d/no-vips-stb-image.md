- **A build without libvips reads JPEG, BMP and GIF.** `UCImage` in a build
  configured with `ULTRACANVAS_ENABLE_VIPS=OFF` (the WebAssembly `no-vips`
  and `single-thread` variants, the ones a static host without cross-origin
  isolation headers serves) read PNG and SVG and nothing else, so a
  photograph came up as "no decoder for this picture". The bundled
  `stb_image` (`UltraCanvas/third_party/stb`, public domain, compiled to
  nothing in a build with libvips) now decodes JPEG, BMP and the first
  frame of a GIF through `libspecific/Cairo/StbRasterDecode`, on the same
  road as PNG: size from the header at load, pixels when a pixmap is first
  asked for, the fit modes as the libvips thumbnail path means them. The
  demo's JPEG, BMP and GIF pages show their pictures in those variants; TIFF,
  WebP, HEIC and AVIF still say the build cannot read them.

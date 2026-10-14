- **The browser reads JPEG, TIFF and WebP.** The WebAssembly sysroot's
  libvips was built with every optional codec off, so beside PNG and the
  built-in GIF decoder it opened nothing: the demo's JPEG, TIFF and WebP
  pages came up empty. `build-wasm-sysroot.sh` now builds libjpeg-turbo,
  libtiff and libwebp into the sysroot and libvips with `jpeg`, `tiff` and
  `webp` enabled, for reading and writing; the workflow's sysroot cache is
  keyed on the script, so the next run rebuilds it. Every pkg-config query
  of a WebAssembly configure is a static one now (`--static`, set by
  `cmake/UltraCanvasWasmSysroot.cmake`): the program is one static link,
  and libwebp names its libsharpyuv only in `Requires.private`, without
  which the link stopped on `SharpYuvConvert`. HEIC / AVIF (libheif
  and a decoder, a build of their own and megabytes of download) and BMP
  (ImageMagick) stay out, and the WebAssembly README says which formats a
  browser build has.

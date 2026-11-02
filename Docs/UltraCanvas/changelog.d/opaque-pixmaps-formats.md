- **A picture without an alpha channel is an opaque pixmap.** `UCImage`
  decodes a JPEG, a HEIC, an opaque WebP - any picture with no alpha band -
  to a `CAIRO_FORMAT_RGB24` pixmap: the same four bytes a pixel (cairo has
  no packed 24-bit image format), the top byte 0xFF and ignored, so cairo
  composites it as a copy instead of blending every pixel, and the QOI
  pixmap blob the thumbnail cache stores says so and inflates it opaque
  again (`UCPixmapCairo::IsOpaque`, `Init(w, h, opaque)`). A picture with
  an alpha channel is ARGB32 as before. The decode itself is quicker too,
  the opaque rows needing no premultiplication: the wallpaper fitted to
  3840 × 2160 took 300 ms and takes 155.
- **A large fitted pixmap is kept on disk between runs.** `UCImage::GetPixmap`
  for a raster file fitted to a box of at least 640 × 480 raw pixels - a
  desktop's wallpaper, a viewer's photo - looks in the thumbnail disk cache
  first and stores its pixmap there as a QOI blob after the first decode,
  stamped against the file as it was before; the next run inflates the
  blob instead of decoding the file and resizing it. Measured on the
  2752 × 1536 desktop wallpaper as a JPEG: 40 ms (decode and fit to 1280 × 800)
  against 10 ms from the blob, 155 ms against 75 at 3840 × 2160; the first
  run pays once for the blob's compression and write. The QOI
  *file* format was measured for that wallpaper and not taken: libjpeg-turbo
  reads the 4-megapixel JPEG in 31 ms and fits it in 40, the QOI reference
  decoder 41 and 54 (it has no reduced-size decode), and the file is six
  times the size - QOI pays only without a decode and a resize, which is
  what the cached blob is.
- **Every modern picture format in every build.** `ImageFormatsTest` (under
  `BUILD_TESTS`, so every CI row) reads a JPEG, PNG, GIF, BMP, TIFF, WebP,
  AVIF, HEIC, JPEG XL, QOI and Windows icon from `media/images` and fails
  the build that cannot decode one of them (`UC_IMAGE_FORMATS_OPTIONAL`
  names the formats a machine cannot have: CI sets it to `jxl` on the
  ubuntu-22.04 runners, whose libvips is built without libjxl); the Linux CI rows and the
  dependency lists in the READMEs and `AGENTS.md` name libheif's HEVC and
  AV1 plug-in packages (separate from Ubuntu 24.04 on), and
  `package-linux.sh` bundles libvips' loader modules and libheif's plug-ins
  - both opened at run time, so the library closure never carried them -
  with the launcher pointing `VIPSHOME` and `LIBHEIF_PLUGIN_PATH` at them.
  `media/images/dice.ico` is the icon sample.

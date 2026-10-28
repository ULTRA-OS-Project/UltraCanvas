- **`ImageHeifTest`: HEIF and AVIF are under test, on every platform.** The
  libheif counterpart of `ImageJxlTest`. It asks what the build can do the
  way the framework does (`VipsCanDecode` / `VipsCanEncode` - libheif reads
  and writes each format only through a codec for it), decodes the
  repository's `dice.heic` / `dice.avif` through `UCImageRaster` for each
  format it reads, and for each it writes has the export encoder and
  `UCImageRaster::Save` write a lossy (quality 75) and a lossless file that
  must read back close to the picture, transparency kept.
  `UC_HEIF_EXPECT` says what a platform must - and must not - do, and CI
  sets it per row: Linux (Ubuntu 24.04 with every libheif plug-in) and
  Windows (MSYS2's libheif links them all) read and write both; macOS reads
  both and writes AVIF, and fails if it can encode HEIC, so x265 (GPL)
  cannot enter the core every Mac app links unnoticed.
  `ULTRACANVAS_BUILD_HEIF_TESTS` builds it without `BUILD_TESTS`, as the
  macOS and Windows rows need.

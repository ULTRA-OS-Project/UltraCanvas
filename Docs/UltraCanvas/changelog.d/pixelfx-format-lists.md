- **PixelFX reports FileLoader's file types.**
  `PixelFX::FileIO::GetSupportedLoadFormats` and `GetSupportedSaveFormats`
  returned a fixed list. It offered HEIC and AVIF everywhere and a "raw" no
  loader takes, and it left out ICO, QOI, JPEG XL, BMP, camera raw files and
  everything else libvips reads. They are now FileLoader's lists, as this
  build has them:
  - Reading: every picture type `UltraCanvasFileLoader` reads, plus the
    drawings and documents among its types that libvips itself opens (SVG,
    PDF).
  - Writing: every picture type it writes.
  - HEIC follows libheif's decoder and encoder.
  - `IsFormatSupported` accepts a type with its dot (".png").
  - `FileLoaderTypeListsTest` holds PixelFX to FileLoader's lists.

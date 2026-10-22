- **CI tests JPEG XL for real.** `build.yml`'s Linux rows run on Ubuntu
  22.04, whose libvips has no libjxl, so `ImageJxlTest` only ever reported
  SKIP there. A new workflow, `Image codecs` (`image-codecs.yml`), builds the
  core and the headless image tests (`ImageJxlTest`, `ImageIcoQoiTest`,
  `ImageGifSaverTest`) on Ubuntu 24.04, whose libvips 8.15 reads and writes
  JPEG XL, and fails when `ImageJxlTest` skips instead of passing. It
  configures the core with charts and every app and optional module off -
  the chart-free core no other job builds - and links `ImageJxlTest` (which
  reaches the progress dialog) and `MatrixDiagramRenderTest` (a diagram)
  against it, so that configuration cannot stop linking unnoticed again.

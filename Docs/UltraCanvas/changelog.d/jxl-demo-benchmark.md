- **DemoApp's image performance page benchmarks JPEG XL again.** JXL was
  taken out of the codec list in 0.1.35 because the libvips of the day could
  not write it, so its column only ever showed a failure. It is back, between
  TIFF and BMP, and is charted only where libvips can both save and load
  `.jxl` (a libvips built with libjxl - Ubuntu 24.04's, not 22.04's); on any
  other build the column is left out instead of failing.

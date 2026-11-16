- **A BMP opens at once: the framework reads it itself, not through
  ImageMagick.** libvips has no BMP loader, so a `.bmp` went to `magickload`,
  and the first one a program opened paid for ImageMagick's start-up on the
  thread that asked - on Windows its configuration and every coder DLL under
  `modules-Q16HDRI\coders`, about six seconds in the demo app when the
  Bitmap Elements tree reached BMP Images. `VipsBmpPlugin`
  (`libspecific/Cairo`) registers `ucbmpload` / `ucbmpload_buffer` beside
  the ICO and QOI loaders, reading with the bundled stb_image: 1, 4, 8, 16,
  24 and 32-bit BMPs, top-down or bottom-up, bit-field masks, alpha, and the
  header's resolution as `xres` / `yres`. Every libvips path takes it -
  `UCImageRaster`, `PixelFX::ExtractImageInfo`, the media viewer, the export
  dialog. Pixel for pixel what ImageMagick reads (`ImageFormatsTest`); an
  RLE-compressed BMP, which stb_image does not read, is left to ImageMagick
  as before. BMP is still written through ImageMagick.

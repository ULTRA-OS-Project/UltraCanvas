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
  dialog. RLE8 and RLE4 bitmaps, which stb_image does not read, are decoded
  by the plug-in itself (runs, indices as they are, deltas). Pixel for pixel
  what ImageMagick reads (`ImageFormatsTest`); only OS/2 2.x Huffman and
  RLE24 bitmaps are left to ImageMagick. BMP is still written through it.
- **ImageMagick starts up in the background at launch.** The formats only
  it reads (TGA, PCX, PSD, ...) paid the same start-up on the first file
  opened. `UCImageRaster::WarmUpImageMagickInBackground()` loads a tiny
  picture through `magickload` on a thread of its own; a load meanwhile
  waits only for what is left, and `ShutdownImageSubsysterm` joins the
  thread. `UltraCanvasApplication` calls it after the image subsystem
  starts; command-line tools and tests, which call it not, pay nothing.
  `UC_NO_MAGICK_WARMUP=1` turns it off.

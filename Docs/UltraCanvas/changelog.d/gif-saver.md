- **Every build saves GIF.** libvips writes GIF only when it was built with
  cgif. The MSYS2 package (`-Dcgif=disabled`) and the macOS libraries have no
  cgif, and ImageMagick's GIF coder is an optional delegate too. On those
  builds `write_to_file` and `PixelFX::FileIO::Save` could not write a GIF,
  and FileLoader's save list left GIF out. The image export and
  `PixelFX::FileIO::SaveGif` each carried their own fallback to the
  framework's encoder.
  - The framework's GIF encoder is now a libvips saver, `ucgifsave` and
    `ucgifsave_buffer` (`VipsGifPlugin.h`). It is registered with the ICO
    and QOI plug-ins and ranks below libvips' own `gifsave`, so libvips uses
    it for a `.gif` only where it has no GIF writer.
  - It takes `bitdepth` and `interlace`. It accepts `dither`, as `gifsave`
    does, but does not use it.
  - `vips_foreign_gif_saver()` names the saver to call when the file name
    does not end in `.gif`.
  - The image export and `SaveGif` call that saver, so their two fallbacks
    are gone. `UltraCanvasGifEncoder.h` gained `EncodeGif`, which encodes
    to memory.
  - `ImageGifSaverTest` (new) checks the saver: pixels, transparency, its
    options, the buffer saver, and that libvips picks it only without cgif.

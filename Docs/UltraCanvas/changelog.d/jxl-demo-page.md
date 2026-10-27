- **DemoApp: a JPEG XL Images page.** Bitmap Elements had a page for every
  format the framework reads but JPEG XL. It gets one, with its format
  specification (ISO/IEC 18181: VarDCT and Modular, lossless JPEG
  recompression, up to 32-bit float, alpha, animation, progressive), and a
  JXL-art sample: `media/images/jxl_art2.jxl` draws a 2160 x 3840 picture
  from 82 bytes. `media/images/jxl_art.jxl` (62 bytes, 1024 x 1024, 16 bits a
  channel) joins it. A build whose libvips has no libjxl shows the page with
  "This build cannot read JPEG XL files" in place of the picture.

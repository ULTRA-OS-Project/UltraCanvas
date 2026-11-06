- **OCR: `RecognizeFile` reads any picture, by a UTF-8 name.** It went
  through Leptonica's `pixRead(path.c_str())`, which takes the name in the
  ANSI code page on Windows (a folder with a Thai or emoji name failed) and
  reads no WebP, HEIC, AVIF or JPEG XL. `UltraCanvasOCR::RecognizeFile`
  decodes with libvips now - every type the image pipeline reads, UTF-8
  names on every platform - and the Tesseract engine's own reader, the
  fallback, reads the bytes through `OpenFileUtf8` and decodes them from
  memory. A picture with transparency is flattened onto white: libvips'
  `flatten()` defaults to black, so dark text on a transparent screenshot
  came out black on black; 16-bit pictures are scaled to 8 bits instead of
  clipped. OCR plugin 0.2.1.
- **Linux CI builds the OCR plugin.** The Linux rows install Tesseract,
  Leptonica and the English data (`libtesseract-dev libleptonica-dev
  tesseract-ocr-eng`), as the cloud session hook now does too: Linux had
  never compiled the plugin, and `FileLoaderExtractTextTest`'s real-OCR
  checks ran nowhere in CI. Ubuntu 22.04 ships Tesseract 4.1, the oldest
  the plugin now meets.

- **An image whose header reads but whose pixels do not decode crashed
  whatever drew it.** libvips opens a file lazily, so such an image has a
  size and counts as valid; only making the pixmap finds the pixels missing,
  and `CreatePixmapFromVImage` then copied from the null pointer
  `VImage::data()` returns. The demo's Media Viewer hit it browsing to
  `dice.heic` on a build whose libheif has no HEVC decoder, and a truncated
  PNG does the same everywhere. The pixmap request now fails cleanly: no
  pixmap, the libvips reason in the image's error message, and no decoding
  again on the next request. The function's other two failures (no Cairo
  surface, no surface data) threw an exception the pixmap path does not
  catch, which ended the program too; all three now throw `vips::VError` and
  free the surface first. New `ImageUndecodableTest`.

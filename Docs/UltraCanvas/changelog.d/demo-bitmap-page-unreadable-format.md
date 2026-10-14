- **The demo's bitmap pages build when the sample cannot be read.** The
  page asked PixelFX for the sample's properties, and that call throws when
  libvips cannot open the file; nothing caught it, so a format this build
  has no codec for lost the whole page, About and Format Specification
  cards included, with the reason going to a debug stream that was off. The
  page now says "This build cannot read JPEG files" (with the reader's
  message) where the picture and its properties would be, keeps the cards,
  hides the Full Size and Export buttons, and Export no longer throws out
  of its click handler when libvips cannot reopen the file.

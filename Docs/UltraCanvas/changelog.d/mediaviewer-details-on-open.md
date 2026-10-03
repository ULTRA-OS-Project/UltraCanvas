- **The media viewer builds its Details text only while the panel is open.**
  It used to build it for every file it loaded, which reads the file's
  metadata (EXIF, IPTC, XMP, ICC, PNG text) through libvips and lays out the
  Markdown tables, even with the panel closed, which is how it usually is.
  Now opening the panel builds the text for the file on show, and while the
  panel stays open each file loaded refreshes it, so browsing with it closed
  costs nothing extra. An empty viewer's panel says "No media" instead of
  keeping the last file's details.

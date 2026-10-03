- **The media viewer says when a picture cannot be decoded.** A file whose
  header reads but whose pixels do not (a HEIC on a build without an HEVC
  decoder, a truncated PNG) left an empty display and an info bar listing
  its size as if it were showing. The display now reads "Cannot decode this
  picture", the info bar gives the reason libvips reports
  (`cannot decode - heif: Unsupported feature: Unsupported codec (4.3000)`),
  and an open Details panel adds a *Decoding* row. Only drawing decodes the
  pixels, so the surface finds out then and tells the viewer through the new
  `UltraCanvasMediaSurface::onDecodeFailed`, once per shown image and after
  the frame rather than inside `Render`.

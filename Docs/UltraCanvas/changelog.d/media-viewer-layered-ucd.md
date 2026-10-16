- **The media viewer shows a layered `.ucd` image whole.** A `UCBitmap`
  document's layers are read through `UCRasterDocument::LoadUCD` and
  composited, so UltraViewer - and every pane built on
  `UltraCanvasMediaViewer` - shows the picture at full size and zooms it,
  instead of the 256 px thumbnail stored after the header. A
  password-protected document still shows its thumbnail (none when it is
  private), and Details gains the image size, the layer count and a *Shown*
  row saying what is shown and why.

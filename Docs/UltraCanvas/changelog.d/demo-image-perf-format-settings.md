- **DemoApp: the Image Performance Test chooses its formats from FileLoader.**
  A gear button at the right end of the page's toolbar opens a dialog with
  one switch per bitmap format this build both writes and reads
  (`UltraCanvasFileLoader::GetSupportedFormats(Bitmap)`, `canSave &&
  canLoad`, with options from `GetImageExportOptions`), plus the page's own
  QOI and BMP encoders, so TGA, PPM, ICO, HDR, EXR, PSD and the rest can be
  timed too. The choice is kept while the program runs. JPEG 2000 is now off
  by default - its encoder stretched the time axis until every other bar was
  a sliver - and the *Include BMP* checkbox went into the list (BMP stays off
  by default). Each libvips format is written with its Save settings, as any
  application saves it. Opening the page shows a popup, *"Upload an image to
  start the test."*

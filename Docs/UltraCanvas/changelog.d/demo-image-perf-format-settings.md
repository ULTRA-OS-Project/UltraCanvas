- **DemoApp: the Image Performance Test chooses its formats from FileLoader.**
  A gear button at the right end of the page's toolbar opens a dialog with
  one switch per bitmap format this build both writes and reads
  (`UltraCanvasFileLoader::GetSupportedFormats(Bitmap)`, `canSave &&
  canLoad`, with options from `GetImageExportOptions`), so TGA, PPM, ICO,
  HDR, EXR, PSD and the rest can be timed too. The choice is kept while the program runs. JPEG 2000 is now off
  by default - its encoder stretched the time axis until every other bar was
  a sliver - and the *Include BMP* checkbox went into the list (BMP stays off
  by default). Every format is written with its Save settings, as any
  application saves it. Opening the page shows a popup, *"Upload an image to
  start the test."*
- **DemoApp: the Image Performance Test writes QOI and BMP through
  FileLoader.** They were the last two formats the page encoded and decoded
  with code of its own (`qoi.h`'s `qoi_write` / `qoi_read`, and a 24bpp BMP
  writer and reader in the page), against the rule that every file goes
  through `UltraCanvasFileLoader`. Both now take the path every other format
  does - `UCImage::Save` with `GetImageExportOptions`, then `UCImage::Load`
  with a full decode - and are offered where FileLoader writes and reads
  them. Their timings now include libvips' pipeline, as every other column's
  always did, so they are not comparable with earlier runs. About 420 lines
  of page-local codec code are gone.

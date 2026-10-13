- **A drawing gets a size in the Save dialog's Settings too.** The Size row
  of a bitmap type's Settings needed the picture's pixel size, and a drawing -
  an SVG, a page of shapes - has none, so exactly where a size is wanted most
  there was no field for it. `FileDialogOptions::SetDrawingSize(width,
  height)` (`FileDialogConfig::imageIsDrawing`) gives the size a drawing is
  drawn at unless another is chosen; the row opens on it, Keep proportions
  keeps the drawing's shape, and the tooltips say it is drawn sharp at any
  size. `FileSavePicture` (`width`, `height`, `drawing`) is the same for
  `ShowSaveSettingsDialog`. `FileDialogTest` checks the row, the size coming
  back and that a bitmap's `SetImageSize` is not a drawing.
- **The settings dialog is the controls alone.** The paragraph above the
  controls ("How PNG files are written. Every application ...") is gone - the
  title names the format and each setting's tooltip says what it does. The
  controls make one column of one width: a slider is no longer stretched
  across the dialog with its value at the far edge but sits at the list's
  width with the value beside it, the lists and size fields line up under it,
  and a rule parts the size (this save's) from the format's own settings. A
  slider sat 3 px below its caption - a horizontal `UltraCanvasSlider` draws
  along its bottom edge, leaving room above for a value - and is now exactly
  as tall as its handle, so the row centres it level with the caption.
- **A dialog of controls has no empty room above its buttons.**
  `UltraCanvasModalDialog` sized itself for its message area even when that
  was hidden, so a dialog without a message - the save settings, among
  others - kept an empty line and its slack, some 40 px, between the last
  control and the buttons. A hidden message area now takes no room.
- **Media viewer: Save image as at another size, with the type's save
  settings.** `UltraCanvasMediaViewer` (UltraViewer) never told the Save
  dialog how big its picture was, so a bitmap type's Settings had no Size row
  there, and it wrote every file with the writer's defaults whatever those
  Settings held. It now passes the picture's size as shown (a quarter turn
  swaps the sides) - or, for an SVG, the drawing's size on the page through
  `SetDrawingSize` - and writes with the type's stored settings at the size
  chosen. An SVG is drawn again at that size, so it stays sharp, where
  stretching the picture read at its page size blurred it.
  `UltraCanvasMediaSurface::GetSavePicture` and
  `SaveProcessed(path, size, error)` are the surface's side;
  `MediaViewerSaveTest` (new) saves a bitmap larger and in another shape, an
  SVG at twenty times its size with a hard edge, and a JPEG at two stored
  qualities.
- **`RasterizeVectorDocument`: a loaded drawing's page at any size.**
  `UltraCanvasVectorRaster.h` drew a file at a chosen size
  (`RasterizeVectorFile`) and chosen elements of an open drawing cropped to
  what they paint (`RasterizeVectorElements`), but not the whole page of an
  open drawing - what a drawing program exports as a bitmap. It now does:
  the view box drawn at the pixel size asked for (one side 0 follows the
  page's shape; both given stretch it), sharp at any size, with the
  document's background colour and `VectorRasterOptions::background` under
  it. ArtCreator's Export writes PNG and JPEG with it. `VectorRasterTest`
  checks a page twenty times its size with a hard edge, the sizes, the
  backgrounds and the refusals.
- **Windows icons read and written by the framework's own libvips plugin;
  QOI written by ours too.** An `.ico` was read and written through
  ImageMagick, which many builds lack - there the export encoder could write
  no icon at all - and QOI was written through it as well, though the
  framework's QOI plugin read it. `VipsIcoPlugin` (new, beside
  `VipsQoiLoader`) registers `icoload` / `icoload_buffer` - the file's largest
  picture, a PNG entry or a bitmap one (32-bit with alpha; 24, 8, 4 or 1-bit
  with the AND mask), cursors too - and `icosave`: one PNG picture per size
  (16, 32, 48 and 256 unless told, `ImageExportOptions::ico.sizes`), the
  picture fitted into the square on transparency and never blown up past its
  own size. `VipsQoiLoader` gains `qoisave` through the bundled codec. Both
  register when the image subsystem starts, so `new_from_file` /
  `write_to_file` - PixelFX, `UCRasterDocument::SaveToFile`, the export
  encoder - take them like any other format. `ImageIcoQoiTest` (new) round
  trips QOI, checks an icon's sizes and PNG pictures, reads bitmap icons
  made by hand (32-bit, 24-bit with a mask, a 4-bit palette) and saves both
  from a raster document.
- **A format listed as saveable can be encoded.** libvips has an AVIF and a
  HEIC saver wherever libheif is installed, but they write nothing ("heifsave:
  Unsupported compression") without libheif's AV1 / HEVC encoder.
  `VipsCanEncode` (`ImageCairo.h`) answers for those by encoding a 2 x 2
  picture once; `UltraCanvasFileLoader::GetSupportedFormats` reports a bitmap
  format's `canSave` with it, and names the framework's own plugins as the
  ICO and QOI providers. `UltraCanvasImageExportDialog` lists only formats
  that can be encoded, so it offers ICO and QOI everywhere and AVIF / HEIC
  only where they work.
- **FileLoader saves a drawing as a bitmap, and every application loads and
  saves through FileLoader.** `UltraCanvasFileLoader::SaveVectorDocument`
  writes bitmap types too: the page drawn at a size (a new overload takes a
  `FileSaveImageSize`; without one, `GetVectorDocumentPixelSize` - the page at
  96 DPI), sharp at any, with the format's save settings, a JPEG on white.
  `CanSaveImage` says whether this build writes a bitmap type, and
  `AddVectorDocumentSaveTypes` fills a Save dialog with every vector writer
  and every bitmap type it writes, and the page's size as a drawing.
  ArtCreator's Export is those calls, so it offers PNG, JPEG, WebP, AVIF,
  HEIC, TIFF, GIF, QOI, ICO and whatever else this build writes.
  `AGENTS.md` (*Core conventions*) now says it for every application: what a
  user loads or saves goes through FileLoader - its dialogs, readers and
  writers, format inventory and save settings - and only a program's own
  internal files are read and written directly; what FileLoader cannot do
  yet is added to it, not worked around. `FileLoaderDrawingSaveTest` (new)
  checks the page size, the types, a PNG drawn at ten times its size with a
  hard edge, a JPEG on white and an icon.

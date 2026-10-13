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
  and a rule parts the size (this save's) from the format's own settings.
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

- **Save dialog: the file's icon in front of the name, dragged onto a folder
  to save there.** The framework's Save dialog shows the icon of the file it
  is about to write - the type the name has with the chosen type's
  extension, drawn as the listing draws that type - between "File name:" and
  the field, and it follows the name as it is typed and the type as it is
  switched. As in the RISC OS save box, the icon is picked up and dropped on
  a folder: a folder of the listing is framed while it is under the pointer
  (over empty space, the folder the listing shows), a place or drive of the
  folder tree is highlighted, and the release opens that folder, so OK then
  saves into it. A release anywhere else, over a file or onto a folder inside
  a browsed archive changes nothing; Escape ends the drag and leaves the
  dialog open. The icon is a new element, `UltraCanvasFileDragIcon`
  (`UltraCanvasFileDragIcon.h`, in the element catalogue), for any other save
  box: it carries a name rather than a file, sends `DragEnter` / `DragOver` /
  `DragLeave` with its own mime type and no files - so nothing that takes
  files copies anything - and hands the release to `onDropped` while the
  target still shows where it lands. `UltraCanvasFilerWidget::DrawTypeIcon`
  draws the icon a listing gives a type (glyph or host icon, never a
  thumbnail), and `UltraCanvasTreeView::GetDropTargetNode` answers which row
  a drag would drop on.
- **Save settings per file format, through UltraCanvasFileLoader, and a
  Settings button after the name.** How a format is written - JPEG quality,
  PNG compression and colours, TIFF and EXR codecs, WebP / AVIF / HEIF / JPEG
  XL / JPEG 2000 quality, lossless and effort, GIF colours and dithering, RLE
  for TGA / PCX / SGI / PSD, Netpbm binary, DPX / Cineon bit depth - is now
  described per format (`UltraCanvasFileSaveSettings.h`: `FileSaveFormat`,
  `FileSaveSettingSpec`, `FileSaveSettings`) and kept for every application,
  between runs, in `SaveSettings.conf` in the UltraCanvas settings folder.
  `UltraCanvasFileLoader` gains `HasSaveSettings`, `FindSaveFormat`,
  `GetSaveSettings`, `HasStoredSaveSettings`, `SetSaveSettings`,
  `ResetSaveSettings`, `RegisterSaveFormat` (a plugin's or an application's
  own format), `ShowSaveSettingsDialog` (a checkbox, slider or list per
  setting, with Defaults) and `GetImageExportOptions` (the image writer's
  options from them). The Save dialog's Settings button opens that dialog
  for the type being saved and is greyed out for a type without settings;
  `FileDialogConfig::saveSettingsButton` / `FileDialogOptions::
  SetSaveSettingsButton(false)` leaves it out for a writer that ignores them.
  `UCRasterDocument::SaveToFile` without options writes with the user's
  settings once any are stored for the format (until then, the writer's own
  defaults as before), so UltraPaint's Save follows the button with no change
  of its own.
- **The picture's size in a bitmap type's Settings, for one save.** A caller
  that tells the Save dialog how big the picture is
  (`FileDialogOptions::SetImageSize` / `FileDialogConfig::imageWidth`,
  `imageHeight`) gets a Size row at the top of a bitmap type's Settings:
  width x height in pixels (`UltraCanvasSpinner`s), opened on the picture's
  size, and Keep proportions, which makes one side follow the other. Every
  pixel format offers it - `FileSaveFormat::bitmap`, set for the built-in
  image formats, BMP, ICO, QOI, HDR, FITS and Farbfeld included, which have no
  settings of their own and so get a dialog with the size alone. The size is
  not stored with the settings: it comes back with that save as a
  `FileSaveImageSize`, through a new `SaveFileDialog` overload whose callback
  takes it, `UltraCanvasFileDialog::GetSaveImageSize()`, and the
  `ShowSaveSettingsDialog` overload that takes the picture's size.
  `GetImageExportOptions(path, size, options)` turns it into the image
  writer's target size. UltraPaint's Save As uses it. `FileDialogTest` covers
  the store, the button, a drop on the listing and on the tree, Escape, a
  JPEG written at two stored qualities, the Size row (both sides following,
  Defaults, free sides, a type that is not a bitmap, BMP), the size coming
  back through the loader and a picture written at the size chosen.

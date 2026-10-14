- **Open and Save dialogs take their file types from FileLoader.** Applications
  kept extension lists of their own, which offered types the build could not
  read and missed ones it could. FileLoader now hands its format inventory
  to a dialog:
  - `UltraCanvasFileLoader::AddLoadTypes(opts, label, {categories})` adds one
    Open filter with every type the build reads in those categories, aliases
    included, and nothing when it reads none.
  - `AddSaveTypes(opts, category)` adds one Save filter per type the build
    writes, under the type's name. `AddImageSaveTypes` is now
    `AddSaveTypes(Bitmap)`.
  - Every save-type call puts the type that the proposed file name ends in
    first, because the dialog starts on its first type. Before, a
    `drawing.svg` opened on Adobe Illustrator, the alphabet's first vector
    type.
  - A codec an application registers itself (`RegisterMediaCodec`) joins
    both lists.
- **Text documents get their own types.** The Document category also holds
  PDF and e-books, which `LoadTextDocument` cannot open, so:
  - `AddTextDocumentLoadTypes` offers what it does read: ODT, DOCX, DOC,
    LaTeX, Markdown and plain text, first all together under "Documents",
    then each type on its own. `OpenTextDocument` offers these when it is
    given no filters.
  - `SaveTextDocument` writes ODT or DOCX, and `AddTextDocumentSaveTypes`
    offers those two.
- **VideoFX 0.14.0 reports which files it reads and writes.** `VideoFX_GetFileFormats()`
  asks the FFmpeg it is linked against:
  - Video and sound containers count as read when FFmpeg has a demuxer for
    them, and as written when it has the muxer and the usual encoders.
  - Still pictures count as read when FFmpeg's decoders open them. AVIF
    needs FFmpeg 6. FFmpeg is not asked to read HEIC: from FFmpeg 7.0 its
    demuxer opens it, but a phone's HEIC is a grid of tiles that VideoFX
    does not put together, so it would import one tile of the photo.
  - `VideoFXTest` writes every type the report says it writes and reads
    each one back.
- **VideoFX reads pictures through the host application.** With
  `VideoFX_SetPictureReader`, an application hands VideoFX its own image
  reader for chosen picture types: a cheap size from the header, and the
  upright RGBA picture. `VideoFX_Probe`, `VideoFX_ExtractFrame` and
  `VideoFX_ExtractThumbnails` then read those files through it, and so do
  image segments, overlays and slideshows. `VideoFX_GetFileFormats` reports
  the types as read.
- **QR codes save as any picture type.** The new
  `UltraCanvasQRCode::ExportToImage(path, moduleSize, error)` writes the type
  the file name ends in: any type FileLoader writes (`CanSaveImage`), such as
  ICO, TIFF or GIF, not only the five of `QRImageFormat`.
- **DemoApp: every page offers FileLoader's file types.** Eleven pages kept
  lists of their own: WYSIWYG, ODT, spreadsheet, system dialogs, QR code,
  Filer, OCR, PixelFX, AudioFX, image benchmark and vectorizer. For example,
  WYSIWYG's Insert image offered five picture types, and the vectorizer saved
  only SVG. Now:
  - The vectorizer saves the traced drawing as SVG, as any other vector type,
    or as a bitmap drawn sharp at any size.
  - The QR code page's single Save offers every picture type and SVG.
  - The QR code and image benchmark pages open and save through FileLoader's
    dialogs instead of calling the native dialogs directly.
  - The CDR page still saves only SVG and XAR, because those are its
    plugin's own two converters.

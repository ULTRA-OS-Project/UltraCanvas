- **A PDF opens as an editable drawing, on its own page format.**
  `UltraCanvasFileLoader::LoadVectorDocument` read no PDF at all - the Vector
  plugin's PDF converter only wrote - so a drawing program could not open one.
  With MuPDF in the build, `ImportPDFAsVectorDocument` (`UltraCanvasPDF.h`,
  `Plugins/Documents/UltraCanvasPDFVectorImport.cpp`) runs a page through a
  MuPDF device that builds the `VectorDocument`: `Size` and `ViewBox` are the
  page as a viewer shows it (crop box, `/Rotate`, `/UserUnit`), and paths,
  text (lines of positioned words; outlines for a font without character
  codes), pictures (JPEG and PNG streams embedded unchanged, the rest as PNG
  with their soft masks as alpha), clip paths, clipping text, transparency
  groups and tiling patterns carry over; shadings become pictures, and soft
  masks and the pages after the first are reported. `PDFVectorConverter`
  imports through it (`CanImport()` with MuPDF).
- **Open-only vector formats.** `VectorPreviewProvider::ImportExtensions`
  names formats opened for editing but never previewed as drawings, with
  `CanImportVectorExtension` / `ImportableVectorExtensions` beside
  `CanPreviewVectorExtension`; `LoadVectorDocument`, `CanLoadVectorDocument`
  and `GetVectorLoadExtensions` use them. PDF is the one: the media viewer,
  the Filer and the graphics registry go on showing a PDF through the PDF
  plugin, as a document with pages.

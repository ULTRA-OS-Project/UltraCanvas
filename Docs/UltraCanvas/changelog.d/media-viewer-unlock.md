- **The media viewer unlocks password-protected files.** A layered `.ucd`
  image (UCD 2.2) or a PDF that needs its password shows what it can without
  it - the thumbnail (nothing when private), or a note for the PDF - and an
  **Unlock** button in the info bar and the second toolbar row; Enter does the
  same. Unlock asks for the password over the viewer's window, again when it
  is not right, and then shows the file whole: the image at full size, the PDF
  with every page. Nothing asks on its own while browsing, and the password is
  not kept. `IsLocked`, `Unlock`, `UnlockWithPassword` (`MediaUnlockResult`) and
  `MediaViewerTools::unlock` let a host drive it.
  - `IPDFDocument::GetLastOpenError` and `UltraCanvasPDFView::GetLastOpenError`
    (`PDFOpenError`: `NeedsPassword`, `WrongPassword`, `Failed`) say why an open
    failed, so a protected PDF is no longer taken for a damaged one; a password
    refusal is not retried as a stream.
  - A PDF opens in the PDF view again. `ClassifyFile` asked the vector check
    first, and on a libvips with a PDF loader that took every PDF for a
    drawing: the viewer showed its first page as a flat picture, without the
    other pages or the page inventory. A PDF that fails to open no longer
    leaves the last one on show under its name.

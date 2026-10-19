- **The media viewer unlocks password-protected files.** A layered `.ucd`
  image (UCD 2.2) or a PDF that needs its password shows what it can without
  it - the thumbnail (nothing when private), or a note for the PDF - and an
  **Unlock** button in the info bar and the second toolbar row; Enter does the
  same. Unlock asks for the password over the viewer's window, again when it
  is not right, and then shows the file whole: the image at full size, the PDF
  with every page. Nothing asks on its own while browsing. `IsLocked`, `Unlock`,
  `UnlockWithPassword` (`MediaUnlockResult`) and `MediaViewerTools::unlock` let
  a host drive it.
  - The passwords Unlock is given are kept for the folder being browsed
    (zeroed-on-free, at most eight): a file browsed back to shows without
    asking, and another protected file there that one of them opens shows
    whole straight away. Opening another folder forgets them;
    `ForgetPasswords` does at once, and `SetRememberPasswords(false)` keeps
    none.
  - `IPDFDocument::GetLastOpenError` and `UltraCanvasPDFView::GetLastOpenError`
    (`PDFOpenError`: `NeedsPassword`, `WrongPassword`, `Failed`) say why an open
    failed, so a protected PDF is no longer taken for a damaged one; a password
    refusal is not retried as a stream.
  - A PDF opens in the PDF view again. `ClassifyFile` asked the vector check
    first, and on a libvips with a PDF loader that took every PDF for a
    drawing: the viewer showed its first page as a flat picture, without the
    other pages or the page inventory. A PDF that fails to open no longer
    leaves the last one on show under its name.
  - The viewer's own notes - a `*.ucd`'s header summary, a locked PDF's -
    are in a note view of their own, dark like the rest of the viewer and laid
    out as the Details panel is, instead of plain text on the white page the
    text view gives a text file.

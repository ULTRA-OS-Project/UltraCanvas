- **A rich document's picture of its own size has a size again.**
  `UCRichDocumentEditor::GetImageInfo` gives the size a picture states, and
  0 means "its own pixel size" (Word and ODF files leave some pictures so),
  which the display-free core cannot turn into a number.
  `UltraCanvasRichTextEdit::GetImageDisplaySize` answers it: the size the
  picture is laid out at, in points - the stated size, else its decoded
  pixels at one to each 1/96 inch - through the same code the layout sizes
  pictures with (`PictureLayoutSize`, now shared by the picture paragraph,
  the picture in the line and the floating picture), the picture decoded as
  the layout decodes it. `GetImageNaturalSize` gives its size at 100 %,
  whatever it states. `RichTextEditElementTest` checks a 64 x 32 px PNG of
  no stated size reads 48 x 24 pt, and a stated size reads back as stated.
- **A metafile picture of its own size is laid out at its own size.** A
  Windows metafile (EMF, WMF) is drawn at twice its size, so it stays sharp
  at 200 %; with no size stated in the document it was laid out at those
  pixels, twice as large as it is (a tiled metafile fill's tiles too). It is
  laid out, tiled and reported (`GetImageDisplaySize`, `GetImageNaturalSize`)
  at its own size now. `RichTextEditElementTest` checks a synthetic EMF of a
  2 x 1 inch frame is 144 x 72 pt and its corner handle sits there.
- **The Picture dialog shows a picture of its own size at that size.**
  `ReadPictureDialogValues` reads the size through `GetImageDisplaySize`, so
  a picture the document gives no size of opens at its own size instead of
  0 x 0, and OK on what was shown leaves it unsized. `RichTextDialogsTest`
  checks a 64 x 32 px PNG opens at 48 x 24 pt.

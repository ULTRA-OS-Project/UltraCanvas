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

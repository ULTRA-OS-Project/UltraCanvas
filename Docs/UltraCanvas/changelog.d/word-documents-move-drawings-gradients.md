- **Frames and drawings in a Word document can be moved and resized on the
  page.** A click selects a floating text frame or drawing as it does a
  picture (one behind the text leaves the text to the clicks); dragging it
  moves it, its handles resize it, and the new place and size are saved.
  - `UCRichDocumentEditor::SetFloatOffset` moves a floating object (one undo
    step); the picture calls address text frames and drawings too.
  - A resized drawing keeps the size its shapes were drawn for
    (`RichTextFrame::drawingWidthPt` / `drawingHeightPt`); its shapes scale
    with it, on screen and in the saved file.
- **Gradient fills are drawn as gradients**, not in their first colour:
  `RichShape::gradientStops`, `gradientAngleDeg`, `gradientRadial` and the
  radial centre, read from ODF `draw:gradient`, DOCX `a:gradFill`, VML
  `v:fill` and Word 97 shades, and written to ODT and DOCX.
- **VML pictures show.** A logo kept the Word 2003 way (`v:shape` with
  `v:imagedata`) is read as a picture, in the line or floating where its
  position says.

- **PowerPoint writer.** `WritePptxPresentation` (`UltraCanvasPresentationPptx.h`)
  writes a deck as a package PowerPoint, LibreOffice and the reader open:
  every shape kind at its place, turned and mirrored, with its outline, fill
  and line; text with its fonts, effects, spacing, indents, bullets and
  numbering; styled and merged table cells; pictures with their crop; charts;
  speaker notes; the theme's colours and fonts. `.pptx`, `.potx` and `.ppsx`
  (and the macro-enabled kinds) by their extension; a password encrypts it.
  A deck read and written again draws the same.
- **A document laid out as a deck.** `PptxPresentationFromRichDocument` makes a
  slide per first-level heading - the paragraphs and lists as its text, the
  tables and pictures below them - and reads a deck's own outline back as its
  slides, hidden slides and speaker notes included.
  `UltraCanvasFileLoader::SaveTextDocument` writes a `.pptx` this way, and the
  new `SavePresentation` writes a deck model; the Save dialog's document types
  offer PowerPoint.
- **`WriteDrawingMLChart`**: the chart part writer moved from the `.docx`
  writer into `UltraCanvasDrawingML.h`, shared with the PowerPoint writer; it
  now writes the axis ranges a chart states.

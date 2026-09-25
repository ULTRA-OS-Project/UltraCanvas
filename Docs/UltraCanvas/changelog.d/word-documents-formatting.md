- **Legacy Word `.doc` files open with their formatting.** The reader used to
  pull out only the text: headings, bold, lists and tables were lost, and a
  table became tab-separated lines. It now reads the binary format's
  formatting tables (character and paragraph FKPs, the stylesheet, list
  definitions and overrides, table rows): headings, bold, italic, underline,
  strike-through, super/subscript, font, size, colour, alignment, bullet and
  numbered lists, tables with a header row, column widths and cell alignment,
  hyperlinks, page breaks and embedded PNG/JPEG pictures.
  `UCWordDocumentIO::LoadDoc` is the new name; `LoadDocText` remains as a
  deprecated alias.
- **Numbered lists keep counting across the paragraphs between their items**
  in all three readers (ODT `text:continue-numbering`/`continue-list`, DOCX
  numbering instances, DOC list overrides), and honour start values and
  restarts. A document numbered "1. … note … 2." showed "1. … 1." before.
  New model pieces: `RichDocBlock::listStartNumber`, `RichListNumbering`, and
  `RichDocOrderedItemNumber()`, the one count that readers and
  `UltraCanvasRichTextEdit` share. Markdown writes the number, HTML writes
  `<li value>`, ODT writes `text:start-value`.
- **Tables keep their column widths and cell alignment**
  (`RichDocBlock::tableColumnWidths`, `RichTableCell::align`). They are read
  from ODT, DOCX and DOC, drawn by `UltraCanvasRichTextEdit`, written back by
  the ODT and DOCX writers, and kept in step when the editor inserts or
  deletes a column.
- **ODT: a space between two styled spans is no longer lost.** tinyxml2 drops
  whitespace-only text nodes, so "**bold** red" imported as "boldred". The
  reader now keeps them and applies ODF's white-space rules itself.
- **ODT: tables of contents and other indexes show their text** (the
  generated `text:index-body`), instead of being skipped.
- **`UltraCanvasRichTextEdit`: list numbers and bullets take the item's
  text size and font.** An 11 pt list was drawn with 14 pt numbers.
- **DemoApp: the OpenDocument page uses the WYSIWYG element.** Documents used
  to go through Markdown into a TextArea, which lost fonts, sizes, colours,
  alignment, list numbers and table layout. They now go straight to a
  read-only `UltraCanvasRichTextEdit`.
- New `Docs/UltraCanvas/WordProcessingFeatureCoverage.md` lists what each
  reader, the model and the view support compared with OpenOffice/LibreOffice
  Writer, and orders the remaining gaps: paragraph indents and spacing, tab
  stops, cell borders, number formats, then page layout and positioned
  frames.

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
- **Paragraph indents, spacing and tab stops.** The model gains left, right and
  first-line (or hanging) indents, space above and below, proportional line
  spacing, tab stops (left, centre, right, decimal) and a document default tab
  interval (`RichDocBlock::leftIndentPt` … `tabStops`,
  `UCRichDocument::defaultTabStopPt`). The ODT, DOCX and DOC readers read them
  from styles (with inheritance) and direct formatting. `UltraCanvasRichTextEdit`
  lays them out: a hanging indent, tab-aligned columns and each document's own
  paragraph spacing instead of a fixed gap. Both writers save them, and
  LibreOffice reads the saved files back with the same values. Enter carries
  the paragraph's geometry into the new paragraph.
- **Symbol fonts become Unicode.** Text in Symbol, Wingdings 1–3 or Webdings,
  whether stored as the font code, as U+F000 + code, as DOCX `w:sym` or as a
  DOC `sprmCSymbol`, is mapped to the character it shows (☎ ✉ ✓ α ≥ …). The
  symbol font is then dropped, so the characters draw correctly where that
  font is not installed. A letterhead's Webdings phone and e-mail icons
  showed as boxes or stray letters. The table is generated from
  dingbat-to-unicode (BSD-2-Clause) by `scripts/generate_symbol_font_map.py`.
- **ODT: font names resolve to their family.** A run said "Liberation Sans1" or
  "StarSymbol1" (the font-face declaration's key) rather than the font's
  family.
- **DOCX: a lone space between two styled runs is no longer lost**
  (`<w:t xml:space="preserve"> </w:t>`, dropped by tinyxml2), the same bug the
  ODT reader had.
- `ITextLayout::SetTabs` (Cairo) now invalidates the measured extents, so a
  layout measured before its tabs were set is re-measured.
- **Table borders and cell backgrounds.** Each cell carries its frame (width
  and colour per side) and fill (`RichTableCell::borderTop` …,
  `backgroundColor`), read from ODT cell styles, DOCX table styles, table and
  cell borders and shading, and DOC cell and table borders and shading.
  `UltraCanvasRichTextEdit` draws a document's table as the document frames it
  (`RichDocBlock::tableBordersFromDocument`), so a letterhead's borderless
  layout tables no longer show a grid. An editable view shows faint guides
  instead, as Writer does. Both writers save the frames, and LibreOffice reads
  them back identically. Tables written without document frames (from
  Markdown) now get a thin grid in ODT as they already did in DOCX, and HTML
  output carries the frames as CSS. Rows and columns added in the editor copy
  their neighbour's frame.
- **List number formats, multi-level labels and document bullets.** Ordered
  items carry a number format (1, 01, a, A, i, I, none) and a label template
  in Word's `%1.%2)` notation (`RichDocBlock::numberFormat`,
  `numberTemplate`); unordered items carry the document's bullet
  (`bulletText`). The ODT, DOCX and DOC readers fill them, with symbol-font
  bullets mapped to Unicode. `FormatListNumber()` and `RichDocListLabel()`
  spell the label ("iv.", "b)", "1.2.") and `UltraCanvasRichTextEdit` draws
  it, lining a level's text up behind its widest label. Both writers save the
  labels, and LibreOffice reads them back identically; HTML output sets
  `<ol type>`. Enter keeps an item's label format, and indenting takes the
  format of the new level. The DOCX writer now gives each list its own Word
  numbering. Before, all numbered lists in a document shared one list, so
  Word kept counting from one list into the next.
- **Highlight, fixed line heights, paragraph borders and backgrounds.** Runs
  carry a highlight colour (`RichTextRun::highlightColor`); paragraphs carry
  an exact or at-least line height (`lineHeightPt`, `lineHeightAtLeast`) and
  a frame and fill (`paragraphBorderTop` …, `paragraphBackground`). The ODT,
  DOCX and DOC readers fill them, through style inheritance.
  `UltraCanvasRichTextEdit` draws them, joining consecutive paragraphs with
  the same frame into one box and adding the frame's room to the paragraph's
  space. Both writers save them, and LibreOffice reads them back identically.
  HTML output carries them as CSS. A paragraph frame inside a table cell
  fills in the sides the cell has none on, so a letterhead's rule under the
  sender line shows.
- **DOCX writer: run properties in schema order.** `w:u` and `w:vertAlign`
  came before `w:color` and `w:sz`, which Word's schema does not allow.

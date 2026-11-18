- **ODT table cells are read and written as DOCX ones are**, so a table-heavy
  document converted to ODT reads back the same (the email signatures: 0.01 pt,
  three pages; they were six) and opens in Writer on the same pages:
  - the writer writes every line of a cell as a paragraph (an empty one states
    its font), the cell's line height or multiple, a negative table indent, a
    run of nothing but spaces as `text:s` (it was collapsed away between two
    pictures), and the font a picture in a line stands in;
  - the reader keeps a table in a cell as a nested table (it flattened it into
    the cell's lines), an empty first paragraph as a line of its own font, the
    cell's line height, and the font round a picture in a line.
  - Writer sets a paragraph of proportional line height (122%) in a cell with
    that much more room above its line, but not the first line of the cell:
    read as that room. A multiple on every line of a cell is Word's way, and
    is read from an ODT only where this writer's own settings item says so.
- **Table columns give way to their content** (`RichDocBlock::tableAutoFit`,
  Word's autofit layout, read from and written as `w:tblLayout`): a column
  narrower than the longest word or widest picture in it takes room from the
  columns that have some to spare, then from beside the table. Word's files
  have it unless they say "fixed"; other tables keep their widths.
- **A table's width in points is kept where it is a little wider than its
  column**, as in Word: squeezed to the column, its words no longer fitted
  their cells and were broken in the middle.
- **Spaces a line ends with take no room.** A line that just fitted its cell
  was broken a word early when its paragraph ended in a space.
- **No line break after a slash inside a word in Word's documents**
  (`UCRichDocument::breakAfterSlash`, false for DOCX and DOC, carried through
  ODT in a settings item of our own): "(she/her/hers)" stays whole, as in Word.
- **A table nested in a table cell is edited in place**: a double-click (or
  `EditFrame`) opens it like a text frame's words, and the cell around it
  grows and shrinks with what is typed. So is any frame standing in a line.

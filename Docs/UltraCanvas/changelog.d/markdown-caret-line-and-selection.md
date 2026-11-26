- **TextArea, Markdown view: a selection now shows over rendered lines.** It was
  painted only into a line's own text layout, so across a table it showed on
  the raw line under the caret alone, and never on a rule, a picture or a
  display formula. Each table cell now maps its text back to the source line
  (`cpMap`), so a selection or a search match paints inside the cells; a
  selected rule, table separator, picture or display formula is tinted across
  its row (`DrawBlockSelection`). `TextAreaMarkdownSelectionTest`.
- **TextArea, Markdown view: the caret's line is formatted like the lines
  around it.** It is still its source, character for character - the caret,
  editing and selection are unchanged - but a heading is at its size and bold,
  bold, italic, code, links and footnotes are in their styles, a table row
  has each `|` on its column's divider and each cell's text where the
  rendered cell's starts (the first visual line of a row that wraps), and the
  markup is drawn in the new `MarkdownHybridStyle::markupColor`.
  `MarkdownHybridStyle::styleCaretLine` (on by default) turns it off.
  `TextAreaMarkdownCaretLineTest`.
- TextArea: the empty `ApplySelectionAttributes` / `ClearSelectionAttributes`
  stubs are removed.

- **The media viewer's Details panel uses smaller text, so metadata fits a
  preview pane.** It was 12 pt with document-sized headings (1.5x and 1.3x)
  and two blank lines before each, so in UltraFiler's preview pane the
  "Image information" table alone filled the height and the metadata began
  below the fold. The panel now uses 11 pt, or 10 pt when it is narrower than
  360 px, headings only a step above the text (1.2x / 1.08x), one blank line
  between blocks, table rows 1 px apart instead of 4 and a thin rule under
  the header row instead of a whole empty line - about twice as many rows fit.
  `UltraCanvasMediaViewer::SetDetailsFontSize()` sets a size of one's own
  (0 = automatic).
- **Markdown tables in `UltraCanvasTextArea` take their spacing from the
  style.** New `MarkdownHybridStyle::tableRowPadding` (above and below the
  text in a row, default 4) and `tableSeparatorHeight` (the `| --- |` row;
  default 0 = one text line, as before). The layout also uses
  `tableCellPadding` now: it was hard-coded to 4 while the column dividers
  were drawn from the style, so a changed value put the dividers in the
  wrong place. The defaults leave every other table as it was.

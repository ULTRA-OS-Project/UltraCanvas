- **A paragraph in a table cell keeps its own proportional line height**
  (`RichTextRun::lineSpacing`, new): a card's heading set at 70% or 90% of its
  font's line, in a cell whose first paragraph is not, had its lines a full
  line apart. Read from ODT (`fo:line-height` in per cent) and DOCX
  (`w:line` with "auto") for every paragraph of a cell, written by both, and
  laid out by `UltraCanvasRichTextEdit`. An ODT written by Word sets such a
  height on every line of a cell, as Word does; Writer's own keep Writer's way
  (more room above the paragraph, but not above a cell's first line).

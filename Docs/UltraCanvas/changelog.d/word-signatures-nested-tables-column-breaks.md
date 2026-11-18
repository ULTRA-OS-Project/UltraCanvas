- **A Word template of twenty email signatures (tables in two columns) came
  out on four pages with two signatures half empty**; it is three pages now,
  every signature in Word's page and column. What was missing:
  - **A table inside a table cell** was dropped with everything in it. The
    DOCX reader keeps it as a frame of its own standing in the cell's line
    (`RichTextFrame::nestedTable`), `UltraCanvasRichTextEdit` lays it out and
    draws it there, its words are part of the plain text, and the DOCX and ODT
    writers write it back as a table in the cell. The empty paragraph a cell
    must end with after such a table takes no line, as in Word.
  - **Column breaks** were read as line breaks. `RichDocBlock::columnBreakBefore`
    starts a block in the next column (the next page from the last one): Word's
    `w:br w:type="column"`, ODF's `fo:break-before="column"`, read, written and
    laid out.
  - **A cell's lines of a set height or multiple** (`RichTableCell::lineHeightPt`
    / `lineHeightAtLeast` / `lineSpacing`): Word's `w:spacing w:line` with
    "exact", "atLeast" and "auto".
  - **An empty first paragraph of a cell, an empty last line, and the line a
    break at a paragraph's end leaves** are lines of their paragraph mark's
    font (a 2 pt paragraph sets two parts a hair apart); they were lines of
    the default font, or lost.
  - **A table left of its column** (a negative `w:tblInd`), and a column as
    narrow as its document states (it was at least 18 pt).
  - **Pictures are stretched to the size the document gives them**; they kept
    their own proportions inside it.
  - A picture in a line carries its run's font: one lower than a line of that
    font stands in such a line. A picture alone in its line has no descent
    under it.
  - The DOCX writer writes every line of a cell as a paragraph (an empty one
    keeps its font in its mark), so a saved document reads back the same.
- **A drawing Word sets in the line of a body paragraph** (`wp:inline`, ODF's
  `as-char`) stands in that line (wrap `Inline`), where the paragraph's
  alignment puts it; it was a float at the left.
- **A turned picture in the line** (`RichTextRun::imageRotationDeg`) is drawn
  turned, in a line as tall as the turned picture; read and written by the
  DOCX reader and writer.
- **Pictures and drawings behind or in front of the text in a cell of a
  header's or footer's table** float from that cell, as in the body.
- **Print and PDF export are on white paper.** The page was painted in the
  screen theme's page colour - a light grey by default, a dark sheet with
  light text under a dark theme. Under a dark theme the output also takes the
  light style's inks.
- `DocumentRenderProbe` lists each table cell's lines with
  `DOCUMENT_PROBE_VERBOSE=1`.

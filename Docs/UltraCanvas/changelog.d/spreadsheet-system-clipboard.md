- **The spreadsheet's Copy and Paste never reached the system clipboard.**
  Copy filled the grid's own buffer and nothing else, so cells could not be
  pasted into another program, nor another program's cells into the grid.
  Now:
  - Copy and Cut also put the cells on the system clipboard, as
    tab-separated text of the shown values and as an HTML table (bold,
    italic and right-aligned numbers kept): `RangeAsText`, `RangeAsHtml`.
  - Paste takes the grid's own copy while the clipboard still holds it, and
    otherwise another program's HTML table, read with `HTML::Parser`
    (spanned cells keep the cells they cover free), or its tab-, comma- or
    semicolon-separated text: `PasteHtml`, `PasteText`. Each value is read
    as if typed: numbers, percentages, `=` formulas. The pasted cells are
    selected.
- **A pasted formula lost its formula.** Paste (and Paste Special *All*)
  wrote a formula cell's last result under the Formula type, which showed
  `#NAME?`. A copied formula is now pasted as a formula with its relative
  references moved (`=B2*2` from B3 to B8 is `=B7*2`); a cut one keeps
  them. *Values* pastes the result as the number or text it is
  (`SpreadsheetSheet::PasteCells` takes where the cells came from).
- **A paste could not be undone.** Every paste, the grid's own or another
  program's, is now one undo step.

  Covered by `SpreadsheetClipboardTest` (a stand-in clipboard backend), and
  by hand on X11: `xclip` reads both flavours, and an HTML table put there
  by another program pastes with its formula working.

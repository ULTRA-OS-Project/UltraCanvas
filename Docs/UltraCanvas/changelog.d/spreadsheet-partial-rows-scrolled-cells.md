- **Spreadsheet: the grid's edges are drawn cleanly.** The row header of the
  last, partly visible row was drawn whole across the horizontal scrollbar's
  row while its cells were cut off, so the bottom of the grid looked broken;
  row and column headers are now clipped at the grid edge together with their
  cells, and the box under the row headers beside the horizontal scrollbar is
  filled. A selected cell scrolled out of view (A1 after scrolling to column
  D) was still painted over the first visible column, with its selection
  border; cells before the scroll position now lie outside the cell area and
  are clipped, as are the clipboard outline, the auto-fill handle and the
  in-cell editor.
- **Spreadsheet: the "+" button beside the sheet tabs adds a sheet.** It was
  drawn but never handled a click. It now appends a sheet with the next free
  name ("Sheet2", ...) and switches to it. Tabs and the button are laid out
  once for drawing and clicking, so a click on a tab always selects the tab
  drawn there, and the button stays in view when many tabs do not fit.
- **Spreadsheet: every sheet tab can be reached.** The box left of the tabs
  was an empty placeholder, so with more sheets than fit the rest could not be
  clicked. It now holds ◀ ▶ buttons that scroll the strip one tab at a time
  (greyed when there is nothing further that way), and switching sheets in
  code or with "+" scrolls the active tab into view. Tab widths are measured
  in the tab font instead of guessed at 8 px per byte, so long and non-Latin
  names fit; a name wider than the whole strip is clipped inside its tab.
- **Spreadsheet: Sort in the right-click menu.** A Sort submenu sorts the
  selected block ascending or descending by the right-clicked cell's column,
  or by any column of the selection through "Ascending by" / "Descending by"
  submenus - the same sort as the header buttons, offered for the same
  selections, with the same warning before moving formulas.
  `RequestSortSelectionByColumn()` (formerly the private `RequestHeaderSort`)
  and `IsSelectionSortable()` are public.
- **Spreadsheet: a formula's ranges are highlighted for every formula cell.**
  Selecting a formula cell outlines the ranges it reads, but only once the
  engine had parsed the formula - which it does on first evaluation - so in a
  loaded workbook `=SUM(D2:D8)` showed no outline while `=SUM(C2:C8)` beside
  it did. The highlight parses an unparsed formula itself.
- **Spreadsheet: an "fx" function menu in the formula bar.** A button at the
  bar's right end lists all formula functions - a submenu per category and
  one A to Z - with each one's description. Picking one inserts it at the
  caret while editing, wraps the cell's formula (=A1/B1 becomes
  =ROUND(A1/B1)), or starts =NAME(); the caret is left inside the call.
  `InsertFunction()`, `BuildSpreadsheetFunctionMenuItems()` and
  `SetFunctionMenuEnabled()` are public.
- **Spreadsheet: the formula bar no longer shows its text twice while
  editing.** The bar drew the text and the editor drew it again 10 px to the
  right, so a formula read "==SUM(...)"; the bar now leaves the text to the
  editor, which sits where the text was.
- **Spreadsheet: every function names its arguments, and a hint shows them
  while typing.** Most of the 150 built-in functions were registered without
  argument names, so they could only be listed by a one-line description. All
  now carry the names Excel uses: the fx menu lists `ROUND(number,
  num_digits)`, `IF(logical_test, value_if_true, [value_if_false])`, ...; and
  while a formula is typed, a hint under the editor shows the call the caret
  is in with the current argument in bold. `GetSignature()`,
  `GetSignatureArguments()` and `FindFormulaCallAtCaret()` are public.

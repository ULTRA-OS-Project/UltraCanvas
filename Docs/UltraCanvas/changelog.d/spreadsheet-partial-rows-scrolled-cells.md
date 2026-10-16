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

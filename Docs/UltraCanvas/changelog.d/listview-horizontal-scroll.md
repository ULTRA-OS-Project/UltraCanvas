- **List view: a horizontal scrollbar when the columns are wider than the
  view.** A multi-column `UltraCanvasListView` whose columns did not fit cut
  the rightmost ones off with no way to reach them (UltraDBAdmin's browse
  grid on a wide table). Now a horizontal scrollbar shows along the bottom,
  the header and the cells scroll sideways together, Shift+wheel moves them,
  and when both scrollbars show each stops short of the other's track.
  `ScrollToColumn` / `EnsureColumnVisible` / `GetHorizontalScrollOffset` do
  it from code; `GetCellRect`, `GetColumnAt` and `GetHeaderColumnAt` account
  for the scroll; `ScrollMetrics` gains the sideways figures.
  - **The Left / Right keys scroll the columns, and stay with the view.**
    They bubbled up unhandled, so in a table inside a tabbed container they
    switched the tab instead of moving the table. The view now takes them,
    whether or not the columns fit.

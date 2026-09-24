- **Spreadsheet: sort a selected block from its column headers.** Select two
  or more rows and each column header over the block shows an up/down sort
  button, like the ListView's sortable headers. Clicking it sorts only the
  selected rows by that column; the other selected columns move with it, so
  every row stays together, and the title and totals rows outside the block
  stay where they are. Clicking the same button again reverses the order, the
  header shows the direction, and Ctrl+Z undoes the sort.
  - When the block contains formulas, the button first shows an OK/Cancel
    warning: sorting moves formulas with their rows but does not rewrite
    their references, so a row formula can end up reading another row.
    `SetSortFormulaWarningEnabled(false)` turns it off.
  - New API: `SortSelectionByColumn(column, order)`,
    `SetHeaderSortEnabled` / `IsHeaderSortEnabled`, `GetHeaderSortColumn` /
    `GetHeaderSortAscending`, `SetSortFormulaWarningEnabled`, the
    `onSelectionSorted` callback, and `SpreadsheetSheet::CountFormulaCells`.
  - DemoApp: the Spreadsheet page's hint and status line explain and report
    the header sort.
  - New `SpreadsheetRangeSortTest` covers the block sort.

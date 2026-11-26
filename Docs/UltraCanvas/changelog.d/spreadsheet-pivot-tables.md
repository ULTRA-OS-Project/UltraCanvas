- **Pivot tables in spreadsheets** (`UltraCanvasSpreadsheetPivot.h`). A
  `SpreadsheetPivotTable` summarises a block of cells whose first row names
  its fields: row and column fields (nested, each with its order, subtotals,
  hidden items and a grouping - dates by years, quarters or months, numbers
  by a step), filter fields above the table, and value fields summed,
  counted, averaged, as min, max, product, count of numbers, standard
  deviation or variance (sample or population), shown as they are or as a
  share of the grand total, of the row or of the column; grand totals at the
  bottom and the right on or off, several values side by side or one under
  the other. The model is display-free: `ComputeSpreadsheetPivot` lays it
  out as cells, `RefreshSpreadsheetPivot` writes them onto a sheet with a
  header style (and clears where it stood), `SpreadsheetPivotDrillDownRows`
  gives the source rows behind a value.
- **On the grid.** A sheet keeps the pivots standing on it
  (`SpreadsheetSheet::GetPivotTables`); `InsertPivotTable`, `SetPivotTable`,
  `RemovePivotTable`, `RefreshPivotTable(s)`, `SetPivotTableSource`. A
  pivot's cells are read-only (typing, pasting, cutting and clearing them is
  refused with a message), a change to its source refreshes it before the
  grid draws and before it saves (`RefreshChangedPivotTables`), and a
  double-click on a value opens a sheet of the rows behind it
  (`DrillDownPivotCell`, `onPivotDrillDown`) - on any other of its cells, its
  field list. The field list is an element, `UltraCanvasSpreadsheetPivotPanel`
  (fields, rows, columns, values and filters, each field's settings and
  items), in a dialog (`ShowSpreadsheetPivotDialog`, the grid's
  `ShowInsertPivotTableDialog` / `ShowPivotTableDialog`) or docked beside
  the grid; `ShowPivotSourceDialog` asks for a new source.
- **.xlsx and .ods keep them.** .xlsx: a pivot cache definition per pivot
  (`xl/pivotCache/pivotCacheDefinitionN.xml`, its source, its fields'
  values and groupings, `refreshOnLoad`), its records, a pivot table part
  (`xl/pivotTables/pivotTableN.xml`) with their relationships and content
  types, and the workbook's `<pivotCaches>`; .ods: `table:data-pilot-tables`
  with a `table:data-pilot-table` per pivot (`table:source-cell-range`,
  `table:data-pilot-field`s with their levels, members, sort order, groups
  and field references). Both are read back - Excel's and LibreOffice's own
  too; what the model cannot represent (a calculated field, a grouping that
  adds fields, a source outside the workbook) keeps the file's cells and
  says why (`unsupportedReason`). `SpreadsheetPivotTest` checks the sums
  against hand-worked ones and the files through LibreOffice, including
  pivots LibreOffice made itself.
- `SpreadsheetSheet::NotifyCellsChanged`: cells written straight through
  `GetOrCreateCell` reach the formula engine and the grid.

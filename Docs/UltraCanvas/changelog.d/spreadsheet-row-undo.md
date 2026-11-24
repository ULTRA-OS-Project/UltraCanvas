- **Undoing an inserted or deleted spreadsheet row or column erased the
  sheet.** The four operations recorded a placeholder step with no saved
  cells. Undo treated it as a cell edit, cleared the sheet's whole used range
  and put nothing back.
  - Each insert or delete (`InsertRowsAt`, `DeleteRowsAt`, `InsertColumnsAt`,
    `DeleteColumnsAt`) is now one undo step ("Insert 2 rows") that keeps:
    - the sheet as it was (a copy that keeps its identity);
    - the formulas it rewrote on other sheets;
    - the workbook's names.
  - Undo and redo swap that state with the current one. Undo puts back the
    cells, row and column sizes, merges, deleted cells and every formula as
    they were, and redo does the change again.
  - `RecordStructureChange`, which recorded the placeholder, is gone.
  - New test: `SpreadsheetRowUndoTest`.

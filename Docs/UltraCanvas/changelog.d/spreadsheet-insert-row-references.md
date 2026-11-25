- **Inserting or deleting spreadsheet rows or columns left formulas reading
  the wrong cells.** The cells moved but their formulas' references did not:
  `=A1*3` in B1 became `=A1*3` in B2 after a row was inserted above. Even
  `RecalculateAll` could not put that right. `SpreadsheetSheet::InsertRows` /
  `DeleteRows` / `InsertColumns` / `DeleteColumns` now rewrite the sheet's
  formulas:
  - a reference at or past the insertion moves, `$`-anchored ones too;
  - a range across an insertion grows, and one across a deletion shrinks;
  - a reference wholly inside deleted rows becomes `#REF!`.

  The grid's `InsertRowsAt` / `DeleteRowsAt` / `InsertColumnsAt` /
  `DeleteColumnsAt` do the same for other sheets' formulas that name the
  sheet (`Data.A3`, `'Q 1'!B2:C3`), and for workbook names on it. The rewrite
  keeps the formula's own text otherwise. The old `SpreadsheetFormula::AdjustReferences`
  shifted every reference by a fixed offset and rebuilt the text with every
  operation in parentheses; nothing calls it. New:
  `MoveFormulaReferencesForRowsColumns`, `SpreadsheetSheet::MoveFormulaReferences`.
  Test: `SpreadsheetMoveReferencesTest`.

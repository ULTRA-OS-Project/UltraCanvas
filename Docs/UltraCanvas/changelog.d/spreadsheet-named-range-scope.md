- **Spreadsheet named ranges ignored their scope.** Names were stored by
  their text alone and looked up the same way from every sheet:
  - a name scoped to one sheet was used from any other;
  - a scoped name whose range named no sheet read the cells of whichever
    sheet the formula was on;
  - the same name scoped to two sheets ("Total" on each month, common in
    Excel files) kept only the last one;
  - renaming a sheet left the names scoped to it, or covering it, behind.

  Names are now kept one per scope and name, matched without regard to case.
  A formula sees the name scoped to its own sheet, else the workbook's
  (`ResolveNamedRange`), and a scoped name covers its own sheet unless its
  range says otherwise. Renaming a sheet, and inserting or deleting rows or
  columns there, carries its names along. `HasNamedRange`,
  `RemoveNamedRange` and `GetNamedRange` take an optional scope.
- **A spreadsheet formula naming a range that does not exist read cell A1.**
  The lookup's "not found" was an empty `CellRange`, which is A1:A1 and
  valid. It now gives `#NAME?`.
- `SpreadsheetFormula::AdjustReferences` and `GetAdjustedText` are removed.
  Nothing called them. They moved every reference by the same offset
  wherever rows were inserted, and rebuilt the text with every operation in
  parentheses. `ShiftFormulaReferences` (copying) and
  `MoveFormulaReferencesForRowsColumns` (inserting and deleting) do those
  jobs.
- New test: `SpreadsheetNamedRangeTest`.

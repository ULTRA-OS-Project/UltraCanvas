- **Spreadsheet: undo after a sheet is renamed or deleted wrote into the open
  sheet.** An undo step finds its sheet by name, and when none had that name
  it fell back to the active sheet - so after renaming "Sales", undoing an
  edit made on it cleared the same cells on whatever sheet was open, and the
  same after deleting a sheet. Renaming now carries the history along (the
  steps name the new name, and formulas they put back refer to it); deleting
  drops the sheet's steps; and undo never falls back to another sheet.
- **Spreadsheet: deleting a sheet makes references to it `#REF!` in the
  formula text,** as Excel does: `=Gone.E5*2` becomes `=#REF!*2`,
  `=SUM(Gone.A1:B3)` becomes `=SUM(#REF!)`. They used to keep the deleted
  sheet's name, and a sheet later added under that name would quietly have
  been read in its place (`RemoveSheetFromFormula`).

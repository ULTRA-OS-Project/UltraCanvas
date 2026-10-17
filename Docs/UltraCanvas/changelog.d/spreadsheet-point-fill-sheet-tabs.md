- **Spreadsheet: clicking a cell while typing a formula types its address.**
  Typing `=SUM(` and dragging over C2:C8 used to end the edit and select the
  cells; it now types `C2:C8` at the caret and the edit goes on, as in Excel
  and LibreOffice Calc. A click types one address, a drag a range, Shift+click
  grows the reference just typed, Ctrl+click adds another after a `,`, and a
  further click replaces it. It applies wherever a reference may go - after
  `=`, `(`, a separator, an operator or a `:` - and the pointer turns into a
  crosshair there; elsewhere a click ends the edit as before. The formula's
  references are now outlined while editing in the formula bar too
  (`CanPointReference`, `FormulaAcceptsReferenceAt`).
- **Spreadsheet: more of the fill handle.** Ctrl held on release copies the
  selection instead of continuing its series (`AutoFillMode::Copy`); a
  double-click on the handle fills down as far as the neighbouring column's
  data goes (`AutoFillSelectionToDataEnd`, `GetAutoFillDownEnd`); Ctrl+D and
  Ctrl+R fill down and right (`FillSelectionDown`, `FillSelectionRight`). A
  fill, selection or pointing drag past the grid's edge now scrolls it.
- **Spreadsheet: a sheet tab menu.** Right-clicking a tab offers Insert sheet
  before / after, Duplicate, Rename..., Move left / right / to start / to end
  and Delete (asking first when the sheet has cells; never the last sheet); a
  double-click on a tab renames it. `RenameSheet` now refuses names the file
  formats do not allow or another sheet has (`ValidateSheetName`,
  `CheckSheetName`) and rewrites every formula naming the sheet
  (`RenameSheetInFormula`); removing a sheet recalculates, so formulas naming
  it show `#REF!`. `onSheetTabContextMenu` and `SetSheetTabMenuEnabled` let an
  application replace or turn off the menu.

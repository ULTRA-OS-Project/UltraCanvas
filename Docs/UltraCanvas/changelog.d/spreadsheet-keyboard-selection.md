- **Moving with the keyboard in a spreadsheet left the old cell selected.**
  The arrows, Enter, Tab, Home, End, Page Up / Down and Ctrl+arrow moved
  only the active cell, so the highlight stayed behind and the next Bold,
  number format or Delete went to the cell just left. Each of them now
  selects the cell it lands on (`MoveActiveCell` and the `MoveTo...`
  functions), and `onSelectionChange` reports it.
- **Shift+arrow did not extend the selection.** It now grows or shrinks the
  range at the corner away from the active cell, which stays where it is
  (`UltraCanvasSpreadsheet::ExtendSelectionBy`).

  Covered by `SpreadsheetKeyboardMoveTest`.

- **Spreadsheet: deleting a sheet can be undone.** `RemoveSheet` (and the
  sheet tab menu's Delete) is now one undo step: Ctrl+Z puts the sheet back at
  its place with its cells, its own undo history, and the formulas the delete
  had turned to `#REF!` naming it again; Ctrl+Y deletes it again. A sheet that
  comes back to find its name taken returns as "Name (2)". Undo steps now find
  their sheet by a fixed id (`SpreadsheetSheet::GetId`) instead of its name,
  so a deleted sheet's history waits for it instead of being thrown away, and
  can never reach a different sheet that took the name.

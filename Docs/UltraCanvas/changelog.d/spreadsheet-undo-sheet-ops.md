- **Spreadsheet: inserting, duplicating, renaming and moving a sheet can be
  undone.** Each is now one undo step, like deleting: undo takes an inserted
  or duplicated sheet away again (redo brings it back with its cells), gives
  a renamed sheet its old name - the formulas naming it follow - and moves a
  moved sheet back. The steps find the sheet by its id, as deleting does.
- **Spreadsheet: loading a file starts a fresh undo history.** The loaders
  clear the workbook with `RemoveSheet`, which since sheet deletion became
  undoable recorded a "Delete sheet" step - with a full copy of the sheet -
  for every sheet of the document being replaced, so Ctrl+Z after opening a
  file brought the old document's sheets back into the new one. Loads now run
  without recording and clear the history when they succeed.

- **Files can be dropped onto a Kanban board.** `UltraCanvasKanbanBoardElement`
  gains `onFilesDropped(files, columnId, laneId)`: files dragged in from a file
  manager or the desktop land on the column under the pointer, and while the
  callback is set the column the files would join is highlighted during the
  drag. Unset, drops pass on to the parent elements as before. Two public
  helpers go with it: `ColumnIdAt(point)`, the column under a point, and
  `BeginColumnRename(columnId, startEmpty)`, the inline rename that
  double-clicking a header opens, for a host's own "add column" button.
- **`ScrollToColumn(columnId)`** scrolls a board too wide for its columns
  just far enough to show one, also right after `AddColumn` (the scroll is
  worked out by the next layout). A column opened for renaming - the board's
  own "+" and `BeginColumnRename` - is scrolled into view the same way.
- **Typing into a Kanban board's column rename and card editor works.** Both
  read characters only from `TextInput` events, which no platform backend
  sends: every one puts the typed text on the `KeyDown`. So a new column kept
  "New Column", and a card added with "+ Add card" could not be given a
  title. The keys that type now go into the field; Tab, Delete and
  Ctrl/Alt/Cmd shortcuts still do not. (UltraDesktop's Sticker board uses
  all of the above.)

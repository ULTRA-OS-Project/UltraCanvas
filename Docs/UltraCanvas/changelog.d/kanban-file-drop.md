- **Files can be dropped onto a Kanban board.** `UltraCanvasKanbanBoardElement`
  gains `onFilesDropped(files, columnId, laneId)`: files dragged in from a file
  manager or the desktop land on the column under the pointer, and while the
  callback is set the column the files would join is highlighted during the
  drag. Unset, drops pass on to the parent elements as before. Two public
  helpers go with it: `ColumnIdAt(point)`, the column under a point, and
  `BeginColumnRename(columnId, startEmpty)`, the inline rename that
  double-clicking a header opens, for a host's own "add column" button
  (UltraDesktop's Sticker board uses all three).

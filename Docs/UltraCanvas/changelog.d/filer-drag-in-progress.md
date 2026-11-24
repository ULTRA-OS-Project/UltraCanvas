- **FilerWidget: `IsDragInProgress()`.** True while a selection rectangle is
  drawn or entries are dragged. Letting the rectangle go now fires
  `onSelectionChanged` once more, so a host that waits for the drag to end
  (UltraFiler's preview pane) sees the final selection.

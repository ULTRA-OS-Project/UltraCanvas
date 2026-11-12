- **TextArea: zooming out at the end of a long document left the view blank.**
  A smaller font shortened the content, but `verticalScrollOffset` kept its old
  value past the new end: no line was drawn and the scrollbar thumb left its
  track. `SetFont` / `SetFontSize` now remember the line at the top of the view
  and `Render()` puts it back there once the layouts are rebuilt; after every
  relayout the offset is clamped to `MaxVerticalScroll()`.

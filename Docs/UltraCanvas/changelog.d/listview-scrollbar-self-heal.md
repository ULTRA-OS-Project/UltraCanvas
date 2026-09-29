- **`UltraCanvasListView` checks its scrollbar before it paints.** The
  scrollbar's range, visibility and bounds were computed only when the
  model changed or the element was arranged; a paint that came between
  the two (a model that grew while the element was still at an old size)
  drew the rows with no scrollbar until the next arrange. `Render` now
  recomputes what `UpdateScrollbar` would give for the current rows and
  bounds, and when the scrollbar disagrees it logs one
  `scrollbar was stale at paint` line to the debug stream and refreshes it
  before drawing. `GetScrollMetrics()` returns the same numbers (rows, row
  height, content and viewport height, range, offset, whether the scrollbar
  shows and where) for diagnostics and tests.

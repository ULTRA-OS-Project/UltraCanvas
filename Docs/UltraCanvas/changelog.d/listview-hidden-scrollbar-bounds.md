- **`UltraCanvasListView` clears the scrollbar's bounds when it hides it.**
  A hidden scrollbar kept the rectangle of its last visible layout, computed
  for an earlier size - a list arranged at its parent's full width before the
  split pane sized it reported its bar at x 1251 with a negative height while
  the list was 470 wide. Nothing painted it while hidden, but the stale
  rectangle was what `GetScrollMetrics()` and the layout log showed, and
  what a paint would use if the bar were shown again without a fresh
  `UpdateScrollbar`. `UpdateScrollbar` now sets the hidden bar's bounds to
  0,0 0x0, so a hidden bar has no position at all.

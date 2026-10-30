- **Widening a Details column no longer narrows the next one.** Dragging the
  splitter on a column's right edge in `UltraCanvasFilerWidget`'s Details view
  now resizes that column alone: the columns after it move along with their
  widths intact (`FilerColumnResizeMode::PushFollowing`, the new default).
  A table pushed wider than the view gets a horizontal scrollbar under the
  rows - the header scrolls with them, Shift+wheel or a sideways wheel scrolls
  it, and the hover icon menu stays at the visible right edge - and the last
  column gets a splitter of its own. The Name column still fills the view
  until the first drag pins it; `ResetDetailsColumnWidths()` frees it again.
  The old split-pane behaviour, where the width a column gains is taken from
  its neighbour, is `SetDetailsColumnResizeMode(FilerColumnResizeMode::SplitWithNeighbour)`.

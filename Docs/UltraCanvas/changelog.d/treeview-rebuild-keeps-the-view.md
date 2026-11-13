- **A tree view built again from its source no longer throws the view back to
  the first row.** `UltraCanvasTreeView::SetRootNode` empties the tree before
  the new rows are added, and the measure that followed each `AddNode` found
  no content at all and clamped the scroll offset to 0 - so an application
  that rebuilds its tree on a refresh (a folder rescan, a device list, a
  reload) left the user looking at the top of a tree they had scrolled
  through, with the row they had selected often off screen. The rebuild is now
  wrapped: `BeginRebuild()` / `EndRebuild()`, or the `TreeRebuildScope` that
  calls both for a block, hold the view while the rows are replaced. The
  scroll position comes back as it was, the row that was selected returns to
  the same height on screen even when rows were added above it, and a
  selection that moved on during the rebuild - another account, a folder that
  is gone - is scrolled into view instead. The tree is also measured once at
  the end rather than after every row, so a rebuild of *n* rows is no longer
  *n* measures of the whole tree. Nothing changes for a caller that does not
  wrap its rebuild.

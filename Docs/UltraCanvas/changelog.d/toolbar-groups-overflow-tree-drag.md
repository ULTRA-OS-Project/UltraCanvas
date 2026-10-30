- **A toolbar's `groupSpacing` is read.** It is the gap between two groups,
  across a separator, with the line in the middle; it was declared, set by
  `ToolbarAppearance::Ribbon()` and never used. The default (8 = two 4 px item
  gaps) is a separator as it always looked, and a bar that only tightens its
  items does not change.
- **`ToolbarOverflowMode::Menu` works.** It did nothing: its two functions
  were empty `TODO` stubs. The items that do not fit are taken off the end of
  the bar into a menu behind a chevron button, and come back when there is
  room: a button is an entry with its icon and its tooltip's shortcut, a
  toggle button a check, a tool group or a dropdown a submenu
  (`GetOverflowItemIds`, `GetOverflowMenuItems`, `OpenOverflowMenu`).
  `UltraCanvasToolGroupButton::SelectItem` picks a tool as a click in its bar
  does; `UltraCanvasButton::GetIconImage` returns the icon a button shows.
- **Tree rows can be dragged onto other rows.** `UltraCanvasTreeView`'s
  `onNodeDragDrop` was declared from the first version but never fired.
  `SetNodeDragEnabled(true)` turns dragging on (off by default): past a
  six-pixel threshold the row under the pointer that `onNodeDragAccept` takes
  is highlighted, the tree scrolls past its edges, and the release fires
  `onNodeDragDrop(dragged, target)`. Escape cancels. Without
  `onNodeDragAccept`, a row cannot be dropped on itself or into its own
  subtree.

- **The list view shows which row has the keyboard focus.** It worked out the
  focused row and handed it to the delegate as `ListItemStyleOption::isFocused`,
  but nothing drew it, so a keyboard user could not tell the row the arrow
  keys go on from apart from one that is merely selected. While the list holds
  the focus, that row now carries a dashed one-pixel ring just inside its
  edges, the way the tree view's does; it goes when the focus leaves and the
  selection stays. The view draws it over the delegate, once across the row,
  so custom delegates and multi-column rows get one ring. Its colour is
  `ListViewStyle::focusRingOnSelectionColor` on a selected row (the theme's
  text-on-accent) and the selection colour on any other;
  `UltraCanvasListView::SetFocusRingColor` sets one for every row. A list that
  never takes the focus (a dropdown's open list) shows none.
- **ListViewFocusRingTest** checks it on real pixels under Xvfb - the ring on
  all four sides of the focused row while focused and on none after, one ring
  across a two-column row, the automatic and the set colour - and CI's
  virtual-display step runs it next to TreeViewKeyboardTest, whose pixel
  helpers (`RowShows`, `RingSides`) moved to `Tests/DisplayTestSupport.h` for
  both.

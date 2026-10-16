- **The tree view's "move to the top" button stays in its corner.** It used to
  slide up as the view neared the end of the list, so it would not cover the
  last rows; that moved it under the pointer and is gone, together with
  `TreeScrollToTopStyle::keepClearRows`. The button now always rests in the
  bottom-right corner of the rows.
- **The tree view has a "go to the selected entry" button.** Directly above the
  top button's corner slot, a second floating button - a dot with an arrow
  pointing at it - shows while the selected row is not fully on screen
  (scrolled away, or under a collapsed parent). A click opens the collapsed
  parents and brings the row to the middle of the view, animated with smooth
  scrolling; the selection does not change. On by default,
  `SetShowScrollToSelectionButton(false)` (also on `TreeViewBuilder`) switches
  it off, `ScrollToSelection()` does the same from code, and it shares
  `TreeScrollToTopStyle` with the top button. `UltraCanvasColumnsTreeView`
  inherits it. The DemoApp's multi-select tree has a check box for it, and
  `Tests/TreeViewScrollButtonsTest.cpp` covers both buttons.

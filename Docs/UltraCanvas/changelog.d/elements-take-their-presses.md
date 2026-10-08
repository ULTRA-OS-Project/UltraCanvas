- **Elements take the presses they act on.** Since a press nobody took climbs
  to the elements around it, an element that acted on a press and then
  reported it as untaken handed the same press to its parents. These did:
  the kanban board (a press on the background took the focus and cleared the
  selection, firing `onSelectionChanged`), the Gantt chart (a click off the
  rows cleared the selection and started a pan), the block diagram and the
  Gource tree (deselect on the empty canvas), the tree map (deselect on a
  background double-click), the arc and adjacency diagrams (the press of a
  click whose release selects, and the release that deselects), the gradient
  editor (a press off its stops took the focus) and the video player (the
  release that ends a seek or volume drag). Each now returns `true` there.
  The curve editor and the PDF view took the focus for a middle press and
  then let it go on; they now leave a button they have no use for alone, the
  focus included. The rich-text editor's right press without a menu still
  goes on, on purpose, so a surrounding pane's menu can open - as a button's
  does; the code now says so.
- **The arc diagram can be clicked where it is drawn.** Its hover and click
  hit test subtracted the diagram's position in its parent from a pointer
  that was already local, so a diagram away from its parent's top left
  showed the wrong node's tooltip and selected the wrong node, or none.
- **The video player's seek and volume drags capture the mouse**, so their
  release arrives even off the player; before, a release outside it left the
  scrub running.
- **A movable toolbar is dragged by its own surface, and stays where it is
  dropped.** With `ToolbarDragMode::Movable` / `Both` any left press the bar
  got started a drag - with presses climbing, a press on a label, separator
  or disabled button would carry the whole bar off. The drag also tracked the
  pointer in the bar's own coordinates while moving the bar (the reference
  moved with every step), was not captured (it stuck once the pointer left
  the bar), and moved only the laid-out bounds, which the next layout pass
  put back. It now starts only from the bar's own surface, follows the
  pointer in window coordinates with the mouse captured, and sets the bar's
  CSS position (`SetElementAbsolutePosition`). `BeginDrag` / `UpdateDrag`
  take window coordinates. Docs: `UltraCanvasToolbarExamples.md`.
- The file view's guard against presses on its own elements now hit-tests
  with `FindElementAtPoint`, as the window does (scrolling, clipping), instead
  of a bounds check of its own.
- Tests: `ElementPressTakenTest` (a window under Xvfb; it skips without a
  display) - 11 of its 15 checks fail on the previous code.

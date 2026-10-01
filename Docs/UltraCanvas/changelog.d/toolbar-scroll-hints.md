- **`UltraCanvasToolbar` 1.6.0: a scrolling toolbar shows where it
  continues.** In `ToolbarOverflowMode::Scroll` the items past the edge were
  simply cut off, so a bar with more items than room looked complete and the
  wheel was the only way to find the rest - and a window manager that takes
  the wheel over a desktop window (openbox does, by default) left no way at
  all. While there is something past an edge, a 12 px strip in the toolbar's
  colour now covers that edge with a chevron pointing the way the items go,
  and a click on it scrolls a page. Nothing is drawn while the items fit;
  `SetScrollHints(false)` turns it off. The hint shares the window-level
  pointer watch the reorder drag uses, since the item under the strip would
  otherwise take the press.

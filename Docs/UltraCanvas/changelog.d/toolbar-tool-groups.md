- **Tool groups: several tools in one toolbar slot, with a fold-out bar.**
  `UltraCanvasToolGroupButton` (new, `UltraCanvasToolGroupButton.h`) is a
  slot of a tool palette or toolbar holding a group of tools - the
  selections, the brushes, the shapes. It shows one of them, and a click on
  it picks that tool; resting the pointer on it for 150 ms
  (`SetFoldOutDelay`) folds out a bar beside it with the whole group - to
  the right of a slot in a vertical toolbar as a row, below one in a
  horizontal toolbar as a column, on the opposite side where the window has
  no room - and a click there picks a tool (`onItemSelected`). The bar
  folds back when the pointer leaves the slot and the bar, on Escape and on
  a click elsewhere. A small triangle in the slot's corner marks a group;
  a group of one tool is a plain toggle button. The slot shows the tool last
  picked - an adaptive toolbar - or, with `SetSlotShowsLastPicked(false)`,
  always the group's first, main tool - a static one; either way the bar
  marks the tool in use while the slot is pressed. `SetCurrentItem` tells
  the group which of its tools became active some other way (a shortcut
  key) without firing anything. The bar's buttons copy the slot's size,
  icon size and style, so a flat palette folds out a flat bar.
  `UltraCanvasToolbar::AddToolGroup` (and the builder's) adds one with the
  toolbar's button appearance, as `AddToggleButton` does a single tool, and
  `UltraCanvasButton` reports its icon size (`GetIconWidth` /
  `GetIconHeight`). `Tests/ToolGroupButtonTest` drives the window's real
  pointer dispatch under Xvfb: folding out, crossing into the bar, picking,
  folding back, the static slot, the horizontal and the window-edge cases.
  Doc: `Docs/UltraCanvas/UltraCanvasToolGroupButton.md`. UltraPaint's tool
  palette is the first to use it (UltraPaint 0.2.16).

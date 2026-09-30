- **`SetMargin()` did nothing inside an ordinary container or group box.**
  Block layout, the default display, stacked its children at their bare
  border-box height and never read their margins, so a margin only took effect
  under flex, grid or absolute positioning. The Group Box demo's label margins
  had no visible effect, and the TreeView demo had to switch its group boxes to
  a flex column to put any space between a tree and its options. Block layout
  now offsets each child by its margin, adds the vertical margins to the stack
  and to the parent's automatic height, and narrows the width a child is
  offered by its left and right margins (percentages against the content
  width) - so wrapped multi-line text wraps inside its margins. Margins do not
  collapse, as in flex; `margin: auto` does not centre in block layout.
  `Tests/CSSLayoutBlockMarginTest.cpp` pins it.

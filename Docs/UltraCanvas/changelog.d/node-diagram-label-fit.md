- **A node diagram's labels fit their nodes.** `UltraCanvasNodeDiagram` 2.4.0.
  The demo's Organization tab drew a degree-1 analyst as a 26 px square under
  a 9 pt name some 50 px wide, so every name ran over its neighbours and the
  hub names over the cluster titles. The component now has a **label fit**
  (`NodeLabelFit`, `SetLabelFit()`, `SetLabelFitConfig()`): `ShrinkFont`
  reduces the font until the label fits, down to `minFontSize`, then shortens
  it with an ellipsis; `GrowNode` enlarges the node until the label fits at
  its font size; `ShrinkThenGrow`, the default, shrinks first and grows what
  still does not fit; `Overflow` is the old behaviour. The fit is per shape -
  a circle grows to the chord its name needs, a diamond to about twice what a
  square needs - and a node grows about its centre, then its neighbours move
  apart (`ResolveOverlaps`) and the view is refitted. The data-driven sizing
  (by degree or value) keeps every node at least as large as its label, and
  the circular, grid and hierarchical layouts end without overlaps, as the
  force-directed one already did. The cluster boxes are kept apart from each
  other as well, after every layout and again after the fit (a box reserves
  room for its title at the current zoom, so a title no longer sits on the
  first member), half a move each or, when a box squeezed between two keeps
  bouncing, placed one by one at the nearest free spot. The force-directed
  layout held every node inside the element (forty nodes of a hundred pixels
  do not fit 800 x 600, and the overlap pass then scattered the clusters
  through each other), repelled by the distance between centres and started
  a group's members all round the ring: its area now grows with the nodes,
  the charge and the springs act between borders, and a group's members start
  together. `MinimumNodeSizeForLabel()`,
  `FitNodesToLabels()` and `GetNodeLabelFontSize()` expose the pieces;
  `MeasureLabel()` measures with the window's font once the element has one,
  and a layout run before that is corrected with the real metrics on the
  first frame. The fit is saved in `ToJson()`. The Organization tab uses
  `GrowNode` with a degree range that starts above what a member's name
  needs, and its sidebar has a LABEL FIT dropdown for all four modes.
  `Tests/NodeDiagramOverlapTest.cpp` covers the minimum sizes per shape, the
  growth, the shrink, the default, the three other layouts and the JSON.
- **The node diagram demo's Friends tab is a personal social network.** In
  place of five circles on a grid: twenty-four friends as card nodes, each
  with an icon for what they are into, their first name and their interest,
  in five circles (college, the climbing club, work, the neighbours, family)
  drawn as soft boxes with a legend, two friends who belong to two circles at
  once and sit in both boxes, "You" at the centre with every tie, the ties in
  their circle's colour inside a circle and grey across, wider the closer the
  tie, the cards collapsing to their icon discs when zoomed out, and a click
  on a person that says who they are, how many ties they have and which
  circles they are in. For it, two groups that share a member now move as one
  unit when the cluster boxes are separated (they overlap on purpose), and a
  node in no group is pushed out of every box.

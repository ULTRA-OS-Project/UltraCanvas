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
  force-directed one already did. `MinimumNodeSizeForLabel()`,
  `FitNodesToLabels()` and `GetNodeLabelFontSize()` expose the pieces;
  `MeasureLabel()` measures with the window's font once the element has one,
  and a layout run before that is corrected with the real metrics on the
  first frame. The fit is saved in `ToJson()`. The Organization tab uses
  `GrowNode` with a degree range that starts above what a member's name
  needs, and its sidebar has a LABEL FIT dropdown for all four modes.
  `Tests/NodeDiagramOverlapTest.cpp` covers the minimum sizes per shape, the
  growth, the shrink, the default, the three other layouts and the JSON.

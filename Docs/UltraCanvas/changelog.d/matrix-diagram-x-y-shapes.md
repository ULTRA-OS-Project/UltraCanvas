- **Matrix diagram, Phase 2: the X and Y shapes.** `UltraCanvasMatrixDiagram`
  now lays out `MatrixShape::X` - four sets on the arms of a cross (left, top,
  right, bottom in model order), four panels in the quadrants, and a hub that is
  either blank or holds the four set titles in boxes with arrows out to their
  arms (`SetHubMode(MatrixHubMode::Captions)`) - and `MatrixShape::Y`, three
  rhombic panels between seams 120 degrees apart. Both used to fall back to the
  L layout of panel 0. Every panel is now one cell lattice (origin, row step,
  column step), so the grid, marks, highlight and hit test serve all four
  shapes without shape-specific copies; an X panel may name either of its sets
  as the row set and is turned to fit.
- Matrix diagram: a form header band (PROJECT / AREA / TEAM / DATE ...) from the
  new `MatrixModel::headerFields`, in every shape; `SetShowHeaderBand(false)`
  hides it.
- Matrix diagram: `MatrixDiagramStyle::tintCellsByLevel` fills each marked cell
  faintly with its level's colour, and a level filtered out through the legend
  fades its tint with its mark.
- Matrix diagram: hovering a cell lights its row and column item in every panel
  that lists them - across both wings of a T and both quadrants of an X - not
  only in the hovered panel.
- `MatrixModel::Validate()` rejects an X panel that pairs two sets on one axis,
  two panels claiming one quadrant, and a Y panel that crosses a set with
  itself or repeats a pair. New samples `MatrixSamples::SupplyChain()` (X) and
  `MatrixSamples::DesignTraceability()` (Y) are on new X-Shaped and Y-Shaped
  tabs of the DemoApp page, which also gains hub, tint and header-band
  controls. `Tests/MatrixModelTest.cpp` covers the new checks and samples.

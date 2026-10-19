- **Matrix diagram: the C shape (a cube of three sets).** `MatrixShape::C`
  relates three lists at once. Every cell is one item from each list, such as
  a defect type on a production line in a shift, which no pair of two-way
  panels can express.
  - `MatrixPanel::depthSetId` makes panel 0 a cube. `SetCubeCell` and
    `SetCubeCellAt` set its cells. `FindCell`, `LevelAt` and `ClearCell` take
    a defaulted depth. `DepthScore` totals one slice. `RowScore` and
    `ColumnScore` sum through all slices.
  - The cube is drawn exploded: one isometric slice per depth item, stacked
    and outlined by the cube's side edges, so no cell is hidden behind
    another. Hovering a cell lights its row and column in every slice. Each
    slice's label lights the slice and fires the new `onSliceClick`.
    `Page Up` and `Page Down` move the selection between slices.
  - JSON carries cubes. The CSV forms refuse them, because a table has no
    third axis. `Validate()` checks that the three sets differ and that every
    depth is in range.
  - New sample: `MatrixSamples::DefectCube()`, shown on a **C-Shaped (Cube)**
    tab in the DemoApp.

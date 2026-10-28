- **New element: the performance matrix.** `UltraCanvasPerformanceMatrix`
  draws named categories where two rated axes cross: the 9-box talent grid,
  the performance management matrix, performance-vs-potential codes, talent
  review cards and skill / will. It replaces the demo's *Performance Matrix
  is not ready yet* placeholder (**Info Graphics → Performance Matrix**).
  - Four designs: `Grid` (contiguous cells with level labels, axis arrows and
    titles), `Tiles` (rounded tiles with a large code), `HeaderTabs` (rounded
    level tabs with icons above and to the left of plain cells) and `Cards`
    (bordered cards with a badge and bullets).
  - Four palettes (`TrafficLight`, `Vivid`, `Monochrome(hue)`, `Pastel`), kept
    separate from the design. The cell colourings are `Plain`, `Explicit`,
    `DiagonalBands` (by rank from the best corner), `ByRow` and `ByColumn`.
  - All cells share one font size, the largest at which every cell fits,
    down to 9. Below that, a crowded cell drops bullets, then its
    description, then its action. The tooltip keeps everything.
  - Hover outlines the cell and marks its row and column. Click selects the
    cell. `onCellHover` / `onCellClick` report it, with the cell geometry
    (`GetCellRect`, `CellAt`) and the tooltip text alongside.
  - The UI-free model (`UltraCanvasPerformanceMatrixModel.h`) stores levels
    low to high on both axes whichever way they are drawn, with sparse cells,
    `Rank` from a configurable best corner and `Validate()`. It ships five
    samples.

  `Tests/PerformanceMatrixModelTest.cpp` and
  `Tests/PerformanceMatrixRenderTest.cpp` cover the model and the drawing; the
  render test writes PNGs when `ULTRACANVAS_SCREENSHOT_DIR` is set. Guide:
  `Docs/UltraCanvas/UltraCanvasPerformanceMatrix.md`. Placing people in the
  boxes and calibration are the proposal's next phases.

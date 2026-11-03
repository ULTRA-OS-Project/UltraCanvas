- **New chart: `UltraCanvasTernaryChart` (Phase 1).** It draws compositions of
  three parts - a three-party vote, soil texture, an alloy - as points in an
  equilateral triangle, where each corner is all of one part.
  - The chart engine gains `ChartProjectionKind::Ternary`
    (`ChartTernaryProjection`). It maps `(u, v)` as the shares of the second
    and third part into the largest triangle that fits the plot. The map is
    affine, so polygons, hit regions and the label plan work through it
    unchanged. The engine fills the triangle as the plot area.
  - `UltraCanvasTernaryModel` keeps each point's raw amounts and divides them
    by their sum, so the shares always add up to 1 and a lab result that sums
    to 99.7 is still drawn. A point with no shares (a negative part, a
    missing one, a sum of 0) is left out, and `Validate()` names it.
  - The chart draws the grid, three edge axes with ticks every 10 %, the
    edge titles with arrows and the corner titles. The axes read clockwise
    (the USDA soil triangle) or anticlockwise, set by one flag, and each tick
    runs along its own grid line.
  - The series are drawn as markers with a legend that hides a series.
    Tooltips give each share and the raw amounts. `onPointClick` reports
    clicks. Optional point labels steer around the axis numbers and titles.

  The demo's Charts section gains *Ternary Chart*, with invented vote and
  soil samples and controls for the reading direction, point names, the grid
  and the edge titles. `TernaryModelTest`, `TernaryChartRenderTest` and a
  `ChartEngineTest` case for the projection cover the shares, the reading in
  both directions, hit-testing, clicks and hidden series.

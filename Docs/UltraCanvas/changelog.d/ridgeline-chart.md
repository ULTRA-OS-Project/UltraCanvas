- **Ridgeline chart (Phase 1).** `UltraCanvasRidgelineChart` (chart engine)
  draws one curve per row on its own baseline, rising over the rows above it
  (a joyplot): the rows back to front, so the lower row covers the one
  behind it, their names, a value column with its header ("Average score"),
  and a tooltip with the row, the x and its statistics. The UI-free
  `UltraCanvasRidgelineModel` turns raw values into curves by a Gaussian
  kernel density (Scott's or Silverman's bandwidth or a fixed one,
  reflection at the bounds of a scale such as 0-10, binning for large rows)
  or takes curves as given, scales them to one scale, to each row's peak or
  to equal areas, sorts the rows and gives each its count, mean, median,
  quartiles and peak. Colourings: one colour, alternating, a colour map down
  the rows or by the value column. Demo: **Charts > Ridgeline Chart** (word
  ratings and times of day, invented). Tests: `RidgelineModelTest`,
  `RidgelineChartRenderTest`. Guide: `UltraCanvasRidgelineChart.md`.

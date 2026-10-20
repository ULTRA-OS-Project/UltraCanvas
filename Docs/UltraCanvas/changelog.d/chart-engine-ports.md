- **Chart engine: fifteen legacy charts ported, and a demo page that shows
  each port next to its original.** Twelve engine-native elements under
  `Plugins/Charts/Engine/Charts/` take over the charts that still painted their
  own axes, legend and labels: `UltraCanvasEngineLineChart` (line, area,
  stacked and 100% area, cumulative flow), `UltraCanvasEngineScatterChart`
  (scatter and bubble), `UltraCanvasEngineWaterfallChart`,
  `UltraCanvasEngineDivergingBarChart` (Likert, tornado, opposing bars,
  population pyramid), `UltraCanvasEngineDumbbellChart`,
  `UltraCanvasEngineFinancialChart` (candlestick, OHLC, Heikin-Ashi, volume
  pane, SMA / EMA), `UltraCanvasEngineHeatmapChart`,
  `UltraCanvasEngineMekkoChart`, `UltraCanvasEngineRadarChart`,
  `UltraCanvasEngineRadialBarChart`, `UltraCanvasEnginePieChart` and
  `UltraCanvasEnginePolarChart`. Each implements only the content contract;
  themes, measured layout, the solved label plan, tooltips, the entrance
  animation and the three projections come from the engine. The legacy charts
  are unchanged, so applications switch one chart at a time. **Charts → Chart
  Engine Comparison** in the demo shows every port beside the legacy chart on
  the same data. Doc: `Docs/UltraCanvas/UltraCanvasEngineCharts.md`.
- **Chart engine: the features the ports needed, in the engine.**
  - Polar furniture: under Polar an edge axis follows its role - a domain
    axis labels the rim, a value axis is the radial rule - with ring or
    polygon grids (`SetPolarGridShape`), spokes (`SetDomainGridAxis`), a round
    plot fill, rim labels as label obstacles, and `SetPolarAngles` /
    `SetPolarInnerRadius`. Before, both axes were straight rules along the
    rectangular plot and the value grid drew degenerate chords; the bar chart
    page's Polar mode gets the round furniture with no change of its own.
  - `ChartAxis`: `role`, sub-bands (`bandStart` / `bandEnd`, stacked price and
    volume panes), `symmetric`, `cyclic` categories, `absoluteLabels`,
    `rangePadding` and the `Sqrt` scale.
  - Series: line runs (straight, monotone curved, stepped; broken at gaps;
    `closed` loops), area outlines, line-family stacking, marker shapes and
    `DrawChartMarker`; a UI-free analysis module (`FitLinearTrend`,
    `MovingAverage`, `ExponentialMovingAverage`, `HeikinAshiCandles`).
  - Pinned labels (`ChartLabelRequest::pinned`), span annotations, limiter
    extents and domain-axis limiters, value bands that follow the axis role
    and the projection, edge axes on one side stacking outward, and an
    area-scaled size legend with explicit values.
- **Chart engine: fixes the comparison showed.** An auto range rounded twice
  (0..1072 became 0..1500, a third of the plot empty) and now rounds once
  (0..1200); tick steps likewise. A prefixed negative reads `-$25K`, not
  `$-25K`. Left / right axis titles are rotated along their axis instead of
  sitting under it on the bottom axis's first label. The end labels of a
  bottom / top axis are no longer cut off at the element's edge.
- **Chart engine: tick labels inside the plot are label obstacles.** The labels
  of an in-plot axis and of the radial value rule under Polar render above the
  content but did not take part in the label plan, so a solved label could
  land on one - the polar chart's "Under-Absorp" band caption on its "0 ppm".
  They now claim their space before the solve (the tick declutter runs first,
  and one helper places a tick label for the plan and the draw pass), and a
  polar limiter's caption sits a little way round its ring instead of beside
  the rule.
- **Chart engine: `OnThemeChanged()` is now `OnChartThemeChanged()`.** The
  engine's parameterless hook for a chart-theme change hid the element's
  `OnThemeChanged(const UltraCanvasTheme&)` (`-Woverloaded-virtual` in every
  file including the engine header), and the two hooks mean different things:
  a chart palette swap, and the application's theme. A chart overriding the
  old name with `override` now fails to compile and is renamed; the twelve
  engine ports and the demo are.

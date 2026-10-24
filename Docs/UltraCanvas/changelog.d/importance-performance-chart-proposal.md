- **New design proposal: an importance–performance chart, and the performance
  matrix proposal gives up the image that needed it.** The fourth reference
  image of the performance matrix proposal turned out to be Slack's
  importance–performance matrix. That is a chart from operations strategy:
  competitive factors are plotted as points over authored zones (*Urgent
  action*, *Improve*, *Appropriate*, *Excess?*). It is not a talent grid. No
  existing chart draws it: the quadrant chart has only two straight dividers,
  and the engine scatter chart's highlights are computed from points, with no
  outline of your own.
  `Docs/UltraCanvas/UltraCanvasImportancePerformanceChartProposal.md` proposes
  `UltraCanvasImportancePerformanceChart`, derived from
  `UltraCanvasEngineScatterChart`, with one general engine addition: an
  explicit `ChartHighlightShape::Polygon`. It covers zones that are stepped or
  smoothed, a labelled boundary band, named level ranges on the axes,
  classification that uses the same geometry as the drawing, and Slack,
  Martilla & James and diagonal presets, in three phases.
  `UltraCanvasPerformanceMatrixProposal.md` revision 2 drops its `Zones`
  design and its fourth phase accordingly.

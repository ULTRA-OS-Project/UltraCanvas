- **Chart engine: axis distributions.** `AddAxisDistribution` draws the
  distribution of an axis's values along that axis, at the axis's own scale:
  a histogram, a kernel density curve, a box summary (quartiles, median,
  Tukey whiskers, outliers), a strip of ticks, or a violin. It is the layer
  the engine reserved slot 550 for and never drew - the joint plot's
  histograms above and to the right of a scatter field, or a histogram on
  every axis of a parallel coordinate chart. An `Outside` band takes its
  depth beyond the plot's opposite edge in the layout, and the axes and the
  legend on that edge move out by as much, so nothing is drawn over it;
  `Near`, `Far` and `Both` draw inside the plot or on either side of an
  in-plot rule, stopping short of the next rule and never leaving the plot:
  the first and last rules of a parallel coordinate chart, which stand on
  the plot's edges, draw theirs inward rather than over the margin's tick
  labels or the legend. A subset - a brushed
  selection, one series - can be drawn over the whole on the same bins. The
  statistics (`ComputeHistogram` with Freedman-Diaconis bins,
  `ComputeKernelDensity` with Silverman's bandwidth, `ComputeBoxSummary`)
  are UI-free and unit-tested in `Engine/UltraCanvasChartDistribution.h`,
  `ChartAxis::Samples()` returns what an axis observed, and the demo's
  Chart Engine page shows every kind and side on its Annotations tab.
- **The chart engine documents match the code.** `UltraCanvasChartEngine.md`
  no longer calls the highlight layer "not yet implemented" beside its own
  section on it, and the engine proposal shows the contract that was built
  (`RenderChartContent`, `ChartEngineFrame`, `AddHitRegion`, axes added in
  `DescribeAxes`, no engine singleton) instead of eight types no header
  declares, with a table from the names it first used to the shipped ones.

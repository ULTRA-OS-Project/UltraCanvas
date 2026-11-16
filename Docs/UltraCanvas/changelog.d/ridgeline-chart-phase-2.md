- **Ridgeline chart, Phase 2.** Several series per row (`RidgelineRow::more`,
  `model.series`) drawn over each other in their own colours, with a legend
  that switches one off; a wrapping x axis (`model.period`, a circular
  density, statistics on the wrapped values: a median bedtime of 00:40); a
  tail cut (`density.tailCut`); binned curves, the histogram look
  (`density.binned`, `bins`); marks at the mean, median or quartiles
  (`SetMarks`) and the middle half shaded (`SetQuantileBands`); the other
  rows fading while one is hovered (`SetHighlightOnHover`); `onRowClick`.
  The model goes to and from JSON (`ExportRidgelineJson`,
  `ImportRidgelineJson`) and its data to and from CSV (`ExportRidgelineCsv`,
  `ImportRidgelineCsv`). The value column writes x values through the x
  formatter. Demo: the Sleep sample (two series on a 24-hour axis from
  18:00), marks, shading, tail trim, histogram, fading, a click readout,
  Open / Save JSON and Import / Export CSV through `UltraCanvasFileLoader`.

- **Charts: zoom and pan of the line, area and scatter charts work, and are
  off by default.** `SetEnableZoom` / `SetEnablePan` did nothing visible (the
  zoom level was never read, pan was an empty stub), yet these three charts
  switched both on in their constructors, so they swallowed the mouse wheel
  and a scrolling page around them could not scroll. Now, once enabled, the
  wheel zooms the x axis around the pointer (up to 50x, in both x-axis label
  modes) and a drag pans a zoomed axis; `ResetZoom()` and `IsZoomed()` are
  new. A wheel turn or drag that changes nothing - over the margins, zooming
  out of the whole range, on a chart that does not zoom such as the bar
  chart - is left to the parent.
- **Charts: the plot area follows a resize.** The chart base worked out its
  plot rectangle only when the data source or label mode was set, so a chart
  the layout resized kept its old plot area. `SetBounds` and `Arrange` now
  make it be worked out again.
- **Charts: the line chart's dots follow `SetPointRadius`.** The line chart
  had a radius of its own (4 px) that hid the base class's; it is still 4 by
  default.
- **Charts: a chart's background is the element's own.** The chart base kept
  a second `backgroundColor` and `SetBackgroundColor` beside
  `UltraCanvasUIElement`'s, so a colour set through one was not seen by the
  other. There is one colour now, white by default for charts.
- **Charts: bar charts rise from zero.** The value axis ran from the smallest
  to the largest value plus 5 %, so bars stood on the smallest value; it now
  always includes 0, and negative values hang below the zero line.
- **Charts: a CSV's first row labelled "May" or "July" is kept.**
  `ChartDataVector` and `ChartDataStream` took the first line for a header
  when it contained an `x` or a `y` anywhere; it is a header now only when its
  x and y columns are not numbers.
- **Waterfall chart:** `WaterfallChartDataVector::LoadFromCSV` (lines
  `label,change[,type]`) and `LoadFromArray(std::vector<ChartDataPoint>)` load
  steps instead of nothing; the running totals are worked out when the steps
  change instead of on every `GetPoint`; and the chart no longer re-declares
  `showValueLabels`, so `SetShowValueLabels` through the base class reaches
  it.
- **`CreatePopulationPyramid` lays out its `rowLabels`** as empty rows in
  that order, and `AddDataRow` with a label that is already a row fills that
  row instead of adding a second one.

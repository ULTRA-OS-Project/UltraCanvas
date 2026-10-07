- Docs: `UltraCanvasScatterPlotElement.md` matches the code and passes
  `check_doc_examples.py`. The constructor and `CreateScatterPlotElement`
  take no numeric id; `SetPointSize` takes a `double` and is the point's
  radius (6 by default); `SetEnableSelection` is the ring around the hovered
  point, not click selection; the tooltip picks a point within its size plus
  5 px; and an invented `ShowErrorMessage` and `ScatterDataPoint` gave way to
  the chart's empty state and the real `ChartDataPoint`.

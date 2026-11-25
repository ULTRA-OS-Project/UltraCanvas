- **Charts in spreadsheets.** A sheet holds charts
  (`UltraCanvasSpreadsheetChart.h`): a `SpreadsheetChart` anchored to two
  cells with offsets, as .xlsx's two-cell anchor keeps it, holding a
  `RichChart` whose series are ranges of cells - names, categories (a scatter
  series' x values) and values - read from the cells again whenever they
  change. The grid draws them with the chart engine over the cells, where
  they stand as it scrolls, and prints them; a click selects one, a drag
  moves it, its eight handles resize it, Delete removes it, the arrow keys
  nudge it, a double-click opens its properties (kind, title, legend, series
  in rows), and each change is an undo step. `InsertChartFromSelection` makes
  columns, bars (side by side or stacked), lines, areas, pies, doughnuts,
  scatter and radar charts from the selection, the first row and column
  taken as names and categories where they hold words. Inserted rows and
  columns move a chart and its ranges with the cells; a renamed sheet's
  ranges follow it.
- **.xlsx and .ods keep them.** .xlsx: a drawing per sheet
  (`xl/drawings/drawingN.xml`, `xdr:twoCellAnchor` and `graphicFrame`) and a
  chart part per chart with `c:f` references and their caches
  (`WriteDrawingMLChart(chart, sources)`, `ReadDrawingMLChartSources`); .ods:
  a `draw:frame` in the sheet's `table:shapes` naming an `Object N/` chart
  whose series point at the sheets (`WriteOdfChartObject(chart, w, h,
  sources, plotArea)`, `ReadOdfChartSources`), and LibreOffice's cell-anchored
  frames read too. LibreOffice opens both, and its conversions read back.
  Covered in `SpreadsheetChartRoundTripTest` and `SpreadsheetChartDrawTest`.
- **An untitled chart's top value label is no longer cut off.** The chart
  engine centres a left or right axis's labels on their ticks but reserved
  no room above the plot for the top one, so a chart without a title showed
  half of its "60.0" at the top edge - in spreadsheets, documents and slides
  alike. `UltraCanvasChartEngineElement` now reserves half a label's height
  above and below the plot for such an axis (under a title, what the title
  band does not already leave).

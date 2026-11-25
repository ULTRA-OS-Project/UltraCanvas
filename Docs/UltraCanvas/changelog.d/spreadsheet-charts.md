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
- **Every chart of the framework in a spreadsheet.** `RichChart::Kind` names
  every chart a table of cells can feed - waterfall, diverging bars,
  population pyramid, dumbbell, Mekko, cumulative flow, nested areas, polar,
  radial bars, progress rings, gauge, quadrant, importance-performance,
  ternary, hexbin, 3D scatter, histogram, box and whisker, jitter,
  ridgeline, funnel, pyramid, Sankey, chord, treemap, sunburst, Gantt,
  timeline, heatmap, calendar heatmap, contour, surface, word cloud and
  parliament, beside the office kinds - and `CreateRichChartElement` /
  `DrawRichChart` (`UltraCanvasRichDocumentChart.h`) build the element of
  any of them from the chart's categories and series, so documents and
  slides draw them too (`RichChartSeries::labels` carries a column of words,
  `RichChart::bins` a histogram's bins). `SpreadsheetChartTypes()` lists 52
  types in nine categories with their names and descriptions;
  `MakeSpreadsheetChartOfType` reads the columns as each type does (a
  bubble chart's x, y and size, a Sankey diagram's sources, targets and
  amounts, a Gantt chart's tasks, starts and ends, a first column of dates
  as categories), and `SpreadsheetChartFitMessage` says why a selection does
  not fit a type ("A bubble chart needs three columns of numbers: x, y and
  size."). Kanban boards, the circular infographic, the spectrogram, the
  image histogram, the GL surface and the parallel-coordinates module are
  left out: no table of cells feeds them.
- **The chart gallery.** `ShowSpreadsheetChartGallery` shows every type by
  category, each tile a picture of the selection drawn by the chart itself
  (a faded sample, with the reason, where it does not fit), its name and
  description; a click or the arrow keys pick one, Enter or a double click
  takes it, the category chips scroll to their section. The grid's
  `ShowInsertChartDialog` and `ShowChartTypeDialog` use it, and the
  properties dialog's *Type* is a button that opens it. The gallery is a
  new element, `UltraCanvasGalleryPicker`: tiles of live pictures in titled
  sections, picked with the mouse or the keys.
- **Kept in .xlsx and .ods.** A kind the format has a form for is written
  as itself - now also a surface (`c:surface3DChart`), a contour map
  (`c:surfaceChart`) and a bubble chart's size references (`c:bubbleSize`,
  `DrawingMLSeriesSource::sizes`). Every other kind is written as a column
  chart of its numbers, which Excel and LibreOffice show, with an
  UltraCanvas extension holding the real kind, its look and every series'
  ranges: `c:extLst/c:ext` in the chart part, and an element of our own
  namespace at the end of the .ods chart object. Reading puts the kind
  back. Excel 2016's `cx:` parts are not written; those kinds take the
  extension too.
- `UltraCanvasChartElementBase::SetEntranceAnimation`: a chart shown
  complete from the first frame - on a page, in print, as a thumbnail.

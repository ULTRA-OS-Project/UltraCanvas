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

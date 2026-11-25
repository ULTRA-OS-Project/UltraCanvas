# UltraCanvasDrawingML — charts and colours from Office files

Word, PowerPoint and Excel keep a chart the same way: a DrawingML chart part
(`c:chartSpace` - `word/charts/chart1.xml`, `ppt/charts/chart1.xml`,
`xl/charts/chart1.xml`) holding its kind, its series with the values the
file last calculated, its categories, title, legend and axes. Colours are
written the same way in all three too (`a:srgbClr`, `a:schemeClr` with
`a:lumMod` ...). `UltraCanvasDrawingML.h` reads both once, for every format:
the `.docx` reader and the PowerPoint reader
([UltraCanvasPresentationPptx](UltraCanvasPresentationPptx.md)) call it, and
an `.xlsx` chart view would.

Header: `UltraCanvasDrawingML.h`. It returns UCRichDocument's chart model,
`RichChart` (`UltraCanvasRichDocument.h`), which
`UCRichDocument::ChartAsShapes` draws as shapes and
`DrawRichDocumentChart` (`UltraCanvasRichDocumentChart.h`) draws with the
chart engine.

## Reading a chart

<!-- doc-check: std::string chartXml; void Show(const UltraCanvas::RichChart& chart); -->
```cpp
#include "UltraCanvasDrawingML.h"

using namespace UltraCanvas;

// chartXml: a chart part's XML, read from its package (UCZipPackageReader).
RichChart chart;
if (ReadDrawingMLChart(chartXml, DrawingMLDefaultThemeColors(), chart)) {
    Show(chart);   // e.g. DrawRichDocumentChart into a slide or a page
}
```

`ReadDrawingMLChart(xml, themeColors, out)` reads:

- the kind: columns (side by side, stacked, stacked to 100 %), bars lying
  down (`c:barDir="bar"`), lines, areas, pies and doughnuts (`c:ofPieChart`
  as a pie), scatter and bubble charts, radar charts (filled or marked) and
  stock charts; any other kind reads as columns of its series;
- each series' name, values (`c:val`, or a scatter chart's `c:yVal`), the
  x of each point (`c:xVal`) and a bubble's size (`c:bubbleSize`), whether
  its points are joined and marked, smoothed lines (`c:smooth`), and its
  colour where the file states one - a series without one is left to the
  viewer's palette;
- a pie's slices in their own colours (`c:dPt`);
- the categories, the title (its runs joined), whether there is a legend,
  and the ranges the file states for its value axes (`c:min` / `c:max`, a
  scatter chart's x axis apart).

It answers false - and leaves `out` alone - when the XML does not parse or
holds no series with a value. Elements are matched by their local name,
whatever prefix the part binds the namespace to (`c:`, `ch:`, `ns0:` ...).

## Writing a chart

`WriteDrawingMLChart(chart)` gives the chart part for a `RichChart` - the
`.docx` and the PowerPoint writers both call it: the kind, each series' name,
values as literals (there is no workbook behind them; Word and PowerPoint show
the chart and offer no data to edit), its colour where it states one (lines,
scatter points and bubbles without one take the Office palette's), a pie's
slice colours, the categories, the title, the legend, the axes and the ranges
the chart states (`valueMin` / `valueMax`, a scatter chart's `xMin` / `xMax`).
`ReadDrawingMLChart` reads it back as it was.

A workbook's chart (`xl/charts/chartN.xml`) keeps references to the cells
instead: `WriteDrawingMLChart(chart, sources)` writes each series' name,
categories, values and - scatter - x values as `c:strRef` / `c:numRef` with
the formula a `DrawingMLSeriesSource` gives (`'Sales Data'!$B$2:$B$7`) and the
chart's values as their cache, and `ReadDrawingMLChartSources` reads the
formulas back, one per series in the part's order. The spreadsheet's charts
([UltraCanvasSpreadsheetExamples](UltraCanvasSpreadsheetExamples.md#charts))
are written and read this way.

<!-- doc-check: UltraCanvas::RichChart chart; -->
```cpp
std::vector<DrawingMLSeriesSource> sources(1);
sources[0].name = "Sheet1!$B$1";
sources[0].categories = "Sheet1!$A$2:$A$7";
sources[0].values = "Sheet1!$B$2:$B$7";
const std::string part = WriteDrawingMLChart(chart, sources);
for (const DrawingMLSeriesSource& source : ReadDrawingMLChartSources(part)) {
    // source.values == "Sheet1!$B$2:$B$7" ...
}
```

## Colours

<!-- doc-check: const tinyxml2::XMLElement* solidFill; std::map<std::string, std::string> theme; -->
```cpp
#include "UltraCanvasDrawingML.h"

using namespace UltraCanvas;

// theme: the package's theme colours, "accent1" -> "4472C4" (from its theme part).
std::string hex = DrawingMLColor(solidFill, theme);            // "#RRGGBB", or empty
std::string office = DrawingMLColor(solidFill, DrawingMLDefaultThemeColors());
```

`DrawingMLColor(holder, themeColors, placeholder)` gives the colour an
element holding one names (`a:solidFill`, a series' `c:spPr` fill ...) as
`"#RRGGBB"`: `a:srgbClr`, `a:sysClr` (its `lastClr`), `a:scrgbClr` (linear
light, converted), `a:prstClr` (the common names) and `a:schemeClr` through
`themeColors` (`bg1` / `tx1` / `bg2` / `tx2` read as `lt1` / `dk1` / `lt2` /
`dk2`; `phClr` is `placeholder`, a style reference's colour), with
`a:shade`, `a:tint`, `a:lumMod` and `a:lumOff` applied. Empty when it names
none of these. `DrawingMLDefaultThemeColors()` is Office's own scheme, for a
package without a theme.

## Test

`Tests/WordFormatsTest.cpp` reads charts through the `.docx` reader (columns,
bars, lines, areas, pies, doughnuts, scatter, bubble, radar and stock
charts, written and read back), and `ReadDrawingMLChart` directly: a part
that binds the chart namespace to another prefix, a theme colour darkened
with `a:lumMod`, an unstated colour left empty, stated axis ranges, the
title's runs joined; a chart without series and broken XML refused, the
model left alone. `Tests/PresentationPptxTest.cpp` reads a PowerPoint chart
python-pptx wrote.

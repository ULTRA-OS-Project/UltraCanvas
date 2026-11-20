# UltraCanvasOdfChart — charts in OpenDocument files

An OpenDocument file keeps a chart as an object of its own inside the
package: `Object N/content.xml`, an `office:chart` document holding a
`chart:chart` - its class, its series with their styles, the axes, and a
small table whose cells the series' ranges name - shown on the page by a
`draw:frame` / `draw:object` (in a flat file, the chart document sits inline
in the `draw:object`). `UltraCanvasOdfChart.h` writes and reads that document
once, for every ODF format, into UCRichDocument's chart model `RichChart`:
the `.odt` reader and writer and the `.odp` reader and writer
([UltraCanvasPresentationOdp](UltraCanvasPresentationOdp.md)) all call it, as
Office's formats call [UltraCanvasDrawingML](UltraCanvasDrawingML.md).

## Writing and reading a chart

<!-- doc-check: UltraCanvas::RichChart chart; void Store(const std::string& part, const std::string& xml); -->
```cpp
#include "UltraCanvasOdfChart.h"

using namespace UltraCanvas;

// The object's content.xml, for a chart drawn 300 x 200 pt.
const std::string xml = WriteOdfChartObject(chart, 300.0f, 200.0f);
Store("Object 1/content.xml", xml);   // and a manifest entry for "Object 1/"

RichChart back;
if (ReadOdfChartObject(xml, back)) {
    // back.kind, back.series, back.categories, back.title ...
}
```

`WriteOdfChartObject(chart, widthPt, heightPt)` writes the class (bar,
line, area, circle, ring, scatter, radar or filled radar, bubble, stock),
bars lying down, stacked or per-cent stacking, smoothed lines, each series'
values (a scatter series' x, a bubble's sizes) in the chart's own table,
the series' colours where they state one, a pie's slice colours, the
categories, the title, the legend and the ranges the chart states.

`ReadOdfChartObject(xml, out)` reads that and what LibreOffice writes; a
class the model has no kind for, a document without series, or XML that does
not parse answers false and leaves `out` alone - the object's replacement
picture is then what a reader has to show.

## Test

`Tests/WordFormatsTest.cpp` writes charts into `.odt` files and reads them
back, and reads LibreOffice's (`word-chart*.odt`).
`Tests/PresentationPptxTest.cpp` reads the chart a PowerPoint deck holds
back from the `.odp` and the flat `.fodp` the OpenDocument writer makes.

// include/UltraCanvasOdfChart.h
// A chart as OpenDocument keeps one: an object of its own in the package
// ("Object N/content.xml", an office:chart document holding a chart:chart
// with its series, its styles and the table their ranges point into), shown
// by a draw:frame / draw:object. Written and read here for every ODF format -
// the .odt reader and writer and the .odp writer - into UCRichDocument's
// chart model, RichChart, as UltraCanvasDrawingML does for Office's charts.
//
// Version: 1.0.0 - WriteOdfChartObject and ReadOdfChartObject, moved out of the .odt plugin
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRichDocument.h"   // RichChart

#include <string>

namespace UltraCanvas {

// The chart object's content.xml for `chart`, laid out `widthPt` x
// `heightPt`: its class (bar, line, area, circle, ring, scatter, radar,
// bubble, stock), the series in their colours where they state one, the
// categories, title, legend, the axes' stated ranges, a pie's slice colours,
// and the table of values the series' ranges name.
std::string WriteOdfChartObject(const RichChart& chart, float widthPt, float heightPt);

// Reads a chart object's content.xml into `out`: what WriteOdfChartObject
// writes, and what LibreOffice writes. False when the XML does not parse,
// names a chart class the model has no kind for, or holds no series with
// values (the object's replacement picture is then what there is to show).
bool ReadOdfChartObject(const std::string& contentXml, RichChart& out);

} // namespace UltraCanvas

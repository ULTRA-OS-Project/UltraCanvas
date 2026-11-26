// include/UltraCanvasOdfChart.h
// A chart as OpenDocument keeps one: an object of its own in the package
// ("Object N/content.xml", an office:chart document holding a chart:chart
// with its series, its styles and the table their ranges point into), shown
// by a draw:frame / draw:object. Written and read here for every ODF format -
// the .odt reader and writer and the .odp writer - into UCRichDocument's
// chart model, RichChart, as UltraCanvasDrawingML does for Office's charts.
//
// Version: 1.1.0 - a spreadsheet's chart: series ranges into the sheet's cells (OdfChartSeriesSource),
//                  written and read back (ReadOdfChartSources)
// Version: 1.0.0 - WriteOdfChartObject and ReadOdfChartObject, moved out of the .odt plugin
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRichDocument.h"   // RichChart

#include <string>
#include <vector>

namespace UltraCanvas {

// The chart object's content.xml for `chart`, laid out `widthPt` x
// `heightPt`: its class (bar, line, area, circle, ring, scatter, radar,
// bubble, stock), the series in their colours where they state one, the
// categories, title, legend, the axes' stated ranges, a pie's slice colours,
// and the table of values the series' ranges name.
std::string WriteOdfChartObject(const RichChart& chart, float widthPt, float heightPt);

// Where a series of a spreadsheet's chart takes its data: cell range
// addresses into the spreadsheet ("Sheet1.$B$1", "'My data'.$A$2:.$A$7") -
// its name (chart:label-cell-address), its categories (the x axis'
// chart:categories, a scatter series' chart:domain) and its values. Empty
// where it has none.
struct OdfChartSeriesSource {
    std::string name;
    std::string categories;
    std::string values;
};

// The chart object's content.xml for a chart on a spreadsheet: as above, its
// series' ranges those `sources` names (one per series, in order) and the
// plot area's table:cell-range-address `plotArea` (may be empty); the table
// of values it carries is the cache of what those cells held.
std::string WriteOdfChartObject(const RichChart& chart, float widthPt, float heightPt,
                                const std::vector<OdfChartSeriesSource>& sources,
                                const std::string& plotArea = std::string());

// Each chart:series' ranges, in order (the categories are the x axis', for
// every series where the series has no domain of its own); empty when the
// XML does not parse or holds no chart.
std::vector<OdfChartSeriesSource> ReadOdfChartSources(const std::string& contentXml);

// Reads a chart object's content.xml into `out`: what WriteOdfChartObject
// writes, and what LibreOffice writes. False when the XML does not parse,
// names a chart class the model has no kind for, or holds no series with
// values (the object's replacement picture is then what there is to show).
bool ReadOdfChartObject(const std::string& contentXml, RichChart& out);

} // namespace UltraCanvas

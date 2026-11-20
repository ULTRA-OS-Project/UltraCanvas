// include/UltraCanvasDrawingML.h
// DrawingML (ECMA-376 Part 1, §20-21) read the same way for every Office
// format that carries it - a chart part and a colour - so the .docx, .pptx
// and .xlsx readers share one reading instead of each keeping its own.
//
// ReadDrawingMLChart reads a chart part (c:chartSpace: word/charts/chart1.xml,
// ppt/charts/chart1.xml, xl/charts/chart1.xml - the same XML in each package)
// into UCRichDocument's chart model, RichChart: its kind, series with their
// values and colours, categories, title, legend and stated axis ranges.
// UCRichDocument::ChartAsShapes draws one as shapes, and the rich text view
// draws it with the chart engine (UltraCanvasRichDocumentChart.h).
//
// Elements are matched by their local name, whatever prefix a part binds the
// namespace to (c:, a:, cdr:, ns0:...).
//
// Version: 1.0.0 - ReadDrawingMLChart and DrawingMLColor, moved out of the .docx reader
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRichDocument.h"   // RichChart

#include <map>
#include <string>

namespace tinyxml2 {
class XMLElement;
}

namespace UltraCanvas {

// Office's own colour scheme ("dk1" ... "accent6", "hlink", "folHlink" ->
// "RRGGBB"), for a package without a theme.
const std::map<std::string, std::string>& DrawingMLDefaultThemeColors();

// A DrawingML colour - the element holding a:srgbClr, a:schemeClr, a:sysClr,
// a:scrgbClr or a:prstClr (a:solidFill, a:fgClr ...) - as "#RRGGBB", with its
// tint, shade and luminance changes applied. `themeColors` resolves a theme
// colour ("accent1" -> "4472C4"; bg1 / tx1 / bg2 / tx2 read as lt1 / dk1 /
// lt2 / dk2); `placeholder` ("#RRGGBB") is what a style reference passes in
// for a:schemeClr val="phClr". Empty when the holder names no colour this
// reads.
std::string DrawingMLColor(const tinyxml2::XMLElement* holder,
                           const std::map<std::string, std::string>& themeColors,
                           const std::string& placeholder = std::string());

// Reads a chart part's XML into `out` from the values the file keeps with
// it: a column chart (side by side, stacked or stacked to 100%), bars lying
// down, lines, areas, pies and doughnuts, scatter and bubble charts, radar
// and stock charts; any other kind reads as columns of its series. A series
// keeps its colour only where the file states one (the viewer picks the
// rest). `themeColors` resolves the series' theme colours
// (DrawingMLDefaultThemeColors() when the package has no theme). False when
// the XML does not parse or holds no series with values.
bool ReadDrawingMLChart(const std::string& chartXml, const std::map<std::string, std::string>& themeColors,
                        RichChart& out);

} // namespace UltraCanvas

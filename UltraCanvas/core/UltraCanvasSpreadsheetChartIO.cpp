// core/UltraCanvasSpreadsheetChartIO.cpp
// A spreadsheet's charts in its files (include/UltraCanvasSpreadsheetChart.h):
// .xlsx - a drawing part per sheet (xl/drawings/drawingN.xml) whose
// twoCellAnchor holds a graphicFrame naming a chart part (xl/charts/chartN.xml,
// UltraCanvasDrawingML.h) with c:f references and their caches; .ods - a
// draw:frame in the sheet's table:shapes (or, as LibreOffice anchors one to a
// cell, in the cell) naming an object "Object N/" whose content.xml is a
// chart:chart with ranges into the sheets (UltraCanvasOdfChart.h).
//
// A kind the format has no form for (a Sankey diagram, a Gantt chart ...) is
// written as a column chart of its numbers - what Excel and LibreOffice show -
// with the real kind and every series' ranges in an UltraCanvas extension:
// .xlsx's c:extLst/c:ext, and in .ods an element of our own namespace as the
// last child of chart:chart. Reading takes the extension where there is one.
//
// Version: 1.1.0 - every chart of the framework: the extension; a bubble series' sizes as c:bubbleSize
//                  references; surface and contour charts as .xlsx's own
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasSpreadsheetChart.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetSheet.h"
#include "UltraCanvasSpreadsheetMetrics.h"   // SpreadsheetLengthToPixels, SpreadsheetPixelsToCentimetres
#include "UltraCanvasDrawingML.h"
#include "UltraCanvasOdfChart.h"
#include "UltraCanvasZipPackage.h"

#include "tinyxml2.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>

namespace UltraCanvas {

namespace {

constexpr long kEmuPerPixel = 9525;   // 914400 EMU to the inch, 96 pixels to the inch

const char* Attr(const tinyxml2::XMLElement* e, const char* name) {
    const char* value = e ? e->Attribute(name) : nullptr;
    return value ? value : "";
}

// The element's name without its prefix.
const char* LocalName(const tinyxml2::XMLElement* e) {
    const char* name = e && e->Name() ? e->Name() : "";
    const char* colon = std::strchr(name, ':');
    return colon ? colon + 1 : name;
}

const tinyxml2::XMLElement* Child(const tinyxml2::XMLElement* e, const char* local) {
    for (auto* c = e ? e->FirstChildElement() : nullptr; c; c = c->NextSiblingElement())
        if (std::strcmp(LocalName(c), local) == 0) return c;
    return nullptr;
}

const tinyxml2::XMLElement* Find(const tinyxml2::XMLElement* root, const char* local) {
    for (auto* c = root ? root->FirstChildElement() : nullptr; c; c = c->NextSiblingElement()) {
        if (std::strcmp(LocalName(c), local) == 0) return c;
        if (auto* found = Find(c, local)) return found;
    }
    return nullptr;
}

long ChildNumber(const tinyxml2::XMLElement* e, const char* local) {
    auto* c = Child(e, local);
    const char* text = c ? c->GetText() : nullptr;
    return text ? std::strtol(text, nullptr, 10) : 0;
}

std::string XmlEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

// "xl/worksheets/sheet1.xml" + "../drawings/drawing1.xml" -> "xl/drawings/drawing1.xml".
std::string ResolvePart(const std::string& from, const std::string& target) {
    if (!target.empty() && target[0] == '/') return target.substr(1);
    std::vector<std::string> parts;
    const size_t slash = from.rfind('/');
    std::string base = slash == std::string::npos ? std::string() : from.substr(0, slash);
    std::stringstream path(base.empty() ? target : base + "/" + target);
    std::string piece;
    while (std::getline(path, piece, '/')) {
        if (piece == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!piece.empty() && piece != ".") parts.push_back(piece);
    }
    std::string out;
    for (const std::string& part : parts) out += (out.empty() ? "" : "/") + part;
    return out;
}

// "xl/worksheets/sheet1.xml" -> "xl/worksheets/_rels/sheet1.xml.rels".
std::string RelsPartOf(const std::string& part) {
    const size_t slash = part.rfind('/');
    if (slash == std::string::npos) return "_rels/" + part + ".rels";
    return part.substr(0, slash) + "/_rels/" + part.substr(slash + 1) + ".rels";
}

// A part's relationships: id -> the part it names (resolved).
std::map<std::string, std::string> ReadRelationships(const UCZipPackageReader& package, const std::string& part) {
    std::map<std::string, std::string> out;
    std::string xml;
    if (!package.ReadEntry(RelsPartOf(part), xml)) return out;
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS || !doc.RootElement()) return out;
    for (auto* rel = doc.RootElement()->FirstChildElement(); rel; rel = rel->NextSiblingElement()) {
        if (std::strcmp(LocalName(rel), "Relationship") != 0) continue;
        if (std::string(Attr(rel, "TargetMode")) == "External") continue;
        out[Attr(rel, "Id")] = ResolvePart(part, Attr(rel, "Target"));
    }
    return out;
}

// The series' ranges of a chart read from a file, matched to the series the
// chart was read with; the chart keeps its cached values where they do not
// match (a stock or bubble chart, whose series the model orders its own way).
void TakeSources(SpreadsheetChart& chart, std::vector<SpreadsheetChartSeries> sources) {
    if (chart.chart.kind == RichChart::Kind::Stock) return;
    if (sources.size() != chart.chart.series.size()) return;
    // A bubble series without a reference to its sizes keeps its cache.
    if (chart.chart.kind == RichChart::Kind::Bubble) {
        for (const SpreadsheetChartSeries& source : sources) {
            if (!source.sizes.IsSet()) return;
        }
    }
    for (const SpreadsheetChartSeries& source : sources) {
        if (!source.values.IsSet()) return;
    }
    chart.sources = std::move(sources);
}

// The chart as a file keeps it: every series (and a pie's every slice) in
// the colour it is drawn in - the office palette where it states none - so
// a program without the palette, or a package without a theme to take it
// from (LibreOffice drew an .xlsx chart's columns in no colour at all),
// shows what the grid does.
RichChart WithColours(const RichChart& given) {
    static const char* const kOffice[] = {"#4472C4", "#ED7D31", "#A5A5A5", "#FFC000", "#5B9BD5", "#70AD47"};
    RichChart chart = given;
    for (size_t i = 0; i < chart.series.size(); ++i) {
        if (chart.series[i].color.empty()) chart.series[i].color = kOffice[i % 6];
    }
    if ((chart.kind == RichChart::Kind::Pie || chart.kind == RichChart::Kind::Doughnut) && !chart.series.empty()) {
        if (chart.pointColors.size() < chart.series.front().values.size())
            chart.pointColors.resize(chart.series.front().values.size());
        for (size_t i = 0; i < chart.pointColors.size(); ++i) {
            if (chart.pointColors[i].empty()) chart.pointColors[i] = kOffice[i % 6];
        }
    }
    return chart;
}

// ----- the UltraCanvas extension ------------------------------------------------
// A chart whose kind the file format cannot name is written as a column chart
// of its numbers - what Excel and LibreOffice show - and the real kind with
// its ranges goes into an extension both ignore:
//   .xlsx: <c:extLst><c:ext uri="{...}"><ucc:chart kind="sankey" ...>
//            <ucc:series name="Sheet1!$B$1" categories="..." values="..." sizes="..."/>
//   .ods:  <ucchart:chart ucchart:kind="sankey" ...><ucchart:series .../></ucchart:chart>
//          as the last child of chart:chart, in a namespace of our own.

const char* const kExtensionUri = "{8E1F6B2A-4C3D-4A57-9B0E-2D6C71A5F3B8}";
const char* const kExtensionNamespace = "http://schemas.ultracanvas.org/spreadsheet/chart/2026";

// Whether .xlsx's chart part has a form of `kind` (c:barChart ... c:surface3DChart).
bool NativeInXlsx(RichChart::Kind kind) {
    switch (kind) {
        case RichChart::Kind::Column: case RichChart::Kind::Bar: case RichChart::Kind::Line: case RichChart::Kind::Area:
        case RichChart::Kind::Pie: case RichChart::Kind::Doughnut: case RichChart::Kind::Scatter:
        case RichChart::Kind::Radar: case RichChart::Kind::Bubble: case RichChart::Kind::Stock:
        case RichChart::Kind::Surface: case RichChart::Kind::Contour:
            return true;
        default:
            return false;
    }
}

// Whether .ods's chart object has a form of `kind` and its ranges (a bubble
// chart's sizes are kept in the extension).
bool NativeInOds(RichChart::Kind kind) {
    switch (kind) {
        case RichChart::Kind::Column: case RichChart::Kind::Bar: case RichChart::Kind::Line: case RichChart::Kind::Area:
        case RichChart::Kind::Pie: case RichChart::Kind::Doughnut: case RichChart::Kind::Scatter:
        case RichChart::Kind::Radar: case RichChart::Kind::Stock:
            return true;
        default:
            return false;
    }
}

// A series of words (a Sankey diagram's targets, a treemap's level).
bool IsWordSeries(const RichChartSeries& one) {
    size_t words = 0;
    for (const std::string& label : one.labels) words += label.empty() ? 0 : 1;
    return words > 0 && words * 2 > one.labels.size();
}

// The column chart a format without the kind shows: the series of numbers,
// over the chart's categories. `keep` gets the indexes of the series kept.
RichChart FallbackChart(const RichChart& given, std::vector<size_t>& keep) {
    RichChart chart = given;
    chart.kind = RichChart::Kind::Column;
    chart.stacked = chart.percent = chart.filled = false;
    chart.series.clear();
    keep.clear();
    for (size_t i = 0; i < given.series.size(); ++i) {
        if (IsWordSeries(given.series[i])) continue;
        RichChartSeries one = given.series[i];
        one.labels.clear();
        one.sizes.clear();
        one.xValues.clear();
        chart.series.push_back(std::move(one));
        keep.push_back(i);
    }
    if (chart.series.empty() && !given.series.empty()) {
        chart.series.push_back(given.series.front());
        chart.series.front().labels.clear();
        keep.push_back(0);
    }
    return chart;
}

std::string Flag(bool on) { return on ? "1" : "0"; }

// The extension's chart element: elements named with `prefix` ("ucc:",
// "ucchart:"), attributes with `attributePrefix` ("" for .xlsx's,
// "ucchart:" for .ods's), ranges written by `range`.
std::string ExtensionElement(const SpreadsheetChart& chart, const std::string& prefix, const std::string& attributePrefix,
                             const std::function<std::string(const SpreadsheetChartRange&)>& range) {
    std::ostringstream xml;
    xml.imbue(std::locale::classic());
    const RichChart& model = chart.chart;
    auto attribute = [&](const char* name, const std::string& value) {
        xml << " " << attributePrefix << name << "=\"" << XmlEscape(value) << "\"";
    };
    xml << "<" << prefix << "chart";
    attribute("kind", SpreadsheetChartKindId(model.kind));
    attribute("stacked", Flag(model.stacked));
    attribute("percent", Flag(model.percent));
    attribute("filled", Flag(model.filled));
    attribute("bins", std::to_string(model.bins));
    if (chart.dataRange.IsSet()) attribute("data-range", range(chart.dataRange));
    attribute("series-in-rows", Flag(chart.seriesInRows));
    xml << ">";
    for (const SpreadsheetChartSeries& one : chart.sources) {
        xml << "<" << prefix << "series";
        if (one.name.IsSet()) attribute("name", range(one.name));
        if (one.categories.IsSet()) attribute("categories", range(one.categories));
        if (one.values.IsSet()) attribute("values", range(one.values));
        if (one.sizes.IsSet()) attribute("sizes", range(one.sizes));
        xml << "/>";
    }
    xml << "</" << prefix << "chart>";
    return xml.str();
}

// An attribute of `e` by its local name, whatever its prefix.
std::string LocalAttr(const tinyxml2::XMLElement* e, const char* local) {
    for (const tinyxml2::XMLAttribute* a = e ? e->FirstAttribute() : nullptr; a; a = a->Next()) {
        const char* name = a->Name();
        const char* colon = std::strchr(name, ':');
        if (std::strcmp(colon ? colon + 1 : name, local) == 0) return a->Value();
    }
    return std::string();
}

// The chart element of the extension in `xml` (in our namespace, under any
// prefix), read into `chart`: its kind, look and ranges. False when there is
// none.
bool ReadExtension(const std::string& xml, bool excel, SpreadsheetChart& chart) {
    if (xml.find(kExtensionNamespace) == std::string::npos) return false;
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS || !doc.RootElement()) return false;
    std::string prefix;
    std::function<const tinyxml2::XMLElement*(const tinyxml2::XMLElement*)> find =
        [&](const tinyxml2::XMLElement* at) -> const tinyxml2::XMLElement* {
        for (const tinyxml2::XMLAttribute* a = at->FirstAttribute(); a; a = a->Next()) {
            const std::string name = a->Name();
            if (name.rfind("xmlns:", 0) == 0 && std::string(a->Value()) == kExtensionNamespace) prefix = name.substr(6) + ":";
        }
        if (!prefix.empty() && std::string(at->Name()) == prefix + "chart") return at;
        for (auto* child = at->FirstChildElement(); child; child = child->NextSiblingElement()) {
            if (auto* found = find(child)) return found;
        }
        return nullptr;
    };
    const tinyxml2::XMLElement* found = find(doc.RootElement());
    if (!found) return false;
    RichChart::Kind kind = RichChart::Kind::Column;
    if (!SpreadsheetChartKindFromId(LocalAttr(found, "kind"), kind)) return false;
    auto read = [excel](const std::string& text, SpreadsheetChartRange& out) {
        if (text.empty()) return;
        if (excel) SpreadsheetChartRange::FromExcel(text, out);
        else SpreadsheetChartRange::FromOdf(text, out);
    };
    std::vector<SpreadsheetChartSeries> sources;
    for (auto* series = found->FirstChildElement(); series; series = series->NextSiblingElement()) {
        if (std::strcmp(LocalName(series), "series") != 0) continue;
        SpreadsheetChartSeries one;
        read(LocalAttr(series, "name"), one.name);
        read(LocalAttr(series, "categories"), one.categories);
        read(LocalAttr(series, "values"), one.values);
        read(LocalAttr(series, "sizes"), one.sizes);
        sources.push_back(std::move(one));
    }
    chart.chart.kind = kind;
    chart.chart.stacked = LocalAttr(found, "stacked") == "1";
    chart.chart.percent = LocalAttr(found, "percent") == "1";
    chart.chart.filled = LocalAttr(found, "filled") == "1";
    chart.chart.bins = std::max(0, std::atoi(LocalAttr(found, "bins").c_str()));
    read(LocalAttr(found, "data-range"), chart.dataRange);
    chart.seriesInRows = LocalAttr(found, "series-in-rows") == "1";
    if (!sources.empty()) {
        // The series as the extension lists them: their data is read from
        // the cells again (RefreshSpreadsheetChart).
        std::vector<RichChartSeries> looks = chart.chart.series;
        looks.resize(sources.size());
        chart.chart.series = std::move(looks);
        chart.sources = std::move(sources);
    }
    return true;
}

std::string Centimetres(int pixels) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(4) << SpreadsheetPixelsToCentimetres(pixels) << "cm";
    return out.str();
}

} // namespace

// ============================================================================
// .XLSX
// ============================================================================

void ReadXlsxSheetCharts(const UCZipPackageReader& package, const std::string& sheetPart, SpreadsheetSheet& sheet) {
    const auto sheetRels = ReadRelationships(package, sheetPart);
    std::string drawingXml;
    for (const auto& [id, target] : sheetRels) {
        if (target.find("drawings/") == std::string::npos) continue;
        if (!package.ReadEntry(target, drawingXml)) continue;
        tinyxml2::XMLDocument drawing;
        if (drawing.Parse(drawingXml.c_str(), drawingXml.size()) != tinyxml2::XML_SUCCESS || !drawing.RootElement()) continue;
        const auto drawingRels = ReadRelationships(package, target);
        for (auto* anchor = drawing.RootElement()->FirstChildElement(); anchor; anchor = anchor->NextSiblingElement()) {
            const std::string kind = LocalName(anchor);
            if (kind != "twoCellAnchor" && kind != "oneCellAnchor") continue;
            auto* frame = Child(anchor, "graphicFrame");
            auto* chartRef = frame ? Find(frame, "chart") : nullptr;
            const char* relId = chartRef ? chartRef->Attribute("r:id") : nullptr;
            if (!relId) continue;
            auto found = drawingRels.find(relId);
            std::string chartXml;
            if (found == drawingRels.end() || !package.ReadEntry(found->second, chartXml)) continue;
            SpreadsheetChart chart;
            if (!ReadDrawingMLChart(chartXml, DrawingMLDefaultThemeColors(), chart.chart)) continue;
            if (auto* names = Find(frame, "cNvPr")) chart.name = Attr(names, "name");
            // Where it stands: two cells and how far into each (EMU).
            auto* from = Child(anchor, "from");
            chart.anchor.from = CellAddress(static_cast<int>(ChildNumber(from, "row")), static_cast<int>(ChildNumber(from, "col")));
            chart.anchor.fromOffsetX = static_cast<int>(std::lround(static_cast<double>(ChildNumber(from, "colOff")) / kEmuPerPixel));
            chart.anchor.fromOffsetY = static_cast<int>(std::lround(static_cast<double>(ChildNumber(from, "rowOff")) / kEmuPerPixel));
            if (auto* to = Child(anchor, "to")) {
                chart.anchor.to = CellAddress(static_cast<int>(ChildNumber(to, "row")), static_cast<int>(ChildNumber(to, "col")));
                chart.anchor.toOffsetX = static_cast<int>(std::lround(static_cast<double>(ChildNumber(to, "colOff")) / kEmuPerPixel));
                chart.anchor.toOffsetY = static_cast<int>(std::lround(static_cast<double>(ChildNumber(to, "rowOff")) / kEmuPerPixel));
            } else {
                // One cell and a size.
                auto* extent = Child(anchor, "ext");
                Rect2Di box = SpreadsheetChartBox(sheet, chart.anchor);
                box.width = static_cast<int>(std::lround(extent ? extent->DoubleAttribute("cx") / kEmuPerPixel : 480.0));
                box.height = static_cast<int>(std::lround(extent ? extent->DoubleAttribute("cy") / kEmuPerPixel : 288.0));
                const CellAddress start = chart.anchor.from;
                const int dx = chart.anchor.fromOffsetX, dy = chart.anchor.fromOffsetY;
                chart.anchor = SpreadsheetChartAnchorForBox(sheet, box);
                chart.anchor.from = start;
                chart.anchor.fromOffsetX = dx;
                chart.anchor.fromOffsetY = dy;
            }
            // Its data: the cells the series' formulas name - or, for a kind
            // the format has no form for, those the extension names.
            if (!ReadExtension(chartXml, true, chart)) {
                const bool scatter = chart.chart.kind == RichChart::Kind::Scatter || chart.chart.kind == RichChart::Kind::Bubble;
                std::vector<SpreadsheetChartSeries> sources;
                for (const DrawingMLSeriesSource& source : ReadDrawingMLChartSources(chartXml)) {
                    SpreadsheetChartSeries one;
                    SpreadsheetChartRange::FromExcel(source.name, one.name);
                    SpreadsheetChartRange::FromExcel(scatter ? source.xValues : source.categories, one.categories);
                    SpreadsheetChartRange::FromExcel(source.values, one.values);
                    SpreadsheetChartRange::FromExcel(source.sizes, one.sizes);
                    sources.push_back(std::move(one));
                }
                TakeSources(chart, std::move(sources));
            }
            TouchSpreadsheetChart(chart);
            sheet.GetCharts().push_back(std::move(chart));
        }
    }
}

std::vector<std::pair<std::string, std::string>> WriteXlsxCharts(const UltraCanvasSpreadsheet& workbook,
                                                                  std::vector<std::string>& sheetXmls,
                                                                  std::string& contentTypesXml) {
    std::vector<std::pair<std::string, std::string>> parts;
    std::string types;
    int drawingNumber = 0, chartNumber = 0;
    for (int s = 0; s < workbook.GetSheetCount() && s < static_cast<int>(sheetXmls.size()); ++s) {
        const SpreadsheetSheet* sheet = workbook.GetSheet(s);
        if (!sheet || sheet->GetCharts().empty()) continue;
        ++drawingNumber;
        const std::string drawingName = "drawing" + std::to_string(drawingNumber) + ".xml";
        std::ostringstream drawing, drawingRels;
        drawing.imbue(std::locale::classic());
        drawingRels.imbue(std::locale::classic());
        drawing << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                   "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\""
                   " xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">";
        drawingRels << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                       "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
        int shapeId = 1;
        for (size_t c = 0; c < sheet->GetCharts().size(); ++c) {
            const SpreadsheetChart& chart = sheet->GetCharts()[c];
            ++chartNumber;
            const std::string relId = "rId" + std::to_string(c + 1);
            const std::string chartName = "chart" + std::to_string(chartNumber) + ".xml";
            const SpreadsheetChartAnchor& anchor = chart.anchor;
            auto corner = [&](const char* tag, const CellAddress& cell, int dx, int dy) {
                drawing << "<xdr:" << tag << "><xdr:col>" << cell.col << "</xdr:col><xdr:colOff>"
                        << static_cast<long>(dx) * kEmuPerPixel << "</xdr:colOff><xdr:row>" << cell.row
                        << "</xdr:row><xdr:rowOff>" << static_cast<long>(dy) * kEmuPerPixel << "</xdr:rowOff></xdr:" << tag << ">";
            };
            drawing << "<xdr:twoCellAnchor>";
            corner("from", anchor.from, anchor.fromOffsetX, anchor.fromOffsetY);
            corner("to", anchor.to, anchor.toOffsetX, anchor.toOffsetY);
            const std::string name = chart.name.empty() ? "Chart " + std::to_string(c + 1) : chart.name;
            drawing << "<xdr:graphicFrame macro=\"\"><xdr:nvGraphicFramePr><xdr:cNvPr id=\"" << ++shapeId << "\" name=\""
                    << XmlEscape(name) << "\"/><xdr:cNvGraphicFramePr/></xdr:nvGraphicFramePr>"
                       "<xdr:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/></xdr:xfrm>"
                       "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\">"
                       "<c:chart xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\""
                       " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" r:id=\""
                    << relId << "\"/></a:graphicData></a:graphic></xdr:graphicFrame><xdr:clientData/></xdr:twoCellAnchor>";
            drawingRels << "<Relationship Id=\"" << relId
                        << "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart\""
                           " Target=\"../charts/" << chartName << "\"/>";
            // The chart, its series pointing at the cells; a kind .xlsx has
            // no form for as a column chart of its numbers, the real kind in
            // the extension.
            const bool native = NativeInXlsx(chart.chart.kind);
            const bool scatter = chart.chart.kind == RichChart::Kind::Scatter || chart.chart.kind == RichChart::Kind::Bubble;
            std::vector<size_t> kept;
            const RichChart written = native ? chart.chart : FallbackChart(chart.chart, kept);
            if (native) {
                for (size_t i = 0; i < chart.sources.size(); ++i) kept.push_back(i);
            }
            std::vector<DrawingMLSeriesSource> sources;
            for (size_t i : kept) {
                if (i >= chart.sources.size()) continue;
                const SpreadsheetChartSeries& one = chart.sources[i];
                DrawingMLSeriesSource source;
                source.name = one.name.ToExcel();
                (scatter ? source.xValues : source.categories) = one.categories.ToExcel();
                source.values = one.values.ToExcel();
                source.sizes = one.sizes.ToExcel();
                sources.push_back(std::move(source));
            }
            std::string chartXml = WriteDrawingMLChart(WithColours(written), sources);
            const size_t chartEnd = chartXml.rfind("</c:chartSpace>");
            if (!native && chartEnd != std::string::npos) {
                const std::string extension =
                    ExtensionElement(chart, "ucc:", "", [](const SpreadsheetChartRange& r) { return r.ToExcel(); });
                chartXml.insert(chartEnd, std::string("<c:extLst><c:ext uri=\"") + kExtensionUri + "\" xmlns:ucc=\"" +
                                              kExtensionNamespace + "\">" + extension + "</c:ext></c:extLst>");
            }
            parts.emplace_back("xl/charts/" + chartName, chartXml);
            types += "<Override PartName=\"/xl/charts/" + chartName +
                     "\" ContentType=\"application/vnd.openxmlformats-officedocument.drawingml.chart+xml\"/>";
        }
        drawing << "</xdr:wsDr>";
        drawingRels << "</Relationships>";
        parts.emplace_back("xl/drawings/" + drawingName, drawing.str());
        parts.emplace_back("xl/drawings/_rels/" + drawingName + ".rels", drawingRels.str());
        types += "<Override PartName=\"/xl/drawings/" + drawingName +
                 "\" ContentType=\"application/vnd.openxmlformats-officedocument.drawing+xml\"/>";
        parts.emplace_back("xl/worksheets/_rels/sheet" + std::to_string(s + 1) + ".xml.rels",
                           "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                           "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                           "<Relationship Id=\"rIdUcDrawing1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing\""
                           " Target=\"../drawings/" + drawingName + "\"/></Relationships>");
        // <drawing> goes after the page set-up and breaks, before what the
        // schema puts after it (legacyDrawing, tableParts, extLst ...).
        std::string& xml = sheetXmls[static_cast<size_t>(s)];
        size_t at = std::string::npos;
        for (const char* later : {"<legacyDrawing", "<legacyDrawingHF", "<drawingHF", "<picture", "<oleObjects",
                                  "<controls", "<webPublishItems", "<tableParts", "<extLst", "</worksheet>"}) {
            at = std::min(at, xml.find(later));
        }
        if (at == std::string::npos) continue;
        xml.insert(at, "<drawing xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" r:id=\"rIdUcDrawing1\"/>");
    }
    const size_t end = contentTypesXml.rfind("</Types>");
    if (!types.empty() && end != std::string::npos) contentTypesXml.insert(end, types);
    return parts;
}

// ============================================================================
// .ODS
// ============================================================================

void ReadOdsCharts(UltraCanvasSpreadsheet& workbook, const std::string& contentXml,
                   const std::function<std::string(const std::string&)>& readPart) {
    if (contentXml.find("draw:object") == std::string::npos || !readPart) return;
    tinyxml2::XMLDocument doc;
    if (doc.Parse(contentXml.c_str(), contentXml.size()) != tinyxml2::XML_SUCCESS || !doc.RootElement()) return;
    auto* body = doc.RootElement()->FirstChildElement("office:body");
    auto* spreadsheet = body ? body->FirstChildElement("office:spreadsheet") : nullptr;
    if (!spreadsheet) return;
    for (auto* table = spreadsheet->FirstChildElement("table:table"); table; table = table->NextSiblingElement("table:table")) {
        SpreadsheetSheet* sheet = workbook.GetSheetByName(Attr(table, "table:name"));
        if (!sheet) continue;
        // A frame holding a chart object: its chart, where it stands (`row`,
        // `col` for one in a cell; -1 for one in the table's shapes).
        auto take = [&](const tinyxml2::XMLElement* frame, int row, int col) {
            auto* object = frame->FirstChildElement("draw:object");
            std::string href = Attr(object, "xlink:href");
            if (href.rfind("./", 0) == 0) href.erase(0, 2);
            while (!href.empty() && href.back() == '/') href.pop_back();
            if (href.empty()) return;
            const std::string objectXml = readPart(href + "/content.xml");
            SpreadsheetChart chart;
            if (objectXml.empty() || !ReadOdfChartObject(objectXml, chart.chart)) return;
            chart.name = Attr(frame, "draw:name");
            const int x = SpreadsheetLengthToPixels(Attr(frame, "svg:x"), 0);
            const int y = SpreadsheetLengthToPixels(Attr(frame, "svg:y"), 0);
            const int width = SpreadsheetLengthToPixels(Attr(frame, "svg:width"), 480);
            const int height = SpreadsheetLengthToPixels(Attr(frame, "svg:height"), 288);
            Rect2Di box(x, y, width, height);
            if (row >= 0) {
                // In a cell: its position is from the cell's corner.
                SpreadsheetChartAnchor at;
                at.from = CellAddress(row, col);
                const Rect2Di cell = SpreadsheetChartBox(*sheet, at);
                box.x += cell.x;
                box.y += cell.y;
            }
            chart.anchor = SpreadsheetChartAnchorForBox(*sheet, box);
            SpreadsheetChartRange end;
            const std::string endCell = Attr(frame, "table:end-cell-address");
            if (!endCell.empty() && SpreadsheetChartRange::FromOdf(endCell, end)) {
                chart.anchor.to = end.range.start;
                chart.anchor.toOffsetX = SpreadsheetLengthToPixels(Attr(frame, "table:end-x"), 0);
                chart.anchor.toOffsetY = SpreadsheetLengthToPixels(Attr(frame, "table:end-y"), 0);
            }
            if (!ReadExtension(objectXml, false, chart)) {
                std::vector<SpreadsheetChartSeries> sources;
                for (const OdfChartSeriesSource& source : ReadOdfChartSources(objectXml)) {
                    SpreadsheetChartSeries one;
                    SpreadsheetChartRange::FromOdf(source.name, one.name);
                    SpreadsheetChartRange::FromOdf(source.categories, one.categories);
                    SpreadsheetChartRange::FromOdf(source.values, one.values);
                    sources.push_back(std::move(one));
                }
                TakeSources(chart, std::move(sources));
            }
            TouchSpreadsheetChart(chart);
            sheet->GetCharts().push_back(std::move(chart));
        };
        if (auto* shapes = table->FirstChildElement("table:shapes")) {
            for (auto* frame = shapes->FirstChildElement("draw:frame"); frame; frame = frame->NextSiblingElement("draw:frame"))
                take(frame, -1, -1);
        }
        // Frames anchored to cells, walking the rows and cells as the table
        // repeats them.
        int row = 0;
        std::function<void(const tinyxml2::XMLElement*)> rows = [&](const tinyxml2::XMLElement* parent) {
            for (auto* child = parent->FirstChildElement(); child && row < SpreadsheetLimits::MaxRows; child = child->NextSiblingElement()) {
                const std::string tag = child->Name() ? child->Name() : "";
                if (tag == "table:table-row-group" || tag == "table:table-header-rows" || tag == "table:table-rows") {
                    rows(child);
                    continue;
                }
                if (tag != "table:table-row") continue;
                const int repeated = std::max(1, child->IntAttribute("table:number-rows-repeated", 1));
                int col = 0;
                for (auto* cell = child->FirstChildElement(); cell && col < SpreadsheetLimits::MaxColumns; cell = cell->NextSiblingElement()) {
                    const std::string cellTag = cell->Name() ? cell->Name() : "";
                    if (cellTag != "table:table-cell" && cellTag != "table:covered-table-cell") continue;
                    for (auto* frame = cell->FirstChildElement("draw:frame"); frame; frame = frame->NextSiblingElement("draw:frame"))
                        take(frame, row, col);
                    col += std::max(1, cell->IntAttribute("table:number-columns-repeated", 1));
                }
                row += repeated;
            }
        };
        rows(table);
    }
}

std::string WriteOdsSheetCharts(const SpreadsheetSheet& sheet, std::vector<std::pair<std::string, std::string>>& objects) {
    if (sheet.GetCharts().empty()) return std::string();
    std::ostringstream xml;
    xml.imbue(std::locale::classic());
    xml << "<table:shapes>";
    int z = 0;
    for (const SpreadsheetChart& chart : sheet.GetCharts()) {
        const std::string object = "Object " + std::to_string(objects.size() + 1);
        const Rect2Di box = SpreadsheetChartBox(sheet, chart.anchor);
        std::vector<OdfChartSeriesSource> sources;
        std::string ranges;
        auto note = [&ranges](const SpreadsheetChartRange& part) {
            if (part.IsSet()) ranges += (ranges.empty() ? "" : " ") + part.ToOdf();
        };
        // A kind .ods has no form for: a column chart of its numbers, the
        // real kind and its ranges in the extension.
        const bool native = NativeInOds(chart.chart.kind);
        std::vector<size_t> kept;
        const RichChart written = native ? chart.chart : FallbackChart(chart.chart, kept);
        if (native) {
            for (size_t i = 0; i < chart.sources.size(); ++i) kept.push_back(i);
        }
        for (size_t i : kept) {
            if (i >= chart.sources.size()) continue;
            const SpreadsheetChartSeries& one = chart.sources[i];
            sources.push_back({one.name.ToOdf(), one.categories.ToOdf(), one.values.ToOdf()});
        }
        for (const SpreadsheetChartSeries& one : chart.sources) {
            note(one.name);
            note(one.categories);
            note(one.values);
            note(one.sizes);
        }
        const float widthPt = static_cast<float>(box.width) * 0.75f, heightPt = static_cast<float>(box.height) * 0.75f;
        std::string objectXml = WriteOdfChartObject(WithColours(written), widthPt, heightPt, sources, chart.dataRange.ToOdf());
        const size_t rootAt = objectXml.find(" office:version=");
        const size_t chartEnd = objectXml.rfind("</chart:chart>");
        if (!native && rootAt != std::string::npos && chartEnd != std::string::npos && rootAt < chartEnd) {
            objectXml.insert(chartEnd, ExtensionElement(chart, "ucchart:", "ucchart:",
                                                        [](const SpreadsheetChartRange& r) { return r.ToOdf(); }));
            objectXml.insert(rootAt, std::string(" xmlns:ucchart=\"") + kExtensionNamespace + "\"");
        }
        objects.emplace_back(object + "/content.xml", objectXml);
        SpreadsheetChartRange end;
        end.sheet = sheet.GetName();
        end.range = CellRange(chart.anchor.to);
        xml << "<draw:frame draw:name=\"" << XmlEscape(chart.name) << "\" draw:z-index=\"" << z++ << "\" svg:x=\""
            << Centimetres(box.x) << "\" svg:y=\"" << Centimetres(box.y) << "\" svg:width=\"" << Centimetres(box.width)
            << "\" svg:height=\"" << Centimetres(box.height) << "\" table:end-cell-address=\"" << XmlEscape(end.ToOdf())
            << "\" table:end-x=\"" << Centimetres(chart.anchor.toOffsetX) << "\" table:end-y=\""
            << Centimetres(chart.anchor.toOffsetY) << "\"><draw:object";
        if (!ranges.empty()) xml << " draw:notify-on-update-of-ranges=\"" << XmlEscape(ranges) << "\"";
        xml << " xlink:href=\"./" << XmlEscape(object) << "\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/>"
               "</draw:frame>";
    }
    xml << "</table:shapes>\n";
    return xml.str();
}

std::string OdsChartManifestEntries(const std::vector<std::pair<std::string, std::string>>& objects) {
    std::string entries;
    for (const auto& [path, xml] : objects) {
        (void)xml;
        const size_t slash = path.find('/');
        const std::string folder = slash == std::string::npos ? path : path.substr(0, slash + 1);
        entries += "<manifest:file-entry manifest:full-path=\"" + XmlEscape(folder) +
                   "\" manifest:media-type=\"application/vnd.oasis.opendocument.chart\"/>\n"
                   "<manifest:file-entry manifest:full-path=\"" + XmlEscape(path) + "\" manifest:media-type=\"text/xml\"/>\n";
    }
    return entries;
}

} // namespace UltraCanvas

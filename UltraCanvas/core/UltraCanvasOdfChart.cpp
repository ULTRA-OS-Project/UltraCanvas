// core/UltraCanvasOdfChart.cpp
// OpenDocument chart objects - see UltraCanvasOdfChart.h.
// Version: 1.1.0 - series ranges into a spreadsheet's cells, written and read back
// Version: 1.0.0 - WriteOdfChartObject and ReadOdfChartObject, moved out of the .odt plugin
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasOdfChart.h"

#include "tinyxml2.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <vector>

namespace UltraCanvas {

namespace {

std::string Escape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

// Lengths go out dot-decimal whatever the process locale.
std::string Pt(float value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(2) << value << "pt";
    return out.str();
}

const char* Attr(const tinyxml2::XMLElement* element, const char* name) {
    const char* value = element ? element->Attribute(name) : nullptr;
    return value ? value : "";
}

} // namespace

std::string WriteOdfChartObject(const RichChart& chart, float widthPt, float heightPt) {
    return WriteOdfChartObject(chart, widthPt, heightPt, {}, std::string());
}

std::vector<OdfChartSeriesSource> ReadOdfChartSources(const std::string& contentXml) {
    std::vector<OdfChartSeriesSource> sources;
    tinyxml2::XMLDocument doc;
    if (doc.Parse(contentXml.c_str(), contentXml.size()) != tinyxml2::XML_SUCCESS || !doc.RootElement()) return sources;
    std::function<const tinyxml2::XMLElement*(const tinyxml2::XMLElement*, const char*)> find =
        [&](const tinyxml2::XMLElement* root, const char* name) -> const tinyxml2::XMLElement* {
            for (auto* child = root->FirstChildElement(); child; child = child->NextSiblingElement()) {
                if (child->Name() && std::string(child->Name()) == name) return child;
                if (auto* found = find(child, name)) return found;
            }
            return nullptr;
        };
    auto* chart = find(doc.RootElement(), "chart:chart");
    auto* plot = chart ? chart->FirstChildElement("chart:plot-area") : nullptr;
    if (!plot) return sources;
    std::string categories;
    for (auto* axis = plot->FirstChildElement("chart:axis"); axis; axis = axis->NextSiblingElement("chart:axis")) {
        if (std::string(Attr(axis, "chart:dimension")) != "x") continue;
        if (auto* names = axis->FirstChildElement("chart:categories")) categories = Attr(names, "table:cell-range-address");
    }
    for (auto* ser = plot->FirstChildElement("chart:series"); ser; ser = ser->NextSiblingElement("chart:series")) {
        OdfChartSeriesSource source;
        source.name = Attr(ser, "chart:label-cell-address");
        source.values = Attr(ser, "chart:values-cell-range-address");
        auto* domain = ser->FirstChildElement("chart:domain");
        source.categories = domain ? Attr(domain, "table:cell-range-address") : categories;
        sources.push_back(std::move(source));
    }
    return sources;
}

std::string WriteOdfChartObject(const RichChart& chart, float widthPt, float heightPt,
                                const std::vector<OdfChartSeriesSource>& sources, const std::string& plotArea) {
    using Kind = RichChart::Kind;
    const bool pie = chart.kind == Kind::Pie || chart.kind == Kind::Doughnut;
    const bool scatter = chart.kind == Kind::Scatter;
    const bool line = chart.kind == Kind::Line;
    const bool radar = chart.kind == Kind::Radar, bubble = chart.kind == Kind::Bubble, stock = chart.kind == Kind::Stock;
    // (ODF's stock series run (open,) low, high, close: the model's high and low change places.)
    std::vector<RichChartSeries> ordered = chart.series;
    if (stock && ordered.size() >= 4) std::swap(ordered[1], ordered[2]);
    else if (stock && ordered.size() == 3) std::swap(ordered[0], ordered[1]);
    bool anySmooth = false;
    for (const RichChartSeries& one : ordered) anySmooth = anySmooth || one.smooth;
    const char* kind = radar ? (chart.filled ? "chart:filled-radar" : "chart:radar") : bubble ? "chart:bubble" : stock ? "chart:stock"
                     : chart.kind == Kind::Line ? "chart:line" : chart.kind == Kind::Area ? "chart:area"
                     : chart.kind == Kind::Pie ? "chart:circle" : chart.kind == Kind::Doughnut ? "chart:ring"
                     : scatter ? "chart:scatter" : "chart:bar";
    std::ostringstream styles, body, table;
    for (std::ostringstream* stream : {&styles, &body, &table}) {
        stream->imbue(std::locale::classic());
        stream->precision(15);
    }
    // The table: the categories down the first column, then a column
    // for each series' values (before it, a scatter series' x).
    size_t rows = chart.categories.size();
    for (const RichChartSeries& one : chart.series) rows = std::max(rows, one.values.size());
    auto column = [](size_t index) {              // 0 -> A, 26 -> AA
        std::string name;
        for (size_t n = index + 1; n > 0; n = (n - 1) / 26) name.insert(name.begin(), static_cast<char>('A' + (n - 1) % 26));
        return name;
    };
    auto range = [&](size_t col) {
        return "local-table.$" + column(col) + "$2:.$" + column(col) + "$" + std::to_string(rows + 1);
    };
    std::vector<size_t> valueColumn, xColumn, sizeColumn;
    size_t columns = 1;
    for (const RichChartSeries& one : ordered) {
        const bool ownX = (scatter || bubble) && one.xValues.size() == one.values.size();
        xColumn.push_back(ownX ? columns++ : 0);
        valueColumn.push_back(columns++);
        sizeColumn.push_back(bubble ? columns++ : 0);
    }
    auto words = [&](const std::string& text) {
        table << "<table:table-cell office:value-type=\"string\"><text:p>" << Escape(text) << "</text:p></table:table-cell>";
    };
    auto number = [&](const std::vector<double>& values, size_t row) {
        if (row < values.size() && std::isfinite(values[row])) {
            table << "<table:table-cell office:value-type=\"float\" office:value=\"" << values[row] << "\"><text:p>"
                  << values[row] << "</text:p></table:table-cell>";
        } else {
            table << "<table:table-cell><text:p/></table:table-cell>";
        }
    };
    table << "<table:table table:name=\"local-table\"><table:table-header-columns><table:table-column/></table:table-header-columns>"
             "<table:table-columns><table:table-column table:number-columns-repeated=\"" << (columns - 1) << "\"/></table:table-columns>"
             "<table:table-header-rows><table:table-row><table:table-cell><text:p/></table:table-cell>";
    for (size_t s = 0; s < ordered.size(); s++) {
        if (xColumn[s] != 0) words("x");
        words(ordered[s].name);
        if (sizeColumn[s] != 0) words(ordered[s].name);
    }
    table << "</table:table-row></table:table-header-rows><table:table-rows>";
    for (size_t r = 0; r < rows; r++) {
        table << "<table:table-row>";
        words(r < chart.categories.size() ? chart.categories[r] : std::string());
        for (size_t s = 0; s < ordered.size(); s++) {
            if (xColumn[s] != 0) number(ordered[s].xValues, r);
            number(ordered[s].values, r);
            if (sizeColumn[s] != 0) {
                std::vector<double> sizes = ordered[s].sizes;
                sizes.resize(ordered[s].values.size(), 1.0);
                number(sizes, r);
            }
        }
        table << "</table:table-row>";
    }
    table << "</table:table-rows></table:table>";

    auto stated = [](const std::string& hex) { return hex.size() == 7 && hex[0] == '#'; };
    styles << "<style:style style:name=\"plot\" style:family=\"chart\"><style:chart-properties"
           << (chart.kind == Kind::Bar ? " chart:vertical=\"true\"" : "")
           << (chart.percent ? " chart:percentage=\"true\"" : chart.stacked ? " chart:stacked=\"true\"" : "")
           << (line || scatter || radar ? " chart:lines=\"true\"" : "")
           << (anySmooth && (line || scatter) ? " chart:interpolation=\"cubic-spline\"" : "")
           << (stock && ordered.size() >= 4 ? " chart:japanese-candle-stick=\"true\"" : "") << "/></style:style>";
    auto axisStyle = [&](const char* name, double low, double high, bool show) {
        styles << "<style:style style:name=\"" << name << "\" style:family=\"chart\"><style:chart-properties chart:display-label=\""
               << (show ? "true" : "false") << "\"";
        if (std::isfinite(low)) styles << " chart:minimum=\"" << low << "\"";
        if (std::isfinite(high)) styles << " chart:maximum=\"" << high << "\"";
        styles << "/></style:style>";
    };
    axisStyle("axisX", scatter || bubble ? chart.xMin : std::numeric_limits<double>::quiet_NaN(),
              scatter || bubble ? chart.xMax : std::numeric_limits<double>::quiet_NaN(), !pie);
    axisStyle("axisY", chart.valueMin, chart.valueMax, !pie);
    // No wall behind the plot, and a pale grid, as the view draws it.
    styles << "<style:style style:name=\"wall\" style:family=\"chart\"><style:graphic-properties draw:stroke=\"none\""
              " draw:fill=\"none\"/></style:style><style:style style:name=\"grid\" style:family=\"chart\">"
              "<style:graphic-properties draw:stroke=\"solid\" svg:stroke-color=\"#d0d0d0\"/></style:style>";

    body << "<chart:chart chart:class=\"" << kind << "\" svg:width=\"" << Pt(widthPt) << "\" svg:height=\"" << Pt(heightPt)
         // "." - the data is the chart's own table; ".." - the document the
         // chart stands in (a spreadsheet's cells): LibreOffice takes a
         // chart's ranges into the sheets only with "..".
         << "\" xlink:href=\"" << (sources.empty() ? "." : "..") << "\" xlink:type=\"simple\">";
    if (!chart.title.empty()) body << "<chart:title><text:p>" << Escape(chart.title) << "</text:p></chart:title>";
    if (chart.legend) body << "<chart:legend chart:legend-position=\"end\"/>";
    // (A spreadsheet's chart: the ranges its sources name, in the sheets.)
    auto sourceOf = [&](size_t s) { return s < sources.size() ? sources[s] : OdfChartSeriesSource(); };
    body << "<chart:plot-area chart:style-name=\"plot\" chart:data-source-has-labels=\"both\"";
    if (!plotArea.empty()) body << " table:cell-range-address=\"" << Escape(plotArea) << "\"";
    body << "><chart:axis chart:dimension=\"x\" chart:name=\"primary-x\" chart:style-name=\"axisX\">";
    const std::string categoriesAt = !sources.empty() ? sourceOf(0).categories : range(0);
    if (!scatter && !bubble && !categoriesAt.empty())
        body << "<chart:categories table:cell-range-address=\"" << Escape(categoriesAt) << "\"/>";
    else body << "<chart:grid chart:class=\"major\" chart:style-name=\"grid\"/>";
    body << "</chart:axis><chart:axis chart:dimension=\"y\" chart:name=\"primary-y\" chart:style-name=\"axisY\">"
         << (pie ? "" : "<chart:grid chart:class=\"major\" chart:style-name=\"grid\"/>") << "</chart:axis>";
    for (size_t s = 0; s < ordered.size(); s++) {
        const RichChartSeries& one = ordered[s];
        const std::string name = "series" + std::to_string(s + 1);
        styles << "<style:style style:name=\"" << name << "\" style:family=\"chart\"><style:graphic-properties";
        if (stock) {
            styles << " draw:stroke=\"solid\"/><style:chart-properties chart:symbol-type=\"none\"/>";
        } else if (line || scatter || (radar && !chart.filled)) {
            styles << " draw:stroke=\"" << (one.joined ? "solid" : "none") << "\" svg:stroke-width=\"0.08cm\"";
            if (stated(one.color)) styles << " svg:stroke-color=\"" << one.color << "\" draw:fill-color=\"" << one.color << "\"";
            styles << "/><style:chart-properties chart:symbol-type=\"" << (one.marked ? "automatic" : "none") << "\"/>";
        } else {
            styles << " draw:stroke=\"none\"";
            if (stated(one.color)) styles << " draw:fill=\"solid\" draw:fill-color=\"" << one.color << "\"";
            styles << "/>";
        }
        styles << "</style:style>";
        // (A bubble series' values are its sizes; its y and then its x are the ranges it stands over.)
        const size_t valuesAt = bubble ? sizeColumn[s] : valueColumn[s];
        const bool sheetData = !sources.empty() && !bubble && !stock;
        const OdfChartSeriesSource source = sourceOf(s);
        body << "<chart:series chart:style-name=\"" << name << "\" chart:values-cell-range-address=\""
             << (sheetData ? Escape(source.values) : range(valuesAt)) << "\"";
        if (!sheetData) body << " chart:label-cell-address=\"local-table.$" << column(valueColumn[s]) << "$1\"";
        else if (!source.name.empty()) body << " chart:label-cell-address=\"" << Escape(source.name) << "\"";
        body << " chart:class=\"" << kind << "\">";
        if (bubble) body << "<chart:domain table:cell-range-address=\"" << range(valueColumn[s]) << "\"/>";
        if (scatter && sheetData && !source.categories.empty())
            body << "<chart:domain table:cell-range-address=\"" << Escape(source.categories) << "\"/>";
        else if ((scatter && !sheetData) || bubble)
            body << "<chart:domain table:cell-range-address=\"" << range(xColumn[s]) << "\"/>";
        if (pie && s == 0) {
            for (size_t i = 0; i < one.values.size(); i++) {
                if (i < chart.pointColors.size() && stated(chart.pointColors[i])) {
                    const std::string point = "point" + std::to_string(i + 1);
                    styles << "<style:style style:name=\"" << point << "\" style:family=\"chart\"><style:graphic-properties"
                              " draw:fill=\"solid\" draw:fill-color=\"" << chart.pointColors[i] << "\"/></style:style>";
                    body << "<chart:data-point chart:style-name=\"" << point << "\"/>";
                } else {
                    body << "<chart:data-point/>";
                }
            }
        }
        body << "</chart:series>";
    }
    body << "<chart:wall chart:style-name=\"wall\"/></chart:plot-area>" << table.str() << "</chart:chart>";
    return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
           " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
           " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""
           " xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\""
           " xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\""
           " xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\""
           " xmlns:xlink=\"http://www.w3.org/1999/xlink\""
           " xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\""
           " xmlns:chart=\"urn:oasis:names:tc:opendocument:xmlns:chart:1.0\" office:version=\"1.2\">"
           "<office:automatic-styles>") + styles.str() + "</office:automatic-styles><office:body><office:chart>"
           + body.str() + "</office:chart></office:body></office:document-content>\n";
}

bool ReadOdfChartObject(const std::string& contentXml, RichChart& out) {
tinyxml2::XMLDocument chartDoc;
if (chartDoc.Parse(contentXml.c_str()) != tinyxml2::XML_SUCCESS || !chartDoc.RootElement()) return false;
std::function<tinyxml2::XMLElement*(tinyxml2::XMLElement*, const char*)> find =
    [&](tinyxml2::XMLElement* root, const char* name) -> tinyxml2::XMLElement* {
        for (auto* child = root->FirstChildElement(); child; child = child->NextSiblingElement()) {
            if (child->Name() && std::string(child->Name()) == name) return child;
            if (auto* found = find(child, name)) return found;
        }
        return nullptr;
    };
auto* chart = find(chartDoc.RootElement(), "chart:chart");
auto* plot = chart ? chart->FirstChildElement("chart:plot-area") : nullptr;
if (!plot) return false;

// The chart's automatic styles: what a series looks like, how the
// plot is arranged, an axis' stated range.
struct Look {
    std::string fillColor, strokeColor, stroke, symbol, interpolation;
    bool vertical = false, stacked = false, percentage = false;
    double minimum = std::numeric_limits<double>::quiet_NaN(), maximum = std::numeric_limits<double>::quiet_NaN();
};
std::map<std::string, Look> looks;
if (auto* styles = find(chartDoc.RootElement(), "office:automatic-styles")) {
    for (auto* style = styles->FirstChildElement("style:style"); style; style = style->NextSiblingElement("style:style")) {
        Look look;
        if (auto* graphic = style->FirstChildElement("style:graphic-properties")) {
            look.fillColor = Attr(graphic, "draw:fill-color");
            look.strokeColor = Attr(graphic, "svg:stroke-color");
            look.stroke = Attr(graphic, "draw:stroke");
            if (std::string(Attr(graphic, "draw:fill")) == "none") look.fillColor.clear();
        }
        if (auto* props = style->FirstChildElement("style:chart-properties")) {
            look.symbol = Attr(props, "chart:symbol-type");
            look.interpolation = Attr(props, "chart:interpolation");
            look.vertical = std::string(Attr(props, "chart:vertical")) == "true";
            look.stacked = std::string(Attr(props, "chart:stacked")) == "true";
            look.percentage = std::string(Attr(props, "chart:percentage")) == "true";
            if (props->Attribute("chart:minimum")) look.minimum = props->DoubleAttribute("chart:minimum");
            if (props->Attribute("chart:maximum")) look.maximum = props->DoubleAttribute("chart:maximum");
        }
        looks[Attr(style, "style:name")] = look;
    }
}
auto lookOf = [&](tinyxml2::XMLElement* element) {
    auto found = looks.find(Attr(element, "chart:style-name"));
    return found != looks.end() ? found->second : Look();
};

// The chart's own table: every row's cells, a repeated cell as often
// as it says.
struct Cell { std::string text; double value = std::numeric_limits<double>::quiet_NaN(); };
std::vector<std::vector<Cell>> grid;
if (auto* table = chart->FirstChildElement("table:table")) {
    std::function<void(tinyxml2::XMLElement*)> rows = [&](tinyxml2::XMLElement* parent) {
        for (auto* child = parent->FirstChildElement(); child; child = child->NextSiblingElement()) {
            const std::string tag = child->Name() ? child->Name() : "";
            if (tag != "table:table-row") {
                if (tag == "table:table-header-rows" || tag == "table:table-rows" || tag == "table:table-row-group") rows(child);
                continue;
            }
            std::vector<Cell> row;
            for (auto* cell = child->FirstChildElement("table:table-cell"); cell; cell = cell->NextSiblingElement("table:table-cell")) {
                Cell one;
                if (auto* words = cell->FirstChildElement("text:p")) one.text = words->GetText() ? words->GetText() : "";
                if (cell->Attribute("office:value")) one.value = cell->DoubleAttribute("office:value");
                const int repeated = std::clamp(cell->IntAttribute("table:number-columns-repeated", 1), 1, 1024);
                for (int i = 0; i < repeated && row.size() < 4096; i++) row.push_back(one);
            }
            if (grid.size() < 65536) grid.push_back(std::move(row));
        }
    };
    rows(table);
}
// "local-table.$B$2:.$B$5" (or one cell): the cells it names.
auto cellsOf = [&](const std::string& range) {
    std::vector<Cell> cells;
    std::vector<std::pair<int, int>> corners;       // column, row (from 0)
    size_t at = 0;
    while (at <= range.size() && corners.size() < 2) {
        size_t end = range.find(':', at);
        if (end == std::string::npos) end = range.size();
        std::string part = range.substr(at, end - at);
        const size_t dot = part.rfind('.');
        if (dot != std::string::npos) part = part.substr(dot + 1);
        int column = 0, row = 0;
        bool letters = false, digits = false;
        for (char c : part) {
            if (std::isalpha(static_cast<unsigned char>(c)) && !digits) { column = column * 26 + (std::toupper(static_cast<unsigned char>(c)) - 'A' + 1); letters = true; }
            else if (std::isdigit(static_cast<unsigned char>(c))) { row = row * 10 + (c - '0'); digits = true; }
        }
        if (letters && digits && column <= 4096 && row <= 65536) corners.emplace_back(column - 1, row - 1);
        at = end + 1;
    }
    if (corners.empty()) return cells;
    if (corners.size() == 1) corners.push_back(corners.front());
    for (int r = std::min(corners[0].second, corners[1].second); r <= std::max(corners[0].second, corners[1].second); r++) {
        for (int c = std::min(corners[0].first, corners[1].first); c <= std::max(corners[0].first, corners[1].first); c++) {
            cells.push_back(r < static_cast<int>(grid.size()) && c < static_cast<int>(grid[static_cast<size_t>(r)].size())
                            ? grid[static_cast<size_t>(r)][static_cast<size_t>(c)] : Cell());
        }
    }
    return cells;
};
auto numbersOf = [&](const std::string& range) {
    std::vector<double> values;
    for (const Cell& cell : cellsOf(range)) values.push_back(std::isfinite(cell.value) ? cell.value : 0.0);
    return values;
};

RichChart data;
const std::string kind = Attr(chart, "chart:class");
const Look plotLook = lookOf(plot);
if (kind == "chart:bar") data.kind = plotLook.vertical ? RichChart::Kind::Bar : RichChart::Kind::Column;
else if (kind == "chart:line") data.kind = RichChart::Kind::Line;
else if (kind == "chart:area") data.kind = RichChart::Kind::Area;
else if (kind == "chart:circle") data.kind = RichChart::Kind::Pie;
else if (kind == "chart:ring") data.kind = RichChart::Kind::Doughnut;
else if (kind == "chart:scatter") data.kind = RichChart::Kind::Scatter;
else if (kind == "chart:radar" || kind == "chart:filled-radar") data.kind = RichChart::Kind::Radar;
else if (kind == "chart:bubble") data.kind = RichChart::Kind::Bubble;
else if (kind == "chart:stock") data.kind = RichChart::Kind::Stock;
else return false;
const bool radar = data.kind == RichChart::Kind::Radar, bubble = data.kind == RichChart::Kind::Bubble;
const bool stock = data.kind == RichChart::Kind::Stock;
data.filled = kind == "chart:filled-radar";
auto curved = [](const Look& look) { return !look.interpolation.empty() && look.interpolation != "none"; };
const bool scatter = data.kind == RichChart::Kind::Scatter, line = data.kind == RichChart::Kind::Line;
const bool pie = data.kind == RichChart::Kind::Pie || data.kind == RichChart::Kind::Doughnut;
data.percent = plotLook.percentage && !scatter && !radar && !bubble && !stock;
data.stacked = (plotLook.stacked || plotLook.percentage) && !scatter && !radar && !bubble && !stock;
data.legend = chart->FirstChildElement("chart:legend") != nullptr;
if (auto* title = chart->FirstChildElement("chart:title")) {
    if (auto* words = title->FirstChildElement("text:p")) data.title = words->GetText() ? words->GetText() : "";
}
for (auto* axis = plot->FirstChildElement("chart:axis"); axis; axis = axis->NextSiblingElement("chart:axis")) {
    const std::string dimension = Attr(axis, "chart:dimension");
    const Look axisLook = lookOf(axis);
    if (dimension == "x") {
        if (auto* categories = axis->FirstChildElement("chart:categories")) {
            for (const Cell& cell : cellsOf(Attr(categories, "table:cell-range-address"))) data.categories.push_back(cell.text);
            // (Rows past the last category's are values without a name.)
            while (!data.categories.empty() && data.categories.back().empty()) data.categories.pop_back();
        }
        if (scatter || bubble) { data.xMin = axisLook.minimum; data.xMax = axisLook.maximum; }
    } else if (dimension == "y") {
        data.valueMin = axisLook.minimum;
        data.valueMax = axisLook.maximum;
    }
}
for (auto* ser = plot->FirstChildElement("chart:series"); ser; ser = ser->NextSiblingElement("chart:series")) {
    RichChartSeries one;
    one.values = numbersOf(Attr(ser, "chart:values-cell-range-address"));
    if (one.values.empty()) continue;
    const std::vector<Cell> label = cellsOf(Attr(ser, "chart:label-cell-address"));
    if (!label.empty()) one.name = label.front().text;
    const Look look = lookOf(ser);
    if (scatter || bubble) {
        // The ranges the points stand over: a scatter series' x; a
        // bubble series' y, then its x (its values are the sizes).
        // A series without one shares the first series'.
        std::vector<std::vector<double>> domains;
        for (auto* domain = ser->FirstChildElement("chart:domain"); domain; domain = domain->NextSiblingElement("chart:domain")) {
            domains.push_back(numbersOf(Attr(domain, "table:cell-range-address")));
        }
        if (bubble) {
            if (domains.empty()) continue;
            one.sizes = one.values;
            one.values = domains[0];
            one.sizes.resize(one.values.size(), 1.0);
            if (domains.size() > 1) one.xValues = domains[1];
            one.joined = false;
            one.marked = true;
        } else if (!domains.empty()) {
            one.xValues = domains[0];
        }
        if (one.xValues.empty() && !data.series.empty()) one.xValues = data.series.front().xValues;
        if (one.xValues.size() != one.values.size()) one.xValues.clear();
    }
    if (bubble) {
        one.color = look.fillColor;
    } else if (line || scatter || (radar && !data.filled)) {
        one.color = !look.strokeColor.empty() ? look.strokeColor : look.fillColor;
        one.joined = look.stroke != "none";
        one.marked = look.symbol.empty() ? scatter : look.symbol != "none";
        one.smooth = !radar && (curved(look) || curved(plotLook));
    } else {
        one.color = look.fillColor;
    }
    if (pie && data.series.empty()) {
        for (auto* point = ser->FirstChildElement("chart:data-point"); point; point = point->NextSiblingElement("chart:data-point")) {
            const int repeated = std::clamp(point->IntAttribute("chart:repeated", 1), 1, 4096);
            for (int i = 0; i < repeated && data.pointColors.size() < 4096; i++) data.pointColors.push_back(lookOf(point).fillColor);
        }
    }
    data.series.push_back(std::move(one));
}
if (data.series.empty()) return false;
if (stock) {
    // ODF's order is (open,) low, high, close; the model's (open,) high, low, close.
    if (data.series.size() >= 4) std::swap(data.series[1], data.series[2]);
    else if (data.series.size() == 3) std::swap(data.series[0], data.series[1]);
    else return false;
    // (Writer keeps Word's order - high before low - in a chart it
    // converted: where every "high" lies under its "low", they are
    // the other way round.)
    RichChartSeries& high = data.series[data.series.size() >= 4 ? 1 : 0];
    RichChartSeries& low = data.series[data.series.size() >= 4 ? 2 : 1];
    bool under = false, over = false;
    for (size_t i = 0; i < high.values.size() && i < low.values.size(); i++) {
        under = under || high.values[i] < low.values[i];
        over = over || high.values[i] > low.values[i];
    }
    if (under && !over) std::swap(high, low);
}
    out = std::move(data);
    return true;
}

} // namespace UltraCanvas

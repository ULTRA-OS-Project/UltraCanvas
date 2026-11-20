// core/UltraCanvasDrawingML.cpp
// DrawingML charts and colours - see UltraCanvasDrawingML.h.
// Version: 1.0.0 - ReadDrawingMLChart and DrawingMLColor, moved out of the .docx reader
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework

#include "UltraCanvasDrawingML.h"
#include "UltraCanvasTextUtils.h"   // ParseFloatClassic

#include "tinyxml2.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace UltraCanvas {

namespace {

// The element's name without its namespace prefix.
const char* LocalName(const tinyxml2::XMLElement* e) {
    const char* name = e && e->Name() ? e->Name() : "";
    const char* colon = std::strchr(name, ':');
    return colon ? colon + 1 : name;
}

bool Is(const tinyxml2::XMLElement* e, const char* local) {
    return e && std::strcmp(LocalName(e), local) == 0;
}

const tinyxml2::XMLElement* Child(const tinyxml2::XMLElement* e, const char* local) {
    for (auto* c = e ? e->FirstChildElement() : nullptr; c; c = c->NextSiblingElement())
        if (Is(c, local)) return c;
    return nullptr;
}

const tinyxml2::XMLElement* Next(const tinyxml2::XMLElement* e, const char* local) {
    for (auto* c = e ? e->NextSiblingElement() : nullptr; c; c = c->NextSiblingElement())
        if (Is(c, local)) return c;
    return nullptr;
}

// The first element named `local` under `root`, depth first.
const tinyxml2::XMLElement* Find(const tinyxml2::XMLElement* root, const char* local) {
    for (auto* c = root ? root->FirstChildElement() : nullptr; c; c = c->NextSiblingElement()) {
        if (Is(c, local)) return c;
        if (auto* found = Find(c, local)) return found;
    }
    return nullptr;
}

std::string Attr(const tinyxml2::XMLElement* e, const char* name) {
    const char* value = e ? e->Attribute(name) : nullptr;
    return value ? value : "";
}

// A dot-decimal number at the start of `text` (a leading '+' allowed), never
// read through the process locale. False when there is none.
bool ReadNumber(const std::string& text, double& out) {
    size_t from = 0;
    if (from < text.size() && text[from] == '+') ++from;
    if (from >= text.size()) return false;
    const char* begin = text.c_str() + from;
    return ParseFloatClassic(begin, text.c_str() + text.size(), out) != begin;
}

// The c:pt values under `from` by their c:idx, in order (gaps left empty).
void CollectPoints(const tinyxml2::XMLElement* from, std::vector<std::string>& out) {
    for (auto* child = from ? from->FirstChildElement() : nullptr; child; child = child->NextSiblingElement()) {
        if (Is(child, "pt")) {
            const size_t index = static_cast<size_t>(std::max(0, child->IntAttribute("idx", static_cast<int>(out.size()))));
            if (index < 4096) {
                if (out.size() <= index) out.resize(index + 1);
                auto* value = Child(child, "v");
                out[index] = value && value->GetText() ? value->GetText() : "";
            }
        } else {
            CollectPoints(child, out);
        }
    }
}

// The fill a chart's shape properties (c:spPr) state: its own, else its
// outline's.
const tinyxml2::XMLElement* StatedFill(const tinyxml2::XMLElement* look) {
    if (!look) return nullptr;
    if (auto* fill = Child(look, "solidFill")) return fill;
    auto* line = Child(look, "ln");
    return line ? Child(line, "solidFill") : nullptr;
}

} // namespace

const std::map<std::string, std::string>& DrawingMLDefaultThemeColors() {
    static const std::map<std::string, std::string> scheme = {
        {"dk1", "000000"}, {"lt1", "FFFFFF"}, {"dk2", "44546A"}, {"lt2", "E7E6E6"},
        {"accent1", "4472C4"}, {"accent2", "ED7D31"}, {"accent3", "A5A5A5"},
        {"accent4", "FFC000"}, {"accent5", "5B9BD5"}, {"accent6", "70AD47"},
        {"hlink", "0563C1"}, {"folHlink", "954F72"}};
    return scheme;
}

std::string DrawingMLColor(const tinyxml2::XMLElement* holder, const std::map<std::string, std::string>& themeColors,
                           const std::string& placeholder) {
    if (!holder) return "";
    const tinyxml2::XMLElement* colour = nullptr;
    std::string hex;
    for (auto* child = holder->FirstChildElement(); child && hex.empty(); child = child->NextSiblingElement()) {
        colour = child;
        if (Is(child, "srgbClr")) {
            hex = Attr(child, "val");
        } else if (Is(child, "sysClr")) {
            hex = Attr(child, "lastClr");
        } else if (Is(child, "scrgbClr")) {
            auto channel = [&](const char* name) {
                const float linear = std::clamp(child->FloatAttribute(name, 0.0f) / 100000.0f, 0.0f, 1.0f);
                const float value = linear <= 0.0031308f ? linear * 12.92f
                                                         : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
                return static_cast<int>(std::lround(value * 255.0f));
            };
            char buffer[8];
            std::snprintf(buffer, sizeof buffer, "%02X%02X%02X", channel("r"), channel("g"), channel("b"));
            hex = buffer;
        } else if (Is(child, "schemeClr")) {
            std::string name = Attr(child, "val");
            if (name == "phClr") {
                hex = placeholder.size() == 7 ? placeholder.substr(1) : "";
                continue;
            }
            if (name == "bg1") name = "lt1";
            else if (name == "tx1") name = "dk1";
            else if (name == "bg2") name = "lt2";
            else if (name == "tx2") name = "dk2";
            auto found = themeColors.find(name);
            if (found != themeColors.end()) hex = found->second;
        } else if (Is(child, "prstClr")) {
            static const std::map<std::string, std::string> named = {
                {"black", "000000"}, {"white", "FFFFFF"}, {"red", "FF0000"}, {"green", "008000"},
                {"blue", "0000FF"}, {"yellow", "FFFF00"}, {"gray", "808080"}, {"orange", "FFA500"}};
            auto found = named.find(Attr(child, "val"));
            if (found != named.end()) hex = found->second;
        }
    }
    if (hex.size() != 6 || !colour) return "";
    float rgb[3];
    for (int i = 0; i < 3; ++i) {
        rgb[i] = static_cast<float>(std::strtol(hex.substr(static_cast<size_t>(i) * 2, 2).c_str(), nullptr, 16)) / 255.0f;
    }
    for (auto* change = colour->FirstChildElement(); change; change = change->NextSiblingElement()) {
        const float amount = change->FloatAttribute("val", 100000.0f) / 100000.0f;
        if (Is(change, "shade")) {
            for (float& c : rgb) c *= amount;
        } else if (Is(change, "tint")) {
            for (float& c : rgb) c = c + (1.0f - c) * (1.0f - amount);
        } else if (Is(change, "lumMod") || Is(change, "lumOff")) {
            // In HSL: lightness times lumMod, plus lumOff.
            const float hi = std::max({rgb[0], rgb[1], rgb[2]}), lo = std::min({rgb[0], rgb[1], rgb[2]});
            float l = (hi + lo) * 0.5f;
            float sat = 0.0f, hue = 0.0f;
            if (hi > lo) {
                const float d = hi - lo;
                sat = l > 0.5f ? d / (2.0f - hi - lo) : d / (hi + lo);
                if (hi == rgb[0]) hue = (rgb[1] - rgb[2]) / d + (rgb[1] < rgb[2] ? 6.0f : 0.0f);
                else if (hi == rgb[1]) hue = (rgb[2] - rgb[0]) / d + 2.0f;
                else hue = (rgb[0] - rgb[1]) / d + 4.0f;
                hue /= 6.0f;
            }
            l = std::clamp(Is(change, "lumMod") ? l * amount : l + amount, 0.0f, 1.0f);
            auto toRgb = [](float p, float q, float t) {
                if (t < 0.0f) t += 1.0f;
                if (t > 1.0f) t -= 1.0f;
                if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
                if (t < 0.5f) return q;
                if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
                return p;
            };
            if (sat <= 0.0f) {
                rgb[0] = rgb[1] = rgb[2] = l;
            } else {
                const float q = l < 0.5f ? l * (1.0f + sat) : l + sat - l * sat;
                const float p2 = 2.0f * l - q;
                rgb[0] = toRgb(p2, q, hue + 1.0f / 3.0f);
                rgb[1] = toRgb(p2, q, hue);
                rgb[2] = toRgb(p2, q, hue - 1.0f / 3.0f);
            }
        }
    }
    char out[8];
    std::snprintf(out, sizeof out, "#%02X%02X%02X", static_cast<int>(std::lround(std::clamp(rgb[0], 0.0f, 1.0f) * 255.0f)),
                  static_cast<int>(std::lround(std::clamp(rgb[1], 0.0f, 1.0f) * 255.0f)),
                  static_cast<int>(std::lround(std::clamp(rgb[2], 0.0f, 1.0f) * 255.0f)));
    return out;
}

bool ReadDrawingMLChart(const std::string& chartXml, const std::map<std::string, std::string>& themeColors,
                        RichChart& out) {
    tinyxml2::XMLDocument chart;
    if (chart.Parse(chartXml.c_str(), chartXml.size()) != tinyxml2::XML_SUCCESS || !chart.RootElement()) return false;
    const tinyxml2::XMLElement* root = chart.RootElement();
    auto* plot = Find(root, "plotArea");
    if (!plot) return false;
    auto colourOf = [&](const tinyxml2::XMLElement* fill) { return DrawingMLColor(fill, themeColors); };

    struct Series {
        std::string name, color;
        std::vector<double> values;
        bool stated = false;                // the file states its colour
        std::vector<double> across;         // a scatter chart's x of each value
        bool joined = true, marked = true;  // ...and whether its points are joined by a line, and marked
        std::vector<double> sizes;          // a bubble chart's size of each point
        bool smooth = false;                // a curve through the points
    };
    std::vector<Series> series;
    std::vector<std::string> categories;
    bool lines = false, pie = false, horizontal = false, stacked = false, percent = false, scatter = false;
    bool area = false, doughnut = false, lineMarkers = false;
    bool radar = false, radarFilled = false, radarMarkers = false, bubble = false, stock = false;
    std::vector<std::string> pointColors;       // a pie's slices, where the file colours them
    static const char* const kPalette[] = {"#4472C4", "#ED7D31", "#A5A5A5", "#FFC000", "#5B9BD5", "#70AD47"};
    for (auto* kind = plot->FirstChildElement(); kind; kind = kind->NextSiblingElement()) {
        const std::string tag = LocalName(kind);
        if (tag.size() < 6 || tag.compare(tag.size() - 5, 5, "Chart") != 0) continue;
        const bool scatterKind = tag == "scatterChart", bubbleKind = tag == "bubbleChart";
        if (series.empty()) {
            lines = tag == "lineChart" || tag == "areaChart" || scatterKind;
            scatter = scatterKind;
            radar = tag == "radarChart";
            bubble = bubbleKind;
            stock = tag == "stockChart";
            if (auto* style = Child(kind, "radarStyle")) {
                radarFilled = Attr(style, "val") == "filled";
                radarMarkers = Attr(style, "val") == "marker";
            }
            area = tag == "areaChart" || tag == "area3DChart";
            doughnut = tag == "doughnutChart";
            if (auto* marker = Child(kind, "marker")) lineMarkers = Attr(marker, "val") != "0";
            pie = tag == "pieChart" || tag == "pie3DChart" || tag == "doughnutChart" || tag == "ofPieChart";
            if (auto* direction = Child(kind, "barDir")) horizontal = Attr(direction, "val") == "bar";
            if (auto* grouping = Child(kind, "grouping")) {
                const std::string how = Attr(grouping, "val");
                percent = how == "percentStacked";
                stacked = (!lines || area) && (how == "stacked" || percent);
            }
        }
        for (auto* ser = Child(kind, "ser"); ser; ser = Next(ser, "ser")) {
            Series one;
            auto* title = Child(ser, "tx");       // (a series need not have a name)
            if (auto* name = title ? Find(title, "v") : nullptr) one.name = name->GetText() ? name->GetText() : "";
            if (auto* fill = StatedFill(Child(ser, "spPr"))) one.color = colourOf(fill);
            one.stated = !one.color.empty();
            if (one.color.empty()) one.color = kPalette[series.size() % 6];
            if (pie && series.empty()) {
                for (auto* point = Child(ser, "dPt"); point; point = Next(point, "dPt")) {
                    auto* index = Child(point, "idx");
                    auto* look = Child(point, "spPr");
                    auto* fill = look ? Child(look, "solidFill") : nullptr;
                    const size_t at = static_cast<size_t>(std::max(0, index ? index->IntAttribute("val", 0) : 0));
                    if (!fill || at >= 4096) continue;
                    if (pointColors.size() <= at) pointColors.resize(at + 1);
                    pointColors[at] = colourOf(fill);
                }
            }
            std::vector<std::string> points;
            auto* values = Child(ser, "val");
            if (!values) values = Child(ser, "yVal");
            CollectPoints(values, points);
            for (const std::string& point : points) {
                double value = 0.0;
                if (!point.empty() && !ReadNumber(point, value)) value = 0.0;
                one.values.push_back(value);
            }
            if (auto* curve = Child(ser, "smooth")) one.smooth = Attr(curve, "val") != "0";
            if (scatterKind || bubbleKind) {
                // Each point's own x (its number in the row where the file
                // gives none or words), and how the points show.
                std::vector<std::string> xs;
                CollectPoints(Child(ser, "xVal"), xs);
                for (size_t i = 0; i < one.values.size(); i++) {
                    double x = static_cast<double>(i + 1);
                    if (i < xs.size() && !xs[i].empty() && !ReadNumber(xs[i], x)) x = static_cast<double>(i + 1);
                    one.across.push_back(x);
                }
                auto* look = Child(ser, "spPr");
                auto* line = look ? Child(look, "ln") : nullptr;
                one.joined = !(line && Child(line, "noFill"));
                if (auto* style = Child(kind, "scatterStyle")) {
                    if (Attr(style, "val") == "marker") one.joined = false;
                }
                if (auto* marker = Child(ser, "marker")) {
                    auto* symbol = Child(marker, "symbol");
                    if (symbol && Attr(symbol, "val") == "none") one.marked = false;
                    auto* markerLook = Child(marker, "spPr");
                    auto* fill = markerLook ? Child(markerLook, "solidFill") : nullptr;
                    if (fill && !(look && (Child(look, "solidFill") || (line && Child(line, "solidFill"))))) {
                        one.color = colourOf(fill);
                        one.stated = true;
                    }
                }
                if (bubbleKind) {
                    // Bubbles: each point's size, no line.
                    std::vector<std::string> sizes;
                    CollectPoints(Child(ser, "bubbleSize"), sizes);
                    for (size_t i = 0; i < one.values.size(); i++) {
                        double size = 1.0;
                        if (i < sizes.size() && !sizes[i].empty() && !ReadNumber(sizes[i], size)) size = 1.0;
                        one.sizes.push_back(size);
                    }
                    one.joined = false;
                    one.marked = true;
                    if (look && Child(look, "solidFill")) one.color = colourOf(Child(look, "solidFill"));
                }
                if (!one.joined && !one.marked) one.marked = true;      // something to see
            }
            if (categories.empty()) CollectPoints(Child(ser, "cat"), categories);
            if (!scatterKind && !bubbleKind) {
                if (auto* marker = Child(ser, "marker")) {
                    auto* symbol = Child(marker, "symbol");
                    if (symbol && Attr(symbol, "val") == "none") one.marked = false;
                }
            }
            if (!one.values.empty()) series.push_back(std::move(one));
        }
    }
    if (series.empty()) return false;

    RichChart data;
    data.kind = radar ? RichChart::Kind::Radar : bubble ? RichChart::Kind::Bubble : stock ? RichChart::Kind::Stock
              : pie ? (doughnut ? RichChart::Kind::Doughnut : RichChart::Kind::Pie)
              : scatter ? RichChart::Kind::Scatter
              : area ? RichChart::Kind::Area
              : lines ? RichChart::Kind::Line
              : horizontal ? RichChart::Kind::Bar : RichChart::Kind::Column;
    data.stacked = stacked && !radar && !bubble && !stock;
    data.percent = percent && !radar && !bubble && !stock;
    data.filled = radarFilled;
    data.legend = Find(root, "legend") != nullptr;
    data.categories = categories;
    data.pointColors = pointColors;
    if (auto* title = Find(root, "title")) {
        std::vector<const tinyxml2::XMLElement*> open{title};
        while (!open.empty()) {              // its a:t texts, in document order
            auto* at = open.back();
            open.pop_back();
            if (Is(at, "t") && at->GetText()) data.title += at->GetText();
            std::vector<const tinyxml2::XMLElement*> children;
            for (auto* child = at->FirstChildElement(); child; child = child->NextSiblingElement()) children.push_back(child);
            open.insert(open.end(), children.rbegin(), children.rend());
        }
    }
    for (const Series& one : series) {
        RichChartSeries kept;
        kept.name = one.name;
        kept.color = one.stated ? one.color : std::string();
        kept.values = one.values;
        kept.xValues = one.across;
        kept.joined = scatter || bubble ? one.joined : true;
        kept.marked = scatter || bubble ? one.marked : radar ? (radarMarkers && one.marked)
                    : (lines && !area && lineMarkers && one.marked);
        kept.sizes = one.sizes;
        kept.smooth = one.smooth && (scatter || (lines && !area));
        data.series.push_back(std::move(kept));
    }
    // What the file states for its value axes: the one at the foot or top of
    // a scatter chart is its points' x, any other the values'.
    for (auto* ax = Child(plot, "valAx"); ax; ax = Next(ax, "valAx")) {
        auto* side = Child(ax, "axPos");
        const std::string pos = side ? Attr(side, "val") : "";
        const bool along = (scatter || bubble) && (pos == "b" || pos == "t");
        auto* scaling = Child(ax, "scaling");
        if (!scaling) continue;
        if (auto* least = Child(scaling, "min")) (along ? data.xMin : data.valueMin) = least->DoubleAttribute("val", 0.0);
        if (auto* most = Child(scaling, "max")) (along ? data.xMax : data.valueMax) = most->DoubleAttribute("val", 0.0);
    }
    out = std::move(data);
    return true;
}

} // namespace UltraCanvas

// Plugins/Charts/UltraCanvasCircularInfoGraphic.cpp
// Multi-ring circular infographic element.
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasCircularInfoGraphic.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

namespace {

    // ---- small text/parsing helpers -------------------------------------

    std::string TrimCopy(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    std::string ToLowerCopy(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // Splits a UTF-8 string into whole code points so circular text never
    // slices a multi-byte character in half.
    std::vector<std::string> SplitUtf8(const std::string& s) {
        std::vector<std::string> out;
        size_t i = 0;
        while (i < s.size()) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            size_t len = 1;
            if      ((c & 0x80u) == 0x00u) len = 1;
            else if ((c & 0xE0u) == 0xC0u) len = 2;
            else if ((c & 0xF0u) == 0xE0u) len = 3;
            else if ((c & 0xF8u) == 0xF0u) len = 4;
            if (i + len > s.size()) len = 1;
            out.push_back(s.substr(i, len));
            i += len;
        }
        return out;
    }

    // Splits on `sep` but ignores separators nested inside (), [] or {}, so a
    // field such as rgb(255,0,0) survives a comma-separated split intact.
    std::vector<std::string> SplitRespectingGroups(const std::string& s, char sep) {
        std::vector<std::string> out;
        std::string current;
        int depth = 0;
        for (char c : s) {
            if (c == '(' || c == '[' || c == '{') ++depth;
            else if (c == ')' || c == ']' || c == '}') { if (depth > 0) --depth; }

            if (c == sep && depth == 0) {
                out.push_back(current);
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        out.push_back(current);
        return out;
    }

    int HexPair(const std::string& s, size_t pos) {
        auto digit = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hi = digit(s[pos]), lo = digit(s[pos + 1]);
        if (hi < 0 || lo < 0) return -1;
        return hi * 16 + lo;
    }

    uint8_t ClampChannel(double v) {
        return static_cast<uint8_t>(std::max(0.0, std::min(255.0, v)));
    }

    const std::map<std::string, Color>& NamedColors() {
        static const std::map<std::string, Color> table = {
            {"transparent", Color(0, 0, 0, 0)},
            {"black",   Color(0, 0, 0, 255)},        {"white",   Color(255, 255, 255, 255)},
            {"red",     Color(255, 0, 0, 255)},      {"green",   Color(0, 128, 0, 255)},
            {"lime",    Color(0, 255, 0, 255)},      {"blue",    Color(0, 0, 255, 255)},
            {"yellow",  Color(255, 255, 0, 255)},    {"orange",  Color(255, 165, 0, 255)},
            {"purple",  Color(128, 0, 128, 255)},    {"cyan",    Color(0, 255, 255, 255)},
            {"magenta", Color(255, 0, 255, 255)},    {"gray",    Color(128, 128, 128, 255)},
            {"grey",    Color(128, 128, 128, 255)},  {"silver",  Color(192, 192, 192, 255)},
            {"maroon",  Color(128, 0, 0, 255)},      {"olive",   Color(128, 128, 0, 255)},
            {"navy",    Color(0, 0, 128, 255)},      {"teal",    Color(0, 128, 128, 255)},
            {"pink",    Color(255, 192, 203, 255)},  {"brown",   Color(165, 42, 42, 255)},
            {"gold",    Color(255, 215, 0, 255)},    {"crimson", Color(220, 20, 60, 255)}
        };
        return table;
    }

    // Radial room reserved for (and used by) OutsideRing connection routing.
    constexpr float kOutsideConnectionMargin = 34.0f;

    void BuildPath(IRenderContext* ctx, const std::vector<Point2Dd>& pts) {
        if (!ctx || pts.size() < 3) return;
        ctx->ClearPath();
        ctx->MoveTo(pts[0].x, pts[0].y);
        for (size_t i = 1; i < pts.size(); ++i) ctx->LineTo(pts[i].x, pts[i].y);
        ctx->ClosePath();
    }

} // namespace

// =============================================================================
// COLOR SCALE
// =============================================================================

    Color ColorScale::GetColorForValue(double value) const {
        if (colors.empty()) return Color(128, 128, 128, 255);
        if (colors.size() == 1) return colors.front();
        if (maxValue <= minValue) return colors.front();

        double t = (value - minValue) / (maxValue - minValue);
        t = std::max(0.0, std::min(1.0, t));

        if (!interpolate) {
            size_t idx = static_cast<size_t>(t * static_cast<double>(colors.size() - 1) + 0.5);
            return colors[std::min(idx, colors.size() - 1)];
        }

        double pos = t * static_cast<double>(colors.size() - 1);
        size_t i = static_cast<size_t>(std::floor(pos));
        i = std::min(i, colors.size() - 2);
        return colors[i].Blend(colors[i + 1], static_cast<float>(pos - static_cast<double>(i)));
    }

// =============================================================================
// CONSTRUCTION
// =============================================================================

    UltraCanvasCircularInfoGraphic::UltraCanvasCircularInfoGraphic(
        const std::string& id, int x, int y, int w, int h)
        : UltraCanvasChartElementBase(id, x, y, w, h)
    {
        showGrid = false;
        showAxes = false;
        showBackground = false;

        colorScale.colors = {
            Color( 66, 133, 244, 255),
            Color( 15, 157,  88, 255),
            Color(244, 180,   0, 255),
            Color(255, 112,  67, 255),
            Color(219,  68,  55, 255)
        };
        colorScale.minValue = 0.0;
        colorScale.maxValue = 100.0;

        defaultConnectionStyle.drawStyle = ConnectionDrawStyle::CurvedLine;
        defaultConnectionStyle.color = Color(120, 120, 130, 180);
        defaultConnectionStyle.baseThickness = 1.5f;
        defaultConnectionStyle.proportionalThickness = 6.0f;
    }

// =============================================================================
// RING MANAGEMENT
// =============================================================================

    size_t UltraCanvasCircularInfoGraphic::AddRing(const CircularRing& ring) {
        rings.push_back(ring);
        InvalidateCache();
        RequestRedraw();
        return rings.size() - 1;
    }

    bool UltraCanvasCircularInfoGraphic::SetRing(size_t index, const CircularRing& ring) {
        if (index >= rings.size()) return false;
        rings[index] = ring;
        InvalidateCache();
        RequestRedraw();
        return true;
    }

    const CircularRing* UltraCanvasCircularInfoGraphic::GetRing(size_t index) const {
        return index < rings.size() ? &rings[index] : nullptr;
    }

    void UltraCanvasCircularInfoGraphic::ClearRings() {
        rings.clear();
        connections.clear();
        hoveredRing = SIZE_MAX;
        hoveredCell = SIZE_MAX;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasCircularInfoGraphic::SetNumberOfLevels(int levels) {
        size_t target = static_cast<size_t>(std::max(0, levels));
        if (target == rings.size()) return;

        if (target < rings.size()) {
            rings.resize(target);
            // Drop connections that pointed at rings which no longer exist.
            connections.erase(
                std::remove_if(connections.begin(), connections.end(),
                    [target](const CircularConnection& c) {
                        return c.fromRing >= target || c.toRing >= target;
                    }),
                connections.end());
        } else {
            rings.resize(target);
        }

        hoveredRing = SIZE_MAX;
        hoveredCell = SIZE_MAX;
        InvalidateCache();
        RequestRedraw();
    }

    bool UltraCanvasCircularInfoGraphic::SetRingTextStyle(size_t index, CircularTextStyle style) {
        if (index >= rings.size()) return false;
        rings[index].textStyle = style;
        RequestRedraw();
        return true;
    }

    bool UltraCanvasCircularInfoGraphic::SetRingValueType(size_t index, ValueVisualizationType type) {
        if (index >= rings.size()) return false;
        rings[index].valueType = type;
        RequestRedraw();
        return true;
    }

// =============================================================================
// CELL ACCESS
// =============================================================================

    size_t UltraCanvasCircularInfoGraphic::GetCellCount(size_t ringIndex) const {
        return ringIndex < rings.size() ? rings[ringIndex].cells.size() : 0;
    }

    const CircularCell* UltraCanvasCircularInfoGraphic::GetCell(size_t ringIndex,
                                                                size_t cellIndex) const {
        if (ringIndex >= rings.size()) return nullptr;
        if (cellIndex >= rings[ringIndex].cells.size()) return nullptr;
        return &rings[ringIndex].cells[cellIndex];
    }

    bool UltraCanvasCircularInfoGraphic::UpdateCellValue(size_t ringIndex, size_t cellIndex,
                                                         double value) {
        if (ringIndex >= rings.size()) return false;
        if (cellIndex >= rings[ringIndex].cells.size()) return false;
        rings[ringIndex].cells[cellIndex].value = value;
        RequestRedraw();
        return true;
    }

    bool UltraCanvasCircularInfoGraphic::UpdateCellText(size_t ringIndex, size_t cellIndex,
                                                        const std::string& text) {
        if (ringIndex >= rings.size()) return false;
        if (cellIndex >= rings[ringIndex].cells.size()) return false;
        rings[ringIndex].cells[cellIndex].text = text;
        RequestRedraw();
        return true;
    }

    bool UltraCanvasCircularInfoGraphic::AddCell(size_t ringIndex, const CircularCell& cell) {
        if (ringIndex >= rings.size()) return false;
        rings[ringIndex].cells.push_back(cell);
        RequestRedraw();
        return true;
    }

// =============================================================================
// GEOMETRY CONFIGURATION
// =============================================================================

    void UltraCanvasCircularInfoGraphic::SetRadiusRange(float inner, float outer) {
        explicitInnerRadius = std::max(0.0f, inner);
        explicitOuterRadius = std::max(explicitInnerRadius + 4.0f, outer);
        autoFitRadius = false;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasCircularInfoGraphic::SetAutoFitRadius(bool on) {
        autoFitRadius = on;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasCircularInfoGraphic::SetInnerRadiusFraction(float fraction) {
        innerRadiusFraction = std::max(0.0f, std::min(0.9f, fraction));
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasCircularInfoGraphic::SetAngleOffset(float degrees) {
        angleOffsetDeg = degrees;
        RequestRedraw();
    }

    void UltraCanvasCircularInfoGraphic::SetTotalAngle(float degrees) {
        totalAngleDeg = std::max(10.0f, std::min(360.0f, degrees));
        RequestRedraw();
    }

    void UltraCanvasCircularInfoGraphic::SetRingSpacing(float pixels) {
        ringSpacing = std::max(0.0f, pixels);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasCircularInfoGraphic::SetCellSpacingAngle(float degrees) {
        cellSpacingDeg = std::max(0.0f, std::min(15.0f, degrees));
        RequestRedraw();
    }

// =============================================================================
// APPEARANCE
// =============================================================================

    void UltraCanvasCircularInfoGraphic::SetCenterGraphic(const std::string& imagePath) {
        centerGraphic = imagePath;
        RequestRedraw();
    }

    void UltraCanvasCircularInfoGraphic::SetBackgroundGraphic(const std::string& imagePath) {
        backgroundGraphic = imagePath;
        RequestRedraw();
    }

    void UltraCanvasCircularInfoGraphic::SetShowBackground(bool show) {
        drawOwnBackground = show;
        RequestRedraw();
    }

    void UltraCanvasCircularInfoGraphic::SetShowCenterCircle(bool show) {
        showCenterCircle = show;
        RequestRedraw();
    }

    void UltraCanvasCircularInfoGraphic::SetCenterColor(const Color& fill, const Color& border) {
        centerFillColor = fill;
        centerBorderColor = border;
        RequestRedraw();
    }

    void UltraCanvasCircularInfoGraphic::SetCenterText(const std::string& text) {
        centerText = text;
        RequestRedraw();
    }

    void UltraCanvasCircularInfoGraphic::SetColorScale(const ColorScale& scale) {
        colorScale = scale;
        RequestRedraw();
    }

    void UltraCanvasCircularInfoGraphic::SetLabelFont(const std::string& family, float size,
                                                      FontWeight weight) {
        labelFontFamily = family;
        labelFontSize = std::max(4.0f, size);
        labelFontWeight = weight;
        RequestRedraw();
    }

    void UltraCanvasCircularInfoGraphic::SetLabelColor(const Color& c) {
        labelColor = c;
        RequestRedraw();
    }

    void UltraCanvasCircularInfoGraphic::SetGroupOutlineColor(const Color& c) {
        groupOutlineColor = c;
        RequestRedraw();
    }

    void UltraCanvasCircularInfoGraphic::SetShowGroupOutlines(bool show) {
        showGroupOutlines = show;
        RequestRedraw();
    }

// =============================================================================
// CONNECTIONS
// =============================================================================

    void UltraCanvasCircularInfoGraphic::SetConnectionLineType(ConnectionLineType type) {
        connectionType = type;
        RequestRedraw();
    }

    size_t UltraCanvasCircularInfoGraphic::AddConnection(size_t fromRing, size_t fromCell,
                                                          size_t toRing, size_t toCell,
                                                          double value) {
        CircularConnection conn;
        conn.fromRing = fromRing;
        conn.fromCell = fromCell;
        conn.toRing   = toRing;
        conn.toCell   = toCell;
        conn.value    = value;
        conn.style    = defaultConnectionStyle;
        conn.style.flowValue = value;
        return AddConnection(conn);
    }

    size_t UltraCanvasCircularInfoGraphic::AddConnection(const CircularConnection& connection) {
        if (connection.fromRing >= rings.size() || connection.toRing >= rings.size()) {
            return SIZE_MAX;
        }
        if (connection.fromCell >= rings[connection.fromRing].cells.size() ||
            connection.toCell   >= rings[connection.toRing].cells.size()) {
            return SIZE_MAX;
        }
        connections.push_back(connection);
        RequestRedraw();
        return connections.size() - 1;
    }

    bool UltraCanvasCircularInfoGraphic::SetConnectionStyle(size_t index,
                                                            const ConnectionStyle& style) {
        if (index >= connections.size()) return false;
        connections[index].style = style;
        RequestRedraw();
        return true;
    }

    void UltraCanvasCircularInfoGraphic::SetDefaultConnectionStyle(const ConnectionStyle& style) {
        defaultConnectionStyle = style;
    }

    void UltraCanvasCircularInfoGraphic::ClearConnections() {
        connections.clear();
        RequestRedraw();
    }

// =============================================================================
// COLOR / CSV PARSING
// =============================================================================

    Color UltraCanvasCircularInfoGraphic::ParseColorToken(const std::string& rawToken,
                                                           const Color& fallback) {
        std::string token = TrimCopy(rawToken);
        if (token.empty()) return fallback;

        // #RGB / #RRGGBB / #RRGGBBAA
        if (token[0] == '#') {
            std::string hex = token.substr(1);
            if (hex.size() == 3) {
                std::string expanded;
                for (char c : hex) { expanded.push_back(c); expanded.push_back(c); }
                hex = expanded;
            }
            if (hex.size() == 6 || hex.size() == 8) {
                int r = HexPair(hex, 0), g = HexPair(hex, 2), b = HexPair(hex, 4);
                int a = (hex.size() == 8) ? HexPair(hex, 6) : 255;
                if (r >= 0 && g >= 0 && b >= 0 && a >= 0) {
                    return Color(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                                 static_cast<uint8_t>(b), static_cast<uint8_t>(a));
                }
            }
            return fallback;
        }

        // rgb(r,g,b) / rgba(r,g,b,a)
        std::string lower = ToLowerCopy(token);
        if (lower.rfind("rgb", 0) == 0) {
            size_t open = token.find('(');
            size_t close = token.rfind(')');
            if (open != std::string::npos && close != std::string::npos && close > open) {
                std::string inner = token.substr(open + 1, close - open - 1);
                auto parts = SplitRespectingGroups(inner, ',');
                if (parts.size() >= 3) {
                    double r = std::atof(TrimCopy(parts[0]).c_str());
                    double g = std::atof(TrimCopy(parts[1]).c_str());
                    double b = std::atof(TrimCopy(parts[2]).c_str());
                    // The alpha component of rgba() is 0..1 by CSS convention.
                    double a = 255.0;
                    if (parts.size() >= 4) {
                        double raw = std::atof(TrimCopy(parts[3]).c_str());
                        a = (raw <= 1.0) ? raw * 255.0 : raw;
                    }
                    return Color(ClampChannel(r), ClampChannel(g),
                                 ClampChannel(b), ClampChannel(a));
                }
            }
            return fallback;
        }

        const auto& names = NamedColors();
        auto it = names.find(lower);
        if (it != names.end()) return it->second;

        return fallback;
    }

    bool UltraCanvasCircularInfoGraphic::ImportDataFromCSV(const std::string& csvData) {
        std::string data = TrimCopy(csvData);
        if (data.empty()) return false;

        auto segments = SplitRespectingGroups(data, ';');
        if (segments.empty()) return false;

        CircularRing ring;
        ring.label = TrimCopy(segments[0]);
        ring.textStyle = CircularTextStyle::Horizontal;

        for (size_t s = 1; s < segments.size(); ++s) {
            std::string segment = TrimCopy(segments[s]);
            if (segment.empty()) continue;

            auto fields = SplitRespectingGroups(segment, ',');
            CircularCell cell;
            cell.text = TrimCopy(fields[0]);

            if (fields.size() > 1) {
                cell.textColor = ParseColorToken(fields[1], Color(30, 30, 30, 255));
            }
            if (fields.size() > 2) {
                cell.backgroundColor = ParseColorToken(fields[2], Color(255, 255, 255, 255));
            }
            if (fields.size() > 3) {
                std::string v = TrimCopy(fields[3]);
                // A blank or unparsable value leaves the cell at 0 rather than
                // failing the whole import.
                cell.value = v.empty() ? 0.0 : std::atof(v.c_str());
            }
            if (fields.size() > 4) {
                cell.backgroundImage = TrimCopy(fields[4]);
            }
            ring.cells.push_back(cell);
        }

        if (ring.cells.empty()) return false;

        AddRing(ring);
        return true;
    }

// =============================================================================
// INTERACTION CONFIG
// =============================================================================

    void UltraCanvasCircularInfoGraphic::SetHoverHighlightEnabled(bool on) {
        hoverHighlightEnabled = on;
        if (!on && hoveredRing != SIZE_MAX) {
            hoveredRing = SIZE_MAX;
            hoveredCell = SIZE_MAX;
            RequestRedraw();
        }
    }

// =============================================================================
// LAYOUT
// =============================================================================

    ChartPlotArea UltraCanvasCircularInfoGraphic::CalculatePlotArea() {
        // Reserve room for star-style labels parked outside the outermost ring.
        bool hasStarLabels = std::any_of(rings.begin(), rings.end(),
            [](const CircularRing& r) {
                return r.textStyle == CircularTextStyle::StarStyle;
            });
        float labelRoom = 0.0f;
        if (hasStarLabels) {
            // Text cannot be measured here (no render context), so the widest
            // label is estimated from its character count.
            size_t longest = 0;
            for (const auto& r : rings) {
                if (r.textStyle != CircularTextStyle::StarStyle) continue;
                for (const auto& c : r.cells) longest = std::max(longest, c.text.size());
            }
            labelRoom = 18.0f + static_cast<float>(longest) * labelFontSize * 0.62f + 6.0f;
        }

        // Connections routed outside the rings bow past the outer radius and
        // would otherwise be clipped at the element edge.
        float connectionRoom = (connectionType == ConnectionLineType::OutsideRing &&
                                !connections.empty())
                             ? kOutsideConnectionMargin : 0.0f;
        labelRoom = std::max(labelRoom, connectionRoom);

        float marginSide   = 8.0f + labelRoom;
        float marginTop    = (chartTitle.empty() ? 8.0f : 32.0f) + labelRoom * 0.4f;
        float marginBottom = 8.0f + labelRoom * 0.4f;

        ChartPlotArea area;
        area.x = marginSide;
        area.y = marginTop;
        area.width  = std::max(40.0, static_cast<double>(GetWidth())  - 2.0 * marginSide);
        area.height = std::max(40.0, static_cast<double>(GetHeight()) - marginTop - marginBottom);
        return area;
    }

    void UltraCanvasCircularInfoGraphic::ComputeGeometry() {
        cachedCenter = Point2Dd(cachedPlotArea.x + cachedPlotArea.width / 2.0,
                                cachedPlotArea.y + cachedPlotArea.height / 2.0);

        float fitted = static_cast<float>(
            std::min(cachedPlotArea.width, cachedPlotArea.height) * 0.5 - 2.0);
        fitted = std::max(12.0f, fitted);

        if (autoFitRadius) {
            cachedOuterRadius = fitted;
            cachedInnerRadius = cachedOuterRadius * innerRadiusFraction;
        } else {
            // Clamp explicit radii so the chart cannot overflow the element and
            // be clipped; the requested proportions are preserved.
            float scale = (explicitOuterRadius > fitted && explicitOuterRadius > 0.0f)
                        ? fitted / explicitOuterRadius : 1.0f;
            cachedOuterRadius = std::max(12.0f, explicitOuterRadius * scale);
            cachedInnerRadius = std::max(0.0f, explicitInnerRadius * scale);
        }
        cachedInnerRadius = std::min(cachedInnerRadius, cachedOuterRadius - 4.0f);
        cachedInnerRadius = std::max(0.0f, cachedInnerRadius);

        size_t count = rings.size();
        if (count == 0) return;

        float usable = cachedOuterRadius - cachedInnerRadius
                     - ringSpacing * static_cast<float>(count - 1);
        float band = std::max(2.0f, usable / static_cast<float>(count));

        for (size_t i = 0; i < count; ++i) {
            rings[i].innerRadius = cachedInnerRadius
                                 + static_cast<float>(i) * (band + ringSpacing);
            rings[i].outerRadius = rings[i].innerRadius + band;
        }
    }

    double UltraCanvasCircularInfoGraphic::StartAngleRad() const {
        return angleOffsetDeg * M_PI / 180.0;
    }

    double UltraCanvasCircularInfoGraphic::TotalAngleRad() const {
        return totalAngleDeg * M_PI / 180.0;
    }

    bool UltraCanvasCircularInfoGraphic::CellAngles(size_t ringIndex, size_t cellIndex,
                                                     double& startAngle, double& endAngle) const {
        if (ringIndex >= rings.size()) return false;
        const CircularRing& ring = rings[ringIndex];
        size_t n = ring.cells.size();
        if (n == 0 || cellIndex >= n) return false;

        double per = TotalAngleRad() / static_cast<double>(n);
        startAngle = StartAngleRad() + static_cast<double>(cellIndex) * per;
        endAngle   = startAngle + per;
        return true;
    }

    Point2Dd UltraCanvasCircularInfoGraphic::CellCenterPoint(size_t ringIndex,
                                                              size_t cellIndex) const {
        double a0 = 0.0, a1 = 0.0;
        if (!CellAngles(ringIndex, cellIndex, a0, a1)) return cachedCenter;
        const CircularRing& ring = rings[ringIndex];
        double mid = (a0 + a1) * 0.5;
        double r = (ring.innerRadius + ring.outerRadius) * 0.5;
        return Point2Dd(cachedCenter.x + r * std::cos(mid),
                        cachedCenter.y + r * std::sin(mid));
    }

    void UltraCanvasCircularInfoGraphic::BuildSectorOutline(
        double innerR, double outerR, double a0, double a1,
        std::vector<Point2Dd>& out) const {

        out.clear();
        int steps = std::max(3, static_cast<int>(std::ceil(std::fabs(a1 - a0) / (M_PI / 90.0))));
        ConnectionGeometry::SampleArc(cachedCenter, outerR, a0, a1, steps, out);

        std::vector<Point2Dd> inner;
        ConnectionGeometry::SampleArc(cachedCenter, innerR, a0, a1, steps, inner);
        out.insert(out.end(), inner.rbegin(), inner.rend());
    }

    double UltraCanvasCircularInfoGraphic::NormalizedValue(double value) const {
        if (colorScale.maxValue <= colorScale.minValue) return 0.0;
        double t = (value - colorScale.minValue) /
                   (colorScale.maxValue - colorScale.minValue);
        return std::max(0.0, std::min(1.0, t));
    }

    Color UltraCanvasCircularInfoGraphic::ResolveCellColor(const CircularCell& cell) const {
        return cell.useColorScale ? colorScale.GetColorForValue(cell.value)
                                  : cell.backgroundColor;
    }

    std::string UltraCanvasCircularInfoGraphic::BuildTooltip(const CircularCell& cell,
                                                              size_t ringIndex,
                                                              size_t /*cellIndex*/) const {
        if (customTooltipGenerator) return customTooltipGenerator(cell);
        if (!cell.tooltip.empty()) return cell.tooltip;

        std::string text = cell.text.empty() ? std::string("(unnamed)") : cell.text;
        if (ringIndex < rings.size() && !rings[ringIndex].label.empty()) {
            text = rings[ringIndex].label + "\n" + text;
        }
        if (cell.value != 0.0) {
            char buf[48];
            if (std::floor(cell.value) == cell.value && std::fabs(cell.value) < 1e12) {
                std::snprintf(buf, sizeof(buf), "%.0f", cell.value);
            } else {
                std::snprintf(buf, sizeof(buf), "%g", cell.value);
            }
            text += "\nValue: " + std::string(buf);
        }
        if (!cell.groupName.empty()) {
            text += "\nGroup: " + cell.groupName;
        }
        return text;
    }

// =============================================================================
// RENDERING
// =============================================================================

    void UltraCanvasCircularInfoGraphic::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        if (!ctx) return;
        UpdateRenderingCache();

        if (rings.empty()) {
            DrawEmptyState(ctx);
            return;
        }
        RenderChart(ctx);
    }

    void UltraCanvasCircularInfoGraphic::RenderChart(IRenderContext* ctx) {
        if (!ctx) return;
        UpdateRenderingCache();
        ComputeGeometry();

        if (rings.empty()) {
            DrawEmptyState(ctx);
            return;
        }

        DrawBackground(ctx);
        RenderTitle(ctx);

        // Inner to outer: outer rings paint over the inner ones' borders, which
        // is what reads correctly when bands touch.
        for (size_t i = 0; i < rings.size(); ++i) {
            DrawRing(ctx, i);
        }

        DrawConnections(ctx);

        for (size_t i = 0; i < rings.size(); ++i) {
            if (rings[i].showConnectionIndicators) DrawConnectionIndicators(ctx, i);
        }

        DrawCenter(ctx);
        DrawStarLabels(ctx);
    }

    void UltraCanvasCircularInfoGraphic::DrawBackground(IRenderContext* ctx) {
        if (!backgroundGraphic.empty()) {
            Rect2Df local = GetLocalBounds();
            ctx->DrawImage(backgroundGraphic,
                           Rect2Dd(local.x, local.y, local.width, local.height),
                           ImageFitMode::Cover);
            return;
        }
        if (drawOwnBackground && backgroundColor.a > 0) {
            ctx->SetFillPaint(backgroundColor);
            ctx->FillRectangle(GetLocalBounds());
        }
    }

    void UltraCanvasCircularInfoGraphic::RenderTitle(IRenderContext* ctx) {
        if (chartTitle.empty()) return;
        ctx->PushState();
        ctx->SetFontFamily(labelFontFamily);
        ctx->SetFontSize(labelFontSize + 4.0f);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(labelColor);
        Size2Di ts = ctx->GetTextLineDimensions(chartTitle);
        ctx->DrawText(chartTitle,
                      Point2Dd(cachedPlotArea.x + (cachedPlotArea.width - ts.width) / 2.0,
                               std::max(2.0, cachedPlotArea.y - ts.height - 6.0)));
        ctx->PopState();
    }

    void UltraCanvasCircularInfoGraphic::DrawRing(IRenderContext* ctx, size_t ringIndex) {
        const CircularRing& ring = rings[ringIndex];
        if (ring.cells.empty()) {
            DrawDecorativeRing(ctx, ring);
            return;
        }

        double pad = cellSpacingDeg * M_PI / 180.0 * 0.5;

        for (size_t i = 0; i < ring.cells.size(); ++i) {
            const CircularCell& cell = ring.cells[i];
            if (cell.isEmpty) continue;

            double a0 = 0.0, a1 = 0.0;
            if (!CellAngles(ringIndex, i, a0, a1)) continue;

            // Never let padding swallow a narrow cell whole.
            double span = a1 - a0;
            double p = (span <= 2.0 * pad) ? span * 0.25 : pad;
            a0 += p;
            a1 -= p;
            if (a1 <= a0) continue;

            DrawCellBackground(ctx, ring, cell, a0, a1);

            switch (ring.valueType) {
                case ValueVisualizationType::Bars:
                    DrawCellBar(ctx, ring, cell, a0, a1); break;
                case ValueVisualizationType::CircularBar:
                    DrawCellCircularBar(ctx, ring, cell, a0, a1); break;
                case ValueVisualizationType::CircularLine:
                case ValueVisualizationType::None:
                default:
                    break;
            }

            if (ring.textStyle != CircularTextStyle::StarStyle) {
                DrawCellText(ctx, ring, cell, a0, a1);
            }
        }

        if (ring.valueType == ValueVisualizationType::CircularLine) {
            DrawRingValueLine(ctx, ring);
        }

        if (showGroupOutlines) DrawGroupOutlines(ctx, ring);
        DrawDecorativeRing(ctx, ring);
    }

    void UltraCanvasCircularInfoGraphic::DrawCellBackground(IRenderContext* ctx,
                                                             const CircularRing& ring,
                                                             const CircularCell& cell,
                                                             double a0, double a1) {
        std::vector<Point2Dd> outline;
        BuildSectorOutline(ring.innerRadius, ring.outerRadius, a0, a1, outline);
        if (outline.size() < 3) return;

        bool hovered = hoverHighlightEnabled &&
                       hoveredRing < rings.size() &&
                       &rings[hoveredRing] == &ring &&
                       hoveredCell < ring.cells.size() &&
                       &ring.cells[hoveredCell] == &cell;

        if (!cell.backgroundImage.empty()) {
            // Clip to the sector so the artwork cannot bleed into neighbours.
            ctx->PushState();
            BuildPath(ctx, outline);
            ctx->ClipPath();
            Rect2Dd extents = ctx->GetPathExtents();
            ctx->ClearPath();
            ImageFitMode fit = cell.autoScale
                             ? (cell.maintainAspectRatio ? ImageFitMode::Cover : ImageFitMode::Fill)
                             : ImageFitMode::NoScale;
            ctx->DrawImage(cell.backgroundImage, extents, fit);
            ctx->PopState();
        } else if (!cell.isTransparent) {
            Color fill = ResolveCellColor(cell);
            if (hovered) fill = fill.Lighten(0.18f);
            ctx->SetFillPaint(fill);
            ctx->FillLinePath(outline);
        }

        if (ring.borderWidth > 0.0f && ring.borderColor.a > 0) {
            ctx->SetStrokePaint(hovered ? ring.borderColor.Darken(0.3f) : ring.borderColor);
            ctx->SetStrokeWidth(hovered ? ring.borderWidth + 1.0f : ring.borderWidth);
            ctx->DrawLinePath(outline, true);
        }
    }

    void UltraCanvasCircularInfoGraphic::DrawCellBar(IRenderContext* ctx,
                                                      const CircularRing& ring,
                                                      const CircularCell& cell,
                                                      double a0, double a1) {
        double t = NormalizedValue(cell.value);
        if (t <= 0.0) return;

        // The bar grows outward from the band's inner edge, inset slightly so
        // the cell's own border stays readable.
        double inset = std::min(2.0, (ring.outerRadius - ring.innerRadius) * 0.12);
        double rIn  = ring.innerRadius + inset;
        double rMax = ring.outerRadius - inset;
        if (rMax <= rIn) return;
        double rOut = rIn + (rMax - rIn) * t;

        double shrink = (a1 - a0) * 0.18;
        double b0 = a0 + shrink, b1 = a1 - shrink;
        if (b1 <= b0) { b0 = a0; b1 = a1; }

        std::vector<Point2Dd> outline;
        BuildSectorOutline(rIn, rOut, b0, b1, outline);
        if (outline.size() < 3) return;

        Color c = cell.useColorScale ? ring.valueColor
                                     : colorScale.GetColorForValue(cell.value);
        ctx->SetFillPaint(c);
        ctx->FillLinePath(outline);
    }

    void UltraCanvasCircularInfoGraphic::DrawCellCircularBar(IRenderContext* ctx,
                                                              const CircularRing& ring,
                                                              const CircularCell& cell,
                                                              double a0, double a1) {
        double t = NormalizedValue(cell.value);
        if (t <= 0.0) return;

        double rMid = (ring.innerRadius + ring.outerRadius) * 0.5;
        double thickness = std::max(2.0, (ring.outerRadius - ring.innerRadius) * 0.28);
        double sweep = (a1 - a0) * t;
        if (sweep <= 1e-6) return;

        // Track.
        std::vector<Point2Dd> track;
        BuildSectorOutline(rMid - thickness * 0.5, rMid + thickness * 0.5, a0, a1, track);
        ctx->SetFillPaint(Color(0, 0, 0, 28));
        ctx->FillLinePath(track);

        // Progress.
        std::vector<Point2Dd> bar;
        BuildSectorOutline(rMid - thickness * 0.5, rMid + thickness * 0.5,
                           a0, a0 + sweep, bar);
        ctx->SetFillPaint(colorScale.GetColorForValue(cell.value));
        ctx->FillLinePath(bar);
    }

    void UltraCanvasCircularInfoGraphic::DrawRingValueLine(IRenderContext* ctx,
                                                            const CircularRing& ring) {
        size_t n = ring.cells.size();
        if (n < 2) return;

        double per = TotalAngleRad() / static_cast<double>(n);
        double start = StartAngleRad();
        double rIn = ring.innerRadius, rOut = ring.outerRadius;

        std::vector<Point2Dd> pts;
        pts.reserve(n + 1);
        for (size_t i = 0; i < n; ++i) {
            const CircularCell& cell = ring.cells[i];
            if (cell.isEmpty) continue;
            double mid = start + (static_cast<double>(i) + 0.5) * per;
            double r = rIn + (rOut - rIn) * NormalizedValue(cell.value);
            pts.emplace_back(cachedCenter.x + r * std::cos(mid),
                             cachedCenter.y + r * std::sin(mid));
        }
        if (pts.size() < 2) return;

        // Close the loop only on a full circle; a partial arc stays open.
        bool closed = totalAngleDeg >= 359.9f && pts.size() > 2;

        ctx->SetStrokePaint(ring.valueColor);
        ctx->SetStrokeWidth(2.0f);
        ctx->DrawLinePath(pts, closed);

        ctx->SetFillPaint(ring.valueColor);
        for (const auto& p : pts) ctx->FillCircle(p, 2.5);
    }

    void UltraCanvasCircularInfoGraphic::DrawCellText(IRenderContext* ctx,
                                                       const CircularRing& ring,
                                                       const CircularCell& cell,
                                                       double a0, double a1) {
        if (cell.text.empty()) return;

        ctx->PushState();
        ctx->SetFontFamily(labelFontFamily);
        ctx->SetFontSize(labelFontSize);
        ctx->SetFontWeight(labelFontWeight);
        ctx->SetTextPaint(cell.textColor.a > 0 ? cell.textColor : labelColor);

        double mid = (a0 + a1) * 0.5;
        double rMid = (ring.innerRadius + ring.outerRadius) * 0.5;
        double band = ring.outerRadius - ring.innerRadius;
        Size2Di ts = ctx->GetTextLineDimensions(cell.text);
        double arcLen = (a1 - a0) * rMid;
        double extra = cell.rotation * M_PI / 180.0;

        switch (ring.textStyle) {
            case CircularTextStyle::Horizontal: {
                // Skip rather than overflow into the neighbouring cell.
                if (ts.width > arcLen + 2.0 || ts.height > band + 2.0) break;
                ctx->DrawText(cell.text,
                              Point2Dd(cachedCenter.x + rMid * std::cos(mid) - ts.width / 2.0,
                                       cachedCenter.y + rMid * std::sin(mid) - ts.height / 2.0));
                break;
            }

            case CircularTextStyle::Radial: {
                if (ts.width > band - 2.0 && ts.height > arcLen) break;
                double norm = std::fmod(mid + 4.0 * M_PI, 2.0 * M_PI);
                bool leftSide = norm > M_PI / 2.0 && norm < 3.0 * M_PI / 2.0;
                ctx->PushState();
                ctx->Translate(cachedCenter.x + rMid * std::cos(mid),
                               cachedCenter.y + rMid * std::sin(mid));
                ctx->Rotate((cell.autoRotate ? (leftSide ? mid + M_PI : mid) : 0.0) + extra);
                ctx->DrawText(cell.text, Point2Dd(-ts.width / 2.0, -ts.height / 2.0));
                ctx->PopState();
                break;
            }

            case CircularTextStyle::Circular: {
                if (ts.width > arcLen + 4.0) break;
                DrawCircularText(ctx, cell.text, rMid, mid + extra);
                break;
            }

            case CircularTextStyle::StarStyle:
            default:
                break;  // Handled by DrawStarLabels once all rings are painted.
        }

        ctx->PopState();
    }

    void UltraCanvasCircularInfoGraphic::DrawCircularText(IRenderContext* ctx,
                                                           const std::string& text,
                                                           double radius, double midAngle) {
        if (radius < 1.0) return;
        auto glyphs = SplitUtf8(text);
        if (glyphs.empty()) return;

        std::vector<double> widths;
        widths.reserve(glyphs.size());
        double totalWidth = 0.0;
        for (const auto& g : glyphs) {
            double w = static_cast<double>(ctx->GetTextLineWidth(g));
            widths.push_back(w);
            totalWidth += w;
        }
        if (totalWidth <= 0.0) return;

        double height = static_cast<double>(ctx->GetTextLineHeight(text));
        double totalAngle = totalWidth / radius;

        // Below the centre the tangent flips, so walk the arc backwards and
        // rotate the other way to keep the text right side up.
        double norm = std::fmod(midAngle + 4.0 * M_PI, 2.0 * M_PI);
        bool bottomHalf = norm > 0.0 && norm < M_PI;

        if (!bottomHalf) {
            double cursor = midAngle - totalAngle * 0.5;
            for (size_t i = 0; i < glyphs.size(); ++i) {
                double ga = cursor + (widths[i] * 0.5) / radius;
                ctx->PushState();
                ctx->Translate(cachedCenter.x + radius * std::cos(ga),
                               cachedCenter.y + radius * std::sin(ga));
                ctx->Rotate(ga + M_PI / 2.0);
                ctx->DrawText(glyphs[i], Point2Dd(-widths[i] / 2.0, -height / 2.0));
                ctx->PopState();
                cursor += widths[i] / radius;
            }
        } else {
            double cursor = midAngle + totalAngle * 0.5;
            for (size_t i = 0; i < glyphs.size(); ++i) {
                double ga = cursor - (widths[i] * 0.5) / radius;
                ctx->PushState();
                ctx->Translate(cachedCenter.x + radius * std::cos(ga),
                               cachedCenter.y + radius * std::sin(ga));
                ctx->Rotate(ga - M_PI / 2.0);
                ctx->DrawText(glyphs[i], Point2Dd(-widths[i] / 2.0, -height / 2.0));
                ctx->PopState();
                cursor -= widths[i] / radius;
            }
        }
    }

    void UltraCanvasCircularInfoGraphic::DrawStarLabels(IRenderContext* ctx) {
        bool any = std::any_of(rings.begin(), rings.end(), [](const CircularRing& r) {
            return r.textStyle == CircularTextStyle::StarStyle;
        });
        if (!any) return;

        ctx->PushState();
        ctx->SetFontFamily(labelFontFamily);
        ctx->SetFontSize(labelFontSize);
        ctx->SetFontWeight(labelFontWeight);

        double labelRadius = cachedOuterRadius + 14.0;

        for (size_t ri = 0; ri < rings.size(); ++ri) {
            const CircularRing& ring = rings[ri];
            if (ring.textStyle != CircularTextStyle::StarStyle) continue;

            for (size_t ci = 0; ci < ring.cells.size(); ++ci) {
                const CircularCell& cell = ring.cells[ci];
                if (cell.isEmpty || cell.text.empty()) continue;

                double a0 = 0.0, a1 = 0.0;
                if (!CellAngles(ri, ci, a0, a1)) continue;
                double mid = (a0 + a1) * 0.5;

                double norm = std::fmod(mid + 4.0 * M_PI, 2.0 * M_PI);
                bool leftSide = norm > M_PI / 2.0 && norm < 3.0 * M_PI / 2.0;

                Point2Dd from(cachedCenter.x + ring.outerRadius * std::cos(mid),
                              cachedCenter.y + ring.outerRadius * std::sin(mid));
                Point2Dd to(cachedCenter.x + labelRadius * std::cos(mid),
                            cachedCenter.y + labelRadius * std::sin(mid));

                ctx->SetStrokePaint(ring.borderColor.a > 0 ? ring.borderColor
                                                           : Color(170, 170, 170, 255));
                ctx->SetStrokeWidth(1.0f);
                ctx->DrawLine(from, to);

                Size2Di ts = ctx->GetTextLineDimensions(cell.text);
                Point2Dd textPos(to.x + (leftSide ? -(ts.width + 4.0) : 4.0),
                                 to.y - ts.height / 2.0);
                ctx->SetTextPaint(cell.textColor.a > 0 ? cell.textColor : labelColor);
                ctx->DrawText(cell.text, textPos);
            }
        }
        ctx->PopState();
    }

    void UltraCanvasCircularInfoGraphic::DrawDecorativeRing(IRenderContext* ctx,
                                                             const CircularRing& ring) {
        double rMid = (ring.innerRadius + ring.outerRadius) * 0.5;
        double band = ring.outerRadius - ring.innerRadius;
        double start = StartAngleRad();
        double total = TotalAngleRad();

        switch (ring.decorativeType) {
            case DecorativeRingType::ColorHighlight: {
                ctx->SetStrokePaint(ring.decorativeColor);
                ctx->SetStrokeWidth(std::max(1.5, band * 0.12));
                std::vector<Point2Dd> arc;
                int steps = std::max(24, static_cast<int>(std::ceil(total / (M_PI / 90.0))));
                ConnectionGeometry::SampleArc(cachedCenter, rMid, start, start + total,
                                              steps, arc);
                ctx->DrawLinePath(arc, totalAngleDeg >= 359.9f);
                break;
            }

            case DecorativeRingType::Pattern: {
                int ticks = std::max(8, static_cast<int>(total / (M_PI / 30.0)));
                ctx->SetStrokePaint(ring.decorativeColor);
                ctx->SetStrokeWidth(1.0f);
                for (int i = 0; i <= ticks; ++i) {
                    double a = start + total * (static_cast<double>(i) / ticks);
                    double r0 = rMid - band * 0.18, r1 = rMid + band * 0.18;
                    ctx->DrawLine(Point2Dd(cachedCenter.x + r0 * std::cos(a),
                                           cachedCenter.y + r0 * std::sin(a)),
                                  Point2Dd(cachedCenter.x + r1 * std::cos(a),
                                           cachedCenter.y + r1 * std::sin(a)));
                }
                break;
            }

            case DecorativeRingType::Gradient: {
                // IRenderContext gradients are paint patterns rather than path
                // fills, so the ramp is built from thin concentric bands.
                const int bands = 12;
                for (int i = 0; i < bands; ++i) {
                    double t0 = static_cast<double>(i) / bands;
                    double t1 = static_cast<double>(i + 1) / bands;
                    double r0 = ring.innerRadius + band * t0;
                    double r1 = ring.innerRadius + band * t1;
                    Color c = ring.decorativeColor.WithAlpha(static_cast<uint8_t>(
                        ring.decorativeColor.a * (1.0 - t0)));
                    std::vector<Point2Dd> seg;
                    BuildSectorOutline(r0, r1, start, start + total, seg);
                    ctx->SetFillPaint(c);
                    ctx->FillLinePath(seg);
                }
                break;
            }

            case DecorativeRingType::Graphics: {
                if (ring.decorativeGraphic.empty()) break;
                int marks = std::max(4, static_cast<int>(total / (M_PI / 12.0)));
                double size = std::max(6.0, band * 0.5);
                for (int i = 0; i < marks; ++i) {
                    double a = start + total * (static_cast<double>(i) / marks);
                    Point2Dd p(cachedCenter.x + rMid * std::cos(a),
                               cachedCenter.y + rMid * std::sin(a));
                    ctx->DrawImage(ring.decorativeGraphic,
                                   Rect2Dd(p.x - size / 2.0, p.y - size / 2.0, size, size),
                                   ImageFitMode::Contain);
                }
                break;
            }

            case DecorativeRingType::None:
            default:
                break;
        }
    }

    void UltraCanvasCircularInfoGraphic::DrawGroupOutlines(IRenderContext* ctx,
                                                            const CircularRing& ring) {
        size_t n = ring.cells.size();
        if (n == 0) return;

        size_t i = 0;
        while (i < n) {
            const std::string& group = ring.cells[i].groupName;
            if (group.empty()) { ++i; continue; }

            // Consecutive cells carrying the same group name form one band.
            size_t j = i;
            while (j + 1 < n && ring.cells[j + 1].groupName == group) ++j;

            double per = TotalAngleRad() / static_cast<double>(n);
            double a0 = StartAngleRad() + static_cast<double>(i) * per;
            double a1 = StartAngleRad() + static_cast<double>(j + 1) * per;

            std::vector<Point2Dd> outline;
            BuildSectorOutline(ring.innerRadius - 1.0, ring.outerRadius + 1.0, a0, a1, outline);
            ctx->SetStrokePaint(groupOutlineColor);
            ctx->SetStrokeWidth(2.0f);
            ctx->DrawLinePath(outline, true);

            i = j + 1;
        }
    }

    void UltraCanvasCircularInfoGraphic::DrawConnections(IRenderContext* ctx) {
        if (connectionType == ConnectionLineType::None || connections.empty()) return;

        double maxValue = 0.0;
        for (const auto& c : connections) maxValue = std::max(maxValue, c.value);

        ConnectionRenderer renderer(ctx, cachedCenter);

        for (const auto& conn : connections) {
            if (conn.fromRing >= rings.size() || conn.toRing >= rings.size()) continue;
            if (conn.fromCell >= rings[conn.fromRing].cells.size()) continue;
            if (conn.toCell   >= rings[conn.toRing].cells.size()) continue;
            if (!conn.style.isVisible) continue;

            ConnectionStyle style = conn.style;
            style.flowValue = conn.value;

            Point2Dd from = CellCenterPoint(conn.fromRing, conn.fromCell);
            Point2Dd to   = CellCenterPoint(conn.toRing, conn.toCell);

            switch (connectionType) {
                case ConnectionLineType::InsideCenter:
                    // Controls land on the centre, so the pair reads as a
                    // hub-and-spoke link through the middle of the chart.
                    renderer.SetCenter(cachedCenter);
                    style.curvature = 1.0f;
                    break;

                case ConnectionLineType::DirectCurve:
                    renderer.SetCenter(cachedCenter);
                    style.curvature = 0.35f;
                    break;

                case ConnectionLineType::OutsideRing: {
                    double a0 = 0.0, a1 = 0.0, b0 = 0.0, b1 = 0.0;
                    if (!CellAngles(conn.fromRing, conn.fromCell, a0, a1)) continue;
                    if (!CellAngles(conn.toRing, conn.toCell, b0, b1)) continue;
                    double fromAngle = (a0 + a1) * 0.5;
                    double toAngle   = (b0 + b1) * 0.5;
                    double edge = cachedOuterRadius + 4.0;
                    from = Point2Dd(cachedCenter.x + edge * std::cos(fromAngle),
                                    cachedCenter.y + edge * std::sin(fromAngle));
                    to   = Point2Dd(cachedCenter.x + edge * std::cos(toAngle),
                                    cachedCenter.y + edge * std::sin(toAngle));
                    // Bow the curve toward an anchor outside the chart so it
                    // arcs around the rings instead of cutting across them.
                    // The anchor is kept inside the margin CalculatePlotArea
                    // reserved, so the bulge cannot be clipped.
                    double bisector = (fromAngle + toAngle) * 0.5;
                    double anchorR = cachedOuterRadius + kOutsideConnectionMargin * 1.6;
                    renderer.SetCenter(Point2Dd(cachedCenter.x + anchorR * std::cos(bisector),
                                                cachedCenter.y + anchorR * std::sin(bisector)));
                    style.curvature = 0.55f;
                    break;
                }

                case ConnectionLineType::None:
                default:
                    continue;
            }

            renderer.Draw(from, to, style, maxValue);
        }
    }

    void UltraCanvasCircularInfoGraphic::DrawConnectionIndicators(IRenderContext* ctx,
                                                                   size_t ringIndex) {
        const CircularRing& ring = rings[ringIndex];
        ctx->SetFillPaint(ring.connectionIndicatorColor);

        for (const auto& conn : connections) {
            for (int end = 0; end < 2; ++end) {
                size_t r = (end == 0) ? conn.fromRing : conn.toRing;
                size_t c = (end == 0) ? conn.fromCell : conn.toCell;
                if (r != ringIndex) continue;
                if (c >= ring.cells.size() || ring.cells[c].isEmpty) continue;
                ctx->FillCircle(CellCenterPoint(r, c), 3.0);
            }
        }
    }

    void UltraCanvasCircularInfoGraphic::DrawCenter(IRenderContext* ctx) {
        if (cachedInnerRadius < 4.0f) return;

        if (showCenterCircle) {
            ctx->DrawFilledCircle(cachedCenter, cachedInnerRadius,
                                  centerFillColor, centerBorderColor, 1.5f);
        }

        if (!centerGraphic.empty()) {
            // Inscribe the artwork in the hole so a square image cannot poke
            // out past the innermost ring.
            double side = cachedInnerRadius * std::sqrt(2.0);
            ctx->DrawImage(centerGraphic,
                           Rect2Dd(cachedCenter.x - side / 2.0, cachedCenter.y - side / 2.0,
                                   side, side),
                           ImageFitMode::Contain);
        }

        if (!centerText.empty()) {
            ctx->PushState();
            ctx->SetFontFamily(labelFontFamily);
            ctx->SetFontSize(labelFontSize + 2.0f);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(labelColor);
            Size2Di ts = ctx->GetTextLineDimensions(centerText);
            if (ts.width <= cachedInnerRadius * 1.8) {
                ctx->DrawText(centerText,
                              Point2Dd(cachedCenter.x - ts.width / 2.0,
                                       cachedCenter.y - ts.height / 2.0));
            }
            ctx->PopState();
        }
    }

// =============================================================================
// HIT TESTING & EVENTS
// =============================================================================

    bool UltraCanvasCircularInfoGraphic::HitTestCell(const Point2Dd& localPos,
                                                      size_t& ringIndex,
                                                      size_t& cellIndex) const {
        ringIndex = SIZE_MAX;
        cellIndex = SIZE_MAX;
        if (rings.empty()) return false;

        double dx = localPos.x - cachedCenter.x;
        double dy = localPos.y - cachedCenter.y;
        double r = std::sqrt(dx * dx + dy * dy);
        if (r < cachedInnerRadius || r > cachedOuterRadius) return false;

        double start = StartAngleRad();
        double total = TotalAngleRad();

        // Rotate the probe into the chart's own turn, then reject anything
        // beyond the swept arc so the empty wedge of a partial circle is inert.
        double angle = std::atan2(dy, dx);
        while (angle < start) angle += 2.0 * M_PI;
        while (angle >= start + 2.0 * M_PI) angle -= 2.0 * M_PI;
        double offset = angle - start;
        if (offset > total) return false;

        for (size_t ri = 0; ri < rings.size(); ++ri) {
            const CircularRing& ring = rings[ri];
            if (ring.cells.empty()) continue;
            // Reject the spacing gap between adjacent bands.
            if (r < ring.innerRadius || r > ring.outerRadius) continue;

            double per = total / static_cast<double>(ring.cells.size());
            if (per <= 0.0) continue;
            size_t ci = static_cast<size_t>(offset / per);
            if (ci >= ring.cells.size()) ci = ring.cells.size() - 1;
            if (ring.cells[ci].isEmpty) return false;

            ringIndex = ri;
            cellIndex = ci;
            return true;
        }
        return false;
    }

    bool UltraCanvasCircularInfoGraphic::HandleChartMouseMove(const Point2Di& mousePos) {
        if (rings.empty()) return false;

        Point2Dd local(static_cast<double>(mousePos.x), static_cast<double>(mousePos.y));
        size_t ri = SIZE_MAX, ci = SIZE_MAX;
        bool hit = HitTestCell(local, ri, ci);

        if (ri != hoveredRing || ci != hoveredCell) {
            hoveredRing = ri;
            hoveredCell = ci;
            if (hit && onCellHover) onCellHover(ri, ci);
            RequestRedraw();
        }

        if (hit) {
            if (enableTooltips) {
                const CircularCell& cell = rings[ri].cells[ci];
                std::string text = BuildTooltip(cell, ri, ci);
                if (!text.empty()) {
                    auto windowMousePos = MapFromLocal(mousePos, nullptr);
                    UltraCanvasTooltipManager::UpdateAndShowTooltip(
                        this->window, text, windowMousePos);
                    isTooltipActive = true;
                }
            }
            return true;
        }

        if (isTooltipActive) HideTooltip();
        return false;
    }

    bool UltraCanvasCircularInfoGraphic::HandleClick(const UCEvent& event) {
        Point2Dd local(static_cast<double>(event.pointer.x),
                       static_cast<double>(event.pointer.y));
        size_t ri = SIZE_MAX, ci = SIZE_MAX;
        if (!HitTestCell(local, ri, ci)) return false;
        if (onCellClick) onCellClick(ri, ci);
        return true;
    }

    bool UltraCanvasCircularInfoGraphic::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left && HandleClick(event)) {
                    return true;
                }
                break;
            case UCEventType::MouseLeave:
                if (hoveredRing != SIZE_MAX) {
                    hoveredRing = SIZE_MAX;
                    hoveredCell = SIZE_MAX;
                    RequestRedraw();
                }
                HideTooltip();
                return true;
            default:
                break;
        }
        return UltraCanvasChartElementBase::OnEvent(event);
    }

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    std::shared_ptr<UltraCanvasCircularInfoGraphic> CreateCircularInfoGraphic(
        const std::string& id, int x, int y, int w, int h) {
        return std::make_shared<UltraCanvasCircularInfoGraphic>(id, x, y, w, h);
    }

    std::shared_ptr<UltraCanvasCircularInfoGraphic> CreateCircularInfoGraphicFromGroups(
        const std::string& id, int x, int y, int w, int h,
        const std::vector<std::pair<std::string,
              std::vector<std::pair<std::string, double>>>>& groupedData) {

        auto chart = std::make_shared<UltraCanvasCircularInfoGraphic>(id, x, y, w, h);
        for (const auto& group : groupedData) {
            CircularRing ring;
            ring.label = group.first;
            ring.valueType = ValueVisualizationType::Bars;
            for (const auto& entry : group.second) {
                CircularCell cell;
                cell.text = entry.first;
                cell.value = entry.second;
                cell.useColorScale = true;
                ring.cells.push_back(cell);
            }
            chart->AddRing(ring);
        }
        return chart;
    }

} // namespace UltraCanvas

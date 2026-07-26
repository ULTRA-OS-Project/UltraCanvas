// Plugins/Charts/UltraCanvasBubbleChart.cpp
// Implementation of the comprehensive bubble chart element (scatter bubbles,
// packed bubbles, bubble matrix).
// Version: 1.0.0
// Last Modified: 2026-07-26
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasBubbleChart.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <iomanip>

namespace UltraCanvas {

namespace {

// Default categorical palette (colour-blind friendly, alpha applied later).
const std::vector<Color> kDefaultBubblePalette = {
    Color( 78, 121, 167, 255),   // blue
    Color(242, 142,  43, 255),   // orange
    Color(225,  87,  89, 255),   // red
    Color(118, 183, 178, 255),   // teal
    Color( 89, 161,  79, 255),   // green
    Color(237, 201,  72, 255),   // yellow
    Color(176, 122, 161, 255),   // purple
    Color(255, 157, 167, 255),   // pink
    Color(156, 117,  95, 255),   // brown
    Color(186, 176, 172, 255)    // gray
};

inline double EaseOutCubic(double t) {
    double u = 1.0 - t;
    return 1.0 - u * u * u;
}

inline Color Lighten(const Color& c, float factor) {
    return c.Blend(Color(255, 255, 255, c.a), factor);
}

inline Color Darken(const Color& c, float factor) {
    return c.Blend(Color(0, 0, 0, c.a), factor);
}

} // anonymous namespace

// =============================================================================
// CONSTRUCTION / CONFIGURATION
// =============================================================================

UltraCanvasBubbleChartElement::UltraCanvasBubbleChartElement(
        const std::string& id, int x, int y, int width, int height)
    : UltraCanvasChartElementBase(id, x, y, width, height) {
    palette = kDefaultBubblePalette;
    showValueLabels = false;   // this element has its own label system
}

void UltraCanvasBubbleChartElement::SetMode(BubbleChartMode m) {
    mode = m;
    // Packed and matrix charts are axis-free; scatter keeps the classic frame.
    bool scatter = (mode == BubbleChartMode::ScatterBubbles);
    showGrid = scatter;
    showAxes = scatter;
    InvalidateCache();
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::AddBubble(double x, double y, double size,
                                              double colorValue,
                                              const std::string& name,
                                              const std::string& category) {
    AddBubble(BubbleDataPoint(x, y, size, colorValue, name, category));
}

void UltraCanvasBubbleChartElement::AddBubble(const BubbleDataPoint& bubble) {
    bubbles.push_back(bubble);
    SyncDataSource();
}

void UltraCanvasBubbleChartElement::SetMatrixLayout(
        const std::vector<std::string>& rowNames,
        const std::vector<std::string>& columnNames) {
    matrixRows = rowNames;
    matrixColumns = columnNames;
    InvalidateCache();
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::AddMatrixBubble(const std::string& rowName,
                                                    const std::string& columnName,
                                                    double value,
                                                    const Color& overrideColor) {
    auto rowIt = std::find(matrixRows.begin(), matrixRows.end(), rowName);
    auto colIt = std::find(matrixColumns.begin(), matrixColumns.end(), columnName);
    if (rowIt == matrixRows.end() || colIt == matrixColumns.end()) return;

    BubbleDataPoint b;
    b.size = value;
    b.name = rowName;
    b.category = columnName;
    b.overrideColor = overrideColor;
    b.row = static_cast<int>(rowIt - matrixRows.begin());
    b.col = static_cast<int>(colIt - matrixColumns.begin());
    bubbles.push_back(b);
    SyncDataSource();
}

void UltraCanvasBubbleChartElement::ClearBubbles() {
    bubbles.clear();
    layoutCircles.clear();
    hoveredBubble = SIZE_MAX;
    SyncDataSource();
}

void UltraCanvasBubbleChartElement::SyncDataSource() {
    // Mirror the bubbles into the base-class data source so the shared
    // machinery (empty state, bounds, axis labels) keeps working.
    std::vector<ChartDataPoint> mirror;
    mirror.reserve(bubbles.size());
    for (const auto& b : bubbles) {
        mirror.emplace_back(b.x, b.y, b.size, b.name, b.colorValue, b.overrideColor);
    }
    auto data = std::make_shared<ChartDataVector>();
    data->LoadFromArray(mirror);
    dataSource = data;
    InvalidateCache();
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetRadiusRange(float minRadius, float maxRadius) {
    minBubbleRadius = std::max(0.5f, minRadius);
    maxBubbleRadius = std::max(minBubbleRadius, maxRadius);
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetSizeScale(BubbleSizeScale scale) {
    sizeScale = scale;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetSizeDomain(double minValue, double maxValue) {
    autoSizeDomain = false;
    sizeDomainMin = minValue;
    sizeDomainMax = std::max(minValue, maxValue);
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetAutoSizeDomain() {
    autoSizeDomain = true;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetColorMode(BubbleColorMode m) {
    colorMode = m;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetUniformColor(const Color& color) {
    uniformColor = color;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetPalette(const std::vector<Color>& colors) {
    if (!colors.empty()) palette = colors;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetCategoryColorMap(const std::map<std::string, Color>& colorMap) {
    categoryColors = colorMap;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetColormap(HeatmapColormap map, bool reverse) {
    colormap = map;
    colormapReversed = reverse;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetCustomColormap(const std::vector<Color>& anchors) {
    colormap = HeatmapColormap::Custom;
    customColormapAnchors = anchors;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetColorDomain(double minValue, double maxValue) {
    autoColorDomain = false;
    colorDomainMin = minValue;
    colorDomainMax = std::max(minValue, maxValue);
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetAutoColorDomain() {
    autoColorDomain = true;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetBubbleAlpha(float alpha) {
    bubbleAlpha = std::clamp(alpha, 0.0f, 1.0f);
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetBubbleStyle(BubbleRenderStyle style) {
    bubbleStyle = style;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetBubbleBorder(const Color& color, float width) {
    bubbleBorderColor = color;
    bubbleBorderWidth = std::max(0.0f, width);
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetHighlightOnHover(bool enable) {
    highlightOnHover = enable;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetNameLabelMode(BubbleNameLabelMode m) {
    nameLabelMode = m;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetValueLabelMode(BubbleValueLabelMode m) {
    bubbleValueLabelMode = m;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetLabelFontSize(float size) {
    labelFontSize = std::max(6.0f, size);
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetNameLabelColor(const Color& color) {
    nameLabelColor = color;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetValueDecimals(int decimals) {
    valueDecimals = std::clamp(decimals, 0, 6);
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetValueSuffix(const std::string& suffix) {
    valueSuffix = suffix;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetMinRadiusForInsideLabels(float radius) {
    minInsideLabelRadius = std::max(0.0f, radius);
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetHideNamesBelowRadius(float radius) {
    hideNamesBelowRadius = std::max(0.0f, radius);
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetRowColorMap(const std::map<std::string, Color>& colorMap) {
    rowColors = colorMap;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetMatrixHeaderStyle(float fontSize, const Color& color, bool bold) {
    matrixHeaderFontSize = fontSize;
    matrixHeaderColor = color;
    matrixHeaderBold = bold;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetMatrixRowLabelStyle(float fontSize, const Color& color) {
    matrixRowLabelFontSize = fontSize;
    matrixRowLabelColor = color;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetPackedSortOrder(PackedSortOrder order) {
    packedSortOrder = order;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetPackedPadding(float pixels) {
    packedPadding = std::max(0.0f, pixels);
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetPackedEnclosure(bool show, const Color& strokeColor,
                                                       float strokeWidth, const Color& fillColor) {
    packedEnclosureShow = show;
    packedEnclosureStroke = strokeColor;
    packedEnclosureWidth = std::max(0.5f, strokeWidth);
    packedEnclosureFill = fillColor;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetGroupPadding(float pixels) {
    groupPadding = std::max(0.0f, pixels);
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetGroupTint(float lightenFactor) {
    groupTint = std::clamp(lightenFactor, 0.0f, 1.0f);
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetGroupLabelStyle(float fontSize, const Color& color, bool bold) {
    groupLabelFontSize = std::max(6.0f, fontSize);
    groupLabelColor = color;
    groupLabelBold = bold;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetTimelineRows(const std::vector<std::string>& rows) {
    timelineRows = rows;
    InvalidateCache();
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::AddTimelineBubble(const std::string& rowName, double xValue,
                                                      double size, double colorValue) {
    auto it = std::find(timelineRows.begin(), timelineRows.end(), rowName);
    if (it == timelineRows.end()) {
        timelineRows.push_back(rowName);
        it = std::prev(timelineRows.end());
    }
    BubbleDataPoint b;
    b.x = xValue;
    b.size = size;
    b.colorValue = colorValue;
    b.category = rowName;
    b.row = static_cast<int>(it - timelineRows.begin());
    bubbles.push_back(b);
    SyncDataSource();
}

void UltraCanvasBubbleChartElement::SetXAxisLabelFormatter(std::function<std::string(double)> formatter) {
    xLabelFormatter = std::move(formatter);
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetShowSizeLegend(bool show) {
    showSizeLegend = show;
    InvalidateCache();
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetSizeLegendTitle(const std::string& title) {
    sizeLegendTitle = title;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetSizeLegendValues(const std::vector<double>& values) {
    sizeLegendValues = values;
    RequestRedraw();
}

void UltraCanvasBubbleChartElement::SetFootnote(const std::string& text) {
    footnote = text;
    InvalidateCache();
    RequestRedraw();
}

// =============================================================================
// DOMAINS, SCALES AND COLOURS
// =============================================================================

void UltraCanvasBubbleChartElement::GetSizeDomain(double& outMin, double& outMax) const {
    if (!autoSizeDomain) {
        outMin = sizeDomainMin;
        outMax = sizeDomainMax;
        return;
    }
    if (bubbles.empty()) {
        outMin = 0.0; outMax = 1.0;
        return;
    }
    outMin = bubbles[0].size;
    outMax = bubbles[0].size;
    for (const auto& b : bubbles) {
        outMin = std::min(outMin, b.size);
        outMax = std::max(outMax, b.size);
    }
    // The matrix and timeline read best with a zero-anchored scale so bubble
    // areas compare directly ("79" is ~5x the area of "16").
    if (mode == BubbleChartMode::BubbleMatrix ||
        mode == BubbleChartMode::TimelineBubbles) {
        outMin = 0.0;
    }
    if (outMax <= outMin) outMax = outMin + 1.0;
}

void UltraCanvasBubbleChartElement::GetColorDomain(double& outMin, double& outMax) const {
    if (!autoColorDomain) {
        outMin = colorDomainMin;
        outMax = colorDomainMax;
        return;
    }
    if (bubbles.empty()) {
        outMin = 0.0; outMax = 1.0;
        return;
    }
    bool bySize = (colorMode == BubbleColorMode::ColormapBySize);
    outMin = bySize ? bubbles[0].size : bubbles[0].colorValue;
    outMax = outMin;
    for (const auto& b : bubbles) {
        double v = bySize ? b.size : b.colorValue;
        outMin = std::min(outMin, v);
        outMax = std::max(outMax, v);
    }
    if (outMax <= outMin) outMax = outMin + 1.0;
}

double UltraCanvasBubbleChartElement::SizeToRadius(double size, double domainMin,
                                                   double domainMax,
                                                   double minR, double maxR) const {
    double range = domainMax - domainMin;
    double t = range > 0.0 ? (size - domainMin) / range : 0.5;
    t = std::clamp(t, 0.0, 1.0);
    if (sizeScale == BubbleSizeScale::Area) {
        // Interpolate the AREA between the min and max circle, then back to a
        // radius, so the perceived (area) difference tracks the values.
        return std::sqrt(minR * minR + t * (maxR * maxR - minR * minR));
    }
    return minR + t * (maxR - minR);
}

Color UltraCanvasBubbleChartElement::ResolveBubbleColor(const BubbleDataPoint& b,
                                                        size_t index) const {
    Color c;
    if (b.overrideColor.a != 0) {
        c = b.overrideColor;
    } else if (mode == BubbleChartMode::BubbleMatrix && b.row >= 0 &&
               rowColors.count(matrixRows[b.row])) {
        c = rowColors.at(matrixRows[b.row]);
    } else if (mode == BubbleChartMode::HierarchicalPacked) {
        // Children take the saturated group colour; the parent circle gets
        // the tinted version.
        c = ResolveGroupBaseColor(b.category.empty() ? "Other" : b.category);
    } else {
        switch (colorMode) {
            case BubbleColorMode::Uniform:
                c = uniformColor;
                break;
            case BubbleColorMode::Palette:
                c = palette[index % palette.size()];
                break;
            case BubbleColorMode::CategoryMap: {
                auto it = categoryColors.find(b.category);
                if (it != categoryColors.end()) {
                    c = it->second;
                } else {
                    // Stable fallback: hash the category name into the palette.
                    size_t h = std::hash<std::string>{}(b.category);
                    c = palette[h % palette.size()];
                }
                break;
            }
            case BubbleColorMode::ColormapByValue:
            case BubbleColorMode::ColormapBySize: {
                double lo, hi;
                GetColorDomain(lo, hi);
                double v = (colorMode == BubbleColorMode::ColormapBySize) ? b.size
                                                                          : b.colorValue;
                double t = (hi > lo) ? (v - lo) / (hi - lo) : 0.5;
                c = SampleColormap(colormap, customColormapAnchors,
                                   std::clamp(t, 0.0, 1.0), colormapReversed);
                break;
            }
        }
    }
    if (bubbleAlpha < 1.0f) {
        c.a = static_cast<uint8_t>(c.a * bubbleAlpha);
    }
    return c;
}

Color UltraCanvasBubbleChartElement::PickContrastTextColor(const Color& background) const {
    double luminance = 0.299 * background.r + 0.587 * background.g + 0.114 * background.b;
    // Blend towards the background alpha: translucent bubbles sit on white.
    double alphaFactor = background.a / 255.0;
    luminance = luminance * alphaFactor + 255.0 * (1.0 - alphaFactor);
    return luminance > 150.0 ? Color(40, 40, 40, 255) : Color(255, 255, 255, 255);
}

std::string UltraCanvasBubbleChartElement::FormatValue(double value) const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(valueDecimals) << value;
    return ss.str() + valueSuffix;
}

// =============================================================================
// PLOT AREA AND DATA BOUNDS
// =============================================================================

float UltraCanvasBubbleChartElement::SizeLegendReservedWidth() const {
    if (!showSizeLegend) return 0.0f;
    return std::max(90.0f, maxBubbleRadius * 2.0f + 30.0f);
}

ChartPlotArea UltraCanvasBubbleChartElement::CalculatePlotArea() {
    int marginLeft, marginRight, marginTop, marginBottom;
    if (mode == BubbleChartMode::ScatterBubbles) {
        marginLeft = 60;
        marginRight = 20;
        marginTop = chartTitle.empty() ? 20 : 40;
        marginBottom = 50;
    } else {
        marginLeft = 12;
        marginRight = 12;
        marginTop = chartTitle.empty() ? 12 : 40;
        marginBottom = 12;
    }
    marginRight += static_cast<int>(SizeLegendReservedWidth());
    if (!footnote.empty()) marginBottom += 22;

    return ChartPlotArea(marginLeft, marginTop,
                         GetWidth() - marginLeft - marginRight,
                         GetHeight() - marginTop - marginBottom);
}

ChartDataBounds UltraCanvasBubbleChartElement::CalculateDataBounds() {
    if (mode != BubbleChartMode::ScatterBubbles) {
        // Packed and matrix layouts don't use data-space coordinates.
        return ChartDataBounds(0, 1, 0, 1);
    }
    ChartDataBounds bounds = UltraCanvasChartElementBase::CalculateDataBounds();
    if (!bounds.hasData && bubbles.empty()) return bounds;

    // Give the biggest bubbles room so they don't clip at the plot edges.
    double plotW = std::max(1.0, cachedPlotArea.width);
    double plotH = std::max(1.0, cachedPlotArea.height);
    double fx = (maxBubbleRadius * 1.3) / plotW;
    double fy = (maxBubbleRadius * 1.3) / plotH;
    double rangeX = bounds.GetXRange();
    double rangeY = bounds.GetYRange();
    if (rangeX <= 0) rangeX = std::abs(bounds.maxX) > 0 ? std::abs(bounds.maxX) : 1.0;
    if (rangeY <= 0) rangeY = std::abs(bounds.maxY) > 0 ? std::abs(bounds.maxY) : 1.0;
    bounds.minX -= rangeX * fx;
    bounds.maxX += rangeX * fx;
    bounds.minY -= rangeY * fy;
    bounds.maxY += rangeY * fy;
    bounds.hasData = true;
    return bounds;
}

void UltraCanvasBubbleChartElement::RenderCommonBackground(IRenderContext* ctx) {
    if (mode == BubbleChartMode::ScatterBubbles) {
        UltraCanvasChartElementBase::RenderCommonBackground(ctx);
        return;
    }
    // Axis-free modes: plain background plus an optional centred title.
    if (showBackground) {
        ctx->DrawFilledRectangle(GetLocalBounds(), backgroundColor);
    }
    if (!chartTitle.empty()) {
        ctx->SetTextPaint(Color(0, 0, 0, 255));
        ctx->SetFontSize(16.0f);
        int textW = ctx->GetTextLineWidth(chartTitle);
        ctx->DrawText(chartTitle, Point2Dd((GetWidth() - textW) / 2.0, 10.0));
    }
}

// =============================================================================
// LAYOUT BUILDERS
// =============================================================================

void UltraCanvasBubbleChartElement::BuildScatterLayout() {
    double dMin, dMax;
    GetSizeDomain(dMin, dMax);
    effectiveMinRadius = minBubbleRadius;
    effectiveMaxRadius = maxBubbleRadius;
    effectiveDomainMin = dMin;
    effectiveDomainMax = dMax;

    ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
    for (size_t i = 0; i < bubbles.size(); ++i) {
        const auto& b = bubbles[i];
        LayoutCircle c;
        Point2Dd pos = transform.DataToScreen(b.x, b.y);
        c.cx = pos.x;
        c.cy = pos.y;
        c.r = SizeToRadius(b.size, dMin, dMax, minBubbleRadius, maxBubbleRadius);
        c.index = i;
        layoutCircles.push_back(c);
    }
}

// Greedy tangent packing around the origin: each circle is placed at the
// candidate position (tangent to two already-placed circles) closest to the
// centre of mass; the first two circles seed the cluster. Items are placed in
// the order given; radii must be pre-filled, x/y are written.
void UltraCanvasBubbleChartElement::GreedyPack(std::vector<PackItem>& items, double pad) {
    auto overlapsAny = [&](double x, double y, double r, size_t upTo) {
        for (size_t i = 0; i < upTo; ++i) {
            double dx = x - items[i].x, dy = y - items[i].y;
            double minDist = r + items[i].r + pad - 0.01;
            if (dx * dx + dy * dy < minDist * minDist) return true;
        }
        return false;
    };

    for (size_t n = 0; n < items.size(); ++n) {
        double r = items[n].r;
        double bestX = 0.0, bestY = 0.0;
        bool found = false;

        if (n == 0) {
            found = true;
        } else if (n == 1) {
            bestX = items[0].x + items[0].r + r + pad;
            bestY = items[0].y;
            found = true;
        } else {
            double bestDist = 0.0;
            for (size_t a = 0; a < n; ++a) {
                for (size_t b = a + 1; b < n; ++b) {
                    // Intersect the two circles of possible tangent centres.
                    double ra = items[a].r + r + pad;
                    double rb = items[b].r + r + pad;
                    double dx = items[b].x - items[a].x;
                    double dy = items[b].y - items[a].y;
                    double d = std::sqrt(dx * dx + dy * dy);
                    if (d < 1e-9 || d > ra + rb || d < std::abs(ra - rb)) continue;
                    double aLen = (ra * ra - rb * rb + d * d) / (2.0 * d);
                    double h2 = ra * ra - aLen * aLen;
                    if (h2 < 0.0) continue;
                    double h = std::sqrt(h2);
                    double mx = items[a].x + aLen * dx / d;
                    double my = items[a].y + aLen * dy / d;
                    double candX[2] = { mx + h * dy / d, mx - h * dy / d };
                    double candY[2] = { my - h * dx / d, my + h * dx / d };
                    for (int k = 0; k < 2; ++k) {
                        if (overlapsAny(candX[k], candY[k], r, n)) continue;
                        double dist = candX[k] * candX[k] + candY[k] * candY[k];
                        if (!found || dist < bestDist) {
                            bestX = candX[k];
                            bestY = candY[k];
                            bestDist = dist;
                            found = true;
                        }
                    }
                }
            }
            if (!found) {
                // Spiral fallback (degenerate configurations only).
                for (double rad = r; !found; rad += r * 0.25) {
                    for (int step = 0; step < 64 && !found; ++step) {
                        double ang = step * (2.0 * 3.14159265358979 / 64.0);
                        double x = rad * std::cos(ang), y = rad * std::sin(ang);
                        if (!overlapsAny(x, y, r, n)) {
                            bestX = x; bestY = y;
                            found = true;
                        }
                    }
                }
            }
        }
        items[n].x = bestX;
        items[n].y = bestY;
    }
}

// Approximate minimal enclosing circle: start at the centroid and repeatedly
// nudge the centre towards the farthest circle, keeping the best radius seen.
void UltraCanvasBubbleChartElement::ComputeEnclosingCircle(
        const std::vector<PackItem>& items, double& outCx, double& outCy, double& outR) {
    if (items.empty()) {
        outCx = outCy = 0.0;
        outR = 1.0;
        return;
    }
    double cx = 0.0, cy = 0.0;
    for (const auto& it : items) { cx += it.x; cy += it.y; }
    cx /= items.size();
    cy /= items.size();

    double bestCx = cx, bestCy = cy, bestR = 0.0;
    for (int iter = 0; iter < 150; ++iter) {
        double farD = 0.0, fx = cx, fy = cy;
        for (const auto& it : items) {
            double d = std::hypot(it.x - cx, it.y - cy) + it.r;
            if (d > farD) { farD = d; fx = it.x; fy = it.y; }
        }
        if (iter == 0 || farD < bestR) {
            bestCx = cx; bestCy = cy; bestR = farD;
        }
        cx += (fx - cx) * 0.08;
        cy += (fy - cy) * 0.08;
    }
    outCx = bestCx;
    outCy = bestCy;
    outR = std::max(1.0, bestR);
}

Color UltraCanvasBubbleChartElement::ResolveGroupBaseColor(const std::string& group) const {
    auto it = categoryColors.find(group);
    if (it != categoryColors.end()) return it->second;
    size_t h = std::hash<std::string>{}(group);
    return palette[h % palette.size()];
}

void UltraCanvasBubbleChartElement::BuildPackedLayout() {
    double dMin, dMax;
    GetSizeDomain(dMin, dMax);

    // Placement order.
    std::vector<size_t> order(bubbles.size());
    std::iota(order.begin(), order.end(), size_t{0});
    if (packedSortOrder == PackedSortOrder::SizeDescending) {
        std::stable_sort(order.begin(), order.end(), [this](size_t a, size_t b) {
            return bubbles[a].size > bubbles[b].size;
        });
    } else if (packedSortOrder == PackedSortOrder::SizeAscending) {
        std::stable_sort(order.begin(), order.end(), [this](size_t a, size_t b) {
            return bubbles[a].size < bubbles[b].size;
        });
    }

    std::vector<PackItem> items;
    items.reserve(order.size());
    for (size_t idx : order) {
        PackItem it;
        it.r = SizeToRadius(bubbles[idx].size, dMin, dMax,
                            minBubbleRadius, maxBubbleRadius);
        it.index = idx;
        items.push_back(it);
    }
    GreedyPack(items, packedPadding);
    if (items.empty()) return;

    Point2Dd plotCenter = cachedPlotArea.GetCenter();
    double scale = 1.0;
    double cx0 = 0.0, cy0 = 0.0;
    enclosureR = 0.0;

    if (packedEnclosureShow) {
        // Scale so the cluster's enclosing circle fills the plot area.
        double ecx, ecy, er;
        ComputeEnclosingCircle(items, ecx, ecy, er);
        double target = std::min(cachedPlotArea.width, cachedPlotArea.height) / 2.0
                        - packedEnclosureWidth - 2.0;
        scale = std::max(0.01, target / er);
        cx0 = ecx;
        cy0 = ecy;
        enclosureCx = plotCenter.x;
        enclosureCy = plotCenter.y;
        enclosureR = er * scale + packedEnclosureWidth + 1.0;
    } else {
        // Fit the cluster's bounding box, preserving aspect ratio.
        double minX = items[0].x - items[0].r, maxX = items[0].x + items[0].r;
        double minY = items[0].y - items[0].r, maxY = items[0].y + items[0].r;
        for (const auto& p : items) {
            minX = std::min(minX, p.x - p.r);
            maxX = std::max(maxX, p.x + p.r);
            minY = std::min(minY, p.y - p.r);
            maxY = std::max(maxY, p.y + p.r);
        }
        double bw = std::max(1.0, maxX - minX);
        double bh = std::max(1.0, maxY - minY);
        scale = std::min(cachedPlotArea.width / bw, cachedPlotArea.height / bh);
        cx0 = (minX + maxX) / 2.0;
        cy0 = (minY + maxY) / 2.0;
    }

    effectiveMinRadius = minBubbleRadius * scale;
    effectiveMaxRadius = maxBubbleRadius * scale;
    effectiveDomainMin = dMin;
    effectiveDomainMax = dMax;

    for (const auto& p : items) {
        LayoutCircle c;
        c.cx = plotCenter.x + (p.x - cx0) * scale;
        c.cy = plotCenter.y + (p.y - cy0) * scale;
        c.r = p.r * scale;
        c.index = p.index;
        layoutCircles.push_back(c);
    }
}

void UltraCanvasBubbleChartElement::BuildHierarchicalLayout() {
    if (bubbles.empty()) return;

    // Group bubbles by category, keeping category insertion order.
    std::vector<std::string> groups;
    std::map<std::string, std::vector<size_t>> byGroup;
    for (size_t i = 0; i < bubbles.size(); ++i) {
        std::string g = bubbles[i].category.empty() ? "Other" : bubbles[i].category;
        if (!byGroup.count(g)) groups.push_back(g);
        byGroup[g].push_back(i);
    }

    double dMin, dMax;
    GetSizeDomain(dMin, dMax);

    // 1. Pack the children of each group around their own origin and wrap
    //    them in a parent circle with padding plus label headroom.
    struct GroupLayout {
        std::vector<PackItem> children;   // centred on (0, 0)
        double innerR = 0.0;              // children cluster radius
        double parentR = 0.0;
        std::string name;
    };
    std::vector<GroupLayout> layouts;
    double labelHeadroom = groupLabelFontSize * 1.6;

    for (const auto& g : groups) {
        GroupLayout gl;
        gl.name = g;
        auto indices = byGroup[g];
        std::stable_sort(indices.begin(), indices.end(), [this](size_t a, size_t b) {
            return bubbles[a].size > bubbles[b].size;
        });
        for (size_t idx : indices) {
            PackItem it;
            it.r = SizeToRadius(bubbles[idx].size, dMin, dMax,
                                minBubbleRadius, maxBubbleRadius);
            it.index = idx;
            gl.children.push_back(it);
        }
        GreedyPack(gl.children, packedPadding);
        double ecx, ecy, er;
        ComputeEnclosingCircle(gl.children, ecx, ecy, er);
        for (auto& c : gl.children) { c.x -= ecx; c.y -= ecy; }
        gl.innerR = er;
        gl.parentR = er + groupPadding + labelHeadroom;
        layouts.push_back(std::move(gl));
    }

    // 2. Pack the parent circles against each other, biggest first.
    std::vector<size_t> order(layouts.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [&layouts](size_t a, size_t b) {
        return layouts[a].parentR > layouts[b].parentR;
    });
    std::vector<PackItem> parents;
    for (size_t li : order) {
        PackItem it;
        it.r = layouts[li].parentR;
        it.index = li;
        parents.push_back(it);
    }
    GreedyPack(parents, packedPadding * 1.5);

    // 3. Fit everything into the plot area.
    double minX = parents[0].x - parents[0].r, maxX = parents[0].x + parents[0].r;
    double minY = parents[0].y - parents[0].r, maxY = parents[0].y + parents[0].r;
    for (const auto& p : parents) {
        minX = std::min(minX, p.x - p.r);
        maxX = std::max(maxX, p.x + p.r);
        minY = std::min(minY, p.y - p.r);
        maxY = std::max(maxY, p.y + p.r);
    }
    double bw = std::max(1.0, maxX - minX);
    double bh = std::max(1.0, maxY - minY);
    double scale = std::min(cachedPlotArea.width / bw, cachedPlotArea.height / bh);
    double cx0 = (minX + maxX) / 2.0;
    double cy0 = (minY + maxY) / 2.0;
    Point2Dd plotCenter = cachedPlotArea.GetCenter();

    effectiveMinRadius = minBubbleRadius * scale;
    effectiveMaxRadius = maxBubbleRadius * scale;
    effectiveDomainMin = dMin;
    effectiveDomainMax = dMax;

    for (const auto& p : parents) {
        const GroupLayout& gl = layouts[p.index];
        double pcx = plotCenter.x + (p.x - cx0) * scale;
        double pcy = plotCenter.y + (p.y - cy0) * scale;

        ParentCircle pc;
        pc.cx = pcx;
        pc.cy = pcy;
        pc.r = gl.parentR * scale;
        pc.innerR = gl.innerR * scale;
        pc.group = gl.name;
        pc.base = ResolveGroupBaseColor(gl.name);
        pc.fill = Lighten(pc.base, groupTint);
        parentCircles.push_back(pc);

        for (const auto& c : gl.children) {
            LayoutCircle lc;
            lc.cx = pcx + c.x * scale;
            lc.cy = pcy + c.y * scale;
            lc.r = c.r * scale;
            lc.index = c.index;
            layoutCircles.push_back(lc);
        }
    }
}

void UltraCanvasBubbleChartElement::BuildTimelineLayout(IRenderContext* ctx) {
    if (timelineRows.empty() || bubbles.empty()) return;

    ctx->SetFontSize(matrixRowLabelFontSize);
    int rowLabelW = 0;
    for (const auto& row : timelineRows) {
        rowLabelW = std::max(rowLabelW, ctx->GetTextLineWidth(row));
    }

    double x0 = cachedPlotArea.x + rowLabelW + 14.0;
    double y0 = cachedPlotArea.y + 2.0;
    double bottomY = cachedPlotArea.GetBottom() - (matrixRowLabelFontSize + 10.0);
    double width = cachedPlotArea.GetRight() - x0 - 6.0;
    double rowH = (bottomY - y0) / timelineRows.size();

    double xMin = bubbles[0].x, xMax = bubbles[0].x;
    for (const auto& b : bubbles) {
        xMin = std::min(xMin, b.x);
        xMax = std::max(xMax, b.x);
    }
    if (xMax <= xMin) xMax = xMin + 1.0;

    double dMin, dMax;
    GetSizeDomain(dMin, dMax);
    double effMaxR = std::min(static_cast<double>(maxBubbleRadius), rowH * 0.47);
    double effMinR = std::min(static_cast<double>(minBubbleRadius), effMaxR * 0.5);

    double sidePad = effMaxR + 2.0;
    double usable = std::max(1.0, width - 2.0 * sidePad);

    timelineMetrics = {x0, y0, rowH, width, bottomY, sidePad, usable, xMin, xMax};

    effectiveMinRadius = effMinR;
    effectiveMaxRadius = effMaxR;
    effectiveDomainMin = dMin;
    effectiveDomainMax = dMax;

    for (size_t i = 0; i < bubbles.size(); ++i) {
        const auto& b = bubbles[i];
        if (b.row < 0 || b.row >= static_cast<int>(timelineRows.size())) continue;
        double t = (b.x - xMin) / (xMax - xMin);
        LayoutCircle c;
        c.cx = x0 + sidePad + t * usable;
        c.cy = y0 + (b.row + 0.5) * rowH;
        c.r = SizeToRadius(b.size, dMin, dMax, effMinR, effMaxR);
        c.index = i;
        layoutCircles.push_back(c);
    }
}

float UltraCanvasBubbleChartElement::MatrixRowLabelWidth(IRenderContext* ctx) const {
    ctx->SetFontSize(matrixRowLabelFontSize);
    int maxW = 0;
    for (const auto& row : matrixRows) {
        maxW = std::max(maxW, ctx->GetTextLineWidth(row));
    }
    return static_cast<float>(maxW);
}

void UltraCanvasBubbleChartElement::BuildMatrixLayout(IRenderContext* ctx) {
    if (matrixRows.empty() || matrixColumns.empty()) return;

    double rowLabelW = MatrixRowLabelWidth(ctx) + 16.0;
    double headerH = matrixHeaderFontSize + 16.0;
    double x0 = cachedPlotArea.x + rowLabelW;
    double y0 = cachedPlotArea.y + headerH;
    double cellW = (cachedPlotArea.GetRight() - x0) / matrixColumns.size();
    double cellH = (cachedPlotArea.GetBottom() - y0) / matrixRows.size();
    matrixMetrics = {x0, y0, cellW, cellH, headerH, rowLabelW};

    double dMin, dMax;
    GetSizeDomain(dMin, dMax);
    double maxR = std::max(4.0, std::min(cellW, cellH) / 2.0 * 0.92);

    effectiveMinRadius = 0.0;
    effectiveMaxRadius = maxR;
    effectiveDomainMin = dMin;
    effectiveDomainMax = dMax;

    for (size_t i = 0; i < bubbles.size(); ++i) {
        const auto& b = bubbles[i];
        if (b.row < 0 || b.col < 0) continue;
        LayoutCircle c;
        c.cx = x0 + (b.col + 0.5) * cellW;
        c.cy = y0 + (b.row + 0.5) * cellH;
        c.r = SizeToRadius(b.size, dMin, dMax, 0.0, maxR);
        c.index = i;
        layoutCircles.push_back(c);
    }
}

// =============================================================================
// RENDERING
// =============================================================================

void UltraCanvasBubbleChartElement::RenderChart(IRenderContext* ctx) {
    layoutCircles.clear();
    parentCircles.clear();
    switch (mode) {
        case BubbleChartMode::ScatterBubbles:     BuildScatterLayout(); break;
        case BubbleChartMode::PackedBubbles:      BuildPackedLayout(); break;
        case BubbleChartMode::BubbleMatrix:       BuildMatrixLayout(ctx); break;
        case BubbleChartMode::HierarchicalPacked: BuildHierarchicalLayout(); break;
        case BubbleChartMode::TimelineBubbles:    BuildTimelineLayout(ctx); break;
    }

    if (mode == BubbleChartMode::BubbleMatrix) {
        RenderMatrixChrome(ctx);
    }
    if (mode == BubbleChartMode::TimelineBubbles) {
        RenderTimelineChrome(ctx);
    }
    if (mode == BubbleChartMode::PackedBubbles && packedEnclosureShow && enclosureR > 0.0) {
        RenderEnclosureCircle(ctx);
    }
    if (mode == BubbleChartMode::HierarchicalPacked) {
        RenderParentCircles(ctx);
    }

    RenderBubbles(ctx);
    RenderBubbleLabels(ctx);
    if (showSizeLegend) RenderSizeLegend(ctx);
    RenderFootnote(ctx);

    // Grow-in animation: keep repainting until the ease completes.
    if (animationEnabled && !animationComplete) {
        RequestRedraw();
    }
}

void UltraCanvasBubbleChartElement::RenderBubbles(IRenderContext* ctx) {
    double grow = animationEnabled ? EaseOutCubic(GetAnimationProgress()) : 1.0;
    for (const auto& c : layoutCircles) {
        LayoutCircle scaled = c;
        scaled.r = c.r * grow;
        if (scaled.r <= 0.1) continue;
        Color fill = ResolveBubbleColor(bubbles[c.index], c.index);
        bool hovered = highlightOnHover && (c.index == hoveredBubble);
        DrawBubble(ctx, scaled, fill, hovered);
    }
}

void UltraCanvasBubbleChartElement::DrawBubble(IRenderContext* ctx,
                                               const LayoutCircle& c,
                                               const Color& fillIn, bool hovered) {
    Color fill = hovered ? Lighten(fillIn, 0.18f) : fillIn;
    Point2Dd center(c.cx, c.cy);

    switch (bubbleStyle) {
        case BubbleRenderStyle::Flat:
            ctx->SetFillPaint(fill);
            ctx->FillCircle(center, c.r);
            break;
        case BubbleRenderStyle::Shaded:
        case BubbleRenderStyle::Glossy: {
            std::vector<GradientStop> stops = {
                GradientStop(0.0, Lighten(fill, 0.40f)),
                GradientStop(0.65, fill),
                GradientStop(1.0, Darken(fill, 0.22f))
            };
            auto grad = ctx->CreateRadialGradientPattern(
                c.cx - c.r * 0.35, c.cy - c.r * 0.35, c.r * 0.05,
                c.cx, c.cy, c.r, stops);
            ctx->SetFillPaint(grad);
            ctx->FillCircle(center, c.r);

            if (bubbleStyle == BubbleRenderStyle::Glossy) {
                std::vector<GradientStop> gloss = {
                    GradientStop(0.0, Color(255, 255, 255, 150)),
                    GradientStop(1.0, Color(255, 255, 255, 0))
                };
                double gx = c.cx - c.r * 0.30;
                double gy = c.cy - c.r * 0.42;
                auto hi = ctx->CreateRadialGradientPattern(
                    gx, gy, 0.0, gx, gy, c.r * 0.55, gloss);
                ctx->SetFillPaint(hi);
                ctx->FillCircle(Point2Dd(gx, gy), c.r * 0.55);
            }
            break;
        }
    }

    if (bubbleBorderWidth > 0.0f && bubbleBorderColor.a != 0) {
        ctx->SetStrokePaint(bubbleBorderColor);
        ctx->SetStrokeWidth(bubbleBorderWidth);
        ctx->DrawCircle(center, c.r);
    }
    if (hovered) {
        ctx->SetStrokePaint(Darken(fillIn, 0.35f));
        ctx->SetStrokeWidth(std::max(1.5f, bubbleBorderWidth + 1.0f));
        ctx->DrawCircle(center, c.r);
    }
}

void UltraCanvasBubbleChartElement::RenderBubbleLabels(IRenderContext* ctx) {
    if (nameLabelMode == BubbleNameLabelMode::Hidden &&
        bubbleValueLabelMode == BubbleValueLabelMode::Hidden) {
        return;
    }
    double grow = animationEnabled ? EaseOutCubic(GetAnimationProgress()) : 1.0;
    ctx->SetFontSize(labelFontSize);

    for (const auto& c : layoutCircles) {
        const auto& b = bubbles[c.index];
        double r = c.r * grow;
        if (r <= 0.5) continue;
        Color fill = ResolveBubbleColor(b, c.index);
        Color insideColor = PickContrastTextColor(fill);

        // ----- decide where the name goes -----
        bool nameInside = false, nameBelow = false;
        if (!b.name.empty() &&
            !(hideNamesBelowRadius > 0.0f && r < hideNamesBelowRadius)) {
            int nameW = ctx->GetTextLineWidth(b.name);
            bool fits = (nameW <= r * 1.75) && (r >= minInsideLabelRadius);
            switch (nameLabelMode) {
                case BubbleNameLabelMode::Hidden: break;
                case BubbleNameLabelMode::Inside: nameInside = true; break;
                case BubbleNameLabelMode::BelowBubble: nameBelow = true; break;
                case BubbleNameLabelMode::Auto:
                    if (fits) nameInside = true;
                    else nameBelow = true;
                    break;
            }
        }

        // ----- decide whether the value shows -----
        bool valueInside = false;
        std::string valueText;
        if (bubbleValueLabelMode != BubbleValueLabelMode::Hidden) {
            valueText = FormatValue(b.size);
            int valueW = ctx->GetTextLineWidth(valueText);
            bool fits = (valueW <= r * 1.75) && (r >= minInsideLabelRadius);
            valueInside = (bubbleValueLabelMode == BubbleValueLabelMode::Inside) || fits;
            if (bubbleValueLabelMode == BubbleValueLabelMode::Auto && !fits) {
                valueInside = false;
            }
        }

        double lineH = labelFontSize * 1.25;
        if (nameInside && valueInside) {
            Size2Di nameSz = ctx->GetTextLineDimensions(b.name);
            Size2Di valSz = ctx->GetTextLineDimensions(valueText);
            ctx->SetTextPaint(insideColor);
            ctx->DrawText(b.name, Point2Dd(c.cx - nameSz.width / 2.0, c.cy - lineH));
            ctx->DrawText(valueText, Point2Dd(c.cx - valSz.width / 2.0, c.cy + lineH - valSz.height));
        } else if (nameInside) {
            Size2Di nameSz = ctx->GetTextLineDimensions(b.name);
            ctx->SetTextPaint(insideColor);
            ctx->DrawText(b.name, Point2Dd(c.cx - nameSz.width / 2.0, c.cy - nameSz.height / 2.0));
        } else if (valueInside) {
            Size2Di valSz = ctx->GetTextLineDimensions(valueText);
            ctx->SetTextPaint(insideColor);
            ctx->DrawText(valueText, Point2Dd(c.cx - valSz.width / 2.0, c.cy - valSz.height / 2.0));
        }

        if (nameBelow) {
            Size2Di nameSz = ctx->GetTextLineDimensions(b.name);
            ctx->SetTextPaint(nameLabelColor);
            ctx->DrawText(b.name, Point2Dd(c.cx - nameSz.width / 2.0, c.cy + r + 3.0));
        }
    }
}

void UltraCanvasBubbleChartElement::RenderMatrixChrome(IRenderContext* ctx) {
    const auto& mm = matrixMetrics;
    if (mm.cellW <= 0 || mm.cellH <= 0) return;

    // Column headers.
    ctx->SetTextPaint(matrixHeaderColor);
    ctx->SetFontSize(matrixHeaderFontSize);
    if (matrixHeaderBold) ctx->SetFontWeight(FontWeight::Bold);
    for (size_t col = 0; col < matrixColumns.size(); ++col) {
        int w = ctx->GetTextLineWidth(matrixColumns[col]);
        double cx = mm.x0 + (col + 0.5) * mm.cellW;
        ctx->DrawText(matrixColumns[col],
                      Point2Dd(cx - w / 2.0, cachedPlotArea.y + 2.0));
    }
    if (matrixHeaderBold) ctx->SetFontWeight(FontWeight::Normal);

    // Row labels, right-aligned against the first column.
    ctx->SetTextPaint(matrixRowLabelColor);
    ctx->SetFontSize(matrixRowLabelFontSize);
    for (size_t row = 0; row < matrixRows.size(); ++row) {
        Size2Di sz = ctx->GetTextLineDimensions(matrixRows[row]);
        double cy = mm.y0 + (row + 0.5) * mm.cellH;
        ctx->DrawText(matrixRows[row],
                      Point2Dd(mm.x0 - 12.0 - sz.width, cy - sz.height / 2.0));
    }
}

void UltraCanvasBubbleChartElement::RenderParentCircles(IRenderContext* ctx) {
    double grow = animationEnabled ? EaseOutCubic(GetAnimationProgress()) : 1.0;
    for (const auto& p : parentCircles) {
        double r = p.r * grow;
        if (r <= 0.5) continue;
        ctx->SetFillPaint(p.fill);
        ctx->FillCircle(Point2Dd(p.cx, p.cy), r);

        // Group label in the headroom ring between the children and the rim.
        ctx->SetFontSize(groupLabelFontSize);
        if (groupLabelBold) ctx->SetFontWeight(FontWeight::Bold);
        Color labelColor = (groupLabelColor.a != 0) ? groupLabelColor
                                                    : PickContrastTextColor(p.fill);
        ctx->SetTextPaint(labelColor);
        Size2Di sz = ctx->GetTextLineDimensions(p.group);
        double bandCenterY = p.cy - (p.innerR + (p.r - p.innerR) * 0.5) * grow;
        ctx->DrawText(p.group, Point2Dd(p.cx - sz.width / 2.0,
                                        bandCenterY - sz.height / 2.0));
        if (groupLabelBold) ctx->SetFontWeight(FontWeight::Normal);
    }
}

void UltraCanvasBubbleChartElement::RenderEnclosureCircle(IRenderContext* ctx) {
    Point2Dd center(enclosureCx, enclosureCy);
    if (packedEnclosureFill.a != 0) {
        ctx->SetFillPaint(packedEnclosureFill);
        ctx->FillCircle(center, enclosureR);
    }
    ctx->SetStrokePaint(packedEnclosureStroke);
    ctx->SetStrokeWidth(packedEnclosureWidth);
    ctx->DrawCircle(center, enclosureR);
}

void UltraCanvasBubbleChartElement::RenderTimelineChrome(IRenderContext* ctx) {
    const auto& tm = timelineMetrics;
    if (tm.rowH <= 0.0) return;

    // Row separators plus a light band border around the whole plot.
    ctx->SetStrokePaint(gridColor);
    ctx->SetStrokeWidth(1.0f);
    for (size_t i = 0; i <= timelineRows.size(); ++i) {
        double y = tm.y0 + i * tm.rowH;
        ctx->DrawLine(Point2Dd(tm.x0, y), Point2Dd(tm.x0 + tm.width, y));
    }

    // Row labels, right-aligned before the plot.
    ctx->SetTextPaint(matrixRowLabelColor);
    ctx->SetFontSize(matrixRowLabelFontSize);
    for (size_t i = 0; i < timelineRows.size(); ++i) {
        Size2Di sz = ctx->GetTextLineDimensions(timelineRows[i]);
        double cy = tm.y0 + (i + 0.5) * tm.rowH;
        ctx->DrawText(timelineRows[i],
                      Point2Dd(tm.x0 - 10.0 - sz.width, cy - sz.height / 2.0));
    }

    // X tick labels and light vertical gridlines.
    int nTicks = std::clamp(static_cast<int>(tm.width / 150.0), 2, 10);
    for (int k = 0; k <= nTicks; ++k) {
        double v = tm.xMin + k * (tm.xMax - tm.xMin) / nTicks;
        double x = tm.x0 + tm.sidePad + k * tm.usable / nTicks;

        ctx->SetStrokePaint(gridColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(Point2Dd(x, tm.y0), Point2Dd(x, tm.bottomY));

        std::string label = xLabelFormatter ? xLabelFormatter(v) : FormatAxisLabel(v);
        ctx->SetTextPaint(matrixRowLabelColor);
        ctx->SetFontSize(matrixRowLabelFontSize - 1.0f);
        Size2Di sz = ctx->GetTextLineDimensions(label);
        ctx->DrawText(label, Point2Dd(x - sz.width / 2.0, tm.bottomY + 4.0));
    }
}

void UltraCanvasBubbleChartElement::RenderSizeLegend(IRenderContext* ctx) {
    // Legend values: explicit list, or automatic max / mid / min.
    std::vector<double> values = sizeLegendValues;
    if (values.empty()) {
        double lo = effectiveDomainMin, hi = effectiveDomainMax;
        if (lo <= 0.0 && sizeScale == BubbleSizeScale::Area) lo = hi * 0.1;
        values = {hi, (hi + lo) / 2.0, lo};
    }
    std::sort(values.begin(), values.end(), std::greater<double>());

    double maxR = 0.0;
    std::vector<double> radii;
    for (double v : values) {
        double r = SizeToRadius(v, effectiveDomainMin, effectiveDomainMax,
                                effectiveMinRadius, effectiveMaxRadius);
        radii.push_back(r);
        maxR = std::max(maxR, r);
    }
    if (maxR <= 0.0) return;

    double reserved = SizeLegendReservedWidth();
    double cx = GetWidth() - reserved / 2.0;
    double bottomY = cachedPlotArea.GetBottom() - 4.0;

    ctx->SetFontSize(10.0f);
    ctx->SetTextPaint(Color(80, 80, 80, 255));

    // Title above the tallest circle (leave room for the value labels).
    if (!sizeLegendTitle.empty()) {
        int w = ctx->GetTextLineWidth(sizeLegendTitle);
        ctx->DrawText(sizeLegendTitle,
                      Point2Dd(cx - w / 2.0, bottomY - 2.0 * maxR - 30.0));
    }

    // Nested circles, tangent at the bottom, biggest at the back; each value
    // sits just above its circle's top edge.
    for (size_t i = 0; i < radii.size(); ++i) {
        double r = radii[i];
        double cy = bottomY - r;
        ctx->SetStrokePaint(Color(120, 120, 120, 255));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawCircle(Point2Dd(cx, cy), r);

        std::string label = FormatValue(values[i]);
        Size2Di sz = ctx->GetTextLineDimensions(label);
        ctx->DrawText(label, Point2Dd(cx - sz.width / 2.0, cy - r - sz.height - 1.0));
    }
}

void UltraCanvasBubbleChartElement::RenderFootnote(IRenderContext* ctx) {
    if (footnote.empty()) return;
    ctx->SetFontSize(9.5f);
    ctx->SetTextPaint(Color(110, 110, 110, 255));
    Size2Di sz = ctx->GetTextLineDimensions(footnote);
    ctx->DrawText(footnote, Point2Dd((GetWidth() - sz.width) / 2.0,
                                     GetHeight() - sz.height - 4.0));
}

// =============================================================================
// INTERACTION
// =============================================================================

size_t UltraCanvasBubbleChartElement::HitTest(const Point2Di& pos) const {
    // Iterate in reverse draw order so the topmost circle wins.
    for (auto it = layoutCircles.rbegin(); it != layoutCircles.rend(); ++it) {
        double dx = pos.x - it->cx;
        double dy = pos.y - it->cy;
        if (dx * dx + dy * dy <= it->r * it->r) return it->index;
    }
    return SIZE_MAX;
}

bool UltraCanvasBubbleChartElement::HandleChartMouseMove(const Point2Di& mousePos) {
    size_t hit = HitTest(mousePos);
    if (hit != hoveredBubble) {
        hoveredBubble = hit;
        RequestRedraw();
    }
    if (hit == SIZE_MAX) {
        if (isTooltipActive) HideTooltip();
        return false;
    }
    if (enableTooltips) {
        const auto& b = bubbles[hit];
        ChartDataPoint point(b.x, b.y, b.size, b.name, b.colorValue, b.overrideColor);
        ShowChartPointTooltip(mousePos, point, hit);
    }
    return true;
}

bool UltraCanvasBubbleChartElement::OnEvent(const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseDown:
            if (onBubbleClick) {
                size_t hit = HitTest(Point2Di(event.pointer.x, event.pointer.y));
                if (hit != SIZE_MAX) {
                    onBubbleClick(hit, bubbles[hit]);
                    return true;
                }
            }
            break;
        case UCEventType::MouseLeave:
            if (hoveredBubble != SIZE_MAX) {
                hoveredBubble = SIZE_MAX;
                RequestRedraw();
            }
            if (isTooltipActive) HideTooltip();
            break;
        default:
            break;
    }
    return UltraCanvasChartElementBase::OnEvent(event);
}

std::string UltraCanvasBubbleChartElement::GenerateTooltipContent(
        const ChartDataPoint& point, size_t index) {
    if (customTooltipGenerator) {
        return customTooltipGenerator(point, index);
    }
    if (index >= bubbles.size()) {
        return UltraCanvasChartElementBase::GenerateTooltipContent(point, index);
    }
    const auto& b = bubbles[index];
    std::string content;
    if (!seriesName.empty()) content += seriesName + "\n";
    if (!b.name.empty()) content += b.name + "\n";

    switch (mode) {
        case BubbleChartMode::ScatterBubbles:
            content += "X: " + FormatAxisLabel(b.x) + "\n";
            content += "Y: " + FormatAxisLabel(b.y) + "\n";
            content += "Size: " + FormatValue(b.size);
            if (colorMode == BubbleColorMode::ColormapByValue) {
                content += "\nColor: " + FormatAxisLabel(b.colorValue);
            }
            break;
        case BubbleChartMode::PackedBubbles:
            if (!b.category.empty()) content += b.category + "\n";
            content += "Value: " + FormatValue(b.size);
            break;
        case BubbleChartMode::BubbleMatrix:
            if (b.col >= 0 && b.col < static_cast<int>(matrixColumns.size())) {
                content += matrixColumns[b.col] + ": " + FormatValue(b.size);
            } else {
                content += FormatValue(b.size);
            }
            break;
        case BubbleChartMode::HierarchicalPacked:
            if (!b.category.empty()) content += b.category + "\n";
            content += "Value: " + FormatValue(b.size);
            break;
        case BubbleChartMode::TimelineBubbles: {
            if (!b.category.empty()) content += b.category + "\n";
            std::string xs = xLabelFormatter ? xLabelFormatter(b.x)
                                             : FormatAxisLabel(b.x);
            content += FormatValue(b.size) + " @ " + xs;
            break;
        }
    }
    return content;
}

} // namespace UltraCanvas

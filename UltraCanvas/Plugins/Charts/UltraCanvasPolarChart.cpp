// Plugins/Charts/UltraCanvasPolarChart.cpp
// Comprehensive polar chart element: scatter, line, spline, area and column
// series plotted on a configurable angle/radius coordinate system
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasPolarChart.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

    namespace {
        constexpr double kDegToRad = M_PI / 180.0;
        constexpr double kTwoPi = 2.0 * M_PI;

        // Arc flattening step: one vertex every 2.5 degrees keeps curves smooth
        // at the sizes charts are normally drawn at.
        constexpr double kArcStep = 2.5 * kDegToRad;

        double NormalizeRadians(double a) {
            while (a < 0.0) a += kTwoPi;
            while (a >= kTwoPi) a -= kTwoPi;
            return a;
        }

        bool IsFinite(double v) {
            return std::isfinite(v);
        }
    } // namespace

    // =========================================================================
    // CONSTRUCTION / DEFAULTS
    // =========================================================================

    std::vector<Color> UltraCanvasPolarChart::DefaultPalette() {
        return {
            Color(102, 178, 235, 255),   // Light blue
            Color( 25, 118, 210, 255),   // Blue
            Color(245, 130,  32, 255),   // Orange
            Color(255, 202,  40, 255),   // Amber
            Color( 76, 175,  80, 255),   // Green
            Color(233,  30,  99, 255),   // Pink
            Color(126,  87, 194, 255),   // Purple
            Color(  0, 172, 193, 255)    // Teal
        };
    }

    UltraCanvasPolarChart::UltraCanvasPolarChart(
        const std::string& id, int x, int y, int w, int h)
        : UltraCanvasChartElementBase(id, x, y, w, h)
    {
        colorPalette = DefaultPalette();

        // The polar chart draws its own circular frame; the Cartesian helpers
        // inherited from the base class are not used.
        showBackground = true;
        backgroundColor = Color(255, 255, 255, 255);
        showGrid = false;
        showAxes = false;
        showValueLabels = false;
        enableTooltips = true;
        enableZoom = false;
        enablePan = false;
        enableSelection = false;

        secondaryAngleAxis.placement = PolarLabelPlacement::Inside;
        secondaryAngleAxis.labelColor = Color(200, 60, 60, 255);
        secondaryAngleAxis.axisLineColor = Color(200, 60, 60, 160);
        secondaryAngleAxis.tickIntervalDeg = 15.0f;
        secondaryAngleAxis.fontSize = 9.0f;
    }

    // =========================================================================
    // SERIES MANAGEMENT
    // =========================================================================

    void UltraCanvasPolarChart::AddSeries(const PolarSeries& series) {
        seriesList.push_back(series);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::AddSeries(
        const std::string& name, PolarSeriesType type,
        const std::vector<std::pair<double, double>>& angleRadiusPairs,
        const Color& color)
    {
        PolarSeries series(name, type, color);
        series.points.reserve(angleRadiusPairs.size());
        for (const auto& pair : angleRadiusPairs) {
            series.points.emplace_back(pair.first, pair.second);
        }
        if (type == PolarSeriesType::Scatter) series.closed = false;
        AddSeries(series);
    }

    void UltraCanvasPolarChart::AddCategorySeries(
        const std::string& name, PolarSeriesType type,
        const std::vector<double>& values, const Color& color)
    {
        PolarSeries series(name, type, color);
        series.points.reserve(values.size());
        for (size_t i = 0; i < values.size(); ++i) {
            PolarDataPoint point(0.0, values[i]);
            if (i < categories.size()) point.label = categories[i];
            series.points.push_back(point);
        }
        AddSeries(series);
    }

    void UltraCanvasPolarChart::ClearSeries() {
        seriesList.clear();
        hoveredSeries = SIZE_MAX;
        hoveredPoint = SIZE_MAX;
        InvalidateLayout();
        RequestRedraw();
    }

    PolarSeries* UltraCanvasPolarChart::GetSeries(size_t index) {
        if (index >= seriesList.size()) return nullptr;
        // Callers may mutate the returned series; relayout on the next render.
        InvalidateLayout();
        return &seriesList[index];
    }

    const PolarSeries* UltraCanvasPolarChart::GetSeries(size_t index) const {
        if (index >= seriesList.size()) return nullptr;
        return &seriesList[index];
    }

    void UltraCanvasPolarChart::SetSeriesColor(size_t index, const Color& color) {
        if (index >= seriesList.size()) return;
        seriesList[index].color = color;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetSeriesType(size_t index, PolarSeriesType type) {
        if (index >= seriesList.size()) return;
        seriesList[index].type = type;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetSeriesVisible(size_t index, bool visible) {
        if (index >= seriesList.size()) return;
        if (seriesList[index].visible == visible) return;
        seriesList[index].visible = visible;
        if (onSeriesVisibilityChanged) onSeriesVisibilityChanged(index, visible);
        InvalidateLayout();
        RequestRedraw();
    }

    bool UltraCanvasPolarChart::IsSeriesVisible(size_t index) const {
        if (index >= seriesList.size()) return false;
        return seriesList[index].visible;
    }

    // =========================================================================
    // ANGULAR AXIS CONFIGURATION
    // =========================================================================

    void UltraCanvasPolarChart::SetAngleMode(PolarAngleMode mode) {
        angleMode = mode;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetCategories(const std::vector<std::string>& cats) {
        categories = cats;
        angleMode = PolarAngleMode::Categorical;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetCategoryPlacement(PolarCategoryPlacement placement) {
        categoryPlacement = placement;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetAngleRange(double minAngle, double maxAngle) {
        if (maxAngle == minAngle) return;
        angleMin = std::min(minAngle, maxAngle);
        angleMax = std::max(minAngle, maxAngle);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetZeroAngle(float degrees) {
        zeroAngleDeg = degrees;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetDirection(PolarDirection dir) {
        direction = dir;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetSweepAngle(float degrees) {
        sweepAngleDeg = std::max(10.0f, std::min(360.0f, degrees));
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetAngleAxisStyle(const PolarAngleAxisStyle& style) {
        angleAxis = style;
        InvalidateCache();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetShowAngleLabels(bool show) {
        angleAxis.placement = show ? PolarLabelPlacement::Outside
                                   : PolarLabelPlacement::Hidden;
        InvalidateCache();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetAngleTickInterval(float degrees) {
        angleAxis.tickIntervalDeg = std::max(0.0f, degrees);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetAngleLabelOrientation(PolarLabelOrientation orientation) {
        angleAxis.orientation = orientation;
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetAngleLabelPlacement(PolarLabelPlacement placement) {
        angleAxis.placement = placement;
        InvalidateCache();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetAngleLabelFormatter(std::function<std::string(double)> formatter) {
        angleLabelFormatter = std::move(formatter);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetSecondaryAngleAxisEnabled(bool enabled) {
        secondaryAngleAxisEnabled = enabled;
        InvalidateCache();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetSecondaryAngleAxisStyle(const PolarAngleAxisStyle& style) {
        secondaryAngleAxis = style;
        InvalidateCache();
        InvalidateLayout();
        RequestRedraw();
    }

    // =========================================================================
    // RADIAL AXIS CONFIGURATION
    // =========================================================================

    void UltraCanvasPolarChart::SetRadialScale(PolarRadialScale scale) {
        radialScale = scale;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetRadialRange(double minValue, double maxValue) {
        manualRadialRange = true;
        manualRadialMin = std::min(minValue, maxValue);
        manualRadialMax = std::max(minValue, maxValue);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::ClearRadialRange() {
        manualRadialRange = false;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetRadialTickCount(int count) {
        radialTickCount = std::max(2, std::min(20, count));
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetRadialTickInterval(double interval) {
        radialTickInterval = std::max(0.0, interval);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetRadialAxisIncludesZero(bool include) {
        radialIncludesZero = include;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetRadialAxisReversed(bool reversed) {
        radialReversed = reversed;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetInnerRadiusFraction(float fraction) {
        innerRadiusFraction = std::max(0.0f, std::min(0.9f, fraction));
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetShowRadialLabels(bool show) {
        showRadialLabels = show;
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetRadialLabelAngle(double angle) {
        radialLabelAngle = angle;
        radialLabelAngleSet = true;
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetRadialLabelFormatter(std::function<std::string(double)> formatter) {
        radialLabelFormatter = std::move(formatter);
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetRadialLabelUnit(const std::string& unit) {
        radialLabelUnit = unit;
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetRadialAxisLineVisible(bool visible) {
        radialAxisLineVisible = visible;
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetRadialLabelColor(const Color& color) {
        radialLabelColor = color;
        RequestRedraw();
    }

    // =========================================================================
    // GRID / BACKGROUND CONFIGURATION
    // =========================================================================

    void UltraCanvasPolarChart::SetGridShape(PolarGridShape shape) {
        gridShape = shape;
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetShowRadialGrid(bool show) {
        showRadialGrid = show;
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetShowAngularGrid(bool show) {
        showAngularGrid = show;
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetGridLineWidth(float width) {
        gridLineWidth = std::max(0.0f, width);
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetMinorRadialGridCount(int count) {
        minorRadialGridCount = std::max(0, std::min(10, count));
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetAlternatingBands(bool enabled,
                                                    const Color& colorA,
                                                    const Color& colorB) {
        alternatingBands = enabled;
        alternatingColorA = colorA;
        alternatingColorB = colorB;
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetPlotBackgroundColor(const Color& color) {
        plotBackgroundColor = color;
        RequestRedraw();
    }

    void UltraCanvasPolarChart::AddRadialBand(const PolarRadialBand& band) {
        radialBands.push_back(band);
        RequestRedraw();
    }

    void UltraCanvasPolarChart::AddRadialBand(double from, double to,
                                              const Color& color,
                                              const std::string& label) {
        radialBands.emplace_back(from, to, color, label);
        RequestRedraw();
    }

    void UltraCanvasPolarChart::ClearRadialBands() {
        radialBands.clear();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::AddAngularBand(const PolarAngularBand& band) {
        angularBands.push_back(band);
        RequestRedraw();
    }

    void UltraCanvasPolarChart::AddAngularBand(double fromAngle, double toAngle,
                                               const Color& color,
                                               const std::string& label) {
        angularBands.emplace_back(fromAngle, toAngle, color, label);
        RequestRedraw();
    }

    void UltraCanvasPolarChart::ClearAngularBands() {
        angularBands.clear();
        RequestRedraw();
    }

    // =========================================================================
    // SERIES LAYOUT / STYLE CONFIGURATION
    // =========================================================================

    void UltraCanvasPolarChart::SetStackMode(PolarStackMode mode) {
        stackMode = mode;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetColumnWidthFraction(float fraction) {
        columnWidthFraction = std::max(0.05f, std::min(1.0f, fraction));
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetColumnBorder(const Color& color, float width) {
        columnBorderColor = color;
        columnBorderWidth = std::max(0.0f, width);
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetSortPointsByAngle(bool sort) {
        sortPointsByAngle = sort;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetSplineTension(float tension) {
        splineTension = std::max(0.0f, std::min(1.0f, tension));
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetColorPalette(const std::vector<Color>& palette) {
        colorPalette = palette.empty() ? DefaultPalette() : palette;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetSubtitle(const std::string& text) {
        subtitle = text;
        InvalidateCache();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetTitleColor(const Color& color) {
        titleColor = color;
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetLabelFont(const std::string& family, float size) {
        labelFontFamily = family;
        labelFontSize = std::max(6.0f, size);
        angleAxis.fontFamily = family;
        angleAxis.fontSize = labelFontSize;
        InvalidateCache();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetLegendPosition(PolarLegendPosition position) {
        legendPosition = position;
        InvalidateCache();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetLegendFont(const std::string& family, float size) {
        legendFontFamily = family;
        legendFontSize = std::max(6.0f, size);
        InvalidateCache();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetLegendTextColor(const Color& color) {
        legendTextColor = color;
        RequestRedraw();
    }

    void UltraCanvasPolarChart::SetLegendToggleEnabled(bool enabled) {
        legendToggleEnabled = enabled;
    }

    void UltraCanvasPolarChart::SetHoverHighlightEnabled(bool enabled) {
        hoverHighlightEnabled = enabled;
        if (!enabled && hoveredPoint != SIZE_MAX) {
            hoveredSeries = SIZE_MAX;
            hoveredPoint = SIZE_MAX;
            RequestRedraw();
        }
    }

    void UltraCanvasPolarChart::SetPolarTooltipFormatter(
        std::function<std::string(const PolarSeries&, const PolarDataPoint&)> formatter) {
        polarTooltipFormatter = std::move(formatter);
    }

    void UltraCanvasPolarChart::SetRotateOnDrag(bool enabled) {
        rotateOnDrag = enabled;
        if (!enabled) isRotating = false;
    }

    // =========================================================================
    // COORDINATE MAPPING
    // =========================================================================

    size_t UltraCanvasPolarChart::CategorySlotCount() const {
        size_t count = categories.size();
        for (const auto& series : seriesList) {
            count = std::max(count, series.points.size());
        }
        return count;
    }

    double UltraCanvasPolarChart::CategoryAngle(size_t index) const {
        size_t slots = CategorySlotCount();
        if (slots == 0) return angleMin;
        double span = angleMax - angleMin;
        double offset = (categoryPlacement == PolarCategoryPlacement::BetweenSpokes)
                        ? 0.5 : 0.0;
        return angleMin + (static_cast<double>(index) + offset) * span
                          / static_cast<double>(slots);
    }

    double UltraCanvasPolarChart::AngleToScreenRadians(double dataAngle) const {
        double span = angleMax - angleMin;
        if (std::fabs(span) < 1e-12) span = 360.0;
        double fraction = (dataAngle - angleMin) / span;
        double dirSign = (direction == PolarDirection::Clockwise) ? 1.0 : -1.0;
        double screenDeg = zeroAngleDeg + dirSign * fraction * sweepAngleDeg;
        return screenDeg * kDegToRad;
    }

    double UltraCanvasPolarChart::ScreenRadiansToAngle(double screenRad) const {
        double span = angleMax - angleMin;
        if (std::fabs(span) < 1e-12) span = 360.0;
        double dirSign = (direction == PolarDirection::Clockwise) ? 1.0 : -1.0;
        if (sweepAngleDeg <= 0.0f) return angleMin;
        double deltaDeg = (screenRad / kDegToRad - zeroAngleDeg) * dirSign;
        // Bring the offset into [0, 360) so a full-circle sweep wraps correctly.
        while (deltaDeg < 0.0) deltaDeg += 360.0;
        while (deltaDeg >= 360.0) deltaDeg -= 360.0;
        return angleMin + deltaDeg / sweepAngleDeg * span;
    }

    double UltraCanvasPolarChart::NormalizeRadial(double value) const {
        double lo = resolvedRadialMin;
        double hi = resolvedRadialMax;
        if (hi <= lo) return 0.0;

        double t;
        switch (radialScale) {
            case PolarRadialScale::Logarithmic: {
                double safeLo = std::max(lo, 1e-9);
                double safeVal = std::max(value, safeLo);
                double safeHi = std::max(hi, safeLo * 10.0);
                t = (std::log10(safeVal) - std::log10(safeLo))
                  / (std::log10(safeHi) - std::log10(safeLo));
                break;
            }
            case PolarRadialScale::SquareRoot: {
                double span = hi - lo;
                double v = std::max(0.0, value - lo);
                t = std::sqrt(v) / std::sqrt(span);
                break;
            }
            case PolarRadialScale::Linear:
            default:
                t = (value - lo) / (hi - lo);
                break;
        }

        if (!IsFinite(t)) t = 0.0;
        t = std::max(0.0, std::min(1.0, t));
        return radialReversed ? 1.0 - t : t;
    }

    double UltraCanvasPolarChart::RadiusToPixels(double value) const {
        return cachedInnerRadius
             + NormalizeRadial(value) * (cachedOuterRadius - cachedInnerRadius);
    }

    double UltraCanvasPolarChart::PixelsToRadius(double pixels) const {
        double band = cachedOuterRadius - cachedInnerRadius;
        if (band <= 0.0) return resolvedRadialMin;
        double t = (pixels - cachedInnerRadius) / band;
        t = std::max(0.0, std::min(1.0, t));
        if (radialReversed) t = 1.0 - t;

        switch (radialScale) {
            case PolarRadialScale::Logarithmic: {
                double safeLo = std::max(resolvedRadialMin, 1e-9);
                double safeHi = std::max(resolvedRadialMax, safeLo * 10.0);
                double logLo = std::log10(safeLo);
                return std::pow(10.0, logLo + t * (std::log10(safeHi) - logLo));
            }
            case PolarRadialScale::SquareRoot: {
                double span = resolvedRadialMax - resolvedRadialMin;
                return resolvedRadialMin + t * t * span;
            }
            case PolarRadialScale::Linear:
            default:
                return resolvedRadialMin
                     + t * (resolvedRadialMax - resolvedRadialMin);
        }
    }

    Point2Dd UltraCanvasPolarChart::PolarToScreen(double dataAngle,
                                                  double radiusValue) const {
        double a = AngleToScreenRadians(dataAngle);
        double r = RadiusToPixels(radiusValue);
        return Point2Dd(cachedCenter.x + r * std::cos(a),
                        cachedCenter.y + r * std::sin(a));
    }

    bool UltraCanvasPolarChart::ScreenToPolar(const Point2Dd& pos,
                                              double& dataAngle,
                                              double& radiusValue) const {
        double dx = pos.x - cachedCenter.x;
        double dy = pos.y - cachedCenter.y;
        double r = std::sqrt(dx * dx + dy * dy);
        if (r < cachedInnerRadius - 1.0 || r > cachedOuterRadius + 1.0) return false;
        dataAngle = ScreenRadiansToAngle(std::atan2(dy, dx));
        radiusValue = PixelsToRadius(r);
        return true;
    }

    Color UltraCanvasPolarChart::ResolveSeriesColor(size_t index) const {
        if (index < seriesList.size() && seriesList[index].color.a > 0) {
            return seriesList[index].color;
        }
        if (colorPalette.empty()) return Color(128, 128, 128, 255);
        return colorPalette[index % colorPalette.size()];
    }

    Color UltraCanvasPolarChart::ResolveFillColor(const PolarSeries& series,
                                                  size_t index) const {
        if (series.fillColor.a > 0) return series.fillColor;
        Color base = ResolveSeriesColor(index);
        float opacity = std::max(0.0f, std::min(1.0f, series.fillOpacity));
        return base.WithAlpha(static_cast<uint8_t>(base.a * opacity));
    }

    // =========================================================================
    // FORMATTING
    // =========================================================================

    std::string UltraCanvasPolarChart::FormatRadialLabel(double value) const {
        if (radialLabelFormatter) return radialLabelFormatter(value);

        char buf[48];
        double magnitude = std::fabs(value);
        if (magnitude >= 1e6) {
            std::snprintf(buf, sizeof(buf), "%.1fM", value / 1e6);
        } else if (magnitude >= 1e4) {
            std::snprintf(buf, sizeof(buf), "%.0fk", value / 1e3);
        } else if (std::floor(value) == value) {
            std::snprintf(buf, sizeof(buf), "%.0f", value);
        } else if (magnitude >= 1.0) {
            std::snprintf(buf, sizeof(buf), "%.1f", value);
        } else {
            std::snprintf(buf, sizeof(buf), "%.2f", value);
        }
        return std::string(buf) + radialLabelUnit;
    }

    std::string UltraCanvasPolarChart::FormatAngleLabel(double value) const {
        if (angleLabelFormatter) return angleLabelFormatter(value);
        char buf[32];
        if (std::floor(value) == value) {
            std::snprintf(buf, sizeof(buf), "%.0f\xC2\xB0", value);   // UTF-8 degree sign
        } else {
            std::snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", value);
        }
        return std::string(buf);
    }

    std::string UltraCanvasPolarChart::BuildTooltipText(size_t seriesIndex,
                                                        size_t pointIndex) const {
        if (seriesIndex >= seriesList.size()) return "";
        const PolarSeries& series = seriesList[seriesIndex];
        if (pointIndex >= series.points.size()) return "";
        const PolarDataPoint& point = series.points[pointIndex];

        if (polarTooltipFormatter) return polarTooltipFormatter(series, point);

        std::string text = series.name;
        if (!text.empty()) text += "\n";

        if (angleMode == PolarAngleMode::Categorical) {
            if (!point.label.empty()) {
                text += point.label;
            } else if (pointIndex < categories.size()) {
                text += categories[pointIndex];
            } else {
                text += "Slot " + std::to_string(pointIndex + 1);
            }
        } else {
            if (!point.label.empty()) text += point.label + "\n";
            text += "Angle: " + FormatAngleLabel(point.angle);
        }
        text += "\nValue: " + FormatRadialLabel(point.radius);
        return text;
    }

    std::vector<size_t> UltraCanvasPolarChart::SeriesDrawOrder(
        const PolarSeries& series) const
    {
        std::vector<size_t> order(series.points.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;

        if (sortPointsByAngle && angleMode == PolarAngleMode::Numeric) {
            std::stable_sort(order.begin(), order.end(),
                [&series](size_t a, size_t b) {
                    return series.points[a].angle < series.points[b].angle;
                });
        }
        return order;
    }

    // =========================================================================
    // LAYOUT
    // =========================================================================

    void UltraCanvasPolarChart::InvalidateLayout() {
        layoutValid = false;
    }

    void UltraCanvasPolarChart::EnsureLayout() {
        // The plot area reserves room for the title, so react to a title set
        // through the base class, which cannot notify us.
        if (layoutTitleSnapshot != chartTitle) {
            layoutTitleSnapshot = chartTitle;
            InvalidateLayout();
        }
        if (lastLayoutWidth != GetWidth() || lastLayoutHeight != GetHeight()) {
            lastLayoutWidth = GetWidth();
            lastLayoutHeight = GetHeight();
            InvalidateLayout();
        }
        // Margins depend on the legend, the categories and the axis fonts, so
        // any change that invalidates the layout also invalidates the plot area.
        if (!layoutValid) InvalidateCache();
        UpdateRenderingCache();
        if (!layoutValid) RebuildLayout();
    }

    ChartPlotArea UltraCanvasPolarChart::CalculatePlotArea() {
        double top = 10.0, bottom = 10.0, left = 10.0, right = 10.0;

        if (!chartTitle.empty()) top += 26.0;
        if (!subtitle.empty())   top += 18.0;

        // Legend band. Text is measured at render time; this reserves a band
        // sized from the font metrics so the plot circle never overlaps it.
        legendBandSize = 0.0;
        if (legendPosition != PolarLegendPosition::NoLegend && !seriesList.empty()) {
            if (legendPosition == PolarLegendPosition::Top ||
                legendPosition == PolarLegendPosition::Bottom) {
                legendBandSize = legendFontSize + 16.0;
            } else {
                size_t longest = 0;
                for (const auto& series : seriesList) {
                    longest = std::max(longest, series.name.size());
                }
                legendBandSize = std::min(180.0,
                    28.0 + static_cast<double>(longest) * legendFontSize * 0.58);
            }
            switch (legendPosition) {
                case PolarLegendPosition::Top:    top    += legendBandSize; break;
                case PolarLegendPosition::Bottom: bottom += legendBandSize; break;
                case PolarLegendPosition::Left:   left   += legendBandSize; break;
                case PolarLegendPosition::Right:  right  += legendBandSize; break;
                default: break;
            }
        }

        // Room for the angular labels drawn outside the axis circle.
        double labelMargin = 0.0;
        auto marginFor = [this](const PolarAngleAxisStyle& style) -> double {
            if (!style.visible || style.placement != PolarLabelPlacement::Outside) {
                return 0.0;
            }
            size_t longest = 4;   // "330°"
            if (angleMode == PolarAngleMode::Categorical) {
                for (const auto& category : categories) {
                    longest = std::max(longest, category.size());
                }
            }
            double estimate = style.labelOffset + style.tickLength
                            + static_cast<double>(longest) * style.fontSize * 0.55;
            return std::min(estimate, 110.0);
        };
        labelMargin = marginFor(angleAxis);
        if (secondaryAngleAxisEnabled) {
            labelMargin = std::max(labelMargin, marginFor(secondaryAngleAxis));
        }

        left += labelMargin; right += labelMargin;
        top += labelMargin;  bottom += labelMargin;

        ChartPlotArea area;
        area.x = left;
        area.y = top;
        area.width  = std::max(20.0, static_cast<double>(GetWidth())  - left - right);
        area.height = std::max(20.0, static_cast<double>(GetHeight()) - top - bottom);
        return area;
    }

    void UltraCanvasPolarChart::ComputeRadialRange() {
        if (manualRadialRange) {
            resolvedRadialMin = manualRadialMin;
            resolvedRadialMax = manualRadialMax;
            if (resolvedRadialMax <= resolvedRadialMin) {
                resolvedRadialMax = resolvedRadialMin + 1.0;
            }
            return;
        }

        double dataMin = std::numeric_limits<double>::max();
        double dataMax = std::numeric_limits<double>::lowest();
        bool hasValue = false;

        auto observe = [&](double v) {
            if (!IsFinite(v)) return;
            dataMin = std::min(dataMin, v);
            dataMax = std::max(dataMax, v);
            hasValue = true;
        };

        const bool stacking = stackMode != PolarStackMode::NoStacking &&
                              angleMode == PolarAngleMode::Categorical;

        if (stacking) {
            size_t slots = CategorySlotCount();
            std::vector<double> positiveTotals(slots, 0.0);
            std::vector<double> negativeTotals(slots, 0.0);

            for (const auto& series : seriesList) {
                if (!series.visible) continue;
                bool stacked = series.type == PolarSeriesType::Column ||
                               series.type == PolarSeriesType::Area ||
                               series.type == PolarSeriesType::SplineArea;
                for (size_t i = 0; i < series.points.size() && i < slots; ++i) {
                    double v = series.points[i].radius;
                    if (!IsFinite(v)) continue;
                    if (stacked) {
                        if (v >= 0.0) positiveTotals[i] += v;
                        else          negativeTotals[i] += v;
                    } else {
                        observe(v);
                    }
                }
            }
            for (size_t i = 0; i < slots; ++i) {
                observe(positiveTotals[i]);
                if (negativeTotals[i] < 0.0) observe(negativeTotals[i]);
            }
        } else {
            for (const auto& series : seriesList) {
                if (!series.visible) continue;
                for (const auto& point : series.points) observe(point.radius);
            }
        }

        if (!hasValue) {
            resolvedRadialMin = 0.0;
            resolvedRadialMax = 1.0;
            return;
        }

        if (stackMode == PolarStackMode::PercentStacked &&
            angleMode == PolarAngleMode::Categorical) {
            resolvedRadialMin = 0.0;
            resolvedRadialMax = 100.0;
            return;
        }

        if (radialScale == PolarRadialScale::Logarithmic) {
            // Logarithmic axes need a strictly positive lower bound.
            double smallestPositive = std::numeric_limits<double>::max();
            for (const auto& series : seriesList) {
                if (!series.visible) continue;
                for (const auto& point : series.points) {
                    if (point.radius > 0.0) {
                        smallestPositive = std::min(smallestPositive, point.radius);
                    }
                }
            }
            resolvedRadialMin = (smallestPositive < std::numeric_limits<double>::max())
                              ? smallestPositive : 1.0;
            resolvedRadialMax = std::max(dataMax, resolvedRadialMin * 10.0);
            return;
        }

        resolvedRadialMin = (radialIncludesZero && dataMin > 0.0) ? 0.0 : dataMin;
        resolvedRadialMax = (radialIncludesZero && dataMax < 0.0) ? 0.0 : dataMax;
        if (resolvedRadialMax <= resolvedRadialMin) {
            resolvedRadialMax = resolvedRadialMin
                              + (std::fabs(resolvedRadialMin) > 0.0
                                 ? std::fabs(resolvedRadialMin) * 0.5 : 1.0);
        }
    }

    // Round a rough tick spacing to the nearest 1 / 2 / 5 x 10^n step.
    double UltraCanvasPolarChart::NiceTickSpacing(double roughSpacing) {
        if (roughSpacing <= 0.0) return 1.0;
        double exponent = std::floor(std::log10(roughSpacing));
        double fraction = roughSpacing / std::pow(10.0, exponent);

        double niceFraction;
        if      (fraction < 1.5) niceFraction = 1.0;
        else if (fraction < 3.0) niceFraction = 2.0;
        else if (fraction < 7.0) niceFraction = 5.0;
        else                     niceFraction = 10.0;

        return niceFraction * std::pow(10.0, exponent);
    }

    void UltraCanvasPolarChart::ComputeRadialTicks() {
        radialTicks.clear();

        if (radialScale == PolarRadialScale::Logarithmic) {
            double lo = std::max(resolvedRadialMin, 1e-9);
            double hi = std::max(resolvedRadialMax, lo * 10.0);
            double decade = std::pow(10.0, std::floor(std::log10(lo)));
            for (int guard = 0; decade <= hi * 1.0000001 && guard < 40; ++guard) {
                if (decade >= lo * 0.9999999) radialTicks.push_back(decade);
                decade *= 10.0;
            }
            if (radialTicks.empty()) {
                radialTicks.push_back(lo);
                radialTicks.push_back(hi);
            }
            return;
        }

        double spacing = radialTickInterval;
        if (spacing <= 0.0) {
            spacing = NiceTickSpacing((resolvedRadialMax - resolvedRadialMin)
                                      / std::max(1, radialTickCount - 1));
        }
        if (spacing <= 0.0) spacing = 1.0;

        double first = resolvedRadialMin;
        double last  = resolvedRadialMax;
        if (!manualRadialRange) {
            // Snap the automatic range outward so ticks land on round numbers.
            first = std::floor(resolvedRadialMin / spacing) * spacing;
            last  = std::ceil(resolvedRadialMax / spacing) * spacing;
            if (last <= first) last = first + spacing;
            resolvedRadialMin = first;
            resolvedRadialMax = last;
        } else {
            first = std::ceil(resolvedRadialMin / spacing) * spacing;
        }

        for (double v = first; v <= last + spacing * 1e-6; v += spacing) {
            // Guard against float drift producing values such as -0.0000001.
            double snapped = std::fabs(v) < spacing * 1e-9 ? 0.0 : v;
            radialTicks.push_back(snapped);
            if (radialTicks.size() > 64) break;
        }
    }

    void UltraCanvasPolarChart::BuildAngleTicksFor(const PolarAngleAxisStyle& style,
                                                   std::vector<AngleTick>& out) const {
        out.clear();
        if (!style.visible) return;

        if (angleMode == PolarAngleMode::Categorical) {
            size_t slots = CategorySlotCount();
            for (size_t i = 0; i < slots; ++i) {
                AngleTick tick;
                tick.dataAngle = CategoryAngle(i);
                tick.screenRad = AngleToScreenRadians(tick.dataAngle);
                tick.label = (i < categories.size()) ? categories[i]
                                                     : std::to_string(i + 1);
                out.push_back(tick);
            }
            return;
        }

        double span = angleMax - angleMin;
        if (std::fabs(span) < 1e-12) return;

        double interval = style.tickIntervalDeg;
        if (interval <= 0.0f) {
            // Prefer familiar angular steps that divide the domain evenly.
            const double candidates[] = {1.0, 2.0, 5.0, 10.0, 15.0, 20.0, 30.0,
                                         45.0, 60.0, 90.0, 120.0, 180.0};
            interval = span / 8.0;
            for (double candidate : candidates) {
                if (candidate >= span / 12.0) { interval = candidate; break; }
            }
        }
        if (interval <= 0.0) return;

        bool fullCircle = sweepAngleDeg >= 359.99f;
        int steps = static_cast<int>(std::floor(span / interval + 1e-9));
        for (int i = 0; i <= steps; ++i) {
            double value = angleMin + i * interval;
            if (value > angleMax + 1e-9) break;
            // A full sweep would draw the last tick on top of the first one.
            if (fullCircle && std::fabs(value - angleMax) < 1e-9 &&
                std::fabs(span - 360.0) < 1e-9) {
                break;
            }
            AngleTick tick;
            tick.dataAngle = value;
            tick.screenRad = AngleToScreenRadians(value);
            tick.label = FormatAngleLabel(value);
            out.push_back(tick);
            if (out.size() > 180) break;
        }
    }

    void UltraCanvasPolarChart::ComputeAngleTicks() {
        BuildAngleTicksFor(angleAxis, angleTicks);
        if (secondaryAngleAxisEnabled) {
            BuildAngleTicksFor(secondaryAngleAxis, secondaryTicks);
        } else {
            secondaryTicks.clear();
        }

        // Grid spokes normally coincide with the primary ticks. With categories
        // placed between spokes the grid marks slot boundaries instead.
        gridSpokeRadians.clear();
        if (angleMode == PolarAngleMode::Categorical &&
            categoryPlacement == PolarCategoryPlacement::BetweenSpokes) {
            size_t slots = CategorySlotCount();
            if (slots == 0) return;
            double span = angleMax - angleMin;
            bool fullCircle = sweepAngleDeg >= 359.99f;
            size_t lines = fullCircle ? slots : slots + 1;
            for (size_t i = 0; i < lines; ++i) {
                double value = angleMin + static_cast<double>(i) * span
                                        / static_cast<double>(slots);
                gridSpokeRadians.push_back(AngleToScreenRadians(value));
            }
            return;
        }

        std::vector<AngleTick> gridTicks;
        if (angleAxis.visible) {
            gridTicks = angleTicks;
        } else {
            PolarAngleAxisStyle style = angleAxis;
            style.visible = true;
            BuildAngleTicksFor(style, gridTicks);
        }
        for (const AngleTick& tick : gridTicks) {
            gridSpokeRadians.push_back(tick.screenRad);
        }
        // Close an open fan with a spoke on its trailing edge.
        if (angleMode == PolarAngleMode::Numeric && sweepAngleDeg < 359.99f) {
            gridSpokeRadians.push_back(AngleToScreenRadians(angleMax));
        }
    }

    void UltraCanvasPolarChart::BuildPlottedSeries() {
        plottedPoints.clear();

        const bool stacking = stackMode != PolarStackMode::NoStacking &&
                              angleMode == PolarAngleMode::Categorical;
        size_t slots = CategorySlotCount();
        std::vector<double> cumulative(slots, 0.0);
        std::vector<double> slotTotals(slots, 0.0);

        if (stacking && stackMode == PolarStackMode::PercentStacked) {
            // Only the stackable area types contribute to the 100% total;
            // columns are totalled separately when their sectors are built.
            for (const auto& series : seriesList) {
                if (!series.visible) continue;
                if (series.type != PolarSeriesType::Area &&
                    series.type != PolarSeriesType::SplineArea) continue;
                for (size_t i = 0; i < series.points.size() && i < slots; ++i) {
                    slotTotals[i] += std::fabs(series.points[i].radius);
                }
            }
        }

        for (size_t si = 0; si < seriesList.size(); ++si) {
            const PolarSeries& series = seriesList[si];
            if (!series.visible) continue;
            // Columns are laid out separately as annular sectors.
            if (series.type == PolarSeriesType::Column) continue;

            bool stacked = stacking && (series.type == PolarSeriesType::Area ||
                                        series.type == PolarSeriesType::SplineArea);

            std::vector<size_t> order = SeriesDrawOrder(series);
            for (size_t index : order) {
                const PolarDataPoint& point = series.points[index];
                if (!IsFinite(point.radius)) continue;

                double dataAngle = (angleMode == PolarAngleMode::Categorical)
                                 ? CategoryAngle(index) : point.angle;
                double value = point.radius;

                if (stacked && index < cumulative.size()) {
                    if (stackMode == PolarStackMode::PercentStacked) {
                        double total = slotTotals[index];
                        value = (total > 0.0) ? std::fabs(value) / total * 100.0 : 0.0;
                    }
                    cumulative[index] += value;
                    value = cumulative[index];
                }

                PlottedPoint plotted;
                plotted.seriesIndex = si;
                plotted.pointIndex = index;
                plotted.screenRad = AngleToScreenRadians(dataAngle);
                plotted.radiusPx = RadiusToPixels(value);
                plotted.pos = Point2Dd(
                    cachedCenter.x + plotted.radiusPx * std::cos(plotted.screenRad),
                    cachedCenter.y + plotted.radiusPx * std::sin(plotted.screenRad));
                plottedPoints.push_back(plotted);
            }
        }
    }

    void UltraCanvasPolarChart::BuildPlottedColumns() {
        plottedColumns.clear();

        std::vector<size_t> columnSeries;
        for (size_t si = 0; si < seriesList.size(); ++si) {
            if (seriesList[si].visible &&
                seriesList[si].type == PolarSeriesType::Column) {
                columnSeries.push_back(si);
            }
        }
        if (columnSeries.empty()) return;

        size_t slots = CategorySlotCount();
        if (angleMode == PolarAngleMode::Numeric) {
            // Numeric columns still need a slot width; derive one from the
            // densest series so neighbouring columns do not overlap.
            size_t densest = 1;
            for (size_t si : columnSeries) {
                densest = std::max(densest, seriesList[si].points.size());
            }
            slots = densest;
        }
        if (slots == 0) return;

        double sweepRad = sweepAngleDeg * kDegToRad;
        double slotRad = sweepRad / static_cast<double>(slots);
        double groupRad = slotRad * columnWidthFraction;

        const bool stacking = stackMode != PolarStackMode::NoStacking &&
                              angleMode == PolarAngleMode::Categorical;
        size_t groupCount = stacking ? 1 : columnSeries.size();
        double columnRad = groupRad / static_cast<double>(std::max<size_t>(1, groupCount));

        // Column base: the zero line when the axis crosses it, otherwise the
        // inner end of the axis.
        double baseValue = (resolvedRadialMin <= 0.0 && resolvedRadialMax >= 0.0)
                         ? 0.0 : resolvedRadialMin;
        double basePx = RadiusToPixels(baseValue);

        std::vector<double> cumulative(slots, 0.0);
        std::vector<double> slotTotals(slots, 0.0);
        if (stacking && stackMode == PolarStackMode::PercentStacked) {
            for (size_t si : columnSeries) {
                const PolarSeries& series = seriesList[si];
                for (size_t i = 0; i < series.points.size() && i < slots; ++i) {
                    slotTotals[i] += std::fabs(series.points[i].radius);
                }
            }
        }

        for (size_t groupIndex = 0; groupIndex < columnSeries.size(); ++groupIndex) {
            size_t si = columnSeries[groupIndex];
            const PolarSeries& series = seriesList[si];
            Color seriesColor = ResolveSeriesColor(si);

            for (size_t pi = 0; pi < series.points.size(); ++pi) {
                const PolarDataPoint& point = series.points[pi];
                if (!IsFinite(point.radius)) continue;

                double centerAngle = (angleMode == PolarAngleMode::Categorical)
                                   ? AngleToScreenRadians(CategoryAngle(pi))
                                   : AngleToScreenRadians(point.angle);

                double start, end;
                if (stacking) {
                    start = centerAngle - groupRad * 0.5;
                    end   = centerAngle + groupRad * 0.5;
                } else {
                    double groupStart = centerAngle - groupRad * 0.5;
                    start = groupStart + static_cast<double>(groupIndex) * columnRad;
                    end   = start + columnRad;
                }

                double innerPx, outerPx;
                if (stacking && pi < cumulative.size()) {
                    double value = point.radius;
                    if (stackMode == PolarStackMode::PercentStacked) {
                        double total = slotTotals[pi];
                        value = (total > 0.0) ? std::fabs(value) / total * 100.0 : 0.0;
                    }
                    double from = cumulative[pi];
                    cumulative[pi] += value;
                    innerPx = RadiusToPixels(from);
                    outerPx = RadiusToPixels(cumulative[pi]);
                } else {
                    innerPx = basePx;
                    outerPx = RadiusToPixels(point.radius);
                }
                if (outerPx < innerPx) std::swap(innerPx, outerPx);
                if (outerPx - innerPx < 0.5) continue;

                PlottedColumn column;
                column.seriesIndex = si;
                column.pointIndex = pi;
                column.startRad = start;
                column.endRad = end;
                column.innerPx = innerPx;
                column.outerPx = outerPx;
                column.color = point.color.a > 0 ? point.color : seriesColor;
                plottedColumns.push_back(column);
            }
        }
    }

    void UltraCanvasPolarChart::RebuildLayout() {
        layoutValid = true;

        cachedCenter = Point2Dd(cachedPlotArea.x + cachedPlotArea.width / 2.0,
                                cachedPlotArea.y + cachedPlotArea.height / 2.0);
        double maxRadius = std::min(cachedPlotArea.width, cachedPlotArea.height) * 0.5;
        cachedOuterRadius = std::max(10.0, maxRadius - 2.0);
        cachedInnerRadius = cachedOuterRadius * innerRadiusFraction;

        ComputeRadialRange();
        ComputeRadialTicks();
        ComputeAngleTicks();
        BuildPlottedSeries();
        BuildPlottedColumns();
    }

    // =========================================================================
    // LOW-LEVEL DRAWING HELPERS
    // =========================================================================

    void UltraCanvasPolarChart::DrawRingPath(IRenderContext* ctx, double radiusPx,
                                             bool polygonal) const {
        if (radiusPx <= 0.0) return;
        bool fullCircle = sweepAngleDeg >= 359.99f;

        if (polygonal && !gridSpokeRadians.empty()) {
            std::vector<Point2Dd> pts;
            pts.reserve(gridSpokeRadians.size() + 1);
            for (double a : gridSpokeRadians) {
                pts.emplace_back(cachedCenter.x + radiusPx * std::cos(a),
                                 cachedCenter.y + radiusPx * std::sin(a));
            }
            ctx->DrawLinePath(pts, fullCircle);
            return;
        }

        if (fullCircle) {
            ctx->DrawCircle(cachedCenter, radiusPx);
            return;
        }

        double a0 = AngleToScreenRadians(angleMin);
        double a1 = AngleToScreenRadians(angleMax);
        int steps = std::max(4, static_cast<int>(std::ceil(std::fabs(a1 - a0) / kArcStep)));
        std::vector<Point2Dd> pts;
        pts.reserve(steps + 1);
        for (int i = 0; i <= steps; ++i) {
            double a = a0 + (a1 - a0) * static_cast<double>(i) / steps;
            pts.emplace_back(cachedCenter.x + radiusPx * std::cos(a),
                             cachedCenter.y + radiusPx * std::sin(a));
        }
        ctx->DrawLinePath(pts, false);
    }

    void UltraCanvasPolarChart::DrawAnnularSector(IRenderContext* ctx,
                                                  double a0, double a1,
                                                  double r0, double r1,
                                                  const Color& fill,
                                                  const Color& border,
                                                  float borderWidth) const {
        if (a1 < a0) std::swap(a0, a1);
        if (r1 < r0) std::swap(r0, r1);
        if (r1 - r0 < 0.01) return;

        int steps = std::max(2, static_cast<int>(std::ceil((a1 - a0) / kArcStep)));
        std::vector<Point2Dd> pts;
        pts.reserve(2 * (steps + 1));

        for (int i = 0; i <= steps; ++i) {
            double a = a0 + (a1 - a0) * static_cast<double>(i) / steps;
            pts.emplace_back(cachedCenter.x + r1 * std::cos(a),
                             cachedCenter.y + r1 * std::sin(a));
        }
        if (r0 <= 0.01) {
            // Degenerate inner arc: close through the centre point.
            pts.emplace_back(cachedCenter.x, cachedCenter.y);
        } else {
            for (int i = steps; i >= 0; --i) {
                double a = a0 + (a1 - a0) * static_cast<double>(i) / steps;
                pts.emplace_back(cachedCenter.x + r0 * std::cos(a),
                                 cachedCenter.y + r0 * std::sin(a));
            }
        }

        if (fill.a > 0) {
            ctx->SetFillPaint(fill);
            ctx->FillLinePath(pts);
        }
        if (borderWidth > 0.0f && border.a > 0) {
            ctx->SetStrokePaint(border);
            ctx->SetStrokeWidth(borderWidth);
            ctx->DrawLinePath(pts, true);
        }
    }

    void UltraCanvasPolarChart::DrawMarker(IRenderContext* ctx,
                                           PolarMarkerShape shape,
                                           const Point2Dd& pos, double size,
                                           const Color& fill,
                                           const Color& stroke) const {
        if (shape == PolarMarkerShape::NoMarker || size <= 0.0) return;
        double half = size * 0.5;

        switch (shape) {
            case PolarMarkerShape::Circle:
                ctx->DrawFilledCircle(pos, static_cast<float>(half), fill, stroke, 1.0f);
                break;

            case PolarMarkerShape::Square: {
                Rect2Dd rect(pos.x - half, pos.y - half, size, size);
                ctx->DrawFilledRectangle(rect, fill, 1.0f, stroke);
                break;
            }

            case PolarMarkerShape::Diamond: {
                std::vector<Point2Dd> pts = {
                    {pos.x, pos.y - half}, {pos.x + half, pos.y},
                    {pos.x, pos.y + half}, {pos.x - half, pos.y}
                };
                ctx->SetFillPaint(fill);
                ctx->FillLinePath(pts);
                if (stroke.a > 0) {
                    ctx->SetStrokePaint(stroke);
                    ctx->SetStrokeWidth(1.0f);
                    ctx->DrawLinePath(pts, true);
                }
                break;
            }

            case PolarMarkerShape::Triangle: {
                std::vector<Point2Dd> pts = {
                    {pos.x, pos.y - half},
                    {pos.x + half, pos.y + half * 0.8},
                    {pos.x - half, pos.y + half * 0.8}
                };
                ctx->SetFillPaint(fill);
                ctx->FillLinePath(pts);
                if (stroke.a > 0) {
                    ctx->SetStrokePaint(stroke);
                    ctx->SetStrokeWidth(1.0f);
                    ctx->DrawLinePath(pts, true);
                }
                break;
            }

            case PolarMarkerShape::Cross: {
                Color line = stroke.a > 0 ? stroke : fill;
                ctx->SetStrokePaint(line);
                ctx->SetStrokeWidth(std::max(1.0, size * 0.22));
                ctx->DrawLine({pos.x - half, pos.y - half}, {pos.x + half, pos.y + half});
                ctx->DrawLine({pos.x - half, pos.y + half}, {pos.x + half, pos.y - half});
                break;
            }

            case PolarMarkerShape::Star: {
                std::vector<Point2Dd> pts;
                pts.reserve(10);
                for (int i = 0; i < 10; ++i) {
                    double r = (i % 2 == 0) ? half : half * 0.45;
                    double a = -M_PI / 2.0 + i * M_PI / 5.0;
                    pts.emplace_back(pos.x + r * std::cos(a), pos.y + r * std::sin(a));
                }
                ctx->SetFillPaint(fill);
                ctx->FillLinePath(pts);
                if (stroke.a > 0) {
                    ctx->SetStrokePaint(stroke);
                    ctx->SetStrokeWidth(1.0f);
                    ctx->DrawLinePath(pts, true);
                }
                break;
            }

            default:
                break;
        }
    }

    void UltraCanvasPolarChart::DrawRotatedText(IRenderContext* ctx,
                                                const std::string& text,
                                                const Point2Dd& anchor,
                                                double rotationRad) const {
        Size2Di ts = ctx->GetTextLineDimensions(text);
        ctx->PushState();
        ctx->Translate(anchor.x, anchor.y);
        ctx->Rotate(rotationRad);
        ctx->DrawText(text, Point2Dd(-ts.width / 2.0, -ts.height / 2.0));
        ctx->PopState();
    }

    std::vector<Point2Dd> UltraCanvasPolarChart::BuildSplinePath(
        const std::vector<Point2Dd>& pts, bool closed) const
    {
        if (pts.size() < 3) return pts;

        const int samplesPerSegment = 12;
        const double tension = splineTension;
        size_t count = pts.size();
        size_t segments = closed ? count : count - 1;

        std::vector<Point2Dd> out;
        out.reserve(segments * samplesPerSegment + 1);

        auto at = [&](long long index) -> const Point2Dd& {
            if (closed) {
                long long n = static_cast<long long>(count);
                index = ((index % n) + n) % n;
                return pts[static_cast<size_t>(index)];
            }
            index = std::max<long long>(0,
                    std::min<long long>(index, static_cast<long long>(count) - 1));
            return pts[static_cast<size_t>(index)];
        };

        for (size_t seg = 0; seg < segments; ++seg) {
            const Point2Dd& p0 = at(static_cast<long long>(seg) - 1);
            const Point2Dd& p1 = at(static_cast<long long>(seg));
            const Point2Dd& p2 = at(static_cast<long long>(seg) + 1);
            const Point2Dd& p3 = at(static_cast<long long>(seg) + 2);

            for (int s = 0; s < samplesPerSegment; ++s) {
                double t = static_cast<double>(s) / samplesPerSegment;
                double t2 = t * t;
                double t3 = t2 * t;

                // Catmull-Rom basis scaled by the tension factor.
                double m0x = tension * (p2.x - p0.x);
                double m0y = tension * (p2.y - p0.y);
                double m1x = tension * (p3.x - p1.x);
                double m1y = tension * (p3.y - p1.y);

                double h00 =  2.0 * t3 - 3.0 * t2 + 1.0;
                double h10 =        t3 - 2.0 * t2 + t;
                double h01 = -2.0 * t3 + 3.0 * t2;
                double h11 =        t3 -       t2;

                out.emplace_back(h00 * p1.x + h10 * m0x + h01 * p2.x + h11 * m1x,
                                 h00 * p1.y + h10 * m0y + h01 * p2.y + h11 * m1y);
            }
        }
        if (!closed) out.push_back(pts.back());
        return out;
    }

    // =========================================================================
    // RENDERING
    // =========================================================================

    void UltraCanvasPolarChart::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        if (!ctx) return;

        EnsureLayout();

        if (showBackground) {
            ctx->DrawFilledRectangle(GetLocalBounds(), backgroundColor);
        }

        RenderChart(ctx);
    }

    void UltraCanvasPolarChart::RenderChart(IRenderContext* ctx) {
        if (!ctx) return;
        EnsureLayout();

        RenderPlotBackground(ctx);
        RenderRadialBands(ctx);
        RenderAngularBands(ctx);
        if (showRadialGrid)  RenderGridRings(ctx);
        if (showAngularGrid) RenderGridSpokes(ctx);

        if (seriesList.empty()) {
            RenderTitles(ctx);
            RenderAngleAxis(ctx, angleAxis, angleTicks);
            if (showRadialLabels) RenderRadialAxis(ctx);
            return;
        }

        RenderSeries(ctx);

        RenderAngleAxis(ctx, angleAxis, angleTicks);
        if (secondaryAngleAxisEnabled) {
            RenderAngleAxis(ctx, secondaryAngleAxis, secondaryTicks);
        }
        if (showRadialLabels) RenderRadialAxis(ctx);

        RenderLegend(ctx);
        RenderTitles(ctx);
    }

    void UltraCanvasPolarChart::RenderPlotBackground(IRenderContext* ctx) {
        if (plotBackgroundColor.a > 0) {
            DrawAnnularSector(ctx,
                              AngleToScreenRadians(angleMin),
                              AngleToScreenRadians(angleMax),
                              cachedInnerRadius, cachedOuterRadius,
                              plotBackgroundColor, Colors::Transparent, 0.0f);
        }

        if (!alternatingBands || radialTicks.size() < 2) return;

        double a0 = AngleToScreenRadians(angleMin);
        double a1 = AngleToScreenRadians(angleMax);
        for (size_t i = 0; i + 1 < radialTicks.size(); ++i) {
            double r0 = RadiusToPixels(radialTicks[i]);
            double r1 = RadiusToPixels(radialTicks[i + 1]);
            const Color& color = (i % 2 == 0) ? alternatingColorA : alternatingColorB;
            DrawAnnularSector(ctx, a0, a1, r0, r1, color, Colors::Transparent, 0.0f);
        }
    }

    void UltraCanvasPolarChart::RenderRadialBands(IRenderContext* ctx) {
        if (radialBands.empty()) return;
        double a0 = AngleToScreenRadians(angleMin);
        double a1 = AngleToScreenRadians(angleMax);

        for (const PolarRadialBand& band : radialBands) {
            double lo = std::min(band.from, band.to);
            double hi = std::max(band.from, band.to);
            // Clip to the axis so bands never bleed outside the plot.
            lo = std::max(lo, resolvedRadialMin);
            hi = std::min(hi, resolvedRadialMax);
            if (hi <= lo) continue;
            DrawAnnularSector(ctx, a0, a1, RadiusToPixels(lo), RadiusToPixels(hi),
                              band.color, Colors::Transparent, 0.0f);
        }
    }

    void UltraCanvasPolarChart::RenderAngularBands(IRenderContext* ctx) {
        if (angularBands.empty()) return;

        for (const PolarAngularBand& band : angularBands) {
            double a0 = AngleToScreenRadians(std::min(band.fromAngle, band.toAngle));
            double a1 = AngleToScreenRadians(std::max(band.fromAngle, band.toAngle));
            if (std::fabs(a1 - a0) < 1e-6) continue;
            DrawAnnularSector(ctx, a0, a1, cachedInnerRadius, cachedOuterRadius,
                              band.color, Colors::Transparent, 0.0f);
        }
    }

    void UltraCanvasPolarChart::RenderGridRings(IRenderContext* ctx) {
        if (gridLineWidth <= 0.0f) return;
        bool polygonal = gridShape == PolarGridShape::Polygonal;

        ctx->PushState();
        ctx->SetStrokeWidth(gridLineWidth);

        for (size_t i = 0; i < radialTicks.size(); ++i) {
            double radius = RadiusToPixels(radialTicks[i]);
            if (radius <= 0.5) continue;
            ctx->SetStrokePaint(gridColor);
            DrawRingPath(ctx, radius, polygonal);

            // Minor rings sit between consecutive major ticks.
            if (minorRadialGridCount > 0 && i + 1 < radialTicks.size()) {
                double next = RadiusToPixels(radialTicks[i + 1]);
                ctx->SetStrokePaint(gridColor.WithAlpha(
                    static_cast<uint8_t>(gridColor.a * 0.45f)));
                for (int m = 1; m <= minorRadialGridCount; ++m) {
                    double t = static_cast<double>(m) / (minorRadialGridCount + 1);
                    DrawRingPath(ctx, radius + (next - radius) * t, polygonal);
                }
            }
        }
        ctx->PopState();
    }

    void UltraCanvasPolarChart::RenderGridSpokes(IRenderContext* ctx) {
        if (gridLineWidth <= 0.0f || gridSpokeRadians.empty()) return;

        ctx->PushState();
        ctx->SetStrokePaint(gridColor);
        ctx->SetStrokeWidth(gridLineWidth);
        for (double a : gridSpokeRadians) {
            Point2Dd from(cachedCenter.x + cachedInnerRadius * std::cos(a),
                          cachedCenter.y + cachedInnerRadius * std::sin(a));
            Point2Dd to(cachedCenter.x + cachedOuterRadius * std::cos(a),
                        cachedCenter.y + cachedOuterRadius * std::sin(a));
            ctx->DrawLine(from, to);
        }
        ctx->PopState();
    }

    void UltraCanvasPolarChart::RenderAngleAxis(IRenderContext* ctx,
                                                const PolarAngleAxisStyle& style,
                                                const std::vector<AngleTick>& ticks) {
        if (!style.visible) return;

        bool inside = style.placement == PolarLabelPlacement::Inside;
        double axisRadius = inside
                          ? std::max(cachedInnerRadius, cachedOuterRadius * 0.82)
                          : cachedOuterRadius;

        ctx->PushState();

        if (style.showAxisLine && style.axisLineColor.a > 0) {
            ctx->SetStrokePaint(style.axisLineColor);
            ctx->SetStrokeWidth(1.0f);
            DrawRingPath(ctx, axisRadius, false);
        }

        if (style.showTicks && style.tickLength > 0.0f && style.axisLineColor.a > 0) {
            ctx->SetStrokePaint(style.axisLineColor);
            ctx->SetStrokeWidth(1.0f);
            for (const AngleTick& tick : ticks) {
                double cosA = std::cos(tick.screenRad);
                double sinA = std::sin(tick.screenRad);
                double outer = axisRadius + (inside ? -style.tickLength : style.tickLength);
                ctx->DrawLine({cachedCenter.x + axisRadius * cosA,
                               cachedCenter.y + axisRadius * sinA},
                              {cachedCenter.x + outer * cosA,
                               cachedCenter.y + outer * sinA});
            }
        }

        if (style.placement == PolarLabelPlacement::Hidden) {
            ctx->PopState();
            return;
        }

        ctx->SetFontFamily(style.fontFamily);
        ctx->SetFontSize(style.fontSize);
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetTextPaint(style.labelColor);

        double labelRadius = axisRadius
                           + (inside ? -(style.labelOffset + style.tickLength)
                                     :  (style.labelOffset + style.tickLength));

        for (const AngleTick& tick : ticks) {
            if (tick.label.empty()) continue;
            double cosA = std::cos(tick.screenRad);
            double sinA = std::sin(tick.screenRad);
            Point2Dd anchor(cachedCenter.x + labelRadius * cosA,
                            cachedCenter.y + labelRadius * sinA);

            switch (style.orientation) {
                case PolarLabelOrientation::Tangential: {
                    double rotation = tick.screenRad + M_PI / 2.0;
                    // Keep text readable on the lower half of the circle.
                    if (std::cos(rotation) < 0.0) rotation += M_PI;
                    DrawRotatedText(ctx, tick.label, anchor, rotation);
                    break;
                }
                case PolarLabelOrientation::Radial: {
                    double rotation = tick.screenRad;
                    if (std::cos(rotation) < 0.0) rotation += M_PI;
                    DrawRotatedText(ctx, tick.label, anchor, rotation);
                    break;
                }
                case PolarLabelOrientation::Horizontal:
                default: {
                    Size2Di ts = ctx->GetTextLineDimensions(tick.label);
                    // Nudge the box outward so it clears the axis on every side.
                    Point2Dd pos(anchor.x + cosA * ts.width * 0.5 - ts.width / 2.0,
                                 anchor.y + sinA * ts.height * 0.5 - ts.height / 2.0);
                    ctx->DrawText(tick.label, pos);
                    break;
                }
            }
        }
        ctx->PopState();
    }

    void UltraCanvasPolarChart::RenderRadialAxis(IRenderContext* ctx) {
        if (radialTicks.empty()) return;

        double labelAngle = radialLabelAngleSet ? radialLabelAngle : angleMin;
        double screenRad = AngleToScreenRadians(labelAngle);
        double cosA = std::cos(screenRad);
        double sinA = std::sin(screenRad);

        ctx->PushState();

        if (radialAxisLineVisible) {
            ctx->SetStrokePaint(gridColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine({cachedCenter.x + cachedInnerRadius * cosA,
                           cachedCenter.y + cachedInnerRadius * sinA},
                          {cachedCenter.x + cachedOuterRadius * cosA,
                           cachedCenter.y + cachedOuterRadius * sinA});
        }

        ctx->SetFontFamily(labelFontFamily);
        ctx->SetFontSize(labelFontSize);
        ctx->SetFontWeight(FontWeight::Normal);

        for (double tick : radialTicks) {
            double radius = RadiusToPixels(tick);
            std::string text = FormatRadialLabel(tick);
            Size2Di ts = ctx->GetTextLineDimensions(text);

            Point2Dd anchor(cachedCenter.x + radius * cosA,
                            cachedCenter.y + radius * sinA);
            Rect2Dd chip(anchor.x - ts.width / 2.0 - 3.0,
                         anchor.y - ts.height / 2.0 - 1.0,
                         ts.width + 6.0, ts.height + 2.0);

            // A translucent chip keeps the tick readable over series fills.
            ctx->DrawFilledRectangle(chip, backgroundColor.WithAlpha(200));
            ctx->SetTextPaint(radialLabelColor);
            ctx->DrawText(text, Point2Dd(anchor.x - ts.width / 2.0,
                                         anchor.y - ts.height / 2.0));
        }
        ctx->PopState();
    }

    void UltraCanvasPolarChart::RenderSeries(IRenderContext* ctx) {
        RenderColumns(ctx);

        // Fills first so outlines and markers stay legible on top of them.
        std::vector<size_t> areaSeries;
        for (size_t si = 0; si < seriesList.size(); ++si) {
            const PolarSeries& series = seriesList[si];
            if (!series.visible) continue;
            if (series.type == PolarSeriesType::Area ||
                series.type == PolarSeriesType::SplineArea) {
                areaSeries.push_back(si);
            }
        }
        // Stacked areas grow outward with the series index, and each one is
        // filled all the way back to the base radius. Painting them outermost
        // first keeps the inner bands visible instead of buried.
        if (stackMode != PolarStackMode::NoStacking &&
            angleMode == PolarAngleMode::Categorical) {
            std::reverse(areaSeries.begin(), areaSeries.end());
        }
        for (size_t si : areaSeries) RenderLineSeries(ctx, si);
        for (size_t si = 0; si < seriesList.size(); ++si) {
            const PolarSeries& series = seriesList[si];
            if (!series.visible) continue;
            if (series.type == PolarSeriesType::Line ||
                series.type == PolarSeriesType::Spline) {
                RenderLineSeries(ctx, si);
            }
        }
        for (size_t si = 0; si < seriesList.size(); ++si) {
            if (!seriesList[si].visible) continue;
            if (seriesList[si].type == PolarSeriesType::Column) continue;
            RenderMarkers(ctx, si);
        }
        for (size_t si = 0; si < seriesList.size(); ++si) {
            if (!seriesList[si].visible) continue;
            if (seriesList[si].showValueLabels || showValueLabels) {
                RenderValueLabelsForSeries(ctx, si);
            }
        }
    }

    void UltraCanvasPolarChart::RenderColumns(IRenderContext* ctx) {
        for (const PlottedColumn& column : plottedColumns) {
            bool highlighted = hoverHighlightEnabled &&
                               column.seriesIndex == hoveredSeries &&
                               column.pointIndex == hoveredPoint;
            Color fill = highlighted ? column.color.Lighten(0.2f) : column.color;
            DrawAnnularSector(ctx, column.startRad, column.endRad,
                              column.innerPx, column.outerPx,
                              fill, columnBorderColor, columnBorderWidth);
        }
    }

    void UltraCanvasPolarChart::RenderLineSeries(IRenderContext* ctx, size_t seriesIndex) {
        const PolarSeries& series = seriesList[seriesIndex];

        std::vector<Point2Dd> pts;
        for (const PlottedPoint& plotted : plottedPoints) {
            if (plotted.seriesIndex == seriesIndex) pts.push_back(plotted.pos);
        }
        if (pts.size() < 2) return;

        bool smooth = series.type == PolarSeriesType::Spline ||
                      series.type == PolarSeriesType::SplineArea;
        bool filled = series.type == PolarSeriesType::Area ||
                      series.type == PolarSeriesType::SplineArea;
        bool closed = series.closed;

        std::vector<Point2Dd> path = smooth ? BuildSplinePath(pts, closed) : pts;

        ctx->PushState();

        if (filled) {
            std::vector<Point2Dd> fillPath = path;
            if (!closed) {
                // Drop to the base radius under the end points so the fill has
                // a well-defined inner boundary.
                double baseValue = (resolvedRadialMin <= 0.0 && resolvedRadialMax >= 0.0)
                                 ? 0.0 : resolvedRadialMin;
                double basePx = RadiusToPixels(baseValue);
                double lastAngle = std::atan2(path.back().y - cachedCenter.y,
                                              path.back().x - cachedCenter.x);
                double firstAngle = std::atan2(path.front().y - cachedCenter.y,
                                               path.front().x - cachedCenter.x);
                fillPath.emplace_back(cachedCenter.x + basePx * std::cos(lastAngle),
                                      cachedCenter.y + basePx * std::sin(lastAngle));
                fillPath.emplace_back(cachedCenter.x + basePx * std::cos(firstAngle),
                                      cachedCenter.y + basePx * std::sin(firstAngle));
            }
            ctx->SetFillPaint(ResolveFillColor(series, seriesIndex));
            ctx->FillLinePath(fillPath);
        }

        if (series.lineWidth > 0.0f) {
            Color stroke = ResolveSeriesColor(seriesIndex);
            ctx->SetStrokePaint(stroke);
            ctx->SetStrokeWidth(series.lineWidth);
            ctx->SetLineJoin(LineJoin::Round);
            ctx->SetLineCap(LineCap::Round);
            if (series.dashed) {
                ctx->SetLineDash(UCDashPattern({6.0, 4.0}));
            }
            ctx->DrawLinePath(path, closed);
            if (series.dashed) {
                ctx->SetLineDash(UCDashPattern::EMPTY);
            }
        }

        ctx->PopState();
    }

    void UltraCanvasPolarChart::RenderMarkers(IRenderContext* ctx, size_t seriesIndex) {
        const PolarSeries& series = seriesList[seriesIndex];
        if (series.marker == PolarMarkerShape::NoMarker) return;

        Color seriesColor = ResolveSeriesColor(seriesIndex);
        for (const PlottedPoint& plotted : plottedPoints) {
            if (plotted.seriesIndex != seriesIndex) continue;
            const PolarDataPoint& point = series.points[plotted.pointIndex];

            double size = point.markerSize > 0.0f ? point.markerSize : series.markerSize;
            Color fill = point.color.a > 0 ? point.color : seriesColor;

            bool highlighted = hoverHighlightEnabled &&
                               seriesIndex == hoveredSeries &&
                               plotted.pointIndex == hoveredPoint;
            if (highlighted) {
                size *= 1.6;
                fill = fill.Lighten(0.2f);
            }
            DrawMarker(ctx, series.marker, plotted.pos, size, fill,
                       Color(255, 255, 255, 200));
        }
    }

    void UltraCanvasPolarChart::RenderValueLabelsForSeries(IRenderContext* ctx,
                                                           size_t seriesIndex) {
        const PolarSeries& series = seriesList[seriesIndex];

        ctx->PushState();
        ctx->SetFontFamily(labelFontFamily);
        ctx->SetFontSize(std::max(7.0f, labelFontSize - 1.0f));
        ctx->SetTextPaint(valueLabelColor);

        auto drawLabel = [&](const Point2Dd& pos, double screenRad, double value) {
            std::string text = FormatRadialLabel(value);
            Size2Di ts = ctx->GetTextLineDimensions(text);
            double offset = 10.0;
            Point2Dd at(pos.x + std::cos(screenRad) * offset - ts.width / 2.0,
                        pos.y + std::sin(screenRad) * offset - ts.height / 2.0);
            ctx->DrawText(text, at);
        };

        if (series.type == PolarSeriesType::Column) {
            for (const PlottedColumn& column : plottedColumns) {
                if (column.seriesIndex != seriesIndex) continue;
                double mid = (column.startRad + column.endRad) * 0.5;
                Point2Dd pos(cachedCenter.x + column.outerPx * std::cos(mid),
                             cachedCenter.y + column.outerPx * std::sin(mid));
                drawLabel(pos, mid, series.points[column.pointIndex].radius);
            }
        } else {
            for (const PlottedPoint& plotted : plottedPoints) {
                if (plotted.seriesIndex != seriesIndex) continue;
                drawLabel(plotted.pos, plotted.screenRad,
                          series.points[plotted.pointIndex].radius);
            }
        }
        ctx->PopState();
    }

    void UltraCanvasPolarChart::BuildLegendEntries(IRenderContext* ctx) {
        legendEntries.clear();
        if (legendPosition == PolarLegendPosition::NoLegend || seriesList.empty()) return;

        ctx->PushState();
        ctx->SetFontFamily(legendFontFamily);
        ctx->SetFontSize(legendFontSize);

        const double swatch = legendFontSize;
        const double gap = 6.0;
        const double itemGap = 16.0;
        const double rowHeight = legendFontSize + 8.0;

        std::vector<std::pair<std::string, double>> measured;
        double totalWidth = 0.0;
        for (const auto& series : seriesList) {
            Size2Di ts = ctx->GetTextLineDimensions(series.name);
            double width = swatch + gap + ts.width;
            measured.emplace_back(series.name, width);
            totalWidth += width + itemGap;
        }
        if (totalWidth > 0.0) totalWidth -= itemGap;
        ctx->PopState();

        bool horizontal = legendPosition == PolarLegendPosition::Top ||
                          legendPosition == PolarLegendPosition::Bottom;

        double startX, startY;
        if (horizontal) {
            startX = (GetWidth() - totalWidth) / 2.0;
            startY = (legendPosition == PolarLegendPosition::Top)
                   ? (chartTitle.empty() ? 8.0 : 34.0) + (subtitle.empty() ? 0.0 : 18.0)
                   : GetHeight() - legendBandSize + 4.0;
        } else {
            startX = (legendPosition == PolarLegendPosition::Left)
                   ? 10.0 : GetWidth() - legendBandSize + 6.0;
            startY = cachedCenter.y
                   - static_cast<double>(seriesList.size()) * rowHeight / 2.0;
        }

        double x = startX;
        double y = startY;
        for (size_t i = 0; i < seriesList.size(); ++i) {
            LegendEntry entry;
            entry.seriesIndex = i;
            entry.color = ResolveSeriesColor(i);
            entry.text = measured[i].first;
            entry.bounds = Rect2Dd(x, y, measured[i].second, rowHeight);
            legendEntries.push_back(entry);

            if (horizontal) x += measured[i].second + itemGap;
            else            y += rowHeight;
        }
    }

    void UltraCanvasPolarChart::RenderLegend(IRenderContext* ctx) {
        if (legendPosition == PolarLegendPosition::NoLegend) return;
        BuildLegendEntries(ctx);
        if (legendEntries.empty()) return;

        ctx->PushState();
        ctx->SetFontFamily(legendFontFamily);
        ctx->SetFontSize(legendFontSize);

        const double swatch = legendFontSize;
        for (const LegendEntry& entry : legendEntries) {
            bool visible = seriesList[entry.seriesIndex].visible;
            Color swatchColor = visible ? entry.color
                                        : entry.color.WithAlpha(70);
            Color textColor = visible ? legendTextColor
                                      : legendTextColor.WithAlpha(110);

            double cy = entry.bounds.y + entry.bounds.height / 2.0;
            ctx->DrawFilledRectangle(
                Rect2Dd(entry.bounds.x, cy - swatch / 2.0, swatch, swatch),
                swatchColor, 0.0f, Colors::Transparent, 2.0f);

            Size2Di ts = ctx->GetTextLineDimensions(entry.text);
            ctx->SetTextPaint(textColor);
            ctx->DrawText(entry.text,
                          Point2Dd(entry.bounds.x + swatch + 6.0, cy - ts.height / 2.0));
        }
        ctx->PopState();
    }

    void UltraCanvasPolarChart::RenderTitles(IRenderContext* ctx) {
        if (chartTitle.empty() && subtitle.empty()) return;

        ctx->PushState();
        double y = 8.0;

        if (!chartTitle.empty()) {
            ctx->SetFontFamily(labelFontFamily);
            ctx->SetFontSize(labelFontSize + 4.0f);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(titleColor);
            Size2Di ts = ctx->GetTextLineDimensions(chartTitle);
            ctx->DrawText(chartTitle, Point2Dd((GetWidth() - ts.width) / 2.0, y));
            y += ts.height + 4.0;
        }

        if (!subtitle.empty()) {
            ctx->SetFontFamily(labelFontFamily);
            ctx->SetFontSize(labelFontSize);
            ctx->SetFontWeight(FontWeight::Normal);
            ctx->SetTextPaint(titleColor.WithAlpha(180));
            Size2Di ts = ctx->GetTextLineDimensions(subtitle);
            ctx->DrawText(subtitle, Point2Dd((GetWidth() - ts.width) / 2.0, y));
        }
        ctx->PopState();
    }

    // =========================================================================
    // HIT TESTING & EVENTS
    // =========================================================================

    bool UltraCanvasPolarChart::HitTestPoint(const Point2Dd& localPos,
                                             size_t& seriesIndex,
                                             size_t& pointIndex) const {
        // Columns win over markers: they cover a larger area and are drawn below.
        double dx = localPos.x - cachedCenter.x;
        double dy = localPos.y - cachedCenter.y;
        double r = std::sqrt(dx * dx + dy * dy);
        double angle = NormalizeRadians(std::atan2(dy, dx));

        for (const PlottedColumn& column : plottedColumns) {
            if (r < column.innerPx - 1.0 || r > column.outerPx + 1.0) continue;
            // Counter-clockwise charts produce endRad < startRad, so measure the
            // sector from its lower edge rather than from startRad.
            double low = std::min(column.startRad, column.endRad);
            double span = std::fabs(column.endRad - column.startRad);
            double delta = NormalizeRadians(angle - NormalizeRadians(low));
            if (delta <= span) {
                seriesIndex = column.seriesIndex;
                pointIndex = column.pointIndex;
                return true;
            }
        }

        const double threshold = 12.0;
        double bestDistance = threshold;
        bool found = false;
        for (const PlottedPoint& plotted : plottedPoints) {
            double pdx = localPos.x - plotted.pos.x;
            double pdy = localPos.y - plotted.pos.y;
            double distance = std::sqrt(pdx * pdx + pdy * pdy);
            if (distance < bestDistance) {
                bestDistance = distance;
                seriesIndex = plotted.seriesIndex;
                pointIndex = plotted.pointIndex;
                found = true;
            }
        }
        return found;
    }

    size_t UltraCanvasPolarChart::HitTestLegend(const Point2Dd& localPos) const {
        for (const LegendEntry& entry : legendEntries) {
            if (localPos.x >= entry.bounds.x &&
                localPos.x <= entry.bounds.x + entry.bounds.width &&
                localPos.y >= entry.bounds.y &&
                localPos.y <= entry.bounds.y + entry.bounds.height) {
                return entry.seriesIndex;
            }
        }
        return SIZE_MAX;
    }

    bool UltraCanvasPolarChart::HandleChartMouseMove(const Point2Di& mousePos) {
        if (plottedPoints.empty() && plottedColumns.empty()) return false;

        Point2Dd local(static_cast<double>(mousePos.x), static_cast<double>(mousePos.y));
        size_t si = SIZE_MAX, pi = SIZE_MAX;
        bool hit = HitTestPoint(local, si, pi);

        if (si != hoveredSeries || pi != hoveredPoint) {
            hoveredSeries = hit ? si : SIZE_MAX;
            hoveredPoint = hit ? pi : SIZE_MAX;
            if (hit && onPointHover) onPointHover(si, pi);
            RequestRedraw();
        }

        if (hit) {
            if (enableTooltips) {
                std::string tooltip = BuildTooltipText(si, pi);
                if (!tooltip.empty()) {
                    auto windowMousePos = MapFromLocal(mousePos, nullptr);
                    UltraCanvasTooltipManager::UpdateAndShowTooltip(
                        this->window, tooltip, windowMousePos);
                    isTooltipActive = true;
                }
            }
            return true;
        }
        if (isTooltipActive) HideTooltip();
        return false;
    }

    bool UltraCanvasPolarChart::HandleClick(const UCEvent& event) {
        Point2Dd local(static_cast<double>(event.pointer.x),
                       static_cast<double>(event.pointer.y));

        if (legendToggleEnabled) {
            size_t legendHit = HitTestLegend(local);
            if (legendHit != SIZE_MAX) {
                SetSeriesVisible(legendHit, !seriesList[legendHit].visible);
                return true;
            }
        }

        size_t si = SIZE_MAX, pi = SIZE_MAX;
        if (HitTestPoint(local, si, pi)) {
            if (onPointClick) onPointClick(si, pi);
            return true;
        }
        return false;
    }

    bool UltraCanvasPolarChart::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left) {
                    if (HandleClick(event)) return true;
                    if (rotateOnDrag) {
                        isRotating = true;
                        rotateAnchor = Point2Di(event.pointer.x, event.pointer.y);
                        rotateAnchorAngle = zeroAngleDeg;
                        return true;
                    }
                }
                break;

            case UCEventType::MouseMove:
                if (isRotating) {
                    // One pixel of horizontal travel rotates the chart by 0.5°.
                    float delta = static_cast<float>(event.pointer.x - rotateAnchor.x) * 0.5f;
                    zeroAngleDeg = rotateAnchorAngle + delta;
                    InvalidateLayout();
                    RequestRedraw();
                    return true;
                }
                break;

            case UCEventType::MouseUp:
                if (isRotating) {
                    isRotating = false;
                    return true;
                }
                break;

            case UCEventType::MouseLeave:
                isRotating = false;
                if (hoveredPoint != SIZE_MAX || hoveredSeries != SIZE_MAX) {
                    hoveredSeries = SIZE_MAX;
                    hoveredPoint = SIZE_MAX;
                    RequestRedraw();
                }
                HideTooltip();
                return true;

            default:
                break;
        }
        return UltraCanvasChartElementBase::OnEvent(event);
    }

    // =========================================================================
    // FACTORY FUNCTIONS
    // =========================================================================

    std::shared_ptr<UltraCanvasPolarChart> CreatePolarChart(
        const std::string& id, int x, int y, int w, int h)
    {
        return std::make_shared<UltraCanvasPolarChart>(id, x, y, w, h);
    }

    std::shared_ptr<UltraCanvasPolarChart> CreatePolarScatterChart(
        const std::string& id, int x, int y, int w, int h,
        const std::vector<std::pair<std::string, std::vector<std::pair<double, double>>>>& seriesData)
    {
        auto chart = std::make_shared<UltraCanvasPolarChart>(id, x, y, w, h);
        chart->SetAngleMode(PolarAngleMode::Numeric);
        for (const auto& entry : seriesData) {
            chart->AddSeries(entry.first, PolarSeriesType::Scatter, entry.second);
        }
        return chart;
    }

    std::shared_ptr<UltraCanvasPolarChart> CreatePolarRoseChart(
        const std::string& id, int x, int y, int w, int h,
        const std::vector<std::string>& categories,
        const std::vector<std::pair<std::string, std::vector<double>>>& seriesData,
        PolarStackMode stackMode)
    {
        auto chart = std::make_shared<UltraCanvasPolarChart>(id, x, y, w, h);
        chart->SetCategories(categories);
        chart->SetCategoryPlacement(PolarCategoryPlacement::BetweenSpokes);
        chart->SetColumnWidthFraction(1.0f);
        chart->SetStackMode(stackMode);
        for (const auto& entry : seriesData) {
            chart->AddCategorySeries(entry.first, PolarSeriesType::Column, entry.second);
        }
        return chart;
    }

} // namespace UltraCanvas

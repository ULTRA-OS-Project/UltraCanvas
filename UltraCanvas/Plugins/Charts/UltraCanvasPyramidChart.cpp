// Plugins/Charts/UltraCanvasPyramidChart.cpp
// Pyramid diagram implementation
// Version: 1.1.0
// Last Modified: 2026-08-20
// V1.1.0: legend: migrated to the shared ChartLegend component
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasPyramidChart.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

namespace {

    constexpr double kColumnGap = 10.0;
    constexpr double kEdgePadding = 12.0;
    constexpr double kConnectorRoom = 26.0;
    constexpr double kInsidePadding = 8.0;
    constexpr double kMinBandThickness = 6.0;
    constexpr double kNotchSize = 7.0;

    double Clamp(double value, double low, double high) {
        return std::max(low, std::min(high, value));
    }

    Color LerpColor(const Color& from, const Color& to, double t) {
        t = Clamp(t, 0.0, 1.0);
        auto mix = [t](uint8_t a, uint8_t b) {
            return static_cast<uint8_t>(std::lround(a + (static_cast<double>(b) - a) * t));
        };
        return Color(mix(from.r, to.r), mix(from.g, to.g), mix(from.b, to.b), mix(from.a, to.a));
    }

    Color Lighten(const Color& color, double amount) {
        return LerpColor(color, Color(255, 255, 255, color.a), amount);
    }

    Color Darken(const Color& color, double amount) {
        return LerpColor(color, Color(0, 0, 0, color.a), amount);
    }

    Color WithAlpha(const Color& color, double factor) {
        return Color(color.r, color.g, color.b,
                     static_cast<uint8_t>(Clamp(color.a * factor, 0.0, 255.0)));
    }

    // Perceived brightness, used to decide between white and dark text on a tier
    double Luminance(const Color& color) {
        return (0.299 * color.r + 0.587 * color.g + 0.114 * color.b) / 255.0;
    }

    // The tooltip manager renders its text as Pango markup, so anything coming from
    // the data has to be XML-escaped or a stray '&' silently produces an empty tooltip.
    std::string EscapeMarkup(const std::string& text) {
        std::string escaped;
        escaped.reserve(text.size());
        for (char c : text) {
            switch (c) {
                case '&': escaped += "&amp;"; break;
                case '<': escaped += "&lt;"; break;
                case '>': escaped += "&gt;"; break;
                default:  escaped += c; break;
            }
        }
        return escaped;
    }

    const std::vector<Color>& PresetPalette(PyramidPalettePreset preset) {
        // Apex first: the reader scans a pyramid from its point downwards
        static const std::vector<Color> spectrum = {
            Color(228, 68, 55, 255),  Color(240, 142, 40, 255), Color(243, 200, 46, 255),
            Color(120, 190, 78, 255), Color(52, 176, 186, 255), Color(58, 118, 194, 255),
            Color(120, 88, 180, 255), Color(198, 74, 150, 255)
        };
        static const std::vector<Color> warm = {
            Color(150, 40, 60, 255),  Color(198, 62, 58, 255),  Color(226, 106, 54, 255),
            Color(238, 152, 62, 255), Color(246, 196, 96, 255), Color(250, 224, 150, 255)
        };
        static const std::vector<Color> cool = {
            Color(24, 62, 104, 255),  Color(30, 100, 150, 255), Color(42, 142, 178, 255),
            Color(72, 178, 190, 255), Color(126, 206, 202, 255),Color(184, 228, 224, 255)
        };
        static const std::vector<Color> corporate = {
            Color(38, 60, 92, 255),   Color(58, 96, 140, 255),  Color(84, 134, 178, 255),
            Color(126, 170, 202, 255),Color(172, 202, 224, 255),Color(214, 230, 242, 255)
        };
        static const std::vector<Color> grayscale = {
            Color(58, 60, 64, 255),   Color(92, 95, 100, 255),  Color(126, 130, 136, 255),
            Color(160, 164, 170, 255),Color(194, 198, 204, 255),Color(224, 227, 232, 255)
        };

        switch (preset) {
            case PyramidPalettePreset::WarmPalette:      return warm;
            case PyramidPalettePreset::CoolPalette:      return cool;
            case PyramidPalettePreset::CorporatePalette: return corporate;
            case PyramidPalettePreset::GrayscalePalette: return grayscale;
            case PyramidPalettePreset::SpectrumPalette:
            default:                                     return spectrum;
        }
    }

    Point2Dd Centroid(const std::vector<Point2Dd>& points) {
        if (points.empty()) return Point2Dd(0.0, 0.0);
        double x = 0.0, y = 0.0;
        for (const auto& point : points) { x += point.x; y += point.y; }
        return Point2Dd(x / points.size(), y / points.size());
    }

} // anonymous namespace

// =============================================================================
// DATA SOURCE
// =============================================================================

    void PyramidDataSource::LoadFromCSV(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) return;

        levels.clear();

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;

            std::vector<std::string> fields;
            std::stringstream ss(line);
            std::string field;
            while (std::getline(ss, field, ',')) {
                // Trim surrounding whitespace and quotes
                size_t begin = field.find_first_not_of(" \t\r\n\"");
                size_t end = field.find_last_not_of(" \t\r\n\"");
                fields.push_back(begin == std::string::npos ? std::string()
                                                           : field.substr(begin, end - begin + 1));
            }

            if (fields.empty() || fields[0].empty()) continue;

            // A pyramid is often a hierarchy with no numbers at all, so a row that is
            // nothing but a label is valid. A second column that does not parse is
            // treated as a description, which also swallows a header line.
            PyramidLevel level(fields[0], 0.0);
            size_t descriptionField = 1;
            if (fields.size() > 1) {
                try {
                    size_t consumed = 0;
                    double value = std::stod(fields[1], &consumed);
                    if (consumed == fields[1].size()) {
                        level.levelValue = value;
                        level.y = value;
                        level.value = value;
                        descriptionField = 2;
                    }
                } catch (const std::exception&) {
                    // Leave the value at zero and read the column as text
                }
            }
            if (fields.size() > descriptionField) level.description = fields[descriptionField];
            levels.push_back(level);
        }
    }

// =============================================================================
// CONSTRUCTION
// =============================================================================

    UltraCanvasPyramidChart::UltraCanvasPyramidChart(const std::string& id, int x, int y,
                                                     int width, int height)
            : UltraCanvasChartElementBase(id, x, y, width, height) {
        pyramidDataSource = CreatePyramidDataSource();
        dataSource = pyramidDataSource;   // Base class renders empty state from this

        enableTooltips = true;
        // A pyramid has no axes and no grid to draw against
        showGrid = false;
        showAxes = false;
        showValueLabels = false;
        // Animation is opt-in: it drives repaints from inside Render()
        animationEnabled = false;
        animationComplete = true;
        animationDuration = 0.7f;
        palette = PresetPalette(PyramidPalettePreset::SpectrumPalette);

        // Legend defaults match the retired private implementation: hidden,
        // bottom centre, 10px text with font-sized borderless swatches, level
        // label colour for the entry text.
        legend.SetVisible(false);
        legend.SetPosition(ChartLegendPosition::BottomCenter);
        ChartLegendStyle legendStyle;
        legendStyle.fontSize = 10.0f;
        legendStyle.swatchWidth = 10.0f;
        legendStyle.swatchHeight = 10.0f;
        legendStyle.drawSwatchBorder = false;
        legendStyle.textColor = levelLabelColor;
        legend.SetStyle(legendStyle);
    }

// =============================================================================
// DATA
// =============================================================================

    void UltraCanvasPyramidChart::SetPyramidDataSource(std::shared_ptr<PyramidDataSource> data) {
        pyramidDataSource = data ? data : CreatePyramidDataSource();
        dataSource = pyramidDataSource;
        orderValid = false;
        hoveredLevelIndex = -1;
        selectedLevelIndex = -1;
        InvalidateCache();
        if (animationEnabled) StartAnimation();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::AddLevel(const std::string& label, double value,
                                           const std::string& description) {
        if (!pyramidDataSource) return;
        pyramidDataSource->AddLevel(label, value, description);
        orderValid = false;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::AddLevel(const PyramidLevel& level) {
        if (!pyramidDataSource) return;
        pyramidDataSource->AddLevel(level);
        orderValid = false;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::ClearData() {
        if (!pyramidDataSource) return;
        pyramidDataSource->Clear();
        displayOrder.clear();
        geometry.clear();
        apexCapPolygon.clear();
        orderValid = true;
        hoveredLevelIndex = -1;
        selectedLevelIndex = -1;
        InvalidateCache();
        RequestRedraw();
    }

    PyramidLevelMetrics UltraCanvasPyramidChart::GetLevelMetrics(size_t dataIndex) const {
        // Prefer the drawn metrics: sorting and the value policy can make them differ
        // from the raw source order.
        for (const auto& geo : geometry) {
            if (geo.dataIndex == dataIndex) return geo.metrics;
        }

        PyramidLevelMetrics metrics;
        if (!pyramidDataSource || dataIndex >= pyramidDataSource->GetPointCount()) return metrics;

        metrics.value = pyramidDataSource->GetLevel(dataIndex).levelValue;
        double total = pyramidDataSource->GetTotalValue();
        if (total != 0.0) metrics.percentOfTotal = metrics.value / total * 100.0;
        return metrics;
    }

    double UltraCanvasPyramidChart::GetBaseToApexRatio() const {
        if (geometry.size() < 2) return 0.0;
        double apex = geometry.front().metrics.value;
        double base = geometry.back().metrics.value;
        if (apex == 0.0) return 0.0;
        return base / apex;
    }

    size_t UltraCanvasPyramidChart::GetTopHeavyLevel() const {
        // Walking apex to base, the first tier that is wider than the one holding it up
        for (size_t i = 0; i + 1 < geometry.size(); ++i) {
            if (geometry[i].metrics.value > geometry[i + 1].metrics.value) {
                return geometry[i].dataIndex;
            }
        }
        return SIZE_MAX;
    }

    void UltraCanvasPyramidChart::SetInputOrder(PyramidInputOrder order) {
        if (inputOrder == order) return;
        inputOrder = order;
        orderValid = false;
        hoveredLevelIndex = -1;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetSortOrder(PyramidSortOrder order) {
        if (sortOrder == order) return;
        sortOrder = order;
        orderValid = false;
        hoveredLevelIndex = -1;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetValuePolicy(PyramidValuePolicy policy) {
        if (valuePolicy == policy) return;
        valuePolicy = policy;
        orderValid = false;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::RebuildDisplayOrder() {
        displayOrder.clear();
        if (!pyramidDataSource) {
            orderValid = true;
            return;
        }

        size_t count = pyramidDataSource->GetPointCount();
        displayOrder.reserve(count);

        // The chart always draws apex first, whichever end the caller supplied first
        if (inputOrder == PyramidInputOrder::BaseFirst) {
            for (size_t i = count; i > 0; --i) displayOrder.push_back(i - 1);
        } else {
            for (size_t i = 0; i < count; ++i) displayOrder.push_back(i);
        }

        const auto& levels = pyramidDataSource->GetLevels();

        PyramidSortOrder effective = sortOrder;
        // The policy wins over InsertionOrder but never overrides an explicit sort.
        // Ascending along the drawn order puts the widest tier at the base, which is
        // what "always widens downwards" means.
        if (valuePolicy == PyramidValuePolicy::AutoSortToWiden &&
            sortOrder == PyramidSortOrder::InsertionOrder) {
            effective = PyramidSortOrder::ValueAscending;
        }

        if (effective != PyramidSortOrder::InsertionOrder) {
            // Every order below is read along the drawn sequence, apex to base
            std::stable_sort(displayOrder.begin(), displayOrder.end(),
                             [&levels, effective](size_t a, size_t b) {
                switch (effective) {
                    case PyramidSortOrder::ValueDescending: return levels[b].levelValue < levels[a].levelValue;
                    case PyramidSortOrder::ValueAscending:  return levels[a].levelValue < levels[b].levelValue;
                    case PyramidSortOrder::LabelAscending:  return levels[a].levelLabel < levels[b].levelLabel;
                    case PyramidSortOrder::LabelDescending: return levels[b].levelLabel < levels[a].levelLabel;
                    default:                                return a < b;
                }
            });
        }

        orderValid = true;
    }

// =============================================================================
// SHAPE CONFIGURATION
// =============================================================================

    void UltraCanvasPyramidChart::SetShapeMode(PyramidShapeMode mode) {
        shapeMode = mode;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetDirection(PyramidDirection value) {
        direction = value;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetScaleMode(PyramidScaleMode mode) {
        scaleMode = mode;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetApexMode(PyramidApexMode mode) {
        apexMode = mode;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetApexCap(PyramidApexCap cap, double capHeight) {
        apexCap = cap;
        apexCapHeight = Clamp(capHeight, 8.0, 200.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetAlignment(PyramidAlignment value) {
        alignment = value;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetApexRatio(double ratio) {
        apexRatio = Clamp(ratio, 0.0, 0.9);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetMinLevelExtent(double extent) {
        minLevelExtent = Clamp(extent, 0.0, 400.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetLevelGap(double gap) {
        levelGap = Clamp(gap, 0.0, 200.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetLevelThickness(double thickness) {
        levelThickness = Clamp(thickness, 6.0, 400.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetAutoFitLevels(bool autoFit) {
        autoFitLevels = autoFit;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetCornerRadius(double radius) {
        cornerRadius = Clamp(radius, 0.0, 60.0);
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetSlabSkew(double skew) {
        slabSkew = Clamp(skew, -120.0, 120.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetExplodeOffset(double offset) {
        explodeOffset = Clamp(offset, 0.0, 200.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetHalfPyramid(bool half) {
        halfPyramid = half;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetPyramidWidthRatio(double ratio) {
        pyramidWidthRatio = Clamp(ratio, 0.1, 1.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetShowBasePlinth(bool show, const Color& color) {
        showBasePlinth = show;
        basePlinthColor = color;
        InvalidateCache();
        RequestRedraw();
    }

    double UltraCanvasPyramidChart::GetPreferredThickness() const {
        size_t count = GetLevelCount();
        if (count == 0) return GetMarginFlowStart() + GetMarginFlowEnd() + 40.0;
        double gap = (shapeMode == PyramidShapeMode::SolidPyramid) ? 0.0 : levelGap;
        double cap = (apexCap == PyramidApexCap::ApexSeparateCap) ? apexCapHeight + levelGap : 0.0;
        return GetMarginFlowStart() + GetMarginFlowEnd() + cap +
               count * levelThickness + (count - 1) * gap;
    }

// =============================================================================
// 3D CONFIGURATION
// =============================================================================

    void UltraCanvasPyramidChart::SetDepth3D(double depth) {
        depth3D = Clamp(depth, 0.0, 160.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetAngle3D(double degrees) {
        angle3D = Clamp(degrees, 5.0, 80.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetFaceShading(double topLighten, double sideDarken) {
        topFaceLighten = Clamp(topLighten, 0.0, 1.0);
        sideFaceDarken = Clamp(sideDarken, 0.0, 1.0);
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetShowTopFace(bool show) {
        showTopFace = show;
        RequestRedraw();
    }

// =============================================================================
// COLOUR CONFIGURATION
// =============================================================================

    void UltraCanvasPyramidChart::SetFillStyle(PyramidFillStyle style) {
        fillStyle = style;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetColorMode(PyramidColorMode mode) {
        colorMode = mode;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetColorRamp(const Color& apexColor, const Color& baseColorValue) {
        rampStartColor = apexColor;
        rampEndColor = baseColorValue;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetBaseColor(const Color& color) {
        baseColor = color;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetPalette(const std::vector<Color>& colors) {
        palette = colors.empty() ? PresetPalette(PyramidPalettePreset::SpectrumPalette) : colors;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetPalettePreset(PyramidPalettePreset preset) {
        palette = PresetPalette(preset);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetGradientLightening(double amount) {
        gradientLightening = Clamp(amount, 0.0, 1.0);
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetTintStrength(double amount) {
        tintStrength = Clamp(amount, 0.0, 1.0);
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetShowLevelBorder(bool show, const Color& color, double width) {
        showLevelBorder = show;
        levelBorderColor = color;
        levelBorderWidth = Clamp(width, 0.0, 12.0);
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetOutlineStyle(double width, bool dashed) {
        outlineWidth = Clamp(width, 0.5, 12.0);
        dashedOutline = dashed;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetDimUnhoveredLevels(bool dim) {
        dimUnhoveredLevels = dim;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetShareThresholds(double majorPercent, double minorPercent) {
        majorShareThreshold = majorPercent;
        minorShareThreshold = minorPercent;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetShareColors(const Color& major, const Color& middle,
                                                 const Color& minor) {
        majorShareColor = major;
        middleShareColor = middle;
        minorShareColor = minor;
        InvalidateCache();
        RequestRedraw();
    }

// =============================================================================
// LABEL CONFIGURATION
// =============================================================================

    void UltraCanvasPyramidChart::SetLevelLabelPlacement(PyramidLevelLabelPlacement placement) {
        levelLabelPlacement = placement;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetPyramidValueLabelPlacement(PyramidValueLabelPlacement placement) {
        pyramidValueLabelPlacement = placement;
        showValueLabels = (placement != PyramidValueLabelPlacement::HideValueLabels);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetPercentPlacement(PyramidPercentPlacement placement) {
        percentPlacement = placement;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetInsideTextAlign(PyramidInsideTextAlign align) {
        insideTextAlign = align;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetLevelLabelColumnWidth(double width) {
        levelLabelColumnWidth = Clamp(width, 0.0, 600.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetPercentColumnWidth(double width) {
        percentColumnWidth = Clamp(width, 0.0, 400.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetLevelLabelFontSize(double size) {
        levelLabelFontSize = Clamp(size, 6.0, 40.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetLevelLabelColor(const Color& color) {
        levelLabelColor = color;
        // The legacy legend drew its entry text in the level label colour
        legend.GetStyle().textColor = color;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetTitleColor(const Color& color) {
        titleColor = color;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetSubtitle(const std::string& text) {
        subtitle = text;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetSubtitleStyle(double fontSize, const Color& color) {
        subtitleFontSize = Clamp(fontSize, 6.0, 40.0);
        subtitleColor = color;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetShowLevelSwatch(bool show) {
        showLevelSwatch = show;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetShowLevelListRules(bool show) {
        showLevelListRules = show;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetValueFormat(const std::string& format) {
        valueFormat = format;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetPercentFormat(const std::string& format) {
        percentFormat = format;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetAbbreviateValues(bool abbreviate) {
        abbreviateValues = abbreviate;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetInsideLabelColor(const Color& color) {
        insideLabelColor = color;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetAutoContrastInsideLabels(bool autoContrast) {
        autoContrastInsideLabels = autoContrast;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetWrapInsideText(bool wrap) {
        wrapInsideText = wrap;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetShrinkInsideText(bool shrink, double minimumFontSize) {
        shrinkInsideText = shrink;
        minInsideFontSize = Clamp(minimumFontSize, 4.0, 30.0);
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetShowDescriptionInside(bool show) {
        showDescriptionInside = show;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetShowPercentOfTotal(bool show) {
        showPercentOfTotal = show;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetShowPercentOfBase(bool show) {
        showPercentOfBase = show;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetShowRatioToLevelBelow(bool show) {
        showRatioToLevelBelow = show;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetShowCumulativeFromBase(bool show) {
        showCumulativeFromBase = show;
        InvalidateCache();
        RequestRedraw();
    }

// =============================================================================
// BADGE CONFIGURATION
// =============================================================================

    void UltraCanvasPyramidChart::SetBadgeShape(PyramidBadgeShape shape) {
        badgeShape = shape;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetBadgePlacement(PyramidBadgePlacement placement) {
        badgePlacement = placement;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetBadgeStyle(double radius, double fontSize) {
        badgeRadius = Clamp(radius, 5.0, 60.0);
        badgeFontSize = Clamp(fontSize, 5.0, 40.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetBadgeColors(const Color& fill, const Color& text) {
        badgeColor = fill;
        badgeTextColor = text;
        RequestRedraw();
    }

// =============================================================================
// CALLOUT CONFIGURATION
// =============================================================================

    void UltraCanvasPyramidChart::SetCalloutStyle(PyramidCalloutStyle style) {
        calloutStyle = style;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetCalloutSide(PyramidCalloutSide side) {
        calloutSide = side;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetCalloutLayout(PyramidCalloutLayout layout) {
        calloutLayout = layout;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetConnectorStyle(PyramidConnectorStyle style) {
        connectorStyle = style;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetCalloutColumnWidth(double width) {
        calloutColumnWidth = Clamp(width, 40.0, 900.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetCalloutSpacing(double spacing) {
        calloutSpacing = Clamp(spacing, 0.0, 80.0);
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetCalloutPadding(double padding) {
        calloutPadding = Clamp(padding, 0.0, 60.0);
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetCalloutCornerRadius(double radius) {
        calloutCornerRadius = Clamp(radius, 0.0, 40.0);
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetCalloutFontSizes(double titleSize, double bodySize) {
        calloutTitleFontSize = Clamp(titleSize, 5.0, 40.0);
        calloutBodyFontSize = Clamp(bodySize, 5.0, 40.0);
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetCalloutColors(const Color& title, const Color& body) {
        calloutTitleColor = title;
        calloutBodyColor = body;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetCalloutCardStyle(const Color& fill, const Color& border) {
        calloutCardColor = fill;
        calloutBorderColor = border;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetConnectorAppearance(const Color& color, double width) {
        connectorColor = color;
        connectorWidth = Clamp(width, 0.25, 10.0);
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetGhostNumberStyle(double fontSize, double alpha) {
        ghostNumberFontSize = Clamp(fontSize, 8.0, 120.0);
        ghostNumberAlpha = Clamp(alpha, 0.0, 1.0);
        RequestRedraw();
    }

// =============================================================================
// OVERLAY, LEGEND, SELECTION AND ANIMATION CONFIGURATION
// =============================================================================

    void UltraCanvasPyramidChart::SetShowTargetOverlay(bool show) {
        showTargetOverlay = show;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetTargetOverlayColor(const Color& color) {
        targetOverlayColor = color;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetReferenceShape(const std::vector<double>& shares,
                                                    const Color& color) {
        referenceShape = shares;
        referenceShapeColor = color;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::ClearReferenceShape() {
        referenceShape.clear();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetHighlightTopHeavy(bool highlight) {
        highlightTopHeavy = highlight;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetTopHeavyColor(const Color& color) {
        topHeavyColor = color;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetBenchmarkValue(double value, const std::string& label) {
        benchmarkEnabled = true;
        benchmarkValue = value;
        benchmarkLabel = label;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::ClearBenchmark() {
        benchmarkEnabled = false;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetBenchmarkColor(const Color& color) {
        benchmarkColor = color;
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetShowLegend(bool show) {
        legend.SetVisible(show);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetLegendPosition(PyramidLegendPosition position) {
        // Closest ChartLegendPosition match: Left/Center/Right map onto
        // Start/Center/End along the same edge.
        switch (position) {
            case PyramidLegendPosition::LegendTopLeft:
                legend.SetPosition(ChartLegendPosition::TopStart); break;
            case PyramidLegendPosition::LegendTopCenter:
                legend.SetPosition(ChartLegendPosition::TopCenter); break;
            case PyramidLegendPosition::LegendTopRight:
                legend.SetPosition(ChartLegendPosition::TopEnd); break;
            case PyramidLegendPosition::LegendBottomLeft:
                legend.SetPosition(ChartLegendPosition::BottomStart); break;
            case PyramidLegendPosition::LegendBottomRight:
                legend.SetPosition(ChartLegendPosition::BottomEnd); break;
            case PyramidLegendPosition::LegendBottomCenter:
            default:
                legend.SetPosition(ChartLegendPosition::BottomCenter); break;
        }
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetLegendFontSize(double size) {
        double clamped = Clamp(size, 6.0, 24.0);
        ChartLegendStyle& style = legend.GetStyle();
        style.fontSize = static_cast<float>(clamped);
        // The legacy legend sized its swatches to the font
        style.swatchWidth = static_cast<float>(clamped);
        style.swatchHeight = static_cast<float>(clamped);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetCardStyle(const Color& fill, const Color& border,
                                               double radius) {
        cardFillColor = fill;
        cardBorderColor = border;
        cardCornerRadius = Clamp(radius, 0.0, 60.0);
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetSelectedLevel(size_t dataIndex) {
        selectedLevelIndex = -1;
        for (size_t i = 0; i < geometry.size(); ++i) {
            if (geometry[i].dataIndex == dataIndex) {
                selectedLevelIndex = static_cast<int>(i);
                break;
            }
        }
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::ClearSelection() {
        selectedLevelIndex = -1;
        RequestRedraw();
    }

    size_t UltraCanvasPyramidChart::GetSelectedLevel() const {
        if (selectedLevelIndex < 0 || static_cast<size_t>(selectedLevelIndex) >= geometry.size()) {
            return SIZE_MAX;
        }
        return geometry[selectedLevelIndex].dataIndex;
    }

    void UltraCanvasPyramidChart::SetAnimationEnabled(bool enable) {
        animationEnabled = enable;
        if (enable) {
            StartAnimation();
        } else {
            animationComplete = true;
        }
        RequestRedraw();
    }

    void UltraCanvasPyramidChart::SetAnimationDuration(float seconds) {
        animationDuration = std::max(0.05f, seconds);
    }

    void UltraCanvasPyramidChart::RestartAnimation() {
        if (!animationEnabled) return;
        StartAnimation();
        RequestRedraw();
    }

// =============================================================================
// LAYOUT
// =============================================================================

// Asked while the margins are being reserved, which happens before the geometry
// exists, so this reads the data source rather than the cached flag.
    bool UltraCanvasPyramidChart::HasAnyPercentChannel() const {
        if (!pyramidDataSource || pyramidDataSource->IsValueless()) return false;
        return showPercentOfTotal || showPercentOfBase ||
               showRatioToLevelBelow || showCumulativeFromBase;
    }

    bool UltraCanvasPyramidChart::LabelTakesStartColumn() const {
        return levelLabelPlacement == PyramidLevelLabelPlacement::OutsideStartLabels ||
               (showValueLabels &&
                pyramidValueLabelPlacement == PyramidValueLabelPlacement::OutsideStartValues);
    }

    bool UltraCanvasPyramidChart::LabelTakesEndColumn() const {
        return levelLabelPlacement == PyramidLevelLabelPlacement::OutsideEndLabels ||
               levelLabelPlacement == PyramidLevelLabelPlacement::LeaderLineLabels ||
               (showValueLabels &&
                pyramidValueLabelPlacement == PyramidValueLabelPlacement::OutsideEndValues);
    }

    bool UltraCanvasPyramidChart::PercentTakesStartColumn() const {
        return percentPlacement == PyramidPercentPlacement::OutsideStartPercent &&
               HasAnyPercentChannel();
    }

    bool UltraCanvasPyramidChart::PercentTakesEndColumn() const {
        return percentPlacement == PyramidPercentPlacement::OutsideEndPercent &&
               HasAnyPercentChannel();
    }

// Callout columns only exist on a vertical pyramid: laid out sideways they would
// fight the tiers for the same axis. A horizontal pyramid falls back to the
// level label column, which reads the same way.
    bool UltraCanvasPyramidChart::CalloutsTakeStartColumn() const {
        if (calloutStyle == PyramidCalloutStyle::NoCallouts || !IsVerticalPyramid()) return false;
        return calloutSide == PyramidCalloutSide::CalloutsLeft ||
               calloutSide == PyramidCalloutSide::CalloutsAlternating ||
               calloutSide == PyramidCalloutSide::CalloutsBothSides;
    }

    bool UltraCanvasPyramidChart::CalloutsTakeEndColumn() const {
        if (calloutStyle == PyramidCalloutStyle::NoCallouts || !IsVerticalPyramid()) return false;
        return calloutSide == PyramidCalloutSide::CalloutsRight ||
               calloutSide == PyramidCalloutSide::CalloutsAlternating ||
               calloutSide == PyramidCalloutSide::CalloutsBothSides;
    }

    bool UltraCanvasPyramidChart::CalloutAtStartSide(size_t displayIndex) const {
        switch (calloutSide) {
            case PyramidCalloutSide::CalloutsLeft:
                return true;
            case PyramidCalloutSide::CalloutsAlternating:
                return (displayIndex % 2) == 1;
            case PyramidCalloutSide::CalloutsBothSides: {
                // Split the tiers down the middle so both columns fill evenly
                size_t half = (displayOrder.size() + 1) / 2;
                return displayIndex < half;
            }
            case PyramidCalloutSide::CalloutsRight:
            default:
                return false;
        }
    }

// The shared legend always occupies the top or bottom screen edge, so its
// measured inset comes out of the flow margins of a vertical pyramid and out
// of the cross margins of a horizontal one.
    double UltraCanvasPyramidChart::GetMarginStart() const {
        double margin = kEdgePadding;
        if (CalloutsTakeStartColumn()) margin += calloutColumnWidth + kColumnGap + kConnectorRoom;
        if (LabelTakesStartColumn()) margin += levelLabelColumnWidth + kColumnGap;
        if (PercentTakesStartColumn()) margin += percentColumnWidth + kColumnGap;
        if (badgeShape != PyramidBadgeShape::NoBadge &&
            badgePlacement == PyramidBadgePlacement::BadgeOutsideStart) {
            margin += badgeRadius * 2.4 + kColumnGap;
        }
        if (!IsVerticalPyramid() && LegendAtTop()) margin += legendInset;
        return margin;
    }

    double UltraCanvasPyramidChart::GetMarginEnd() const {
        double margin = kEdgePadding;
        if (CalloutsTakeEndColumn()) margin += calloutColumnWidth + kColumnGap + kConnectorRoom;
        if (LabelTakesEndColumn()) margin += levelLabelColumnWidth + kColumnGap;
        if (PercentTakesEndColumn()) margin += percentColumnWidth + kColumnGap;
        if (levelLabelPlacement == PyramidLevelLabelPlacement::LeaderLineLabels) {
            margin += kConnectorRoom;   // Room for the leader lines themselves
        }
        // The extrusion leans up and to the right, so it needs headroom on both axes
        if (shapeMode == PyramidShapeMode::Pyramid3D) {
            margin += depth3D * std::cos(angle3D * M_PI / 180.0) + 4.0;
        }
        if (!IsVerticalPyramid() && !LegendAtTop()) margin += legendInset;
        return margin;
    }

    double UltraCanvasPyramidChart::GetMarginFlowStart() const {
        double margin = chartTitle.empty() ? kEdgePadding : 34.0;
        if (!subtitle.empty()) margin += subtitleFontSize * 1.9;
        if (IsVerticalPyramid() && LegendAtTop()) margin += legendInset;
        if (shapeMode == PyramidShapeMode::Pyramid3D) {
            margin += depth3D * std::sin(angle3D * M_PI / 180.0) + 4.0;
        }
        return margin;
    }

    double UltraCanvasPyramidChart::GetMarginFlowEnd() const {
        double margin = kEdgePadding;
        if (IsVerticalPyramid() && !LegendAtTop()) margin += legendInset;
        if (showBasePlinth) margin += 14.0;
        return margin;
    }

// =============================================================================
// LEGEND (shared ChartLegend component)
// =============================================================================

    bool UltraCanvasPyramidChart::LegendAtTop() const {
        switch (legend.GetPosition()) {
            case ChartLegendPosition::TopStart:
            case ChartLegendPosition::TopCenter:
            case ChartLegendPosition::TopEnd:
                return true;
            default:
                return false;
        }
    }

// The strip the legend may occupy: the element bounds inset by the edge
// padding, starting below the title and subtitle.
    Rect2Dd UltraCanvasPyramidChart::LegendArea() const {
        double top = chartTitle.empty() ? kEdgePadding : 34.0;
        if (!subtitle.empty()) top += subtitleFontSize * 1.9;
        return Rect2Dd(kEdgePadding, top,
                       std::max(1.0, static_cast<double>(GetWidth()) - 2.0 * kEdgePadding),
                       std::max(1.0, static_cast<double>(GetHeight()) - top - kEdgePadding));
    }

// A stacked pyramid explains its segments; a plain one explains its tiers.
// Labels and colours match what the bands draw, palette indices included.
// Rebuilt with the geometry so the two can never disagree.
    void UltraCanvasPyramidChart::RebuildLegendEntries() {
        std::vector<ChartLegendEntry> entries;
        for (const auto& geo : geometry) {
            const PyramidLevel& level = pyramidDataSource->GetLevel(geo.dataIndex);
            if (!level.HasSegments()) continue;
            for (size_t s = 0; s < level.segments.size(); ++s) {
                const auto& segment = level.segments[s];
                bool seen = false;
                for (const auto& entry : entries) {
                    if (entry.label == segment.label) { seen = true; break; }
                }
                if (seen) continue;
                Color color = segment.color.a > 0
                        ? segment.color
                        : Lighten(geo.color, 0.36 * static_cast<double>(s) /
                                             std::max<size_t>(1, level.segments.size() - 1));
                entries.emplace_back(segment.label, color, LegendSwatch::Square);
            }
        }
        if (entries.empty()) {
            for (const auto& geo : geometry) {
                entries.emplace_back(pyramidDataSource->GetLevel(geo.dataIndex).levelLabel,
                                     geo.color, LegendSwatch::Square);
            }
        }
        legend.SetEntries(entries);
    }

// Measure the legend and reserve the space it consumes. The measured inset
// feeds the margins, so a change re-runs the geometry pass before the frame
// is drawn - Measure() is cached, so this settles immediately.
    void UltraCanvasPyramidChart::UpdateLegendLayout(IRenderContext* ctx) {
        double inset = 0.0;
        if (legend.IsVisible() && legend.GetEntryCount() > 0) {
            Rect2Dd area = LegendArea();
            legend.Measure(ctx, area);
            inset = area.height - legend.RemainingArea(area).height;
        }
        if (std::fabs(inset - legendInset) > 0.5) {
            legendInset = inset;
            InvalidateCache();
            UpdateRenderingCache();
        }
    }

    ChartPlotArea UltraCanvasPyramidChart::CalculatePlotArea() {
        double crossStart = GetMarginStart();
        double crossEnd = GetMarginEnd();
        double flowStart = GetMarginFlowStart();
        double flowEnd = GetMarginFlowEnd();

        double w = static_cast<double>(GetWidth());
        double h = static_cast<double>(GetHeight());

        // Never hand back a degenerate rectangle - callers divide by width/height
        if (IsVerticalPyramid()) {
            return ChartPlotArea(crossStart, flowStart,
                                 std::max(1.0, w - crossStart - crossEnd),
                                 std::max(1.0, h - flowStart - flowEnd));
        }
        return ChartPlotArea(flowStart, crossStart,
                             std::max(1.0, w - flowStart - flowEnd),
                             std::max(1.0, h - crossStart - crossEnd));
    }

    ChartDataBounds UltraCanvasPyramidChart::CalculateDataBounds() {
        ChartDataBounds bounds;
        size_t count = GetLevelCount();
        if (count == 0) return bounds;
        return ChartDataBounds(0.0, static_cast<double>(count - 1),
                               0.0, pyramidDataSource->GetMaxValue());
    }

// The flow axis always runs apex to base, whichever way the pyramid points.
    double UltraCanvasPyramidChart::GetFlowOrigin() const {
        if (IsVerticalPyramid()) {
            return IsReversedFlow() ? cachedPlotArea.GetBottom() : cachedPlotArea.y;
        }
        return IsReversedFlow() ? cachedPlotArea.GetRight() : cachedPlotArea.x;
    }

    double UltraCanvasPyramidChart::GetFlowLength() const {
        return IsVerticalPyramid() ? cachedPlotArea.height : cachedPlotArea.width;
    }

    double UltraCanvasPyramidChart::GetCrossCenter() const {
        return IsVerticalPyramid() ? cachedPlotArea.x + cachedPlotArea.width / 2.0
                                   : cachedPlotArea.y + cachedPlotArea.height / 2.0;
    }

    double UltraCanvasPyramidChart::GetCrossHalfSpan() const {
        return (IsVerticalPyramid() ? cachedPlotArea.width : cachedPlotArea.height) / 2.0;
    }

    Point2Dd UltraCanvasPyramidChart::MapFlowCross(double flow, double cross) const {
        double origin = GetFlowOrigin();
        double along = IsReversedFlow() ? origin - flow : origin + flow;
        double center = GetCrossCenter();
        if (IsVerticalPyramid()) return Point2Dd(center + cross, along);
        return Point2Dd(along, center + cross);
    }

    void UltraCanvasPyramidChart::CrossRangeForExtent(double halfExtent, double offset,
                                                      double& low, double& high) const {
        double span = GetCrossHalfSpan();
        // A half pyramid keeps one face vertical, which is what anchoring to an edge
        // does; an explicit end alignment still wins over the default start edge.
        PyramidAlignment effective = alignment;
        if (halfPyramid && effective == PyramidAlignment::CenterAligned) {
            effective = PyramidAlignment::StartAligned;
        }

        switch (effective) {
            case PyramidAlignment::StartAligned:
                low = -span;
                high = -span + 2.0 * halfExtent;
                break;
            case PyramidAlignment::EndAligned:
                high = span;
                low = span - 2.0 * halfExtent;
                break;
            case PyramidAlignment::CenterAligned:
            default:
                low = -halfExtent;
                high = halfExtent;
                break;
        }
        low += offset;
        high += offset;
    }

    void UltraCanvasPyramidChart::BuildBandPolygon(std::vector<Point2Dd>& out, double bandStart,
                                                   double bandEnd, double halfStart, double halfEnd,
                                                   double offset, double skew) const {
        double lowStart, highStart, lowEnd, highEnd;
        CrossRangeForExtent(halfStart, offset, lowStart, highStart);
        CrossRangeForExtent(halfEnd, offset, lowEnd, highEnd);

        out.clear();
        out.reserve(4);
        out.push_back(MapFlowCross(bandStart, lowStart + skew));
        out.push_back(MapFlowCross(bandStart, highStart + skew));
        out.push_back(MapFlowCross(bandEnd, highEnd - skew));
        out.push_back(MapFlowCross(bandEnd, lowEnd - skew));
    }

    Point2Dd UltraCanvasPyramidChart::Offset3D() const {
        double radians = angle3D * M_PI / 180.0;
        // Up and to the right: the reader is looking slightly down on the solid
        return Point2Dd(depth3D * std::cos(radians), -depth3D * std::sin(radians));
    }

    double UltraCanvasPyramidChart::HalfExtentAtT(double t) const {
        t = Clamp(t, 0.0, 1.0);
        return apexHalfExtent + (maxHalfExtent - apexHalfExtent) * t;
    }

    double UltraCanvasPyramidChart::HalfExtentForValue(double value) const {
        if (maxLevelValue <= 0.0) return maxHalfExtent;
        double half = maxHalfExtent * (value / maxLevelValue);
        return std::max(half, minLevelExtent / 2.0);
    }

    std::vector<double> UltraCanvasPyramidChart::ComputeDrawValues() const {
        std::vector<double> values;
        values.reserve(displayOrder.size());
        for (size_t index : displayOrder) {
            values.push_back(std::max(0.0, pyramidDataSource->GetLevel(index).levelValue));
        }

        // A hierarchy with no numbers still has to be drawn, so every tier counts one
        if (!valuesAvailable) {
            std::fill(values.begin(), values.end(), 1.0);
            return values;
        }

        if (valuePolicy == PyramidValuePolicy::ClampToBelow) {
            // A pyramid that narrows downwards is almost always a data error; draw it
            // widening while the tooltips keep reporting the real figures.
            for (size_t i = values.size(); i-- > 1;) {
                values[i - 1] = std::min(values[i - 1], values[i]);
            }
        }
        return values;
    }

// The cut positions are where the honesty of the chart lives. A pyramid is one
// triangle sliced by horizontal lines, so a tier's area grows faster than its
// height: solving for the cut is what makes the drawn quantity match the value.
    bool UltraCanvasPyramidChart::ScaleEncodesValue() const {
        return scaleMode == PyramidScaleMode::HeightProportional ||
               scaleMode == PyramidScaleMode::AreaProportional ||
               scaleMode == PyramidScaleMode::VolumeProportional;
    }

    std::vector<double> UltraCanvasPyramidChart::ComputeCuts(
            const std::vector<double>& values, PyramidScaleMode mode) const {
        size_t n = values.size();
        std::vector<double> cuts(n + 1, 0.0);
        if (n == 0) return cuts;

        if (mode == PyramidScaleMode::EqualLevels ||
            mode == PyramidScaleMode::WidthProportional) {
            for (size_t i = 0; i <= n; ++i) cuts[i] = static_cast<double>(i) / n;
            return cuts;
        }

        double total = 0.0;
        for (double value : values) total += std::max(0.0, value);
        if (total <= 0.0) {
            for (size_t i = 0; i <= n; ++i) cuts[i] = static_cast<double>(i) / n;
            return cuts;
        }

        // Normalised half-width at the apex, in units of the base half-width
        double a = (maxHalfExtent > 0.0) ? Clamp(apexHalfExtent / maxHalfExtent, 0.0, 1.0) : 0.0;
        const double kDegenerate = 1.0 - 1e-6;

        double cumulative = 0.0;
        cuts[0] = 0.0;
        for (size_t i = 0; i < n; ++i) {
            cumulative += std::max(0.0, values[i]);
            double share = Clamp(cumulative / total, 0.0, 1.0);

            double t = share;
            if (mode == PyramidScaleMode::AreaProportional && a < kDegenerate) {
                // Area from the apex to t is a*t + (1-a)*t^2/2, so the cut is the
                // positive root of that quadratic against the requested share.
                double totalArea = a + (1.0 - a) / 2.0;
                double target = share * totalArea;
                double discriminant = a * a + 2.0 * (1.0 - a) * target;
                t = (std::sqrt(std::max(0.0, discriminant)) - a) / (1.0 - a);
            } else if (mode == PyramidScaleMode::VolumeProportional && a < kDegenerate) {
                // The extruded solid grows with the cube of the width, which is what
                // a reader integrates when the pyramid is drawn in 3D.
                double a3 = a * a * a;
                double target = a3 + share * (1.0 - a3);
                t = (std::cbrt(std::max(0.0, target)) - a) / (1.0 - a);
            }
            cuts[i + 1] = Clamp(t, 0.0, 1.0);
        }

        cuts[n] = 1.0;
        // Rounding can leave a cut fractionally behind its predecessor
        for (size_t i = 1; i <= n; ++i) cuts[i] = std::max(cuts[i], cuts[i - 1]);
        return cuts;
    }

    void UltraCanvasPyramidChart::RebuildGeometry() {
        geometry.clear();
        apexCapPolygon.clear();
        topHeavyIndex = -1;
        if (!pyramidDataSource || displayOrder.empty()) {
            legend.ClearEntries();
            return;
        }

        size_t n = displayOrder.size();
        valuesAvailable = !pyramidDataSource->IsValueless();

        maxLevelValue = 0.0;
        totalLevelValue = 0.0;
        for (size_t index : displayOrder) {
            double value = std::max(0.0, pyramidDataSource->GetLevel(index).levelValue);
            maxLevelValue = std::max(maxLevelValue, value);
            totalLevelValue += value;
        }

        maxHalfExtent = std::max(1.0, GetCrossHalfSpan() * pyramidWidthRatio);

        switch (apexMode) {
            case PyramidApexMode::FlatApex:     apexHalfExtent = maxHalfExtent * apexRatio; break;
            case PyramidApexMode::MinWidthApex: apexHalfExtent = minLevelExtent / 2.0; break;
            case PyramidApexMode::PointApex:
            default:                            apexHalfExtent = 0.0; break;
        }
        // A detached cap needs the body to start with a real edge to sit on
        if (apexCap == PyramidApexCap::ApexSeparateCap) {
            apexHalfExtent = std::max(apexHalfExtent, maxHalfExtent * std::max(0.06, apexRatio));
        }

        std::vector<double> values = ComputeDrawValues();
        std::vector<double> cuts = ComputeCuts(values, scaleMode);

        double gap = (shapeMode == PyramidShapeMode::SolidPyramid) ? 0.0 : levelGap;
        double flowLength = GetFlowLength();
        double capReserve = (apexCap == PyramidApexCap::ApexSeparateCap)
                ? apexCapHeight + gap : 0.0;

        double available = autoFitLevels
                ? std::max(1.0, flowLength - capReserve - gap * (n - 1))
                : levelThickness * n;

        bool bandIsRectangular = (shapeMode == PyramidShapeMode::SteppedPyramid ||
                                  shapeMode == PyramidShapeMode::BarStylePyramid ||
                                  shapeMode == PyramidShapeMode::CardRows);

        double cursor = capReserve;
        geometry.reserve(n);

        for (size_t i = 0; i < n; ++i) {
            PyramidLevelGeometry geo;
            geo.dataIndex = displayOrder[i];

            double thickness = std::max(kMinBandThickness, (cuts[i + 1] - cuts[i]) * available);
            geo.bandStart = cursor;
            geo.bandEnd = cursor + thickness;

            if (scaleMode == PyramidScaleMode::WidthProportional) {
                geo.halfExtentStart = geo.halfExtentEnd = HalfExtentForValue(values[i]);
            } else if (bandIsRectangular) {
                geo.halfExtentStart = geo.halfExtentEnd =
                        HalfExtentAtT((cuts[i] + cuts[i + 1]) / 2.0);
            } else {
                // The extents come from the cut positions, not from the band after the
                // gaps have been taken out, so the silhouette stays a clean triangle.
                geo.halfExtentStart = HalfExtentAtT(cuts[i]);
                geo.halfExtentEnd = HalfExtentAtT(cuts[i + 1]);
            }
            geo.halfExtentStart = std::max(geo.halfExtentStart, 0.0);
            geo.halfExtentEnd = std::max(geo.halfExtentEnd, minLevelExtent / 2.0);

            geo.crossOffset = (shapeMode == PyramidShapeMode::ExplodedPyramid)
                    ? explodeOffset * ((i % 2 == 0) ? -1.0 : 1.0)
                    : 0.0;

            double skew = (shapeMode == PyramidShapeMode::SkewedInfographic)
                    ? slabSkew * ((i % 2 == 0) ? 1.0 : -1.0)
                    : 0.0;

            BuildBandPolygon(geo.polygon, geo.bandStart, geo.bandEnd,
                             geo.halfExtentStart, geo.halfExtentEnd, geo.crossOffset, skew);
            geo.center = Centroid(geo.polygon);

            geometry.push_back(std::move(geo));
            cursor = geometry.back().bandEnd + gap;
        }

        // ApexHidden suppresses the closing taper without moving anything else
        if (apexCap == PyramidApexCap::ApexHidden && !geometry.empty()) {
            auto& top = geometry.front();
            top.halfExtentStart = top.halfExtentEnd;
            BuildBandPolygon(top.polygon, top.bandStart, top.bandEnd,
                             top.halfExtentStart, top.halfExtentEnd, top.crossOffset, 0.0);
            top.center = Centroid(top.polygon);
        }

        // The detached cap is a triangle closing onto the body's first edge
        if (apexCap == PyramidApexCap::ApexSeparateCap && !geometry.empty()) {
            BuildBandPolygon(apexCapPolygon, 0.0, apexCapHeight, 0.0,
                             geometry.front().halfExtentStart, geometry.front().crossOffset, 0.0);
        }

        // Extruded faces, chosen from the drawn outline so all four directions work
        if (shapeMode == PyramidShapeMode::Pyramid3D) {
            Point2Dd d = Offset3D();
            for (auto& geo : geometry) {
                const auto& p = geo.polygon;
                if (p.size() < 4) continue;

                // The face the reader looks down on is the edge highest on screen;
                // the shaded flank is the edge furthest to the right.
                size_t topEdge = 0, sideEdge = 0;
                double bestY = 1e18, bestX = -1e18;
                for (size_t e = 0; e < 4; ++e) {
                    const Point2Dd& from = p[e];
                    const Point2Dd& to = p[(e + 1) % 4];
                    double midY = (from.y + to.y) / 2.0;
                    double midX = (from.x + to.x) / 2.0;
                    if (midY < bestY) { bestY = midY; topEdge = e; }
                    if (midX > bestX) { bestX = midX; sideEdge = e; }
                }

                auto extrude = [&d](const Point2Dd& from, const Point2Dd& to) {
                    return std::vector<Point2Dd>{
                        from, to, Point2Dd(to.x + d.x, to.y + d.y),
                        Point2Dd(from.x + d.x, from.y + d.y)
                    };
                };
                geo.topFace = extrude(p[topEdge], p[(topEdge + 1) % 4]);
                geo.sideFace = extrude(p[sideEdge], p[(sideEdge + 1) % 4]);
            }
        }

        // Metrics always describe the drawn neighbours, so a sorted pyramid still
        // reports the relationships the reader can actually see.
        double runningFromApex = 0.0;
        for (size_t i = 0; i < n; ++i) {
            auto& geo = geometry[i];
            auto& metrics = geo.metrics;
            metrics.value = pyramidDataSource->GetLevel(geo.dataIndex).levelValue;
            metrics.levelFromApex = i;
            metrics.levelFromBase = n - 1 - i;

            if (totalLevelValue != 0.0) {
                metrics.percentOfTotal = metrics.value / totalLevelValue * 100.0;
            }
            double baseValue = pyramidDataSource->GetLevel(displayOrder[n - 1]).levelValue;
            if (baseValue != 0.0) metrics.percentOfBase = metrics.value / baseValue * 100.0;

            if (i + 1 < n) {
                double below = pyramidDataSource->GetLevel(displayOrder[i + 1]).levelValue;
                if (below != 0.0) metrics.ratioToLevelBelow = metrics.value / below * 100.0;
            }

            runningFromApex += metrics.value;
            if (totalLevelValue != 0.0) {
                metrics.cumulativeFromApex = runningFromApex / totalLevelValue * 100.0;
                metrics.cumulativeFromBase =
                        (totalLevelValue - runningFromApex + metrics.value) / totalLevelValue * 100.0;
            }
        }

        // The top-heavy tier: one that outweighs the tier holding it up
        if (valuesAvailable) {
            for (size_t i = 0; i + 1 < n; ++i) {
                if (geometry[i].metrics.value > geometry[i + 1].metrics.value) {
                    topHeavyIndex = static_cast<int>(i);
                    break;
                }
            }
        }

        // Colours depend on the metrics, so they are resolved once everything is known
        for (size_t i = 0; i < n; ++i) {
            geometry[i].color = ResolveLevelColor(i, geometry[i].dataIndex, geometry[i].metrics);
        }

        RebuildLegendEntries();
    }

    void UltraCanvasPyramidChart::UpdateRenderingCache() {
        if (!orderValid) RebuildDisplayOrder();
        if (cacheValid) return;

        // The base class computes the plot area, and the margins it asks for depend
        // on whether there are any values to label, so this has to be known first
        valuesAvailable = pyramidDataSource && !pyramidDataSource->IsValueless();
        UltraCanvasChartElementBase::UpdateRenderingCache();
        RebuildGeometry();
    }

// =============================================================================
// COLOUR
// =============================================================================

    Color UltraCanvasPyramidChart::ResolveLevelColor(size_t displayIndex, size_t dataIndex,
                                                     const PyramidLevelMetrics& metrics) const {
        // An explicit per-level colour always wins, whatever the mode
        const PyramidLevel& level = pyramidDataSource->GetLevel(dataIndex);
        if (level.levelColor.a > 0) return level.levelColor;

        size_t n = std::max<size_t>(1, displayOrder.size());

        switch (colorMode) {
            case PyramidColorMode::CategoricalPalette:
                return palette.empty() ? baseColor : palette[displayIndex % palette.size()];

            case PyramidColorMode::SingleHue:
            case PyramidColorMode::PerLevelOverride:
                return baseColor;

            case PyramidColorMode::ThresholdColor: {
                if (metrics.percentOfTotal >= majorShareThreshold) return majorShareColor;
                if (metrics.percentOfTotal >= minorShareThreshold) return middleShareColor;
                return minorShareColor;
            }

            case PyramidColorMode::SequentialRamp:
            default: {
                double t = (n <= 1) ? 0.0 : static_cast<double>(displayIndex) / (n - 1);
                return LerpColor(rampStartColor, rampEndColor, t);
            }
        }
    }

    Color UltraCanvasPyramidChart::PickReadableTextColor(const Color& background) const {
        if (!autoContrastInsideLabels) return insideLabelColor;
        return Luminance(background) > 0.62 ? Color(40, 45, 50, 255) : Color(255, 255, 255, 255);
    }

    Color UltraCanvasPyramidChart::ResolveBadgeFill(const PyramidLevelGeometry& geo) const {
        return badgeColor.a > 0 ? badgeColor : geo.color;
    }

// =============================================================================
// TEXT HELPERS
// =============================================================================

    std::string UltraCanvasPyramidChart::FormatLevelValue(double value) const {
        char buffer[128];

        if (abbreviateValues) {
            double magnitude = std::abs(value);
            const char* suffix = "";
            double scaled = value;
            if (magnitude >= 1e9)      { scaled = value / 1e9; suffix = "B"; }
            else if (magnitude >= 1e6) { scaled = value / 1e6; suffix = "M"; }
            else if (magnitude >= 1e3) { scaled = value / 1e3; suffix = "K"; }

            if (*suffix != '\0') {
                std::snprintf(buffer, sizeof(buffer), "%.2f%s", scaled, suffix);
                return std::string(buffer);
            }
        }

        // valueFormat is supplied by the caller; guard the buffer and fall back on a
        // plain rendering when the format produces nothing usable.
        int written = std::snprintf(buffer, sizeof(buffer), valueFormat.c_str(), value);
        if (written <= 0) {
            std::snprintf(buffer, sizeof(buffer), "%.0f", value);
        }
        return std::string(buffer);
    }

    std::string UltraCanvasPyramidChart::FormatPercent(double percent) const {
        char buffer[128];
        int written = std::snprintf(buffer, sizeof(buffer), percentFormat.c_str(), percent);
        if (written <= 0) {
            std::snprintf(buffer, sizeof(buffer), "%.1f%%", percent);
        }
        return std::string(buffer);
    }

    std::string UltraCanvasPyramidChart::BadgeTextFor(size_t displayIndex) const {
        if (displayIndex >= geometry.size()) return std::string();
        const PyramidLevel& level = pyramidDataSource->GetLevel(geometry[displayIndex].dataIndex);
        if (!level.badgeText.empty()) return level.badgeText;

        // An empty badge falls back to the tier's position, counted from the apex
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%zu", displayIndex + 1);
        return std::string(buffer);
    }

    std::string UltraCanvasPyramidChart::TruncateToWidth(IRenderContext* ctx,
                                                         const std::string& text,
                                                         double maxWidth) const {
        if (text.empty() || maxWidth <= 0.0) return std::string();
        if (ctx->GetTextLineDimensions(text).width <= maxWidth) return text;

        const std::string ellipsis = "...";
        std::string result = text;
        while (!result.empty()) {
            result.pop_back();
            if (ctx->GetTextLineDimensions(result + ellipsis).width <= maxWidth) {
                return result + ellipsis;
            }
        }
        return std::string();
    }

    std::vector<std::string> UltraCanvasPyramidChart::WrapText(IRenderContext* ctx,
                                                               const std::string& text,
                                                               double maxWidth) const {
        std::vector<std::string> lines;
        if (text.empty() || maxWidth <= 0.0) return lines;

        std::istringstream stream(text);
        std::string word;
        std::string current;
        while (stream >> word) {
            std::string candidate = current.empty() ? word : current + " " + word;
            if (ctx->GetTextLineDimensions(candidate).width <= maxWidth || current.empty()) {
                current = candidate;
                // A single word wider than the column still has to go somewhere, so it
                // is accepted here and ellipsized when it is drawn.
                if (current == word &&
                    ctx->GetTextLineDimensions(current).width > maxWidth) {
                    lines.push_back(TruncateToWidth(ctx, current, maxWidth));
                    current.clear();
                }
            } else {
                lines.push_back(current);
                current = word;
            }
        }
        if (!current.empty()) lines.push_back(current);
        return lines;
    }

    void UltraCanvasPyramidChart::DrawCenteredText(IRenderContext* ctx, const std::string& text,
                                                   const Point2Dd& center) const {
        if (text.empty()) return;
        Size2Di size = ctx->GetTextLineDimensions(text);
        ctx->DrawText(text, Point2Dd(center.x - size.width / 2.0, center.y - size.height / 2.0));
    }

    std::vector<std::string> UltraCanvasPyramidChart::BuildPercentLines(
            const PyramidLevelMetrics& metrics) const {
        std::vector<std::string> lines;
        if (!valuesAvailable) return lines;
        if (showPercentOfTotal)      lines.push_back(FormatPercent(metrics.percentOfTotal));
        if (showPercentOfBase)       lines.push_back(FormatPercent(metrics.percentOfBase));
        if (showRatioToLevelBelow)   lines.push_back(FormatPercent(metrics.ratioToLevelBelow));
        if (showCumulativeFromBase)  lines.push_back(FormatPercent(metrics.cumulativeFromBase));
        return lines;
    }

// =============================================================================
// RENDERING
// =============================================================================

    void UltraCanvasPyramidChart::RenderCommonBackground(IRenderContext* ctx) {
        if (!ctx) return;

        if (showBackground) {
            ctx->DrawFilledRectangle(GetLocalBounds(), backgroundColor);
        }
        RenderTitle(ctx);
    }

    void UltraCanvasPyramidChart::RenderTitle(IRenderContext* ctx) {
        if (chartTitle.empty() && subtitle.empty()) return;

        ctx->PushState();
        // A dark chart background needs light title text, and the caller only has to
        // say so explicitly when the automatic choice is wrong.
        bool darkGround = Luminance(backgroundColor) < 0.5;
        Color color = titleColor.a > 0
                ? titleColor
                : (darkGround ? Color(235, 232, 245, 255) : Color(60, 60, 66, 255));

        double y = 10.0;
        if (!chartTitle.empty()) {
            ctx->SetTextPaint(color);
            ctx->SetFontSize(14.0);
            ctx->SetFontWeight(FontWeight::Bold);
            Size2Di size = ctx->GetTextLineDimensions(chartTitle);
            double x = (static_cast<double>(GetWidth()) - size.width) / 2.0;
            ctx->DrawText(chartTitle, Point2Dd(std::max(4.0, x), y));
            y += size.height + 4.0;
        }

        if (!subtitle.empty()) {
            ctx->SetFontWeight(FontWeight::Normal);
            ctx->SetFontSize(subtitleFontSize);
            ctx->SetTextPaint(darkGround ? Lighten(subtitleColor, 0.25) : subtitleColor);
            Size2Di size = ctx->GetTextLineDimensions(subtitle);
            double x = (static_cast<double>(GetWidth()) - size.width) / 2.0;
            ctx->DrawText(subtitle, Point2Dd(std::max(4.0, x), y));
        }
        ctx->PopState();
    }

    float UltraCanvasPyramidChart::GetLevelProgress(size_t displayIndex) const {
        if (!animationEnabled || animationComplete) return 1.0f;

        // Tiers land base first: a pyramid is built from the ground up
        size_t n = std::max<size_t>(1, displayOrder.size());
        size_t fromBase = n - 1 - std::min(displayIndex, n - 1);

        float t = GetAnimationProgress();
        float count = static_cast<float>(n) + 1.0f;
        float start = static_cast<float>(fromBase) / count;
        float span = 2.0f / count;
        return static_cast<float>(Clamp((t - start) / span, 0.0, 1.0));
    }

    std::vector<Point2Dd> UltraCanvasPyramidChart::ScalePolygon(
            const std::vector<Point2Dd>& points, float progress) const {
        if (progress >= 1.0f) return points;

        // Tiers grow out from whichever edge the pyramid is anchored to
        double span = GetCrossHalfSpan();
        double center = GetCrossCenter();
        double anchor = center;
        PyramidAlignment effective = alignment;
        if (halfPyramid && effective == PyramidAlignment::CenterAligned) {
            effective = PyramidAlignment::StartAligned;
        }
        if (effective == PyramidAlignment::StartAligned) anchor = center - span;
        else if (effective == PyramidAlignment::EndAligned) anchor = center + span;

        std::vector<Point2Dd> scaled;
        scaled.reserve(points.size());
        for (const auto& point : points) {
            if (IsVerticalPyramid()) {
                scaled.emplace_back(anchor + (point.x - anchor) * progress, point.y);
            } else {
                scaled.emplace_back(point.x, anchor + (point.y - anchor) * progress);
            }
        }
        return scaled;
    }

    void UltraCanvasPyramidChart::DrawPolygon(IRenderContext* ctx,
                                              const std::vector<Point2Dd>& points,
                                              const Color& fill, const Color& border,
                                              double borderWidth) const {
        if (points.size() < 3) return;

        if (fill.a > 0) {
            ctx->SetFillPaint(fill);
            ctx->FillLinePath(points);
        }
        if (border.a > 0 && borderWidth > 0.0) {
            ctx->SetStrokePaint(border);
            ctx->SetStrokeWidth(borderWidth);
            ctx->DrawLinePath(points, true);
        }
    }

    void UltraCanvasPyramidChart::DrawGradientPolygon(IRenderContext* ctx,
                                                      const std::vector<Point2Dd>& points,
                                                      const Color& fill) const {
        if (points.size() < 3) return;

        double minX = points[0].x, maxX = points[0].x;
        double minY = points[0].y, maxY = points[0].y;
        for (const auto& point : points) {
            minX = std::min(minX, point.x);
            maxX = std::max(maxX, point.x);
            minY = std::min(minY, point.y);
            maxY = std::max(maxY, point.y);
        }

        // The gradient runs across the pyramid, so the lit edge follows the taper
        std::vector<GradientStop> stops = {
            GradientStop(0.0, Lighten(fill, gradientLightening)),
            GradientStop(1.0, fill)
        };

        auto pattern = IsVerticalPyramid()
                ? ctx->CreateLinearGradientPattern(minX, minY, maxX, minY, stops)
                : ctx->CreateLinearGradientPattern(minX, minY, minX, maxY, stops);
        if (!pattern) {
            DrawPolygon(ctx, points, fill, Colors::Transparent, 0.0);
            return;
        }

        ctx->SetFillPaint(pattern);
        ctx->FillLinePath(points);
    }

    void UltraCanvasPyramidChart::FillBandByStyle(IRenderContext* ctx,
                                                  const std::vector<Point2Dd>& points,
                                                  const Color& color) const {
        switch (fillStyle) {
            case PyramidFillStyle::GradientFill:
                DrawGradientPolygon(ctx, points, color);
                break;

            case PyramidFillStyle::OutlineOnly:
            case PyramidFillStyle::OutlineWithTint: {
                if (fillStyle == PyramidFillStyle::OutlineWithTint) {
                    DrawPolygon(ctx, points, WithAlpha(color, tintStrength),
                                Colors::Transparent, 0.0);
                }
                ctx->PushState();
                if (dashedOutline) ctx->SetLineDash(UCDashPattern({5.0, 4.0}));
                DrawPolygon(ctx, points, Colors::Transparent, color, outlineWidth);
                ctx->PopState();
                break;
            }

            case PyramidFillStyle::SolidFill:
            default:
                DrawPolygon(ctx, points, color, Colors::Transparent, 0.0);
                break;
        }
    }

    void UltraCanvasPyramidChart::RenderChart(IRenderContext* ctx) {
        if (!ctx || geometry.empty()) return;

        // The legend consumes real space, so its measured layout comes first
        UpdateLegendLayout(ctx);

        LayoutCallouts(ctx);

        if (shapeMode == PyramidShapeMode::CardRows) RenderCardBackgrounds(ctx);
        if (showBasePlinth) RenderBasePlinth(ctx);

        RenderLevels(ctx);
        if (apexCap == PyramidApexCap::ApexSeparateCap) {
            RenderApexCap(ctx, GetLevelProgress(0));
        }
        // The reference silhouette shares the pyramid's footprint, so it has to be
        // drawn over the tiers or the fills swallow it whole
        if (!referenceShape.empty()) RenderReferenceShape(ctx);
        if (showTargetOverlay) RenderTargetOverlay(ctx);
        if (benchmarkEnabled) RenderBenchmark(ctx);

        RenderInsideLabels(ctx);
        RenderLevelLabelColumn(ctx);
        RenderPercentColumn(ctx);
        if (badgeShape != PyramidBadgeShape::NoBadge &&
            badgePlacement != PyramidBadgePlacement::BadgeOnLabelRow) {
            RenderBadges(ctx);
        }
        RenderCallouts(ctx);
        legend.Render(ctx, LegendArea());   // No-op while hidden or empty

        // Animation drives itself: keep asking for frames until every tier is in
        if (animationEnabled && !animationComplete) RequestRedraw();
    }

    void UltraCanvasPyramidChart::RenderCardBackgrounds(IRenderContext* ctx) {
        ctx->PushState();
        for (size_t i = 0; i < geometry.size(); ++i) {
            const auto& geo = geometry[i];
            double thickness = geo.bandEnd - geo.bandStart;
            Point2Dd from = MapFlowCross(geo.bandStart, -GetCrossHalfSpan());
            Point2Dd to = MapFlowCross(geo.bandEnd, GetCrossHalfSpan());

            Rect2Dd card(std::min(from.x, to.x), std::min(from.y, to.y),
                         std::abs(to.x - from.x), std::abs(to.y - from.y));
            if (IsVerticalPyramid()) {
                card.height = thickness;
                card.y = std::min(from.y, to.y);
            } else {
                card.width = thickness;
                card.x = std::min(from.x, to.x);
            }

            bool isHovered = (static_cast<int>(i) == hoveredLevelIndex);
            Color border = isHovered ? geo.color : cardBorderColor;
            ctx->DrawFilledRectangle(card, cardFillColor, isHovered ? 2.0f : 1.0f, border,
                                     static_cast<float>(cardCornerRadius));
        }
        ctx->PopState();
    }

    void UltraCanvasPyramidChart::RenderBasePlinth(IRenderContext* ctx) {
        if (geometry.empty()) return;

        const auto& base = geometry.back();
        double low, high;
        CrossRangeForExtent(base.halfExtentEnd * 1.08, base.crossOffset, low, high);

        std::vector<Point2Dd> plinth = {
            MapFlowCross(base.bandEnd + 2.0, low),
            MapFlowCross(base.bandEnd + 2.0, high),
            MapFlowCross(base.bandEnd + 10.0, high),
            MapFlowCross(base.bandEnd + 10.0, low)
        };

        ctx->PushState();
        DrawPolygon(ctx, plinth, basePlinthColor, Colors::Transparent, 0.0);
        ctx->PopState();
    }

    void UltraCanvasPyramidChart::RenderLevels(IRenderContext* ctx) {
        ctx->PushState();
        // Base to apex: an upper tier is nearer the reader and has to occlude the
        // extruded faces of the tier below it.
        for (size_t i = geometry.size(); i-- > 0;) {
            const auto& geo = geometry[i];
            float progress = GetLevelProgress(i);
            if (progress <= 0.0f) continue;

            const PyramidLevel& level = pyramidDataSource->GetLevel(geo.dataIndex);
            RenderLevelSlab(ctx, geo, level, i, progress);
        }
        ctx->PopState();
    }

    void UltraCanvasPyramidChart::RenderLevelSlab(IRenderContext* ctx,
                                                  const PyramidLevelGeometry& geo,
                                                  const PyramidLevel& level, size_t displayIndex,
                                                  float progress) {
        std::vector<Point2Dd> polygon = ScalePolygon(geo.polygon, progress);

        bool isHovered = (static_cast<int>(displayIndex) == hoveredLevelIndex);
        bool isSelected = (static_cast<int>(displayIndex) == selectedLevelIndex);

        Color fill = geo.color;
        if (isHovered) fill = Lighten(fill, 0.14);
        if (dimUnhoveredLevels && hoveredLevelIndex >= 0 && !isHovered) {
            fill = WithAlpha(fill, 0.4);
        }

        // The extrusion goes down first so the front face always sits on top of it
        if (shapeMode == PyramidShapeMode::Pyramid3D) {
            if (!geo.sideFace.empty()) {
                DrawPolygon(ctx, ScalePolygon(geo.sideFace, progress),
                            Darken(fill, sideFaceDarken), Colors::Transparent, 0.0);
            }
            if (showTopFace && !geo.topFace.empty()) {
                DrawPolygon(ctx, ScalePolygon(geo.topFace, progress),
                            Lighten(fill, topFaceLighten), Colors::Transparent, 0.0);
            }
        }

        if (level.HasSegments()) {
            RenderSubSegments(ctx, geo, level, progress);
        } else {
            FillBandByStyle(ctx, polygon, fill);
        }

        bool isTopHeavy = highlightTopHeavy && topHeavyIndex == static_cast<int>(displayIndex);
        if (isTopHeavy) {
            DrawPolygon(ctx, polygon, Colors::Transparent, topHeavyColor, 2.5);
        } else if (isSelected) {
            DrawPolygon(ctx, polygon, Colors::Transparent, Darken(geo.color, 0.35), 2.5);
        } else if (showLevelBorder && fillStyle != PyramidFillStyle::OutlineOnly &&
                   fillStyle != PyramidFillStyle::OutlineWithTint) {
            DrawPolygon(ctx, polygon, Colors::Transparent, levelBorderColor, levelBorderWidth);
        }
    }

    void UltraCanvasPyramidChart::RenderSubSegments(IRenderContext* ctx,
                                                    const PyramidLevelGeometry& geo,
                                                    const PyramidLevel& level, float progress) {
        double total = level.GetSegmentTotal();
        if (total <= 0.0) {
            FillBandByStyle(ctx, ScalePolygon(geo.polygon, progress), geo.color);
            return;
        }

        double lowStart, highStart, lowEnd, highEnd;
        CrossRangeForExtent(geo.halfExtentStart, geo.crossOffset, lowStart, highStart);
        CrossRangeForExtent(geo.halfExtentEnd, geo.crossOffset, lowEnd, highEnd);

        double cursor = 0.0;
        for (size_t s = 0; s < level.segments.size(); ++s) {
            const auto& segment = level.segments[s];
            double share = std::max(0.0, segment.value) / total;
            double from = cursor;
            double to = cursor + share;
            cursor = to;

            // Each slice keeps the tier's taper, so the stack still reads as a pyramid
            std::vector<Point2Dd> slice = {
                MapFlowCross(geo.bandStart, lowStart + (highStart - lowStart) * from),
                MapFlowCross(geo.bandStart, lowStart + (highStart - lowStart) * to),
                MapFlowCross(geo.bandEnd, lowEnd + (highEnd - lowEnd) * to),
                MapFlowCross(geo.bandEnd, lowEnd + (highEnd - lowEnd) * from)
            };

            Color fill = segment.color.a > 0
                    ? segment.color
                    : Lighten(geo.color, 0.36 * static_cast<double>(s) /
                                         std::max<size_t>(1, level.segments.size() - 1));

            DrawPolygon(ctx, ScalePolygon(slice, progress), fill, Colors::Transparent, 0.0);
        }
    }

    void UltraCanvasPyramidChart::RenderApexCap(IRenderContext* ctx, float progress) {
        if (apexCapPolygon.size() < 3 || progress <= 0.0f || geometry.empty()) return;

        ctx->PushState();
        Color color = geometry.front().color;
        FillBandByStyle(ctx, ScalePolygon(apexCapPolygon, progress), color);
        ctx->PopState();
    }

    void UltraCanvasPyramidChart::RenderTargetOverlay(IRenderContext* ctx) {
        ctx->PushState();
        for (const auto& geo : geometry) {
            const PyramidLevel& level = pyramidDataSource->GetLevel(geo.dataIndex);
            if (level.targetValue <= 0.0) continue;

            double halfTarget = HalfExtentForValue(level.targetValue);
            std::vector<Point2Dd> ghost;
            BuildBandPolygon(ghost, geo.bandStart, geo.bandEnd, halfTarget, halfTarget,
                             geo.crossOffset, 0.0);
            DrawPolygon(ctx, ghost, Colors::Transparent, targetOverlayColor, 1.5);
        }
        ctx->PopState();
    }

    void UltraCanvasPyramidChart::RenderReferenceShape(IRenderContext* ctx) {
        double total = 0.0;
        for (double share : referenceShape) total += std::max(0.0, share);
        if (total <= 0.0) return;
        // The overlay is a comparison against the drawn cuts, so it is only
        // meaningful where those cuts encode the values in the first place
        if (!ScaleEncodesValue()) return;

        double flowLength = GetFlowLength();
        double capReserve = (apexCap == PyramidApexCap::ApexSeparateCap)
                ? apexCapHeight + levelGap : 0.0;
        double bodyLength = std::max(1.0, flowLength - capReserve);

        ctx->PushState();
        ctx->SetLineDash(UCDashPattern({6.0, 4.0}));

        // The ideal silhouette, plus a rule at every proportion it asks for
        std::vector<Point2Dd> silhouette;
        BuildBandPolygon(silhouette, capReserve, capReserve + bodyLength,
                         apexHalfExtent, maxHalfExtent, 0.0, 0.0);
        DrawPolygon(ctx, silhouette, Colors::Transparent, referenceShapeColor, 1.5);

        // The ideal cuts go through the same solver as the drawn ones, or the two
        // would not be measuring the same thing
        std::vector<double> idealCuts = ComputeCuts(referenceShape, scaleMode);
        for (size_t i = 1; i + 1 < idealCuts.size(); ++i) {
            double t = idealCuts[i];
            double half = HalfExtentAtT(t);
            double low, high;
            CrossRangeForExtent(half, 0.0, low, high);
            ctx->SetStrokePaint(referenceShapeColor);
            ctx->SetStrokeWidth(1.5);
            ctx->DrawLine(MapFlowCross(capReserve + t * bodyLength, low),
                          MapFlowCross(capReserve + t * bodyLength, high));
        }
        ctx->PopState();
    }

    void UltraCanvasPyramidChart::RenderBenchmark(IRenderContext* ctx) {
        if (!valuesAvailable || totalLevelValue <= 0.0 || geometry.empty()) return;

        // Walk up from the base until the running total passes the benchmark, then
        // interpolate inside the tier where it lands.
        double running = 0.0;
        double flow = geometry.back().bandEnd;
        bool found = false;
        for (size_t i = geometry.size(); i-- > 0;) {
            double value = std::max(0.0, geometry[i].metrics.value);
            if (running + value >= benchmarkValue && value > 0.0) {
                double within = (benchmarkValue - running) / value;
                flow = geometry[i].bandEnd -
                       (geometry[i].bandEnd - geometry[i].bandStart) * Clamp(within, 0.0, 1.0);
                found = true;
                break;
            }
            running += value;
            flow = geometry[i].bandStart;
        }
        if (!found && benchmarkValue > running) return;

        double low, high;
        CrossRangeForExtent(GetCrossHalfSpan(), 0.0, low, high);

        ctx->PushState();
        ctx->SetLineDash(UCDashPattern({5.0, 4.0}));
        ctx->SetStrokePaint(benchmarkColor);
        ctx->SetStrokeWidth(1.5);
        ctx->DrawLine(MapFlowCross(flow, low), MapFlowCross(flow, high));

        if (!benchmarkLabel.empty()) {
            ctx->SetLineDash(UCDashPattern());
            ctx->SetFontSize(levelLabelFontSize * 0.92);
            ctx->SetTextPaint(benchmarkColor);
            Point2Dd anchor = MapFlowCross(flow, high);
            Size2Di size = ctx->GetTextLineDimensions(benchmarkLabel);
            ctx->DrawText(benchmarkLabel, Point2Dd(anchor.x + 6.0, anchor.y - size.height - 2.0));
        }
        ctx->PopState();
    }

    void UltraCanvasPyramidChart::RenderInsideLabels(IRenderContext* ctx) {
        bool wantLabels = (levelLabelPlacement == PyramidLevelLabelPlacement::InsideLevelLabels);
        bool wantValues = showValueLabels && valuesAvailable &&
                          pyramidValueLabelPlacement == PyramidValueLabelPlacement::InsideValueLabels;
        bool wantPercent = (percentPlacement == PyramidPercentPlacement::InsideLevelPercent) &&
                           HasAnyPercentChannel();

        if (!wantLabels && !wantValues && !wantPercent && !showDescriptionInside) return;

        ctx->PushState();

        for (size_t i = 0; i < geometry.size(); ++i) {
            const auto& geo = geometry[i];
            if (GetLevelProgress(i) < 1.0f) continue;

            const PyramidLevel& level = pyramidDataSource->GetLevel(geo.dataIndex);

            // A tier is a trapezoid: text has to fit its narrowest edge, not its mean,
            // or the last line runs out over the slope.
            double narrowHalf = std::min(geo.halfExtentStart, geo.halfExtentEnd);
            double bandThickness = geo.bandEnd - geo.bandStart;
            double availableWidth = (IsVerticalPyramid() ? 2.0 * narrowHalf : bandThickness)
                                    - 2.0 * kInsidePadding;
            double availableHeight = IsVerticalPyramid() ? bandThickness : 2.0 * narrowHalf;
            if (availableWidth <= 4.0 || availableHeight <= 4.0) continue;

            // Badges sitting inside the tier take their room out of the text column
            if (badgeShape != PyramidBadgeShape::NoBadge &&
                badgePlacement == PyramidBadgePlacement::BadgeInsideLevel) {
                availableWidth -= badgeRadius * 2.4;
            }
            if (availableWidth <= 4.0) continue;

            double fontSize = valueLabelFontSize > 0 ? valueLabelFontSize : 11.0;
            std::vector<std::string> lines;

            // Shrink the type before dropping content: on a pyramid the lower tiers
            // have room to spare and the apex never does.
            for (;;) {
                ctx->SetFontSize(fontSize);
                lines.clear();

                if (wantLabels && !level.levelLabel.empty()) {
                    if (wrapInsideText) {
                        auto wrapped = WrapText(ctx, level.levelLabel, availableWidth);
                        lines.insert(lines.end(), wrapped.begin(), wrapped.end());
                    } else {
                        lines.push_back(TruncateToWidth(ctx, level.levelLabel, availableWidth));
                    }
                }
                if (wantValues) lines.push_back(FormatLevelValue(geo.metrics.value));
                if (wantPercent) {
                    auto percents = BuildPercentLines(geo.metrics);
                    lines.insert(lines.end(), percents.begin(), percents.end());
                }
                if (showDescriptionInside && !level.description.empty()) {
                    auto wrapped = WrapText(ctx, level.description, availableWidth);
                    lines.insert(lines.end(), wrapped.begin(), wrapped.end());
                }
                if (lines.empty()) break;

                double lineHeight = ctx->GetTextLineDimensions(lines[0]).height;
                if (lineHeight * lines.size() <= availableHeight) break;
                if (!shrinkInsideText || fontSize <= minInsideFontSize) break;
                fontSize = std::max(minInsideFontSize, fontSize - 0.5);
            }

            if (lines.empty()) continue;
            ctx->SetFontSize(fontSize);

            double lineHeight = ctx->GetTextLineDimensions(lines[0]).height;
            // Drop the least important lines rather than the whole label, so a narrow
            // apex tier degrades to just its name instead of going blank
            while (lines.size() > 1 && lineHeight * lines.size() > availableHeight) {
                lines.pop_back();
            }
            if (lineHeight * lines.size() > availableHeight) continue;

            ctx->SetTextPaint(fillStyle == PyramidFillStyle::OutlineOnly
                                      ? levelLabelColor
                                      : PickReadableTextColor(geo.color));

            double top = geo.center.y - lineHeight * lines.size() / 2.0;
            for (size_t l = 0; l < lines.size(); ++l) {
                Size2Di size = ctx->GetTextLineDimensions(lines[l]);
                if (size.width > availableWidth) {
                    lines[l] = TruncateToWidth(ctx, lines[l], availableWidth);
                    size = ctx->GetTextLineDimensions(lines[l]);
                }

                double x = geo.center.x - size.width / 2.0;
                if (IsVerticalPyramid()) {
                    if (insideTextAlign == PyramidInsideTextAlign::InsideTextStart) {
                        x = geo.center.x - availableWidth / 2.0;
                    } else if (insideTextAlign == PyramidInsideTextAlign::InsideTextEnd) {
                        x = geo.center.x + availableWidth / 2.0 - size.width;
                    }
                }
                ctx->DrawText(lines[l], Point2Dd(x, top + lineHeight * l));
            }
        }
        ctx->PopState();
    }

    void UltraCanvasPyramidChart::RenderLevelLabelColumn(IRenderContext* ctx) {
        bool labelsAtStart = LabelTakesStartColumn();
        bool labelsAtEnd = LabelTakesEndColumn();
        if (!labelsAtStart && !labelsAtEnd) return;

        bool leaderLines = (levelLabelPlacement == PyramidLevelLabelPlacement::LeaderLineLabels);
        bool nameAtStart = (levelLabelPlacement == PyramidLevelLabelPlacement::OutsideStartLabels);
        bool nameAtEnd = (levelLabelPlacement == PyramidLevelLabelPlacement::OutsideEndLabels ||
                          leaderLines);
        bool valueAtStart = showValueLabels && valuesAvailable &&
                pyramidValueLabelPlacement == PyramidValueLabelPlacement::OutsideStartValues;
        bool valueAtEnd = showValueLabels && valuesAvailable &&
                pyramidValueLabelPlacement == PyramidValueLabelPlacement::OutsideEndValues;

        bool wantsBadge = (badgeShape != PyramidBadgeShape::NoBadge &&
                           badgePlacement == PyramidBadgePlacement::BadgeOnLabelRow);
        double swatch = levelLabelFontSize * 0.72;
        double badgeRoom = wantsBadge ? badgeRadius * 2.2 + 6.0 : 0.0;
        double swatchRoom = showLevelSwatch ? swatch + 8.0 : 0.0;

        ctx->PushState();
        ctx->SetFontSize(levelLabelFontSize);

        for (size_t i = 0; i < geometry.size(); ++i) {
            const auto& geo = geometry[i];
            if (GetLevelProgress(i) <= 0.0f) continue;

            const PyramidLevel& level = pyramidDataSource->GetLevel(geo.dataIndex);

            // A name and a value sharing one column read best as "Referrals (742)"
            auto compose = [&](bool wantsName, bool wantsValue) {
                std::string text = wantsName ? level.levelLabel : std::string();
                if (wantsValue) {
                    std::string value = FormatLevelValue(geo.metrics.value);
                    text = text.empty() ? value : text + " (" + value + ")";
                }
                return text;
            };

            bool isHovered = (static_cast<int>(i) == hoveredLevelIndex);

            for (int side = 0; side < 2; ++side) {
                bool atStart = (side == 0);
                if (atStart && !labelsAtStart) continue;
                if (!atStart && !labelsAtEnd) continue;

                std::string text = atStart ? compose(nameAtStart, valueAtStart)
                                           : compose(nameAtEnd, valueAtEnd);
                if (text.empty()) continue;

                double textRoom = levelLabelColumnWidth - badgeRoom - swatchRoom;
                std::string drawn = TruncateToWidth(ctx, text, textRoom);
                if (drawn.empty()) continue;

                Size2Di size = ctx->GetTextLineDimensions(drawn);
                ctx->SetTextPaint(isHovered ? Color(20, 20, 25, 255) : levelLabelColor);

                if (IsVerticalPyramid()) {
                    double rowCenterY = geo.center.y;
                    double columnRight = cachedPlotArea.x - kColumnGap;
                    double columnLeft = cachedPlotArea.GetRight() + kColumnGap +
                                        (leaderLines ? kConnectorRoom : 0.0);

                    double textX = atStart ? columnRight - size.width
                                           : columnLeft + swatchRoom + badgeRoom;
                    if (atStart) textX -= badgeRoom;

                    if (leaderLines && !atStart) {
                        double edgeX = geo.center.x +
                                       std::min(geo.halfExtentStart, geo.halfExtentEnd);
                        ctx->SetStrokePaint(connectorColor);
                        ctx->SetStrokeWidth(connectorWidth);
                        ctx->DrawLine({edgeX, rowCenterY}, {columnLeft - 6.0, rowCenterY});
                        ctx->SetTextPaint(levelLabelColor);
                    }

                    if (showLevelSwatch) {
                        double swatchX = atStart ? textX - swatch - 8.0
                                                 : columnLeft + badgeRoom;
                        ctx->DrawFilledCircle(Point2Dd(swatchX + swatch / 2.0, rowCenterY),
                                              static_cast<float>(swatch / 2.0), geo.color,
                                              Colors::Transparent, 0.0f);
                    }

                    ctx->DrawText(drawn, Point2Dd(textX, rowCenterY - size.height / 2.0));

                    if (wantsBadge) {
                        double badgeX = atStart ? columnRight - badgeRadius
                                                : columnLeft + badgeRadius;
                        DrawBadgeChip(ctx, Point2Dd(badgeX, rowCenterY), BadgeTextFor(i),
                                      ResolveBadgeFill(geo));
                        ctx->SetFontSize(levelLabelFontSize);
                        ctx->SetTextPaint(levelLabelColor);
                    }

                    if (showLevelListRules) {
                        double ruleY = rowCenterY + size.height / 2.0 + 5.0;
                        double from = atStart ? columnRight - levelLabelColumnWidth : columnLeft;
                        ctx->SetStrokePaint(WithAlpha(levelLabelColor, 0.22));
                        ctx->SetStrokeWidth(1.0);
                        ctx->DrawLine({from, ruleY}, {from + levelLabelColumnWidth, ruleY});
                    }
                } else {
                    double x = geo.center.x - size.width / 2.0;
                    if (atStart) {
                        ctx->DrawText(drawn, Point2Dd(x, cachedPlotArea.y - kColumnGap
                                                         - size.height));
                    } else {
                        double top = cachedPlotArea.GetBottom() + kColumnGap +
                                     (leaderLines ? kConnectorRoom : 0.0);
                        if (leaderLines) {
                            double edgeY = geo.center.y +
                                           std::min(geo.halfExtentStart, geo.halfExtentEnd);
                            ctx->SetStrokePaint(connectorColor);
                            ctx->SetStrokeWidth(connectorWidth);
                            ctx->DrawLine({geo.center.x, edgeY}, {geo.center.x, top - 4.0});
                            ctx->SetTextPaint(levelLabelColor);
                        }
                        ctx->DrawText(drawn, Point2Dd(x, top));
                    }
                }
            }
        }
        ctx->PopState();
    }

    void UltraCanvasPyramidChart::RenderPercentColumn(IRenderContext* ctx) {
        if (!PercentTakesEndColumn() && !PercentTakesStartColumn()) return;

        bool startColumn = PercentTakesStartColumn();

        ctx->PushState();
        ctx->SetFontSize(levelLabelFontSize);
        ctx->SetTextPaint(levelLabelColor);

        for (size_t i = 0; i < geometry.size(); ++i) {
            const auto& geo = geometry[i];
            if (GetLevelProgress(i) < 1.0f) continue;

            std::vector<std::string> lines = BuildPercentLines(geo.metrics);
            if (lines.empty()) continue;

            double lineHeight = ctx->GetTextLineDimensions(lines[0]).height;

            if (IsVerticalPyramid()) {
                double columnX = startColumn
                        ? cachedPlotArea.x - kColumnGap - percentColumnWidth
                        : cachedPlotArea.GetRight() + kColumnGap +
                          (LabelTakesEndColumn() ? levelLabelColumnWidth + kColumnGap : 0.0);
                double top = geo.center.y - lineHeight * lines.size() / 2.0;
                for (size_t l = 0; l < lines.size(); ++l) {
                    ctx->DrawText(lines[l], Point2Dd(columnX, top + lineHeight * l));
                }
            } else {
                double columnY = startColumn
                        ? cachedPlotArea.y - kColumnGap - lineHeight * lines.size()
                        : cachedPlotArea.GetBottom() + kColumnGap +
                          (LabelTakesEndColumn() ? levelLabelFontSize * 2.0 : 0.0);
                for (size_t l = 0; l < lines.size(); ++l) {
                    Size2Di size = ctx->GetTextLineDimensions(lines[l]);
                    ctx->DrawText(lines[l], Point2Dd(geo.center.x - size.width / 2.0,
                                                     columnY + lineHeight * l));
                }
            }
        }
        ctx->PopState();
    }

    void UltraCanvasPyramidChart::DrawBadgeChip(IRenderContext* ctx, const Point2Dd& center,
                                                const std::string& text, const Color& fill) {
        ctx->PushState();
        switch (badgeShape) {
            case PyramidBadgeShape::CircleBadge:
                ctx->DrawFilledCircle(center, static_cast<float>(badgeRadius), fill,
                                      Colors::Transparent, 0.0f);
                break;
            case PyramidBadgeShape::RoundedBadge:
                ctx->DrawFilledRectangle(Rect2Dd(center.x - badgeRadius, center.y - badgeRadius,
                                                 badgeRadius * 2.0, badgeRadius * 2.0),
                                         fill, 0.0f, Colors::Transparent,
                                         static_cast<float>(badgeRadius * 0.45));
                break;
            case PyramidBadgeShape::SquareBadge:
                ctx->DrawFilledRectangle(Rect2Dd(center.x - badgeRadius, center.y - badgeRadius,
                                                 badgeRadius * 2.0, badgeRadius * 2.0),
                                         fill, 0.0f, Colors::Transparent, 0.0f);
                break;
            case PyramidBadgeShape::PlainNumber:
            case PyramidBadgeShape::NoBadge:
            default:
                break;
        }

        ctx->SetFontSize(badgeFontSize);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(badgeShape == PyramidBadgeShape::PlainNumber
                                  ? (badgeColor.a > 0 ? badgeColor : levelLabelColor)
                                  : badgeTextColor);
        DrawCenteredText(ctx, text, center);
        ctx->PopState();
    }

    void UltraCanvasPyramidChart::RenderBadges(IRenderContext* ctx) {
        ctx->PushState();
        for (size_t i = 0; i < geometry.size(); ++i) {
            const auto& geo = geometry[i];
            if (GetLevelProgress(i) < 1.0f) continue;

            double midFlow = (geo.bandStart + geo.bandEnd) / 2.0;
            double half = (geo.halfExtentStart + geo.halfExtentEnd) / 2.0;
            double low, high;
            CrossRangeForExtent(half, geo.crossOffset, low, high);

            double cross = low;
            switch (badgePlacement) {
                case PyramidBadgePlacement::BadgeInsideLevel:
                    cross = low + badgeRadius + 4.0;
                    break;
                case PyramidBadgePlacement::BadgeOutsideStart:
                    cross = -GetCrossHalfSpan() - kColumnGap - badgeRadius;
                    break;
                case PyramidBadgePlacement::BadgeOnEdge:
                default:
                    cross = low;   // Straddling the tier's leading edge
                    break;
            }

            DrawBadgeChip(ctx, MapFlowCross(midFlow, cross), BadgeTextFor(i),
                          ResolveBadgeFill(geo));
        }
        ctx->PopState();
    }

// =============================================================================
// CALLOUTS
// =============================================================================

    void UltraCanvasPyramidChart::LayoutCallouts(IRenderContext* ctx) {
        for (auto& geo : geometry) {
            geo.calloutRect = Rect2Dd(0.0, 0.0, 0.0, 0.0);
        }
        if (calloutStyle == PyramidCalloutStyle::NoCallouts || !IsVerticalPyramid()) return;
        if (!ctx || geometry.empty()) return;

        bool isCard = (calloutStyle == PyramidCalloutStyle::RoundedCardCallouts ||
                       calloutStyle == PyramidCalloutStyle::IconCardCallouts ||
                       calloutStyle == PyramidCalloutStyle::ColorRibbonCallouts);
        double padding = isCard ? calloutPadding : 2.0;
        double iconRoom = (calloutStyle == PyramidCalloutStyle::IconCardCallouts)
                ? badgeRadius * 2.4 : 0.0;
        double ghostRoom = (calloutStyle == PyramidCalloutStyle::NumberedBlockCallouts)
                ? ghostNumberFontSize * 1.1 : 0.0;

        double textWidth = calloutColumnWidth - 2.0 * padding - iconRoom - ghostRoom;
        if (textWidth <= 10.0) return;

        // Measure every block first: the collision pass needs all the heights
        struct Block {
            size_t displayIndex;
            bool atStart;
            double height;
            double desiredCenter;
        };
        std::vector<Block> blocks;
        blocks.reserve(geometry.size());

        for (size_t i = 0; i < geometry.size(); ++i) {
            const PyramidLevel& level = pyramidDataSource->GetLevel(geometry[i].dataIndex);
            std::string title = level.calloutTitle.empty() ? level.levelLabel : level.calloutTitle;
            if (title.empty() && level.description.empty()) continue;

            double height = 2.0 * padding;
            if (!title.empty()) {
                ctx->SetFontSize(calloutTitleFontSize);
                height += ctx->GetTextLineDimensions(title).height;
            }
            if (!level.description.empty()) {
                ctx->SetFontSize(calloutBodyFontSize);
                auto lines = WrapText(ctx, level.description, textWidth);
                if (!lines.empty()) {
                    double lineHeight = ctx->GetTextLineDimensions(lines[0]).height;
                    height += lineHeight * lines.size() + (title.empty() ? 0.0 : 3.0);
                }
            }
            // The icon and the ghost numeral both set a floor on the block height
            height = std::max(height, iconRoom + 2.0);
            if (ghostRoom > 0.0) {
                height = std::max(height, ghostNumberFontSize * 1.35 + padding);
            }

            blocks.push_back({i, CalloutAtStartSide(i), height, geometry[i].center.y});
        }
        if (blocks.empty()) return;

        double top = kEdgePadding + (chartTitle.empty() ? 0.0 : 26.0);
        double bottom = static_cast<double>(GetHeight()) - kEdgePadding;

        // Two independent columns, each packed on its own
        for (int side = 0; side < 2; ++side) {
            bool atStart = (side == 0);
            std::vector<Block*> column;
            for (auto& block : blocks) {
                if (block.atStart == atStart) column.push_back(&block);
            }
            if (column.empty()) continue;

            std::stable_sort(column.begin(), column.end(),
                             [](const Block* a, const Block* b) {
                                 return a->desiredCenter < b->desiredCenter;
                             });

            if (calloutLayout == PyramidCalloutLayout::EvenlyStacked) {
                double totalHeight = 0.0;
                for (const Block* block : column) totalHeight += block->height;
                double slack = (bottom - top) - totalHeight -
                               calloutSpacing * (column.size() - 1);
                double cursor = top + std::max(0.0, slack) / 2.0;
                for (Block* block : column) {
                    block->desiredCenter = cursor + block->height / 2.0;
                    cursor += block->height + calloutSpacing;
                }
            } else {
                // Aligned to the tiers, then pushed apart just enough not to overlap
                double cursor = top;
                for (Block* block : column) {
                    double blockTop = std::max(cursor, block->desiredCenter - block->height / 2.0);
                    block->desiredCenter = blockTop + block->height / 2.0;
                    cursor = blockTop + block->height + calloutSpacing;
                }
                // Anything pushed off the bottom comes back up the same way
                double overflow = cursor - calloutSpacing - bottom;
                if (overflow > 0.0) {
                    double limit = bottom;
                    for (size_t i = column.size(); i-- > 0;) {
                        Block* block = column[i];
                        double blockBottom = std::min(limit,
                                                      block->desiredCenter + block->height / 2.0);
                        block->desiredCenter = blockBottom - block->height / 2.0;
                        limit = blockBottom - block->height - calloutSpacing;
                    }
                }
            }
        }

        double startColumnX = kEdgePadding;
        double endColumnX = static_cast<double>(GetWidth()) - kEdgePadding - calloutColumnWidth;

        for (const auto& block : blocks) {
            auto& geo = geometry[block.displayIndex];
            geo.calloutAtStartSide = block.atStart;
            geo.calloutRect = Rect2Dd(block.atStart ? startColumnX : endColumnX,
                                      block.desiredCenter - block.height / 2.0,
                                      calloutColumnWidth, block.height);

            // The connector leaves from the tier edge nearest its own column
            double midFlow = (geo.bandStart + geo.bandEnd) / 2.0;
            double half = (geo.halfExtentStart + geo.halfExtentEnd) / 2.0;
            double low, high;
            CrossRangeForExtent(half, geo.crossOffset, low, high);
            geo.calloutAnchor = MapFlowCross(midFlow, block.atStart ? low : high);
        }
    }

    void UltraCanvasPyramidChart::RenderCalloutConnector(IRenderContext* ctx,
                                                         const PyramidLevelGeometry& geo) {
        if (connectorStyle == PyramidConnectorStyle::NoConnector) return;
        if (geo.calloutRect.width <= 0.0) return;

        double blockEdgeX = geo.calloutAtStartSide
                ? geo.calloutRect.x + geo.calloutRect.width
                : geo.calloutRect.x;
        double blockCenterY = geo.calloutRect.y + geo.calloutRect.height / 2.0;

        if (connectorStyle == PyramidConnectorStyle::NotchPointer) {
            // A wedge on the block's inner edge instead of a rule across the gap
            double sign = geo.calloutAtStartSide ? 1.0 : -1.0;
            std::vector<Point2Dd> notch = {
                Point2Dd(blockEdgeX, blockCenterY - kNotchSize),
                Point2Dd(blockEdgeX + sign * kNotchSize, blockCenterY),
                Point2Dd(blockEdgeX, blockCenterY + kNotchSize)
            };
            Color fill = (calloutStyle == PyramidCalloutStyle::ColorRibbonCallouts)
                    ? geo.color : connectorColor;
            DrawPolygon(ctx, notch, fill, Colors::Transparent, 0.0);
            return;
        }

        ctx->PushState();
        ctx->SetStrokePaint(connectorColor);
        ctx->SetStrokeWidth(connectorWidth);
        if (connectorStyle == PyramidConnectorStyle::DottedConnector) {
            ctx->SetLineDash(UCDashPattern({2.0, 3.0}));
        }

        double gap = geo.calloutAtStartSide ? 4.0 : -4.0;
        if (connectorStyle == PyramidConnectorStyle::ElbowConnector) {
            double elbowX = (blockEdgeX + geo.calloutAnchor.x) / 2.0;
            ctx->DrawLine(Point2Dd(blockEdgeX + gap, blockCenterY),
                          Point2Dd(elbowX, blockCenterY));
            ctx->DrawLine(Point2Dd(elbowX, blockCenterY),
                          Point2Dd(elbowX, geo.calloutAnchor.y));
            ctx->DrawLine(Point2Dd(elbowX, geo.calloutAnchor.y), geo.calloutAnchor);
        } else {
            ctx->DrawLine(Point2Dd(blockEdgeX + gap, blockCenterY), geo.calloutAnchor);
        }
        ctx->PopState();
    }

    void UltraCanvasPyramidChart::RenderCallouts(IRenderContext* ctx) {
        if (calloutStyle == PyramidCalloutStyle::NoCallouts || !IsVerticalPyramid()) return;

        bool isCard = (calloutStyle == PyramidCalloutStyle::RoundedCardCallouts ||
                       calloutStyle == PyramidCalloutStyle::IconCardCallouts ||
                       calloutStyle == PyramidCalloutStyle::ColorRibbonCallouts);
        bool isRibbon = (calloutStyle == PyramidCalloutStyle::ColorRibbonCallouts);
        double padding = isCard ? calloutPadding : 2.0;
        double iconRoom = (calloutStyle == PyramidCalloutStyle::IconCardCallouts)
                ? badgeRadius * 2.4 : 0.0;
        double ghostRoom = (calloutStyle == PyramidCalloutStyle::NumberedBlockCallouts)
                ? ghostNumberFontSize * 1.1 : 0.0;

        ctx->PushState();

        for (size_t i = 0; i < geometry.size(); ++i) {
            const auto& geo = geometry[i];
            if (geo.calloutRect.width <= 0.0) continue;
            if (GetLevelProgress(i) < 1.0f) continue;

            const PyramidLevel& level = pyramidDataSource->GetLevel(geo.dataIndex);
            bool isHovered = (static_cast<int>(i) == hoveredLevelIndex);

            RenderCalloutConnector(ctx, geo);

            const Rect2Dd& rect = geo.calloutRect;
            if (isCard) {
                Color fill = isRibbon ? geo.color : calloutCardColor;
                Color border = isRibbon
                        ? Colors::Transparent
                        : (calloutBorderColor.a > 0 ? calloutBorderColor : geo.color);
                if (isHovered) fill = Lighten(fill, isRibbon ? 0.12 : 0.02);
                ctx->DrawFilledRectangle(rect, fill, isRibbon ? 0.0f : 1.5f, border,
                                         static_cast<float>(calloutCornerRadius));
            }

            // Icons and ghost numerals sit on the block's inner edge - the side facing
            // the pyramid - so the text column only shifts on the far column.
            double decorationRoom = iconRoom + ghostRoom;
            double textLeft = rect.x + padding +
                              (geo.calloutAtStartSide ? 0.0 : decorationRoom);
            double textWidth = rect.width - 2.0 * padding - decorationRoom;
            if (textWidth <= 6.0) continue;

            if (calloutStyle == PyramidCalloutStyle::NumberedBlockCallouts) {
                ctx->SetFontSize(ghostNumberFontSize);
                ctx->SetFontWeight(FontWeight::Bold);
                ctx->SetTextPaint(WithAlpha(geo.color, ghostNumberAlpha));
                std::string badge = BadgeTextFor(i);
                Size2Di size = ctx->GetTextLineDimensions(badge);
                double numberX = geo.calloutAtStartSide
                        ? rect.x + rect.width - padding - size.width
                        : rect.x + padding;
                ctx->DrawText(badge, Point2Dd(numberX, rect.y + padding));
                ctx->SetFontWeight(FontWeight::Normal);
            }

            if (calloutStyle == PyramidCalloutStyle::IconCardCallouts) {
                Point2Dd iconCenter(
                        geo.calloutAtStartSide
                                ? rect.x + rect.width - padding - badgeRadius
                                : rect.x + padding + badgeRadius,
                        rect.y + rect.height / 2.0);
                ctx->DrawFilledCircle(iconCenter, static_cast<float>(badgeRadius),
                                      WithAlpha(geo.color, 0.16), geo.color, 1.2f);
                if (!level.iconPath.empty()) {
                    double side = badgeRadius * 1.15;
                    ctx->DrawImage(level.iconPath,
                                   Rect2Dd(iconCenter.x - side / 2.0, iconCenter.y - side / 2.0,
                                           side, side),
                                   ImageFitMode::Contain);
                } else {
                    ctx->SetFontSize(badgeFontSize);
                    ctx->SetFontWeight(FontWeight::Bold);
                    ctx->SetTextPaint(geo.color);
                    DrawCenteredText(ctx, BadgeTextFor(i), iconCenter);
                    ctx->SetFontWeight(FontWeight::Normal);
                }
            }

            double cursorY = rect.y + padding;
            std::string title = level.calloutTitle.empty() ? level.levelLabel : level.calloutTitle;

            if (!title.empty()) {
                ctx->SetFontSize(calloutTitleFontSize);
                ctx->SetFontWeight(FontWeight::Bold);
                ctx->SetTextPaint(isRibbon ? PickReadableTextColor(geo.color) : calloutTitleColor);
                std::string drawn = TruncateToWidth(ctx, title, textWidth);
                ctx->DrawText(drawn, Point2Dd(textLeft, cursorY));
                cursorY += ctx->GetTextLineDimensions(drawn).height + 3.0;
                ctx->SetFontWeight(FontWeight::Normal);
            }

            if (!level.description.empty()) {
                ctx->SetFontSize(calloutBodyFontSize);
                ctx->SetTextPaint(isRibbon
                                          ? WithAlpha(PickReadableTextColor(geo.color), 0.88)
                                          : calloutBodyColor);
                auto lines = WrapText(ctx, level.description, textWidth);
                for (const auto& line : lines) {
                    double lineHeight = ctx->GetTextLineDimensions(line).height;
                    if (cursorY + lineHeight > rect.y + rect.height - padding + 1.0) break;
                    ctx->DrawText(line, Point2Dd(textLeft, cursorY));
                    cursorY += lineHeight;
                }
            }
        }
        ctx->PopState();
    }

// =============================================================================
// INTERACTION
// =============================================================================

    bool UltraCanvasPyramidChart::PointInPolygon(const std::vector<Point2Dd>& polygon,
                                                 double x, double y) {
        if (polygon.size() < 3) return false;

        bool inside = false;
        for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
            double xi = polygon[i].x, yi = polygon[i].y;
            double xj = polygon[j].x, yj = polygon[j].y;
            bool crosses = ((yi > y) != (yj > y)) &&
                           (x < (xj - xi) * (y - yi) / (yj - yi) + xi);
            if (crosses) inside = !inside;
        }
        return inside;
    }

    int UltraCanvasPyramidChart::FindLevelAt(const Point2Di& mousePos) const {
        for (size_t i = 0; i < geometry.size(); ++i) {
            if (PointInPolygon(geometry[i].polygon, mousePos.x, mousePos.y)) {
                return static_cast<int>(i);
            }
            if (shapeMode == PyramidShapeMode::Pyramid3D &&
                (PointInPolygon(geometry[i].topFace, mousePos.x, mousePos.y) ||
                 PointInPolygon(geometry[i].sideFace, mousePos.x, mousePos.y))) {
                return static_cast<int>(i);
            }
        }

        // The apex tier is a sliver a few pixels wide; fall back to its band so it
        // can still be hovered.
        double origin = GetFlowOrigin();
        double alongPos = IsVerticalPyramid() ? mousePos.y : mousePos.x;
        double flow = IsReversedFlow() ? origin - alongPos : alongPos - origin;
        double cross = (IsVerticalPyramid() ? mousePos.x : mousePos.y) - GetCrossCenter();

        for (size_t i = 0; i < geometry.size(); ++i) {
            const auto& geo = geometry[i];
            if (flow < geo.bandStart || flow > geo.bandEnd) continue;
            double half = std::max(6.0, std::max(geo.halfExtentStart, geo.halfExtentEnd));
            double low, high;
            CrossRangeForExtent(half, geo.crossOffset, low, high);
            if (cross >= low - 4.0 && cross <= high + 4.0) return static_cast<int>(i);
        }
        return -1;
    }

    int UltraCanvasPyramidChart::FindCalloutAt(const Point2Di& mousePos) const {
        for (size_t i = 0; i < geometry.size(); ++i) {
            const Rect2Dd& rect = geometry[i].calloutRect;
            if (rect.width <= 0.0 || rect.height <= 0.0) continue;
            if (mousePos.x >= rect.x && mousePos.x <= rect.x + rect.width &&
                mousePos.y >= rect.y && mousePos.y <= rect.y + rect.height) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    std::string UltraCanvasPyramidChart::GeneratePyramidTooltip(
            const PyramidLevel& level, const PyramidLevelMetrics& metrics) const {
        // A caller-supplied tooltip is passed through untouched so it can use markup
        if (!level.tooltipText.empty()) return level.tooltipText;

        std::ostringstream oss;
        oss << "<b>" << EscapeMarkup(level.levelLabel) << "</b>";

        if (valuesAvailable) {
            oss << "\n" << EscapeMarkup(FormatLevelValue(metrics.value))
                << "\nOf total: " << EscapeMarkup(FormatPercent(metrics.percentOfTotal))
                << "\nOf base: " << EscapeMarkup(FormatPercent(metrics.percentOfBase));
            if (metrics.levelFromBase > 0) {
                oss << "\nAgainst the tier below: "
                    << EscapeMarkup(FormatPercent(metrics.ratioToLevelBelow));
            }
            oss << "\nThis tier and below: "
                << EscapeMarkup(FormatPercent(metrics.cumulativeFromBase));
        } else {
            oss << "\nLevel " << (metrics.levelFromApex + 1) << " of " << displayOrder.size();
        }

        if (level.HasSegments()) {
            for (const auto& segment : level.segments) {
                oss << "\n  " << EscapeMarkup(segment.label) << ": "
                    << EscapeMarkup(FormatLevelValue(segment.value));
            }
        }

        if (!level.description.empty()) {
            oss << "\n" << EscapeMarkup(level.description);
        }

        return oss.str();
    }

    bool UltraCanvasPyramidChart::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!pyramidDataSource) return false;

        // Hit testing reads the cached geometry, which Render() refreshes. A mouse
        // move can arrive before the first paint, so make sure it is current.
        UpdateRenderingCache();

        int previous = hoveredLevelIndex;

        hoveredLevelIndex = FindLevelAt(mousePos);
        if (hoveredLevelIndex < 0) hoveredLevelIndex = FindCalloutAt(mousePos);

        bool changed = (previous != hoveredLevelIndex);

        if (changed && hoveredLevelIndex >= 0 && onLevelHover) {
            onLevelHover(geometry[hoveredLevelIndex].dataIndex);
        }

        if (enableTooltips) {
            if (hoveredLevelIndex >= 0) {
                const auto& geo = geometry[hoveredLevelIndex];
                std::string text = GeneratePyramidTooltip(
                        pyramidDataSource->GetLevel(geo.dataIndex), geo.metrics);
                Point2Di windowPos = MapFromLocal(Point2Df(mousePos.x, mousePos.y), nullptr);
                UltraCanvasTooltipManager::UpdateAndShowTooltip(GetWindow(), text, windowPos);
                isTooltipActive = true;
            } else if (isTooltipActive) {
                UltraCanvasTooltipManager::HideTooltip();
                isTooltipActive = false;
            }
        }

        if (changed) RequestRedraw();
        return changed;
    }

    bool UltraCanvasPyramidChart::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        if (event.type == UCEventType::MouseLeave) {
            if (hoveredLevelIndex != -1) {
                hoveredLevelIndex = -1;
                RequestRedraw();
            }
            if (isTooltipActive) {
                UltraCanvasTooltipManager::HideTooltip();
                isTooltipActive = false;
            }
            return false;
        }

        if (event.type == UCEventType::MouseDown && event.button == UCMouseButton::Left) {
            UpdateRenderingCache();
            Point2Di mousePos(event.pointer.x, event.pointer.y);

            int level = FindLevelAt(mousePos);
            if (level >= 0 && static_cast<size_t>(level) < geometry.size()) {
                if (onLevelClick) onLevelClick(geometry[level].dataIndex);
            } else {
                int callout = FindCalloutAt(mousePos);
                if (callout >= 0 && onCalloutClick) {
                    onCalloutClick(geometry[callout].dataIndex);
                }
            }
        }

        return UltraCanvasChartElementBase::OnEvent(event);
    }

} // namespace UltraCanvas

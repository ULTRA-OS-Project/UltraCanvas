// Plugins/Charts/UltraCanvasFunnelChart.cpp
// Funnel chart implementation
// Version: 1.1.0
// Last Modified: 2026-08-20
// V1.1.0: legend: migrated to the shared ChartLegend component
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasFunnelChart.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>

namespace UltraCanvas {

namespace {

    constexpr double kColumnGap = 10.0;
    constexpr double kEdgePadding = 12.0;
    constexpr double kMinBandThickness = 5.0;

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

    Color WithAlpha(const Color& color, double factor) {
        return Color(color.r, color.g, color.b,
                     static_cast<uint8_t>(Clamp(color.a * factor, 0.0, 255.0)));
    }

    // Perceived brightness, used to decide between white and dark text on a slab
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

    const std::vector<Color>& DefaultPalette() {
        static const std::vector<Color> palette = {
            Color(58, 160, 165, 255), Color(233, 126, 43, 255), Color(18, 71, 82, 255),
            Color(150, 145, 24, 255), Color(211, 62, 54, 255),  Color(96, 108, 176, 255),
            Color(120, 175, 90, 255), Color(180, 100, 160, 255)
        };
        return palette;
    }

} // anonymous namespace

// =============================================================================
// DATA SOURCE
// =============================================================================

    void FunnelDataSource::LoadFromCSV(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) return;

        stages.clear();

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

            if (fields.size() < 2) continue;

            // Rows whose value column does not parse are skipped - this also swallows
            // a header line without needing to special-case it.
            try {
                FunnelStage stage(fields[0], std::stod(fields[1]));
                if (fields.size() > 2) stage.description = fields[2];
                stages.push_back(stage);
            } catch (const std::exception&) {
                continue;
            }
        }
    }

    FunnelStageMetrics FunnelDataSource::GetMetrics(size_t index) const {
        FunnelStageMetrics metrics;
        if (index >= stages.size()) return metrics;

        metrics.value = stages[index].stageValue;

        double first = stages[0].stageValue;
        if (first != 0.0) metrics.percentOfFirst = metrics.value / first * 100.0;

        if (index > 0) {
            double previous = stages[index - 1].stageValue;
            if (previous != 0.0) metrics.percentOfPrevious = metrics.value / previous * 100.0;
            metrics.dropOff = previous - metrics.value;
            metrics.dropOffPercent = 100.0 - metrics.percentOfPrevious;
        }

        double total = GetTotalValue();
        if (total != 0.0) metrics.percentOfTotal = metrics.value / total * 100.0;

        return metrics;
    }

// =============================================================================
// CONSTRUCTION
// =============================================================================

    UltraCanvasFunnelChart::UltraCanvasFunnelChart(const std::string& id, int x, int y,
                                                   int width, int height)
            : UltraCanvasChartElementBase(id, x, y, width, height) {
        funnelDataSource = CreateFunnelDataSource();
        dataSource = funnelDataSource;   // Base class renders empty state from this

        enableTooltips = true;
        // A funnel has no axes and no grid to draw against
        showGrid = false;
        showAxes = false;
        showValueLabels = true;
        // Animation is opt-in: it drives repaints from inside Render()
        animationEnabled = false;
        animationComplete = true;
        animationDuration = 0.7f;
        palette = DefaultPalette();

        // Legend defaults match the retired private implementation: hidden,
        // top centre, 10px text with font-sized borderless swatches, stage
        // label colour for the entry text.
        legend.SetVisible(false);
        legend.SetPosition(ChartLegendPosition::TopCenter);
        ChartLegendStyle legendStyle;
        legendStyle.fontSize = 10.0f;
        legendStyle.swatchWidth = 10.0f;
        legendStyle.swatchHeight = 10.0f;
        legendStyle.drawSwatchBorder = false;
        legendStyle.textColor = stageLabelColor;
        legend.SetStyle(legendStyle);
    }

// =============================================================================
// DATA
// =============================================================================

    void UltraCanvasFunnelChart::SetFunnelDataSource(std::shared_ptr<FunnelDataSource> data) {
        funnelDataSource = data ? data : CreateFunnelDataSource();
        dataSource = funnelDataSource;
        orderValid = false;
        hoveredStageIndex = -1;
        hoveredConnectorIndex = -1;
        InvalidateCache();
        if (animationEnabled) StartAnimation();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::AddStage(const std::string& label, double value,
                                          const std::string& description) {
        if (!funnelDataSource) return;
        funnelDataSource->AddStage(label, value, description);
        orderValid = false;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::AddStage(const FunnelStage& stage) {
        if (!funnelDataSource) return;
        funnelDataSource->AddStage(stage);
        orderValid = false;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::ClearData() {
        if (!funnelDataSource) return;
        funnelDataSource->Clear();
        displayOrder.clear();
        geometry.clear();
        orderValid = true;
        hoveredStageIndex = -1;
        hoveredConnectorIndex = -1;
        InvalidateCache();
        RequestRedraw();
    }

    FunnelStageMetrics UltraCanvasFunnelChart::GetStageMetrics(size_t dataIndex) const {
        // Prefer the drawn metrics: sorting and the value policy can make them differ
        // from the raw source order.
        for (const auto& geo : geometry) {
            if (geo.dataIndex == dataIndex) return geo.metrics;
        }
        return funnelDataSource ? funnelDataSource->GetMetrics(dataIndex) : FunnelStageMetrics();
    }

    double UltraCanvasFunnelChart::GetOverallConversion() const {
        if (!funnelDataSource || funnelDataSource->GetPointCount() < 2) return 100.0;
        const auto& stages = funnelDataSource->GetStages();
        double first = stages.front().stageValue;
        if (first == 0.0) return 0.0;
        return stages.back().stageValue / first * 100.0;
    }

    size_t UltraCanvasFunnelChart::GetBottleneckStage() const {
        if (!funnelDataSource || funnelDataSource->GetPointCount() < 2) return SIZE_MAX;

        const auto& stages = funnelDataSource->GetStages();
        size_t worst = SIZE_MAX;
        double worstConversion = 0.0;
        for (size_t i = 1; i < stages.size(); ++i) {
            double previous = stages[i - 1].stageValue;
            if (previous == 0.0) continue;
            double conversion = stages[i].stageValue / previous * 100.0;
            if (worst == SIZE_MAX || conversion < worstConversion) {
                worst = i;
                worstConversion = conversion;
            }
        }
        return worst;
    }

    void UltraCanvasFunnelChart::SetSortOrder(FunnelSortOrder order) {
        if (sortOrder == order) return;
        sortOrder = order;
        orderValid = false;
        hoveredStageIndex = -1;
        hoveredConnectorIndex = -1;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetValuePolicy(FunnelValuePolicy policy) {
        if (valuePolicy == policy) return;
        valuePolicy = policy;
        orderValid = false;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::RebuildDisplayOrder() {
        displayOrder.clear();
        if (!funnelDataSource) {
            orderValid = true;
            return;
        }

        size_t count = funnelDataSource->GetPointCount();
        displayOrder.reserve(count);
        for (size_t i = 0; i < count; ++i) displayOrder.push_back(i);

        const auto& stages = funnelDataSource->GetStages();

        FunnelSortOrder effective = sortOrder;
        // The policy wins over InsertionOrder but never overrides an explicit sort
        if (valuePolicy == FunnelValuePolicy::AutoSortDescending &&
            sortOrder == FunnelSortOrder::InsertionOrder) {
            effective = FunnelSortOrder::ValueDescending;
        }

        if (effective != FunnelSortOrder::InsertionOrder) {
            std::stable_sort(displayOrder.begin(), displayOrder.end(),
                             [&stages, effective](size_t a, size_t b) {
                switch (effective) {
                    case FunnelSortOrder::ValueDescending: return stages[b].stageValue < stages[a].stageValue;
                    case FunnelSortOrder::ValueAscending:  return stages[a].stageValue < stages[b].stageValue;
                    case FunnelSortOrder::LabelAscending:  return stages[a].stageLabel < stages[b].stageLabel;
                    case FunnelSortOrder::LabelDescending: return stages[b].stageLabel < stages[a].stageLabel;
                    default:                               return a < b;
                }
            });
        }

        orderValid = true;
    }

// =============================================================================
// SHAPE CONFIGURATION
// =============================================================================

    void UltraCanvasFunnelChart::SetShapeMode(FunnelShapeMode mode) {
        shapeMode = mode;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetOrientation(FunnelOrientation value) {
        orientation = value;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetScaleMode(FunnelScaleMode mode) {
        scaleMode = mode;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetTipMode(FunnelTipMode mode) {
        tipMode = mode;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetAlignment(FunnelAlignment value) {
        alignment = value;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetNeckRatio(double ratio) {
        neckRatio = Clamp(ratio, 0.0, 1.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetMinStageExtent(double extent) {
        minStageExtent = Clamp(extent, 0.0, 400.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetStageGap(double gap) {
        stageGap = Clamp(gap, 0.0, 200.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetStageThickness(double thickness) {
        stageThickness = Clamp(thickness, 6.0, 400.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetAutoFitStages(bool autoFit) {
        autoFitStages = autoFit;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetCornerRadius(double radius) {
        cornerRadius = Clamp(radius, 0.0, 60.0);
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetSlabSkew(double skew) {
        slabSkew = Clamp(skew, -120.0, 120.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetShowTipTriangle(bool show) {
        showTipTriangle = show;
        InvalidateCache();
        RequestRedraw();
    }

    double UltraCanvasFunnelChart::GetPreferredThickness() const {
        size_t count = GetStageCount();
        if (count == 0) return GetMarginFlowStart() + GetMarginFlowEnd() + 40.0;
        double gap = (shapeMode == FunnelShapeMode::ContinuousFunnel) ? 0.0 : stageGap;
        return GetMarginFlowStart() + GetMarginFlowEnd() +
               count * stageThickness + (count - 1) * gap;
    }

// =============================================================================
// COLOUR CONFIGURATION
// =============================================================================

    void UltraCanvasFunnelChart::SetColorMode(FunnelColorMode mode) {
        colorMode = mode;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetColorRamp(const Color& startColor, const Color& endColor) {
        rampStartColor = startColor;
        rampEndColor = endColor;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetBaseColor(const Color& color) {
        baseColor = color;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetPalette(const std::vector<Color>& colors) {
        palette = colors.empty() ? DefaultPalette() : colors;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetUseGradientFill(bool use, double lightening) {
        useGradientFill = use;
        gradientLightening = Clamp(lightening, 0.0, 1.0);
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetShowStageBorder(bool show, const Color& color, double width) {
        showStageBorder = show;
        stageBorderColor = color;
        stageBorderWidth = Clamp(width, 0.0, 12.0);
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetDimUnhoveredStages(bool dim) {
        dimUnhoveredStages = dim;
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetConversionThresholds(double goodPercent, double warnPercent) {
        goodConversionThreshold = goodPercent;
        warnConversionThreshold = warnPercent;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetConversionColors(const Color& good, const Color& warn,
                                                     const Color& poor) {
        goodConversionColor = good;
        warnConversionColor = warn;
        poorConversionColor = poor;
        InvalidateCache();
        RequestRedraw();
    }

// =============================================================================
// LABEL CONFIGURATION
// =============================================================================

    void UltraCanvasFunnelChart::SetStageLabelPlacement(FunnelStageLabelPlacement placement) {
        stageLabelPlacement = placement;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetFunnelValueLabelPlacement(FunnelValueLabelPlacement placement) {
        funnelValueLabelPlacement = placement;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetPercentPlacement(FunnelPercentPlacement placement) {
        percentPlacement = placement;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetStageLabelColumnWidth(double width) {
        stageLabelColumnWidth = Clamp(width, 20.0, 500.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetPercentColumnWidth(double width) {
        percentColumnWidth = Clamp(width, 20.0, 400.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetDescriptionColumnWidth(double width) {
        descriptionColumnWidth = Clamp(width, 0.0, 600.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetStageLabelFontSize(double size) {
        stageLabelFontSize = Clamp(size, 6.0, 40.0);
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetStageLabelColor(const Color& color) {
        stageLabelColor = color;
        // The legacy legend drew its entry text in the stage label colour
        legend.GetStyle().textColor = color;
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetTitleColor(const Color& color) {
        titleColor = color;
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetDescriptionStyle(double fontSize, const Color& color) {
        descriptionFontSize = Clamp(fontSize, 6.0, 30.0);
        descriptionColor = color;
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetShowDescriptionLeaderLines(bool show) {
        showDescriptionLeaderLines = show;
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetValueFormat(const std::string& format) {
        valueFormat = format;
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetPercentFormat(const std::string& format) {
        percentFormat = format;
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetAbbreviateValues(bool abbreviate) {
        abbreviateValues = abbreviate;
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetInsideLabelColor(const Color& color) {
        insideLabelColor = color;
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetAutoContrastInsideLabels(bool autoContrast) {
        autoContrastInsideLabels = autoContrast;
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetShowPercentOfFirst(bool show) {
        showPercentOfFirst = show;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetShowPercentOfPrevious(bool show) {
        showPercentOfPrevious = show;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetShowPercentOfTotal(bool show) {
        showPercentOfTotal = show;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetShowDropOffLabels(bool show) {
        showDropOffLabels = show;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetShowConversionOnConnector(bool show) {
        showConversionOnConnector = show;
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetConnectorColors(const Color& fill, const Color& label) {
        connectorColor = fill;
        connectorLabelColor = label;
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetShowBadges(bool show, double fontSize) {
        showBadges = show;
        badgeFontSize = Clamp(fontSize, 8.0, 60.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetBadgeColor(const Color& color) {
        badgeColor = color;
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetLeaderLineColor(const Color& color) {
        leaderLineColor = color;
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetShowTerminalCallout(bool show, double radius) {
        showTerminalCallout = show;
        terminalCalloutRadius = Clamp(radius, 8.0, 90.0);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetTerminalCalloutColor(const Color& color) {
        terminalCalloutColor = color;
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetShowTargetOverlay(bool show) {
        showTargetOverlay = show;
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetTargetOverlayColor(const Color& color) {
        targetOverlayColor = color;
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetHighlightBottleneck(bool highlight) {
        highlightBottleneck = highlight;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetBottleneckColor(const Color& color) {
        bottleneckColor = color;
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetShowLegend(bool show) {
        legend.SetVisible(show);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetLegendPosition(FunnelLegendPosition position) {
        // Closest ChartLegendPosition match: Left/Center/Right map onto
        // Start/Center/End along the same edge.
        switch (position) {
            case FunnelLegendPosition::LegendTopLeft:
                legend.SetPosition(ChartLegendPosition::TopStart); break;
            case FunnelLegendPosition::LegendTopRight:
                legend.SetPosition(ChartLegendPosition::TopEnd); break;
            case FunnelLegendPosition::LegendBottomLeft:
                legend.SetPosition(ChartLegendPosition::BottomStart); break;
            case FunnelLegendPosition::LegendBottomCenter:
                legend.SetPosition(ChartLegendPosition::BottomCenter); break;
            case FunnelLegendPosition::LegendBottomRight:
                legend.SetPosition(ChartLegendPosition::BottomEnd); break;
            case FunnelLegendPosition::LegendTopCenter:
            default:
                legend.SetPosition(ChartLegendPosition::TopCenter); break;
        }
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetLegendFontSize(double size) {
        double clamped = Clamp(size, 6.0, 24.0);
        ChartLegendStyle& style = legend.GetStyle();
        style.fontSize = static_cast<float>(clamped);
        // The legacy legend sized its swatches to the font
        style.swatchWidth = static_cast<float>(clamped);
        style.swatchHeight = static_cast<float>(clamped);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetCardStyle(const Color& fill, const Color& border, double radius) {
        cardFillColor = fill;
        cardBorderColor = border;
        cardCornerRadius = Clamp(radius, 0.0, 60.0);
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetAnimationEnabled(bool enable) {
        animationEnabled = enable;
        if (enable) {
            StartAnimation();
        } else {
            animationComplete = true;
        }
        RequestRedraw();
    }

    void UltraCanvasFunnelChart::SetAnimationDuration(float seconds) {
        animationDuration = std::max(0.05f, seconds);
    }

    void UltraCanvasFunnelChart::RestartAnimation() {
        if (!animationEnabled) return;
        StartAnimation();
        RequestRedraw();
    }

// =============================================================================
// LAYOUT
// =============================================================================

// Stage names and values placed on the same side share one column, so both have
// to be considered when reserving the margin for it.
    bool UltraCanvasFunnelChart::OutsideTextTakesStartColumn() const {
        return stageLabelPlacement == FunnelStageLabelPlacement::OutsideStartLabels ||
               (showValueLabels &&
                funnelValueLabelPlacement == FunnelValueLabelPlacement::OutsideStartValues);
    }

    bool UltraCanvasFunnelChart::OutsideTextTakesEndColumn() const {
        return stageLabelPlacement == FunnelStageLabelPlacement::OutsideEndLabels ||
               stageLabelPlacement == FunnelStageLabelPlacement::LeaderLineLabels ||
               (showValueLabels &&
                funnelValueLabelPlacement == FunnelValueLabelPlacement::OutsideEndValues);
    }

    bool UltraCanvasFunnelChart::PercentTakesEndColumn() const {
        return percentPlacement == FunnelPercentPlacement::OutsideEndPercent &&
               (showPercentOfFirst || showPercentOfPrevious || showPercentOfTotal ||
                showDropOffLabels);
    }

    bool UltraCanvasFunnelChart::PercentTakesStartColumn() const {
        return percentPlacement == FunnelPercentPlacement::OutsideStartPercent &&
               (showPercentOfFirst || showPercentOfPrevious || showPercentOfTotal ||
                showDropOffLabels);
    }

// The shared legend always occupies the top or bottom screen edge, so its
// measured inset comes out of the flow margins of a vertical funnel and out of
// the cross margins of a horizontal one.
    double UltraCanvasFunnelChart::GetMarginStart() const {
        double margin = kEdgePadding;
        if (OutsideTextTakesStartColumn()) margin += stageLabelColumnWidth + kColumnGap;
        if (PercentTakesStartColumn()) margin += percentColumnWidth + kColumnGap;
        if (showBadges) margin += badgeFontSize * 2.2 + kColumnGap;
        if (!IsVertical() && LegendAtTop()) margin += legendInset;
        return margin;
    }

    double UltraCanvasFunnelChart::GetMarginEnd() const {
        double margin = kEdgePadding;
        if (OutsideTextTakesEndColumn()) margin += stageLabelColumnWidth + kColumnGap;
        if (PercentTakesEndColumn()) margin += percentColumnWidth + kColumnGap;
        if (descriptionColumnWidth > 0.0) margin += descriptionColumnWidth + kColumnGap;
        if (stageLabelPlacement == FunnelStageLabelPlacement::LeaderLineLabels) {
            margin += 26.0;   // Room for the leader lines themselves
        }
        if (!IsVertical() && !LegendAtTop()) margin += legendInset;
        return margin;
    }

    double UltraCanvasFunnelChart::GetMarginFlowStart() const {
        double margin = chartTitle.empty() ? kEdgePadding : 36.0;
        if (IsVertical() && LegendAtTop()) margin += legendInset;
        return margin;
    }

    double UltraCanvasFunnelChart::GetMarginFlowEnd() const {
        double margin = kEdgePadding;
        if (IsVertical() && !LegendAtTop()) margin += legendInset;
        if (showTerminalCallout) margin += terminalCalloutRadius * 2.0 + 16.0;
        if (showTipTriangle) margin += 26.0;
        return margin;
    }

// =============================================================================
// LEGEND (shared ChartLegend component)
// =============================================================================

    bool UltraCanvasFunnelChart::LegendAtTop() const {
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
// padding, starting below the title.
    Rect2Dd UltraCanvasFunnelChart::LegendArea() const {
        double top = chartTitle.empty() ? kEdgePadding : 36.0;
        return Rect2Dd(kEdgePadding, top,
                       std::max(1.0, static_cast<double>(GetWidth()) - 2.0 * kEdgePadding),
                       std::max(1.0, static_cast<double>(GetHeight()) - top - kEdgePadding));
    }

// The legend explains the stages: same labels, same resolved colours (palette
// index and all), in drawn order. Rebuilt with the geometry so the two can
// never disagree.
    void UltraCanvasFunnelChart::RebuildLegendEntries() {
        std::vector<ChartLegendEntry> entries;
        entries.reserve(geometry.size());
        for (const auto& geo : geometry) {
            entries.emplace_back(funnelDataSource->GetStage(geo.dataIndex).stageLabel,
                                 geo.color, LegendSwatch::Square);
        }
        legend.SetEntries(entries);
    }

// Measure the legend and reserve the space it consumes. The measured inset
// feeds the margins, so a change re-runs the geometry pass before the frame
// is drawn - Measure() is cached, so this settles immediately.
    void UltraCanvasFunnelChart::UpdateLegendLayout(IRenderContext* ctx) {
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

    ChartPlotArea UltraCanvasFunnelChart::CalculatePlotArea() {
        double crossStart = GetMarginStart();
        double crossEnd = GetMarginEnd();
        double flowStart = GetMarginFlowStart();
        double flowEnd = GetMarginFlowEnd();

        double w = static_cast<double>(GetWidth());
        double h = static_cast<double>(GetHeight());

        // Never hand back a degenerate rectangle - callers divide by width/height
        if (IsVertical()) {
            return ChartPlotArea(crossStart, flowStart,
                                 std::max(1.0, w - crossStart - crossEnd),
                                 std::max(1.0, h - flowStart - flowEnd));
        }
        return ChartPlotArea(flowStart, crossStart,
                             std::max(1.0, w - flowStart - flowEnd),
                             std::max(1.0, h - crossStart - crossEnd));
    }

    ChartDataBounds UltraCanvasFunnelChart::CalculateDataBounds() {
        ChartDataBounds bounds;
        size_t count = GetStageCount();
        if (count == 0) return bounds;
        return ChartDataBounds(0.0, static_cast<double>(count - 1),
                               0.0, funnelDataSource->GetMaxValue());
    }

    double UltraCanvasFunnelChart::GetFlowStart() const {
        return IsVertical() ? cachedPlotArea.y : cachedPlotArea.x;
    }

    double UltraCanvasFunnelChart::GetFlowLength() const {
        return IsVertical() ? cachedPlotArea.height : cachedPlotArea.width;
    }

    double UltraCanvasFunnelChart::GetCrossCenter() const {
        return IsVertical() ? cachedPlotArea.x + cachedPlotArea.width / 2.0
                            : cachedPlotArea.y + cachedPlotArea.height / 2.0;
    }

    double UltraCanvasFunnelChart::GetCrossHalfSpan() const {
        return (IsVertical() ? cachedPlotArea.width : cachedPlotArea.height) / 2.0;
    }

    Point2Dd UltraCanvasFunnelChart::MapFlowCross(double flow, double cross) const {
        double center = GetCrossCenter();
        if (IsVertical()) return Point2Dd(center + cross, flow);
        return Point2Dd(flow, center + cross);
    }

    void UltraCanvasFunnelChart::CrossRangeForExtent(double halfExtent, double& low,
                                                     double& high) const {
        double span = GetCrossHalfSpan();
        switch (alignment) {
            case FunnelAlignment::StartAligned:
                low = -span;
                high = -span + 2.0 * halfExtent;
                break;
            case FunnelAlignment::EndAligned:
                high = span;
                low = span - 2.0 * halfExtent;
                break;
            case FunnelAlignment::CenterAligned:
            default:
                low = -halfExtent;
                high = halfExtent;
                break;
        }
    }

    double UltraCanvasFunnelChart::HalfExtentForValue(double value) const {
        if (scaleMode == FunnelScaleMode::EqualStages) return maxHalfExtent;
        if (maxStageValue <= 0.0) return maxHalfExtent;
        double half = maxHalfExtent * (value / maxStageValue);
        return std::max(half, minStageExtent / 2.0);
    }

    double UltraCanvasFunnelChart::TipHalfExtent(double lastStageHalfExtent) const {
        switch (tipMode) {
            case FunnelTipMode::PointTip:   return 0.0;
            case FunnelTipMode::FlatNeck:   return maxHalfExtent * neckRatio;
            case FunnelTipMode::MinWidthTip:
            default:                        return lastStageHalfExtent;
        }
    }

    void UltraCanvasFunnelChart::FillPolygon(std::vector<Point2Dd>& out, double bandStart,
                                             double bandEnd, double halfStart, double halfEnd,
                                             double skew) const {
        double lowStart, highStart, lowEnd, highEnd;
        CrossRangeForExtent(halfStart, lowStart, highStart);
        CrossRangeForExtent(halfEnd, lowEnd, highEnd);

        out.clear();
        out.reserve(4);
        out.push_back(MapFlowCross(bandStart, lowStart + skew));
        out.push_back(MapFlowCross(bandStart, highStart + skew));
        out.push_back(MapFlowCross(bandEnd, highEnd - skew));
        out.push_back(MapFlowCross(bandEnd, lowEnd - skew));
    }

    std::vector<double> UltraCanvasFunnelChart::ComputeDrawValues() const {
        std::vector<double> values;
        values.reserve(displayOrder.size());
        for (size_t index : displayOrder) {
            values.push_back(std::max(0.0, funnelDataSource->GetStage(index).stageValue));
        }

        if (valuePolicy == FunnelValuePolicy::ClampToPrevious) {
            // A funnel that widens is almost always a data error; draw it narrowing
            // while the tooltips keep reporting the real figures.
            for (size_t i = 1; i < values.size(); ++i) {
                values[i] = std::min(values[i], values[i - 1]);
            }
        }
        return values;
    }

    std::vector<double> UltraCanvasFunnelChart::ComputeBandThicknesses(
            const std::vector<double>& values, double available) const {
        size_t n = values.size();
        std::vector<double> thicknesses(n, 0.0);
        if (n == 0 || available <= 0.0) return thicknesses;

        std::vector<double> weights(n, 1.0);
        double weightSum = 0.0;

        if (scaleMode == FunnelScaleMode::HeightProportional) {
            for (size_t i = 0; i < n; ++i) weights[i] = std::max(0.0, values[i]);
        } else if (scaleMode == FunnelScaleMode::AreaProportional) {
            // Slab area is thickness * mean breadth, so dividing the value by the
            // mean breadth is what makes the drawn area track the value.
            for (size_t i = 0; i < n; ++i) {
                double halfHere = HalfExtentForValue(values[i]);
                double halfNext = (i + 1 < n) ? HalfExtentForValue(values[i + 1])
                                              : TipHalfExtent(halfHere);
                double meanHalf = std::max(0.5, (halfHere + halfNext) / 2.0);
                weights[i] = std::max(0.0, values[i]) / meanHalf;
            }
        }

        for (double w : weights) weightSum += w;
        if (weightSum <= 0.0) {
            std::fill(weights.begin(), weights.end(), 1.0);
            weightSum = static_cast<double>(n);
        }

        for (size_t i = 0; i < n; ++i) thicknesses[i] = available * weights[i] / weightSum;

        // A stage whose value rounds to nothing still has to be visible and clickable.
        double minThickness = std::min(kMinBandThickness, available / n);
        double deficit = 0.0;
        double surplusPool = 0.0;
        for (size_t i = 0; i < n; ++i) {
            if (thicknesses[i] < minThickness) {
                deficit += minThickness - thicknesses[i];
                thicknesses[i] = minThickness;
            } else {
                surplusPool += thicknesses[i] - minThickness;
            }
        }
        if (deficit > 0.0 && surplusPool > 0.0) {
            double scale = std::max(0.0, (surplusPool - deficit) / surplusPool);
            for (size_t i = 0; i < n; ++i) {
                if (thicknesses[i] > minThickness) {
                    thicknesses[i] = minThickness + (thicknesses[i] - minThickness) * scale;
                }
            }
        }

        return thicknesses;
    }

    void UltraCanvasFunnelChart::RebuildGeometry() {
        geometry.clear();
        bottleneckIndex = -1;
        if (!funnelDataSource || displayOrder.empty()) {
            legend.ClearEntries();
            return;
        }

        size_t n = displayOrder.size();
        maxStageValue = 0.0;
        totalStageValue = 0.0;
        for (size_t index : displayOrder) {
            double value = std::max(0.0, funnelDataSource->GetStage(index).stageValue);
            maxStageValue = std::max(maxStageValue, value);
            totalStageValue += value;
        }
        maxHalfExtent = std::max(1.0, GetCrossHalfSpan());

        std::vector<double> values = ComputeDrawValues();

        double gap = (shapeMode == FunnelShapeMode::ContinuousFunnel) ? 0.0 : stageGap;
        double flowLength = GetFlowLength();
        double available = autoFitStages
                ? std::max(1.0, flowLength - gap * (n - 1))
                : stageThickness * n;

        std::vector<double> thicknesses = ComputeBandThicknesses(values, available);

        // Slabs taper inside their own band in the continuous shapes; the segmented
        // shapes keep a constant breadth and hand the taper to the connector.
        bool taperWithinStage = (shapeMode == FunnelShapeMode::ContinuousFunnel ||
                                 shapeMode == FunnelShapeMode::CardRows ||
                                 shapeMode == FunnelShapeMode::SkewedInfographic);
        bool flatBars = (shapeMode == FunnelShapeMode::BarStyleFunnel);

        std::vector<double> halfExtents(n, maxHalfExtent);
        for (size_t i = 0; i < n; ++i) halfExtents[i] = HalfExtentForValue(values[i]);
        double tipHalf = TipHalfExtent(halfExtents.empty() ? maxHalfExtent : halfExtents[n - 1]);

        // In HeightProportional mode the outline tapers evenly with distance rather
        // than with the values, which is what gives the solid cone look.
        double heightModeTip = (tipMode == FunnelTipMode::PointTip) ? 0.0
                                                                   : maxHalfExtent * neckRatio;
        auto extentAtFlow = [&](double flow) {
            double t = Clamp((flow - GetFlowStart()) / std::max(1.0, flowLength), 0.0, 1.0);
            return maxHalfExtent + (heightModeTip - maxHalfExtent) * t;
        };

        double cursor = GetFlowStart();
        geometry.reserve(n);

        for (size_t i = 0; i < n; ++i) {
            FunnelStageGeometry geo;
            geo.dataIndex = displayOrder[i];
            geo.bandStart = cursor;
            geo.bandEnd = cursor + thicknesses[i];

            if (scaleMode == FunnelScaleMode::HeightProportional) {
                geo.halfExtentStart = extentAtFlow(geo.bandStart);
                geo.halfExtentEnd = extentAtFlow(geo.bandEnd);
            } else if (flatBars) {
                geo.halfExtentStart = geo.halfExtentEnd = halfExtents[i];
            } else if (taperWithinStage) {
                geo.halfExtentStart = halfExtents[i];
                geo.halfExtentEnd = (i + 1 < n) ? halfExtents[i + 1] : tipHalf;
            } else {
                geo.halfExtentStart = geo.halfExtentEnd = halfExtents[i];
            }

            double skew = (shapeMode == FunnelShapeMode::SkewedInfographic)
                    ? slabSkew * ((i % 2 == 0) ? 1.0 : -1.0)
                    : 0.0;
            FillPolygon(geo.polygon, geo.bandStart, geo.bandEnd,
                        geo.halfExtentStart, geo.halfExtentEnd, skew);

            double midFlow = (geo.bandStart + geo.bandEnd) / 2.0;
            double lowMid, highMid;
            CrossRangeForExtent((geo.halfExtentStart + geo.halfExtentEnd) / 2.0, lowMid, highMid);
            geo.center = MapFlowCross(midFlow, (lowMid + highMid) / 2.0);

            // Metrics always describe the drawn neighbours, so a sorted funnel still
            // reports the conversion the reader can actually see.
            geo.metrics.value = funnelDataSource->GetStage(geo.dataIndex).stageValue;
            double first = funnelDataSource->GetStage(displayOrder[0]).stageValue;
            if (first != 0.0) geo.metrics.percentOfFirst = geo.metrics.value / first * 100.0;
            if (i > 0) {
                double previous = funnelDataSource->GetStage(displayOrder[i - 1]).stageValue;
                if (previous != 0.0) {
                    geo.metrics.percentOfPrevious = geo.metrics.value / previous * 100.0;
                }
                geo.metrics.dropOff = previous - geo.metrics.value;
                geo.metrics.dropOffPercent = 100.0 - geo.metrics.percentOfPrevious;
            }
            if (totalStageValue != 0.0) {
                geo.metrics.percentOfTotal = geo.metrics.value / totalStageValue * 100.0;
            }

            geometry.push_back(std::move(geo));
            cursor = geometry.back().bandEnd + gap;
        }

        // Connector necks live in the gaps of the segmented shapes
        bool wantsConnectors = (gap > 0.0) && (shapeMode == FunnelShapeMode::SegmentedBars ||
                                               shapeMode == FunnelShapeMode::CardRows);
        if (wantsConnectors) {
            for (size_t i = 0; i + 1 < n; ++i) {
                auto& geo = geometry[i];
                double startFlow = geo.bandEnd;
                double endFlow = geometry[i + 1].bandStart;
                FillPolygon(geo.connectorPolygon, startFlow, endFlow,
                            geo.halfExtentEnd, geometry[i + 1].halfExtentStart, 0.0);

                double midFlow = (startFlow + endFlow) / 2.0;
                double low, high;
                CrossRangeForExtent((geo.halfExtentEnd + geometry[i + 1].halfExtentStart) / 2.0,
                                    low, high);
                geo.connectorCenter = MapFlowCross(midFlow, (low + high) / 2.0);
            }
        }

        // Worst conversion among the drawn stages
        if (n > 1) {
            double worst = 0.0;
            for (size_t i = 1; i < n; ++i) {
                if (bottleneckIndex < 0 || geometry[i].metrics.percentOfPrevious < worst) {
                    bottleneckIndex = static_cast<int>(i);
                    worst = geometry[i].metrics.percentOfPrevious;
                }
            }
        }

        // Colours depend on the metrics, so they are resolved once everything is known
        for (size_t i = 0; i < n; ++i) {
            geometry[i].color = ResolveStageColor(i, geometry[i].dataIndex, geometry[i].metrics);
        }

        RebuildLegendEntries();
    }

    void UltraCanvasFunnelChart::UpdateRenderingCache() {
        if (!orderValid) RebuildDisplayOrder();
        if (cacheValid) return;

        UltraCanvasChartElementBase::UpdateRenderingCache();
        RebuildGeometry();
    }

// =============================================================================
// COLOUR
// =============================================================================

    Color UltraCanvasFunnelChart::ResolveStageColor(size_t displayIndex, size_t dataIndex,
                                                    const FunnelStageMetrics& metrics) const {
        // An explicit per-stage colour always wins, whatever the mode
        const FunnelStage& stage = funnelDataSource->GetStage(dataIndex);
        if (stage.stageColor.a > 0) return stage.stageColor;

        size_t n = std::max<size_t>(1, displayOrder.size());

        switch (colorMode) {
            case FunnelColorMode::CategoricalPalette:
                return palette.empty() ? baseColor : palette[displayIndex % palette.size()];

            case FunnelColorMode::SingleHue:
            case FunnelColorMode::PerStageOverride:
                return baseColor;

            case FunnelColorMode::ThresholdColor: {
                // The first stage has nothing to convert from, so it stays neutral
                if (displayIndex == 0) return baseColor;
                if (metrics.percentOfPrevious >= goodConversionThreshold) return goodConversionColor;
                if (metrics.percentOfPrevious >= warnConversionThreshold) return warnConversionColor;
                return poorConversionColor;
            }

            case FunnelColorMode::SequentialRamp:
            default: {
                double t = (n <= 1) ? 0.0 : static_cast<double>(displayIndex) / (n - 1);
                return LerpColor(rampStartColor, rampEndColor, t);
            }
        }
    }

    Color UltraCanvasFunnelChart::PickReadableTextColor(const Color& background) const {
        if (!autoContrastInsideLabels) return insideLabelColor;
        return Luminance(background) > 0.62 ? Color(40, 45, 50, 255) : Color(255, 255, 255, 255);
    }

// =============================================================================
// TEXT HELPERS
// =============================================================================

    std::string UltraCanvasFunnelChart::FormatStageValue(double value) const {
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

    std::string UltraCanvasFunnelChart::FormatPercent(double percent) const {
        char buffer[128];
        int written = std::snprintf(buffer, sizeof(buffer), percentFormat.c_str(), percent);
        if (written <= 0) {
            std::snprintf(buffer, sizeof(buffer), "%.1f%%", percent);
        }
        return std::string(buffer);
    }

    std::string UltraCanvasFunnelChart::TruncateToWidth(IRenderContext* ctx,
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

    void UltraCanvasFunnelChart::DrawCenteredText(IRenderContext* ctx, const std::string& text,
                                                   const Point2Dd& center) const {
        if (text.empty()) return;
        Size2Di size = ctx->GetTextLineDimensions(text);
        ctx->DrawText(text, Point2Dd(center.x - size.width / 2.0, center.y - size.height / 2.0));
    }

// =============================================================================
// RENDERING
// =============================================================================

    void UltraCanvasFunnelChart::RenderCommonBackground(IRenderContext* ctx) {
        if (!ctx) return;

        if (showBackground) {
            ctx->DrawFilledRectangle(GetLocalBounds(), backgroundColor);
        }
        RenderTitle(ctx);
    }

    void UltraCanvasFunnelChart::RenderTitle(IRenderContext* ctx) {
        if (chartTitle.empty()) return;

        ctx->PushState();
        // A dark chart background needs light title text, and the caller only has
        // to say so explicitly when the automatic choice is wrong.
        Color color = titleColor.a > 0
                ? titleColor
                : (Luminance(backgroundColor) < 0.5 ? Color(235, 232, 245, 255)
                                                    : Color(60, 60, 66, 255));
        ctx->SetTextPaint(color);
        ctx->SetFontSize(14.0);
        ctx->SetFontWeight(FontWeight::Bold);
        Size2Di size = ctx->GetTextLineDimensions(chartTitle);
        double x = (static_cast<double>(GetWidth()) - size.width) / 2.0;
        ctx->DrawText(chartTitle, Point2Dd(std::max(4.0, x), 10.0));
        ctx->PopState();
    }

    float UltraCanvasFunnelChart::GetStageProgress(size_t displayIndex) const {
        if (!animationEnabled || animationComplete) return 1.0f;

        float t = GetAnimationProgress();
        float count = static_cast<float>(displayOrder.size()) + 1.0f;
        float start = static_cast<float>(displayIndex) / count;
        float span = 2.0f / count;
        return static_cast<float>(Clamp((t - start) / span, 0.0, 1.0));
    }

    std::vector<Point2Dd> UltraCanvasFunnelChart::ScalePolygon(const std::vector<Point2Dd>& points,
                                                               float progress) const {
        if (progress >= 1.0f) return points;

        // Stages grow out from whichever edge the funnel is anchored to
        double span = GetCrossHalfSpan();
        double center = GetCrossCenter();
        double anchor = center;
        if (alignment == FunnelAlignment::StartAligned) anchor = center - span;
        else if (alignment == FunnelAlignment::EndAligned) anchor = center + span;

        std::vector<Point2Dd> scaled;
        scaled.reserve(points.size());
        for (const auto& point : points) {
            if (IsVertical()) {
                scaled.emplace_back(anchor + (point.x - anchor) * progress, point.y);
            } else {
                scaled.emplace_back(point.x, anchor + (point.y - anchor) * progress);
            }
        }
        return scaled;
    }

    void UltraCanvasFunnelChart::DrawPolygon(IRenderContext* ctx,
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

    void UltraCanvasFunnelChart::DrawGradientPolygon(IRenderContext* ctx,
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

        // The gradient runs across the funnel, so the lit edge follows the taper
        std::vector<GradientStop> stops = {
            GradientStop(0.0, Lighten(fill, gradientLightening)),
            GradientStop(1.0, fill)
        };

        auto pattern = IsVertical()
                ? ctx->CreateLinearGradientPattern(minX, minY, maxX, minY, stops)
                : ctx->CreateLinearGradientPattern(minX, minY, minX, maxY, stops);
        if (!pattern) {
            DrawPolygon(ctx, points, fill, Colors::Transparent, 0.0);
            return;
        }

        ctx->SetFillPaint(pattern);
        ctx->FillLinePath(points);
    }

    void UltraCanvasFunnelChart::RenderChart(IRenderContext* ctx) {
        if (!ctx || geometry.empty()) return;

        // The legend consumes real space, so its measured layout comes first
        UpdateLegendLayout(ctx);

        if (shapeMode == FunnelShapeMode::CardRows) RenderCardBackgrounds(ctx);

        RenderConnectors(ctx);
        RenderStages(ctx);
        if (showTipTriangle) RenderTipTriangle(ctx, GetStageProgress(geometry.size() - 1));
        if (showTargetOverlay) RenderTargetOverlay(ctx);

        RenderInsideLabels(ctx);
        RenderStageLabels(ctx);
        RenderPercentColumn(ctx);
        if (descriptionColumnWidth > 0.0) RenderDescriptions(ctx);
        if (showBadges) RenderBadges(ctx);
        if (showTerminalCallout) RenderTerminalCallout(ctx);
        legend.Render(ctx, LegendArea());   // No-op while hidden or empty

        // Animation drives itself: keep asking for frames until every stage is in
        if (animationEnabled && !animationComplete) RequestRedraw();
    }

    void UltraCanvasFunnelChart::RenderCardBackgrounds(IRenderContext* ctx) {
        ctx->PushState();
        for (size_t i = 0; i < geometry.size(); ++i) {
            const auto& geo = geometry[i];
            Rect2Dd card = IsVertical()
                    ? Rect2Dd(cachedPlotArea.x - GetMarginStart() + kEdgePadding, geo.bandStart,
                              cachedPlotArea.width + GetMarginStart() + GetMarginEnd()
                                      - 2.0 * kEdgePadding,
                              geo.bandEnd - geo.bandStart)
                    : Rect2Dd(geo.bandStart, cachedPlotArea.y - GetMarginStart() + kEdgePadding,
                              geo.bandEnd - geo.bandStart,
                              cachedPlotArea.height + GetMarginStart() + GetMarginEnd()
                                      - 2.0 * kEdgePadding);

            bool isHovered = (static_cast<int>(i) == hoveredStageIndex);
            Color border = isHovered ? geo.color : cardBorderColor;
            ctx->DrawFilledRectangle(card, cardFillColor, isHovered ? 2.0f : 1.0f, border,
                                     static_cast<float>(cardCornerRadius));
        }
        ctx->PopState();
    }

    void UltraCanvasFunnelChart::RenderConnectors(IRenderContext* ctx) {
        ctx->PushState();
        for (size_t i = 0; i < geometry.size(); ++i) {
            const auto& geo = geometry[i];
            if (geo.connectorPolygon.size() < 3) continue;

            float progress = std::min(GetStageProgress(i), GetStageProgress(i + 1));
            std::vector<Point2Dd> polygon = ScalePolygon(geo.connectorPolygon, progress);

            // Card rows carry the wedge straight through the gap in the stage colour;
            // segmented bars use the neutral neck of the classic funnel.
            Color fill = (shapeMode == FunnelShapeMode::CardRows) ? geo.color : connectorColor;
            if (dimUnhoveredStages && hoveredStageIndex >= 0 &&
                hoveredStageIndex != static_cast<int>(i)) {
                fill = WithAlpha(fill, 0.45);
            }
            if (static_cast<int>(i) == hoveredConnectorIndex) {
                fill = Lighten(fill, 0.15);
            }

            DrawPolygon(ctx, polygon, fill, Colors::Transparent, 0.0);
        }

        if (!showConversionOnConnector) {
            ctx->PopState();
            return;
        }

        ctx->SetFontSize(connectorLabelFontSize);
        ctx->SetTextPaint(connectorLabelColor);
        for (size_t i = 0; i < geometry.size(); ++i) {
            const auto& geo = geometry[i];
            if (geo.connectorPolygon.size() < 3) continue;
            if (i + 1 >= geometry.size()) continue;
            if (GetStageProgress(i + 1) <= 0.0f) continue;

            // The neck carries the conversion into the stage below it. Horizontally
            // the text runs along the gap rather than across the funnel, so it only
            // fits when the gap is wide enough.
            std::string text = FormatPercent(geometry[i + 1].metrics.percentOfPrevious);
            double available = IsVertical()
                    ? geo.halfExtentEnd + geometry[i + 1].halfExtentStart
                    : geometry[i + 1].bandStart - geo.bandEnd;
            if (ctx->GetTextLineDimensions(text).width > available) continue;

            DrawCenteredText(ctx, text, geo.connectorCenter);
        }
        ctx->PopState();
    }

    void UltraCanvasFunnelChart::RenderStages(IRenderContext* ctx) {
        ctx->PushState();
        for (size_t i = 0; i < geometry.size(); ++i) {
            const auto& geo = geometry[i];
            float progress = GetStageProgress(i);
            if (progress <= 0.0f) continue;

            const FunnelStage& stage = funnelDataSource->GetStage(geo.dataIndex);
            RenderStageSlab(ctx, geo, stage, progress);
        }
        ctx->PopState();
    }

    void UltraCanvasFunnelChart::RenderStageSlab(IRenderContext* ctx,
                                                 const FunnelStageGeometry& geo,
                                                 const FunnelStage& stage, float progress) {
        std::vector<Point2Dd> polygon = ScalePolygon(geo.polygon, progress);

        size_t displayIndex = static_cast<size_t>(&geo - geometry.data());
        bool isHovered = (static_cast<int>(displayIndex) == hoveredStageIndex);

        Color fill = geo.color;
        if (isHovered) fill = Lighten(fill, 0.14);
        if (dimUnhoveredStages && hoveredStageIndex >= 0 && !isHovered) {
            fill = WithAlpha(fill, 0.4);
        }

        if (stage.HasSegments()) {
            RenderSubSegments(ctx, geo, stage, progress);
        } else if (useGradientFill) {
            DrawGradientPolygon(ctx, polygon, fill);
        } else {
            DrawPolygon(ctx, polygon, fill, Colors::Transparent, 0.0);
        }

        bool isBottleneck = highlightBottleneck &&
                            bottleneckIndex == static_cast<int>(displayIndex);
        if (isBottleneck) {
            DrawPolygon(ctx, polygon, Colors::Transparent, bottleneckColor, 2.5);
        } else if (showStageBorder) {
            DrawPolygon(ctx, polygon, Colors::Transparent, stageBorderColor, stageBorderWidth);
        }
    }

    void UltraCanvasFunnelChart::RenderSubSegments(IRenderContext* ctx,
                                                   const FunnelStageGeometry& geo,
                                                   const FunnelStage& stage, float progress) {
        double total = stage.GetSegmentTotal();
        if (total <= 0.0) {
            DrawPolygon(ctx, ScalePolygon(geo.polygon, progress), geo.color,
                        Colors::Transparent, 0.0);
            return;
        }

        double lowStart, highStart, lowEnd, highEnd;
        CrossRangeForExtent(geo.halfExtentStart, lowStart, highStart);
        CrossRangeForExtent(geo.halfExtentEnd, lowEnd, highEnd);

        double cursor = 0.0;
        for (size_t s = 0; s < stage.segments.size(); ++s) {
            const auto& segment = stage.segments[s];
            double share = std::max(0.0, segment.value) / total;
            double from = cursor;
            double to = cursor + share;
            cursor = to;

            // Each slice keeps the slab's taper, so the stack still reads as one funnel
            std::vector<Point2Dd> slice = {
                MapFlowCross(geo.bandStart, lowStart + (highStart - lowStart) * from),
                MapFlowCross(geo.bandStart, lowStart + (highStart - lowStart) * to),
                MapFlowCross(geo.bandEnd, lowEnd + (highEnd - lowEnd) * to),
                MapFlowCross(geo.bandEnd, lowEnd + (highEnd - lowEnd) * from)
            };

            Color fill = segment.color.a > 0
                    ? segment.color
                    : Lighten(geo.color, 0.36 * static_cast<double>(s) /
                                         std::max<size_t>(1, stage.segments.size() - 1));

            DrawPolygon(ctx, ScalePolygon(slice, progress), fill, Colors::Transparent, 0.0);
        }
    }

    void UltraCanvasFunnelChart::RenderTipTriangle(IRenderContext* ctx, float progress) {
        if (geometry.empty() || progress <= 0.0f) return;

        const auto& last = geometry.back();
        double tipStart = last.bandEnd;
        double tipLength = std::min(24.0, std::max(8.0, (last.bandEnd - last.bandStart) * 0.6));

        std::vector<Point2Dd> triangle;
        FillPolygon(triangle, tipStart, tipStart + tipLength, last.halfExtentEnd, 0.0, 0.0);

        ctx->PushState();
        DrawPolygon(ctx, ScalePolygon(triangle, progress), Lighten(last.color, 0.45),
                    Colors::Transparent, 0.0);
        ctx->PopState();
    }

    void UltraCanvasFunnelChart::RenderTargetOverlay(IRenderContext* ctx) {
        ctx->PushState();
        for (const auto& geo : geometry) {
            const FunnelStage& stage = funnelDataSource->GetStage(geo.dataIndex);
            if (stage.targetValue <= 0.0) continue;

            double halfTarget = HalfExtentForValue(stage.targetValue);
            std::vector<Point2Dd> ghost;
            FillPolygon(ghost, geo.bandStart, geo.bandEnd, halfTarget, halfTarget, 0.0);
            DrawPolygon(ctx, ghost, Colors::Transparent, targetOverlayColor, 1.5);
        }
        ctx->PopState();
    }

    void UltraCanvasFunnelChart::RenderInsideLabels(IRenderContext* ctx) {
        bool wantValues = (funnelValueLabelPlacement == FunnelValueLabelPlacement::InsideValueLabels)
                          && showValueLabels;
        bool wantPercent = (percentPlacement == FunnelPercentPlacement::InsideStagePercent) &&
                           (showPercentOfFirst || showPercentOfPrevious || showPercentOfTotal);
        bool wantLabels = (stageLabelPlacement == FunnelStageLabelPlacement::InsideStageLabels);

        if (!wantValues && !wantPercent && !wantLabels) return;

        ctx->PushState();
        ctx->SetFontSize(valueLabelFontSize > 0 ? valueLabelFontSize : 11.0);

        for (size_t i = 0; i < geometry.size(); ++i) {
            const auto& geo = geometry[i];
            if (GetStageProgress(i) < 1.0f) continue;

            const FunnelStage& stage = funnelDataSource->GetStage(geo.dataIndex);

            std::vector<std::string> lines;
            if (wantLabels && !stage.stageLabel.empty()) lines.push_back(stage.stageLabel);
            if (wantValues) lines.push_back(FormatStageValue(geo.metrics.value));
            if (wantPercent) {
                if (showPercentOfFirst)    lines.push_back(FormatPercent(geo.metrics.percentOfFirst));
                if (showPercentOfPrevious) lines.push_back(FormatPercent(geo.metrics.percentOfPrevious));
                if (showPercentOfTotal)    lines.push_back(FormatPercent(geo.metrics.percentOfTotal));
            }
            if (lines.empty()) continue;

            // The slab has to actually hold the text or it spills over the taper.
            // Labels sit on the slab's centre line, so its mean breadth is the
            // width they really have to fit into.
            double slabBreadth = geo.halfExtentStart + geo.halfExtentEnd;
            double slabThickness = geo.bandEnd - geo.bandStart;
            double availableWidth = IsVertical() ? slabBreadth : slabThickness;
            double availableHeight = IsVertical() ? slabThickness : slabBreadth;

            double lineHeight = ctx->GetTextLineDimensions(lines[0]).height;
            // Drop the least important lines rather than the whole label, so a
            // narrow tail stage degrades to just its value instead of going blank
            while (lines.size() > 1 && lineHeight * lines.size() > availableHeight) {
                lines.pop_back();
            }
            if (lineHeight * lines.size() > availableHeight) continue;

            ctx->SetTextPaint(PickReadableTextColor(geo.color));
            double top = geo.center.y - lineHeight * lines.size() / 2.0;
            for (size_t l = 0; l < lines.size(); ++l) {
                Size2Di size = ctx->GetTextLineDimensions(lines[l]);
                // An ellipsized number inside a slab says nothing, so a line that
                // does not fit is dropped rather than truncated
                if (size.width > availableWidth - 6.0) continue;
                ctx->DrawText(lines[l], Point2Dd(geo.center.x - size.width / 2.0,
                                                 top + lineHeight * l));
            }
        }
        ctx->PopState();
    }

    void UltraCanvasFunnelChart::DrawOutsideColumnText(IRenderContext* ctx,
                                                       const FunnelStageGeometry& geo,
                                                       const std::string& text, bool atStart,
                                                       bool withLeaderLine) {
        if (text.empty()) return;

        std::string drawn = TruncateToWidth(ctx, text, stageLabelColumnWidth);
        if (drawn.empty()) return;

        Size2Di size = ctx->GetTextLineDimensions(drawn);
        double leaderRoom = withLeaderLine ? 26.0 : 0.0;

        if (IsVertical()) {
            double y = geo.center.y - size.height / 2.0;
            if (atStart) {
                // Right-aligned against the funnel so the labels read as a column
                double right = cachedPlotArea.x - kColumnGap;
                ctx->DrawText(drawn, Point2Dd(right - size.width, y));
                return;
            }
            double left = cachedPlotArea.GetRight() + kColumnGap + leaderRoom;
            if (withLeaderLine) {
                double edgeX = geo.center.x + std::min(geo.halfExtentStart, geo.halfExtentEnd);
                ctx->SetStrokePaint(leaderLineColor);
                ctx->SetStrokeWidth(1.0);
                ctx->DrawLine({edgeX, geo.center.y}, {left - 6.0, geo.center.y});
            }
            ctx->DrawText(drawn, Point2Dd(left, y));
            return;
        }

        double x = geo.center.x - size.width / 2.0;
        if (atStart) {
            ctx->DrawText(drawn, Point2Dd(x, cachedPlotArea.y - kColumnGap - size.height));
            return;
        }
        double top = cachedPlotArea.GetBottom() + kColumnGap + leaderRoom;
        if (withLeaderLine) {
            double edgeY = geo.center.y + std::min(geo.halfExtentStart, geo.halfExtentEnd);
            ctx->SetStrokePaint(leaderLineColor);
            ctx->SetStrokeWidth(1.0);
            ctx->DrawLine({geo.center.x, edgeY}, {geo.center.x, top - 4.0});
        }
        ctx->DrawText(drawn, Point2Dd(x, top));
    }

    void UltraCanvasFunnelChart::RenderStageLabels(IRenderContext* ctx) {
        bool labelsAtStart = (stageLabelPlacement == FunnelStageLabelPlacement::OutsideStartLabels);
        bool labelsAtEnd = (stageLabelPlacement == FunnelStageLabelPlacement::OutsideEndLabels ||
                            stageLabelPlacement == FunnelStageLabelPlacement::LeaderLineLabels);
        bool valuesAtStart = showValueLabels &&
                funnelValueLabelPlacement == FunnelValueLabelPlacement::OutsideStartValues;
        bool valuesAtEnd = showValueLabels &&
                funnelValueLabelPlacement == FunnelValueLabelPlacement::OutsideEndValues;

        if (!labelsAtStart && !labelsAtEnd && !valuesAtStart && !valuesAtEnd) return;

        bool leaderLines = (stageLabelPlacement == FunnelStageLabelPlacement::LeaderLineLabels);

        ctx->PushState();
        ctx->SetFontSize(stageLabelFontSize);

        for (size_t i = 0; i < geometry.size(); ++i) {
            const auto& geo = geometry[i];
            if (GetStageProgress(i) <= 0.0f) continue;

            const FunnelStage& stage = funnelDataSource->GetStage(geo.dataIndex);

            // A name and a value sharing one column read best as "Impressions (1,163)"
            auto compose = [&](bool wantsLabel, bool wantsValue) {
                std::string text = wantsLabel ? stage.stageLabel : std::string();
                if (wantsValue) {
                    std::string value = FormatStageValue(geo.metrics.value);
                    text = text.empty() ? value : text + " (" + value + ")";
                }
                return text;
            };

            bool isHovered = (static_cast<int>(i) == hoveredStageIndex);
            ctx->SetTextPaint(isHovered ? Color(20, 20, 25, 255) : stageLabelColor);

            DrawOutsideColumnText(ctx, geo, compose(labelsAtStart, valuesAtStart), true, false);
            DrawOutsideColumnText(ctx, geo, compose(labelsAtEnd, valuesAtEnd), false, leaderLines);
        }
        ctx->PopState();
    }

    void UltraCanvasFunnelChart::RenderPercentColumn(IRenderContext* ctx) {
        if (!PercentTakesEndColumn() && !PercentTakesStartColumn()) return;

        bool startColumn = PercentTakesStartColumn();

        ctx->PushState();
        ctx->SetFontSize(stageLabelFontSize);
        ctx->SetTextPaint(stageLabelColor);

        for (size_t i = 0; i < geometry.size(); ++i) {
            const auto& geo = geometry[i];
            if (GetStageProgress(i) < 1.0f) continue;

            std::vector<std::string> lines;
            if (showPercentOfFirst)    lines.push_back(FormatPercent(geo.metrics.percentOfFirst));
            if (showPercentOfPrevious) lines.push_back(FormatPercent(geo.metrics.percentOfPrevious));
            if (showPercentOfTotal)    lines.push_back(FormatPercent(geo.metrics.percentOfTotal));
            if (showDropOffLabels && i > 0) {
                lines.push_back("-" + FormatStageValue(geo.metrics.dropOff));
            }
            if (lines.empty()) continue;

            double lineHeight = ctx->GetTextLineDimensions(lines[0]).height;

            if (IsVertical()) {
                double columnX = startColumn
                        ? cachedPlotArea.x - kColumnGap - percentColumnWidth
                        : cachedPlotArea.GetRight() + kColumnGap +
                          (OutsideTextTakesEndColumn() ? stageLabelColumnWidth + kColumnGap : 0.0);
                double top = geo.center.y - lineHeight * lines.size() / 2.0;
                for (size_t l = 0; l < lines.size(); ++l) {
                    ctx->DrawText(lines[l], Point2Dd(columnX, top + lineHeight * l));
                }
            } else {
                double columnY = startColumn
                        ? cachedPlotArea.y - kColumnGap - lineHeight * lines.size()
                        : cachedPlotArea.GetBottom() + kColumnGap +
                          (OutsideTextTakesEndColumn() ? stageLabelFontSize * 2.0 : 0.0);
                for (size_t l = 0; l < lines.size(); ++l) {
                    Size2Di size = ctx->GetTextLineDimensions(lines[l]);
                    ctx->DrawText(lines[l], Point2Dd(geo.center.x - size.width / 2.0,
                                                     columnY + lineHeight * l));
                }
            }
        }
        ctx->PopState();
    }

    void UltraCanvasFunnelChart::RenderDescriptions(IRenderContext* ctx) {
        ctx->PushState();
        ctx->SetFontSize(descriptionFontSize);
        ctx->SetTextPaint(descriptionColor);

        double columnStart = IsVertical()
                ? static_cast<double>(GetWidth()) - kEdgePadding - descriptionColumnWidth
                : static_cast<double>(GetHeight()) - kEdgePadding - descriptionColumnWidth;

        for (size_t i = 0; i < geometry.size(); ++i) {
            const auto& geo = geometry[i];
            if (GetStageProgress(i) < 1.0f) continue;

            const FunnelStage& stage = funnelDataSource->GetStage(geo.dataIndex);
            if (stage.description.empty()) continue;

            std::string text = TruncateToWidth(ctx, stage.description, descriptionColumnWidth);
            if (text.empty()) continue;

            Size2Di size = ctx->GetTextLineDimensions(text);
            if (IsVertical()) {
                if (showDescriptionLeaderLines) {
                    // Joins the slab to its annotation, the way presentation funnels do
                    double edgeX = geo.center.x + std::min(geo.halfExtentStart, geo.halfExtentEnd);
                    ctx->SetStrokePaint(leaderLineColor);
                    ctx->SetStrokeWidth(1.0);
                    ctx->DrawLine({edgeX, geo.center.y}, {columnStart - 8.0, geo.center.y});
                    ctx->SetTextPaint(descriptionColor);
                }
                ctx->DrawText(text, Point2Dd(columnStart, geo.center.y - size.height / 2.0));
            } else {
                if (showDescriptionLeaderLines) {
                    double edgeY = geo.center.y + std::min(geo.halfExtentStart, geo.halfExtentEnd);
                    ctx->SetStrokePaint(leaderLineColor);
                    ctx->SetStrokeWidth(1.0);
                    ctx->DrawLine({geo.center.x, edgeY}, {geo.center.x, columnStart - 6.0});
                    ctx->SetTextPaint(descriptionColor);
                }
                ctx->DrawText(text, Point2Dd(geo.center.x - size.width / 2.0, columnStart));
            }
        }
        ctx->PopState();
    }

    void UltraCanvasFunnelChart::RenderBadges(IRenderContext* ctx) {
        ctx->PushState();
        ctx->SetFontSize(badgeFontSize);
        ctx->SetFontWeight(FontWeight::Bold);

        for (size_t i = 0; i < geometry.size(); ++i) {
            const auto& geo = geometry[i];
            if (GetStageProgress(i) < 1.0f) continue;

            const FunnelStage& stage = funnelDataSource->GetStage(geo.dataIndex);
            // An empty badge falls back to the stage's position in the funnel
            std::string badge = stage.badgeText;
            if (badge.empty()) {
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer), "%02zu", i + 1);
                badge = buffer;
            }

            ctx->SetTextPaint(badgeColor);
            Size2Di size = ctx->GetTextLineDimensions(badge);

            if (IsVertical()) {
                double x = cachedPlotArea.x - kColumnGap - size.width;
                if (OutsideTextTakesStartColumn()) x -= stageLabelColumnWidth + kColumnGap;
                ctx->DrawText(badge, Point2Dd(std::max(2.0, x), geo.center.y - size.height / 2.0));
            } else {
                double y = cachedPlotArea.y - kColumnGap - size.height;
                if (OutsideTextTakesStartColumn()) y -= stageLabelFontSize * 2.0;
                ctx->DrawText(badge, Point2Dd(geo.center.x - size.width / 2.0, std::max(2.0, y)));
            }
        }
        ctx->PopState();
    }

    void UltraCanvasFunnelChart::RenderTerminalCallout(IRenderContext* ctx) {
        if (geometry.empty()) return;

        const auto& last = geometry.back();
        double flow = last.bandEnd + terminalCalloutRadius + 14.0 + (showTipTriangle ? 24.0 : 0.0);
        Point2Dd center = MapFlowCross(flow, 0.0);

        ctx->PushState();
        ctx->DrawFilledCircle(center, static_cast<float>(terminalCalloutRadius),
                              Colors::White, terminalCalloutColor, 1.5f);
        ctx->SetFontSize(std::max(9.0, terminalCalloutRadius * 0.62));
        ctx->SetTextPaint(Color(60, 60, 66, 255));
        DrawCenteredText(ctx, FormatStageValue(last.metrics.value), center);
        ctx->PopState();
    }

// =============================================================================
// INTERACTION
// =============================================================================

    bool UltraCanvasFunnelChart::PointInPolygon(const std::vector<Point2Dd>& polygon,
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

    int UltraCanvasFunnelChart::FindStageAt(const Point2Di& mousePos) const {
        for (size_t i = 0; i < geometry.size(); ++i) {
            if (PointInPolygon(geometry[i].polygon, mousePos.x, mousePos.y)) {
                return static_cast<int>(i);
            }
        }

        // A near-empty stage is a sliver a few pixels wide; fall back to its band so
        // it can still be hovered.
        double flow = IsVertical() ? mousePos.y : mousePos.x;
        double cross = IsVertical() ? mousePos.x : mousePos.y;
        double center = GetCrossCenter();
        for (size_t i = 0; i < geometry.size(); ++i) {
            const auto& geo = geometry[i];
            if (flow < geo.bandStart || flow > geo.bandEnd) continue;
            double half = std::max(6.0, std::max(geo.halfExtentStart, geo.halfExtentEnd));
            double low, high;
            CrossRangeForExtent(half, low, high);
            if (cross >= center + low - 4.0 && cross <= center + high + 4.0) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int UltraCanvasFunnelChart::FindConnectorAt(const Point2Di& mousePos) const {
        for (size_t i = 0; i < geometry.size(); ++i) {
            if (geometry[i].connectorPolygon.size() < 3) continue;
            if (PointInPolygon(geometry[i].connectorPolygon, mousePos.x, mousePos.y)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    std::string UltraCanvasFunnelChart::GenerateFunnelTooltip(
            const FunnelStage& stage, const FunnelStageMetrics& metrics) const {
        // A caller-supplied tooltip is passed through untouched so it can use markup
        if (!stage.tooltipText.empty()) return stage.tooltipText;

        std::ostringstream oss;
        oss << "<b>" << EscapeMarkup(stage.stageLabel) << "</b>\n"
            << EscapeMarkup(FormatStageValue(metrics.value)) << "\n"
            << "Of first stage: " << EscapeMarkup(FormatPercent(metrics.percentOfFirst)) << "\n"
            << "From previous: " << EscapeMarkup(FormatPercent(metrics.percentOfPrevious));

        if (metrics.dropOff > 0.0) {
            oss << "\nLost here: " << EscapeMarkup(FormatStageValue(metrics.dropOff))
                << " (" << EscapeMarkup(FormatPercent(metrics.dropOffPercent)) << ")";
        }

        if (stage.HasSegments()) {
            for (const auto& segment : stage.segments) {
                oss << "\n  " << EscapeMarkup(segment.label) << ": "
                    << EscapeMarkup(FormatStageValue(segment.value));
            }
        }

        if (!stage.description.empty()) {
            oss << "\n" << EscapeMarkup(stage.description);
        }

        return oss.str();
    }

    bool UltraCanvasFunnelChart::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!funnelDataSource) return false;

        // Hit testing reads the cached geometry, which Render() refreshes. A mouse
        // move can arrive before the first paint, so make sure it is current.
        UpdateRenderingCache();

        int previousStage = hoveredStageIndex;
        int previousConnector = hoveredConnectorIndex;

        hoveredStageIndex = FindStageAt(mousePos);
        hoveredConnectorIndex = (hoveredStageIndex < 0) ? FindConnectorAt(mousePos) : -1;

        bool changed = (previousStage != hoveredStageIndex ||
                        previousConnector != hoveredConnectorIndex);

        if (changed && hoveredStageIndex >= 0 && onStageHover) {
            onStageHover(geometry[hoveredStageIndex].dataIndex);
        }

        if (enableTooltips) {
            if (hoveredStageIndex >= 0) {
                const auto& geo = geometry[hoveredStageIndex];
                std::string text = GenerateFunnelTooltip(
                        funnelDataSource->GetStage(geo.dataIndex), geo.metrics);
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

    bool UltraCanvasFunnelChart::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        if (event.type == UCEventType::MouseLeave) {
            if (hoveredStageIndex != -1 || hoveredConnectorIndex != -1) {
                hoveredStageIndex = -1;
                hoveredConnectorIndex = -1;
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

            int stage = FindStageAt(mousePos);
            if (stage >= 0 && static_cast<size_t>(stage) < geometry.size()) {
                if (onStageClick) onStageClick(geometry[stage].dataIndex);
            } else {
                int connector = FindConnectorAt(mousePos);
                if (connector >= 0 && onConnectorClick) {
                    onConnectorClick(geometry[connector].dataIndex);
                }
            }
        }

        return UltraCanvasChartElementBase::OnEvent(event);
    }

} // namespace UltraCanvas

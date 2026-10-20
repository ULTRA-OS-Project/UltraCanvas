// Plugins/Charts/Engine/Charts/UltraCanvasEngineWaterfallChart.cpp
// Waterfall chart on the chart engine
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineWaterfallChart.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

namespace {

// Legend entry order: the kinds the data actually uses.
constexpr size_t kLegendIncrease = 0;
constexpr size_t kLegendDecrease = 1;
constexpr size_t kLegendSubtotal = 2;
constexpr size_t kLegendTotal = 3;

} // namespace

UltraCanvasEngineWaterfallChart::UltraCanvasEngineWaterfallChart(const std::string& id,
                                                                 int x, int y, int w, int h)
    : UltraCanvasChartEngineElement(id, x, y, w, h) {
    SetAnimateOnDataChange(true, 0.7f);
    SetLegendPosition(ChartLegendPosition::TopCenter);
}

// =============================================================================
// DATA
// =============================================================================

void UltraCanvasEngineWaterfallChart::SetSteps(const std::vector<WaterfallStep>& newSteps) {
    steps = newSteps;
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineWaterfallChart::AddStep(const std::string& label, double value,
                                              WaterfallStepKind kind) {
    steps.emplace_back(label, value, kind);
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineWaterfallChart::ClearSteps() {
    steps.clear();
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
}

std::vector<WaterfallResolvedStep> UltraCanvasEngineWaterfallChart::ResolveSteps() const {
    std::vector<WaterfallResolvedStep> out;
    out.reserve(steps.size());
    double running = 0.0;
    for (const WaterfallStep& step : steps) {
        WaterfallResolvedStep r;
        const double value = std::isfinite(step.value) ? step.value : 0.0;
        switch (step.kind) {
            case WaterfallStepKind::Change:
                r.start = running;
                running += value;
                r.end = running;
                break;
            case WaterfallStepKind::Subtotal:
                r.start = 0.0;
                r.end = running;
                break;
            case WaterfallStepKind::Total:
                r.start = 0.0;
                r.end = running + value;
                running = r.end;
                break;
        }
        r.runningTotal = running;
        out.push_back(r);
    }
    return out;
}

double UltraCanvasEngineWaterfallChart::GetFinalTotal() const {
    const std::vector<WaterfallResolvedStep> resolved = ResolveSteps();
    return resolved.empty() ? 0.0 : resolved.back().runningTotal;
}

// =============================================================================
// APPEARANCE
// =============================================================================

void UltraCanvasEngineWaterfallChart::SetIncreaseColor(const Color& color) {
    increaseColor = color;
    RefreshLegend();
    RequestRedraw();
}

void UltraCanvasEngineWaterfallChart::SetDecreaseColor(const Color& color) {
    decreaseColor = color;
    RefreshLegend();
    RequestRedraw();
}

void UltraCanvasEngineWaterfallChart::SetSubtotalColor(const Color& color) {
    subtotalColor = color;
    RefreshLegend();
    RequestRedraw();
}

void UltraCanvasEngineWaterfallChart::SetTotalColor(const Color& color) {
    totalColor = color;
    RefreshLegend();
    RequestRedraw();
}

void UltraCanvasEngineWaterfallChart::SetBarStyle(WaterfallBarStyle style) {
    barStyle = style;
    RefreshLegend();                       // the swatch mirrors the fill
    RequestRedraw();
}

void UltraCanvasEngineWaterfallChart::SetSlotFill(double fill) {
    slotFill = std::clamp(fill, 0.2, 1.0);
    MarkEngineDirty(ChartDirty::Style);    // label anchors move with the bars
}

void UltraCanvasEngineWaterfallChart::SetConnectorStyle(WaterfallConnectorStyle style,
                                                        const Color& color, float width) {
    connectorStyle = style;
    connectorColor = color;
    connectorWidth = width;
    RequestRedraw();                       // repaint-only: nothing moves
}

void UltraCanvasEngineWaterfallChart::SetShowValueLabels(bool show) {
    showValues = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineWaterfallChart::SetShowCumulativeLabels(bool show) {
    showCumulative = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineWaterfallChart::SetValueFormat(const std::string& prefix,
                                                     const std::string& suffix,
                                                     bool compact, int decimals) {
    valuePrefix = prefix;
    valueSuffix = suffix;
    compactValues = compact;
    valueDecimals = decimals;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineWaterfallChart::SetShowZeroLine(bool show) {
    showZeroLine = show;
    RefreshLimiters();
}

void UltraCanvasEngineWaterfallChart::OnThemeChanged() {
    RefreshLegend();
    RefreshLimiters();
}

Color UltraCanvasEngineWaterfallChart::ColorFor(const WaterfallStep& step) const {
    if (step.color.a > 0) return step.color;
    switch (step.kind) {
        case WaterfallStepKind::Subtotal: return subtotalColor;
        case WaterfallStepKind::Total:    return totalColor;
        case WaterfallStepKind::Change:
        default:
            return step.value >= 0.0 ? increaseColor : decreaseColor;
    }
}

void UltraCanvasEngineWaterfallChart::RefreshLegend() {
    bool used[4] = {false, false, false, false};
    for (const WaterfallStep& step : steps) {
        switch (step.kind) {
            case WaterfallStepKind::Subtotal: used[kLegendSubtotal] = true; break;
            case WaterfallStepKind::Total:    used[kLegendTotal] = true; break;
            case WaterfallStepKind::Change:
                used[step.value >= 0.0 ? kLegendIncrease : kLegendDecrease] = true;
                break;
        }
    }
    const LegendSwatch swatch = (barStyle == WaterfallBarStyle::Gradient)
                                    ? LegendSwatch::Gradient : LegendSwatch::Square;
    std::vector<ChartLegendEntry> entries;
    if (used[kLegendIncrease]) entries.emplace_back("Increase", increaseColor, swatch);
    if (used[kLegendDecrease]) entries.emplace_back("Decrease", decreaseColor, swatch);
    if (used[kLegendSubtotal]) entries.emplace_back("Subtotal", subtotalColor, swatch);
    if (used[kLegendTotal])    entries.emplace_back("Total", totalColor, swatch);
    SetLegendEntries(entries);
    SetShowLegend(entries.size() > 1);
}

void UltraCanvasEngineWaterfallChart::RefreshLimiters() {
    ClearLimiters();
    if (!showZeroLine) return;
    ChartLimiter zero;
    zero.kind = ChartLimiterKind::Threshold;
    zero.axisIndex = 0;
    zero.value = 0.0;
    zero.color = GetTheme().axisLineColor;
    zero.width = 1.0f;
    zero.dashed = false;
    AddLimiter(zero);
}

std::string UltraCanvasEngineWaterfallChart::FormatValue(double value) const {
    // The value axis carries the formatting rules (and its range-derived
    // decimals); fall back to a configured stand-in before the first layout.
    if (Axes().Count() > 0) return Axes().At(0).FormatValue(value);
    ChartAxis format("value");
    format.unitPrefix = valuePrefix;
    format.unitSuffix = valueSuffix;
    format.compactNumbers = compactValues;
    format.decimals = valueDecimals;
    return format.FormatValue(value);
}

std::string UltraCanvasEngineWaterfallChart::TooltipFor(
        size_t index, const WaterfallResolvedStep& resolved) const {
    const WaterfallStep& step = steps[index];
    std::string text = step.label;
    switch (step.kind) {
        case WaterfallStepKind::Change: {
            const std::string sign = step.value >= 0.0 ? "+" : "";
            text += "\nChange: " + sign + FormatValue(step.value);
            text += "\nRunning total: " + FormatValue(resolved.runningTotal);
            break;
        }
        case WaterfallStepKind::Subtotal:
            text += "\nSubtotal: " + FormatValue(resolved.end);
            break;
        case WaterfallStepKind::Total:
            text += "\nTotal: " + FormatValue(resolved.end);
            break;
    }
    return text;
}

std::string UltraCanvasEngineWaterfallChart::LabelText(
        size_t index, const WaterfallResolvedStep& resolved) const {
    const WaterfallStep& step = steps[index];
    std::string text;
    if (showValues) {
        text = (step.kind == WaterfallStepKind::Change)
                   ? (step.value >= 0.0 ? "+" : "") + FormatValue(step.value)
                   : FormatValue(resolved.end);
    }
    if (showCumulative && step.kind == WaterfallStepKind::Change) {
        const std::string running = "= " + FormatValue(resolved.runningTotal);
        text = text.empty() ? running : text + " " + running;
    }
    return text;
}

// =============================================================================
// ENGINE CONTRACT
// =============================================================================

void UltraCanvasEngineWaterfallChart::DescribeAxes(ChartAxisSet& axes) {
    const bool horizontal = GetProjectionKind() == ChartProjectionKind::Horizontal;

    ChartAxis value("value");
    value.side = horizontal ? ChartAxisSide::Bottom : ChartAxisSide::Left;
    value.unitPrefix = valuePrefix;
    value.unitSuffix = valueSuffix;
    value.compactNumbers = compactValues;
    value.decimals = valueDecimals;
    value.Observe(0.0);                         // subtotals and totals stand on zero
    for (const WaterfallResolvedStep& r : ResolveSteps()) {
        value.Observe(r.start);
        value.Observe(r.end);
    }
    axes.Add(value);

    ChartAxis category("step");
    category.scale = ChartScale::Category;
    category.side = horizontal ? ChartAxisSide::Left : ChartAxisSide::Bottom;
    for (const WaterfallStep& step : steps) category.categories.push_back(step.label);
    category.categoryPadding = 0.5;
    axes.Add(category);

    SetGridAxis(0);
}

void UltraCanvasEngineWaterfallChart::StepSpan(const ChartAxis& category, size_t index,
                                               double& u0, double& u1) const {
    const double center = category.Normalize(static_cast<double>(index));
    const double slot = std::abs(category.Normalize(1.0) - category.Normalize(0.0));
    u0 = center - slot * slotFill * 0.5;
    u1 = center + slot * slotFill * 0.5;
}

void UltraCanvasEngineWaterfallChart::RenderChartContent(IRenderContext* ctx,
                                                         const ChartEngineFrame& frame) {
    if (steps.empty() || !frame.axes || frame.axes->Count() < 2) return;
    const ChartAxis& value = frame.axes->At(0);
    const ChartAxis& category = frame.axes->At(1);
    const IChartProjection& projection = *frame.projection;
    const std::vector<WaterfallResolvedStep> resolved = ResolveSteps();
    const double progress = frame.animationProgress;
    const double radius = (barStyle == WaterfallBarStyle::Rounded) ? 4.0 : 0.0;

    // Bars grow out of their start level, so the cascade unfolds in place.
    std::vector<double> u0s(steps.size()), u1s(steps.size()), ends(steps.size());
    for (size_t i = 0; i < steps.size(); ++i) {
        StepSpan(category, i, u0s[i], u1s[i]);
        const double v0 = value.Normalize(resolved[i].start);
        const double vEnd = value.Normalize(resolved[i].end);
        const double v1 = v0 + (vEnd - v0) * progress;
        ends[i] = v1;

        const std::vector<Point2Dd> outline =
            BuildBarOutline(projection, u0s[i], v0, u1s[i], v1, 8, radius);
        if (outline.size() < 3) continue;

        const int64_t id = static_cast<int64_t>(i);
        const bool hovered = HoveredRegionId() == id;
        const Color base = ColorFor(steps[i]);
        const Color fill = hovered ? base.Lighten(0.25f) : base;

        if (barStyle == WaterfallBarStyle::Gradient) {
            // Light at the value end, saturated at the base - along the value
            // direction whichever way the projection runs it.
            const double uc = (u0s[i] + u1s[i]) * 0.5;
            const Point2Dd from = projection.ToScreen(ChartNormalizedPoint(uc, v1));
            const Point2Dd to = projection.ToScreen(ChartNormalizedPoint(uc, v0));
            auto gradient = ctx->CreateLinearGradientPattern(
                from.x, from.y, to.x, to.y,
                {GradientStop(0.0, fill.Lighten(0.45f)), GradientStop(1.0, fill)});
            if (gradient) ctx->SetFillPaint(gradient); else ctx->SetFillPaint(fill);
        } else {
            ctx->SetFillPaint(fill);
        }
        ctx->FillLinePath(outline);
        ctx->SetStrokePaint(base.Darken(0.35f).WithAlpha(hovered ? 255 : 160));
        ctx->SetStrokeWidth(hovered ? 2.0f : 1.0f);
        ctx->DrawLinePath(outline, true);

        AddHitRegion(outline, id, TooltipFor(i, resolved[i]));
    }

    // Connectors carry each bar's end level across the gap to the next bar.
    if (connectorStyle == WaterfallConnectorStyle::Hidden || steps.size() < 2) return;
    ctx->PushState();
    ctx->SetStrokePaint(connectorColor);
    ctx->SetStrokeWidth(connectorWidth);
    if (connectorStyle == WaterfallConnectorStyle::Dashed) {
        ctx->SetLineDash(UCDashPattern({6.0, 4.0}));
    } else if (connectorStyle == WaterfallConnectorStyle::Dotted) {
        ctx->SetLineCap(LineCap::Round);
        ctx->SetLineDash(UCDashPattern({1.0, 3.0}));
    }
    for (size_t i = 0; i + 1 < steps.size(); ++i) {
        // The level the next bar continues from: this bar's end.
        const double level = ends[i];
        const double from = std::max(u0s[i], u1s[i]);
        const double to = std::min(u0s[i + 1], u1s[i + 1]);
        ctx->DrawLine(projection.ToScreen(ChartNormalizedPoint(from, level)),
                      projection.ToScreen(ChartNormalizedPoint(to, level)));
    }
    ctx->PopState();
}

void UltraCanvasEngineWaterfallChart::MeasureContent(IRenderContext* ctx,
                                                     ChartLayoutRequest& request) {
    if (!showValues && !showCumulative) return;
    // A spill band beyond the value end, so a bar reaching the axis maximum
    // keeps its label just past the plot edge.
    ctx->SetFontSize(axisFontSize);
    const double lineHeight = ctx->GetTextLineHeight("Ag");
    if (GetProjectionKind() == ChartProjectionKind::Horizontal) {
        const std::vector<WaterfallResolvedStep> resolved = ResolveSteps();
        double widest = 0.0;
        for (size_t i = 0; i < steps.size(); ++i) {
            widest = std::max(widest, static_cast<double>(
                ctx->GetTextLineWidth(LabelText(i, resolved[i]))));
        }
        request.Reserve(ChartAxisEdge::Right, widest + 10.0);
    } else {
        request.Reserve(ChartAxisEdge::Top, lineHeight + 8.0);
    }
}

void UltraCanvasEngineWaterfallChart::CollectChartLabels(IRenderContext* ctx,
                                                         ChartLabelBroker& broker) {
    if ((!showValues && !showCumulative) || steps.empty() || !Frame().axes ||
        Frame().axes->Count() < 2 || !Frame().projection) return;
    const ChartAxis& value = Frame().axes->At(0);
    const ChartAxis& category = Frame().axes->At(1);
    const bool horizontal = GetProjectionKind() == ChartProjectionKind::Horizontal;
    const std::vector<WaterfallResolvedStep> resolved = ResolveSteps();
    ctx->SetFontSize(axisFontSize);

    for (size_t i = 0; i < steps.size(); ++i) {
        const WaterfallStep& step = steps[i];
        const std::string text = LabelText(i, resolved[i]);
        if (text.empty()) continue;

        double u0, u1;
        StepSpan(category, i, u0, u1);
        // Label the end the bar grows towards: above an increase, below a
        // decrease (right / left under the horizontal projection).
        const bool rising = resolved[i].end >= resolved[i].start;
        ChartLabelRequest r;
        r.text = text;
        r.klass = ChartLabelClass::ValueLabel;
        r.anchor = Frame().projection->ToScreen(
            ChartNormalizedPoint((u0 + u1) * 0.5, value.Normalize(resolved[i].end)));
        const Size2Di size = ctx->GetTextLineDimensions(text);
        r.textSize = Size2Dd(size.width, size.height);
        r.preferredSide = horizontal ? (rising ? LabelSide::Right : LabelSide::Left)
                                     : (rising ? LabelSide::Top : LabelSide::Bottom);
        r.allowSuppress = true;
        // Totals are the numbers a waterfall exists to show.
        r.priority = (step.kind == WaterfallStepKind::Change) ? 0 : 10;
        r.userData = i;
        broker.Add(r);
    }
}

} // namespace UltraCanvas

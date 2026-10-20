// Plugins/Charts/Engine/Charts/UltraCanvasEnginePieChart.cpp
// Pie and donut chart on the chart engine.
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasEnginePieChart.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

namespace {

constexpr double kTwoPi = 6.283185307179586;

// The slice index rides the region id and the label's userData.
int64_t SliceRegion(size_t index) { return static_cast<int64_t>(index); }

} // namespace

UltraCanvasEnginePieChart::UltraCanvasEnginePieChart(const std::string& id, int x, int y,
                                                     int w, int h)
    : UltraCanvasChartEngineElement(id, x, y, w, h) {
    SetProjectionKind(ChartProjectionKind::Polar);
    SetAnimateOnDataChange(true, 0.8f);     // the slices sweep in
    SetEnableTooltips(true);
    SetLegendPosition(ChartLegendPosition::RightStart);
    ApplyRadii();
}

// =============================================================================
// DATA AND CONFIGURATION
// =============================================================================

void UltraCanvasEnginePieChart::SetSlices(const std::vector<EnginePieSlice>& newSlices) {
    slices = newSlices;
    sliceHidden.assign(slices.size(), false);
    RefreshLegend();
    ApplyRadii();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEnginePieChart::AddSlice(const std::string& name, double value,
                                         const Color& color) {
    slices.emplace_back(name, value, color);
    sliceHidden.push_back(false);
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEnginePieChart::ClearSlices() {
    slices.clear();
    sliceHidden.clear();
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEnginePieChart::SetDonutMode(bool on) {
    donutMode = on;
    ApplyRadii();
    MarkEngineDirty(ChartDirty::Style);      // inside labels move to the ring
}

void UltraCanvasEnginePieChart::SetInnerRadius(float fraction) {
    innerFraction = std::clamp(fraction, 0.05f, 0.9f);
    ApplyRadii();
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEnginePieChart::SetStartAngle(double degrees) {
    startAngle = degrees;
    SetPolarAngles(startAngle, 360.0, clockwiseSweep);
}

void UltraCanvasEnginePieChart::SetClockwise(bool clockwise) {
    clockwiseSweep = clockwise;
    SetPolarAngles(startAngle, 360.0, clockwiseSweep);
}

void UltraCanvasEnginePieChart::SetGlobalExplosion(float fraction) {
    globalExplosion = std::clamp(fraction, 0.0f, 0.5f);
    ApplyRadii();
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEnginePieChart::SetSliceExplosion(size_t index, float fraction) {
    if (index >= slices.size()) return;
    slices[index].explosion = std::clamp(fraction, 0.0f, 0.5f);
    ApplyRadii();
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEnginePieChart::ClearSliceExplosions() {
    for (EnginePieSlice& slice : slices) slice.explosion = -1.0f;
    ApplyRadii();
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEnginePieChart::SetHoverExplosion(float fraction) {
    hoverExplosion = std::clamp(fraction, 0.0f, 0.3f);
    ApplyRadii();
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEnginePieChart::SetBorder(const Color& color, float width) {
    borderColor = color;
    borderWidth = std::max(0.0f, width);
    RequestRedraw();
}

void UltraCanvasEnginePieChart::SetRadialGradients(bool on) {
    radialGradients = on;
    RequestRedraw();
}

void UltraCanvasEnginePieChart::SetCenterText(const std::string& text,
                                              const std::string& caption) {
    centerText = text;
    centerCaption = caption;
    MarkEngineDirty(ChartDirty::Style);      // the centre is a label obstacle
}

void UltraCanvasEnginePieChart::SetUseThemePalette(bool use) {
    useThemePalette = use;
    RefreshLegend();
    RequestRedraw();
}

void UltraCanvasEnginePieChart::SetLabelPosition(EnginePieLabelPosition position) {
    labelPosition = position;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEnginePieChart::SetLabelContent(EnginePieLabelContent content) {
    labelContent = content;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEnginePieChart::SetLeaderLinesEnabled(bool on) {
    leaderLines = on;
    RequestRedraw();
}

void UltraCanvasEnginePieChart::SetLeaderLineStyle(const Color& color, float width) {
    leaderColor = color;
    leaderWidth = width;
    RequestRedraw();
}

void UltraCanvasEnginePieChart::SetValueFormatter(std::function<std::string(double)> formatter) {
    valueFormatter = std::move(formatter);
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEnginePieChart::SetPercentFormatter(std::function<std::string(double)> formatter) {
    percentFormatter = std::move(formatter);
    MarkEngineDirty(ChartDirty::Style);
}

// =============================================================================
// HELPERS
// =============================================================================

Color UltraCanvasEnginePieChart::SliceColor(size_t index) const {
    const Color& own = slices[index].color;
    if (!useThemePalette && own.a > 0) return own;
    return Palette().ColorAt(index, slices.size());
}

float UltraCanvasEnginePieChart::ExplosionOf(size_t index) const {
    const float own = slices[index].explosion;
    return own >= 0.0f ? own : globalExplosion;
}

float UltraCanvasEnginePieChart::MaxExplosion() const {
    float most = globalExplosion;
    for (size_t i = 0; i < slices.size(); ++i) most = std::max(most, ExplosionOf(i));
    return most + hoverExplosion;
}

void UltraCanvasEnginePieChart::ApplyRadii() {
    // The slices are drawn inside a radius R / (1 + e), e being the largest
    // pull-out, so an exploded slice stays in the plot. The donut hole keeps
    // its fraction of that slice radius.
    const double e = MaxExplosion();
    SetPolarInnerRadius(donutMode ? innerFraction / (1.0 + e) : 0.0);
}

double UltraCanvasEnginePieChart::SliceOuterV() const {
    const double e = MaxExplosion();
    const double hole = donutMode ? innerFraction / (1.0 + e) : 0.0;
    return (1.0 / (1.0 + e) - hole) / (1.0 - hole);
}

std::vector<UltraCanvasEnginePieChart::SliceSpan> UltraCanvasEnginePieChart::Spans() const {
    std::vector<SliceSpan> spans;
    double total = 0.0;
    for (size_t i = 0; i < slices.size(); ++i) {
        if (!sliceHidden[i] && std::isfinite(slices[i].value) && slices[i].value > 0.0) {
            total += slices[i].value;
        }
    }
    if (total <= 0.0) return spans;
    double cursor = 0.0;
    for (size_t i = 0; i < slices.size(); ++i) {
        if (sliceHidden[i] || !std::isfinite(slices[i].value) || slices[i].value <= 0.0) continue;
        SliceSpan span;
        span.index = i;
        span.share = slices[i].value / total;
        span.u0 = cursor;
        cursor += span.share;
        span.u1 = cursor;
        spans.push_back(span);
    }
    return spans;
}

Point2Dd UltraCanvasEnginePieChart::ExplosionOffset(const ChartEngineFrame& frame, double midU,
                                                    float explosion) const {
    const auto* polar = dynamic_cast<const ChartPolarProjection*>(frame.projection);
    if (!polar || explosion <= 0.0f) return Point2Dd(0.0, 0.0);
    const Point2Dd c = polar->Center();
    const Point2Dd rim = frame.projection->ToScreen(ChartNormalizedPoint(midU, SliceOuterV()));
    const double dx = rim.x - c.x, dy = rim.y - c.y;
    // A fraction of the slice radius, along the slice's middle angle.
    return Point2Dd(dx * explosion, dy * explosion);
}

std::string UltraCanvasEnginePieChart::FormatValue(double value) const {
    if (valueFormatter) return valueFormatter(value);
    double most = 0.0;
    for (const EnginePieSlice& slice : slices) most = std::max(most, std::abs(slice.value));
    ChartAxis format;
    format.SetRange(0.0, std::max(most, 1.0));
    format.Finalize();
    return format.FormatValue(value);
}

std::string UltraCanvasEnginePieChart::FormatPercent(double percent) const {
    if (percentFormatter) return percentFormatter(percent);
    ChartAxis format;
    format.decimals = 1;
    format.unitSuffix = "%";
    return format.FormatValue(percent);
}

std::string UltraCanvasEnginePieChart::LabelText(const SliceSpan& span) const {
    const EnginePieSlice& slice = slices[span.index];
    const std::string value = FormatValue(slice.value);
    const std::string percent = FormatPercent(span.share * 100.0);
    switch (labelContent) {
        case EnginePieLabelContent::Name:            return slice.name;
        case EnginePieLabelContent::Value:           return value;
        case EnginePieLabelContent::Percentage:      return percent;
        case EnginePieLabelContent::NameValue:       return slice.name + ": " + value;
        case EnginePieLabelContent::NamePercentage:  return slice.name + ": " + percent;
        case EnginePieLabelContent::ValuePercentage: return value + " (" + percent + ")";
        case EnginePieLabelContent::All:
        default:
            return slice.name + ": " + value + " (" + percent + ")";
    }
}

std::string UltraCanvasEnginePieChart::InsideLabelText(const SliceSpan& span) const {
    // Inside a slice there is room for the number, not the sentence.
    switch (labelContent) {
        case EnginePieLabelContent::Name:  return slices[span.index].name;
        case EnginePieLabelContent::Value:
        case EnginePieLabelContent::NameValue:
            return FormatValue(slices[span.index].value);
        default:
            return FormatPercent(span.share * 100.0);
    }
}

bool UltraCanvasEnginePieChart::WantsOutside(IRenderContext* ctx, const SliceSpan& span) const {
    if (labelPosition == EnginePieLabelPosition::Outside) return true;
    if (labelPosition != EnginePieLabelPosition::Auto) return false;
    const auto* polar = dynamic_cast<const ChartPolarProjection*>(Frame().projection);
    if (!polar) return true;
    // Auto: inside when the label fits the slice at its middle radius.
    const double outer = polar->OuterRadius() / (1.0 + MaxExplosion());
    const double inner = donutMode ? outer * innerFraction : 0.0;
    const double middle = (inner + outer) * 0.55;
    ctx->SetFontSize(axisFontSize);
    const Size2Di size = ctx->GetTextLineDimensions(InsideLabelText(span));
    const double arc = span.share * kTwoPi * middle;
    return arc < size.width + 8.0 || (outer - inner) < size.height + 6.0;
}

Rect2Dd UltraCanvasEnginePieChart::CenterTextBox(IRenderContext* ctx) const {
    const auto* polar = dynamic_cast<const ChartPolarProjection*>(Frame().projection);
    if (!polar || (centerText.empty() && centerCaption.empty())) return Rect2Dd();
    ctx->SetFontSize(16.0f);
    const Size2Di main = ctx->GetTextLineDimensions(centerText);
    ctx->SetFontSize(11.0f);
    const Size2Di caption = ctx->GetTextLineDimensions(centerCaption);
    const double width = std::max(main.width, caption.width);
    const double height = main.height + (centerCaption.empty() ? 0.0 : caption.height + 2.0);
    const Point2Dd c = polar->Center();
    return Rect2Dd(c.x - width * 0.5, c.y - height * 0.5, width, height);
}

void UltraCanvasEnginePieChart::RefreshLegend() {
    std::vector<ChartLegendEntry> entries;
    for (size_t i = 0; i < slices.size(); ++i) {
        ChartLegendEntry entry(slices[i].name, SliceColor(i), LegendSwatch::Circle);
        entry.enabled = !sliceHidden[i];
        entries.push_back(entry);
    }
    SetLegendEntries(entries);
}

void UltraCanvasEnginePieChart::OnChartThemeChanged() { RefreshLegend(); }

void UltraCanvasEnginePieChart::OnLegendEntryToggled(size_t entryIndex, bool enabled) {
    if (entryIndex >= sliceHidden.size()) return;
    // A hidden slice leaves the shares: the others close the gap, and the
    // percentages and labels re-solve.
    sliceHidden[entryIndex] = !enabled;
    MarkEngineDirty(ChartDirty::Style);
}

// =============================================================================
// ENGINE CONTRACT
// =============================================================================

void UltraCanvasEnginePieChart::DescribeAxes(ChartAxisSet& axes) {
    // Both axes exist only to map the shares: the slices themselves are the
    // furniture, so neither is drawn and no grid is ruled.
    ChartAxis share("share");
    share.side = ChartAxisSide::Bottom;
    share.SetRange(0.0, 1.0);
    share.visible = false;
    axes.Add(share);

    ChartAxis radius("radius");
    radius.side = ChartAxisSide::Left;
    radius.SetRange(0.0, 1.0);
    radius.visible = false;
    axes.Add(radius);

    SetGridAxis(static_cast<size_t>(-1));
    SetDomainGridAxis(static_cast<size_t>(-1));
}

void UltraCanvasEnginePieChart::RenderChartContent(IRenderContext* ctx,
                                                   const ChartEngineFrame& frame) {
    const std::vector<SliceSpan> spans = Spans();
    if (spans.empty()) return;
    const auto* polar = dynamic_cast<const ChartPolarProjection*>(frame.projection);
    if (!polar) return;
    const double progress = frame.animationProgress;
    const double outerV = SliceOuterV();
    const Point2Dd center = polar->Center();
    const double sliceRadius = polar->OuterRadius() / (1.0 + MaxExplosion());
    const Color border = borderColor.a > 0 ? borderColor
                                           : (frame.theme ? frame.theme->plotAreaColor
                                                          : Colors::White);

    for (const SliceSpan& span : spans) {
        // The sweep-in: the whole pie unrolls from the start angle.
        const double u0 = span.u0 * progress, u1 = span.u1 * progress;
        const bool hovered = HoveredRegionId() == SliceRegion(span.index);
        const float explosion = ExplosionOf(span.index) + (hovered ? hoverExplosion : 0.0f);
        const Point2Dd offset = ExplosionOffset(frame, (u0 + u1) * 0.5, explosion);

        std::vector<Point2Dd> outline = BuildBarOutline(
            *frame.projection, u0, 0.0, u1, outerV,
            std::max(4, static_cast<int>(span.share * 96.0)));
        if (outline.size() < 3) continue;
        for (Point2Dd& p : outline) { p.x += offset.x; p.y += offset.y; }

        const Color color = SliceColor(span.index);
        std::shared_ptr<IPaintPattern> gradient;
        if (radialGradients) {
            gradient = ctx->CreateRadialGradientPattern(
                center.x + offset.x, center.y + offset.y, 0.0,
                center.x + offset.x, center.y + offset.y, sliceRadius,
                {GradientStop(0.0, color.Lighten(0.45f)), GradientStop(0.85, color),
                 GradientStop(1.0, color.Darken(0.2f))});
        }
        if (gradient) ctx->SetFillPaint(gradient);
        else ctx->SetFillPaint(hovered ? color.Lighten(0.12f) : color);
        ctx->FillLinePath(outline);
        if (borderWidth > 0.0f) {
            ctx->SetStrokePaint(border);
            ctx->SetStrokeWidth(borderWidth);
            ctx->DrawLinePath(outline, true);
        }

        const EnginePieSlice& slice = slices[span.index];
        AddHitRegion(outline, SliceRegion(span.index),
                     slice.name + "\n" + FormatValue(slice.value) + " (" +
                         FormatPercent(span.share * 100.0) + ")");

        // Inside labels sit in their own slice and cannot collide, so they
        // are drawn here rather than solved; outside ones ride the plan.
        if (labelPosition == EnginePieLabelPosition::Hidden || progress < 1.0 ||
            WantsOutside(ctx, span)) continue;
        const std::string text = InsideLabelText(span);
        ctx->SetFontSize(axisFontSize);
        const Size2Di size = ctx->GetTextLineDimensions(text);
        const double innerV = 0.0;
        const Point2Dd at = frame.projection->ToScreen(
            ChartNormalizedPoint((u0 + u1) * 0.5, innerV + (outerV - innerV) * 0.55));
        // Light text on dark slices, dark on light ones.
        const double luminance = 0.299 * color.r + 0.587 * color.g + 0.114 * color.b;
        ctx->SetTextPaint(luminance < 150.0 ? Colors::White : Color(40, 40, 40, 255));
        ctx->DrawText(text, Point2Dd(at.x + offset.x - size.width * 0.5,
                                     at.y + offset.y - size.height * 0.5));
    }
}

void UltraCanvasEnginePieChart::MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) {
    if (labelPosition != EnginePieLabelPosition::Outside &&
        labelPosition != EnginePieLabelPosition::Auto) return;
    // Room beside the pie for the widest outside label and its leader: the
    // label plan may place labels in the bands the content reserves.
    ctx->SetFontSize(axisFontSize);
    double widest = 0.0, tallest = 0.0;
    for (const SliceSpan& span : Spans()) {
        const Size2Di size = ctx->GetTextLineDimensions(LabelText(span));
        widest = std::max(widest, static_cast<double>(size.width));
        tallest = std::max(tallest, static_cast<double>(size.height));
    }
    if (widest <= 0.0) return;
    request.Reserve(ChartAxisEdge::Left, widest + 26.0);
    request.Reserve(ChartAxisEdge::Right, widest + 26.0);
    request.Reserve(ChartAxisEdge::Top, tallest + 10.0);
    request.Reserve(ChartAxisEdge::Bottom, tallest + 10.0);
}

void UltraCanvasEnginePieChart::CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) {
    labelOutside.assign(slices.size(), false);
    const auto* polar = dynamic_cast<const ChartPolarProjection*>(Frame().projection);
    if (!polar) return;

    // The centre text is fixed; the solved labels steer round it.
    const Rect2Dd centre = CenterTextBox(ctx);
    if (centre.width > 0.0) broker.AddObstacleRect(centre);
    if (labelPosition == EnginePieLabelPosition::Hidden) return;

    const Point2Dd c = polar->Center();
    const double outerV = SliceOuterV();
    for (const SliceSpan& span : Spans()) {
        if (!WantsOutside(ctx, span)) continue;
        labelOutside[span.index] = true;
        const double mid = (span.u0 + span.u1) * 0.5;
        const Point2Dd offset = ExplosionOffset(Frame(), mid, ExplosionOf(span.index));
        Point2Dd rim = Frame().projection->ToScreen(ChartNormalizedPoint(mid, outerV));
        rim.x += offset.x;
        rim.y += offset.y;
        const double dx = rim.x - c.x, dy = rim.y - c.y;
        const double len = std::max(1e-9, std::sqrt(dx * dx + dy * dy));

        ChartLabelRequest label;
        label.text = LabelText(span);
        label.klass = ChartLabelClass::ValueLabel;
        // Anchored a little beyond the rim, on the slice's own side of the pie.
        label.anchor = Point2Dd(rim.x + dx / len * 14.0, rim.y + dy / len * 14.0);
        ctx->SetFontSize(axisFontSize);
        const Size2Di size = ctx->GetTextLineDimensions(label.text);
        label.textSize = Size2Dd(size.width, size.height);
        label.preferredSide = (dx >= 0.0) ? LabelSide::Right : LabelSide::Left;
        label.priority = static_cast<int>(span.share * 100.0);   // big slices first
        label.userData = span.index;
        broker.Add(label);
    }
}

void UltraCanvasEnginePieChart::RenderInteractionOverlay(IRenderContext* ctx,
                                                         const ChartEngineFrame& frame) {
    const auto* polar = dynamic_cast<const ChartPolarProjection*>(frame.projection);
    if (!polar) return;

    // Leader lines: from the slice's rim, out along its radius, then to the
    // nearest edge of wherever the plan put the label.
    if (leaderLines && frame.labelPlan && frame.animationProgress >= 1.0) {
        const std::vector<SliceSpan> spans = Spans();
        const Point2Dd c = polar->Center();
        ctx->SetStrokePaint(leaderColor);
        ctx->SetStrokeWidth(leaderWidth);
        for (const PlacedChartLabel& label : frame.labelPlan->labels) {
            if (label.suppressed || label.klass != ChartLabelClass::ValueLabel) continue;
            const auto it = std::find_if(spans.begin(), spans.end(), [&](const SliceSpan& s) {
                return s.index == label.userData;
            });
            if (it == spans.end()) continue;
            const double mid = (it->u0 + it->u1) * 0.5;
            const bool hovered = HoveredRegionId() == SliceRegion(it->index);
            const Point2Dd offset = ExplosionOffset(
                frame, mid, ExplosionOf(it->index) + (hovered ? hoverExplosion : 0.0f));
            Point2Dd rim = frame.projection->ToScreen(ChartNormalizedPoint(mid, SliceOuterV()));
            rim.x += offset.x;
            rim.y += offset.y;
            const double dx = rim.x - c.x, dy = rim.y - c.y;
            const double len = std::max(1e-9, std::sqrt(dx * dx + dy * dy));
            const Point2Dd elbow(rim.x + dx / len * 8.0, rim.y + dy / len * 8.0);
            const Point2Dd target(std::clamp(elbow.x, label.bounds.Left(), label.bounds.Right()),
                                  std::clamp(elbow.y, label.bounds.Top(), label.bounds.Bottom()));
            ctx->DrawLinePath({rim, elbow, target}, false);
        }
    }

    // The centre text (a KPI in a donut, a caption under it).
    const Rect2Dd box = CenterTextBox(ctx);
    if (box.width <= 0.0) return;
    const Color text = frame.theme ? frame.theme->titleTextColor : Color(50, 50, 50, 255);
    ctx->SetTextPaint(text);
    ctx->SetFontWeight(FontWeight::Bold);
    ctx->SetFontSize(16.0f);
    const Size2Di main = ctx->GetTextLineDimensions(centerText);
    ctx->DrawText(centerText, Point2Dd(box.x + (box.width - main.width) * 0.5, box.y));
    ctx->SetFontWeight(FontWeight::Normal);
    if (!centerCaption.empty()) {
        ctx->SetFontSize(11.0f);
        const Size2Di caption = ctx->GetTextLineDimensions(centerCaption);
        ctx->SetTextPaint(frame.theme ? frame.theme->axisLabelColor : Color(90, 90, 90, 255));
        ctx->DrawText(centerCaption, Point2Dd(box.x + (box.width - caption.width) * 0.5,
                                              box.y + main.height + 2.0));
    }
}

} // namespace UltraCanvas

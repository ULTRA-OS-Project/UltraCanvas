// Plugins/Charts/UltraCanvasCumulativeFlowChart.cpp
// Cumulative flow diagram (CFD) element: stacked per-stage bands over time —
// the standard Kanban analytics chart. Binds directly to a KanbanDataSource
// (deriving daily stage counts from its move history) or takes manual series,
// and draws the classic Lead Time / Cycle Time / WIP span annotations.
// Version: 1.1.0
// Last Modified: 2026-08-20
// V1.1.0: legend: migrated to the shared ChartLegend component
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasCumulativeFlowChart.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace UltraCanvas {

namespace {

    // "Nice" tick step targeting 4-6 horizontal grid lines.
    double NiceTickStep(double range) {
        if (range <= 0) return 1.0;
        double raw = range / 5.0;
        double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
        double normalized = raw / magnitude;
        double nice = normalized < 1.5 ? 1.0
                     : normalized < 3.0 ? 2.0
                     : normalized < 7.0 ? 5.0 : 10.0;
        return nice * magnitude;
    }

    std::string FormatCount(double value) {
        char buf[32];
        if (std::fabs(value - std::round(value)) < 1e-9) {
            std::snprintf(buf, sizeof(buf), "%.0f", value);
        } else {
            std::snprintf(buf, sizeof(buf), "%.1f", value);
        }
        return buf;
    }

} // namespace

// =============================================================================
// STYLE PRESETS
// =============================================================================

namespace CumulativeFlowStyles {

    CumulativeFlowChartStyle CreateModern() {
        CumulativeFlowChartStyle s;
        s.palette = {Color(120, 170, 229), Color(84, 110, 190),
                     Color(246, 187, 66), Color(60, 170, 145),
                     Color(190, 120, 200), Color(230, 120, 105)};
        return s;
    }

    CumulativeFlowChartStyle CreateOlive() {
        CumulativeFlowChartStyle s;
        // The olive/amber report look of the reference image.
        s.palette = {Color(176, 184, 150), Color(96, 104, 74),
                     Color(240, 195, 80), Color(224, 162, 26),
                     Color(140, 148, 110), Color(200, 140, 60)};
        s.legendPanelColor = Color(244, 244, 242);
        s.legendBorderColor = Color(226, 226, 222);
        s.annotationBoxColor = Color(230, 229, 222, 240);
        s.annotationColor = Color(235, 235, 235, 210);
        s.annotationTextColor = Color(75, 75, 68);
        s.gridColor = Color(235, 235, 231);
        return s;
    }

    CumulativeFlowChartStyle CreateDark() {
        CumulativeFlowChartStyle s = CreateModern();
        s.background = Color(30, 32, 38);
        s.plotBackground = Color(36, 39, 46);
        s.axisColor = Color(95, 100, 110);
        s.gridColor = Color(52, 56, 64);
        s.tickTextColor = Color(165, 170, 180);
        s.legendPanelColor = Color(42, 45, 53);
        s.legendBorderColor = Color(62, 66, 76);
        s.legendTextColor = Color(210, 213, 220);
        s.annotationColor = Color(180, 183, 190);
        s.annotationBoxColor = Color(55, 59, 68, 235);
        s.annotationTextColor = Color(220, 222, 228);
        s.markerFill = Color(36, 39, 46);
        return s;
    }

} // namespace CumulativeFlowStyles

// =============================================================================
// ELEMENT — DATA
// =============================================================================

UltraCanvasCumulativeFlowChartElement::UltraCanvasCumulativeFlowChartElement(
        const std::string& id, int x, int y, int width, int height)
        : UltraCanvasChartElementBase(id, x, y, width, height) {
    style = CumulativeFlowStyles::CreateModern();
    showBackground = false;
    showGrid = false;
    showAxes = false;
    ApplyLegendStyle();
}

void UltraCanvasCumulativeFlowChartElement::SetStages(
        const std::vector<CFDStage>& newStages) {
    stages = newStages;
    RequestRedraw();
}

void UltraCanvasCumulativeFlowChartElement::AddStage(
        const std::string& name, const std::vector<double>& values,
        const Color& color) {
    CFDStage stage;
    stage.name = name;
    stage.values = values;
    stage.color = color;
    stages.push_back(stage);
    RequestRedraw();
}

void UltraCanvasCumulativeFlowChartElement::ClearStages() {
    stages.clear();
    periodLabels.clear();
    RequestRedraw();
}

void UltraCanvasCumulativeFlowChartElement::SetPeriodLabels(
        const std::vector<std::string>& labels) {
    periodLabels = labels;
    RequestRedraw();
}

void UltraCanvasCumulativeFlowChartElement::LoadFromKanban(
        const std::shared_ptr<KanbanDataSource>& board,
        const GanttDate& from, const GanttDate& to, int stepDays) {
    stages.clear();
    periodLabels.clear();
    if (!board || to < from) {
        RequestRedraw();
        return;
    }
    stepDays = std::max(1, stepDays);

    const auto& columns = board->GetColumns();
    for (const auto& col : columns) {
        CFDStage stage;
        stage.name = col.title;
        stage.color = col.customColor;   // Transparent falls back to palette
        stages.push_back(stage);
    }

    for (long serial = from.serial; serial <= to.serial; serial += stepDays) {
        GanttDate date(serial);
        std::map<int, int> counts = board->GetColumnCountsAt(date);
        for (size_t i = 0; i < columns.size(); ++i) {
            auto it = counts.find(columns[i].id);
            stages[i].values.push_back(it == counts.end() ? 0.0
                                                          : it->second);
        }
        if (stepDays >= 7) {
            int y, m, d;
            date.ToYMD(y, m, d);
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%02d-%02d", m, d);
            periodLabels.push_back(buf);
        } else {
            periodLabels.push_back(std::to_string(date.Day()));
        }
    }
    RequestRedraw();
}

// =============================================================================
// ELEMENT — ANNOTATIONS
// =============================================================================

void UltraCanvasCumulativeFlowChartElement::AddLeadTimeAnnotation(
        const std::string& label) {
    CFDAnnotation a;
    a.kind = CFDAnnotation::Kind::HorizontalSpan;
    a.label = label;
    a.fromBoundary = 0;       // Arrival (overall top) curve
    a.toBoundary = -1;        // Departure (top of Done) curve
    annotations.push_back(a);
    RequestRedraw();
}

void UltraCanvasCumulativeFlowChartElement::AddCycleTimeAnnotation(
        const std::string& label) {
    CFDAnnotation a;
    a.kind = CFDAnnotation::Kind::HorizontalSpan;
    a.label = label;
    a.fromBoundary = 1;       // Start of active work
    a.toBoundary = -1;
    a.value = -2;             // Auto, offset below the lead-time line
    annotations.push_back(a);
    RequestRedraw();
}

void UltraCanvasCumulativeFlowChartElement::AddWipAnnotation(
        const std::string& label) {
    CFDAnnotation a;
    a.kind = CFDAnnotation::Kind::VerticalSpan;
    a.label = label;
    a.fromBoundary = 1;       // Top of active work
    a.toBoundary = -1;        // Top of Done
    annotations.push_back(a);
    RequestRedraw();
}

void UltraCanvasCumulativeFlowChartElement::AddAnnotation(
        const CFDAnnotation& annotation) {
    annotations.push_back(annotation);
    RequestRedraw();
}

void UltraCanvasCumulativeFlowChartElement::ClearAnnotations() {
    annotations.clear();
    RequestRedraw();
}

void UltraCanvasCumulativeFlowChartElement::SetStyle(
        const CumulativeFlowChartStyle& newStyle) {
    style = newStyle;
    ApplyLegendStyle();
    RequestRedraw();
}

void UltraCanvasCumulativeFlowChartElement::StyleChanged() {
    ApplyLegendStyle();
    RequestRedraw();
}

// Map the chart style's legend fields (panel/border/text colors, font size,
// dot radius — including the per-preset values of CreateModern / CreateOlive /
// CreateDark) onto the shared ChartLegendStyle.
void UltraCanvasCumulativeFlowChartElement::ApplyLegendStyle() {
    ChartLegendStyle ls;
    if (!style.fontFamily.empty()) ls.fontFamily = style.fontFamily;
    ls.fontSize = style.legendFontSize;
    ls.textColor = style.legendTextColor;
    ls.titleColor = style.legendTextColor;
    ls.drawBackground = true;
    ls.backgroundColor = style.legendPanelColor;
    ls.drawBorder = true;
    ls.borderColor = style.legendBorderColor;
    ls.cornerRadius = 4.0f;                     // Old rounded panel radius
    // Old look: a colored dot per stage.
    ls.swatchWidth = style.legendDotRadius * 2.0f;
    ls.swatchHeight = style.legendDotRadius * 2.0f;
    ls.drawSwatchBorder = false;
    legend.SetStyle(ls);
    // CFDLegendPosition mapping: Right -> RightCenter (the closest shared
    // position to the old right-hand side panel), Hidden -> invisible.
    legend.SetPosition(ChartLegendPosition::RightCenter);
    legend.SetVisible(style.legendPosition == CFDLegendPosition::Right);
}

// =============================================================================
// ELEMENT — GEOMETRY HELPERS
// =============================================================================

size_t UltraCanvasCumulativeFlowChartElement::PeriodCount() const {
    size_t count = 0;
    for (const auto& stage : stages) {
        count = std::max(count, stage.values.size());
    }
    return count;
}

double UltraCanvasCumulativeFlowChartElement::StageValue(size_t stageIndex,
                                                         size_t period) const {
    if (stageIndex >= stages.size()) return 0.0;
    const auto& values = stages[stageIndex].values;
    return period < values.size() ? values[period] : 0.0;
}

double UltraCanvasCumulativeFlowChartElement::BoundaryValue(
        size_t boundaryIndex, size_t period) const {
    double sum = 0.0;
    for (size_t i = boundaryIndex; i < stages.size(); ++i) {
        sum += StageValue(i, period);
    }
    return sum;
}

Color UltraCanvasCumulativeFlowChartElement::StageColor(size_t stageIndex) const {
    if (stageIndex < stages.size() && stages[stageIndex].color.a != 0) {
        return stages[stageIndex].color;
    }
    const std::vector<Color>& palette =
            style.palette.empty() ? CumulativeFlowStyles::CreateModern().palette
                                  : style.palette;
    return palette[stageIndex % palette.size()];
}

double UltraCanvasCumulativeFlowChartElement::XForPeriod(double period) const {
    if (periodCount <= 1) return plotRect.x + plotRect.width / 2.0;
    return plotRect.x + plotRect.width * period /
           static_cast<double>(periodCount - 1);
}

double UltraCanvasCumulativeFlowChartElement::YForValue(double value) const {
    if (yMax <= 0) return plotRect.y + plotRect.height;
    return plotRect.y + plotRect.height * (1.0 - value / yMax);
}

int UltraCanvasCumulativeFlowChartElement::ResolveBoundary(int boundary) const {
    int last = static_cast<int>(stages.size()) - 1;
    if (boundary < 0) return last;
    return std::clamp(boundary, 0, last);
}

double UltraCanvasCumulativeFlowChartElement::CrossingPeriod(
        size_t boundaryIndex, double value) const {
    if (periodCount == 0) return 0.0;
    double previous = BoundaryValue(boundaryIndex, 0);
    if (previous >= value) return 0.0;
    for (size_t p = 1; p < periodCount; ++p) {
        double current = BoundaryValue(boundaryIndex, p);
        if ((previous < value && current >= value) ||
            (previous > value && current <= value)) {
            double t = current == previous
                               ? 0.0
                               : (value - previous) / (current - previous);
            return static_cast<double>(p - 1) + std::clamp(t, 0.0, 1.0);
        }
        previous = current;
    }
    return static_cast<double>(periodCount - 1);
}

// =============================================================================
// ELEMENT — RENDERING
// =============================================================================

void UltraCanvasCumulativeFlowChartElement::ComputePlot(IRenderContext* ctx) {
    Rect2Df local = GetLocalBounds();
    double top = local.y + style.paddingTop;
    if (!chartTitle.empty()) top += 26.0;

    chartArea = Rect2Dd(local.x + style.paddingLeft, top,
                        local.width - style.paddingLeft - style.paddingRight,
                        local.y + local.height - style.paddingBottom - top);

    // Shared legend: one entry per stage (stage name, stage color), sized by
    // its own measurement. Replaces the old fixed-width right panel.
    std::vector<ChartLegendEntry> entries;
    entries.reserve(stages.size());
    for (size_t k = 0; k < stages.size(); ++k) {
        entries.emplace_back(stages[k].name, StageColor(k),
                             LegendSwatch::Circle);
    }
    legend.SetEntries(entries);
    ctx->PushState();
    legend.Measure(ctx, chartArea);
    ctx->PopState();
    plotRect = legend.RemainingArea(chartArea);

    periodCount = PeriodCount();
    double maxTotal = 0.0;
    for (size_t p = 0; p < periodCount; ++p) {
        maxTotal = std::max(maxTotal, BoundaryValue(0, p));
    }
    double step = style.yTickStep > 0 ? style.yTickStep : NiceTickStep(maxTotal);
    yMax = std::max(step, std::ceil(maxTotal / step + 0.25) * step);
}

void UltraCanvasCumulativeFlowChartElement::Render(IRenderContext* ctx,
                                                   const Rect2Df& dirtyRect) {
    if (!ctx) return;
    ctx->PushState();
    if (!style.fontFamily.empty()) ctx->SetFontFamily(style.fontFamily);

    Rect2Df local = GetLocalBounds();
    ctx->DrawFilledRectangle(Rect2Dd(local.x, local.y, local.width, local.height),
                             style.background);
    ComputePlot(ctx);

    if (stages.empty() || periodCount == 0) {
        ctx->SetTextPaint(style.tickTextColor);
        ctx->SetFontSize(13.0f);
        Size2Di sz = ctx->GetTextLineDimensions("No flow data");
        ctx->DrawText("No flow data",
                      Point2Dd(local.x + (local.width - sz.width) / 2.0,
                               local.y + (local.height - sz.height) / 2.0));
        ctx->PopState();
        return;
    }

    if (!chartTitle.empty()) {
        ctx->SetTextPaint(style.legendTextColor);
        ctx->SetFontSize(14.0f);
        ctx->SetFontWeight(FontWeight::Bold);
        Size2Di sz = ctx->GetTextLineDimensions(chartTitle);
        ctx->DrawText(chartTitle,
                      Point2Dd(plotRect.x + (plotRect.width - sz.width) / 2.0,
                               local.y + style.paddingTop));
        ctx->SetFontWeight(FontWeight::Normal);
    }

    RenderChart(ctx);
    ctx->PopState();
}

void UltraCanvasCumulativeFlowChartElement::RenderChart(IRenderContext* ctx) {
    if (style.plotBackground.a != 0) {
        ctx->DrawFilledRectangle(plotRect, style.plotBackground);
    }
    RenderGridAndAxes(ctx);

    ctx->PushState();
    ctx->ClipRect(Rect2Dd(plotRect.x - 1, plotRect.y - 1,
                          plotRect.width + 2, plotRect.height + 2));
    RenderBands(ctx);
    if (style.showMarkers) RenderMarkers(ctx);

    // Hover guide.
    if (hoveredPeriod >= 0 && hoveredPeriod < static_cast<int>(periodCount)) {
        double x = XForPeriod(hoveredPeriod);
        ctx->SetStrokePaint(style.axisColor.WithAlpha(120));
        ctx->SetStrokeWidth(1.0f);
        ctx->SetLineDash(UCDashPattern({3.0, 3.0}));
        ctx->DrawLine(Point2Dd(x, plotRect.y),
                      Point2Dd(x, plotRect.y + plotRect.height));
        ctx->SetLineDash(UCDashPattern::EMPTY);
    }
    ctx->PopState();

    RenderAnnotations(ctx);
    legend.Render(ctx, chartArea);
}

void UltraCanvasCumulativeFlowChartElement::RenderGridAndAxes(IRenderContext* ctx) {
    double step = style.yTickStep > 0 ? style.yTickStep : NiceTickStep(yMax);

    ctx->SetFontSize(style.tickFontSize);
    for (double v = 0; v <= yMax + 1e-9; v += step) {
        double y = YForValue(v);
        if (style.showHorizontalGrid && v > 0) {
            ctx->SetStrokePaint(style.gridColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine(Point2Dd(plotRect.x, y),
                          Point2Dd(plotRect.x + plotRect.width, y));
        }
        ctx->SetTextPaint(style.tickTextColor);
        std::string label = FormatCount(v);
        Size2Di sz = ctx->GetTextLineDimensions(label);
        ctx->DrawText(label, Point2Dd(plotRect.x - sz.width - 8,
                                      y - sz.height / 2.0));
    }

    // Axis lines.
    ctx->SetStrokePaint(style.axisColor);
    ctx->SetStrokeWidth(1.2f);
    ctx->DrawLine(Point2Dd(plotRect.x, plotRect.y),
                  Point2Dd(plotRect.x, plotRect.y + plotRect.height));
    ctx->DrawLine(Point2Dd(plotRect.x, plotRect.y + plotRect.height),
                  Point2Dd(plotRect.x + plotRect.width,
                           plotRect.y + plotRect.height));

    // X labels, thinned to avoid collisions.
    ctx->SetFontSize(style.xLabelFontSize);
    if (style.boldXLabels) ctx->SetFontWeight(FontWeight::Bold);
    ctx->SetTextPaint(style.tickTextColor);
    double maxLabelW = 0;
    for (size_t p = 0; p < periodCount; ++p) {
        maxLabelW = std::max(maxLabelW, static_cast<double>(
                ctx->GetTextLineDimensions(PeriodLabel(p)).width));
    }
    double slot = periodCount > 1 ? plotRect.width / (periodCount - 1) : plotRect.width;
    size_t every = slot > 0 ? std::max<size_t>(1, static_cast<size_t>(
            std::ceil((maxLabelW + 10.0) / slot))) : 1;
    for (size_t p = 0; p < periodCount; p += every) {
        std::string label = PeriodLabel(p);
        Size2Di sz = ctx->GetTextLineDimensions(label);
        ctx->DrawText(label, Point2Dd(XForPeriod(p) - sz.width / 2.0,
                                      plotRect.y + plotRect.height + 8));
    }
    ctx->SetFontWeight(FontWeight::Normal);
}

void UltraCanvasCumulativeFlowChartElement::RenderBands(IRenderContext* ctx) {
    // Bottom band (Done, last stage) first, then upward, so upper edges
    // stroke over lower fills.
    for (int k = static_cast<int>(stages.size()) - 1; k >= 0; --k) {
        std::vector<Point2Dd> polygon;
        polygon.reserve(periodCount * 2);
        for (size_t p = 0; p < periodCount; ++p) {
            polygon.emplace_back(XForPeriod(p),
                                 YForValue(BoundaryValue(k, p)));
        }
        for (size_t p = periodCount; p > 0; --p) {
            double lower = k + 1 < static_cast<int>(stages.size())
                                   ? BoundaryValue(k + 1, p - 1) : 0.0;
            polygon.emplace_back(XForPeriod(p - 1), YForValue(lower));
        }
        Color fill = StageColor(k);
        fill.a = static_cast<uint8_t>(255 * style.bandAlpha);
        ctx->SetFillPaint(fill);
        ctx->FillLinePath(polygon);
    }

    if (style.showEdgeLines) {
        for (size_t k = 0; k < stages.size(); ++k) {
            std::vector<Point2Dd> line;
            line.reserve(periodCount);
            for (size_t p = 0; p < periodCount; ++p) {
                line.emplace_back(XForPeriod(p),
                                  YForValue(BoundaryValue(k, p)));
            }
            ctx->SetStrokePaint(StageColor(k).Darken(style.edgeDarken));
            ctx->SetStrokeWidth(style.edgeLineWidth);
            ctx->DrawLinePath(line, false);
        }
    }
}

void UltraCanvasCumulativeFlowChartElement::RenderMarkers(IRenderContext* ctx) {
    for (size_t k = 0; k < stages.size(); ++k) {
        Color edge = StageColor(k).Darken(style.edgeDarken + 0.1f);
        for (size_t p = 0; p < periodCount; ++p) {
            Point2Dd center(XForPeriod(p), YForValue(BoundaryValue(k, p)));
            ctx->DrawFilledCircle(center, style.markerRadius,
                                  style.markerFill, edge, 1.4f);
        }
    }
}

void UltraCanvasCumulativeFlowChartElement::DrawArrowHead(IRenderContext* ctx,
                                                          const Point2Dd& tip,
                                                          double angle,
                                                          const Color& color) {
    double s = style.annotationArrowSize;
    Point2Dd a(tip.x - s * std::cos(angle - 0.42),
               tip.y - s * std::sin(angle - 0.42));
    Point2Dd b(tip.x - s * std::cos(angle + 0.42),
               tip.y - s * std::sin(angle + 0.42));
    ctx->SetFillPaint(color);
    ctx->FillLinePath({tip, a, b});
}

void UltraCanvasCumulativeFlowChartElement::RenderAnnotations(IRenderContext* ctx) {
    if (annotations.empty() || stages.empty() || periodCount < 2) return;

    double finalTotal = BoundaryValue(0, periodCount - 1);
    ctx->SetFontSize(style.annotationFontSize);

    for (const auto& a : annotations) {
        int from = ResolveBoundary(a.fromBoundary);
        int to = ResolveBoundary(a.toBoundary);
        Color lineColor = style.annotationColor;

        if (a.kind == CFDAnnotation::Kind::HorizontalSpan) {
            double value = a.value;
            if (value == -1) value = finalTotal * 0.5;
            else if (value == -2) value = finalTotal * 0.38;   // Cycle default
            double p1 = CrossingPeriod(static_cast<size_t>(from), value);
            double p2 = CrossingPeriod(static_cast<size_t>(to), value);
            if (p2 < p1) std::swap(p1, p2);
            double y = YForValue(value);
            double x1 = XForPeriod(p1), x2 = XForPeriod(p2);
            if (x2 - x1 < style.annotationArrowSize * 2 + 4) continue;

            ctx->SetStrokePaint(lineColor);
            ctx->SetStrokeWidth(style.annotationLineWidth);
            ctx->DrawLine(Point2Dd(x1 + 2, y), Point2Dd(x2 - 2, y));
            DrawArrowHead(ctx, Point2Dd(x1, y), M_PI, lineColor);
            DrawArrowHead(ctx, Point2Dd(x2, y), 0.0, lineColor);

            Size2Di sz = ctx->GetTextLineDimensions(a.label);
            Rect2Dd box((x1 + x2) / 2.0 - sz.width / 2.0 - 8, y - sz.height - 12,
                        sz.width + 16, sz.height + 8);
            ctx->SetFillPaint(style.annotationBoxColor);
            ctx->DrawFilledRectangle(box, style.annotationBoxColor);
            ctx->SetTextPaint(style.annotationTextColor);
            ctx->DrawText(a.label, Point2Dd(box.x + 8, box.y + 4));
        } else {
            double position = a.position >= 0
                                      ? std::min(a.position,
                                                 static_cast<double>(periodCount - 1))
                                      : (periodCount - 1) / 3.0;
            // Interpolated boundary values at the (possibly fractional) x.
            size_t p0 = static_cast<size_t>(position);
            size_t p1 = std::min(p0 + 1, periodCount - 1);
            double t = position - p0;
            double upper = BoundaryValue(from, p0) * (1 - t) +
                           BoundaryValue(from, p1) * t;
            double lower = BoundaryValue(to, p0) * (1 - t) +
                           BoundaryValue(to, p1) * t;
            if (upper < lower) std::swap(upper, lower);
            double x = XForPeriod(position);
            double y1 = YForValue(upper), y2 = YForValue(lower);
            if (y2 - y1 < style.annotationArrowSize * 2 + 4) continue;

            ctx->SetStrokePaint(lineColor);
            ctx->SetStrokeWidth(style.annotationLineWidth);
            ctx->DrawLine(Point2Dd(x, y1 + 2), Point2Dd(x, y2 - 2));
            DrawArrowHead(ctx, Point2Dd(x, y1), -M_PI / 2.0, lineColor);
            DrawArrowHead(ctx, Point2Dd(x, y2), M_PI / 2.0, lineColor);

            Size2Di sz = ctx->GetTextLineDimensions(a.label);
            Rect2Dd box(x + 10, (y1 + y2) / 2.0 - sz.height / 2.0 - 4,
                        sz.width + 16, sz.height + 8);
            ctx->SetFillPaint(style.annotationBoxColor);
            ctx->DrawFilledRectangle(box, style.annotationBoxColor);
            ctx->SetTextPaint(style.annotationTextColor);
            ctx->DrawText(a.label, Point2Dd(box.x + 8, box.y + 4));
        }
    }
}

std::string UltraCanvasCumulativeFlowChartElement::PeriodLabel(size_t period) const {
    if (period < periodLabels.size() && !periodLabels[period].empty()) {
        return periodLabels[period];
    }
    return std::to_string(period + 1);
}

// =============================================================================
// ELEMENT — INTERACTION
// =============================================================================

std::string UltraCanvasCumulativeFlowChartElement::BuildPeriodTooltip(
        size_t period) const {
    std::ostringstream out;
    out << PeriodLabel(period);
    double total = BoundaryValue(0, period);
    for (size_t k = 0; k < stages.size(); ++k) {
        out << "\n" << stages[k].name << ": "
            << FormatCount(StageValue(k, period));
    }
    out << "\nTotal: " << FormatCount(total);
    return out.str();
}

bool UltraCanvasCumulativeFlowChartElement::HandleChartMouseMove(
        const Point2Di& mousePos) {
    bool inPlot = mousePos.x >= plotRect.x &&
                  mousePos.x <= plotRect.x + plotRect.width &&
                  mousePos.y >= plotRect.y &&
                  mousePos.y <= plotRect.y + plotRect.height;
    int period = -1;
    if (inPlot && periodCount > 1) {
        double t = (mousePos.x - plotRect.x) / plotRect.width *
                   (periodCount - 1);
        period = static_cast<int>(std::lround(t));
        period = std::clamp(period, 0, static_cast<int>(periodCount) - 1);
    } else if (inPlot && periodCount == 1) {
        period = 0;
    }

    if (period != hoveredPeriod) {
        hoveredPeriod = period;
        if (onHoveredPeriodChanged) onHoveredPeriodChanged(period);
        RequestRedraw();
    }

    if (period >= 0 && enableTooltips) {
        auto windowMousePos = MapFromLocal(mousePos, nullptr);
        UltraCanvasTooltipManager::UpdateAndShowTooltip(
                this->window, BuildPeriodTooltip(static_cast<size_t>(period)),
                windowMousePos);
        isTooltipActive = true;
        return true;
    }
    if (isTooltipActive) HideTooltip();
    return false;
}

bool UltraCanvasCumulativeFlowChartElement::OnEvent(const UCEvent& event) {
    if (IsDisabled() || !IsVisible()) return false;
    switch (event.type) {
        case UCEventType::MouseMove:
            return HandleChartMouseMove(event.pointer);
        case UCEventType::MouseLeave:
            if (hoveredPeriod != -1) {
                hoveredPeriod = -1;
                if (onHoveredPeriodChanged) onHoveredPeriodChanged(-1);
                RequestRedraw();
            }
            if (isTooltipActive) HideTooltip();
            return true;
        default:
            return false;
    }
}

} // namespace UltraCanvas

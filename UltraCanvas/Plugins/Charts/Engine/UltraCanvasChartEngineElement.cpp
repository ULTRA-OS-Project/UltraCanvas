// Plugins/Charts/Engine/UltraCanvasChartEngineElement.cpp
// The chart engine's three-phase render driver
// Version: 1.0.0
// Last Modified: 2026-08-06
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

UltraCanvasChartEngineElement::UltraCanvasChartEngineElement(
        const std::string& id, int x, int y, int w, int h)
    : UltraCanvasChartElementBase(id, x, y, w, h) {
    engineProjection = CreateChartProjection(projectionKind);
    // The engine draws its own furniture; the legacy base paths stay off.
    showAxes = false;
    showGrid = false;
    showValueLabels = false;
    engineProperties.Define("title", std::string());
}

UltraCanvasChartEngineElement::~UltraCanvasChartEngineElement() = default;

// =============================================================================
// CONFIGURATION
// =============================================================================

void UltraCanvasChartEngineElement::SetProjectionKind(ChartProjectionKind kind) {
    if (kind == projectionKind) return;
    projectionKind = kind;
    engineProjection = CreateChartProjection(kind);
    // Data, not just Geometry: DescribeAxes must run again, because a chart
    // may place its edge axes differently per projection (a horizontal bar
    // chart's value axis sits on the bottom, not the left).
    MarkEngineDirty(ChartDirty::Data | ChartDirty::Geometry);
}

ChartProjectionKind UltraCanvasChartEngineElement::GetProjectionKind() const {
    return projectionKind;
}

void UltraCanvasChartEngineElement::SetLabelPolicy(const ChartLabelPolicy& policy) {
    labelPolicy = policy;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasChartEngineElement::SetLabelOptions(const LabelPlacementOptions& options) {
    labelOptions = options;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasChartEngineElement::SetGridAxis(size_t axisIndex) {
    gridAxisIndex = axisIndex;
    RequestRedraw();
}

size_t UltraCanvasChartEngineElement::AddLimiter(const ChartLimiter& limiter) {
    limiters.push_back(limiter);
    MarkEngineDirty(ChartDirty::Style);       // captions join the label plan
    return limiters.size() - 1;
}

void UltraCanvasChartEngineElement::ClearLimiters() {
    limiters.clear();
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasChartEngineElement::SetShowLegend(bool show) {
    showLegend = show;
    MarkEngineDirty(ChartDirty::Geometry);    // the legend reserves margin
}

void UltraCanvasChartEngineElement::SetLegendEntries(const std::vector<ChartLegendEntry>& entries) {
    legendEntries = entries;
    MarkEngineDirty(ChartDirty::Geometry);
}

void UltraCanvasChartEngineElement::MarkEngineDirty(ChartDirty flags) {
    pendingDirty |= flags;
    RequestRedraw();
}

// =============================================================================
// PROPERTIES
// =============================================================================

bool UltraCanvasChartEngineElement::SetProperty(const std::string& key,
                                                const UCPropertyValue& value) {
    if (!engineProperties.Set(key, value)) return false;
    if (key == "title") {
        std::string title;
        if (UCPropertyAsString(value, title)) SetChartTitle(title);
    }
    OnEnginePropertyChanged(key, value);
    return true;
}

UCPropertyValue UltraCanvasChartEngineElement::GetProperty(const std::string& key) const {
    return engineProperties.Get(key);
}

std::vector<std::string> UltraCanvasChartEngineElement::ListProperties() const {
    return engineProperties.Keys();
}

// =============================================================================
// LAYOUT AND LABEL PLAN
// =============================================================================

void UltraCanvasChartEngineElement::EnsureEngineLayout(IRenderContext* ctx) {
    if (GetWidth() != lastLayoutWidth || GetHeight() != lastLayoutHeight) {
        pendingDirty |= ChartDirty::Geometry;
    }
    if (!DirtyRebuildsLayout(pendingDirty)) return;
    RunLayout(ctx);
}

void UltraCanvasChartEngineElement::RunLayout(IRenderContext* ctx) {
    lastLayoutWidth = GetWidth();
    lastLayoutHeight = GetHeight();

    // Axes are re-described only when the data or its presentation changed;
    // a pure resize keeps them and just re-solves the geometry.
    if (HasDirty(pendingDirty, ChartDirty::Data) ||
        HasDirty(pendingDirty, ChartDirty::Style) ||
        engineAxes.Count() == 0) {
        engineAxes.Clear();
        DescribeAxes(engineAxes);
        engineAxes.FinalizeAll();
    }

    // ---- measure pass -------------------------------------------------------
    ChartLayoutRequest request;

    // The title band is added on top of every other top reservation after the
    // measure pass. Reserve() merges by max, which would let axis endpoint
    // labels or top-side tick labels land in the same band and overprint the
    // title.
    double titleBand = 0.0;
    if (!chartTitle.empty()) {
        ctx->SetFontSize(titleFontSize);
        titleBand = ctx->GetTextLineHeight(chartTitle) + 10.0;
    }

    ctx->SetFontSize(axisFontSize);
    for (const ChartAxis& axis : engineAxes) {
        if (!axis.visible || axis.inPlot) continue;
        // Widest tick label of an edge axis decides its margin.
        double maxLabelExtent = 0.0;
        for (const ChartTick& tick : axis.GenerateTicks(targetTickCount)) {
            const Size2Di size = ctx->GetTextLineDimensions(tick.label);
            const bool vertical = (axis.side == ChartAxisSide::Left ||
                                   axis.side == ChartAxisSide::Right);
            maxLabelExtent = std::max(maxLabelExtent,
                                      vertical ? static_cast<double>(size.width)
                                               : static_cast<double>(size.height));
        }
        const double need = tickLength + tickLabelGap + maxLabelExtent + 6.0;
        switch (axis.side) {
            case ChartAxisSide::Left:   request.Reserve(ChartAxisEdge::Left, need); break;
            case ChartAxisSide::Right:  request.Reserve(ChartAxisEdge::Right, need); break;
            case ChartAxisSide::Top:    request.Reserve(ChartAxisEdge::Top, need); break;
            case ChartAxisSide::Bottom: request.Reserve(ChartAxisEdge::Bottom, need); break;
        }
        if (!axis.title.empty()) {
            const double titleNeed = ctx->GetTextLineHeight(axis.title) + 4.0;
            switch (axis.side) {
                case ChartAxisSide::Left:   request.Reserve(ChartAxisEdge::Left, need + titleNeed); break;
                case ChartAxisSide::Right:  request.Reserve(ChartAxisEdge::Right, need + titleNeed); break;
                case ChartAxisSide::Top:    request.Reserve(ChartAxisEdge::Top, need + titleNeed); break;
                case ChartAxisSide::Bottom: request.Reserve(ChartAxisEdge::Bottom, need + titleNeed); break;
            }
        }
    }

    if (showLegend && !legendEntries.empty()) {
        double widest = 0.0;
        for (const ChartLegendEntry& entry : legendEntries) {
            widest = std::max(widest,
                              static_cast<double>(ctx->GetTextLineWidth(entry.label)));
        }
        const double swatch = 14.0, pad = 8.0;
        legendRect.width = swatch + 6.0 + widest + pad * 2.0;
        legendRect.height = legendEntries.size() *
                                (ctx->GetTextLineHeight("Ag") + 6.0) + pad * 2.0;
        request.Reserve(ChartAxisEdge::Right, legendRect.width + 10.0);
    } else {
        legendRect = Rect2Dd(0, 0, 0, 0);
    }

    MeasureContent(ctx, request);
    request.margins.top += titleBand;

    // ---- solve --------------------------------------------------------------
    const Rect2Dd chartArea(0.0, 0.0, static_cast<double>(GetWidth()),
                            static_cast<double>(GetHeight()));
    const Rect2Dd plot = SolvePlotArea(chartArea, request.margins);
    engineProjection->SetPlotArea(plot);

    if (showLegend && !legendEntries.empty()) {
        legendRect.x = plot.Right() + 10.0;
        legendRect.y = plot.y;
    }

    frame.plotArea = plot;
    frame.axes = &engineAxes;
    frame.projection = engineProjection.get();
    frame.labelPlan = &labelPlan;
    frame.animationProgress = GetAnimationProgress();
    frame.generation = ++generation;

    // Geometry moved, so the solved labels are stale too.
    pendingDirty |= ChartDirty::Style;
    pendingDirty = static_cast<ChartDirty>(
        static_cast<uint32_t>(pendingDirty) &
        ~(static_cast<uint32_t>(ChartDirty::Geometry) |
          static_cast<uint32_t>(ChartDirty::Data)));
}

void UltraCanvasChartEngineElement::EnsureLabelPlan(IRenderContext* ctx) {
    if (!DirtyRebuildsLabels(pendingDirty) && labelPlan.generation != 0) return;
    BuildLabelPlan(ctx);
    pendingDirty = ChartDirty::Clean;
}

void UltraCanvasChartEngineElement::BuildLabelPlan(IRenderContext* ctx) {
    LabelPlacementOptions options = labelOptions;
    options.bounds = frame.plotArea;

    ChartLabelBroker broker(labelPolicy, options);

    // The legend is opaque chrome: solved labels must steer around it.
    if (showLegend && legendRect.width > 0.0) {
        ChartLabelRequest legend;
        legend.klass = ChartLabelClass::LegendEntry;
        legend.fixedBounds = legendRect;
        broker.Add(legend);
    }

    // Limiter captions ride the plan so they cannot overprint value labels.
    ctx->SetFontSize(axisFontSize);
    for (const ChartLimiter& limiter : limiters) {
        if (limiter.caption.empty() || limiter.axisIndex >= engineAxes.Count()) continue;
        const ChartAxis& axis = engineAxes.At(limiter.axisIndex);
        const double v = axis.Normalize(limiter.value);
        const Point2Dd anchor = engineProjection->ToScreen(ChartNormalizedPoint(0.98, v));
        ChartLabelRequest caption;
        caption.text = limiter.caption;
        caption.klass = ChartLabelClass::LimiterCaption;
        caption.anchor = anchor;
        const Size2Di size = ctx->GetTextLineDimensions(limiter.caption);
        caption.textSize = Size2Dd(size.width, size.height);
        caption.preferredSide = LabelSide::Top;
        caption.priority = 5;
        broker.Add(caption);
    }

    CollectChartLabels(ctx, broker);
    labelPlan = broker.Solve(generation);

    DeclutterAxisTickLabels(ctx);
}

void UltraCanvasChartEngineElement::DeclutterAxisTickLabels(IRenderContext* ctx) {
    labelPlan.tickVisible.clear();
    labelPlan.tickVisible.resize(engineAxes.Count());
    ctx->SetFontSize(axisFontSize);

    for (size_t i = 0; i < engineAxes.Count(); ++i) {
        const ChartAxis& axis = engineAxes.At(i);
        if (!axis.visible) continue;
        const std::vector<ChartTick> ticks = axis.GenerateTicks(targetTickCount);

        Point2Dd lowEnd, highEnd;
        AxisScreenLine(axis, lowEnd, highEnd);
        const bool alongY = std::abs(highEnd.y - lowEnd.y) > std::abs(highEnd.x - lowEnd.x);

        std::vector<double> positions, extents;
        std::vector<int> priorities;
        positions.reserve(ticks.size());
        for (const ChartTick& tick : ticks) {
            const Point2Dd p{
                lowEnd.x + (highEnd.x - lowEnd.x) * tick.normalized,
                lowEnd.y + (highEnd.y - lowEnd.y) * tick.normalized};
            const Size2Di size = ctx->GetTextLineDimensions(tick.label);
            positions.push_back(alongY ? p.y : p.x);
            extents.push_back(alongY ? size.height : static_cast<double>(size.width));
            priorities.push_back(tick.priority);
        }
        labelPlan.tickVisible[i] = DeclutterAxisTicks(
            positions, extents, 4.0, TickDeclutterPolicy::PriorityGreedy, &priorities);
    }
}

void UltraCanvasChartEngineElement::AxisScreenLine(const ChartAxis& axis,
                                                   Point2Dd& lowEnd,
                                                   Point2Dd& highEnd) const {
    if (axis.inPlot) {
        lowEnd = engineProjection->ToScreen(ChartNormalizedPoint(axis.plotPosition, 0.0));
        highEnd = engineProjection->ToScreen(ChartNormalizedPoint(axis.plotPosition, 1.0));
        return;
    }
    const Rect2Dd& plot = frame.plotArea;
    switch (axis.side) {
        case ChartAxisSide::Left:
            lowEnd = Point2Dd(plot.x, plot.Bottom());
            highEnd = Point2Dd(plot.x, plot.y);
            break;
        case ChartAxisSide::Right:
            lowEnd = Point2Dd(plot.Right(), plot.Bottom());
            highEnd = Point2Dd(plot.Right(), plot.y);
            break;
        case ChartAxisSide::Top:
            lowEnd = Point2Dd(plot.x, plot.y);
            highEnd = Point2Dd(plot.Right(), plot.y);
            break;
        case ChartAxisSide::Bottom:
        default:
            lowEnd = Point2Dd(plot.x, plot.Bottom());
            highEnd = Point2Dd(plot.Right(), plot.Bottom());
            break;
    }
}

// =============================================================================
// RENDER DRIVER
// =============================================================================

void UltraCanvasChartEngineElement::Render(IRenderContext* ctx, const Rect2Df&) {
    if (!ctx) return;
    EnsureEngineLayout(ctx);
    EnsureLabelPlan(ctx);

    // Phase 1 - everything under the chart.
    ctx->PushState();
    RenderPhaseUnder(ctx);
    ctx->PopState();

    // Phase 2 - the chart itself, clipped to the plot area so it cannot paint
    // over the axes, tick labels or legend.
    ctx->PushState();
    ctx->ClipRect(frame.plotArea);
    RenderChartContent(ctx, frame);
    ctx->PopState();

    // Phase 3 - everything over the chart.
    ctx->PushState();
    RenderPhaseOver(ctx);
    ctx->PopState();
}

void UltraCanvasChartEngineElement::RenderChart(IRenderContext* ctx) {
    // Legacy entry point from the base class; the engine path clips and routes.
    ctx->PushState();
    ctx->ClipRect(frame.plotArea);
    RenderChartContent(ctx, frame);
    ctx->PopState();
}

void UltraCanvasChartEngineElement::RenderPhaseUnder(IRenderContext* ctx) {
    RenderEngineBackground(ctx);   // slot 100
    RenderEngineGrid(ctx);         // slot 300
    RenderEngineLimiters(ctx);     // slot 400
    RenderEngineAxes(ctx);         // slot 500 - edge axes only
    RenderEngineTitle(ctx);
}

void UltraCanvasChartEngineElement::RenderPhaseOver(IRenderContext* ctx) {
    // In-plot axes sit above the content: a dense chart would otherwise bury
    // its own axis rules and value labels under the data marks.
    RenderEngineInPlotAxes(ctx);
    RenderPlannedLabels(ctx);      // slot 800
    RenderEngineLegend(ctx);       // slot 800
    ctx->PushState();
    RenderInteractionOverlay(ctx, frame);   // slot 900
    ctx->PopState();
}

void UltraCanvasChartEngineElement::RenderEngineBackground(IRenderContext* ctx) {
    if (!showBackground) return;
    ctx->DrawFilledRectangle(GetLocalBounds(), backgroundColor);
    ctx->SetFillPaint(plotAreaColor);
    ctx->FillRectangle(frame.plotArea);
}

void UltraCanvasChartEngineElement::RenderEngineGrid(IRenderContext* ctx) {
    if (gridAxisIndex >= engineAxes.Count()) return;
    const ChartAxis& axis = engineAxes.At(gridAxisIndex);

    ctx->SetStrokePaint(gridColor);
    ctx->SetStrokeWidth(1.0f);
    // Gridlines run perpendicular to the grid axis, one per tick - derived
    // from the same ticks the labels use, so they always line up.
    for (const ChartTick& tick : axis.GenerateTicks(targetTickCount)) {
        const Point2Dd from = engineProjection->ToScreen(
            ChartNormalizedPoint(0.0, tick.normalized));
        const Point2Dd to = engineProjection->ToScreen(
            ChartNormalizedPoint(1.0, tick.normalized));
        ctx->DrawLine(from, to);
    }
}

void UltraCanvasChartEngineElement::RenderEngineLimiters(IRenderContext* ctx) {
    for (const ChartLimiter& limiter : limiters) {
        if (limiter.axisIndex >= engineAxes.Count()) continue;
        const ChartAxis& axis = engineAxes.At(limiter.axisIndex);
        const double v = axis.Normalize(limiter.value);
        if (v < -0.001 || v > 1.001) continue;

        ctx->SetStrokePaint(limiter.color);
        ctx->SetStrokeWidth(limiter.width);
        if (limiter.dashed) ctx->SetLineDash(UCDashPattern({6.0, 4.0}));
        ctx->DrawLine(engineProjection->ToScreen(ChartNormalizedPoint(0.0, v)),
                      engineProjection->ToScreen(ChartNormalizedPoint(1.0, v)));
        if (limiter.dashed) ctx->SetLineDash(UCDashPattern());
    }
}

void UltraCanvasChartEngineElement::RenderEngineAxes(IRenderContext* ctx) {
    ctx->SetFontSize(axisFontSize);
    for (size_t i = 0; i < engineAxes.Count(); ++i) {
        const ChartAxis& axis = engineAxes.At(i);
        if (!axis.visible || axis.inPlot) continue;
        RenderAxisFurniture(ctx, i);
    }
}

void UltraCanvasChartEngineElement::RenderEngineInPlotAxes(IRenderContext* ctx) {
    ctx->SetFontSize(axisFontSize);
    for (size_t i = 0; i < engineAxes.Count(); ++i) {
        const ChartAxis& axis = engineAxes.At(i);
        if (!axis.visible || !axis.inPlot) continue;
        RenderAxisFurniture(ctx, i);
    }
}

void UltraCanvasChartEngineElement::RenderAxisFurniture(IRenderContext* ctx, size_t i) {
    const ChartAxis& axis = engineAxes.At(i);

    Point2Dd lowEnd, highEnd;
    AxisScreenLine(axis, lowEnd, highEnd);

    ctx->SetStrokePaint(axisLineColor);
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawLine(lowEnd, highEnd);

    const bool alongY = std::abs(highEnd.y - lowEnd.y) > std::abs(highEnd.x - lowEnd.x);
    // Ticks point away from the plot for edge axes, left for in-plot ones.
    double tickDx = 0.0, tickDy = 0.0;
    if (alongY) {
        tickDx = (axis.side == ChartAxisSide::Right && !axis.inPlot) ? tickLength
                                                                     : -tickLength;
    } else {
        tickDy = (axis.side == ChartAxisSide::Top && !axis.inPlot) ? -tickLength
                                                                   : tickLength;
    }

    const std::vector<ChartTick> ticks = axis.GenerateTicks(targetTickCount);
    const std::vector<bool>* visible =
        (i < labelPlan.tickVisible.size()) ? &labelPlan.tickVisible[i] : nullptr;

    for (size_t t = 0; t < ticks.size(); ++t) {
        const ChartTick& tick = ticks[t];
        const Point2Dd p{
            lowEnd.x + (highEnd.x - lowEnd.x) * tick.normalized,
            lowEnd.y + (highEnd.y - lowEnd.y) * tick.normalized};

        ctx->SetStrokePaint(axisTickColor);
        ctx->DrawLine(p, Point2Dd(p.x + tickDx, p.y + tickDy));

        if (visible && t < visible->size() && !(*visible)[t]) continue;
        if (axis.inPlot && !axis.showTickLabels) continue;

        ctx->SetTextPaint(axisLabelColor);
        const Size2Di size = ctx->GetTextLineDimensions(tick.label);
        Point2Dd labelPos;
        if (alongY) {
            const double x = (tickDx < 0.0)
                ? p.x + tickDx - tickLabelGap - size.width
                : p.x + tickDx + tickLabelGap;
            labelPos = Point2Dd(x, p.y - size.height * 0.5);
        } else {
            const double y = (tickDy < 0.0)
                ? p.y + tickDy - tickLabelGap - size.height
                : p.y + tickDy + tickLabelGap;
            labelPos = Point2Dd(p.x - size.width * 0.5, y);
        }
        ctx->DrawText(tick.label, labelPos);
    }

    // Endpoint min/max labels for in-plot axes (parallel coordinates) - only
    // when the integrated tick labels are off, so the extremes are not printed
    // a second time above and below the same axis.
    const bool endpointsDrawn = axis.inPlot && axis.showEndpointLabels &&
                                !axis.showTickLabels;
    if (endpointsDrawn) {
        ctx->SetTextPaint(axisLabelColor);
        const std::string lowText = axis.FormatValue(axis.inverted ? axis.Max() : axis.Min());
        const std::string highText = axis.FormatValue(axis.inverted ? axis.Min() : axis.Max());
        const Size2Di lowSize = ctx->GetTextLineDimensions(lowText);
        const Size2Di highSize = ctx->GetTextLineDimensions(highText);
        ctx->DrawText(lowText, Point2Dd(lowEnd.x - lowSize.width * 0.5,
                                        lowEnd.y + 3.0));
        ctx->DrawText(highText, Point2Dd(highEnd.x - highSize.width * 0.5,
                                         highEnd.y - highSize.height - 3.0));
    }

    // Axis title below (in-plot / bottom) or rotated placement is left to
    // the label plan for charts that promote titles to solved labels; the
    // engine default puts the title past the low end.
    if (!axis.title.empty()) {
        ctx->SetTextPaint(axisLabelColor);
        const Size2Di size = ctx->GetTextLineDimensions(axis.title);
        const double extra = endpointsDrawn ? size.height + 6.0 : 6.0;
        if (alongY) {
            ctx->DrawText(axis.title,
                          Point2Dd(lowEnd.x - size.width * 0.5,
                                   lowEnd.y + extra + tickLength));
        } else {
            ctx->DrawText(axis.title,
                          Point2Dd((lowEnd.x + highEnd.x - size.width) * 0.5,
                                   lowEnd.y + tickLength + tickLabelGap +
                                       size.height + extra));
        }
    }
}

void UltraCanvasChartEngineElement::RenderEngineTitle(IRenderContext* ctx) {
    if (chartTitle.empty()) return;
    ctx->SetTextPaint(titleTextColor);
    ctx->SetFontSize(titleFontSize);
    // Measured centring - replaces the base class's length*5 estimate.
    const Size2Di size = ctx->GetTextLineDimensions(chartTitle);
    ctx->DrawText(chartTitle,
                  Point2Dd((GetWidth() - size.width) * 0.5, 4.0));
}

void UltraCanvasChartEngineElement::RenderPlannedLabels(IRenderContext* ctx) {
    ctx->SetFontSize(axisFontSize);
    for (const PlacedChartLabel& label : labelPlan.labels) {
        if (label.suppressed) continue;
        // Legend entries are chrome drawn by the legend itself.
        if (label.klass == ChartLabelClass::LegendEntry) continue;

        if (label.hasLeader) {
            ctx->SetStrokePaint(Color(150, 150, 150, 180));
            ctx->SetStrokeWidth(1.0f);
            const Point2Dd target(
                std::clamp(label.leaderFrom.x, label.bounds.Left(), label.bounds.Right()),
                std::clamp(label.leaderFrom.y, label.bounds.Top(), label.bounds.Bottom()));
            ctx->DrawLine(label.leaderFrom, target);
        }

        ctx->SetTextPaint(axisLabelColor);
        if (label.rotationDegrees != 0.0) {
            ctx->PushState();
            ctx->Translate(label.bounds.x + label.bounds.width * 0.5,
                           label.bounds.y + label.bounds.height * 0.5);
            ctx->Rotate(label.rotationDegrees * M_PI / 180.0);
            const Size2Di size = ctx->GetTextLineDimensions(label.text);
            ctx->DrawText(label.text, Point2Dd(-size.width * 0.5, -size.height * 0.5));
            ctx->PopState();
        } else {
            ctx->DrawText(label.text, label.bounds.TopLeft());
        }
    }
}

void UltraCanvasChartEngineElement::RenderEngineLegend(IRenderContext* ctx) {
    if (!showLegend || legendEntries.empty() || legendRect.width <= 0.0) return;

    ctx->SetFillPaint(legendBackground);
    ctx->FillRoundedRectangle(legendRect, 4.0);
    ctx->SetStrokePaint(Color(190, 190, 190, 255));
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawRoundedRectangle(legendRect, 4.0);

    ctx->SetFontSize(axisFontSize);
    const double swatch = 14.0, pad = 8.0;
    const double rowHeight = ctx->GetTextLineHeight("Ag") + 6.0;
    double y = legendRect.y + pad;
    for (const ChartLegendEntry& entry : legendEntries) {
        ctx->SetFillPaint(entry.color);
        ctx->FillRoundedRectangle(
            Rect2Dd(legendRect.x + pad, y + 1.0, swatch, rowHeight - 8.0), 2.0);
        ctx->SetTextPaint(legendTextColor);
        ctx->DrawText(entry.label,
                      Point2Dd(legendRect.x + pad + swatch + 6.0, y));
        y += rowHeight;
    }
}

} // namespace UltraCanvas

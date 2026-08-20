// Plugins/Charts/Engine/UltraCanvasChartEngineElement.cpp
// The chart engine's three-phase render driver
// Version: 1.2.0
// Last Modified: 2026-08-20
// V1.2.0: Themes and palettes (Engine/UltraCanvasChartTheme.h) - SetTheme /
//   SetPalette / the "theme" named property fill the engine's furniture
//   colours from a ChartTheme; repaint-only, with an OnThemeChanged() hook.
// V1.1.0: Two fixes the first animated / transposed client exposed.
//   - The animation driver now advances on a ~60fps application timer.
//     RequestRedraw() from inside the paint cannot produce the next frame (the
//     event loop blocks until a native event or a timer wakes it), so an
//     animating chart froze at whatever progress its last paint happened to
//     see - bars stuck part-grown until something else caused a repaint.
//   - AxisScreenLine derives the direction an edge axis runs from the
//     projection instead of assuming bottom-to-top / left-to-right. Under the
//     horizontal projection the domain runs downward, so the category ticks
//     were laid out in the opposite order to the bars they label.
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include "UltraCanvasApplication.h"
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
    engineProperties.Define("theme", std::string("Light"));
    engineTheme = ChartThemes::Light();
    frame.theme = &engineTheme;
}

UltraCanvasChartEngineElement::~UltraCanvasChartEngineElement() {
    StopEngineAnimationTimer();      // the timer callback captures `this`
}

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

void UltraCanvasChartEngineElement::SetTheme(const ChartTheme& theme) {
    engineTheme = theme;
    frame.theme = &engineTheme;
    // The engine layers read these working copies; the base class paints the
    // backgrounds and the grid from its own fields.
    backgroundColor = theme.backgroundColor;
    plotAreaColor = theme.plotAreaColor;
    gridColor = theme.gridColor;
    axisLineColor = theme.axisLineColor;
    axisTickColor = theme.axisTickColor;
    axisLabelColor = theme.axisLabelColor;
    titleTextColor = theme.titleTextColor;
    legendTextColor = theme.legendTextColor;
    legendBackground = theme.legendBackground;
    engineProperties.Set("theme", theme.name);
    OnThemeChanged();
    // A theme is colours only, so nothing can move: no layout, no label
    // re-solve (proposal §5.7) - just repaint.
    RequestRedraw();
}

bool UltraCanvasChartEngineElement::SetTheme(const std::string& themeName) {
    const ChartTheme* theme = ChartThemes::Find(themeName);
    if (!theme) return false;
    SetTheme(*theme);
    return true;
}

void UltraCanvasChartEngineElement::SetPalette(const ChartPalette& palette) {
    engineTheme.palette = palette;
    OnThemeChanged();
    RequestRedraw();
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
    if (animateOnDataChange && HasDirty(flags, ChartDirty::Data)) {
        StartEngineAnimation(animateOnDataDuration);
    }
    RequestRedraw();
}

// =============================================================================
// ANIMATION DRIVER
// =============================================================================

void UltraCanvasChartEngineElement::StartEngineAnimation(float durationSeconds) {
    animationDuration = std::max(0.05f, durationSeconds);
    StartAnimation();                  // base class stamps the start time
    engineAnimating = true;

    // Drive the frames with a ~60fps periodic timer. RequestRedraw() alone
    // cannot advance the animation: the event loop blocks until a native event
    // or a timer wakes it, so without this the chart freezes at whatever
    // progress its last paint happened to see.
    if (engineAnimationTimer == InvalidTimerId) {
        if (auto* app = UltraCanvasApplication::GetInstance()) {
            engineAnimationTimer = app->StartTimer(16, true, [this](TimerId) {
                if (!engineAnimating) {
                    StopEngineAnimationTimer();
                    return;
                }
                RequestRedraw();       // Render() advances and lands the progress
            });
        }
    }
    RequestRedraw();
}

void UltraCanvasChartEngineElement::StopEngineAnimationTimer() {
    if (engineAnimationTimer == InvalidTimerId) return;
    if (auto* app = UltraCanvasApplication::GetInstance()) {
        app->StopTimer(engineAnimationTimer);
    }
    engineAnimationTimer = InvalidTimerId;
}

void UltraCanvasChartEngineElement::SetAnimateOnDataChange(bool enable,
                                                           float durationSeconds) {
    animateOnDataChange = enable;
    animateOnDataDuration = durationSeconds;
}

// =============================================================================
// HIT REGIONS AND TOOLTIPS
// =============================================================================

void UltraCanvasChartEngineElement::ClearHitRegions() {
    hitRegions.clear();
}

void UltraCanvasChartEngineElement::AddHitRegion(const Rect2Dd& bounds, int64_t regionId,
                                                 const std::string& tooltip) {
    ChartHitRegion region;
    region.bounds = bounds;
    region.id = regionId;
    region.tooltip = tooltip;
    hitRegions.push_back(std::move(region));
}

void UltraCanvasChartEngineElement::AddHitRegion(const std::vector<Point2Dd>& polygon,
                                                 int64_t regionId,
                                                 const std::string& tooltip) {
    if (polygon.size() < 3) return;
    ChartHitRegion region;
    region.polygon = polygon;
    double minX = polygon[0].x, maxX = polygon[0].x;
    double minY = polygon[0].y, maxY = polygon[0].y;
    for (const Point2Dd& p : polygon) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }
    region.bounds = Rect2Dd(minX, minY, maxX - minX, maxY - minY);
    region.id = regionId;
    region.tooltip = tooltip;
    hitRegions.push_back(std::move(region));
}

const UltraCanvasChartEngineElement::ChartHitRegion*
UltraCanvasChartEngineElement::HitTestRegions(const Point2Dd& point) const {
    // Last added wins: regions arrive in draw order, so the topmost mark of an
    // overlap is the one the pointer means.
    for (auto it = hitRegions.rbegin(); it != hitRegions.rend(); ++it) {
        const ChartHitRegion& region = *it;
        if (point.x < region.bounds.x || point.x > region.bounds.x + region.bounds.width ||
            point.y < region.bounds.y || point.y > region.bounds.y + region.bounds.height) {
            continue;
        }
        if (region.polygon.empty()) return &region;

        // Even-odd ray cast for polygonal regions.
        bool inside = false;
        const size_t n = region.polygon.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const Point2Dd& a = region.polygon[i];
            const Point2Dd& b = region.polygon[j];
            if (((a.y > point.y) != (b.y > point.y)) &&
                (point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x)) {
                inside = !inside;
            }
        }
        if (inside) return &region;
    }
    return nullptr;
}

bool UltraCanvasChartEngineElement::OnEvent(const UCEvent& event) {
    if (event.type == UCEventType::MouseMove) {
        const ChartHitRegion* hit =
            HitTestRegions(Point2Dd(event.pointer.x, event.pointer.y));
        const int64_t id = hit ? hit->id : -1;
        if (id != hoveredRegionId) {
            hoveredRegionId = id;
            OnHitRegionHoverChanged(id);
            MarkEngineDirty(ChartDirty::Hover);   // repaint only - the frozen
        }                                         // frame and label plan stand
        if (enableTooltips) {
            if (hit && !hit->tooltip.empty()) {
                const Point2Di mousePos(static_cast<int>(event.pointer.x),
                                        static_cast<int>(event.pointer.y));
                UltraCanvasTooltipManager::UpdateAndShowTooltip(
                    this->window, hit->tooltip, MapFromLocal(mousePos, nullptr));
                isTooltipActive = true;
            } else if (isTooltipActive) {
                HideTooltip();
            }
        }
    } else if (event.type == UCEventType::MouseLeave) {
        if (hoveredRegionId != -1) {
            hoveredRegionId = -1;
            OnHitRegionHoverChanged(-1);
            MarkEngineDirty(ChartDirty::Hover);
        }
        if (isTooltipActive) HideTooltip();
    }
    return UltraCanvasChartElementBase::OnEvent(event);
}

// =============================================================================
// PROPERTIES
// =============================================================================

bool UltraCanvasChartEngineElement::SetProperty(const std::string& key,
                                                const UCPropertyValue& value) {
    if (key == "theme") {
        // Validated against the registry before it lands in the bag;
        // SetTheme stores the canonical capitalization itself.
        std::string themeName;
        if (!UCPropertyAsString(value, themeName)) return false;
        if (!SetTheme(themeName)) return false;
        OnEnginePropertyChanged(key, engineProperties.Get(key));
        return true;
    }
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
    const bool verticalEdge = (axis.side == ChartAxisSide::Left ||
                               axis.side == ChartAxisSide::Right);

    // Which way the axis runs on screen is a property of the projection, not of
    // the side it sits on: the vertical projection maps the value upward, the
    // horizontal one maps the domain downward. Probe the projection instead of
    // assuming bottom-to-top, or a transposed chart lays its ticks out in the
    // opposite order to its content - the first category labelled against the
    // last row's marks.
    const Point2Dd origin = engineProjection->ToScreen(ChartNormalizedPoint(0.0, 0.0));
    const Point2Dd alongU = engineProjection->ToScreen(ChartNormalizedPoint(1.0, 0.0));
    const Point2Dd alongV = engineProjection->ToScreen(ChartNormalizedPoint(0.0, 1.0));

    if (verticalEdge) {
        const double x = (axis.side == ChartAxisSide::Right) ? plot.Right() : plot.x;
        // Whichever normalised coordinate actually drives screen-y along this
        // edge decides where normalised 0 sits.
        const double du = alongU.y - origin.y;
        const double dv = alongV.y - origin.y;
        const bool downward = ((std::abs(dv) >= std::abs(du)) ? dv : du) >= 0.0;
        lowEnd  = Point2Dd(x, downward ? plot.y : plot.Bottom());
        highEnd = Point2Dd(x, downward ? plot.Bottom() : plot.y);
    } else {
        const double y = (axis.side == ChartAxisSide::Top) ? plot.y : plot.Bottom();
        const double du = alongU.x - origin.x;
        const double dv = alongV.x - origin.x;
        const bool rightward = ((std::abs(du) >= std::abs(dv)) ? du : dv) >= 0.0;
        lowEnd  = Point2Dd(rightward ? plot.x : plot.Right(), y);
        highEnd = Point2Dd(rightward ? plot.Right() : plot.x, y);
    }
}

// =============================================================================
// RENDER DRIVER
// =============================================================================

void UltraCanvasChartEngineElement::Render(IRenderContext* ctx, const Rect2Df&) {
    if (!ctx) return;
    EnsureEngineLayout(ctx);
    EnsureLabelPlan(ctx);

    // The animation driver advances the frame's progress; content scales its
    // geometry by it. Ease-out cubic: fast start, settled landing.
    if (engineAnimating) {
        const double t = GetAnimationProgress();
        frame.animationProgress = 1.0 - std::pow(1.0 - t, 3.0);
        if (t >= 1.0) {
            engineAnimating = false;
            frame.animationProgress = 1.0;
            StopEngineAnimationTimer();
        }
    } else {
        frame.animationProgress = 1.0;
    }

    // Phase 1 - everything under the chart.
    ctx->PushState();
    RenderPhaseUnder(ctx);
    ctx->PopState();

    // Phase 2 - the chart itself, clipped to the plot area so it cannot paint
    // over the axes, tick labels or legend. Hit regions are rebuilt by the
    // content on every pass, so stale geometry can never be hovered.
    ctx->PushState();
    ctx->ClipRect(frame.plotArea);
    ClearHitRegions();
    RenderChartContent(ctx, frame);
    ctx->PopState();

    // Phase 3 - everything over the chart.
    ctx->PushState();
    RenderPhaseOver(ctx);
    ctx->PopState();

    if (engineAnimating) RequestRedraw();
}

void UltraCanvasChartEngineElement::RenderChart(IRenderContext* ctx) {
    // Legacy entry point from the base class; the engine path clips and routes.
    ctx->PushState();
    ctx->ClipRect(frame.plotArea);
    ClearHitRegions();
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
        const Rect2Dd box(legendRect.x + pad, y + 1.0, swatch, rowHeight - 8.0);
        RenderLegendSwatch(ctx, box, entry);
        ctx->SetTextPaint(legendTextColor);
        ctx->DrawText(entry.label,
                      Point2Dd(legendRect.x + pad + swatch + 6.0, y));
        y += rowHeight;
    }
}

void UltraCanvasChartEngineElement::RenderLegendSwatch(IRenderContext* ctx,
                                                       const Rect2Dd& box,
                                                       const ChartLegendEntry& entry) {
    switch (entry.swatch) {
    case ChartLegendSwatch::Gradient: {
        Color light = entry.color;
        light.r = static_cast<uint8_t>(std::min(255, light.r + 70));
        light.g = static_cast<uint8_t>(std::min(255, light.g + 70));
        light.b = static_cast<uint8_t>(std::min(255, light.b + 70));
        auto gradient = ctx->CreateLinearGradientPattern(
            box.x, box.y, box.x, box.y + box.height,
            {GradientStop(0.0, light), GradientStop(1.0, entry.color)});
        if (gradient) {
            ctx->SetFillPaint(gradient);
            ctx->FillRoundedRectangle(box, 2.0);
        } else {
            ctx->SetFillPaint(entry.color);
            ctx->FillRoundedRectangle(box, 2.0);
        }
        break;
    }
    case ChartLegendSwatch::Outline: {
        Color ghost = entry.color;
        ghost.a = 36;
        ctx->SetFillPaint(ghost);
        ctx->FillRoundedRectangle(box, 2.0);
        ctx->SetStrokePaint(entry.color);
        ctx->SetStrokeWidth(1.5f);
        ctx->DrawRoundedRectangle(box, 2.0);
        break;
    }
    case ChartLegendSwatch::Hatched: {
        Color background = entry.color;
        background.r = static_cast<uint8_t>(std::min(255, background.r + 90));
        background.g = static_cast<uint8_t>(std::min(255, background.g + 90));
        background.b = static_cast<uint8_t>(std::min(255, background.b + 90));
        ctx->SetFillPaint(background);
        ctx->FillRoundedRectangle(box, 2.0);
        ctx->PushState();
        ctx->ClipRect(box);
        ctx->SetStrokePaint(entry.color);
        ctx->SetStrokeWidth(1.0f);
        for (double offset = 0.0; offset < box.width + box.height; offset += 4.0) {
            ctx->DrawLine(Point2Dd(box.x + offset, box.y),
                          Point2Dd(box.x + offset - box.height, box.y + box.height));
        }
        ctx->PopState();
        break;
    }
    case ChartLegendSwatch::Image: {
        if (!entry.imagePath.empty()) {
            ctx->PushState();
            ctx->ClipRect(box);
            ctx->DrawImage(entry.imagePath, box, ImageFitMode::Cover);
            ctx->PopState();
            ctx->SetStrokePaint(Color(150, 150, 150, 255));
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawRoundedRectangle(box, 2.0);
        } else {
            ctx->SetFillPaint(entry.color);
            ctx->FillRoundedRectangle(box, 2.0);
        }
        break;
    }
    case ChartLegendSwatch::Solid:
    default:
        ctx->SetFillPaint(entry.color);
        ctx->FillRoundedRectangle(box, 2.0);
        break;
    }
}

} // namespace UltraCanvas

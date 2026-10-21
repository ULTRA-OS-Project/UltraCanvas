// Plugins/Charts/Engine/UltraCanvasChartEngineElement.cpp
// The chart engine's three-phase render driver
// Version: 1.3.3
// Last Modified: 2026-10-20
// V1.3.3: Tick labels drawn inside the plot (an in-plot axis, the radial
//   value rule under Polar) are obstacles in the label plan, so a solved
//   label no longer overprints them - the polar chart's "0 ppm" under its
//   "Under-Absorp" band caption. The tick declutter runs before the solve and
//   one helper (TickLabelBox) places a tick label for both passes.
//   A polar limiter's caption sits a little way round its ring, not at the
//   ring's end beside the radial rule.
//   OnThemeChanged() is renamed OnChartThemeChanged(): it hid the element's
//   OnThemeChanged(const UltraCanvasTheme&).
// V1.3.2: Fixes the comparison page showed.
//   - A value limiter under Polar is drawn as a ring; it collapsed to a point.
//   - Rim labels are obstacles in the label plan: a radial bar's peak label
//     overprinted the series name on the rim.
//   - An edge axis title sits outside the axis's tick labels, centred on its
//     band - rotated along a left/right axis. It was drawn past the low end,
//     so a scatter plot's y title landed on the x axis's first label.
//   - The labels at the ends of a bottom/top axis are centred on their ticks
//     and spilled past the element's edge (a financial chart's last date was
//     cut in half). The measure pass now reserves their overhang on the
//     left/right, and the labels are clamped inside the element.
//   - Under Polar the plot-area fill follows the circle (or the sector, or
//     the polygon web) instead of painting a rectangle behind a round chart.
// V1.3.1: Edge axes sharing a side stack outward when their bands overlap
//   (the Mekko port's column names under its cumulative-percent ticks), so
//   they no longer overprint; disjoint bands (price above volume) still share
//   the edge line.
// V1.3.0: Polar furniture and two-direction grids for the engine ports of the
//   line, scatter, radar, radial bar and pie charts.
//   - Under Polar an edge axis follows its role: a domain axis labels the rim
//     (ticks and labels around the outer ring, the margin measured on all four
//     sides), a value axis becomes the radial rule at the start angle. Before,
//     both were straight rules along the rectangular plot bounds.
//   - The value grid draws rings under Polar (circles, or polygons through
//     the domain ticks - SetPolarGridShape); it drew degenerate chords.
//   - SetDomainGridAxis adds the perpendicular grid (vertical lines, spokes),
//     and a domain axis given to SetGridAxis now rules across the value range
//     instead of drawing lines at its tick positions in the wrong direction.
//   - SetPolarAngles / SetPolarInnerRadius reach the polar projection and
//     survive projection switches.
//   - An edge axis with a sub-band (ChartAxis::bandStart/bandEnd) draws its
//     rule over the band only, so stacked panes read as separate axes.
//   - DrawChartMarker: the one marker painter of the scatter / line family.
//   - Span annotations (AddSpanAnnotation): dimension lines with arrowheads
//     and a boxed caption that the label plan steers around - the lead time /
//     cycle time / WIP spans of a cumulative flow diagram.
//   - A limiter on a domain axis runs across the value range (a pyramid's
//     average-age line), and ChartLimiter::extentStart/extentEnd trim it to
//     part of the plot (one side of a diverging chart).
//   - A ValueBand highlight follows its axis's role and the projection: a
//     domain-axis band is a vertical strip, and under Polar the outline is
//     sampled, so value bands are rings (the polar chart's radial tolerance
//     zones) and domain bands wedges (its angular sectors). It drew a chord.
// V1.2.0: Themes and palettes (Engine/UltraCanvasChartTheme.h) - SetTheme /
//   SetPalette / the "theme" named property fill the engine's furniture
//   colours from a ChartTheme; repaint-only, with an OnChartThemeChanged() hook.
//   Also: the label solver's bounds now include the margins the content
//   reserved in MeasureContent (SolveLabelBounds), so a bar that reaches the
//   axis maximum keeps its value label just above the plot edge instead of
//   having it pushed down onto the bar.
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

    // The legend is the shared component, hidden until a chart shows it.
    // The engine box look predates the component's plainer default.
    engineLegend.SetVisible(false);
    engineLegend.SetPosition(ChartLegendPosition::RightStart);
    ChartLegendStyle& legendStyle = engineLegend.GetStyle();
    legendStyle.fontSize = axisFontSize;
    legendStyle.textColor = legendTextColor;
    legendStyle.backgroundColor = legendBackground;
    legendStyle.drawBackground = true;
    legendStyle.drawBorder = true;
    legendStyle.borderColor = Color(190, 190, 190, 255);
    legendStyle.cornerRadius = 4.0f;
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
    ApplyPolarGeometry();
    // Data, not just Geometry: DescribeAxes must run again, because a chart
    // may place its edge axes differently per projection (a horizontal bar
    // chart's value axis sits on the bottom, not the left).
    MarkEngineDirty(ChartDirty::Data | ChartDirty::Geometry);
}

ChartProjectionKind UltraCanvasChartEngineElement::GetProjectionKind() const {
    return projectionKind;
}

void UltraCanvasChartEngineElement::ApplyPolarGeometry() {
    if (auto* polar = dynamic_cast<ChartPolarProjection*>(engineProjection.get())) {
        polar->SetAngles(polarStartAngle, polarSweep, polarClockwise);
        polar->SetInnerRadiusFraction(polarInnerFraction);
    }
}

void UltraCanvasChartEngineElement::SetPolarAngles(double startAngleDegrees,
                                                   double sweepDegrees, bool clockwise) {
    polarStartAngle = startAngleDegrees;
    polarSweep = std::clamp(sweepDegrees, 1.0, 360.0);
    polarClockwise = clockwise;
    ApplyPolarGeometry();
    MarkEngineDirty(ChartDirty::Geometry);      // labels sit on the moved rim
}

void UltraCanvasChartEngineElement::SetPolarInnerRadius(double fraction) {
    polarInnerFraction = std::clamp(fraction, 0.0, 0.95);
    ApplyPolarGeometry();
    MarkEngineDirty(ChartDirty::Geometry);
}

void UltraCanvasChartEngineElement::SetPolarGridShape(ChartPolarGridShape shape) {
    polarGridShape = shape;
    RequestRedraw();
}

bool UltraCanvasChartEngineElement::PolarFullCircle() const {
    return polarSweep >= 359.999;
}

bool UltraCanvasChartEngineElement::IsDomainAxis(const ChartAxis& axis) const {
    if (axis.role == ChartAxisRole::Domain) return true;
    if (axis.role == ChartAxisRole::Value) return false;
    const bool acrossBottomOrTop = (axis.side == ChartAxisSide::Bottom ||
                                    axis.side == ChartAxisSide::Top);
    // The horizontal projection transposes the plot: the domain runs down the
    // left/right edges and the value along the bottom/top.
    return (projectionKind == ChartProjectionKind::Horizontal) ? !acrossBottomOrTop
                                                               : acrossBottomOrTop;
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
    // The shared legend paints from its own style; keep it on the theme.
    ChartLegendStyle& legendStyle = engineLegend.GetStyle();
    legendStyle.textColor = theme.legendTextColor;
    legendStyle.titleColor = theme.legendTextColor;
    legendStyle.disabledTextColor = theme.axisLabelColor;
    legendStyle.backgroundColor = theme.legendBackground;
    legendStyle.borderColor = theme.gridColor;
    legendStyle.swatchBorderColor = theme.axisLineColor;
    engineProperties.Set("theme", theme.name);
    OnChartThemeChanged();
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
    OnChartThemeChanged();
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

void UltraCanvasChartEngineElement::SetDomainGridAxis(size_t axisIndex) {
    domainGridAxisIndex = axisIndex;
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

size_t UltraCanvasChartEngineElement::AddHighlight(const ChartHighlight& highlight) {
    highlights.push_back(highlight);
    MarkEngineDirty(ChartDirty::Style);       // labels join the label plan
    return highlights.size() - 1;
}

size_t UltraCanvasChartEngineElement::AddSpanAnnotation(const ChartSpanAnnotation& span) {
    spanAnnotations.push_back(span);
    MarkEngineDirty(ChartDirty::Style);       // the caption joins the label plan
    return spanAnnotations.size() - 1;
}

void UltraCanvasChartEngineElement::ClearSpanAnnotations() {
    spanAnnotations.clear();
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasChartEngineElement::ClearHighlights() {
    highlights.clear();
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasChartEngineElement::SetShowLegend(bool show) {
    engineLegend.SetVisible(show);
    MarkEngineDirty(ChartDirty::Geometry);    // the legend reserves margin
}

void UltraCanvasChartEngineElement::SetLegendEntries(const std::vector<ChartLegendEntry>& entries) {
    engineLegend.SetEntries(entries);
    MarkEngineDirty(ChartDirty::Geometry);
}

void UltraCanvasChartEngineElement::SetLegendPosition(ChartLegendPosition position) {
    engineLegend.SetPosition(position);
    MarkEngineDirty(ChartDirty::Geometry);    // the reserved edge moves
}

void UltraCanvasChartEngineElement::SetLegendOrientation(LegendOrientation orientation) {
    engineLegend.SetOrientation(orientation);
    MarkEngineDirty(ChartDirty::Geometry);
}

void UltraCanvasChartEngineElement::SetLegendTitle(const std::string& title) {
    engineLegend.SetTitle(title);
    MarkEngineDirty(ChartDirty::Geometry);
}

void UltraCanvasChartEngineElement::SetLegendMode(ChartLegendMode mode) {
    engineLegend.SetMode(mode);
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
    // Legend interaction first: the legend is chrome above the plot, so a
    // pointer over it never reaches the content's hit regions.
    if (engineLegend.IsVisible() &&
        (event.type == UCEventType::MouseMove ||
         event.type == UCEventType::MouseDown)) {
        const size_t entry =
            engineLegend.HitTest(Point2Dd(event.pointer.x, event.pointer.y));
        if (event.type == UCEventType::MouseMove) {
            if (entry != engineLegend.GetHighlightedEntry()) {
                engineLegend.SetHighlightedEntry(entry);
                MarkEngineDirty(ChartDirty::Hover);   // repaint only
            }
        } else if (entry != SIZE_MAX) {
            engineLegend.ToggleEntryEnabled(entry);
            OnLegendEntryToggled(entry,
                                 engineLegend.GetEntry(entry).enabled);
            RequestRedraw();
            return true;                              // click consumed
        }
        if (entry != SIZE_MAX && event.type == UCEventType::MouseMove) {
            if (isTooltipActive) HideTooltip();
            return true;                              // hover consumed
        }
    } else if (event.type == UCEventType::MouseLeave &&
               engineLegend.GetHighlightedEntry() != SIZE_MAX) {
        engineLegend.SetHighlightedEntry(SIZE_MAX);
        MarkEngineDirty(ChartDirty::Hover);
    }

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
    const bool polar = (projectionKind == ChartProjectionKind::Polar);
    // Edge axes sharing a side stack outward in the order they were added
    // when their bands overlap (a Mekko chart's column names under its
    // cumulative-percent ticks); axes in disjoint bands (a price pane above a
    // volume pane) share the edge line.
    axisEdgeOffsets.assign(engineAxes.Count(), 0.0);
    struct EdgeSlot { ChartAxisSide side; double lo, hi, outer; };
    std::vector<EdgeSlot> edgeSlots;
    for (const ChartAxis& axis : engineAxes) {
        if (!axis.visible || axis.inPlot) continue;
        if (polar) {
            // Polar furniture: a value axis is the radial rule inside the
            // circle and reserves nothing; a domain axis rings the circle with
            // labels pointing outward, so it needs room on every side.
            if (!IsDomainAxis(axis)) continue;
            double widest = 0.0, tallest = 0.0;
            for (const ChartTick& tick : axis.GenerateTicks(targetTickCount)) {
                const Size2Di size = ctx->GetTextLineDimensions(tick.label);
                widest = std::max(widest, static_cast<double>(size.width));
                tallest = std::max(tallest, static_cast<double>(size.height));
            }
            const double ring = tickLength + tickLabelGap + 4.0;
            request.Reserve(ChartAxisEdge::Left, ring + widest);
            request.Reserve(ChartAxisEdge::Right, ring + widest);
            request.Reserve(ChartAxisEdge::Top, ring + tallest);
            request.Reserve(ChartAxisEdge::Bottom, ring + tallest);
            continue;
        }
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
        // A bottom/top axis centres its labels on the ticks, so the ones at
        // the ends overhang the plot by up to half their width: reserve that
        // on the left/right, or the last date label is cut off at the
        // element's edge. Plot width is not known yet; the element's width
        // stands in, which over-reserves slightly rather than clipping.
        if (axis.side == ChartAxisSide::Bottom || axis.side == ChartAxisSide::Top) {
            const double span = std::max(1.0, static_cast<double>(GetWidth()));
            double overhangLow = 0.0, overhangHigh = 0.0;
            for (const ChartTick& tick : axis.GenerateTicks(targetTickCount)) {
                const double half = ctx->GetTextLineDimensions(tick.label).width * 0.5;
                overhangLow = std::max(overhangLow, half - tick.normalized * span);
                overhangHigh = std::max(overhangHigh, half - (1.0 - tick.normalized) * span);
            }
            // The domain may run either way on screen; reserve both ends.
            const double overhang = std::max(overhangLow, overhangHigh) + 2.0;
            request.Reserve(ChartAxisEdge::Left, overhang);
            request.Reserve(ChartAxisEdge::Right, overhang);
        }
        const double titleNeed = axis.title.empty()
                                     ? 0.0 : ctx->GetTextLineHeight(axis.title) + 4.0;
        const double need = tickLength + tickLabelGap + maxLabelExtent + 6.0 + titleNeed;
        const double lo = std::min(axis.bandStart, axis.bandEnd);
        const double hi = std::max(axis.bandStart, axis.bandEnd);
        double offset = 0.0;
        for (const EdgeSlot& slot : edgeSlots) {
            if (slot.side == axis.side && lo < slot.hi && slot.lo < hi) {
                offset = std::max(offset, slot.outer);
            }
        }
        edgeSlots.push_back({axis.side, lo, hi, offset + need});
        axisEdgeOffsets[static_cast<size_t>(&axis - &engineAxes.At(0))] = offset;
        switch (axis.side) {
            case ChartAxisSide::Left:   request.Reserve(ChartAxisEdge::Left, offset + need); break;
            case ChartAxisSide::Right:  request.Reserve(ChartAxisEdge::Right, offset + need); break;
            case ChartAxisSide::Top:    request.Reserve(ChartAxisEdge::Top, offset + need); break;
            case ChartAxisSide::Bottom: request.Reserve(ChartAxisEdge::Bottom, offset + need); break;
        }
    }

    // Legend: the shared component measures itself against the element minus
    // the exclusive title band. An outside placement's consumed edge becomes
    // a reservation; an inset placement floats over the plot and reserves
    // nothing (RemainingArea returns the area unchanged).
    legendArea = Rect2Dd(0.0, titleBand,
                         static_cast<double>(GetWidth()),
                         std::max(0.0, static_cast<double>(GetHeight()) - titleBand));
    engineLegend.Invalidate();               // layout inputs may have changed
    legendBox = engineLegend.Measure(ctx, legendArea).box;
    if (legendBox.width > 0.0 && legendBox.height > 0.0) {
        const Rect2Dd remaining = engineLegend.RemainingArea(legendArea);
        request.Reserve(ChartAxisEdge::Left, remaining.x - legendArea.x);
        request.Reserve(ChartAxisEdge::Right, legendArea.Right() - remaining.Right());
        request.Reserve(ChartAxisEdge::Top, remaining.y - legendArea.y);
        request.Reserve(ChartAxisEdge::Bottom,
                        legendArea.Bottom() - remaining.Bottom());
    }

    // The content's own reservations are remembered separately: they are the
    // bands its solved labels may spill into (BuildLabelPlan), so a bar that
    // reaches the axis maximum keeps its value label - just above the plot
    // edge instead of pushed down onto the bar.
    ChartLayoutRequest contentRequest;
    MeasureContent(ctx, contentRequest);
    contentMargins = contentRequest.margins;
    request.margins.Merge(contentMargins);
    request.margins.top += titleBand;

    // ---- solve --------------------------------------------------------------
    const Rect2Dd chartArea(0.0, 0.0, static_cast<double>(GetWidth()),
                            static_cast<double>(GetHeight()));
    const Rect2Dd plot = SolvePlotArea(chartArea, request.margins);
    engineProjection->SetPlotArea(plot);

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
    // Solved labels stay within the plot area plus the bands the content
    // reserved for itself in MeasureContent. Axis bands, the title band and
    // the legend margin stay out of bounds (the legend additionally rides the
    // plan as an obstacle below, so a right-spill band shared with it is
    // steered around, not overprinted).
    const Rect2Dd chartArea(0.0, 0.0, static_cast<double>(GetWidth()),
                            static_cast<double>(GetHeight()));
    options.bounds = SolveLabelBounds(frame.plotArea, contentMargins, chartArea);

    ChartLabelBroker broker(labelPolicy, options);

    // The legend is opaque chrome: solved labels must steer around it. This
    // matters most for inset placements, which float over the plot.
    if (legendBox.width > 0.0) {
        ChartLabelRequest legend;
        legend.klass = ChartLabelClass::LegendEntry;
        legend.fixedBounds = legendBox;
        broker.Add(legend);
    }

    // Limiter captions ride the plan so they cannot overprint value labels.
    ctx->SetFontSize(axisFontSize);
    for (const ChartLimiter& limiter : limiters) {
        if (limiter.caption.empty() || limiter.axisIndex >= engineAxes.Count()) continue;
        const ChartAxis& axis = engineAxes.At(limiter.axisIndex);
        const double v = axis.Normalize(limiter.value);
        // Near the far end of the line's extent, along whichever direction
        // the limiter runs (across the value range for a domain axis).
        const bool domainLimiter = !axis.inPlot && IsDomainAxis(axis);
        // A ring under Polar ends where it starts, beside the radial rule and
        // its labels: caption it a little way round instead, clear of them.
        const bool polarRing = projectionKind == ChartProjectionKind::Polar && !domainLimiter;
        const double along = limiter.extentStart +
                             (limiter.extentEnd - limiter.extentStart) * (polarRing ? 0.06 : 0.98);
        const Point2Dd anchor = engineProjection->ToScreen(
            domainLimiter ? ChartNormalizedPoint(v, along) : ChartNormalizedPoint(along, v));
        ChartLabelRequest caption;
        caption.text = limiter.caption;
        caption.klass = ChartLabelClass::LimiterCaption;
        caption.anchor = anchor;
        const Size2Di size = ctx->GetTextLineDimensions(limiter.caption);
        caption.textSize = Size2Dd(size.width, size.height);
        caption.preferredSide = polarRing ? LabelSide::Right : LabelSide::Top;
        caption.priority = 5;
        broker.Add(caption);
    }

    // Highlight labels ride the plan too, anchored at the group's centre.
    for (const ChartHighlight& h : highlights) {
        if (h.label.empty()) continue;
        Point2Dd anchor;
        if (h.shape == ChartHighlightShape::ValueBand) {
            if (h.axisIndex >= engineAxes.Count()) continue;
            const ChartAxis& axis = engineAxes.At(h.axisIndex);
            const double mid = axis.Normalize((h.bandLow + h.bandHigh) / 2.0);
            // A band on a domain axis is a vertical strip (a wedge under
            // Polar): its label sits halfway up it, not across the domain.
            const bool domainBand = !axis.inPlot && IsDomainAxis(axis);
            anchor = engineProjection->ToScreen(domainBand ? ChartNormalizedPoint(mid, 0.5)
                                                           : ChartNormalizedPoint(0.5, mid));
        } else if (h.members.empty()) {
            anchor = MapHighlightPoint(
                h, Point2Dd(h.rectValueSpace.x + h.rectValueSpace.width / 2.0,
                            h.rectValueSpace.y + h.rectValueSpace.height / 2.0));
        } else {
            double mx = 0.0, my = 0.0;
            size_t used = 0;
            for (const Point2Dd& m : h.members) {
                const Point2Dd p = MapHighlightPoint(h, m);
                if (std::isnan(p.x)) continue;
                mx += p.x;
                my += p.y;
                ++used;
            }
            if (used == 0) continue;
            anchor = Point2Dd(mx / used, my / used);
        }
        if (std::isnan(anchor.x)) continue;

        ChartLabelRequest request;
        request.text = h.label;
        request.klass = ChartLabelClass::HighlightLabel;
        request.anchor = anchor;
        const Size2Di size = ctx->GetTextLineDimensions(h.label);
        request.textSize = Size2Dd(size.width, size.height);
        request.preferredSide = LabelSide::Top;
        request.priority = 6;
        broker.Add(request);
    }

    // Span captions are fixed boxes on their lines: solved labels avoid them.
    for (const ChartSpanAnnotation& span : spanAnnotations) {
        Point2Dd a, b;
        if (span.label.empty() || !MapSpan(span, a, b)) continue;
        broker.AddObstacleRect(SpanCaptionBox(ctx, span, a, b));
    }

    // Under Polar the rim labels sit inside the label bounds' reach, so the
    // solved labels must steer around them, as they do around the legend.
    if (projectionKind == ChartProjectionKind::Polar) {
        for (size_t i = 0; i < engineAxes.Count(); ++i) {
            const ChartAxis& axis = engineAxes.At(i);
            if (!axis.visible || axis.inPlot || !IsDomainAxis(axis)) continue;
            for (const PolarRimTick& tick : PolarRimTicks(ctx, i)) {
                if (tick.labelled) broker.AddObstacleRect(tick.box);
            }
        }
    }

    // Tick labels drawn inside the plot - an in-plot axis, or the radial
    // value rule under Polar - are obstacles too: they render above the
    // content, so a solved label on top of one ("0 ppm" under a band's
    // caption) overprinted it. Edge axes sit in the margins, outside the
    // label bounds, and need nothing. Which ticks keep a label is decided
    // first, so only the labels that will be drawn claim space.
    DeclutterAxisTickLabels(ctx);
    std::vector<std::vector<bool>> tickVisible = std::move(labelPlan.tickVisible);
    ctx->SetFontSize(axisFontSize);
    for (size_t i = 0; i < engineAxes.Count(); ++i) {
        const ChartAxis& axis = engineAxes.At(i);
        if (!axis.visible) continue;
        const bool polarValueRule = projectionKind == ChartProjectionKind::Polar &&
                                    !axis.inPlot && !IsDomainAxis(axis);
        if (!polarValueRule && !(axis.inPlot && axis.showTickLabels)) continue;
        Point2Dd lowEnd, highEnd;
        AxisScreenLine(axis, lowEnd, highEnd);
        const std::vector<ChartTick> ticks = axis.GenerateTicks(targetTickCount);
        for (size_t t = 0; t < ticks.size(); ++t) {
            if (i < tickVisible.size() && t < tickVisible[i].size() && !tickVisible[i][t]) continue;
            broker.AddObstacleRect(TickLabelBox(axis, lowEnd, highEnd, ticks[t].normalized,
                                                ctx->GetTextLineDimensions(ticks[t].label)));
        }
    }

    CollectChartLabels(ctx, broker);
    labelPlan = broker.Solve(generation);
    labelPlan.tickVisible = std::move(tickVisible);
}

void UltraCanvasChartEngineElement::DeclutterAxisTickLabels(IRenderContext* ctx) {
    labelPlan.tickVisible.clear();
    labelPlan.tickVisible.resize(engineAxes.Count());
    ctx->SetFontSize(axisFontSize);

    for (size_t i = 0; i < engineAxes.Count(); ++i) {
        const ChartAxis& axis = engineAxes.At(i);
        if (!axis.visible) continue;
        // Rim labels declutter themselves around the circle.
        if (projectionKind == ChartProjectionKind::Polar && !axis.inPlot &&
            IsDomainAxis(axis)) continue;
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
    // A value edge axis under Polar is the radial rule at the start angle,
    // exactly where an in-plot axis at position 0 would stand.
    const bool polarValueRule = projectionKind == ChartProjectionKind::Polar &&
                                !axis.inPlot && !IsDomainAxis(axis);
    if (axis.inPlot || polarValueRule) {
        const double at = axis.inPlot ? axis.plotPosition : 0.0;
        lowEnd = engineProjection->ToScreen(ChartNormalizedPoint(at, 0.0));
        highEnd = engineProjection->ToScreen(ChartNormalizedPoint(at, 1.0));
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

    // Stacked edge axes sit their measured offset further out (RunLayout).
    double offset = 0.0;
    if (engineAxes.Count() > 0 && &axis >= &engineAxes.At(0) &&
        &axis <= &engineAxes.At(engineAxes.Count() - 1)) {
        const size_t index = static_cast<size_t>(&axis - &engineAxes.At(0));
        if (index < axisEdgeOffsets.size()) offset = axisEdgeOffsets[index];
    }

    if (verticalEdge) {
        const double x = (axis.side == ChartAxisSide::Right) ? plot.Right() + offset
                                                             : plot.x - offset;
        // Whichever normalised coordinate actually drives screen-y along this
        // edge decides where normalised 0 sits.
        const double du = alongU.y - origin.y;
        const double dv = alongV.y - origin.y;
        const bool downward = ((std::abs(dv) >= std::abs(du)) ? dv : du) >= 0.0;
        lowEnd  = Point2Dd(x, downward ? plot.y : plot.Bottom());
        highEnd = Point2Dd(x, downward ? plot.Bottom() : plot.y);
    } else {
        const double y = (axis.side == ChartAxisSide::Top) ? plot.y - offset
                                                           : plot.Bottom() + offset;
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
    RenderEngineBackground(ctx);        // slot 100
    RenderEngineHighlights(ctx, 200);   // slot 200 - washes under the grid
    RenderEngineGrid(ctx);              // slot 300
    RenderEngineLimiters(ctx);          // slot 400
    RenderEngineAxes(ctx);              // slot 500 - edge axes only
    RenderEngineTitle(ctx);
}

void UltraCanvasChartEngineElement::RenderPhaseOver(IRenderContext* ctx) {
    // In-plot axes sit above the content: a dense chart would otherwise bury
    // its own axis rules and value labels under the data marks.
    RenderEngineInPlotAxes(ctx);
    RenderEngineHighlights(ctx, 700);   // slot 700 - overlays above content
    RenderEngineSpanAnnotations(ctx);   // slot 700
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
    if (projectionKind != ChartProjectionKind::Polar) {
        ctx->FillRectangle(frame.plotArea);
        return;
    }
    // A round chart gets a round plot area: the disk, the polygon web, or the
    // sector of a partial sweep (closed through the inner radius).
    std::vector<Point2Dd> area = PolarRing(1.0);
    if (!PolarFullCircle()) {
        const std::vector<Point2Dd> inner = PolarRing(0.0);
        area.insert(area.end(), inner.rbegin(), inner.rend());
    }
    if (area.size() >= 3) ctx->FillLinePath(area);
}

Point2Dd UltraCanvasChartEngineElement::MapHighlightPoint(
        const ChartHighlight& highlight, const Point2Dd& value) const {
    if (highlight.xAxisIndex >= engineAxes.Count() ||
        highlight.yAxisIndex >= engineAxes.Count() || !engineProjection) {
        return Point2Dd(std::nan(""), std::nan(""));
    }
    const double u = engineAxes.At(highlight.xAxisIndex).Normalize(value.x);
    const double v = engineAxes.At(highlight.yAxisIndex).Normalize(value.y);
    return engineProjection->ToScreen(ChartNormalizedPoint(u, v));
}

void UltraCanvasChartEngineElement::RenderEngineHighlights(IRenderContext* ctx,
                                                           int zSlot) {
    if (highlights.empty()) return;
    ctx->PushState();
    ctx->ClipRect(frame.plotArea);

    for (const ChartHighlight& h : highlights) {
        if (h.zSlot != zSlot) continue;

        if (h.dashed) ctx->SetLineDash(UCDashPattern({6.0, 4.0}));
        const bool strokes = h.strokeWidth > 0.0f && h.stroke.a > 0;

        switch (h.shape) {
            case ChartHighlightShape::Rectangle:
            case ChartHighlightShape::Ellipse: {
                const Point2Dd a = MapHighlightPoint(
                    h, Point2Dd(h.rectValueSpace.x, h.rectValueSpace.y));
                const Point2Dd b = MapHighlightPoint(
                    h, Point2Dd(h.rectValueSpace.Right(), h.rectValueSpace.Bottom()));
                if (std::isnan(a.x) || std::isnan(b.x)) break;
                const Rect2Dd screen(std::min(a.x, b.x), std::min(a.y, b.y),
                                     std::abs(b.x - a.x), std::abs(b.y - a.y));
                if (h.shape == ChartHighlightShape::Rectangle) {
                    ctx->SetFillPaint(h.fill);
                    ctx->FillRectangle(screen);
                    if (strokes) {
                        ctx->SetStrokePaint(h.stroke);
                        ctx->SetStrokeWidth(h.strokeWidth);
                        ctx->DrawRectangle(screen);
                    }
                } else {
                    ctx->SetFillPaint(h.fill);
                    ctx->FillEllipse(screen);
                    if (strokes) {
                        ctx->SetStrokePaint(h.stroke);
                        ctx->SetStrokeWidth(h.strokeWidth);
                        ctx->DrawEllipse(screen);
                    }
                }
                break;
            }
            case ChartHighlightShape::ConfidenceEllipse: {
                std::vector<Point2Dd> screen;
                screen.reserve(h.members.size());
                for (const Point2Dd& m : h.members) {
                    const Point2Dd p = MapHighlightPoint(h, m);
                    if (!std::isnan(p.x)) screen.push_back(p);
                }
                const ChartEllipseSpec ellipse =
                    ComputeConfidenceEllipse(screen, h.confidence);
                if (!ellipse.valid) break;
                ctx->ClearPath();
                ctx->Ellipse(ellipse.center.x, ellipse.center.y, ellipse.rx,
                             std::max(ellipse.ry, 1.0), ellipse.rotationRadians);
                ctx->SetFillPaint(h.fill);
                ctx->FillPathPreserve();
                if (strokes) {
                    ctx->SetStrokePaint(h.stroke);
                    ctx->SetStrokeWidth(h.strokeWidth);
                    ctx->StrokePathPreserve();
                }
                ctx->ClearPath();
                if (h.showMeanMarker) {
                    Color mean = h.stroke;
                    mean.a = 255;
                    ctx->SetFillPaint(mean);
                    ctx->FillCircle(ellipse.center, 3.5);
                }
                break;
            }
            case ChartHighlightShape::Hull:
            case ChartHighlightShape::Blob: {
                std::vector<Point2Dd> screen;
                screen.reserve(h.members.size());
                for (const Point2Dd& m : h.members) {
                    const Point2Dd p = MapHighlightPoint(h, m);
                    if (!std::isnan(p.x)) screen.push_back(p);
                }
                std::vector<Point2Dd> outline =
                    ExpandPolygon(ComputeConvexHull(std::move(screen)), h.padding);
                if (outline.size() < 3) break;
                if (h.shape == ChartHighlightShape::Blob) {
                    const int iterations = std::clamp(
                        static_cast<int>(std::lround(h.smoothing * 4.0)), 0, 4);
                    outline = SmoothPolygonChaikin(outline, iterations);
                }
                ctx->SetFillPaint(h.fill);
                ctx->FillLinePath(outline);
                if (strokes) {
                    ctx->SetStrokePaint(h.stroke);
                    ctx->SetStrokeWidth(h.strokeWidth);
                    ctx->DrawLinePath(outline, true);
                }
                break;
            }
            case ChartHighlightShape::ValueBand: {
                if (h.axisIndex >= engineAxes.Count()) break;
                const ChartAxis& axis = engineAxes.At(h.axisIndex);
                const double lo = axis.Normalize(std::min(h.bandLow, h.bandHigh));
                const double hi = axis.Normalize(std::max(h.bandLow, h.bandHigh));
                // A band on a value axis spans the domain; on a domain axis it
                // spans the value range. The outline is sampled through the
                // projection, so under Polar a value band is a ring (a radial
                // tolerance zone) and a domain band a wedge (a wind sector).
                const bool domainBand = !axis.inPlot && IsDomainAxis(axis);
                const double u0 = domainBand ? lo : 0.0, u1 = domainBand ? hi : 1.0;
                const double v0 = domainBand ? 0.0 : lo, v1 = domainBand ? 1.0 : hi;
                const bool polarBand = projectionKind == ChartProjectionKind::Polar;
                const std::vector<Point2Dd> outline = BuildBarOutline(
                    *engineProjection, u0, v0, u1, v1, polarBand ? 96 : 1);
                if (outline.size() < 3) break;
                ctx->SetFillPaint(h.fill);
                ctx->FillLinePath(outline);
                if (strokes) {
                    // The two edges along the band, not its ends.
                    auto edge = [&](bool atLow) {
                        std::vector<Point2Dd> line;
                        const int samples = polarBand ? 96 : 1;
                        for (int k = 0; k <= samples; ++k) {
                            const double t = static_cast<double>(k) / samples;
                            line.push_back(engineProjection->ToScreen(domainBand
                                ? ChartNormalizedPoint(atLow ? lo : hi, t)
                                : ChartNormalizedPoint(t, atLow ? lo : hi)));
                        }
                        ctx->DrawLinePath(line, false);
                    };
                    ctx->SetStrokePaint(h.stroke);
                    ctx->SetStrokeWidth(h.strokeWidth);
                    edge(true);
                    edge(false);
                }
                break;
            }
            case ChartHighlightShape::PointHalo: {
                ctx->SetFillPaint(h.fill);
                for (const Point2Dd& m : h.members) {
                    const Point2Dd p = MapHighlightPoint(h, m);
                    if (!std::isnan(p.x)) ctx->FillCircle(p, h.padding);
                }
                break;
            }
        }
        if (h.dashed) ctx->SetLineDash(UCDashPattern());
    }
    ctx->PopState();
}

void UltraCanvasChartEngineElement::RenderEngineGrid(IRenderContext* ctx) {
    ctx->SetStrokePaint(gridColor);
    ctx->SetStrokeWidth(1.0f);
    RenderGridFrom(ctx, gridAxisIndex);
    if (domainGridAxisIndex != gridAxisIndex) RenderGridFrom(ctx, domainGridAxisIndex);
}

void UltraCanvasChartEngineElement::RenderGridFrom(IRenderContext* ctx, size_t axisIndex) {
    if (axisIndex >= engineAxes.Count()) return;
    const ChartAxis& axis = engineAxes.At(axisIndex);
    const bool polar = (projectionKind == ChartProjectionKind::Polar);
    // In-plot axes keep the value direction they always had.
    const bool domain = !axis.inPlot && IsDomainAxis(axis);

    // Gridlines run perpendicular to the grid axis, one per tick - derived
    // from the same ticks the labels use, so they always line up.
    std::vector<double> drawn;
    for (const ChartTick& tick : axis.GenerateTicks(targetTickCount)) {
        if (domain) {
            // Across the value range: vertical lines, or spokes under Polar.
            // A full circle maps u = 0 and u = 1 onto the same spoke.
            double u = tick.normalized;
            if (polar && PolarFullCircle()) u -= std::floor(u);
            bool duplicate = false;
            for (double d : drawn) duplicate = duplicate || std::abs(d - u) < 1e-6;
            if (duplicate) continue;
            drawn.push_back(u);
            ctx->DrawLine(engineProjection->ToScreen(ChartNormalizedPoint(u, 0.0)),
                          engineProjection->ToScreen(ChartNormalizedPoint(u, 1.0)));
        } else if (polar) {
            const std::vector<Point2Dd> ring = PolarRing(tick.normalized);
            if (ring.size() >= 2) ctx->DrawLinePath(ring, PolarFullCircle());
        } else {
            ctx->DrawLine(engineProjection->ToScreen(ChartNormalizedPoint(0.0, tick.normalized)),
                          engineProjection->ToScreen(ChartNormalizedPoint(1.0, tick.normalized)));
        }
    }
}

const ChartAxis* UltraCanvasChartEngineElement::PolarDomainAxis() const {
    if (domainGridAxisIndex < engineAxes.Count()) return &engineAxes.At(domainGridAxisIndex);
    for (const ChartAxis& axis : engineAxes) {
        if (!axis.inPlot && IsDomainAxis(axis)) return &axis;
    }
    return nullptr;
}

std::vector<Point2Dd> UltraCanvasChartEngineElement::PolarRing(double v) const {
    std::vector<Point2Dd> ring;
    const ChartAxis* domain = PolarDomainAxis();
    if (polarGridShape == ChartPolarGridShape::Polygon && domain) {
        // The radar web: straight chords between neighbouring spokes.
        std::vector<double> us;
        for (const ChartTick& tick : domain->GenerateTicks(targetTickCount)) {
            double u = tick.normalized;
            if (PolarFullCircle()) u -= std::floor(u);
            bool duplicate = false;
            for (double d : us) duplicate = duplicate || std::abs(d - u) < 1e-6;
            if (!duplicate) us.push_back(u);
        }
        std::sort(us.begin(), us.end());
        if (us.size() >= 3) {
            for (double u : us) ring.push_back(engineProjection->ToScreen(ChartNormalizedPoint(u, v)));
            return ring;
        }
    }
    constexpr int kSegments = 120;
    for (int k = 0; k <= kSegments; ++k) {
        if (PolarFullCircle() && k == kSegments) break;   // closed by the caller
        ring.push_back(engineProjection->ToScreen(
            ChartNormalizedPoint(static_cast<double>(k) / kSegments, v)));
    }
    return ring;
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
        // A limiter on a domain axis (an average age across a pyramid's age
        // rows, a date marker) runs across the value range instead.
        const bool domainLimiter = !axis.inPlot && IsDomainAxis(axis);
        const double e0 = limiter.extentStart, e1 = limiter.extentEnd;
        if (projectionKind == ChartProjectionKind::Polar && !domainLimiter) {
            // A value limiter under Polar is a ring (an arc for a partial
            // extent); drawn as a straight line it collapsed to a point.
            constexpr int kSegments = 96;
            std::vector<Point2Dd> arc;
            for (int k = 0; k <= kSegments; ++k) {
                const double u = e0 + (e1 - e0) * static_cast<double>(k) / kSegments;
                arc.push_back(engineProjection->ToScreen(ChartNormalizedPoint(u, v)));
            }
            ctx->DrawLinePath(arc, false);
            if (limiter.dashed) ctx->SetLineDash(UCDashPattern());
            continue;
        }
        ctx->DrawLine(engineProjection->ToScreen(domainLimiter ? ChartNormalizedPoint(v, e0)
                                                               : ChartNormalizedPoint(e0, v)),
                      engineProjection->ToScreen(domainLimiter ? ChartNormalizedPoint(v, e1)
                                                               : ChartNormalizedPoint(e1, v)));
        if (limiter.dashed) ctx->SetLineDash(UCDashPattern());
    }
}

void UltraCanvasChartEngineElement::RenderBarConnections(
        IRenderContext* ctx, const std::vector<ChartBarConnection>& connections,
        const std::vector<Color>& seriesColors, const ChartBarConnectionStyle& style,
        size_t emphasizedSeries) {
    if (connections.empty()) return;
    const Color halo = style.haloColor.a > 0 ? style.haloColor : engineTheme.plotAreaColor;

    ctx->PushState();
    ctx->SetLineJoin(LineJoin::Round);
    ctx->SetLineCap(LineCap::Round);
    if (style.dashed) ctx->SetLineDash(UCDashPattern({6.0, 4.0}));
    for (const ChartBarConnection& connection : connections) {
        if (connection.points.size() < 2) continue;
        const Color color = connection.seriesIndex < seriesColors.size()
                                ? seriesColors[connection.seriesIndex]
                                : Palette().ColorAt(connection.seriesIndex);
        const float width = (connection.seriesIndex == emphasizedSeries &&
                             style.emphasisWidth > 0.0f)
                                ? style.emphasisWidth : style.width;
        if (style.haloWidth > 0.0f) {
            ctx->SetStrokePaint(halo);
            ctx->SetStrokeWidth(width + style.haloWidth * 2.0f);
            ctx->DrawLinePath(connection.points, false);
        }
        ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(width);
        ctx->DrawLinePath(connection.points, false);
    }
    if (style.dashed) ctx->SetLineDash(UCDashPattern());

    if (style.markerRadius > 0.0f) {
        for (const ChartBarConnection& connection : connections) {
            const Color color = connection.seriesIndex < seriesColors.size()
                                    ? seriesColors[connection.seriesIndex]
                                    : Palette().ColorAt(connection.seriesIndex);
            for (const Point2Dd& p : connection.barPoints) {
                ctx->SetFillPaint(halo);
                ctx->FillCircle(p, style.markerRadius + style.haloWidth);
                ctx->SetFillPaint(color);
                ctx->FillCircle(p, style.markerRadius);
            }
        }
    }
    ctx->PopState();
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

void UltraCanvasChartEngineElement::TickDirection(const ChartAxis& axis, const Point2Dd& lowEnd,
                                                  const Point2Dd& highEnd,
                                                  double& tickDx, double& tickDy) const {
    // Ticks point away from the plot for edge axes, left for in-plot ones.
    tickDx = 0.0;
    tickDy = 0.0;
    const bool alongY = std::abs(highEnd.y - lowEnd.y) > std::abs(highEnd.x - lowEnd.x);
    if (alongY) {
        tickDx = (axis.side == ChartAxisSide::Right && !axis.inPlot) ? tickLength
                                                                     : -tickLength;
    } else {
        tickDy = (axis.side == ChartAxisSide::Top && !axis.inPlot) ? -tickLength
                                                                   : tickLength;
    }
}

Rect2Dd UltraCanvasChartEngineElement::TickLabelBox(const ChartAxis& axis, const Point2Dd& lowEnd,
                                                    const Point2Dd& highEnd, double normalized,
                                                    const Size2Di& size) const {
    const Point2Dd p{lowEnd.x + (highEnd.x - lowEnd.x) * normalized,
                     lowEnd.y + (highEnd.y - lowEnd.y) * normalized};
    double tickDx = 0.0, tickDy = 0.0;
    TickDirection(axis, lowEnd, highEnd, tickDx, tickDy);
    const bool alongY = std::abs(highEnd.y - lowEnd.y) > std::abs(highEnd.x - lowEnd.x);
    if (alongY) {
        const double x = (tickDx < 0.0)
            ? p.x + tickDx - tickLabelGap - size.width
            : p.x + tickDx + tickLabelGap;
        return Rect2Dd(x, p.y - size.height * 0.5, size.width, size.height);
    }
    const double y = (tickDy < 0.0)
        ? p.y + tickDy - tickLabelGap - size.height
        : p.y + tickDy + tickLabelGap;
    // Centred on the tick, but never past the element's edge.
    const double x = std::clamp(p.x - size.width * 0.5, 0.0,
                                std::max(0.0, static_cast<double>(GetWidth() - size.width)));
    return Rect2Dd(x, y, size.width, size.height);
}

void UltraCanvasChartEngineElement::RenderAxisFurniture(IRenderContext* ctx, size_t i) {
    const ChartAxis& axis = engineAxes.At(i);
    if (projectionKind == ChartProjectionKind::Polar && !axis.inPlot && IsDomainAxis(axis)) {
        RenderPolarRimAxis(ctx, i);
        return;
    }

    Point2Dd lowEnd, highEnd;
    AxisScreenLine(axis, lowEnd, highEnd);

    // The rule covers the axis's sub-band only (stacked panes), so a price
    // axis above a volume axis reads as two axes on the same edge.
    auto along = [&](double t) {
        return Point2Dd(lowEnd.x + (highEnd.x - lowEnd.x) * t,
                        lowEnd.y + (highEnd.y - lowEnd.y) * t);
    };
    ctx->SetStrokePaint(axisLineColor);
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawLine(along(axis.bandStart), along(axis.bandEnd));

    const bool alongY = std::abs(highEnd.y - lowEnd.y) > std::abs(highEnd.x - lowEnd.x);
    double tickDx = 0.0, tickDy = 0.0;
    TickDirection(axis, lowEnd, highEnd, tickDx, tickDy);

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
        const Rect2Dd box = TickLabelBox(axis, lowEnd, highEnd, tick.normalized,
                                         ctx->GetTextLineDimensions(tick.label));
        ctx->DrawText(tick.label, box.TopLeft());
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

    // Axis titles. An in-plot axis keeps its title past the low end (the
    // parallel coordinate headers). An edge axis puts it outside its tick
    // labels, centred on its band: below a bottom axis, above a top one, and
    // rotated along a left or right axis - where the measure pass reserved
    // it. Drawn past the low end, a left axis title landed on the first
    // label of the bottom axis.
    if (!axis.title.empty()) {
        ctx->SetTextPaint(axisLabelColor);
        const Size2Di size = ctx->GetTextLineDimensions(axis.title);
        if (axis.inPlot) {
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
        } else {
            double labelExtent = 0.0;
            for (const ChartTick& tick : ticks) {
                const Size2Di labelSize = ctx->GetTextLineDimensions(tick.label);
                labelExtent = std::max(labelExtent, static_cast<double>(
                                           alongY ? labelSize.width : labelSize.height));
            }
            const Point2Dd mid = along((axis.bandStart + axis.bandEnd) * 0.5);
            const double reach = tickLength + tickLabelGap + labelExtent + 4.0;
            if (alongY) {
                const bool right = (axis.side == ChartAxisSide::Right);
                const double cx = right ? mid.x + reach + size.height * 0.5
                                        : mid.x - reach - size.height * 0.5;
                ctx->PushState();
                ctx->Translate(cx, mid.y);
                ctx->Rotate((right ? 90.0 : -90.0) * M_PI / 180.0);
                ctx->DrawText(axis.title, Point2Dd(-size.width * 0.5, -size.height * 0.5));
                ctx->PopState();
            } else {
                const bool top = (axis.side == ChartAxisSide::Top);
                const double y = top ? mid.y - reach - size.height : mid.y + reach;
                ctx->DrawText(axis.title, Point2Dd(mid.x - size.width * 0.5, y));
            }
        }
    }
}

Rect2Dd UltraCanvasChartEngineElement::PolarRimLabelBox(double u, const Size2Di& size) const {
    const auto* polar = dynamic_cast<const ChartPolarProjection*>(engineProjection.get());
    const Point2Dd c = polar ? polar->Center() : frame.plotArea.Center();
    const Point2Dd rim = engineProjection->ToScreen(ChartNormalizedPoint(u, 1.0));
    double dx = rim.x - c.x, dy = rim.y - c.y;
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-9) { dx = 0.0; dy = -1.0; } else { dx /= len; dy /= len; }
    const double reach = tickLength + tickLabelGap;
    const Point2Dd p(rim.x + dx * reach, rim.y + dy * reach);
    // Push the box outward along the radius: a label at 3 o'clock starts at
    // the anchor, one at 12 o'clock sits centred above it.
    return Rect2Dd(p.x - size.width * 0.5 + dx * size.width * 0.5,
                   p.y - size.height * 0.5 + dy * size.height * 0.5,
                   size.width, size.height);
}

std::vector<UltraCanvasChartEngineElement::PolarRimTick>
UltraCanvasChartEngineElement::PolarRimTicks(IRenderContext* ctx, size_t axisIndex) const {
    std::vector<PolarRimTick> rimTicks;
    if (axisIndex >= engineAxes.Count()) return rimTicks;
    const ChartAxis& axis = engineAxes.At(axisIndex);
    ctx->SetFontSize(axisFontSize);
    std::vector<Rect2Dd> placed;
    for (const ChartTick& tick : axis.GenerateTicks(targetTickCount)) {
        double u = tick.normalized;
        if (PolarFullCircle()) u -= std::floor(u);
        if (u < -1e-6 || u > 1.0 + 1e-6) continue;
        bool duplicate = false;
        for (const PolarRimTick& t : rimTicks) duplicate = duplicate || std::abs(t.u - u) < 1e-6;
        if (duplicate) continue;      // 0 and 360 degrees are one spoke

        PolarRimTick rimTick;
        rimTick.u = u;
        rimTick.label = tick.label;
        rimTick.box = PolarRimLabelBox(u, ctx->GetTextLineDimensions(tick.label));
        // Declutter around the circle: a label that would overprint one
        // already placed is dropped (the rim has no ends to protect).
        rimTick.labelled = true;
        for (const Rect2Dd& other : placed) {
            if (rimTick.box.x < other.Right() + 2.0 && other.x < rimTick.box.Right() + 2.0 &&
                rimTick.box.y < other.Bottom() + 1.0 && other.y < rimTick.box.Bottom() + 1.0) {
                rimTick.labelled = false;
                break;
            }
        }
        if (rimTick.labelled) placed.push_back(rimTick.box);
        rimTicks.push_back(std::move(rimTick));
    }
    return rimTicks;
}

void UltraCanvasChartEngineElement::RenderPolarRimAxis(IRenderContext* ctx, size_t i) {
    const auto* polar = dynamic_cast<const ChartPolarProjection*>(engineProjection.get());
    if (!polar) return;
    const Point2Dd c = polar->Center();

    // The rim itself, at the outer edge of the value range.
    ctx->SetStrokePaint(axisLineColor);
    ctx->SetStrokeWidth(1.0f);
    const std::vector<Point2Dd> rim = PolarRing(1.0);
    if (rim.size() >= 2) ctx->DrawLinePath(rim, PolarFullCircle());

    for (const PolarRimTick& tick : PolarRimTicks(ctx, i)) {
        const Point2Dd p = engineProjection->ToScreen(ChartNormalizedPoint(tick.u, 1.0));
        double dx = p.x - c.x, dy = p.y - c.y;
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len > 1e-9) { dx /= len; dy /= len; }
        ctx->SetStrokePaint(axisTickColor);
        ctx->DrawLine(p, Point2Dd(p.x + dx * tickLength, p.y + dy * tickLength));
        if (!tick.labelled) continue;
        ctx->SetTextPaint(axisLabelColor);
        ctx->DrawText(tick.label, tick.box.TopLeft());
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
    // The shared component draws itself against the same area it was
    // measured with during layout; the call is cheap when nothing changed.
    engineLegend.Render(ctx, legendArea);
}

// =============================================================================
// SPAN ANNOTATIONS
// =============================================================================

bool UltraCanvasChartEngineElement::MapSpan(const ChartSpanAnnotation& span,
                                            Point2Dd& a, Point2Dd& b) const {
    if (span.xAxisIndex >= engineAxes.Count() || span.yAxisIndex >= engineAxes.Count() ||
        !engineProjection) return false;
    const ChartAxis& x = engineAxes.At(span.xAxisIndex);
    const ChartAxis& y = engineAxes.At(span.yAxisIndex);
    a = engineProjection->ToScreen(ChartNormalizedPoint(x.Normalize(span.from.x),
                                                        y.Normalize(span.from.y)));
    b = engineProjection->ToScreen(ChartNormalizedPoint(x.Normalize(span.to.x),
                                                        y.Normalize(span.to.y)));
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(b.x) && std::isfinite(b.y);
}

Rect2Dd UltraCanvasChartEngineElement::SpanCaptionBox(IRenderContext* ctx,
                                                      const ChartSpanAnnotation& span,
                                                      const Point2Dd& a,
                                                      const Point2Dd& b) const {
    ctx->SetFontSize(axisFontSize);
    const Size2Di size = ctx->GetTextLineDimensions(span.label);
    const Point2Dd mid((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
    const double w = size.width + 16.0, h = size.height + 8.0;
    // A mostly horizontal span carries its caption above the line, a
    // vertical one beside it, so the box never sits on the arrow.
    if (std::abs(b.x - a.x) >= std::abs(b.y - a.y)) {
        return Rect2Dd(mid.x - w * 0.5, mid.y - h - 5.0, w, h);
    }
    return Rect2Dd(mid.x + 10.0, mid.y - h * 0.5, w, h);
}

void UltraCanvasChartEngineElement::RenderEngineSpanAnnotations(IRenderContext* ctx) {
    if (spanAnnotations.empty()) return;
    ctx->PushState();
    for (const ChartSpanAnnotation& span : spanAnnotations) {
        Point2Dd a, b;
        if (!MapSpan(span, a, b)) continue;
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double length = std::sqrt(dx * dx + dy * dy);
        if (length < span.arrowSize * 2.0 + 4.0) continue;   // no room for the arrows
        const double ux = dx / length, uy = dy / length;

        ctx->SetStrokePaint(span.color);
        ctx->SetStrokeWidth(span.width);
        ctx->DrawLine(Point2Dd(a.x + ux * 2.0, a.y + uy * 2.0),
                      Point2Dd(b.x - ux * 2.0, b.y - uy * 2.0));
        auto arrow = [&](const Point2Dd& tip, double angle) {
            const double s = span.arrowSize;
            ctx->SetFillPaint(span.color);
            ctx->FillLinePath({tip,
                               Point2Dd(tip.x - s * std::cos(angle - 0.42),
                                        tip.y - s * std::sin(angle - 0.42)),
                               Point2Dd(tip.x - s * std::cos(angle + 0.42),
                                        tip.y - s * std::sin(angle + 0.42))});
        };
        const double angle = std::atan2(dy, dx);
        arrow(b, angle);
        arrow(a, angle + M_PI);

        if (span.label.empty()) continue;
        const Rect2Dd box = SpanCaptionBox(ctx, span, a, b);
        ctx->SetFillPaint(span.boxColor);
        ctx->FillRectangle(box);
        ctx->SetTextPaint(span.textColor);
        ctx->DrawText(span.label, Point2Dd(box.x + 8.0, box.y + 4.0));
    }
    ctx->PopState();
}

// =============================================================================
// MARKERS
// =============================================================================

void DrawChartMarker(IRenderContext* ctx, ChartMarkerShape shape, const Point2Dd& center,
                     double radius, const Color& fill, const Color& stroke,
                     float strokeWidth) {
    if (!ctx || shape == ChartMarkerShape::NoMarker || radius <= 0.0) return;
    const bool outlined = stroke.a > 0 && strokeWidth > 0.0f;

    if (shape == ChartMarkerShape::Cross || shape == ChartMarkerShape::Plus) {
        ctx->PushState();
        ctx->SetLineCap(LineCap::Round);
        const double r = radius;
        const double k = (shape == ChartMarkerShape::Cross) ? r * 0.8 : r;
        auto stroke2 = [&](const Color& color, float width) {
            ctx->SetStrokePaint(color);
            ctx->SetStrokeWidth(width);
            if (shape == ChartMarkerShape::Cross) {
                ctx->DrawLine(Point2Dd(center.x - k, center.y - k), Point2Dd(center.x + k, center.y + k));
                ctx->DrawLine(Point2Dd(center.x - k, center.y + k), Point2Dd(center.x + k, center.y - k));
            } else {
                ctx->DrawLine(Point2Dd(center.x - k, center.y), Point2Dd(center.x + k, center.y));
                ctx->DrawLine(Point2Dd(center.x, center.y - k), Point2Dd(center.x, center.y + k));
            }
        };
        const float width = static_cast<float>(std::max(1.5, radius * 0.45));
        if (outlined) stroke2(stroke, width + strokeWidth * 2.0f);
        stroke2(fill, width);
        ctx->PopState();
        return;
    }

    if (shape == ChartMarkerShape::Circle) {
        ctx->SetFillPaint(fill);
        ctx->FillCircle(center, radius);
        if (outlined) {
            ctx->SetStrokePaint(stroke);
            ctx->SetStrokeWidth(strokeWidth);
            ctx->DrawCircle(center, radius);
        }
        return;
    }

    const std::vector<Point2Dd> polygon = ChartMarkerPolygon(shape, center, radius);
    if (polygon.size() < 3) return;
    ctx->SetFillPaint(fill);
    ctx->FillLinePath(polygon);
    if (outlined) {
        ctx->SetStrokePaint(stroke);
        ctx->SetStrokeWidth(strokeWidth);
        ctx->DrawLinePath(polygon, true);
    }
}

} // namespace UltraCanvas

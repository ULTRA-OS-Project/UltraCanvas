// Plugins/Charts/Engine/Charts/UltraCanvasTernaryChart.cpp
// The ternary chart on the chart engine.
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasTernaryChart.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace UltraCanvas {

namespace {

const char* const kArrowRight = "\xE2\x86\x92";   // U+2192
const char* const kArrowLeft = "\xE2\x86\x90";    // U+2190

LegendSwatch SwatchFor(ChartMarkerShape shape) {
    switch (shape) {
        case ChartMarkerShape::Circle: return LegendSwatch::Circle;
        case ChartMarkerShape::Square: return LegendSwatch::Square;
        default:                       return LegendSwatch::Marker;
    }
}

Point2Dd Lerp(const Point2Dd& from, const Point2Dd& to, double t) {
    return Point2Dd(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
}

Point2Dd Unit(double x, double y) {
    const double length = std::hypot(x, y);
    return length > 0.0 ? Point2Dd(x / length, y / length) : Point2Dd(0.0, 0.0);
}

std::string TickText(double share) {
    char text[16];
    std::snprintf(text, sizeof(text), "%.0f", share * 100.0);   // locale-ok: a tick label a person reads
    return text;
}

std::string AmountText(double amount) {
    char text[32];
    std::snprintf(text, sizeof(text), "%g", amount);   // locale-ok: a tooltip a person reads
    return text;
}

// A box of `size` placed beyond `anchor` in `direction`, its near edge at
// `distance` from the anchor.
Rect2Dd BoxBeyond(const Point2Dd& anchor, const Point2Dd& direction, double distance, const Size2Di& size) {
    const double cx = anchor.x + direction.x * (distance + std::abs(direction.x) * size.width * 0.5);
    const double cy = anchor.y + direction.y * (distance + std::abs(direction.y) * size.height * 0.5);
    return Rect2Dd(cx - size.width * 0.5, cy - size.height * 0.5, size.width, size.height);
}

} // namespace

UltraCanvasTernaryChart::UltraCanvasTernaryChart(const std::string& id, int x, int y, int w, int h)
    : UltraCanvasChartEngineElement(id, x, y, w, h) {
    SetProjectionKind(ChartProjectionKind::Ternary);
    SetAnimateOnDataChange(true, 0.7f);     // the points spread out from the centre
    SetEnableTooltips(true);
    SetLegendPosition(ChartLegendPosition::RightStart);
}

// =============================================================================
// DATA AND OPTIONS
// =============================================================================

void UltraCanvasTernaryChart::SetModel(const TernaryModel& m) {
    model = m;
    seriesHidden.assign(model.series.size(), false);
    chartTitle = model.title;
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasTernaryChart::SetReading(TernaryReading reading) {
    model.reading = reading;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasTernaryChart::SetShowGrid(bool show) {
    showGridLines = show;
    RequestRedraw();
}

void UltraCanvasTernaryChart::SetShowEdgeTitles(bool show) {
    showEdgeTitles = show;
    MarkEngineDirty(ChartDirty::Style);     // the titles reserve margin
}

void UltraCanvasTernaryChart::SetShowPointLabels(bool show) {
    showPointLabels = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasTernaryChart::SetSeriesVisible(size_t seriesIndex, bool visible) {
    if (seriesIndex >= seriesHidden.size()) return;
    seriesHidden[seriesIndex] = !visible;
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Style);
}

bool UltraCanvasTernaryChart::IsSeriesVisible(size_t seriesIndex) const {
    return seriesIndex < seriesHidden.size() && !seriesHidden[seriesIndex];
}

Color UltraCanvasTernaryChart::SeriesColor(size_t seriesIndex) const {
    const Color& own = model.series[seriesIndex].color;
    if (own.a > 0) return own.WithAlpha(255);
    return Palette().ColorAt(seriesIndex, model.series.size());
}

void UltraCanvasTernaryChart::RefreshLegend() {
    std::vector<ChartLegendEntry> entries;
    for (size_t i = 0; i < model.series.size(); ++i) {
        ChartLegendEntry entry(model.series[i].name, SeriesColor(i), SwatchFor(model.series[i].marker));
        entry.enabled = !seriesHidden[i];
        entries.push_back(entry);
    }
    SetLegendEntries(entries);
    SetShowLegend(model.series.size() > 1);
}

void UltraCanvasTernaryChart::OnChartThemeChanged() { RefreshLegend(); }

void UltraCanvasTernaryChart::OnLegendEntryToggled(size_t entryIndex, bool enabled) {
    if (entryIndex >= seriesHidden.size()) return;
    seriesHidden[entryIndex] = !enabled;
    MarkEngineDirty(ChartDirty::Style);     // the point labels follow the visible series
}

// =============================================================================
// GEOMETRY
// =============================================================================

const ChartTernaryProjection* UltraCanvasTernaryChart::Projection() const {
    if (!Frame().projection || Frame().projection->Kind() != ChartProjectionKind::Ternary) return nullptr;
    return static_cast<const ChartTernaryProjection*>(Frame().projection);
}

ChartNormalizedPoint UltraCanvasTernaryChart::Normalized(double sa, double sb, double sc,
                                                         double progress) const {
    (void)sa;
    const double third = 1.0 / 3.0;
    return ChartNormalizedPoint(third + (sb - third) * progress, third + (sc - third) * progress);
}

Point2Dd UltraCanvasTernaryChart::CornerPoint(int corner) const {
    const ChartTernaryProjection* projection = Projection();
    return projection ? projection->Corner(std::clamp(corner, 0, 2)) : Point2Dd(0.0, 0.0);
}

Point2Dd UltraCanvasTernaryChart::SharesToScreen(double a, double b, double c) const {
    const ChartTernaryProjection* projection = Projection();
    const TernaryPoint point(a, b, c);
    double sa = 0.0, sb = 0.0, sc = 0.0;
    if (!projection || !point.Shares(sa, sb, sc)) {
        return Point2Dd(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN());
    }
    return projection->ToScreen(ChartNormalizedPoint(sb, sc));
}

bool UltraCanvasTernaryChart::ScreenToShares(const Point2Dd& screen, double& a, double& b, double& c) const {
    const ChartTernaryProjection* projection = Projection();
    if (!projection) return false;
    const ChartNormalizedPoint p = projection->ToNormalized(screen);
    const double sa = 1.0 - p.u - p.v;
    const double eps = 1e-9;
    if (p.u < -eps || p.v < -eps || sa < -eps) return false;
    a = std::max(0.0, sa);
    b = std::max(0.0, p.u);
    c = std::max(0.0, p.v);
    return true;
}

Point2Dd UltraCanvasTernaryChart::TickPosition(int corner, double share) const {
    corner = std::clamp(corner, 0, 2);
    // The labelled edge runs from the corner before this one (0 %) to this
    // corner (100 %).
    return Lerp(CornerPoint(model.LabelEdgeStart(corner)), CornerPoint(corner), share);
}

Point2Dd UltraCanvasTernaryChart::TickDirection(int corner) const {
    // Along the grid lines of this corner's part, which run parallel to the
    // edge between the other two corners: from the far corner of that edge
    // towards the labelled edge's start, which points out of the triangle.
    const int start = model.LabelEdgeStart(corner);
    const int other = 3 - corner - start;
    const Point2Dd p = CornerPoint(start), q = CornerPoint(other);
    return Unit(p.x - q.x, p.y - q.y);
}

Point2Dd UltraCanvasTernaryChart::PointPosition(size_t seriesIndex, size_t pointIndex) const {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (seriesIndex >= model.series.size() || !IsSeriesVisible(seriesIndex) ||
        pointIndex >= model.series[seriesIndex].points.size()) {
        return Point2Dd(nan, nan);
    }
    const TernaryPoint& point = model.series[seriesIndex].points[pointIndex];
    return SharesToScreen(point.a, point.b, point.c);
}

std::pair<size_t, size_t> UltraCanvasTernaryChart::GetHoveredPoint() const {
    const int64_t id = HoveredRegionId();
    if (id < 0) return {SIZE_MAX, SIZE_MAX};
    return {static_cast<size_t>(id / 65536), static_cast<size_t>(id % 65536)};
}

std::string UltraCanvasTernaryChart::EdgeTitle(int corner) const {
    const TernaryAxis& axis = model.axes[corner];
    return axis.unit.empty() ? axis.title : axis.title + " (" + axis.unit + ")";
}

std::string UltraCanvasTernaryChart::BuildPointTooltip(size_t seriesIndex, size_t pointIndex) const {
    if (seriesIndex >= model.series.size() || pointIndex >= model.series[seriesIndex].points.size()) {
        return std::string();
    }
    const TernarySeries& series = model.series[seriesIndex];
    const TernaryPoint& point = series.points[pointIndex];
    std::string text = point.label.empty() ? series.name : point.label + "\n" + series.name;
    double shares[3] = {0.0, 0.0, 0.0};
    const double amounts[3] = {point.a, point.b, point.c};
    if (!point.Shares(shares[0], shares[1], shares[2])) return text + "\n(no shares)";
    // The raw amounts only when they are not already the percentages.
    const bool raw = std::abs(point.Sum() - 100.0) > 1e-6 && std::abs(point.Sum() - 1.0) > 1e-6;
    for (int i = 0; i < 3; ++i) {
        text += "\n" + model.axes[i].title + ": " + FormatTernaryShare(shares[i]);
        if (raw) text += "  (" + AmountText(amounts[i]) + ")";
    }
    if (!point.note.empty()) text += "\n" + point.note;
    return text;
}

Rect2Dd UltraCanvasTernaryChart::CornerTitleBox(int corner, const Size2Di& size, double labelBand) const {
    // Out from the centroid through the corner, clear of the tick labels
    // that meet there.
    const Point2Dd a = CornerPoint(0), b = CornerPoint(1), c = CornerPoint(2);
    const Point2Dd centre((a.x + b.x + c.x) / 3.0, (a.y + b.y + c.y) / 3.0);
    const Point2Dd at = CornerPoint(corner);
    if (corner == 0) {
        // The apex: straight above (or below, apex down) the corner.
        const double dir = at.y < centre.y ? -1.0 : 1.0;
        return BoxBeyond(at, Point2Dd(0.0, dir), labelBand + 2.0, size);
    }
    // A base corner: below (or above) the tick labels of the base edge,
    // centred on the corner.
    const double dir = at.y > centre.y ? 1.0 : -1.0;
    return BoxBeyond(at, Point2Dd(0.0, dir), labelBand + 4.0, size);
}

double UltraCanvasTernaryChart::SubtitleHeight(IRenderContext* ctx) const {
    if (model.subtitle.empty()) return 0.0;
    ctx->SetFontSize(axisFontSize);
    return ctx->GetTextLineHeight(model.subtitle) + 4.0;
}

// =============================================================================
// ENGINE CONTRACT
// =============================================================================

void UltraCanvasTernaryChart::DescribeAxes(ChartAxisSet& axes) {
    // Two hidden axes over the shares of B (u) and C (v), so engine services
    // that map value space through the axes - highlights above all - work in
    // share space. The three visible axes are the chart's own furniture.
    ChartAxis b("b");
    b.side = ChartAxisSide::Bottom;
    b.visible = false;
    b.SetRange(0.0, 1.0);
    axes.Add(b);
    ChartAxis c("c");
    c.side = ChartAxisSide::Left;
    c.visible = false;
    c.SetRange(0.0, 1.0);
    axes.Add(c);
    SetGridAxis(static_cast<size_t>(-1));
    SetDomainGridAxis(static_cast<size_t>(-1));
}

void UltraCanvasTernaryChart::MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) {
    ctx->SetFontSize(axisFontSize);
    ctx->SetFontWeight(FontWeight::Normal);
    const Size2Di label = ctx->GetTextLineDimensions("100");
    const double band = tickLength + tickLabelGap + std::max(label.width, label.height) + 2.0;
    double edgeTitle = 0.0;
    if (showEdgeTitles) {
        ctx->SetFontWeight(FontWeight::Bold);
        edgeTitle = ctx->GetTextLineHeight("Ag") + 6.0;
    }
    ctx->SetFontSize(cornerFontSize);
    ctx->SetFontWeight(FontWeight::Bold);
    double cornerWidth = 0.0, cornerHeight = 0.0;
    for (const TernaryAxis& axis : model.axes) {
        const Size2Di size = ctx->GetTextLineDimensions(axis.title);
        cornerWidth = std::max(cornerWidth, static_cast<double>(size.width));
        cornerHeight = std::max(cornerHeight, static_cast<double>(size.height));
    }
    ctx->SetFontWeight(FontWeight::Normal);

    // The sloping edges' labels and titles stick out sideways; a base
    // corner's title is centred on the corner.
    const double side = std::max(band + edgeTitle, cornerWidth * 0.5) + 6.0;
    request.Reserve(ChartAxisEdge::Left, side);
    request.Reserve(ChartAxisEdge::Right, side);
    request.Reserve(ChartAxisEdge::Top, band + cornerHeight + 6.0 + SubtitleHeight(ctx));
    request.Reserve(ChartAxisEdge::Bottom, band + std::max(edgeTitle, cornerHeight + 6.0) + 6.0);
}

void UltraCanvasTernaryChart::RenderEngineTitle(IRenderContext* ctx) {
    UltraCanvasChartEngineElement::RenderEngineTitle(ctx);
    if (model.subtitle.empty()) return;
    double top = 4.0;
    if (!chartTitle.empty()) {
        ctx->SetFontSize(titleFontSize);
        top += ctx->GetTextLineHeight(chartTitle) + 2.0;
    }
    ctx->SetFontSize(axisFontSize);
    ctx->SetFontWeight(FontWeight::Normal);
    ctx->SetTextPaint(axisLabelColor);
    const Size2Di size = ctx->GetTextLineDimensions(model.subtitle);
    ctx->DrawText(model.subtitle, Point2Dd((GetWidth() - size.width) * 0.5, top));
}

void UltraCanvasTernaryChart::RenderEngineGrid(IRenderContext* ctx) {
    if (!showGridLines || !Projection()) return;
    ctx->PushState();
    ctx->SetStrokePaint(gridColor);
    ctx->SetStrokeWidth(1.0f);
    for (int corner = 0; corner < 3; ++corner) {
        // The line of equal share k runs between the two edges that meet at
        // this corner, parallel to the edge opposite it.
        const Point2Dd at = CornerPoint(corner);
        const Point2Dd p = CornerPoint((corner + 1) % 3), q = CornerPoint((corner + 2) % 3);
        for (double k : model.Ticks(corner)) {
            if (k <= 1e-9 || k >= 1.0 - 1e-9) continue;   // the edges themselves
            ctx->DrawLine(Lerp(p, at, k), Lerp(q, at, k));
        }
    }
    ctx->PopState();
}

UltraCanvasTernaryChart::Furniture UltraCanvasTernaryChart::LayoutFurniture(IRenderContext* ctx) const {
    Furniture f;
    if (!Projection()) return f;
    const Point2Dd corners[3] = {CornerPoint(0), CornerPoint(1), CornerPoint(2)};
    const Point2Dd centre((corners[0].x + corners[1].x + corners[2].x) / 3.0,
                          (corners[0].y + corners[1].y + corners[2].y) / 3.0);

    // Ticks and their labels, each tick along its own grid line.
    ctx->SetFontSize(axisFontSize);
    ctx->SetFontWeight(FontWeight::Normal);
    for (int corner = 0; corner < 3; ++corner) {
        const Point2Dd dir = TickDirection(corner);
        for (double k : model.Ticks(corner)) {
            TickLabel t;
            t.tick = TickPosition(corner, k);
            t.tickEnd = Point2Dd(t.tick.x + dir.x * tickLength, t.tick.y + dir.y * tickLength);
            t.text = TickText(k);
            const Size2Di size = ctx->GetTextLineDimensions(t.text);
            t.box = BoxBeyond(t.tickEnd, dir, tickLabelGap, size);
            f.band = std::max(f.band, tickLength + tickLabelGap + std::max(size.width, size.height));
            f.ticks.push_back(t);
        }
    }

    // Each labelled edge's title, along the edge and outside its labels, with
    // an arrow in the direction the share grows. Text never runs upside
    // down: an edge that grows right to left reads "<- Title".
    if (showEdgeTitles) {
        ctx->SetFontWeight(FontWeight::Bold);
        for (int corner = 0; corner < 3; ++corner) {
            const Point2Dd from = CornerPoint(model.LabelEdgeStart(corner));
            const Point2Dd to = CornerPoint(corner);
            EdgeTitleLayout e;
            e.angle = std::atan2(to.y - from.y, to.x - from.x);
            e.text = EdgeTitle(corner) + "  " + kArrowRight;
            if (std::cos(e.angle) < -1e-9) {
                e.angle += M_PI;
                e.text = std::string(kArrowLeft) + "  " + EdgeTitle(corner);
            }
            e.size = ctx->GetTextLineDimensions(e.text);
            const Point2Dd middle = Lerp(from, to, 0.5);
            const Point2Dd out = Unit(middle.x - centre.x, middle.y - centre.y);
            const double distance = f.band + 6.0 + e.size.height * 0.5;
            e.centre = Point2Dd(middle.x + out.x * distance, middle.y + out.y * distance);
            // The rotated text's bounding box, for the label plan.
            const double cw = std::abs(std::cos(e.angle)), sw = std::abs(std::sin(e.angle));
            const double bw = e.size.width * cw + e.size.height * sw;
            const double bh = e.size.width * sw + e.size.height * cw;
            e.bounds = Rect2Dd(e.centre.x - bw * 0.5, e.centre.y - bh * 0.5, bw, bh);
            f.edgeTitles.push_back(e);
        }
    }

    // The corner titles: what each corner is 100 % of.
    ctx->SetFontSize(cornerFontSize);
    ctx->SetFontWeight(FontWeight::Bold);
    for (int corner = 0; corner < 3; ++corner) {
        const std::string& title = model.axes[corner].title;
        if (title.empty()) continue;
        f.cornerTitles.emplace_back(title, CornerTitleBox(corner, ctx->GetTextLineDimensions(title), f.band));
    }
    ctx->SetFontWeight(FontWeight::Normal);
    return f;
}

void UltraCanvasTernaryChart::RenderEngineAxes(IRenderContext* ctx) {
    if (!Projection()) return;
    const Furniture f = LayoutFurniture(ctx);
    ctx->PushState();

    // The triangle.
    ctx->SetStrokePaint(axisLineColor);
    ctx->SetStrokeWidth(1.4f);
    ctx->DrawLinePath({CornerPoint(0), CornerPoint(1), CornerPoint(2)}, true);

    ctx->SetFontSize(axisFontSize);
    ctx->SetFontWeight(FontWeight::Normal);
    for (const TickLabel& t : f.ticks) {
        ctx->SetStrokePaint(axisTickColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(t.tick, t.tickEnd);
        ctx->SetTextPaint(axisLabelColor);
        ctx->DrawText(t.text, Point2Dd(t.box.x, t.box.y));
    }

    ctx->SetFontWeight(FontWeight::Bold);
    for (const EdgeTitleLayout& e : f.edgeTitles) {
        ctx->PushState();
        ctx->Translate(e.centre.x, e.centre.y);
        ctx->Rotate(e.angle);
        ctx->SetTextPaint(axisLabelColor);
        ctx->DrawText(e.text, Point2Dd(-e.size.width * 0.5, -e.size.height * 0.5));
        ctx->PopState();
    }

    ctx->SetFontSize(cornerFontSize);
    ctx->SetTextPaint(titleTextColor);
    for (const auto& corner : f.cornerTitles) {
        ctx->DrawText(corner.first, Point2Dd(corner.second.x, corner.second.y));
    }
    ctx->SetFontWeight(FontWeight::Normal);
    ctx->PopState();
}

void UltraCanvasTernaryChart::RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) {
    if (!frame.projection || frame.projection->Kind() != ChartProjectionKind::Ternary) return;
    const double progress = frame.animationProgress;
    const std::pair<size_t, size_t> hovered = GetHoveredPoint();
    const Color outline = frame.theme ? frame.theme->plotAreaColor : Colors::White;
    for (size_t s = 0; s < model.series.size(); ++s) {
        if (seriesHidden[s]) continue;
        const TernarySeries& series = model.series[s];
        const Color color = SeriesColor(s);
        for (size_t p = 0; p < series.points.size(); ++p) {
            double sa = 0.0, sb = 0.0, sc = 0.0;
            if (!series.points[p].Shares(sa, sb, sc)) continue;   // Validate() names it
            const Point2Dd at = frame.projection->ToScreen(Normalized(sa, sb, sc, progress));
            const bool hot = hovered.first == s && hovered.second == p;
            const double radius = series.markerRadius * (hot ? 1.5 : 1.0);
            DrawChartMarker(ctx, series.marker, at, radius, color, outline, 1.2f);
            const double r = std::max(6.0, radius + 3.0);
            AddHitRegion(Rect2Dd(at.x - r, at.y - r, r * 2.0, r * 2.0), RegionId(s, p),
                         BuildPointTooltip(s, p));
        }
    }
}

void UltraCanvasTernaryChart::CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) {
    if (!showPointLabels || !Frame().projection) return;
    // The furniture round the triangle is drawn by the chart itself; it
    // enters the plan as keep-out rectangles (drawn by no one) so the point
    // labels steer around it.
    const Furniture furniture = LayoutFurniture(ctx);
    for (const TickLabel& t : furniture.ticks) broker.AddObstacleRect(t.box);
    for (const EdgeTitleLayout& e : furniture.edgeTitles) broker.AddObstacleRect(e.bounds);
    for (const auto& c : furniture.cornerTitles) broker.AddObstacleRect(c.second);

    ctx->SetFontSize(axisFontSize);
    for (size_t s = 0; s < model.series.size(); ++s) {
        if (seriesHidden[s]) continue;
        const TernarySeries& series = model.series[s];
        for (size_t p = 0; p < series.points.size(); ++p) {
            const TernaryPoint& point = series.points[p];
            double sa = 0.0, sb = 0.0, sc = 0.0;
            if (point.label.empty() || !point.Shares(sa, sb, sc)) continue;
            ChartLabelRequest label;
            label.text = point.label;
            label.klass = ChartLabelClass::ValueLabel;
            label.anchor = Frame().projection->ToScreen(ChartNormalizedPoint(sb, sc));
            label.anchorRadius = series.markerRadius;
            const Size2Di size = ctx->GetTextLineDimensions(label.text);
            label.textSize = Size2Dd(size.width, size.height);
            label.allowSuppress = true;      // a crowded triangle thins its labels
            label.wantLeaderLine = true;     // a label pushed off its point points back to it
            label.userData = static_cast<size_t>(RegionId(s, p));
            broker.Add(label);
        }
    }
}

void UltraCanvasTernaryChart::RenderPlannedLabels(IRenderContext* ctx) {
    constexpr double kMaxLabelDistance = 40.0;
    ctx->SetFontSize(axisFontSize);
    ctx->SetFontWeight(FontWeight::Normal);
    for (const PlacedChartLabel& label : GetLabelPlan().labels) {
        if (label.suppressed || label.klass != ChartLabelClass::ValueLabel) continue;
        const Point2Dd nearest(std::clamp(label.leaderFrom.x, label.bounds.Left(), label.bounds.Right()),
                               std::clamp(label.leaderFrom.y, label.bounds.Top(), label.bounds.Bottom()));
        if (label.hasLeader) {
            if (std::hypot(nearest.x - label.leaderFrom.x, nearest.y - label.leaderFrom.y) > kMaxLabelDistance) continue;
            ctx->SetStrokePaint(Color(150, 150, 150, 180));
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine(label.leaderFrom, nearest);
        }
        ctx->SetTextPaint(axisLabelColor);
        ctx->DrawText(label.text, label.bounds.TopLeft());
    }
}

bool UltraCanvasTernaryChart::OnEvent(const UCEvent& event) {
    const bool handled = UltraCanvasChartEngineElement::OnEvent(event);
    if (!handled && event.type == UCEventType::MouseDown && event.button == UCMouseButton::Left &&
        onPointClick) {
        const std::pair<size_t, size_t> hit = GetHoveredPoint();
        if (hit.first != SIZE_MAX) {
            onPointClick(hit.first, hit.second);
            return true;
        }
    }
    return handled;
}

} // namespace UltraCanvas

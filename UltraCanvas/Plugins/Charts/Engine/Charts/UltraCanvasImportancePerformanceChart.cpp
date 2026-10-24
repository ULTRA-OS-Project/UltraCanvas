// Plugins/Charts/Engine/Charts/UltraCanvasImportancePerformanceChart.cpp
// The importance-performance chart on the chart engine.
// Version: 1.0.0
// Last Modified: 2026-10-24
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasImportancePerformanceChart.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {

namespace {

bool Finite(double v) { return std::isfinite(v); }

// Zone hit regions sit far above the scatter chart's point ids
// (group * 1'000'000 + index), so the two can never collide.
constexpr int64_t kZoneRegionBase = 4000000000LL;

int64_t PointRegionId(size_t group, size_t index) {
    return static_cast<int64_t>(group) * 1000000 + static_cast<int64_t>(index);
}

std::string DisplayNumber(double v) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f", v);   // locale-ok: tooltip text a person reads
    return text;
}

bool SameRect(const Rect2Dd& a, const Rect2Dd& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

// Sutherland-Hodgman against the plot rectangle. The zones run past the plot
// (so smoothing never rounds a visible corner), and unclipped hit regions
// would answer a pointer over the axis labels.
std::vector<Point2Dd> ClipToRect(const std::vector<Point2Dd>& polygon, const Rect2Dd& r) {
    std::vector<Point2Dd> out = polygon;
    auto clipEdge = [&out](auto inside, auto cross) {
        if (out.empty()) return;
        std::vector<Point2Dd> next;
        next.reserve(out.size() + 4);
        for (size_t i = 0; i < out.size(); ++i) {
            const Point2Dd& a = out[i];
            const Point2Dd& b = out[(i + 1) % out.size()];
            const bool ia = inside(a), ib = inside(b);
            if (ia) next.push_back(a);
            if (ia != ib) next.push_back(cross(a, b));
        }
        out = std::move(next);
    };
    auto atX = [](const Point2Dd& a, const Point2Dd& b, double x) {
        const double t = (x - a.x) / (b.x - a.x);
        return Point2Dd(x, a.y + (b.y - a.y) * t);
    };
    auto atY = [](const Point2Dd& a, const Point2Dd& b, double y) {
        const double t = (y - a.y) / (b.y - a.y);
        return Point2Dd(a.x + (b.x - a.x) * t, y);
    };
    const double x0 = r.x, x1 = r.x + r.width, y0 = r.y, y1 = r.y + r.height;
    clipEdge([&](const Point2Dd& p) { return p.x >= x0; }, [&](const Point2Dd& a, const Point2Dd& b) { return atX(a, b, x0); });
    clipEdge([&](const Point2Dd& p) { return p.x <= x1; }, [&](const Point2Dd& a, const Point2Dd& b) { return atX(a, b, x1); });
    clipEdge([&](const Point2Dd& p) { return p.y >= y0; }, [&](const Point2Dd& a, const Point2Dd& b) { return atY(a, b, y0); });
    clipEdge([&](const Point2Dd& p) { return p.y <= y1; }, [&](const Point2Dd& a, const Point2Dd& b) { return atY(a, b, y1); });
    return out;
}

} // namespace

UltraCanvasImportancePerformanceChart::UltraCanvasImportancePerformanceChart(
        const std::string& id, int x, int y, int w, int h)
    : UltraCanvasEngineScatterChart(id, x, y, w, h) {
    groupColors = {Color(28, 31, 40, 255), Color(74, 26, 120, 255), Color(12, 58, 130, 255),
                   Color(92, 44, 12, 255)};
    groupShapes = {ChartMarkerShape::Circle, ChartMarkerShape::Square, ChartMarkerShape::Diamond,
                   ChartMarkerShape::TriangleUp};
    SetMarkerRadius(6.0f);
    SetMarkerOpacity(1.0f);
    SetMarkerOutline(Color(255, 255, 255, 235), 1.5f);
    SetPointLabelMode(EngineScatterLabelMode::Outside);
}

// =============================================================================
// DATA
// =============================================================================

void UltraCanvasImportancePerformanceChart::SetModel(const ImportancePerformanceModel& newModel) {
    model = newModel;
    factorZones = model.ClassifyFactors();
    ++modelGeneration;
    SetXRange(model.importance.min, model.importance.max);
    SetYRange(model.performance.min, model.performance.max);
    SetAxisTitles(model.importance.title, model.performance.title);
    SetChartTitle(model.title);
    RebuildGroups();     // SetGroups rebuilds the highlights, the zones with them
}

void UltraCanvasImportancePerformanceChart::RebuildGroups() {
    groupNames.clear();
    factorSlots.clear();
    std::vector<EngineScatterGroup> groups;
    for (const ImportancePerformanceFactor& f : model.factors) {
        auto it = std::find(groupNames.begin(), groupNames.end(), f.group);
        size_t g = static_cast<size_t>(it - groupNames.begin());
        if (it == groupNames.end()) {
            groupNames.push_back(f.group);
            EngineScatterGroup group;
            group.name = f.group;
            if (!groupColors.empty()) group.color = groupColors[g % groupColors.size()];
            if (!groupShapes.empty()) group.marker = groupShapes[g % groupShapes.size()];
            groups.push_back(group);
        }
        EngineScatterPoint point(f.importance, f.performance);
        point.label = f.label;
        factorSlots.emplace_back(g, groups[g].points.size());
        groups[g].points.push_back(point);
    }
    groupHidden.assign(groups.size(), false);
    SetGroups(groups);
}

void UltraCanvasImportancePerformanceChart::SetGroupStyles(const std::vector<Color>& colors,
                                                           const std::vector<ChartMarkerShape>& shapes) {
    groupColors = colors;
    groupShapes = shapes;
    RebuildGroups();
}

// =============================================================================
// OPTIONS
// =============================================================================

void UltraCanvasImportancePerformanceChart::SetShowZoneTitles(bool show) {
    showZoneTitles = show;
    ++modelGeneration;
    MarkEngineDirty(ChartDirty::Style);      // titles are label-plan obstacles
}

void UltraCanvasImportancePerformanceChart::SetShowZoneIcons(bool show) {
    showZoneIcons = show;
    ++modelGeneration;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasImportancePerformanceChart::SetShowBandLabels(bool show) {
    showBandLabels = show;
    ++modelGeneration;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasImportancePerformanceChart::SetShowLevelRanges(bool show) {
    showLevelRanges = show;
    MarkEngineDirty(ChartDirty::Data);       // the ticks are described with the axes
}

void UltraCanvasImportancePerformanceChart::SetShowGrid(bool show) {
    showGrid = show;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasImportancePerformanceChart::SetZoneTitleFontSize(float size) {
    zoneTitleFontSize = std::clamp(size, 8.0f, 40.0f);
    ++modelGeneration;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasImportancePerformanceChart::SetFactorLabelBackground(const Color& color) {
    factorLabelBackground = color;
    RequestRedraw();
}

// =============================================================================
// HELPERS
// =============================================================================

Color UltraCanvasImportancePerformanceChart::ZoneFill(size_t zoneIndex) const {
    const ImportancePerformanceZone& zone = model.zones[zoneIndex];
    if (zone.fill.a > 0) return zone.fill;
    return Palette().ColorAt(zoneIndex, model.zones.size());
}

Point2Dd UltraCanvasImportancePerformanceChart::ToScreen(const ChartEngineFrame& frame,
                                                         const Point2Dd& value) const {
    const ChartAxis& y = frame.axes->At(0);
    const ChartAxis& x = frame.axes->At(1);
    return frame.projection->ToScreen(ChartNormalizedPoint(x.Normalize(value.x), y.Normalize(value.y)));
}

std::vector<Point2Dd> UltraCanvasImportancePerformanceChart::ScreenShape(
        const ChartEngineFrame& frame, const std::vector<Point2Dd>& value) const {
    std::vector<Point2Dd> screen;
    screen.reserve(value.size());
    for (const Point2Dd& p : value) screen.push_back(ToScreen(frame, p));
    return screen;
}

double UltraCanvasImportancePerformanceChart::BandThickness(const ChartEngineFrame& frame,
                                                            const ImportancePerformanceBand& band) const {
    const double xMid = (model.importance.min + model.importance.max) / 2.0;
    const Point2Dd a = ToScreen(frame, Point2Dd(xMid, model.performance.min));
    const Point2Dd b = ToScreen(frame, Point2Dd(xMid, model.performance.min + band.width));
    return std::max(1.0, std::hypot(b.x - a.x, b.y - a.y));
}

std::string UltraCanvasImportancePerformanceChart::LevelText(const ImportancePerformanceAxis& axis,
                                                             double value) const {
    std::string text = DisplayNumber(value);
    const int level = ImportancePerformanceModel::LevelOf(axis, value);
    if (level >= 0) text += " (" + axis.levels[static_cast<size_t>(level)].label + ")";
    return text;
}

std::string UltraCanvasImportancePerformanceChart::FactorTooltip(size_t k) const {
    const ImportancePerformanceFactor& f = model.factors[k];
    std::string text = f.label;
    if (groupNames.size() > 1 && !f.group.empty()) text += "\n" + f.group;
    text += "\n" + (model.importance.title.empty() ? std::string("Importance") : model.importance.title) +
            ": " + LevelText(model.importance, f.importance);
    text += "\n" + (model.performance.title.empty() ? std::string("Performance") : model.performance.title) +
            ": " + LevelText(model.performance, f.performance);
    const int zone = k < factorZones.size() ? factorZones[k] : -1;
    if (zone >= 0) {
        const ImportancePerformanceZone& z = model.zones[static_cast<size_t>(zone)];
        text += "\nZone: " + z.title;
        if (!z.action.empty()) text += "\n" + z.action;
    }
    if (!f.note.empty()) text += "\n" + f.note;
    return text;
}

std::string UltraCanvasImportancePerformanceChart::ZoneTooltip(size_t zoneIndex) const {
    const ImportancePerformanceZone& zone = model.zones[zoneIndex];
    std::string text = zone.title;
    if (!zone.action.empty()) text += "\n" + zone.action;
    if (!model.factors.empty()) {
        const long count = std::count(factorZones.begin(), factorZones.end(), static_cast<int>(zoneIndex));
        text += "\n" + std::to_string(count) + (count == 1 ? " factor" : " factors");
    }
    return text;
}

void UltraCanvasImportancePerformanceChart::ConfigureAxis(ChartAxis& axis,
                                                          const ImportancePerformanceAxis& spec) const {
    axis.inverted = spec.inverted;
    axis.rangePadding = 0.0;
    axis.SetRange(spec.min, spec.max);
    if (!showLevelRanges) return;

    // Level names at their centres, empty-labelled ticks at the boundaries
    // between them (the range marks), the captions at the screen's ends.
    struct Named { double value; std::string text; };
    std::vector<Named> named;
    const double lowEnd = spec.inverted ? spec.max : spec.min;
    const double highEnd = spec.inverted ? spec.min : spec.max;
    named.push_back({lowEnd, spec.lowCaption});
    named.push_back({highEnd, spec.highCaption});
    const double epsilon = (spec.max - spec.min) * 1e-6;
    auto known = [&](double v) {
        return std::any_of(named.begin(), named.end(),
                           [&](const Named& n) { return std::abs(n.value - v) <= epsilon; });
    };
    for (const ImportancePerformanceLevel& level : spec.levels) {
        if (!known(level.Centre())) named.push_back({level.Centre(), level.label});
    }
    for (const ImportancePerformanceLevel& level : spec.levels) {
        for (double edge : {level.from, level.to}) {
            if (edge > spec.min + epsilon && edge < spec.max - epsilon && !known(edge)) {
                named.push_back({edge, std::string()});
            }
        }
    }
    std::sort(named.begin(), named.end(), [](const Named& a, const Named& b) { return a.value < b.value; });
    axis.tickValues.clear();
    for (const Named& n : named) axis.tickValues.push_back(n.value);
    axis.formatter = [named, epsilon](double v) {
        for (const Named& n : named) {
            if (std::abs(n.value - v) <= epsilon) return n.text;
        }
        return DisplayNumber(v);
    };
}

// =============================================================================
// LAYOUT OF THE DECORATION
// =============================================================================

const UltraCanvasImportancePerformanceChart::Decor&
UltraCanvasImportancePerformanceChart::Layout(IRenderContext* ctx, const ChartEngineFrame& frame) {
    if (decor.modelGeneration == modelGeneration && SameRect(decor.plot, frame.plotArea)) return decor;
    decor = Decor();
    decor.plot = frame.plotArea;
    decor.modelGeneration = modelGeneration;
    if (!frame.axes || frame.axes->Count() < 2 || !frame.projection) return decor;

    // ---- zone titles: at each zone's pole, in the part left visible --------
    std::vector<std::vector<Point2Dd>> screens;
    for (size_t i = 0; i < model.zones.size(); ++i) screens.push_back(ScreenShape(frame, model.ZoneShape(i)));
    decor.zones.resize(model.zones.size());
    // The marks a title should not cover: the visible factors.
    std::vector<Point2Dd> marks;
    for (size_t k = 0; k < model.factors.size() && k < factorSlots.size(); ++k) {
        const ImportancePerformanceFactor& f = model.factors[k];
        const size_t group = factorSlots[k].first;
        if ((group < groupHidden.size() && groupHidden[group]) ||
            !Finite(f.importance) || !Finite(f.performance)) continue;
        marks.push_back(ToScreen(frame, Point2Dd(f.importance, f.performance)));
    }
    ctx->SetFontSize(zoneTitleFontSize);
    ctx->SetFontWeight(FontWeight::Bold);
    for (size_t i = 0; i < model.zones.size() && showZoneTitles; ++i) {
        const ImportancePerformanceZone& zone = model.zones[i];
        if (zone.title.empty() || screens[i].size() < 3) continue;
        const std::vector<std::vector<Point2Dd>> holes(screens.begin() + static_cast<long>(i) + 1, screens.end());
        const Size2Di text = ctx->GetTextLineDimensions(zone.title);
        auto fits = [&](double radius, double icon) {
            const double height = text.height + (icon > 0.0 ? icon + 4.0 : 0.0);
            return text.width * 0.5 <= radius * 1.6 && height * 0.5 <= radius;
        };
        // Clear of the factors first; where they crowd the zone too much for
        // that, the zone's own pole, and the factors' names steer around it.
        double radius = 0.0;
        Point2Dd pole = PoleOfInaccessibility(screens[i], holes, frame.plotArea, &radius, marks);
        if (!fits(radius, 0.0)) pole = PoleOfInaccessibility(screens[i], holes, frame.plotArea, &radius);
        if (radius <= 0.0) continue;

        const double iconWanted = (showZoneIcons && !zone.iconPath.empty()) ? std::min(36.0, radius * 0.7) : 0.0;
        double icon = iconWanted >= 16.0 ? iconWanted : 0.0;
        if (icon > 0.0 && !fits(radius, icon)) icon = 0.0;
        if (!fits(radius, icon)) continue;

        ZoneDecor& d = decor.zones[i];
        d.showTitle = true;
        d.showIcon = icon > 0.0;
        // The fit allows a title wider than the clear circle; keep it inside the plot.
        const double halfWidth = std::max<double>(text.width, icon) * 0.5 + 4.0;
        pole.x = std::clamp(pole.x, frame.plotArea.x + halfWidth,
                            std::max(frame.plotArea.x + halfWidth, frame.plotArea.x + frame.plotArea.width - halfWidth));
        d.centre = pole;
        const double height = text.height + (d.showIcon ? icon + 4.0 : 0.0);
        const double top = pole.y - height * 0.5;
        if (d.showIcon) d.iconBox = Rect2Dd(pole.x - icon * 0.5, top, icon, icon);
        d.titleBox = Rect2Dd(pole.x - text.width * 0.5, top + (d.showIcon ? icon + 4.0 : 0.0),
                             text.width, text.height);
    }

    // ---- band labels: along the longest straight run inside the plot -------
    decor.bands.resize(model.bands.size());
    const Rect2Dd inner(frame.plotArea.x + 2.0, frame.plotArea.y + 2.0,
                        frame.plotArea.width - 4.0, frame.plotArea.height - 4.0);
    auto insidePlot = [&](const Point2Dd& p) {
        return p.x >= inner.x && p.y >= inner.y && p.x <= inner.x + inner.width && p.y <= inner.y + inner.height;
    };
    for (size_t b = 0; b < model.bands.size() && showBandLabels; ++b) {
        const ImportancePerformanceBand& band = model.bands[b];
        if (band.label.empty()) continue;
        const std::vector<Point2Dd> line = ScreenShape(frame, model.BandShape(b));
        if (line.size() < 2) continue;

        double bestLength = 0.0;
        Point2Dd bestFrom, bestTo;
        size_t k = 0;
        while (k + 1 < line.size()) {
            if (!insidePlot(line[k]) || !insidePlot(line[k + 1])) { ++k; continue; }
            const double heading = std::atan2(line[k + 1].y - line[k].y, line[k + 1].x - line[k].x);
            size_t end = k + 1;
            while (end + 1 < line.size() && insidePlot(line[end + 1])) {
                const double next = std::atan2(line[end + 1].y - line[end].y, line[end + 1].x - line[end].x);
                double turn = std::abs(next - heading);
                if (turn > M_PI) turn = 2.0 * M_PI - turn;
                if (turn > 0.2) break;          // about 11 degrees off the run's heading
                ++end;
            }
            const double length = std::hypot(line[end].x - line[k].x, line[end].y - line[k].y);
            if (length > bestLength) { bestLength = length; bestFrom = line[k]; bestTo = line[end]; }
            k = end;
        }

        BandDecor& d = decor.bands[b];
        const double thickness = BandThickness(frame, band);
        d.inside = thickness >= 12.0;
        d.fontSize = d.inside ? static_cast<float>(std::clamp(thickness * 0.62, 9.0, 13.0)) : 11.0f;
        ctx->SetFontSize(d.fontSize);
        const Size2Di text = ctx->GetTextLineDimensions(band.label);
        if (bestLength < text.width + 20.0) continue;

        double angle = std::atan2(bestTo.y - bestFrom.y, bestTo.x - bestFrom.x);
        if (angle > M_PI / 2.0) angle -= M_PI;
        if (angle < -M_PI / 2.0) angle += M_PI;
        Point2Dd centre((bestFrom.x + bestTo.x) * 0.5, (bestFrom.y + bestTo.y) * 0.5);
        if (!d.inside) {
            // A thin band is a line: the text runs beside it, on its upper side.
            const double offset = thickness * 0.5 + 3.0 + text.height * 0.5;
            centre = Point2Dd(centre.x + std::sin(angle) * offset, centre.y - std::cos(angle) * offset);
        }
        d.show = true;
        d.centre = centre;
        d.angle = angle;
        d.textSize = Size2Dd(text.width, text.height);
        const double bw = std::abs(text.width * std::cos(angle)) + std::abs(text.height * std::sin(angle));
        const double bh = std::abs(text.width * std::sin(angle)) + std::abs(text.height * std::cos(angle));
        d.bounds = Rect2Dd(centre.x - bw * 0.5, centre.y - bh * 0.5, bw, bh);
    }
    ctx->SetFontWeight(FontWeight::Normal);
    return decor;
}

// =============================================================================
// ENGINE CONTRACT
// =============================================================================

void UltraCanvasImportancePerformanceChart::AddChartHighlights() {
    for (size_t i = 0; i < model.zones.size(); ++i) {
        const std::vector<Point2Dd> shape = model.ZoneShape(i);
        if (shape.size() < 3) continue;
        ChartHighlight h;
        h.shape = ChartHighlightShape::Polygon;
        h.members = shape;            // already smoothed: exactly what ZoneOf tests
        h.smoothing = 0.0;
        h.fill = ZoneFill(i);
        h.strokeWidth = 0.0f;
        h.xAxisIndex = 1;
        h.yAxisIndex = 0;
        h.zSlot = 200;                // under the grid and the marks
        AddHighlight(h);
    }
}

void UltraCanvasImportancePerformanceChart::DescribeAxes(ChartAxisSet& axes) {
    UltraCanvasEngineScatterChart::DescribeAxes(axes);
    if (axes.Count() < 2) return;
    ConfigureAxis(axes.At(0), model.performance);
    ConfigureAxis(axes.At(1), model.importance);
    SetGridAxis(showGrid ? 0 : static_cast<size_t>(-1));
    SetDomainGridAxis(showGrid ? 1 : static_cast<size_t>(-1));
}

void UltraCanvasImportancePerformanceChart::RenderChartContent(IRenderContext* ctx,
                                                               const ChartEngineFrame& frame) {
    if (!frame.axes || frame.axes->Count() < 2 || !frame.projection) return;
    const Decor& d = Layout(ctx, frame);

    // ---- zones answer hover too; the points added later win over them ------
    for (size_t i = 0; i < model.zones.size(); ++i) {
        const std::vector<Point2Dd> screen = ClipToRect(ScreenShape(frame, model.ZoneShape(i)), frame.plotArea);
        if (screen.size() >= 3) AddHitRegion(screen, kZoneRegionBase + static_cast<int64_t>(i), ZoneTooltip(i));
    }

    // ---- bands: thick round-capped strokes over the zones -------------------
    ctx->PushState();
    ctx->SetLineCap(LineCap::Round);
    ctx->SetLineJoin(LineJoin::Round);
    for (size_t b = 0; b < model.bands.size(); ++b) {
        const std::vector<Point2Dd> line = ScreenShape(frame, model.BandShape(b));
        if (line.size() < 2) continue;
        ctx->SetStrokePaint(model.bands[b].fill);
        ctx->SetStrokeWidth(static_cast<float>(BandThickness(frame, model.bands[b])));
        ctx->DrawLinePath(line, false);
    }
    ctx->PopState();

    // ---- zone icons and titles -----------------------------------------------
    ctx->SetFontSize(zoneTitleFontSize);
    ctx->SetFontWeight(FontWeight::Bold);
    for (size_t i = 0; i < d.zones.size(); ++i) {
        const ZoneDecor& z = d.zones[i];
        if (!z.showTitle) continue;
        if (z.showIcon) ctx->DrawImage(model.zones[i].iconPath, z.iconBox, ImageFitMode::Contain);
        ctx->SetTextPaint(model.zones[i].textColor);
        ctx->DrawText(model.zones[i].title, z.titleBox.TopLeft());
    }

    // ---- band labels, turned along their band --------------------------------
    for (size_t b = 0; b < d.bands.size(); ++b) {
        const BandDecor& band = d.bands[b];
        if (!band.show) continue;
        ctx->PushState();
        ctx->SetFontSize(band.fontSize);
        ctx->SetTextPaint(band.inside ? model.bands[b].textColor : model.bands[b].fill.Darken(0.25f));
        ctx->Translate(band.centre.x, band.centre.y);
        ctx->Rotate(band.angle);
        ctx->DrawText(model.bands[b].label, Point2Dd(-band.textSize.width * 0.5, -band.textSize.height * 0.5));
        ctx->PopState();
    }
    ctx->SetFontWeight(FontWeight::Normal);

    // ---- the factors: the scatter chart's marks ------------------------------
    UltraCanvasEngineScatterChart::RenderChartContent(ctx, frame);

    // The same regions again, last so they win, with the zone in the tooltip.
    for (size_t k = 0; k < model.factors.size() && k < factorSlots.size(); ++k) {
        const ImportancePerformanceFactor& f = model.factors[k];
        const auto [group, index] = factorSlots[k];
        if (group < groupHidden.size() && groupHidden[group]) continue;
        if (!Finite(f.importance) || !Finite(f.performance)) continue;
        const Point2Dd p = ToScreen(frame, Point2Dd(f.importance, f.performance));
        const double r = 8.0;
        AddHitRegion(Rect2Dd(p.x - r, p.y - r, r * 2.0, r * 2.0), PointRegionId(group, index), FactorTooltip(k));
    }
}

void UltraCanvasImportancePerformanceChart::CollectChartLabels(IRenderContext* ctx,
                                                               ChartLabelBroker& broker) {
    UltraCanvasEngineScatterChart::CollectChartLabels(ctx, broker);
    const ChartEngineFrame& frame = Frame();
    if (!frame.axes || frame.axes->Count() < 2 || !frame.projection) return;
    // Factor names steer around the zone titles and the band text.
    const Decor& d = Layout(ctx, frame);
    for (const ZoneDecor& z : d.zones) {
        if (!z.showTitle) continue;
        broker.AddObstacleRect(z.titleBox);
        if (z.showIcon) broker.AddObstacleRect(z.iconBox);
    }
    for (const BandDecor& band : d.bands) {
        if (band.show) broker.AddObstacleRect(band.bounds);
    }
    ctx->SetFontSize(axisFontSize);
}

void UltraCanvasImportancePerformanceChart::RenderPlannedLabels(IRenderContext* ctx) {
    if (factorLabelBackground.a > 0) {
        ctx->SetFillPaint(factorLabelBackground);
        for (const PlacedChartLabel& label : GetLabelPlan().labels) {
            if (label.suppressed || label.klass != ChartLabelClass::ValueLabel || label.rotationDegrees != 0.0) continue;
            const Rect2Dd box(label.bounds.x - 3.0, label.bounds.y - 1.0,
                              label.bounds.width + 6.0, label.bounds.height + 2.0);
            ctx->FillRoundedRectangle(box, 3.0);
        }
    }
    UltraCanvasEngineScatterChart::RenderPlannedLabels(ctx);
}

void UltraCanvasImportancePerformanceChart::OnLegendEntryToggled(size_t entryIndex, bool enabled) {
    UltraCanvasEngineScatterChart::OnLegendEntryToggled(entryIndex, enabled);
    if (entryIndex < groupHidden.size()) groupHidden[entryIndex] = !enabled;
}

} // namespace UltraCanvas

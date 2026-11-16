// Plugins/Charts/Engine/Charts/UltraCanvasSpiralChart.cpp
// A spiral plot on the chart engine.
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasSpiralChart.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kArcStep = 3.0 * kPi / 180.0;     // a point at least every 3 degrees along an arc
constexpr double kColorBarHeight = 46.0;

std::string Fixed(double v, int decimals) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.*f", std::clamp(decimals, 0, 6), v);   // locale-ok: display text
    return buffer;
}

} // namespace

UltraCanvasSpiralChart::UltraCanvasSpiralChart(const std::string& id, int x, int y, int w, int h)
    : UltraCanvasChartEngineElement(id, x, y, w, h) {
    SetShowLegend(false);
}

// =============================================================================
// SETTINGS
// =============================================================================

void UltraCanvasSpiralChart::SetModel(const UltraCanvasSpiralModel& m) {
    model = m;
    SetChartTitle(model.title);
    hoveredSample = -1;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasSpiralChart::SetPeriod(double period) {
    if (!(period > 0.0)) return;
    model.period = period;
    hoveredSample = -1;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasSpiralChart::SetDisplay(SpiralDisplay d) {
    display = d;
    MarkEngineDirty(ChartDirty::Geometry);     // the colour bar comes and goes
}

void UltraCanvasSpiralChart::SetColormap(HeatmapColormap map, bool reversed) {
    colormap = map;
    colormapReversed = reversed;
    RequestRedraw();
}

void UltraCanvasSpiralChart::SetColor(const Color& c) {
    color = c;
    RequestRedraw();
}

void UltraCanvasSpiralChart::SetRibbonWidth(float share) {
    ribbonWidth = std::clamp(share, 0.2f, 1.0f);
    RequestRedraw();
}

void UltraCanvasSpiralChart::SetShowGuide(bool show) {
    showGuide = show;
    RequestRedraw();
}

void UltraCanvasSpiralChart::SetShowColorBar(bool show) {
    showColorBar = show;
    MarkEngineDirty(ChartDirty::Geometry);
}

void UltraCanvasSpiralChart::SetTimeFormatter(ValueFormatter formatter) {
    timeFormatter = std::move(formatter);
    RequestRedraw();
}

void UltraCanvasSpiralChart::SetValueDecimals(int decimals) {
    valueDecimals = decimals;
    RequestRedraw();
}

// =============================================================================
// READING
// =============================================================================

Color UltraCanvasSpiralChart::MainColor() const {
    return color.a > 0 ? color.WithAlpha(255) : Palette().ColorAt(0).WithAlpha(255);
}

Color UltraCanvasSpiralChart::ValueColor(double value) const {
    const double t = model.Normalize(value);
    if (!std::isfinite(t)) return GetTheme().gridColor;
    return SampleColormap(colormap, {}, t, colormapReversed).WithAlpha(255);
}

std::string UltraCanvasSpiralChart::FormatTime(double t) const {
    if (timeFormatter) return timeFormatter(t);
    return Fixed(t, t == std::floor(t) ? 0 : 2);
}

std::string UltraCanvasSpiralChart::FormatValue(double v) const {
    return std::isfinite(v) ? Fixed(v, valueDecimals) : std::string("no value");
}

std::string UltraCanvasSpiralChart::DescribeSample(size_t index) const {
    if (index >= model.samples.size()) return std::string();
    const SpiralSample& s = model.samples[index];
    std::string text = FormatTime(s.t) + "\n" + model.valueName + ": " + FormatValue(s.value);
    if (!s.note.empty()) text += "\n" + s.note;
    return text;
}

Point2Dd UltraCanvasSpiralChart::ScreenPoint(double t, double offset) const {
    const SpiralPosition p = model.Position(t, offset);
    return Point2Dd(centre.x + outerRadius * p.radius * std::cos(p.angle),
                    centre.y - outerRadius * p.radius * std::sin(p.angle));
}

std::vector<Point2Dd> UltraCanvasSpiralChart::Piece(double t0, double t1, double inner, double outer) const {
    // Sampled along its arc, so a long piece still curves.
    const double turns = std::abs(model.TurnsOf(t1) - model.TurnsOf(t0));
    const int steps = std::max(2, static_cast<int>(std::ceil(turns * 2.0 * kPi / kArcStep)) + 1);
    std::vector<Point2Dd> polygon;
    polygon.reserve(static_cast<size_t>(steps) * 2);
    for (int k = 0; k < steps; ++k) polygon.push_back(ScreenPoint(t0 + (t1 - t0) * k / (steps - 1), outer));
    for (int k = steps - 1; k >= 0; --k) polygon.push_back(ScreenPoint(t0 + (t1 - t0) * k / (steps - 1), inner));
    return polygon;
}

void UltraCanvasSpiralChart::OnHitRegionHoverChanged(int64_t regionId) {
    const int index = regionId >= 0 ? static_cast<int>(regionId) : -1;
    if (index == hoveredSample) return;
    hoveredSample = index;
    if (onSampleHover) onSampleHover(index);
}

// =============================================================================
// ENGINE CONTRACT
// =============================================================================

void UltraCanvasSpiralChart::DescribeAxes(ChartAxisSet& axes) {
    // Two hidden axes: the spiral is the chart's own furniture.
    ChartAxis u("u");
    u.side = ChartAxisSide::Bottom;
    u.visible = false;
    u.SetRange(0.0, 1.0);
    axes.Add(u);
    ChartAxis v("v");
    v.side = ChartAxisSide::Left;
    v.visible = false;
    v.SetRange(0.0, 1.0);
    axes.Add(v);
    SetGridAxis(static_cast<size_t>(-1));
    SetDomainGridAxis(static_cast<size_t>(-1));
}

double UltraCanvasSpiralChart::SubtitleHeight(IRenderContext* ctx) const {
    if (model.subtitle.empty()) return 0.0;
    ctx->SetFontSize(axisFontSize);
    return ctx->GetTextLineHeight(model.subtitle) + 4.0;
}

void UltraCanvasSpiralChart::MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) {
    request.Reserve(ChartAxisEdge::Top, SubtitleHeight(ctx) + 6.0);
    request.Reserve(ChartAxisEdge::Bottom, ColorBarShown() ? kColorBarHeight : 8.0);
    request.Reserve(ChartAxisEdge::Left, 8.0);
    request.Reserve(ChartAxisEdge::Right, 8.0);
}

void UltraCanvasSpiralChart::RenderEngineTitle(IRenderContext* ctx) {
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

void UltraCanvasSpiralChart::RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) {
    const Rect2Dd& plot = frame.plotArea;
    const int sectors = static_cast<int>(model.sectorLabels.size());

    // The spiral fills the plot's largest centred circle, less a band for the
    // sector labels around it.
    ctx->SetFontSize(axisFontSize);
    ctx->SetFontWeight(FontWeight::Normal);
    double labelBand = 6.0;
    for (const std::string& label : model.sectorLabels) {
        const Size2Di size = ctx->GetTextLineDimensions(label);
        labelBand = std::max(labelBand, std::max<double>(size.width, size.height) + 10.0);
    }
    centre = Point2Dd(plot.x + plot.width * 0.5, plot.y + plot.height * 0.5);
    outerRadius = std::max(10.0, std::min(plot.width, plot.height) * 0.5 - labelBand);
    if (model.samples.empty() || !(model.period > 0.0)) return;

    const double start = model.Start();
    const int turns = model.TurnCount();
    const double end = start + turns * model.period;
    const double progress = frame.animationProgress;

    // ---- guide: the rings' inner edge as one spiral, and the spokes --------
    if (showGuide) {
        ctx->SetStrokePaint(GetTheme().gridColor);
        ctx->SetStrokeWidth(1.0f);
        std::vector<Point2Dd> guide;
        const int steps = static_cast<int>(std::ceil((turns + 1) * 2.0 * kPi / kArcStep));
        for (int k = 0; k <= steps; ++k) guide.push_back(ScreenPoint(start + (turns + 1.0) * model.period * k / steps, -0.5));
        ctx->DrawLinePath(guide, false);
        for (int k = 0; k < std::max(sectors, 1); ++k) {
            const SpiralPosition p = model.Position(start + model.period * k / std::max(sectors, 1));
            const double inner = std::clamp(model.innerRadius, 0.0, 0.6);
            ctx->DrawLine(Point2Dd(centre.x + outerRadius * inner * std::cos(p.angle), centre.y - outerRadius * inner * std::sin(p.angle)),
                          Point2Dd(centre.x + outerRadius * std::cos(p.angle), centre.y - outerRadius * std::sin(p.angle)));
        }
    }

    // ---- the data ------------------------------------------------------------
    const size_t n = model.samples.size();
    const size_t shown = static_cast<size_t>(std::ceil(n * progress));     // the spiral draws itself in
    if (display == SpiralDisplay::Ribbon) {
        const double half = ribbonWidth * 0.5;
        for (size_t i = 0; i < shown; ++i) {
            const SpiralSample& s = model.samples[i];
            if (!std::isfinite(s.value)) continue;
            const std::vector<Point2Dd> piece = Piece(s.t, s.t + model.SampleSpan(i), -half, half);
            const Color c = ValueColor(s.value);
            ctx->SetFillPaint(c);
            ctx->FillLinePath(piece);
            // A hairline in the same colour closes the seams between pieces.
            ctx->SetStrokePaint(c);
            ctx->SetStrokeWidth(0.6f);
            ctx->DrawLinePath(piece, true);
        }
    } else {
        // Band and Line: one path per run of samples without a gap, the
        // value as the distance out from the ring's inner edge.
        const Color main = MainColor();
        size_t i = 0;
        while (i < shown) {
            if (!std::isfinite(model.samples[i].value)) { ++i; continue; }
            std::vector<double> ts, heights;
            size_t j = i;
            for (; j < shown && std::isfinite(model.samples[j].value); ++j) {
                const double h = model.Normalize(model.samples[j].value) * 0.95;
                const double span = model.SampleSpan(j);
                const double next = j + 1 < n ? model.samples[j + 1].t : model.samples[j].t + span;
                // Points in between along the arc, so the edge curves with the ring.
                const double turnsAcross = std::abs(model.TurnsOf(model.samples[j].t + span) - model.TurnsOf(model.samples[j].t));
                const int steps = std::max(1, static_cast<int>(std::ceil(turnsAcross * 2.0 * kPi / kArcStep)));
                const double hNext = (j + 1 < shown && std::isfinite(model.samples[j + 1].value) && next - model.samples[j].t <= span + 1e-9)
                                         ? model.Normalize(model.samples[j + 1].value) * 0.95 : h;
                for (int k = 0; k < steps; ++k) {
                    ts.push_back(model.samples[j].t + span * k / steps);
                    heights.push_back(h + (hNext - h) * k / steps);
                }
                if (next - model.samples[j].t > span + 1e-9) { ++j; break; }       // a gap ends the run
            }
            const size_t last = j - 1;
            ts.push_back(model.samples[last].t + model.SampleSpan(last));
            heights.push_back(heights.back());

            std::vector<Point2Dd> top;
            top.reserve(ts.size());
            for (size_t k = 0; k < ts.size(); ++k) top.push_back(ScreenPoint(ts[k], -0.5 + heights[k]));
            if (display == SpiralDisplay::Band) {
                std::vector<Point2Dd> polygon = top;
                for (size_t k = ts.size(); k-- > 0;) polygon.push_back(ScreenPoint(ts[k], -0.5));
                ctx->SetFillPaint(main.WithAlpha(190));
                ctx->FillLinePath(polygon);
                ctx->SetStrokePaint(main.Blend(Color(0, 0, 0, 255), 0.3f));
                ctx->SetStrokeWidth(1.0f);
            } else {
                ctx->SetStrokePaint(main);
                ctx->SetStrokeWidth(1.6f);
            }
            ctx->DrawLinePath(top, false);
            i = j;
        }
    }

    // ---- hit regions: each sample's piece of its ring ------------------------
    for (size_t k = 0; k < n; ++k) {
        if (!std::isfinite(model.samples[k].value)) continue;
        AddHitRegion(Piece(model.samples[k].t, model.samples[k].t + model.SampleSpan(k), -0.5, 0.5),
                     static_cast<int64_t>(k), DescribeSample(k));
    }

    // ---- sector labels around the outside ---------------------------------------
    ctx->SetFontSize(axisFontSize);
    ctx->SetFontWeight(FontWeight::Normal);
    ctx->SetTextPaint(axisLabelColor);
    for (int k = 0; k < sectors; ++k) {
        const SpiralPosition p = model.Position(start + model.period * (k + 0.5) / sectors);
        const std::string& label = model.sectorLabels[static_cast<size_t>(k)];
        const Size2Di size = ctx->GetTextLineDimensions(label);
        const double c = std::cos(p.angle), s = std::sin(p.angle);
        // Out from the circle by half the label's extent along the ray.
        const double reach = outerRadius + 5.0 + 0.5 * (std::abs(c) * size.width + std::abs(s) * size.height);
        const Point2Dd at(centre.x + reach * c, centre.y - reach * s);
        ctx->DrawText(label, Point2Dd(at.x - size.width * 0.5, at.y - size.height * 0.5));
    }

    // ---- turn labels on the start ray, where a ring is tall enough ----------------
    const double ringPixels = model.RingWidth() * outerRadius;
    ctx->SetFontSize(std::max(8.0f, axisFontSize - 1.0f));
    for (int k = 0; k < turns && k < static_cast<int>(model.turnLabels.size()); ++k) {
        const std::string& label = model.turnLabels[static_cast<size_t>(k)];
        const Size2Di size = ctx->GetTextLineDimensions(label);
        if (ringPixels < size.height + 2.0) break;
        const Point2Dd at = ScreenPoint(start + k * model.period, 0.0);
        const Rect2Dd box(at.x - size.width * 0.5 - 3.0, at.y - size.height * 0.5 - 1.0, size.width + 6.0, size.height + 2.0);
        ctx->SetFillPaint(GetTheme().backgroundColor.WithAlpha(215));
        ctx->FillRectangle(box);
        ctx->SetTextPaint(axisLabelColor);
        ctx->DrawText(label, Point2Dd(at.x - size.width * 0.5, at.y - size.height * 0.5));
    }
    (void)end;
}

void UltraCanvasSpiralChart::RenderInteractionOverlay(IRenderContext* ctx, const ChartEngineFrame& frame) {
    // The hovered sample's piece of ring, outlined.
    if (hoveredSample >= 0 && static_cast<size_t>(hoveredSample) < model.samples.size()) {
        const SpiralSample& s = model.samples[static_cast<size_t>(hoveredSample)];
        ctx->SetStrokePaint(GetTheme().axisLineColor);
        ctx->SetStrokeWidth(1.5f);
        ctx->DrawLinePath(Piece(s.t, s.t + model.SampleSpan(static_cast<size_t>(hoveredSample)), -0.5, 0.5), true);
    }

    // The colour bar under the spiral: the ribbon's colours from low to high.
    if (!ColorBarShown()) return;
    const Rect2Dd& plot = frame.plotArea;
    const double width = std::min(280.0, plot.width * 0.6);
    const Rect2Dd bar(plot.x + (plot.width - width) * 0.5, plot.y + plot.height + 8.0, width, 10.0);
    std::vector<GradientStop> stops;
    for (int k = 0; k <= 8; ++k) {
        stops.emplace_back(k / 8.0, SampleColormap(colormap, {}, k / 8.0, colormapReversed).WithAlpha(255));
    }
    ctx->SetFillPaint(ctx->CreateLinearGradientPattern(bar.x, bar.y, bar.x + bar.width, bar.y, stops));
    ctx->FillRectangle(bar);
    ctx->SetStrokePaint(GetTheme().axisLineColor.WithAlpha(120));
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawRectangle(bar);

    const auto [lo, hi] = model.ValueRange();
    ctx->SetFontSize(axisFontSize);
    ctx->SetFontWeight(FontWeight::Normal);
    ctx->SetTextPaint(axisLabelColor);
    const std::string low = FormatValue(lo), high = FormatValue(hi);
    const double textTop = bar.y + bar.height + 3.0;
    ctx->DrawText(low, Point2Dd(bar.x, textTop));
    ctx->DrawText(high, Point2Dd(bar.x + bar.width - ctx->GetTextLineDimensions(high).width, textTop));
    const double nameWidth = ctx->GetTextLineDimensions(model.valueName).width;
    ctx->DrawText(model.valueName, Point2Dd(bar.x + (bar.width - nameWidth) * 0.5, textTop));
}

} // namespace UltraCanvas

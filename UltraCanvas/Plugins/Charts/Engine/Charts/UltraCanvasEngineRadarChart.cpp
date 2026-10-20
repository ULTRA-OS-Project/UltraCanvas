// Plugins/Charts/Engine/Charts/UltraCanvasEngineRadarChart.cpp
// Radar chart on the chart engine.
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineRadarChart.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

UltraCanvasEngineRadarChart::UltraCanvasEngineRadarChart(const std::string& id, int x, int y,
                                                         int w, int h)
    : UltraCanvasChartEngineElement(id, x, y, w, h) {
    SetProjectionKind(ChartProjectionKind::Polar);
    SetPolarGridShape(ChartPolarGridShape::Polygon);   // the spider web
    SetAnimateOnDataChange(true, 0.9f);                // the polygons grow out
    SetEnableTooltips(true);
    SetLegendPosition(ChartLegendPosition::RightStart);
}

// =============================================================================
// DATA
// =============================================================================

void UltraCanvasEngineRadarChart::AddAxis(const std::string& name, double minValue,
                                          double maxValue) {
    radarAxes.emplace_back(name, minValue, maxValue);
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineRadarChart::SetAxisRange(size_t axisIndex, double minValue,
                                               double maxValue) {
    if (axisIndex >= radarAxes.size()) return;
    radarAxes[axisIndex].minValue = minValue;
    radarAxes[axisIndex].maxValue = maxValue;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineRadarChart::ClearAxes() {
    radarAxes.clear();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineRadarChart::AddSeries(const EngineRadarSeries& series) {
    radarSeries.push_back(series);
    seriesHidden.push_back(false);
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineRadarChart::AddSeries(const std::string& name,
                                            const std::vector<double>& values,
                                            const Color& color) {
    EngineRadarSeries series;
    series.name = name;
    series.values = values;
    series.color = color;
    AddSeries(series);
}

void UltraCanvasEngineRadarChart::SetSeriesStyle(size_t seriesIndex, const Color& color,
                                                 float strokeWidth, float fillOpacity) {
    if (seriesIndex >= radarSeries.size()) return;
    radarSeries[seriesIndex].color = color;
    radarSeries[seriesIndex].strokeWidth = strokeWidth;
    radarSeries[seriesIndex].fillOpacity = std::clamp(fillOpacity, 0.0f, 1.0f);
    RefreshLegend();
    RequestRedraw();
}

void UltraCanvasEngineRadarChart::SetSeriesPointDisplay(size_t seriesIndex, bool showPoints,
                                                        float pointRadius,
                                                        ChartMarkerShape marker) {
    if (seriesIndex >= radarSeries.size()) return;
    radarSeries[seriesIndex].showPoints = showPoints;
    radarSeries[seriesIndex].pointRadius = pointRadius;
    radarSeries[seriesIndex].marker = marker;
    MarkEngineDirty(ChartDirty::Style);     // value labels clear the markers
}

void UltraCanvasEngineRadarChart::ClearSeries() {
    radarSeries.clear();
    seriesHidden.clear();
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineRadarChart::SetShowGrid(bool show) {
    showGridLines = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineRadarChart::SetGridLevels(int levels) {
    gridLevels = std::clamp(levels, 1, 20);
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineRadarChart::SetShowAxisLabels(bool show) {
    showAxisLabels = show;
    MarkEngineDirty(ChartDirty::Style);     // the rim labels reserve margin
}

void UltraCanvasEngineRadarChart::SetShowValueLabels(bool show) {
    showValues = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineRadarChart::SetRadarOrientation(double startAngleDegrees,
                                                      bool clockwise) {
    SetPolarAngles(startAngleDegrees, 360.0, clockwise);
}

void UltraCanvasEngineRadarChart::SetUseThemePalette(bool use) {
    useThemePalette = use;
    RefreshLegend();
    RequestRedraw();
}

// =============================================================================
// HELPERS
// =============================================================================

Color UltraCanvasEngineRadarChart::SeriesColor(size_t seriesIndex) const {
    const Color& own = radarSeries[seriesIndex].color;
    if (!useThemePalette && own.a > 0) return own.WithAlpha(255);
    return Palette().ColorAt(seriesIndex, radarSeries.size());
}

bool UltraCanvasEngineRadarChart::SharedRange(double& lo, double& hi) const {
    if (radarAxes.empty()) {
        lo = 0.0;
        hi = 100.0;
        return true;
    }
    lo = radarAxes.front().minValue;
    hi = radarAxes.front().maxValue;
    for (const EngineRadarAxis& axis : radarAxes) {
        if (axis.minValue != lo || axis.maxValue != hi) return false;
    }
    return hi > lo;
}

double UltraCanvasEngineRadarChart::NormalizeOnAxis(size_t axisIndex, double value) const {
    const EngineRadarAxis& axis = radarAxes[axisIndex];
    const double span = axis.maxValue - axis.minValue;
    return (std::abs(span) < 1e-12) ? 0.0 : (value - axis.minValue) / span;
}

double UltraCanvasEngineRadarChart::ValueAt(const EngineRadarSeries& series,
                                            size_t axisIndex) const {
    if (axisIndex >= series.values.size() || !std::isfinite(series.values[axisIndex])) {
        return radarAxes[axisIndex].minValue;
    }
    return series.values[axisIndex];
}

std::string UltraCanvasEngineRadarChart::FormatAxisValue(size_t axisIndex, double value) const {
    // The spoke's own range decides the decimals, exactly as an axis would.
    ChartAxis format;
    format.SetRange(radarAxes[axisIndex].minValue, radarAxes[axisIndex].maxValue);
    format.Finalize();
    return format.FormatValue(value);
}

void UltraCanvasEngineRadarChart::RefreshLegend() {
    std::vector<ChartLegendEntry> entries;
    for (size_t i = 0; i < radarSeries.size(); ++i) {
        ChartLegendEntry entry(radarSeries[i].name, SeriesColor(i),
                               radarSeries[i].fillOpacity > 0.0f ? LegendSwatch::Square
                                                                 : LegendSwatch::Line);
        entry.enabled = !seriesHidden[i];       // toggles survive a rebuild
        entries.push_back(entry);
    }
    SetLegendEntries(entries);
    SetShowLegend(radarSeries.size() > 1);
}

void UltraCanvasEngineRadarChart::OnThemeChanged() { RefreshLegend(); }

void UltraCanvasEngineRadarChart::OnLegendEntryToggled(size_t entryIndex, bool enabled) {
    if (entryIndex >= seriesHidden.size()) return;
    seriesHidden[entryIndex] = !enabled;
    MarkEngineDirty(ChartDirty::Style);     // value labels follow the visible series
}

// =============================================================================
// ENGINE CONTRACT
// =============================================================================

void UltraCanvasEngineRadarChart::DescribeAxes(ChartAxisSet& axes) {
    // Index 0: the value axis - a Left edge axis, which the engine turns into
    // the radial rule at the first spoke under Polar.
    ChartAxis value("value");
    value.side = ChartAxisSide::Left;
    double lo = 0.0, hi = 100.0;
    if (SharedRange(lo, hi)) {
        value.SetRange(lo, hi);
    } else {
        // Spokes with ranges of their own: the rule reads the fraction of each.
        value.SetRange(0.0, 100.0);
        value.unitSuffix = "%";
        value.decimals = 0;
        lo = 0.0;
        hi = 100.0;
    }
    for (int k = 0; k <= gridLevels; ++k) {
        value.tickValues.push_back(lo + (hi - lo) * k / gridLevels);
    }
    axes.Add(value);

    // Index 1: the spokes - a cyclic category axis, so n spokes spread evenly
    // round the full circle with the first at the start angle.
    ChartAxis spokes("spokes");
    spokes.scale = ChartScale::Category;
    spokes.side = ChartAxisSide::Bottom;
    spokes.cyclic = true;
    spokes.visible = showAxisLabels;
    for (const EngineRadarAxis& axis : radarAxes) spokes.categories.push_back(axis.name);
    axes.Add(spokes);

    SetGridAxis(showGridLines ? 0 : static_cast<size_t>(-1));
    SetDomainGridAxis(showGridLines ? 1 : static_cast<size_t>(-1));
}

void UltraCanvasEngineRadarChart::RenderChartContent(IRenderContext* ctx,
                                                     const ChartEngineFrame& frame) {
    if (radarAxes.size() < 3 || !frame.axes || frame.axes->Count() < 2) return;
    const ChartAxis& spokes = frame.axes->At(1);
    const double progress = frame.animationProgress;
    const int64_t hovered = HoveredRegionId();
    const size_t hoveredSeries = hovered >= 0 ? static_cast<size_t>(hovered / 4096) : SIZE_MAX;

    ctx->PushState();
    ctx->SetLineJoin(LineJoin::Round);
    for (size_t s = 0; s < radarSeries.size(); ++s) {
        if (seriesHidden[s]) continue;
        const EngineRadarSeries& series = radarSeries[s];
        const Color color = SeriesColor(s);

        // The vertices are projected points; the polygon joins them with
        // straight chords, as the web does.
        std::vector<Point2Dd> polygon;
        polygon.reserve(radarAxes.size());
        for (size_t a = 0; a < radarAxes.size(); ++a) {
            const double v = std::clamp(NormalizeOnAxis(a, ValueAt(series, a)), 0.0, 1.05);
            polygon.push_back(frame.projection->ToScreen(ChartNormalizedPoint(
                spokes.Normalize(static_cast<double>(a)), v * progress)));
        }

        const bool emphasised = (s == hoveredSeries);
        if (series.fillOpacity > 0.0f) {
            const float opacity = std::min(1.0f, series.fillOpacity * (emphasised ? 1.5f : 1.0f));
            ctx->SetFillPaint(color.WithAlpha(static_cast<uint8_t>(opacity * 255.0f)));
            ctx->FillLinePath(polygon);
        }
        ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(series.strokeWidth + (emphasised ? 1.5f : 0.0f));
        ctx->DrawLinePath(polygon, true);

        for (size_t a = 0; a < polygon.size(); ++a) {
            const bool hot = (hovered == RegionId(s, a));
            if (series.showPoints) {
                DrawChartMarker(ctx, series.marker, polygon[a],
                                series.pointRadius * (hot ? 1.5 : 1.0), color,
                                frame.theme ? frame.theme->plotAreaColor : Colors::White, 1.5f);
            }
            const double r = std::max(6.0, static_cast<double>(series.pointRadius) + 3.0);
            AddHitRegion(Rect2Dd(polygon[a].x - r, polygon[a].y - r, r * 2.0, r * 2.0),
                         RegionId(s, a),
                         series.name + "\n" + radarAxes[a].name + ": " +
                             FormatAxisValue(a, ValueAt(series, a)));
        }
    }
    ctx->PopState();
}

void UltraCanvasEngineRadarChart::CollectChartLabels(IRenderContext* ctx,
                                                     ChartLabelBroker& broker) {
    if (!showValues || radarAxes.size() < 3 || !Frame().axes || Frame().axes->Count() < 2 ||
        !Frame().projection) return;
    const ChartAxis& spokes = Frame().axes->At(1);
    ctx->SetFontSize(axisFontSize);
    for (size_t s = 0; s < radarSeries.size(); ++s) {
        if (seriesHidden[s]) continue;
        const EngineRadarSeries& series = radarSeries[s];
        for (size_t a = 0; a < radarAxes.size(); ++a) {
            const double value = ValueAt(series, a);
            const double v = std::clamp(NormalizeOnAxis(a, value), 0.0, 1.05);
            ChartLabelRequest label;
            label.text = FormatAxisValue(a, value);
            label.klass = ChartLabelClass::ValueLabel;
            label.anchor = Frame().projection->ToScreen(
                ChartNormalizedPoint(spokes.Normalize(static_cast<double>(a)), v));
            label.anchorRadius = series.showPoints ? series.pointRadius : 2.0;
            const Size2Di size = ctx->GetTextLineDimensions(label.text);
            label.textSize = Size2Dd(size.width, size.height);
            label.allowSuppress = true;           // dense webs thin themselves
            label.userData = static_cast<size_t>(RegionId(s, a));
            broker.Add(label);
        }
    }
}

} // namespace UltraCanvas

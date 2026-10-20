// Plugins/Charts/Engine/Charts/UltraCanvasEngineRadialBarChart.cpp
// Radial bar ("ray") chart on the chart engine.
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineRadialBarChart.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace UltraCanvas {

UltraCanvasEngineRadialBarChart::UltraCanvasEngineRadialBarChart(const std::string& id, int x,
                                                                 int y, int w, int h)
    : UltraCanvasChartEngineElement(id, x, y, w, h) {
    SetProjectionKind(ChartProjectionKind::Polar);
    SetPolarAngles(startAngle, sweepAngle, true);
    SetPolarInnerRadius(0.25);
    SetAnimateOnDataChange(true, 0.8f);     // the rays grow out of the ring
    SetEnableTooltips(true);
}

// =============================================================================
// CONFIGURATION
// =============================================================================

void UltraCanvasEngineRadialBarChart::AddSeries(const EngineRadialBarSeries& series) {
    seriesList.push_back(series);
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineRadialBarChart::AddSeries(
        const std::string& name, const std::vector<std::pair<std::string, double>>& values,
        const Color& color) {
    EngineRadialBarSeries series;
    series.name = name;
    series.color = color;
    for (const auto& [label, value] : values) series.values.emplace_back(label, value);
    AddSeries(series);
}

void UltraCanvasEngineRadialBarChart::ClearSeries() {
    seriesList.clear();
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineRadialBarChart::SetStartAngle(double degrees) {
    startAngle = degrees;
    SetPolarAngles(startAngle, sweepAngle, true);
}

void UltraCanvasEngineRadialBarChart::SetSweepAngle(double degrees) {
    sweepAngle = std::clamp(degrees, 10.0, 360.0);
    SetPolarAngles(startAngle, sweepAngle, true);
    MarkEngineDirty(ChartDirty::Data);      // the gaps are angles: the slots move
}

void UltraCanvasEngineRadialBarChart::SetInnerRadiusFraction(float fraction) {
    SetPolarInnerRadius(std::clamp(fraction, 0.05f, 0.9f));
}

void UltraCanvasEngineRadialBarChart::SetSeriesGapAngle(double degrees) {
    seriesGap = std::max(0.0, degrees);
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineRadialBarChart::SetBarWidth(float pixels) {
    barWidth = std::max(0.0f, pixels);
    RequestRedraw();
}

void UltraCanvasEngineRadialBarChart::SetNormalization(EngineRadialBarNormalization mode) {
    normalization = mode;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineRadialBarChart::SetValueRange(double minValue, double maxValue) {
    manualRange = maxValue > minValue;
    manualMin = minValue;
    manualMax = maxValue;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineRadialBarChart::ClearValueRange() {
    manualRange = false;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineRadialBarChart::SetMinBarLengthFraction(float fraction) {
    minBarLength = std::clamp(fraction, 0.0f, 0.5f);
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineRadialBarChart::SetBarStyle(EngineRadialBarStyle style) {
    barStyle = style;
    RequestRedraw();
}

void UltraCanvasEngineRadialBarChart::SetCapStyle(EngineRadialBarCap cap) {
    capStyle = cap;
    RequestRedraw();
}

void UltraCanvasEngineRadialBarChart::SetGradientFade(bool on) {
    gradientFade = on;
    RequestRedraw();
}

void UltraCanvasEngineRadialBarChart::SetUseThemePalette(bool use) {
    useThemePalette = use;
    RefreshLegend();
    RequestRedraw();
}

void UltraCanvasEngineRadialBarChart::SetShowRingGuides(bool show) {
    showRingGuides = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineRadialBarChart::SetRingGuideCount(int count) {
    ringGuideCount = std::clamp(count, 1, 12);
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineRadialBarChart::SetInnerCircleColor(const Color& fill, const Color& border) {
    innerFill = fill;
    innerBorder = border;
    RequestRedraw();
}

void UltraCanvasEngineRadialBarChart::SetCenterText(const std::string& text) {
    centerText = text;
    RequestRedraw();
}

void UltraCanvasEngineRadialBarChart::SetShowSeriesLabels(bool show) {
    showSeriesLabels = show;
    MarkEngineDirty(ChartDirty::Style);     // the rim labels reserve margin
}

void UltraCanvasEngineRadialBarChart::SetShowPeakLabels(bool show) {
    showPeakLabels = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineRadialBarChart::SetValueFormatter(
        std::function<std::string(double)> formatter) {
    valueFormatter = std::move(formatter);
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineRadialBarChart::SetHoverHighlightEnabled(bool on) {
    hoverHighlight = on;
    RequestRedraw();
}

// =============================================================================
// HELPERS
// =============================================================================

Color UltraCanvasEngineRadialBarChart::SeriesColor(size_t seriesIndex) const {
    const Color& own = seriesList[seriesIndex].color;
    if (!useThemePalette && own.a > 0) return own;
    return Palette().ColorAt(seriesIndex, seriesList.size());
}

Color UltraCanvasEngineRadialBarChart::RayColor(const Ray& ray) const {
    const Color& own = seriesList[ray.seriesIndex].values[ray.valueIndex].color;
    return (!useThemePalette && own.a > 0) ? own : SeriesColor(ray.seriesIndex);
}

std::string UltraCanvasEngineRadialBarChart::FormatValue(double value) const {
    if (valueFormatter) return valueFormatter(value);
    double most = 1.0;
    for (const EngineRadialBarSeries& s : seriesList) {
        for (const EngineRadialBarValue& v : s.values) most = std::max(most, std::abs(v.value));
    }
    ChartAxis format;
    format.SetRange(0.0, most);
    format.Finalize();
    return format.FormatValue(value);
}

void UltraCanvasEngineRadialBarChart::RefreshLegend() {
    std::vector<ChartLegendEntry> entries;
    for (size_t i = 0; i < seriesList.size(); ++i) {
        entries.emplace_back(seriesList[i].name, SeriesColor(i), LegendSwatch::Line);
    }
    SetLegendEntries(entries);
}

void UltraCanvasEngineRadialBarChart::OnThemeChanged() { RefreshLegend(); }

void UltraCanvasEngineRadialBarChart::Layout(std::vector<Ray>& rays,
                                             std::vector<Sector>& sectors) const {
    rays.clear();
    sectors.clear();
    size_t total = 0;
    for (const EngineRadialBarSeries& s : seriesList) total += s.values.size();
    if (total == 0) return;

    // ---- value ranges (from zero, so lengths stay proportional) ------------
    double globalMin = std::numeric_limits<double>::max();
    double globalMax = std::numeric_limits<double>::lowest();
    std::vector<std::pair<double, double>> ranges(seriesList.size(),
        {std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest()});
    for (size_t si = 0; si < seriesList.size(); ++si) {
        for (const EngineRadialBarValue& v : seriesList[si].values) {
            if (!std::isfinite(v.value)) continue;
            globalMin = std::min(globalMin, v.value);
            globalMax = std::max(globalMax, v.value);
            ranges[si].first = std::min(ranges[si].first, v.value);
            ranges[si].second = std::max(ranges[si].second, v.value);
        }
    }
    auto lengthOf = [&](size_t si, double value) {
        double lo, hi;
        if (manualRange) {
            lo = manualMin;
            hi = manualMax;
        } else if (normalization == EngineRadialBarNormalization::PerSeries) {
            lo = std::min(0.0, ranges[si].first);
            hi = ranges[si].second;
        } else {
            lo = std::min(0.0, globalMin);
            hi = globalMax;
        }
        if (!(hi > lo) || !std::isfinite(value)) return 1.0;
        const double n = std::clamp((value - lo) / (hi - lo), 0.0, 1.0);
        return minBarLength + (1.0 - minBarLength) * n;
    };

    // ---- angular layout, in normalised domain units ------------------------
    const bool fullCircle = sweepAngle >= 359.9;
    const size_t gapCount = fullCircle ? seriesList.size() : seriesList.size() - 1;
    double gap = seriesGap / sweepAngle;
    double usable = 1.0 - gap * static_cast<double>(gapCount);
    if (usable <= 0.0) {
        gap = 0.0;
        usable = 1.0;
    }
    const double slot = usable / static_cast<double>(total);

    double cursor = 0.0;
    for (size_t si = 0; si < seriesList.size(); ++si) {
        const EngineRadialBarSeries& series = seriesList[si];
        if (series.values.empty()) continue;
        Sector sector;
        sector.seriesIndex = si;
        sector.u0 = cursor;
        double peak = std::numeric_limits<double>::lowest();
        for (size_t vi = 0; vi < series.values.size(); ++vi) {
            Ray ray;
            ray.seriesIndex = si;
            ray.valueIndex = vi;
            ray.slot = slot;
            ray.u = cursor + slot * 0.5;
            ray.length = lengthOf(si, series.values[vi].value);
            rays.push_back(ray);
            if (series.values[vi].value > peak) {
                peak = series.values[vi].value;
                sector.peakRay = rays.size() - 1;
            }
            cursor += slot;
        }
        sector.u1 = cursor;
        sectors.push_back(sector);
        cursor += gap;
    }
}

// =============================================================================
// ENGINE CONTRACT
// =============================================================================

void UltraCanvasEngineRadialBarChart::DescribeAxes(ChartAxisSet& axes) {
    std::vector<Ray> rays;
    std::vector<Sector> sectors;
    Layout(rays, sectors);

    // Index 0: the ray length, 0 at the base ring and 1 at the rim. Not drawn
    // (lengths may be per-series), but its ticks are the ring guides.
    ChartAxis length("length");
    length.side = ChartAxisSide::Left;
    length.SetRange(0.0, 1.0);
    length.visible = false;
    for (int k = 1; k <= ringGuideCount; ++k) {
        length.tickValues.push_back(static_cast<double>(k) / ringGuideCount);
    }
    axes.Add(length);

    // Index 1: position round the fan. Its ticks sit at the sector middles
    // and read as the series names, so the engine's rim labels are the
    // series captions.
    ChartAxis around("around");
    around.side = ChartAxisSide::Bottom;
    around.SetRange(0.0, 1.0);
    around.visible = showSeriesLabels && !sectors.empty();
    std::vector<std::pair<double, std::string>> names;
    for (const Sector& sector : sectors) {
        const double mid = (sector.u0 + sector.u1) * 0.5;
        around.tickValues.push_back(mid);
        names.emplace_back(mid, seriesList[sector.seriesIndex].name);
    }
    around.formatter = [names](double u) {
        for (const auto& [mid, name] : names) {
            if (std::abs(mid - u) < 1e-9) return name;
        }
        return std::string();
    };
    axes.Add(around);

    SetGridAxis(showRingGuides ? 0 : static_cast<size_t>(-1));
    SetDomainGridAxis(static_cast<size_t>(-1));
}

void UltraCanvasEngineRadialBarChart::RenderChartContent(IRenderContext* ctx,
                                                         const ChartEngineFrame& frame) {
    const auto* polar = dynamic_cast<const ChartPolarProjection*>(frame.projection);
    if (!polar) return;
    std::vector<Ray> rays;
    std::vector<Sector> sectors;
    Layout(rays, sectors);

    const Point2Dd center = polar->Center();
    const Point2Dd ringPoint = frame.projection->ToScreen(ChartNormalizedPoint(0.0, 0.0));
    const double innerRadius = std::hypot(ringPoint.x - center.x, ringPoint.y - center.y);
    const double sweepRadians = sweepAngle * M_PI / 180.0;
    const Color plotColor = frame.theme ? frame.theme->plotAreaColor : Colors::White;
    const Color gridColorNow = frame.theme ? frame.theme->gridColor : Color(210, 210, 210, 255);

    // The inner disc - a full circle even under a partial sweep.
    ctx->SetFillPaint(innerFill.a > 0 ? innerFill : plotColor.Darken(0.03f));
    ctx->FillCircle(center, innerRadius);
    ctx->SetStrokePaint(innerBorder.a > 0 ? innerBorder : gridColorNow);
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawCircle(center, innerRadius);

    const double progress = frame.animationProgress;
    for (const Ray& ray : rays) {
        const Point2Dd base = frame.projection->ToScreen(ChartNormalizedPoint(ray.u, 0.0));
        const Point2Dd tip = frame.projection->ToScreen(
            ChartNormalizedPoint(ray.u, ray.length * progress));
        const double length = std::hypot(tip.x - base.x, tip.y - base.y);
        if (length <= 0.5) continue;

        const int64_t id = RegionId(ray.seriesIndex, ray.valueIndex);
        const bool hot = hoverHighlight && HoveredRegionId() == id;
        Color color = RayColor(ray);
        if (hot) color = color.Lighten(0.25f);

        double thickness;
        if (barStyle == EngineRadialBarStyle::Lines) {
            thickness = barWidth > 0.0f ? barWidth : 1.5;
        } else if (barWidth > 0.0f) {
            thickness = barWidth;
        } else {
            // ~70% of the angular slot, measured at the base ring.
            thickness = std::clamp(ray.slot * sweepRadians * innerRadius * 0.7, 1.5, 24.0);
        }
        if (hot) thickness *= 1.5;

        ctx->PushState();
        std::shared_ptr<IPaintPattern> fade;
        if (gradientFade && barStyle == EngineRadialBarStyle::Bars) {
            fade = ctx->CreateLinearGradientPattern(
                base.x, base.y, tip.x, tip.y,
                {GradientStop(0.0, color),
                 GradientStop(1.0, color.WithAlpha(static_cast<uint8_t>(color.a * 0.35f)))});
        }
        if (fade) ctx->SetStrokePaint(fade);
        else ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(static_cast<float>(thickness));
        ctx->SetLineCap(capStyle == EngineRadialBarCap::Round    ? LineCap::Round
                        : capStyle == EngineRadialBarCap::Square ? LineCap::Square
                                                                 : LineCap::Butt);
        ctx->DrawLine(base, tip);

        if (capStyle == EngineRadialBarCap::Arrow) {
            const double angle = std::atan2(tip.y - base.y, tip.x - base.x);
            const double reach = std::max(6.0, thickness * 2.0);
            constexpr double kWing = M_PI / 6.0;
            ctx->SetStrokePaint(color);
            ctx->SetStrokeWidth(static_cast<float>(std::max(1.0, thickness * 0.6)));
            ctx->SetLineCap(LineCap::Round);
            ctx->DrawLine(tip, Point2Dd(tip.x - reach * std::cos(angle - kWing),
                                        tip.y - reach * std::sin(angle - kWing)));
            ctx->DrawLine(tip, Point2Dd(tip.x - reach * std::cos(angle + kWing),
                                        tip.y - reach * std::sin(angle + kWing)));
        }
        ctx->PopState();

        // The hit region is the ray's angular slot from the ring to its tip.
        const std::vector<Point2Dd> region = BuildBarOutline(
            *frame.projection, ray.u - ray.slot * 0.5, 0.0, ray.u + ray.slot * 0.5,
            std::min(1.0, ray.length * progress + 0.02), 2);
        const EngineRadialBarSeries& series = seriesList[ray.seriesIndex];
        const EngineRadialBarValue& value = series.values[ray.valueIndex];
        AddHitRegion(region, id,
                     series.name + "\n" + (value.label.empty() ? std::string() : value.label + ": ") +
                         FormatValue(value.value));
    }
}

void UltraCanvasEngineRadialBarChart::CollectChartLabels(IRenderContext* ctx,
                                                         ChartLabelBroker& broker) {
    if (!showPeakLabels || !Frame().projection) return;
    std::vector<Ray> rays;
    std::vector<Sector> sectors;
    Layout(rays, sectors);
    ctx->SetFontSize(axisFontSize - 1.0f);
    for (const Sector& sector : sectors) {
        if (sector.peakRay >= rays.size()) continue;
        const Ray& ray = rays[sector.peakRay];
        ChartLabelRequest label;
        label.text = "Max " + FormatValue(seriesList[ray.seriesIndex].values[ray.valueIndex].value);
        label.klass = ChartLabelClass::ValueLabel;
        label.anchor = Frame().projection->ToScreen(ChartNormalizedPoint(ray.u, ray.length));
        label.anchorRadius = 4.0;
        const Size2Di size = ctx->GetTextLineDimensions(label.text);
        label.textSize = Size2Dd(size.width, size.height);
        label.priority = 8;
        label.userData = sector.peakRay;
        broker.Add(label);
    }
}

void UltraCanvasEngineRadialBarChart::RenderInteractionOverlay(IRenderContext* ctx,
                                                               const ChartEngineFrame& frame) {
    const auto* polar = dynamic_cast<const ChartPolarProjection*>(frame.projection);
    if (!polar || centerText.empty()) return;
    const Point2Dd c = polar->Center();
    ctx->SetFontSize(13.0f);
    ctx->SetFontWeight(FontWeight::Bold);
    ctx->SetTextPaint(frame.theme ? frame.theme->titleTextColor : Color(60, 60, 60, 255));
    const Size2Di size = ctx->GetTextLineDimensions(centerText);
    ctx->DrawText(centerText, Point2Dd(c.x - size.width * 0.5, c.y - size.height * 0.5));
    ctx->SetFontWeight(FontWeight::Normal);
}

} // namespace UltraCanvas

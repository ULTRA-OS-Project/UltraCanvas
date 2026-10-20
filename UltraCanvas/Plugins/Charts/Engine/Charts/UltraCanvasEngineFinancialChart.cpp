// Plugins/Charts/Engine/Charts/UltraCanvasEngineFinancialChart.cpp
// Financial (OHLC) chart on the chart engine.
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineFinancialChart.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace UltraCanvas {

namespace {

// The legend rows the chart keys, in the order RefreshLegend lists them.
enum LegendRole { RoleBullish = 0, RoleBearish = 1, RoleMovingAverage = 2, RoleVolumeAverage = 3 };

std::string PeriodLabel(const char* prefix, int period) {
    return std::string(prefix) + "(" + std::to_string(period) + ")";
}

} // namespace

UltraCanvasEngineFinancialChart::UltraCanvasEngineFinancialChart(
        const std::string& id, int x, int y, int w, int h)
    : UltraCanvasChartEngineElement(id, x, y, w, h) {
    enableTooltips = true;
    SetAnimateOnDataChange(true, 0.6f);
    SetLegendPosition(ChartLegendPosition::TopStart);
    SetLegendOrientation(LegendOrientation::Horizontal);
    SetShowLegend(true);
    RefreshLegend();
}

// =============================================================================
// CONFIGURATION
// =============================================================================

void UltraCanvasEngineFinancialChart::SetData(const std::vector<ChartOHLCBar>& data) {
    bars = data;
    RebuildDisplayed();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineFinancialChart::SetCandleStyle(ChartCandleStyle style) {
    if (style == candleStyle) return;
    candleStyle = style;
    RebuildDisplayed();                       // Heikin-Ashi moves the price range
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineFinancialChart::SetBullishColor(const Color& color) {
    bullishColor = color;
    RefreshLegend();
    RequestRedraw();
}

void UltraCanvasEngineFinancialChart::SetBearishColor(const Color& color) {
    bearishColor = color;
    RefreshLegend();
    RequestRedraw();
}

void UltraCanvasEngineFinancialChart::SetWickColor(const Color& color) {
    wickColor = color;
    RequestRedraw();
}

void UltraCanvasEngineFinancialChart::SetCandleWidthRatio(double ratio) {
    candleWidthRatio = std::clamp(ratio, 0.1, 1.0);
    RequestRedraw();
}

void UltraCanvasEngineFinancialChart::SetPriceFormat(const std::string& prefix, int decimals) {
    pricePrefix = prefix;
    priceDecimals = decimals;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineFinancialChart::SetShowVolumePane(bool show) {
    showVolume = show;
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Style);       // the price band changes
}

void UltraCanvasEngineFinancialChart::SetVolumePaneRatio(double ratio) {
    volumePaneRatio = std::clamp(ratio, 0.1, 0.5);
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineFinancialChart::SetVolumeColor(const Color& color) {
    volumeColor = color;
    RequestRedraw();
}

void UltraCanvasEngineFinancialChart::SetShowMovingAverage(bool show, int period,
                                                           ChartMovingAverageKind kind) {
    showMovingAverage = show;
    movingAveragePeriod = std::max(1, period);
    movingAverageKind = kind;
    hideMovingAverage = false;
    RefreshLegend();
    RequestRedraw();
}

void UltraCanvasEngineFinancialChart::SetMovingAverageColor(const Color& color) {
    movingAverageColor = color;
    RefreshLegend();
    RequestRedraw();
}

void UltraCanvasEngineFinancialChart::SetShowVolumeMovingAverage(bool show, int period) {
    showVolumeMovingAverage = show;
    volumeMovingAveragePeriod = std::max(1, period);
    hideVolumeMovingAverage = false;
    RefreshLegend();
    RequestRedraw();
}

void UltraCanvasEngineFinancialChart::SetVolumeMovingAverageColor(const Color& color) {
    volumeMovingAverageColor = color;
    RefreshLegend();
    RequestRedraw();
}

void UltraCanvasEngineFinancialChart::SetShowCrosshair(bool show) {
    showCrosshair = show;
    RequestRedraw();
}

void UltraCanvasEngineFinancialChart::RebuildDisplayed() {
    std::vector<ChartCandle> raw;
    raw.reserve(bars.size());
    for (const ChartOHLCBar& bar : bars) raw.push_back({bar.open, bar.high, bar.low, bar.close});
    displayed = (candleStyle == ChartCandleStyle::HeikinAshi) ? HeikinAshiCandles(raw) : raw;
}

Color UltraCanvasEngineFinancialChart::MovingAverageColor() const {
    return movingAverageColor.a > 0 ? movingAverageColor : Palette().ColorAt(0);
}

void UltraCanvasEngineFinancialChart::RefreshLegend() {
    std::vector<ChartLegendEntry> entries;
    legendRoles.clear();
    auto add = [&](const std::string& label, const Color& color, LegendSwatch swatch,
                   int role, bool hidden) {
        ChartLegendEntry entry(label, color, swatch);
        entry.enabled = !hidden;
        entries.push_back(entry);
        legendRoles.push_back(role);
    };
    add("Rising", bullishColor, LegendSwatch::Square, RoleBullish, hideBullish);
    add("Falling", bearishColor, LegendSwatch::Square, RoleBearish, hideBearish);
    if (showMovingAverage) {
        add(PeriodLabel(movingAverageKind == ChartMovingAverageKind::Exponential ? "EMA" : "SMA",
                        movingAveragePeriod),
            MovingAverageColor(), LegendSwatch::Line, RoleMovingAverage, hideMovingAverage);
    }
    if (showVolume && showVolumeMovingAverage) {
        add(PeriodLabel("Volume SMA", volumeMovingAveragePeriod), volumeMovingAverageColor,
            LegendSwatch::Line, RoleVolumeAverage, hideVolumeMovingAverage);
    }
    SetLegendEntries(entries);
}

void UltraCanvasEngineFinancialChart::OnThemeChanged() {
    RefreshLegend();     // the default moving-average colour follows the palette
}

void UltraCanvasEngineFinancialChart::OnLegendEntryToggled(size_t entryIndex, bool enabled) {
    if (entryIndex >= legendRoles.size()) return;
    switch (legendRoles[entryIndex]) {
        case RoleBullish:       hideBullish = !enabled; break;
        case RoleBearish:       hideBearish = !enabled; break;
        case RoleMovingAverage: hideMovingAverage = !enabled; break;
        case RoleVolumeAverage: hideVolumeMovingAverage = !enabled; break;
        default: break;
    }
    RequestRedraw();     // nothing moves: the axes keep the full range
}

// =============================================================================
// ENGINE CONTRACT
// =============================================================================

void UltraCanvasEngineFinancialChart::DescribeAxes(ChartAxisSet& axes) {
    // Two panes in one plot: the price axis takes the upper band and the
    // volume axis the lower one, with a gap between them. Both sit on the
    // left edge - their bands are disjoint, so they share the edge line.
    const double gap = 0.05;
    ChartAxis price("price");
    price.side = ChartAxisSide::Left;
    price.unitPrefix = pricePrefix;
    price.decimals = priceDecimals;
    price.bandStart = showVolume ? volumePaneRatio + gap : 0.0;
    price.bandEnd = 1.0;
    for (const ChartCandle& c : displayed) {
        price.Observe(c.low);
        price.Observe(c.high);
    }
    axes.Add(price);                          // kPriceAxis

    ChartAxis volume("volume");
    volume.side = ChartAxisSide::Left;
    volume.compactNumbers = true;
    volume.visible = showVolume;
    volume.bandStart = 0.0;
    volume.bandEnd = volumePaneRatio;
    volume.Observe(0.0);                      // volume bars grow from zero
    for (const ChartOHLCBar& bar : bars) volume.Observe(bar.volume);
    axes.Add(volume);                         // kVolumeAxis

    ChartAxis date("date");
    date.scale = ChartScale::Category;
    date.side = ChartAxisSide::Bottom;
    date.categoryPadding = 0.5;               // full slots for the outer candles
    for (const ChartOHLCBar& bar : bars) date.categories.push_back(bar.date);
    axes.Add(date);                           // kDateAxis

    SetGridAxis(kPriceAxis);
}

std::vector<double> UltraCanvasEngineFinancialChart::MovingAverageOf(
        const std::vector<double>& values, int period, ChartMovingAverageKind kind) const {
    const size_t window = static_cast<size_t>(std::max(1, period));
    return kind == ChartMovingAverageKind::Exponential ? ExponentialMovingAverage(values, window)
                                                       : MovingAverage(values, window);
}

void UltraCanvasEngineFinancialChart::StrokeSeriesLine(IRenderContext* ctx,
                                                       const ChartEngineFrame& frame,
                                                       const ChartAxis& valueAxis,
                                                       const std::vector<double>& values,
                                                       const Color& color, float width) {
    const ChartAxis& date = frame.axes->At(kDateAxis);
    std::vector<ChartNormalizedPoint> points;
    points.reserve(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        points.emplace_back(date.Normalize(static_cast<double>(i)),
                            std::isfinite(values[i]) ? valueAxis.Normalize(values[i])
                                                     : std::nan(""));
    }
    ChartLineOptions options;
    options.samplesPerSegment = 2;
    ctx->PushState();
    ctx->SetLineJoin(LineJoin::Round);
    ctx->SetLineCap(LineCap::Round);
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(width);
    for (const ChartLineRun& run : BuildLineRuns(*frame.projection, points, options)) {
        if (run.points.size() >= 2) ctx->DrawLinePath(run.points, false);
    }
    ctx->PopState();
}

void UltraCanvasEngineFinancialChart::RenderChartContent(IRenderContext* ctx,
                                                         const ChartEngineFrame& frame) {
    if (displayed.empty() || !frame.axes || frame.axes->Count() < 3) return;
    const ChartAxis& price = frame.axes->At(kPriceAxis);
    const ChartAxis& volume = frame.axes->At(kVolumeAxis);
    const ChartAxis& date = frame.axes->At(kDateAxis);
    const IChartProjection& projection = *frame.projection;
    const double progress = frame.animationProgress;

    // Half a candle in normalised domain units: one slot is the distance
    // between neighbouring categories.
    const double slot = std::abs(date.Normalize(1.0) - date.Normalize(0.0));
    const double half = slot * 0.5 * candleWidthRatio;
    const int64_t hovered = HoveredRegionId();

    // ---- volume pane ------------------------------------------------------
    if (showVolume) {
        const double base = volume.Normalize(0.0);
        for (size_t i = 0; i < bars.size(); ++i) {
            if (bars[i].volume <= 0.0) continue;
            const bool rising = displayed[i].close >= displayed[i].open;
            if ((rising && hideBullish) || (!rising && hideBearish)) continue;
            const double u = date.Normalize(static_cast<double>(i));
            const double top = base + (volume.Normalize(bars[i].volume) - base) * progress;
            Color fill = volumeColor.a > 0 ? volumeColor
                                           : (rising ? bullishColor : bearishColor).WithAlpha(110);
            if (hovered == static_cast<int64_t>(i)) fill = fill.WithAlpha(200);
            const std::vector<Point2Dd> outline =
                BuildBarOutline(projection, u - half, base, u + half, top, 2);
            if (outline.size() < 3) continue;
            ctx->SetFillPaint(fill);
            ctx->FillLinePath(outline);
        }
        if (showVolumeMovingAverage && !hideVolumeMovingAverage) {
            std::vector<double> volumes;
            volumes.reserve(bars.size());
            for (const ChartOHLCBar& bar : bars) volumes.push_back(bar.volume * progress);
            StrokeSeriesLine(ctx, frame, volume,
                             MovingAverageOf(volumes, volumeMovingAveragePeriod,
                                             ChartMovingAverageKind::Simple),
                             volumeMovingAverageColor, 1.5f);
        }
    }

    // ---- price pane -------------------------------------------------------
    for (size_t i = 0; i < displayed.size(); ++i) {
        const ChartCandle& c = displayed[i];
        const bool rising = c.close >= c.open;
        if ((rising && hideBullish) || (!rising && hideBearish)) continue;
        const double u = date.Normalize(static_cast<double>(i));

        // Entrance: every candle grows out of its own body midpoint.
        const double mid = price.Normalize((c.open + c.close) * 0.5);
        auto grow = [&](double value) { return mid + (price.Normalize(value) - mid) * progress; };
        const double vOpen = grow(c.open), vClose = grow(c.close);
        const double vHigh = grow(c.high), vLow = grow(c.low);

        const bool isHovered = hovered == static_cast<int64_t>(i);
        Color color = rising ? bullishColor : bearishColor;
        if (isHovered) color = color.Lighten(0.25f);
        const Color wick = wickColor.a > 0 ? wickColor : color;

        ctx->SetStrokeWidth(isHovered ? 2.0f : 1.0f);
        if (candleStyle == ChartCandleStyle::OHLCBars) {
            // Vertical high-low rule, open tick to the left, close to the right.
            ctx->SetStrokePaint(color);
            ctx->SetStrokeWidth(isHovered ? 2.5f : 1.5f);
            ctx->DrawLine(projection.ToScreen(ChartNormalizedPoint(u, vLow)),
                          projection.ToScreen(ChartNormalizedPoint(u, vHigh)));
            ctx->DrawLine(projection.ToScreen(ChartNormalizedPoint(u - half, vOpen)),
                          projection.ToScreen(ChartNormalizedPoint(u, vOpen)));
            ctx->DrawLine(projection.ToScreen(ChartNormalizedPoint(u, vClose)),
                          projection.ToScreen(ChartNormalizedPoint(u + half, vClose)));
        } else {
            ctx->SetStrokePaint(wick);
            ctx->DrawLine(projection.ToScreen(ChartNormalizedPoint(u, vLow)),
                          projection.ToScreen(ChartNormalizedPoint(u, vHigh)));
            std::vector<Point2Dd> body =
                BuildBarOutline(projection, u - half, vOpen, u + half, vClose, 2);
            if (body.size() >= 3) {
                ctx->SetFillPaint(color);
                ctx->FillLinePath(body);
                ctx->SetStrokePaint(color.Darken(0.25f));
                ctx->DrawLinePath(body, true);
            } else {
                // A doji: open == close, so the body is a horizontal rule.
                ctx->SetStrokePaint(color);
                ctx->DrawLine(projection.ToScreen(ChartNormalizedPoint(u - half, vOpen)),
                              projection.ToScreen(ChartNormalizedPoint(u + half, vOpen)));
            }
        }

        // The whole date slot, both panes, is the hover target - a thin
        // candle is still easy to point at.
        const Point2Dd a = projection.ToScreen(ChartNormalizedPoint(u - slot * 0.5, 0.0));
        const Point2Dd b = projection.ToScreen(ChartNormalizedPoint(u + slot * 0.5, 1.0));
        AddHitRegion(Rect2Dd(std::min(a.x, b.x), std::min(a.y, b.y),
                             std::abs(b.x - a.x), std::abs(b.y - a.y)),
                     static_cast<int64_t>(i), TooltipFor(i));
    }

    if (showMovingAverage && !hideMovingAverage) {
        std::vector<double> closes;
        closes.reserve(displayed.size());
        for (const ChartCandle& c : displayed) closes.push_back(c.close);
        std::vector<double> average = MovingAverageOf(closes, movingAveragePeriod, movingAverageKind);
        if (progress < 1.0) {
            // Grow the line with the candles: towards each period's body mid.
            for (size_t i = 0; i < average.size(); ++i) {
                if (!std::isfinite(average[i])) continue;
                const double mid = (displayed[i].open + displayed[i].close) * 0.5;
                average[i] = mid + (average[i] - mid) * progress;
            }
        }
        StrokeSeriesLine(ctx, frame, price, average, MovingAverageColor(), 2.0f);
    }
}

void UltraCanvasEngineFinancialChart::RenderInteractionOverlay(IRenderContext* ctx,
                                                               const ChartEngineFrame& frame) {
    const int64_t hovered = HoveredRegionId();
    if (!showCrosshair || hovered < 0 || static_cast<size_t>(hovered) >= displayed.size() ||
        !frame.axes || frame.axes->Count() < 3) return;
    const ChartAxis& price = frame.axes->At(kPriceAxis);
    const ChartAxis& date = frame.axes->At(kDateAxis);
    const double u = date.Normalize(static_cast<double>(hovered));
    const double v = price.Normalize(displayed[static_cast<size_t>(hovered)].close);

    ctx->PushState();
    ctx->ClipRect(frame.plotArea);
    ctx->SetStrokePaint(GetTheme().axisLineColor.WithAlpha(150));
    ctx->SetStrokeWidth(1.0f);
    ctx->SetLineDash(UCDashPattern({4.0, 3.0}));
    ctx->DrawLine(frame.projection->ToScreen(ChartNormalizedPoint(u, 0.0)),
                  frame.projection->ToScreen(ChartNormalizedPoint(u, 1.0)));
    ctx->DrawLine(frame.projection->ToScreen(ChartNormalizedPoint(0.0, v)),
                  frame.projection->ToScreen(ChartNormalizedPoint(1.0, v)));
    ctx->SetLineDash(UCDashPattern());
    ctx->PopState();
}

std::string UltraCanvasEngineFinancialChart::TooltipFor(size_t index) const {
    const ChartOHLCBar& bar = bars[index];
    const ChartCandle& c = displayed[index];
    std::ostringstream out;
    out << std::fixed << std::setprecision(2);
    out << "Date: " << bar.date << "\n";
    if (candleStyle == ChartCandleStyle::HeikinAshi) out << "Heikin-Ashi\n";
    out << "Open: " << pricePrefix << c.open << "\n"
        << "High: " << pricePrefix << c.high << "\n"
        << "Low: " << pricePrefix << c.low << "\n"
        << "Close: " << pricePrefix << c.close;
    if (bar.volume > 0.0) out << "\nVolume: " << FormatCompactNumber(bar.volume, 1);
    return out.str();
}

} // namespace UltraCanvas

// include/Plugins/Charts/Engine/Charts/UltraCanvasEngineFinancialChart.h
// Financial (OHLC) chart on the chart engine: candlesticks, OHLC bars and
// Heikin-Ashi candles over a date axis, a volume pane below the price pane,
// and simple / exponential moving averages of the close and of the volume.
//
// The engine port of UltraCanvasFinancialChartElement. The two panes are two
// value axes in disjoint sub-bands of one plot (ChartAxis::bandStart /
// bandEnd), so the price grid, the volume bars and both edge axes come from
// the engine's axis and grid layers instead of two hand-laid rectangles; the
// date axis is a Category axis whose labels the engine declutters; candles
// are hit regions with OHLC + volume tooltips; the moving averages are line
// runs keyed in the legend. Heikin-Ashi candles are computed properly
// (HeikinAshiCandles) rather than drawn as plain candles.
//
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include "Plugins/Charts/Engine/UltraCanvasChartAnalysis.h"
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// One trading period.
struct ChartOHLCBar {
    std::string date;          // the date axis label
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 0.0;

    ChartOHLCBar() = default;
    ChartOHLCBar(const std::string& d, double o, double h, double l, double c, double v = 0.0)
        : date(d), open(o), high(h), low(l), close(c), volume(v) {}
};

enum class ChartCandleStyle { Candlestick, OHLCBars, HeikinAshi };

enum class ChartMovingAverageKind { Simple, Exponential };

class UltraCanvasEngineFinancialChart : public UltraCanvasChartEngineElement {
public:
    UltraCanvasEngineFinancialChart(const std::string& id, int x = 0, int y = 0,
                                    int w = 0, int h = 0);

    // ---- data -----------------------------------------------------------
    void SetData(const std::vector<ChartOHLCBar>& bars);
    const std::vector<ChartOHLCBar>& GetData() const { return bars; }

    // ---- style ----------------------------------------------------------
    void SetCandleStyle(ChartCandleStyle style);
    ChartCandleStyle GetCandleStyle() const { return candleStyle; }
    void SetBullishColor(const Color& color);
    void SetBearishColor(const Color& color);
    // Wick colour; alpha 0 (the default) draws each wick in its candle's colour.
    void SetWickColor(const Color& color);
    // Fraction of a date slot a candle occupies, 0.1..1.
    void SetCandleWidthRatio(double ratio);
    // Price axis formatting, e.g. ("$", 2).
    void SetPriceFormat(const std::string& prefix, int decimals = -1);

    // ---- volume pane ----------------------------------------------------
    void SetShowVolumePane(bool show);
    bool GetShowVolumePane() const { return showVolume; }
    // Share of the plot height the volume pane takes, 0.1..0.5.
    void SetVolumePaneRatio(double ratio);
    // Volume bar colour; alpha 0 (the default) tints each bar bullish/bearish.
    void SetVolumeColor(const Color& color);

    // ---- moving averages ------------------------------------------------
    void SetShowMovingAverage(bool show, int period = 20,
                              ChartMovingAverageKind kind = ChartMovingAverageKind::Simple);
    bool GetShowMovingAverage() const { return showMovingAverage; }
    // Alpha 0 (the default) takes the palette's first colour.
    void SetMovingAverageColor(const Color& color);
    void SetShowVolumeMovingAverage(bool show, int period = 20);
    void SetVolumeMovingAverageColor(const Color& color);

    // A crosshair through the hovered period (vertical) and its close (horizontal).
    void SetShowCrosshair(bool show);

    // ---- engine contract ------------------------------------------------
    void DescribeAxes(ChartAxisSet& axes) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void RenderInteractionOverlay(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void OnThemeChanged() override;
    void OnLegendEntryToggled(size_t entryIndex, bool enabled) override;

private:
    std::vector<ChartOHLCBar> bars;
    std::vector<ChartCandle> displayed;     // raw or Heikin-Ashi, per style
    ChartCandleStyle candleStyle = ChartCandleStyle::Candlestick;
    Color bullishColor = Color(0, 160, 80, 255);
    Color bearishColor = Color(215, 50, 50, 255);
    Color wickColor = Color(0, 0, 0, 0);
    Color volumeColor = Color(0, 0, 0, 0);
    double candleWidthRatio = 0.75;
    std::string pricePrefix = "$";
    int priceDecimals = -1;

    bool showVolume = true;
    double volumePaneRatio = 0.22;

    bool showMovingAverage = false;
    int movingAveragePeriod = 20;
    ChartMovingAverageKind movingAverageKind = ChartMovingAverageKind::Simple;
    Color movingAverageColor = Color(0, 0, 0, 0);
    bool showVolumeMovingAverage = false;
    int volumeMovingAveragePeriod = 20;
    Color volumeMovingAverageColor = Color(80, 80, 200, 255);

    bool showCrosshair = true;

    // Legend entries hidden by a click (indices into the entry list).
    bool hideBullish = false, hideBearish = false;
    bool hideMovingAverage = false, hideVolumeMovingAverage = false;
    // Which entry each legend row is: 0 bull, 1 bear, 2 MA, 3 volume MA.
    std::vector<int> legendRoles;

    static constexpr size_t kPriceAxis = 0;
    static constexpr size_t kVolumeAxis = 1;
    static constexpr size_t kDateAxis = 2;

    void RebuildDisplayed();
    void RefreshLegend();
    Color MovingAverageColor() const;
    std::vector<double> MovingAverageOf(const std::vector<double>& values, int period,
                                        ChartMovingAverageKind kind) const;
    std::string TooltipFor(size_t index) const;
    void StrokeSeriesLine(IRenderContext* ctx, const ChartEngineFrame& frame,
                          const ChartAxis& valueAxis, const std::vector<double>& values,
                          const Color& color, float width);
};

inline std::shared_ptr<UltraCanvasEngineFinancialChart> CreateEngineFinancialChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasEngineFinancialChart>(id, x, y, w, h);
}

} // namespace UltraCanvas

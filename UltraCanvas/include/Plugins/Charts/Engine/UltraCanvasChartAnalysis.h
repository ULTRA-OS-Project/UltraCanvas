// include/Plugins/Charts/Engine/UltraCanvasChartAnalysis.h
// The analysis layer's arithmetic (slot 700, chart engine proposal §5.4):
// trend lines, moving averages and the derived candle series the scatter,
// line and financial charts each computed privately before the engine.
//
// Free of UI dependencies - results are value-space numbers the content maps
// through its axes and the projection, so they unit test without a window.
//
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework
#pragma once

#include <cstddef>
#include <vector>

namespace UltraCanvas {

// Least-squares line y = slope * x + intercept through the finite pairs.
struct ChartLinearFit {
    double slope = 0.0;
    double intercept = 0.0;
    double r2 = 0.0;              // coefficient of determination, 0..1
    double correlation = 0.0;     // Pearson r, -1..1
    size_t count = 0;             // pairs used
    bool valid = false;           // needs two distinct x values

    double Evaluate(double x) const { return slope * x + intercept; }
};

ChartLinearFit FitLinearTrend(const std::vector<double>& xs, const std::vector<double>& ys);

// Trailing simple moving average. Index i averages the window ending at i;
// the first window-1 entries, and any window holding a non-finite value, are
// NaN - a gap in the line, not a misleading partial average.
std::vector<double> MovingAverage(const std::vector<double>& values, size_t window);

// Exponential moving average with smoothing 2 / (window + 1), seeded with the
// simple average of the first window. NaN before the seed.
std::vector<double> ExponentialMovingAverage(const std::vector<double>& values, size_t window);

// One open-high-low-close bar.
struct ChartCandle {
    double open = 0.0, high = 0.0, low = 0.0, close = 0.0;
};

// Heikin-Ashi smoothing: close = mean of the four prices, open = mean of the
// previous HA open and close (the first bar seeds from its own open/close),
// high/low widened to cover both.
std::vector<ChartCandle> HeikinAshiCandles(const std::vector<ChartCandle>& candles);

} // namespace UltraCanvas

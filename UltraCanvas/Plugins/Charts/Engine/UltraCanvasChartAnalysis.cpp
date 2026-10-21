// Plugins/Charts/Engine/UltraCanvasChartAnalysis.cpp
// Trend lines, moving averages and derived candle series for the chart engine.
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/UltraCanvasChartAnalysis.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

ChartLinearFit FitLinearTrend(const std::vector<double>& xs, const std::vector<double>& ys) {
    ChartLinearFit fit;
    const size_t n = std::min(xs.size(), ys.size());
    double sx = 0.0, sy = 0.0;
    for (size_t i = 0; i < n; ++i) {
        if (!std::isfinite(xs[i]) || !std::isfinite(ys[i])) continue;
        sx += xs[i];
        sy += ys[i];
        ++fit.count;
    }
    if (fit.count < 2) return fit;
    const double mx = sx / static_cast<double>(fit.count);
    const double my = sy / static_cast<double>(fit.count);

    // Centred sums: numerically kinder than the raw-moment formula when the
    // x values are large (timestamps) and close together.
    double sxx = 0.0, syy = 0.0, sxy = 0.0;
    for (size_t i = 0; i < n; ++i) {
        if (!std::isfinite(xs[i]) || !std::isfinite(ys[i])) continue;
        const double dx = xs[i] - mx, dy = ys[i] - my;
        sxx += dx * dx;
        syy += dy * dy;
        sxy += dx * dy;
    }
    if (sxx <= 0.0) return fit;              // every x identical: no slope

    fit.slope = sxy / sxx;
    fit.intercept = my - fit.slope * mx;
    fit.correlation = (syy > 0.0) ? sxy / std::sqrt(sxx * syy) : 0.0;
    fit.r2 = fit.correlation * fit.correlation;
    fit.valid = true;
    return fit;
}

std::vector<double> MovingAverage(const std::vector<double>& values, size_t window) {
    std::vector<double> out(values.size(), std::nan(""));
    if (window == 0) return out;
    for (size_t i = window - 1; i < values.size(); ++i) {
        double sum = 0.0;
        bool finite = true;
        for (size_t k = i + 1 - window; k <= i; ++k) {
            if (!std::isfinite(values[k])) { finite = false; break; }
            sum += values[k];
        }
        if (finite) out[i] = sum / static_cast<double>(window);
    }
    return out;
}

std::vector<double> ExponentialMovingAverage(const std::vector<double>& values,
                                             size_t window) {
    std::vector<double> out(values.size(), std::nan(""));
    if (window == 0 || values.size() < window) return out;
    double seed = 0.0;
    for (size_t k = 0; k < window; ++k) {
        if (!std::isfinite(values[k])) return out;
        seed += values[k];
    }
    double ema = seed / static_cast<double>(window);
    out[window - 1] = ema;
    const double alpha = 2.0 / (static_cast<double>(window) + 1.0);
    for (size_t i = window; i < values.size(); ++i) {
        if (std::isfinite(values[i])) ema = alpha * values[i] + (1.0 - alpha) * ema;
        out[i] = ema;
    }
    return out;
}

std::vector<ChartCandle> HeikinAshiCandles(const std::vector<ChartCandle>& candles) {
    std::vector<ChartCandle> out;
    out.reserve(candles.size());
    for (size_t i = 0; i < candles.size(); ++i) {
        const ChartCandle& c = candles[i];
        ChartCandle ha;
        ha.close = (c.open + c.high + c.low + c.close) / 4.0;
        ha.open = (i == 0) ? (c.open + c.close) / 2.0
                           : (out[i - 1].open + out[i - 1].close) / 2.0;
        ha.high = std::max({c.high, ha.open, ha.close});
        ha.low = std::min({c.low, ha.open, ha.close});
        out.push_back(ha);
    }
    return out;
}

} // namespace UltraCanvas

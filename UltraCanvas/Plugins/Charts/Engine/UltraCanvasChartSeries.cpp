// Plugins/Charts/Engine/UltraCanvasChartSeries.cpp
// Shared series geometry for the bar chart family.
// Version: 1.0.0
// Last Modified: 2026-08-07
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/UltraCanvasChartSeries.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

namespace {

size_t CategoryCountOf(const std::vector<std::vector<double>>& series) {
    size_t count = 0;
    for (const auto& values : series) count = std::max(count, values.size());
    return count;
}

// Positive and negative totals of one category across the series. Stacks
// accumulate the signs separately, so a mixed category diverges around zero
// instead of cancelling out visually.
void CategoryTotals(const std::vector<std::vector<double>>& series, size_t category,
                    double& positive, double& negative) {
    positive = 0.0;
    negative = 0.0;
    for (const auto& values : series) {
        if (category >= values.size()) continue;
        const double v = values[category];
        if (!std::isfinite(v)) continue;
        if (v >= 0.0) positive += v; else negative += v;
    }
}

} // namespace

void ObserveBarSeries(ChartAxis& valueAxis,
                      const std::vector<std::vector<double>>& series,
                      const ChartBarLayoutOptions& options) {
    valueAxis.Observe(0.0);
    const size_t categories = CategoryCountOf(series);

    switch (options.arrangement) {
    case ChartBarArrangement::Grouped:
        for (const auto& values : series) valueAxis.Observe(values);
        break;
    case ChartBarArrangement::Stacked:
        for (size_t c = 0; c < categories; ++c) {
            double positive, negative;
            CategoryTotals(series, c, positive, negative);
            valueAxis.Observe(positive);
            valueAxis.Observe(negative);
        }
        break;
    case ChartBarArrangement::PercentStacked:
        // Shares of the absolute total: all-positive data spans 0..100, a
        // mixed category diverges to its signed percent extremes.
        for (size_t c = 0; c < categories; ++c) {
            double positive, negative;
            CategoryTotals(series, c, positive, negative);
            const double absTotal = positive - negative;
            if (absTotal <= 0.0) continue;
            valueAxis.Observe(positive / absTotal * 100.0);
            valueAxis.Observe(negative / absTotal * 100.0);
        }
        break;
    }
}

std::vector<ChartBarSpan> BuildBarSpans(const ChartAxis& valueAxis,
                                        const ChartAxis& categoryAxis,
                                        size_t categoryCount,
                                        const std::vector<std::vector<double>>& series,
                                        const ChartBarLayoutOptions& options) {
    std::vector<ChartBarSpan> spans;
    const size_t m = series.size();
    if (m == 0 || categoryCount == 0) return spans;

    // Slot width in normalised domain space: the category axis maps the slot
    // centres, so the distance between two neighbours is one slot. A single
    // category owns the whole domain.
    const double slotWidth =
        (categoryCount > 1)
            ? std::abs(categoryAxis.Normalize(1.0) - categoryAxis.Normalize(0.0))
            : 1.0;
    const double fillHalf = slotWidth * std::clamp(options.slotFill, 0.05, 1.0) * 0.5;

    for (size_t c = 0; c < categoryCount; ++c) {
        const double center = categoryAxis.Normalize(static_cast<double>(c));

        if (options.arrangement == ChartBarArrangement::Grouped) {
            const double gap = std::max(0.0, options.groupGap);
            // n bars and n-1 gaps of (gap * barWidth) share the filled slot.
            const double barWidth =
                (fillHalf * 2.0) / (static_cast<double>(m) +
                                    gap * static_cast<double>(m > 0 ? m - 1 : 0));
            for (size_t s = 0; s < m; ++s) {
                if (c >= series[s].size() || !std::isfinite(series[s][c])) continue;
                ChartBarSpan span;
                span.seriesIndex = s;
                span.categoryIndex = c;
                span.value = series[s][c];
                span.plotted = span.value;
                span.u0 = center - fillHalf +
                          barWidth * (1.0 + gap) * static_cast<double>(s);
                span.u1 = span.u0 + barWidth;
                span.v0 = valueAxis.Normalize(0.0);
                span.v1 = valueAxis.Normalize(span.value);
                spans.push_back(span);
            }
        } else {
            const bool percent = options.arrangement == ChartBarArrangement::PercentStacked;
            double positive, negative;
            CategoryTotals(series, c, positive, negative);
            const double absTotal = positive - negative;
            const double scale = (percent && absTotal > 0.0) ? 100.0 / absTotal : 1.0;
            if (percent && absTotal <= 0.0) continue;

            double upward = 0.0, downward = 0.0;   // running stack totals per sign
            for (size_t s = 0; s < m; ++s) {
                if (c >= series[s].size() || !std::isfinite(series[s][c])) continue;
                const double value = series[s][c];
                const double plotted = value * scale;
                double& running = (value >= 0.0) ? upward : downward;

                ChartBarSpan span;
                span.seriesIndex = s;
                span.categoryIndex = c;
                span.value = value;
                span.plotted = plotted;
                span.u0 = center - fillHalf;
                span.u1 = center + fillHalf;
                span.v0 = valueAxis.Normalize(running);
                running += plotted;
                span.v1 = valueAxis.Normalize(running);
                spans.push_back(span);
            }
        }
    }
    return spans;
}

} // namespace UltraCanvas

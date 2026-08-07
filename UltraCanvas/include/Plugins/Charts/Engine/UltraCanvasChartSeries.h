// include/Plugins/Charts/Engine/UltraCanvasChartSeries.h
// Shared series geometry for the bar chart family: grouped (clustered),
// stacked and 100% stacked arrangements resolved into normalised (u, v) spans.
//
// Every bar-family chart needs the same arithmetic - group offsets inside a
// category slot, stack accumulation, percent normalisation, and the value-axis
// range those imply. This module owns that arithmetic so charts describe WHAT
// they show and the engine decides WHERE every bar sits. Negative values are
// first-class: grouped bars grow downward from zero, stacked bars accumulate
// positives upward and negatives downward (a diverging stack), and percent
// stacking shares out the absolute total.
//
// Free of UI dependencies - spans live in normalised chart space and are
// mapped through the projection by the caller, so the same spans work under
// Vertical, Horizontal and Polar.
//
// Version: 1.0.0
// Last Modified: 2026-08-07
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartAxis.h"
#include <cstddef>
#include <vector>

namespace UltraCanvas {

enum class ChartBarArrangement { Grouped, Stacked, PercentStacked };

// One bar (or stack segment) resolved into normalised chart space. The span is
// direction-agnostic: v0 is the base edge, v1 the value edge, and v1 < v0 for
// negative values - consumers order the edges however their drawing needs.
struct ChartBarSpan {
    size_t seriesIndex = 0;
    size_t categoryIndex = 0;
    double value = 0.0;        // the datum as supplied
    double plotted = 0.0;      // what the span shows: the value, or its percent share
    double u0 = 0.0, u1 = 0.0; // domain extent
    double v0 = 0.0, v1 = 0.0; // value extent (base edge -> value edge)
};

struct ChartBarLayoutOptions {
    ChartBarArrangement arrangement = ChartBarArrangement::Grouped;
    // Fraction of a category slot the bars occupy; the rest is the gap
    // between neighbouring slots.
    double slotFill = 0.7;
    // Gap between bars inside a group, as a fraction of one bar's width.
    double groupGap = 0.0;
};

// Feeds the value axis everything the arrangement will plot: zero (bars grow
// from it), every value for Grouped, the positive and negative stack totals
// for Stacked, and the signed percent extremes for PercentStacked. Call from
// DescribeAxes before adding the axis, in place of hand-rolled Observe loops.
void ObserveBarSeries(ChartAxis& valueAxis,
                      const std::vector<std::vector<double>>& series,
                      const ChartBarLayoutOptions& options);

// Resolves every bar into a normalised span. `series` is one value vector per
// series, indexed by category; short series simply have no bar in the missing
// categories. The category axis provides the slot centres (its Category scale
// or any axis whose Normalize maps category indices); the value axis maps the
// value edges. Spans are emitted category-major, stacking order = series order.
std::vector<ChartBarSpan> BuildBarSpans(const ChartAxis& valueAxis,
                                        const ChartAxis& categoryAxis,
                                        size_t categoryCount,
                                        const std::vector<std::vector<double>>& series,
                                        const ChartBarLayoutOptions& options);

} // namespace UltraCanvas

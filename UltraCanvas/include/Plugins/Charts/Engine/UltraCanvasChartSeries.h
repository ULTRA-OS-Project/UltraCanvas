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
// Bar connections (1.1.0) join each series' bars across the categories with a
// straight or curved line - see BuildBarConnections below.
//
// Version: 1.1.0
// Last Modified: 2026-09-27
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartAxis.h"
#include "Plugins/Charts/Engine/UltraCanvasChartProjection.h"
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

// A bar span's screen outline under any projection: the four edges subdivided
// so the shape follows the projection (a rectangle when orthogonal, a ring
// sector under Polar). cornerRadiusPx > 0 rounds the four corners in screen
// space - each corner is trimmed along its (possibly curved) edges by the
// radius and bridged with a sampled fillet, so rounded bars work on ring
// sectors too, not just rectangles. The radius is clamped so it never eats
// more than a fraction of an edge; a bar collapsing during an animation
// degrades gracefully to its plain outline.
std::vector<Point2Dd> BuildBarOutline(const IChartProjection& projection,
                                      double u0, double v0, double u1, double v1,
                                      int subdivisions = 8,
                                      double cornerRadiusPx = 0.0);

// =============================================================================
// BAR CONNECTIONS
// =============================================================================
// A line from the value end of one bar to the value end of the same series'
// bar in the next category: in a clustered chart it traces each series across
// the groups, in a stacked chart it is the classic "series line" joining the
// segment tops. The line is built in normalised space and sampled through the
// projection, so it follows the chart under Vertical, Horizontal and Polar.

enum class ChartBarConnectionShape {
    Straight,   // a polyline, corner to corner
    Curved      // a monotone cubic: smooth, and never overshoots a bar's value
};

enum class ChartBarConnectionAnchor {
    BarCenter,  // the middle of each bar's value edge
    BarEdges    // along each bar's value edge, leaving from its trailing corner
};

struct ChartBarConnectionOptions {
    ChartBarConnectionShape shape = ChartBarConnectionShape::Straight;
    ChartBarConnectionAnchor anchor = ChartBarConnectionAnchor::BarCenter;
    // A series with no bar in a category (a short series, a non-finite value)
    // breaks its line there; true joins the bars either side instead.
    bool bridgeGaps = false;
    // Screen samples per joined pair of bars. Straight lines are sampled too,
    // so they bend with the Polar projection instead of cutting across it.
    int samplesPerSegment = 16;
};

// One unbroken line of one series.
struct ChartBarConnection {
    size_t seriesIndex = 0;
    std::vector<size_t> categories;   // the bars joined, in category order
    std::vector<Point2Dd> points;     // the screen polyline, ready to stroke
    std::vector<Point2Dd> barPoints;  // each joined bar's value-edge centre (markers)
};

// Joins the spans of each series in category order. Pass the spans as drawn -
// scaled by the entrance animation, if any - and the lines move with the bars.
// A series with fewer than two bars in a run produces no line for that run.
// Lines are returned series by series, in series order.
std::vector<ChartBarConnection> BuildBarConnections(const IChartProjection& projection,
                                                    const std::vector<ChartBarSpan>& spans,
                                                    const ChartBarConnectionOptions& options = {});

} // namespace UltraCanvas

// Plugins/Charts/Engine/UltraCanvasChartSeries.cpp
// Shared series geometry for the bar and line chart families.
// Version: 1.3.0 - closed line loops
// Version: 1.2.0 - line and area series, stacking, marker polygons
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/UltraCanvasChartSeries.h"
#include <algorithm>
#include <cmath>
#include <functional>

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

// =============================================================================
// BAR OUTLINE
// =============================================================================

namespace {

using Polyline = std::vector<Point2Dd>;

double PolylineLength(const Polyline& pts) {
    double length = 0.0;
    for (size_t i = 1; i < pts.size(); ++i) {
        length += std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
    }
    return length;
}

// Point at arc length s from the start (clamped to the ends).
Point2Dd PointAtLength(const Polyline& pts, double s) {
    if (s <= 0.0) return pts.front();
    double walked = 0.0;
    for (size_t i = 1; i < pts.size(); ++i) {
        const double seg = std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
        if (walked + seg >= s && seg > 0.0) {
            const double t = (s - walked) / seg;
            return Point2Dd(pts[i - 1].x + (pts[i].x - pts[i - 1].x) * t,
                            pts[i - 1].y + (pts[i].y - pts[i - 1].y) * t);
        }
        walked += seg;
    }
    return pts.back();
}

// The polyline between arc lengths s0 and s1, endpoints interpolated.
void AppendTrimmed(Polyline& out, const Polyline& pts, double s0, double s1) {
    if (s1 <= s0) return;
    out.push_back(PointAtLength(pts, s0));
    double walked = 0.0;
    for (size_t i = 1; i < pts.size(); ++i) {
        const double seg = std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
        walked += seg;
        if (walked > s1) break;
        if (walked > s0) out.push_back(pts[i]);
    }
    out.push_back(PointAtLength(pts, s1));
}

// Corner fillet: a sampled quadratic Bezier from A through the corner C to B.
// For the near-right-angle corners of a bar it deviates from a true circular
// arc by a few percent of the radius - invisible at chart scale, and it stays
// well-behaved when the adjacent edges are curved (ring sectors).
void AppendFillet(Polyline& out, const Point2Dd& a, const Point2Dd& c, const Point2Dd& b) {
    constexpr int kSamples = 8;
    for (int i = 0; i <= kSamples; ++i) {
        const double t = static_cast<double>(i) / kSamples;
        const double w0 = (1.0 - t) * (1.0 - t);
        const double w1 = 2.0 * (1.0 - t) * t;
        const double w2 = t * t;
        out.push_back(Point2Dd(w0 * a.x + w1 * c.x + w2 * b.x,
                               w0 * a.y + w1 * c.y + w2 * b.y));
    }
}

} // namespace

std::vector<Point2Dd> BuildBarOutline(const IChartProjection& projection,
                                      double u0, double v0, double u1, double v1,
                                      int subdivisions,
                                      double cornerRadiusPx) {
    const int steps = std::max(1, subdivisions);

    // The four edges, wound corner 0 -> 1 -> 2 -> 3; corner k is edge k's
    // first point.
    auto makeEdge = [&](double ua, double va, double ub, double vb) {
        Polyline edge;
        edge.reserve(steps + 1);
        for (int s = 0; s <= steps; ++s) {
            const double t = static_cast<double>(s) / steps;
            edge.push_back(projection.ToScreen(
                ChartNormalizedPoint(ua + (ub - ua) * t, va + (vb - va) * t)));
        }
        return edge;
    };
    const Polyline edges[4] = {
        makeEdge(u0, v0, u1, v0),
        makeEdge(u1, v0, u1, v1),
        makeEdge(u1, v1, u0, v1),
        makeEdge(u0, v1, u0, v0),
    };

    std::vector<Point2Dd> outline;
    if (cornerRadiusPx <= 0.0) {
        for (const Polyline& edge : edges) {
            outline.insert(outline.end(), edge.begin(), edge.end() - 1);
        }
        return outline;
    }

    double lengths[4];
    for (int k = 0; k < 4; ++k) lengths[k] = PolylineLength(edges[k]);

    // Corner k sits between edge k-1 and edge k; its radius may never eat
    // more than 45% of either edge, so opposite fillets cannot overlap.
    double radii[4];
    for (int k = 0; k < 4; ++k) {
        radii[k] = std::min({cornerRadiusPx,
                             0.45 * lengths[(k + 3) % 4],
                             0.45 * lengths[k]});
    }

    for (int k = 0; k < 4; ++k) {
        const int prev = (k + 3) % 4;
        const Point2Dd a = PointAtLength(edges[prev], lengths[prev] - radii[k]);
        const Point2Dd b = PointAtLength(edges[k], radii[k]);
        AppendFillet(outline, a, edges[k].front(), b);
        AppendTrimmed(outline, edges[k], radii[k], lengths[k] - radii[(k + 1) % 4]);
    }
    return outline;
}

// =============================================================================
// BAR CONNECTIONS
// =============================================================================

namespace {

struct ConnectionKnot {
    double u = 0.0, v = 0.0;
};

// Knots closer than this in u are one vertical step (touching stacked bars
// with slotFill 1): no slope exists across them, so they join straight.
constexpr double kKnotEpsilon = 1e-12;

// Fritsch-Carlson tangents for a monotone cubic Hermite through the knots.
// The curve is monotone between every pair of knots, so a connection never
// bulges above the taller bar or dips below the shorter one - the line can
// never suggest a value no bar has.
std::vector<double> MonotoneTangents(const std::vector<ConnectionKnot>& knots) {
    const size_t n = knots.size();
    std::vector<double> slope(n - 1, 0.0);
    std::vector<bool> vertical(n - 1, false);
    for (size_t i = 0; i + 1 < n; ++i) {
        const double h = knots[i + 1].u - knots[i].u;
        if (std::abs(h) <= kKnotEpsilon) { vertical[i] = true; continue; }
        slope[i] = (knots[i + 1].v - knots[i].v) / h;
    }

    std::vector<double> tangent(n, 0.0);
    tangent[0] = vertical[0] ? 0.0 : slope[0];
    tangent[n - 1] = vertical[n - 2] ? 0.0 : slope[n - 2];
    for (size_t i = 1; i + 1 < n; ++i) {
        // A peak, a trough, a flat run or a vertical step: level off there.
        if (vertical[i - 1] || vertical[i] || slope[i - 1] * slope[i] <= 0.0) continue;
        tangent[i] = (slope[i - 1] + slope[i]) * 0.5;
    }
    for (size_t i = 0; i + 1 < n; ++i) {
        if (vertical[i]) continue;
        if (slope[i] == 0.0) { tangent[i] = tangent[i + 1] = 0.0; continue; }
        const double a = tangent[i] / slope[i];
        const double b = tangent[i + 1] / slope[i];
        const double sum = a * a + b * b;
        if (sum > 9.0) {
            const double tau = 3.0 / std::sqrt(sum);
            tangent[i] = tau * a * slope[i];
            tangent[i + 1] = tau * b * slope[i];
        }
    }
    return tangent;
}

// The knots one run of bars puts on its line, in travel order.
std::vector<ConnectionKnot> ConnectionKnots(const std::vector<const ChartBarSpan*>& run,
                                            ChartBarConnectionAnchor anchor) {
    std::vector<ConnectionKnot> knots;
    if (anchor == ChartBarConnectionAnchor::BarCenter) {
        for (const ChartBarSpan* span : run) {
            knots.push_back({(span->u0 + span->u1) * 0.5, span->v1});
        }
        return knots;
    }

    // BarEdges: the line leaves each bar from the corner facing the next one
    // and meets the next bar at its near corner, running along the value edge
    // of every bar in between. Category order can run against u (an inverted
    // category axis), so "facing" follows the direction of travel.
    const double first = (run.front()->u0 + run.front()->u1) * 0.5;
    const double last = (run.back()->u0 + run.back()->u1) * 0.5;
    const bool forward = last >= first;
    for (size_t i = 0; i < run.size(); ++i) {
        const ChartBarSpan* span = run[i];
        const double lo = std::min(span->u0, span->u1);
        const double hi = std::max(span->u0, span->u1);
        const double leading = forward ? lo : hi;
        const double trailing = forward ? hi : lo;
        if (i > 0) knots.push_back({leading, span->v1});
        if (i + 1 < run.size()) knots.push_back({trailing, span->v1});
    }
    return knots;
}

} // namespace

std::vector<ChartBarConnection> BuildBarConnections(const IChartProjection& projection,
                                                    const std::vector<ChartBarSpan>& spans,
                                                    const ChartBarConnectionOptions& options) {
    std::vector<ChartBarConnection> connections;
    if (spans.empty()) return connections;

    size_t seriesCount = 0;
    for (const ChartBarSpan& span : spans) {
        seriesCount = std::max(seriesCount, span.seriesIndex + 1);
    }
    const int samples = std::max(1, options.samplesPerSegment);

    for (size_t s = 0; s < seriesCount; ++s) {
        std::vector<const ChartBarSpan*> bars;
        for (const ChartBarSpan& span : spans) {
            if (span.seriesIndex == s && std::isfinite(span.v1)) bars.push_back(&span);
        }
        std::stable_sort(bars.begin(), bars.end(),
                         [](const ChartBarSpan* a, const ChartBarSpan* b) {
                             return a->categoryIndex < b->categoryIndex;
                         });

        // Split into runs of neighbouring categories unless gaps are bridged.
        size_t start = 0;
        while (start < bars.size()) {
            size_t end = start + 1;
            while (end < bars.size() &&
                   (options.bridgeGaps ||
                    bars[end]->categoryIndex == bars[end - 1]->categoryIndex + 1)) {
                ++end;
            }
            const std::vector<const ChartBarSpan*> run(bars.begin() + start,
                                                       bars.begin() + end);
            start = end;
            if (run.size() < 2) continue;

            const std::vector<ConnectionKnot> knots = ConnectionKnots(run, options.anchor);
            const bool curved = options.shape == ChartBarConnectionShape::Curved;
            const std::vector<double> tangent =
                curved ? MonotoneTangents(knots) : std::vector<double>(knots.size(), 0.0);

            ChartBarConnection connection;
            connection.seriesIndex = s;
            for (const ChartBarSpan* span : run) {
                connection.categories.push_back(span->categoryIndex);
                connection.barPoints.push_back(projection.ToScreen(
                    ChartNormalizedPoint((span->u0 + span->u1) * 0.5, span->v1)));
            }

            connection.points.push_back(projection.ToScreen(
                ChartNormalizedPoint(knots.front().u, knots.front().v)));
            for (size_t i = 0; i + 1 < knots.size(); ++i) {
                const ConnectionKnot& a = knots[i];
                const ConnectionKnot& b = knots[i + 1];
                const double h = b.u - a.u;
                const bool hermite = curved && std::abs(h) > kKnotEpsilon;
                for (int k = 1; k <= samples; ++k) {
                    const double t = static_cast<double>(k) / samples;
                    double v = a.v + (b.v - a.v) * t;
                    if (hermite) {
                        const double t2 = t * t, t3 = t2 * t;
                        v = (2.0 * t3 - 3.0 * t2 + 1.0) * a.v +
                            (t3 - 2.0 * t2 + t) * h * tangent[i] +
                            (-2.0 * t3 + 3.0 * t2) * b.v +
                            (t3 - t2) * h * tangent[i + 1];
                    }
                    connection.points.push_back(projection.ToScreen(
                        ChartNormalizedPoint(a.u + h * t, v)));
                }
            }
            connections.push_back(std::move(connection));
        }
    }
    return connections;
}

// =============================================================================
// LINE AND AREA SERIES
// =============================================================================

namespace {

bool FinitePoint(const ChartNormalizedPoint& p) {
    return std::isfinite(p.u) && std::isfinite(p.v);
}

// The normalised path of one run of knots under the shape: the knots
// themselves come out at the start of every segment, the samples between
// them follow the shape. Every vertex is later mapped through the projection.
std::vector<ChartNormalizedPoint> SampleRun(const std::vector<ChartNormalizedPoint>& run,
                                            const ChartLineOptions& options) {
    std::vector<ChartNormalizedPoint> path;
    if (run.empty()) return path;
    path.push_back(run.front());
    if (run.size() < 2) return path;

    const int samples = std::max(1, options.samplesPerSegment);
    std::vector<double> tangent;
    if (options.shape == ChartLineShape::Curved) {
        std::vector<ConnectionKnot> knots;
        knots.reserve(run.size());
        for (const ChartNormalizedPoint& p : run) knots.push_back({p.u, p.v});
        tangent = MonotoneTangents(knots);
    }

    auto straight = [&](const ChartNormalizedPoint& a, const ChartNormalizedPoint& b) {
        for (int k = 1; k <= samples; ++k) {
            const double t = static_cast<double>(k) / samples;
            path.emplace_back(a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t);
        }
    };

    for (size_t i = 0; i + 1 < run.size(); ++i) {
        const ChartNormalizedPoint& a = run[i];
        const ChartNormalizedPoint& b = run[i + 1];
        switch (options.shape) {
            case ChartLineShape::Straight:
                straight(a, b);
                break;
            case ChartLineShape::Curved: {
                const double h = b.u - a.u;
                if (std::abs(h) <= kKnotEpsilon) { straight(a, b); break; }
                for (int k = 1; k <= samples; ++k) {
                    const double t = static_cast<double>(k) / samples;
                    const double t2 = t * t, t3 = t2 * t;
                    const double v = (2.0 * t3 - 3.0 * t2 + 1.0) * a.v +
                                     (t3 - 2.0 * t2 + t) * h * tangent[i] +
                                     (-2.0 * t3 + 3.0 * t2) * b.v +
                                     (t3 - t2) * h * tangent[i + 1];
                    path.emplace_back(a.u + h * t, v);
                }
                break;
            }
            case ChartLineShape::StepAfter: {
                const ChartNormalizedPoint corner(b.u, a.v);
                straight(a, corner);
                path.push_back(b);
                break;
            }
            case ChartLineShape::StepBefore: {
                const ChartNormalizedPoint corner(a.u, b.v);
                path.push_back(corner);
                straight(corner, b);
                break;
            }
            case ChartLineShape::StepMiddle: {
                const double mid = (a.u + b.u) * 0.5;
                const ChartNormalizedPoint c1(mid, a.v), c2(mid, b.v);
                straight(a, c1);
                path.push_back(c2);
                straight(c2, b);
                break;
            }
        }
    }
    return path;
}

// Splits indices into runs of finite points (or one run when bridging).
std::vector<std::vector<size_t>> FiniteRuns(size_t count,
                                            const std::function<bool(size_t)>& finite,
                                            bool bridgeGaps) {
    std::vector<std::vector<size_t>> runs;
    std::vector<size_t> current;
    for (size_t i = 0; i < count; ++i) {
        if (finite(i)) {
            current.push_back(i);
        } else if (!bridgeGaps && !current.empty()) {
            runs.push_back(std::move(current));
            current.clear();
        }
    }
    if (!current.empty()) runs.push_back(std::move(current));
    return runs;
}

} // namespace

std::vector<ChartLineRun> BuildLineRuns(const IChartProjection& projection,
                                        const std::vector<ChartNormalizedPoint>& points,
                                        const ChartLineOptions& options) {
    std::vector<ChartLineRun> runs;
    for (const std::vector<size_t>& indices :
         FiniteRuns(points.size(), [&](size_t i) { return FinitePoint(points[i]); },
                    options.bridgeGaps)) {
        ChartLineRun run;
        run.indices = indices;
        std::vector<ChartNormalizedPoint> knots;
        knots.reserve(indices.size() + 1);
        for (size_t i : indices) {
            knots.push_back(points[i]);
            run.knots.push_back(projection.ToScreen(points[i]));
        }
        // The seam: one period on, so the last segment travels forward round
        // the circle to the first point instead of back across it.
        if (options.closed && indices.size() == points.size() && knots.size() >= 3) {
            knots.emplace_back(knots.front().u + 1.0, knots.front().v);
        }
        run.path = SampleRun(knots, options);
        if (knots.size() >= 2) {
            run.points.reserve(run.path.size());
            for (const ChartNormalizedPoint& p : run.path) {
                run.points.push_back(projection.ToScreen(p));
            }
        }
        runs.push_back(std::move(run));
    }
    return runs;
}

std::vector<std::vector<Point2Dd>> BuildAreaOutlines(const IChartProjection& projection,
                                                     const std::vector<ChartNormalizedPoint>& top,
                                                     const std::vector<ChartNormalizedPoint>& base,
                                                     const ChartLineOptions& options) {
    std::vector<std::vector<Point2Dd>> outlines;
    const size_t count = std::min(top.size(), base.size());
    for (const std::vector<size_t>& indices :
         FiniteRuns(count,
                    [&](size_t i) { return FinitePoint(top[i]) && FinitePoint(base[i]); },
                    options.bridgeGaps)) {
        if (indices.size() < 2) continue;
        std::vector<ChartNormalizedPoint> topKnots, baseKnots;
        for (size_t i : indices) {
            topKnots.push_back(top[i]);
            baseKnots.push_back(base[i]);
        }
        if (options.closed && indices.size() == count && topKnots.size() >= 3) {
            topKnots.emplace_back(topKnots.front().u + 1.0, topKnots.front().v);
            baseKnots.emplace_back(baseKnots.front().u + 1.0, baseKnots.front().v);
        }
        const std::vector<ChartNormalizedPoint> topPath = SampleRun(topKnots, options);
        const std::vector<ChartNormalizedPoint> basePath = SampleRun(baseKnots, options);

        std::vector<Point2Dd> outline;
        outline.reserve(topPath.size() + basePath.size());
        for (const ChartNormalizedPoint& p : topPath) outline.push_back(projection.ToScreen(p));
        for (auto it = basePath.rbegin(); it != basePath.rend(); ++it) {
            outline.push_back(projection.ToScreen(*it));
        }
        outlines.push_back(std::move(outline));
    }
    return outlines;
}

ChartStackedValues StackSeriesValues(const std::vector<std::vector<double>>& series,
                                     ChartBarArrangement arrangement) {
    ChartStackedValues out;
    const size_t count = CategoryCountOf(series);
    const double nan = std::nan("");
    out.base.assign(series.size(), std::vector<double>(count, nan));
    out.top.assign(series.size(), std::vector<double>(count, nan));

    for (size_t i = 0; i < count; ++i) {
        if (arrangement == ChartBarArrangement::Grouped) {
            for (size_t s = 0; s < series.size(); ++s) {
                if (i >= series[s].size() || !std::isfinite(series[s][i])) continue;
                out.base[s][i] = 0.0;
                out.top[s][i] = series[s][i];
            }
            continue;
        }

        double scale = 1.0;
        if (arrangement == ChartBarArrangement::PercentStacked) {
            double positive = 0.0, negative = 0.0;
            CategoryTotals(series, i, positive, negative);
            const double total = positive - negative;
            scale = (total > 0.0) ? 100.0 / total : 0.0;
        }
        double up = 0.0, down = 0.0;
        for (size_t s = 0; s < series.size(); ++s) {
            double v = (i < series[s].size()) ? series[s][i] : 0.0;
            if (!std::isfinite(v)) v = 0.0;
            v *= scale;
            if (v >= 0.0) {
                out.base[s][i] = up;
                up += v;
                out.top[s][i] = up;
            } else {
                out.base[s][i] = down;
                down += v;
                out.top[s][i] = down;
            }
        }
    }
    return out;
}

std::vector<Point2Dd> ChartMarkerPolygon(ChartMarkerShape shape, const Point2Dd& c,
                                         double r) {
    switch (shape) {
        case ChartMarkerShape::Square: {
            const double h = r * 0.82;      // equal visual weight to the circle
            return {{c.x - h, c.y - h}, {c.x + h, c.y - h},
                    {c.x + h, c.y + h}, {c.x - h, c.y + h}};
        }
        case ChartMarkerShape::Diamond:
            return {{c.x, c.y - r * 1.1}, {c.x + r * 1.1, c.y},
                    {c.x, c.y + r * 1.1}, {c.x - r * 1.1, c.y}};
        case ChartMarkerShape::TriangleUp:
            return {{c.x, c.y - r * 1.15}, {c.x + r, c.y + r * 0.75},
                    {c.x - r, c.y + r * 0.75}};
        case ChartMarkerShape::TriangleDown:
            return {{c.x, c.y + r * 1.15}, {c.x - r, c.y - r * 0.75},
                    {c.x + r, c.y - r * 0.75}};
        case ChartMarkerShape::NoMarker:
        case ChartMarkerShape::Circle:
        case ChartMarkerShape::Cross:
        case ChartMarkerShape::Plus:
        default:
            return {};
    }
}

} // namespace UltraCanvas

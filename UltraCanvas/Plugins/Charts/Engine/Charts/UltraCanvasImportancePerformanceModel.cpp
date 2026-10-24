// Plugins/Charts/Engine/Charts/UltraCanvasImportancePerformanceModel.cpp
// The UI-free model of the importance-performance chart.
// Version: 1.0.0
// Last Modified: 2026-10-24
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasImportancePerformanceModel.h"
#include "Plugins/Charts/Engine/UltraCanvasChartHighlights.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {

namespace {

bool Finite(double v) { return std::isfinite(v); }

double SegmentDistance(const Point2Dd& p, const Point2Dd& a, const Point2Dd& b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double lengthSq = dx * dx + dy * dy;
    double t = 0.0;
    if (lengthSq > 0.0) t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSq, 0.0, 1.0);
    const double cx = a.x + dx * t - p.x, cy = a.y + dy * t - p.y;
    return std::sqrt(cx * cx + cy * cy);
}

double PolygonEdgeDistance(const std::vector<Point2Dd>& polygon, const Point2Dd& p) {
    double best = std::numeric_limits<double>::max();
    for (size_t i = 0; i < polygon.size(); ++i) {
        best = std::min(best, SegmentDistance(p, polygon[i], polygon[(i + 1) % polygon.size()]));
    }
    return best;
}

std::string Describe(double v) {
    // Validation messages only; a person reads them.
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f", v);   // locale-ok: diagnostic text, not a file format
    return text;
}

void ValidateAxis(const ImportancePerformanceAxis& axis, const std::string& name,
                  std::vector<std::string>& problems) {
    if (!Finite(axis.min) || !Finite(axis.max) || axis.min >= axis.max) {
        problems.push_back(name + " axis: the range is empty or reversed");
        return;
    }
    double previousEnd = -std::numeric_limits<double>::max();
    for (const ImportancePerformanceLevel& level : axis.levels) {
        if (!(level.from < level.to)) {
            problems.push_back(name + " axis: level \"" + level.label + "\" has an empty or reversed range");
            continue;
        }
        if (level.from < axis.min - 1e-9 || level.to > axis.max + 1e-9) {
            problems.push_back(name + " axis: level \"" + level.label + "\" runs outside the axis range");
        }
        if (level.from < previousEnd - 1e-9) {
            problems.push_back(name + " axis: level \"" + level.label +
                               "\" overlaps the one before it, or the levels are out of order");
        }
        previousEnd = std::max(previousEnd, level.to);
    }
}

} // namespace

// =============================================================================
// GEOMETRY
// =============================================================================

std::vector<Point2Dd> SmoothPolylineChaikin(const std::vector<Point2Dd>& line, int iterations) {
    std::vector<Point2Dd> result = line;
    if (line.size() < 3) return result;
    for (int it = 0; it < iterations; ++it) {
        std::vector<Point2Dd> next;
        next.reserve(result.size() * 2);
        next.push_back(result.front());
        for (size_t i = 0; i + 1 < result.size(); ++i) {
            const Point2Dd& a = result[i];
            const Point2Dd& b = result[i + 1];
            next.emplace_back(0.75 * a.x + 0.25 * b.x, 0.75 * a.y + 0.25 * b.y);
            next.emplace_back(0.25 * a.x + 0.75 * b.x, 0.25 * a.y + 0.75 * b.y);
        }
        next.push_back(result.back());
        result = std::move(next);
    }
    return result;
}

bool PolygonContains(const std::vector<Point2Dd>& polygon, const Point2Dd& point) {
    if (polygon.size() < 3) return false;
    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Point2Dd& a = polygon[i];
        const Point2Dd& b = polygon[j];
        if ((a.y > point.y) != (b.y > point.y) &&
            point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}

Point2Dd PoleOfInaccessibility(const std::vector<Point2Dd>& region,
                               const std::vector<std::vector<Point2Dd>>& holes,
                               const Rect2Dd& clip, double* outRadius,
                               const std::vector<Point2Dd>& avoid) {
    if (outRadius) *outRadius = 0.0;
    if (region.size() < 3 || clip.width <= 0.0 || clip.height <= 0.0) return Point2Dd(0.0, 0.0);

    // Search the region's bounding box, cut to the clip.
    double x0 = region[0].x, y0 = region[0].y, x1 = x0, y1 = y0;
    for (const Point2Dd& p : region) {
        x0 = std::min(x0, p.x); x1 = std::max(x1, p.x);
        y0 = std::min(y0, p.y); y1 = std::max(y1, p.y);
    }
    x0 = std::max(x0, clip.x);
    y0 = std::max(y0, clip.y);
    x1 = std::min(x1, clip.x + clip.width);
    y1 = std::min(y1, clip.y + clip.height);
    if (x0 >= x1 || y0 >= y1) return Point2Dd(0.0, 0.0);

    // Distance to the nearest drawn edge: the region's, a hole's, or the clip's
    // (any edge outside the clip is farther than the clip's own edge).
    auto score = [&](const Point2Dd& p) -> double {
        if (p.x < clip.x || p.y < clip.y || p.x > clip.x + clip.width || p.y > clip.y + clip.height) return -1.0;
        if (!PolygonContains(region, p)) return -1.0;
        for (const std::vector<Point2Dd>& hole : holes) {
            if (PolygonContains(hole, p)) return -1.0;
        }
        double d = std::min({p.x - clip.x, clip.x + clip.width - p.x,
                             p.y - clip.y, clip.y + clip.height - p.y});
        d = std::min(d, PolygonEdgeDistance(region, p));
        for (const std::vector<Point2Dd>& hole : holes) {
            if (hole.size() >= 3) d = std::min(d, PolygonEdgeDistance(hole, p));
        }
        for (const Point2Dd& a : avoid) d = std::min(d, std::hypot(a.x - p.x, a.y - p.y));
        return d;
    };

    const int steps = 32;
    double stepX = (x1 - x0) / steps, stepY = (y1 - y0) / steps;
    Point2Dd best(0.0, 0.0);
    double bestScore = -1.0;
    for (int iy = 0; iy <= steps; ++iy) {
        for (int ix = 0; ix <= steps; ++ix) {
            const Point2Dd p(x0 + stepX * ix, y0 + stepY * iy);
            const double s = score(p);
            if (s > bestScore) { bestScore = s; best = p; }
        }
    }
    if (bestScore < 0.0) return Point2Dd(0.0, 0.0);

    // Refine around the best cell: a finer grid, three times.
    for (int round = 0; round < 3; ++round) {
        const Point2Dd centre = best;
        const int fine = 8;
        for (int iy = -fine; iy <= fine; ++iy) {
            for (int ix = -fine; ix <= fine; ++ix) {
                const Point2Dd p(centre.x + stepX * ix / fine, centre.y + stepY * iy / fine);
                const double s = score(p);
                if (s > bestScore) { bestScore = s; best = p; }
            }
        }
        stepX /= fine / 2.0;
        stepY /= fine / 2.0;
    }
    if (outRadius) *outRadius = bestScore;
    return best;
}

// =============================================================================
// MODEL
// =============================================================================

std::vector<Point2Dd> ImportancePerformanceModel::ZoneShape(size_t zoneIndex) const {
    if (zoneIndex >= zones.size()) return {};
    const ImportancePerformanceZone& zone = zones[zoneIndex];
    if (zone.outline.size() < 3) return {};
    return SmoothPolygonChaikin(zone.outline, std::clamp(zone.smoothing, 0, 6));
}

std::vector<Point2Dd> ImportancePerformanceModel::BandShape(size_t bandIndex) const {
    if (bandIndex >= bands.size()) return {};
    const ImportancePerformanceBand& band = bands[bandIndex];
    return SmoothPolylineChaikin(band.line, std::clamp(band.smoothing, 0, 6));
}

int ImportancePerformanceModel::ZoneOf(double importanceScore, double performanceScore) const {
    if (!Finite(importanceScore) || !Finite(performanceScore)) return -1;
    const Point2Dd p(importanceScore, performanceScore);
    for (size_t i = zones.size(); i-- > 0;) {
        if (PolygonContains(ZoneShape(i), p)) return static_cast<int>(i);
    }
    return -1;
}

std::vector<int> ImportancePerformanceModel::ClassifyFactors() const {
    // Smooth each outline once, not once per factor.
    std::vector<std::vector<Point2Dd>> shapes;
    shapes.reserve(zones.size());
    for (size_t i = 0; i < zones.size(); ++i) shapes.push_back(ZoneShape(i));

    std::vector<int> result;
    result.reserve(factors.size());
    for (const ImportancePerformanceFactor& f : factors) {
        int zone = -1;
        if (Finite(f.importance) && Finite(f.performance)) {
            const Point2Dd p(f.importance, f.performance);
            for (size_t i = shapes.size(); i-- > 0;) {
                if (PolygonContains(shapes[i], p)) { zone = static_cast<int>(i); break; }
            }
        }
        result.push_back(zone);
    }
    return result;
}

int ImportancePerformanceModel::LevelOf(const ImportancePerformanceAxis& axis, double value) {
    if (!Finite(value)) return -1;
    for (size_t i = 0; i < axis.levels.size(); ++i) {
        const ImportancePerformanceLevel& level = axis.levels[i];
        // Half-open, except that the last level keeps its upper edge.
        const bool last = (i + 1 == axis.levels.size());
        if (value >= level.from && (value < level.to || (last && value <= level.to))) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<std::string> ImportancePerformanceModel::Validate() const {
    std::vector<std::string> problems;
    ValidateAxis(importance, "Importance", problems);
    ValidateAxis(performance, "Performance", problems);
    for (size_t i = 0; i < zones.size(); ++i) {
        if (zones[i].outline.size() < 3) {
            problems.push_back("Zone \"" + zones[i].title + "\" has fewer than three points");
        }
    }
    for (size_t i = 0; i < bands.size(); ++i) {
        if (bands[i].line.size() < 2) {
            problems.push_back("Band \"" + bands[i].label + "\" has fewer than two points");
        }
    }
    for (const ImportancePerformanceFactor& f : factors) {
        const std::string name = f.label.empty() ? f.id : f.label;
        if (!Finite(f.importance) || !Finite(f.performance)) {
            problems.push_back("Factor \"" + name + "\" is missing a score");
            continue;
        }
        if (f.importance < importance.min || f.importance > importance.max) {
            problems.push_back("Factor \"" + name + "\" has importance " + Describe(f.importance) +
                               ", outside the axis range");
        }
        if (f.performance < performance.min || f.performance > performance.max) {
            problems.push_back("Factor \"" + name + "\" has performance " + Describe(f.performance) +
                               ", outside the axis range");
        }
    }
    return problems;
}

// =============================================================================
// PRESETS
// =============================================================================

namespace ImportancePerformanceSamples {

ImportancePerformanceModel SlackZones(bool smooth) {
    ImportancePerformanceModel model;
    model.title = "Importance-performance matrix";

    // Slack's nine-point scales, 1 = best, read in three named thirds.
    model.importance.title = "Importance for customers";
    model.importance.lowCaption = "Low";
    model.importance.highCaption = "High";
    model.importance.inverted = true;
    model.importance.levels = {{"Order winning", 0.5, 3.5},
                               {"Qualifying", 3.5, 6.5},
                               {"Less important", 6.5, 9.5}};
    model.performance.title = "Performance against competitors";
    model.performance.lowCaption = "Bad";
    model.performance.highCaption = "Good";
    model.performance.inverted = true;
    model.performance.levels = {{"Better than", 0.5, 3.5},
                                {"Same as", 3.5, 6.5},
                                {"Worse than", 6.5, 9.5}};

    // The boundaries are written in display terms - x grows with importance
    // to the right, y with performance upwards, 0.5 .. 9.5 - and turned into
    // Slack's numbering (10 - d) below. Every chain starts and ends past the
    // plot (-2.5 / 12.5), so smoothing never rounds off a visible corner.
    using Chain = std::vector<Point2Dd>;
    const Chain acceptability = smooth
        ? Chain{{-2.5, 1.2}, {1.5, 2.0}, {4.0, 3.4}, {6.5, 5.2}, {9.0, 6.4}, {12.5, 6.9}}
        : Chain{{-2.5, 1.5}, {1.5, 1.5}, {1.5, 2.5}, {3.5, 2.5}, {3.5, 3.5}, {4.5, 3.5},
                {4.5, 4.5}, {5.5, 4.5}, {5.5, 5.5}, {7.5, 5.5}, {7.5, 6.5}, {12.5, 6.5}};
    const Chain urgent = smooth
        ? Chain{{5.0, -2.5}, {6.0, 1.0}, {7.5, 2.8}, {9.5, 4.0}, {12.5, 4.4}}
        : Chain{{5.5, -2.5}, {5.5, 1.5}, {6.5, 1.5}, {6.5, 2.5}, {7.5, 2.5}, {7.5, 3.5},
                {8.5, 3.5}, {8.5, 4.5}, {12.5, 4.5}};
    const Chain excess = smooth
        ? Chain{{-2.5, 5.2}, {1.0, 5.8}, {2.6, 7.2}, {3.8, 8.8}, {4.6, 12.5}}
        : Chain{{-2.5, 5.5}, {1.5, 5.5}, {1.5, 6.5}, {2.5, 6.5}, {2.5, 7.5}, {3.5, 7.5},
                {3.5, 8.5}, {4.5, 8.5}, {4.5, 12.5}};
    const double lo = -2.5, hi = 12.5;
    auto slack = [](Chain chain) {
        for (Point2Dd& p : chain) p = Point2Dd(10.0 - p.x, 10.0 - p.y);
        return chain;
    };
    auto closed = [](Chain chain, std::initializer_list<Point2Dd> tail) {
        chain.insert(chain.end(), tail);
        return chain;
    };
    const int smoothing = smooth ? 4 : 0;

    ImportancePerformanceZone appropriate;
    appropriate.id = "appropriate";
    appropriate.title = "Appropriate";
    appropriate.action = "Performance matches importance: maintain it";
    appropriate.fill = Color(0, 158, 168, 255);
    appropriate.outline = slack({{lo, lo}, {hi, lo}, {hi, hi}, {lo, hi}});

    ImportancePerformanceZone improve;
    improve.id = "improve";
    improve.title = "Improve";
    improve.action = "Below the lower bound of acceptability: raise performance";
    improve.fill = Color(214, 34, 76, 255);
    improve.outline = slack(closed(acceptability, {{hi, lo}, {lo, lo}}));
    improve.smoothing = smoothing;

    ImportancePerformanceZone urgentAction;
    urgentAction.id = "urgent";
    urgentAction.title = "Urgent Action";
    urgentAction.action = "Important and well below competitors: improve first";
    urgentAction.fill = Color(244, 146, 30, 255);
    urgentAction.outline = slack(closed(urgent, {{hi, lo}}));
    urgentAction.smoothing = smoothing;

    ImportancePerformanceZone excessive;
    excessive.id = "excess";
    excessive.title = "Excess?";
    excessive.action = "Better than the factor's importance needs: resources to move elsewhere?";
    excessive.fill = Color(176, 188, 34, 255);
    excessive.textColor = Color(40, 44, 10, 255);
    excessive.outline = slack(closed(excess, {{lo, hi}}));
    excessive.smoothing = smoothing;

    model.zones = {appropriate, improve, urgentAction, excessive};

    ImportancePerformanceBand band;
    band.label = "Lower Bound of Acceptability";
    band.line = slack(acceptability);
    band.smoothing = smoothing;
    band.width = smooth ? 0.45 : 0.14;
    model.bands = {band};
    return model;
}

std::vector<ImportancePerformanceFactor> SampleFactors() {
    // Slack's numbering: 1 = most important / far better than competitors.
    std::vector<ImportancePerformanceFactor> factors = {
        {"Delivery reliability", "Delivery", 2.0, 8.0},
        {"Damage claims", "Service", 3.2, 8.5},
        {"Call centre wait", "Service", 3.6, 9.2},
        {"Price", "Delivery", 1.4, 5.0},
        {"Returns handling", "Service", 3.0, 7.0},
        {"Packaging", "Delivery", 7.0, 8.2},
        {"Delivery speed", "Delivery", 2.0, 2.0},
        {"Tracking app", "Service", 5.0, 3.0},
        {"Courier courtesy", "Service", 6.0, 4.5},
        {"Weekend delivery", "Delivery", 7.4, 3.0},
        {"Gift wrapping", "Service", 8.4, 2.0},
        {"Branded uniforms", "Service", 9.0, 4.0},
    };
    for (size_t i = 0; i < factors.size(); ++i) factors[i].id = "f" + std::to_string(i + 1);
    return factors;
}

} // namespace ImportancePerformanceSamples

} // namespace UltraCanvas

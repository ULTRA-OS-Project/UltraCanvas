// include/Plugins/Charts/Engine/Charts/UltraCanvasImportancePerformanceModel.h
// The UI-free model of the importance-performance chart: two scored axes with
// named level ranges, authored zones (Slack's "urgent action / improve /
// appropriate / excess?"), labelled boundary bands, and the factors plotted
// over them. Classification uses the same smoothed outline the chart draws,
// so a factor drawn inside a zone is always reported in it.
//
// Coordinates are value space: x is importance, y is performance, both in
// the units of their axes. Nothing here knows about pixels except
// PoleOfInaccessibility, which works in whatever space it is given.
//
// Design: Docs/UltraCanvas/UltraCanvasImportancePerformanceChartProposal.md
// Guide:  Docs/UltraCanvas/UltraCanvasImportancePerformanceChart.md
//
// Version: 1.0.0
// Last Modified: 2026-10-24
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <limits>
#include <string>
#include <vector>

namespace UltraCanvas {

// A named range of one axis: "Order winning" covers 6.5 .. 9.5.
struct ImportancePerformanceLevel {
    std::string label;
    double from = 0.0;
    double to   = 0.0;

    double Centre() const { return (from + to) / 2.0; }
};

struct ImportancePerformanceAxis {
    std::string title;                      // "Importance for customer"
    std::string lowCaption;                 // at the screen's low end: "Low", "Bad"
    std::string highCaption;                // at the screen's high end: "High", "Good"
    std::vector<ImportancePerformanceLevel> levels;   // in value order
    double min = 0.5;
    double max = 9.5;
    // Slack's scales number the best grade 1, so 1 is drawn at the far end
    // (right, top). The captions stay with the screen ends either way.
    bool inverted = false;
};

struct ImportancePerformanceZone {
    std::string id;                         // "urgent"
    std::string title;                      // "Urgent Action"
    std::string action;                     // tooltip: "Improve before anything else"
    std::string iconPath;                   // drawn above the title; empty = none
    Color fill = Color(0, 0, 0, 0);         // transparent = from the theme palette
    Color textColor = Color(255, 255, 255, 255);
    // Closed, in value space. May run past the axis ranges: the chart clips
    // to the plot, and an outline that ends outside it keeps smoothing from
    // rounding off the plot's corners.
    std::vector<Point2Dd> outline;
    int smoothing = 0;                      // Chaikin iterations: 0 = as given, 1..6
};

// A labelled boundary drawn over the zones: Slack's lower bound of
// acceptability. Only drawn; it plays no part in classification.
struct ImportancePerformanceBand {
    std::string label;                      // "Lower Bound of Acceptability"
    std::vector<Point2Dd> line;             // open polyline, value space
    int smoothing = 0;                      // Chaikin iterations, ends kept
    double width = 0.4;                     // in y-axis value units
    Color fill = Color(52, 101, 200, 235);
    Color textColor = Color(255, 255, 255, 255);
};

struct ImportancePerformanceFactor {
    std::string id;
    std::string label;                      // "Delivery speed"
    std::string group;                      // "Logistics"; one scatter group per name
    double importance  = std::numeric_limits<double>::quiet_NaN();
    double performance = std::numeric_limits<double>::quiet_NaN();
    std::string note;                       // appended to the tooltip

    ImportancePerformanceFactor() = default;
    ImportancePerformanceFactor(std::string factorLabel, std::string factorGroup,
                                double importanceScore, double performanceScore)
        : label(std::move(factorLabel)), group(std::move(factorGroup)),
          importance(importanceScore), performance(performanceScore) {}
};

struct ImportancePerformanceModel {
    std::string title;
    ImportancePerformanceAxis importance;               // x
    ImportancePerformanceAxis performance;              // y
    std::vector<ImportancePerformanceZone> zones;       // later zones cover earlier ones
    std::vector<ImportancePerformanceBand> bands;
    std::vector<ImportancePerformanceFactor> factors;

    // The zone's outline after its smoothing - the polygon the chart draws
    // and classification tests. Empty when the outline has fewer than three
    // points.
    std::vector<Point2Dd> ZoneShape(size_t zoneIndex) const;
    // The band's centre line after its smoothing.
    std::vector<Point2Dd> BandShape(size_t bandIndex) const;

    // The zone the point is in, -1 = none. Even-odd test against ZoneShape;
    // the last zone that contains the point wins, as it does in the drawing.
    int ZoneOf(double importanceScore, double performanceScore) const;
    // One zone index per factor (-1 for a factor in no zone or without both
    // scores).
    std::vector<int> ClassifyFactors() const;
    // The level of an axis a value falls in, -1 = none.
    static int LevelOf(const ImportancePerformanceAxis& axis, double value);

    // Problems that make the model unusable or misleading: an empty or
    // reversed range, a zone with fewer than three points, overlapping or
    // out-of-range levels, a factor outside the axis ranges. Empty = valid.
    std::vector<std::string> Validate() const;
};

// ===== GEOMETRY (UI-free) =====

// Chaikin corner cutting on an open polyline: like SmoothPolygonChaikin
// (Engine/UltraCanvasChartHighlights.h) on the interior, but the two end
// points stay where they are. An interior stretch therefore comes out exactly
// as the closed version smooths it, so a band laid over a zone edge follows
// the edge.
std::vector<Point2Dd> SmoothPolylineChaikin(const std::vector<Point2Dd>& line, int iterations);

// Even-odd point-in-polygon test.
bool PolygonContains(const std::vector<Point2Dd>& polygon, const Point2Dd& point);

// The point of `region` farthest from its edges and from the edges of every
// polygon in `holes` (the zones drawn on top of it), and not inside any hole:
// where a title fits best in the part of the zone that is visible. It is not
// the centroid, which can fall outside a concave zone or under a later one.
// Grid search with refinement; `outRadius` (optional) receives the distance
// to the nearest edge, 0 when no such point exists. `clip` bounds the search
// (the plot area): edges outside it do not count, as they are not drawn.
// `avoid` are points to keep clear of as if each were an edge (the factors'
// marks, so a title does not cover one).
Point2Dd PoleOfInaccessibility(const std::vector<Point2Dd>& region,
                               const std::vector<std::vector<Point2Dd>>& holes,
                               const Rect2Dd& clip, double* outRadius = nullptr,
                               const std::vector<Point2Dd>& avoid = {});

// ===== PRESETS =====

namespace ImportancePerformanceSamples {

    // Slack's importance-performance matrix on his nine-point scales: 1 is
    // the best grade on both (strongly order-winning, considerably better
    // than competitors), so both axes are inverted. Four zones and the lower
    // bound of acceptability. `smooth` = false gives the textbook staircase,
    // true the rounded slide form (reference image 4).
    ImportancePerformanceModel SlackZones(bool smooth);

    // Invented service factors of a parcel carrier, in two groups, spread
    // over all four zones - for the demo and the guide.
    std::vector<ImportancePerformanceFactor> SampleFactors();

} // namespace ImportancePerformanceSamples

} // namespace UltraCanvas

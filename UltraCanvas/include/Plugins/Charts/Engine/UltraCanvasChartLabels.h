// include/Plugins/Charts/Engine/UltraCanvasChartLabels.h
// Chart-facing label layer: the per-chart-type policy that decides which label
// classes take part in collision solving, and the solved plan the draw path
// reads from.
//
// The plan is built when the chart is created or invalidated and read on every
// redraw; the solver never runs inside a paint handler. Sits on top of
// UltraCanvasLabelPlacement and adds no UI dependency of its own.
//
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartTypes.h"
#include "UltraCanvasLabelPlacement.h"
#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// LABEL CLASSES AND POLICY
// =============================================================================

enum class ChartLabelClass : uint32_t {
    AxisTick       = 1u << 0,
    AxisTitle      = 1u << 1,
    GridLabel      = 1u << 2,
    ValueLabel     = 1u << 3,   // "graph values" - the per-datum numbers
    SeriesName     = 1u << 4,
    LimiterCaption = 1u << 5,
    HighlightLabel = 1u << 6,
    LegendEntry    = 1u << 7,
    Annotation     = 1u << 8
};

inline uint32_t operator|(ChartLabelClass a, ChartLabelClass b) {
    return static_cast<uint32_t>(a) | static_cast<uint32_t>(b);
}
inline uint32_t operator|(uint32_t a, ChartLabelClass b) {
    return a | static_cast<uint32_t>(b);
}
inline bool HasClass(uint32_t set, ChartLabelClass c) {
    return (set & static_cast<uint32_t>(c)) != 0u;
}

// Which label classes are solved, which merely block, and which are left alone.
// Axis furniture is excluded by default: it sits at constructed positions in
// the margin bands where it cannot collide with in-plot labels, and it is
// decluttered by the cheap 1-D pass instead. Chart types that place axis
// furniture *inside* the plot - parallel coordinate axis headers, heatmap row
// and column labels - promote it to an obstacle.
struct ChartLabelPolicy {
    uint32_t solved = ChartLabelClass::ValueLabel | ChartLabelClass::SeriesName |
                      ChartLabelClass::LimiterCaption | ChartLabelClass::HighlightLabel |
                      ChartLabelClass::Annotation;
    uint32_t obstacles = static_cast<uint32_t>(ChartLabelClass::LegendEntry);
    uint32_t excluded = ChartLabelClass::AxisTick | ChartLabelClass::AxisTitle |
                        ChartLabelClass::GridLabel;

    static ChartLabelPolicy Default() { return ChartLabelPolicy{}; }

    // Axis headers sit inside the plot band, so in-plot labels must avoid them.
    static ChartLabelPolicy ParallelCoordinates();
    // Row and column labels sit against the cell grid.
    static ChartLabelPolicy Heatmap();
    // Spoke labels ring the plot at arbitrary angles and genuinely collide.
    static ChartLabelPolicy Radial();

    // Moves a class into one of the three roles, clearing it from the others.
    void SetRole(ChartLabelClass c, bool isSolved, bool isObstacle);
};

// =============================================================================
// REQUESTS AND RESULTS
// =============================================================================

// One label a chart wants drawn. Text is already measured - the engine never
// measures text itself, exactly as the placement solver never has.
struct ChartLabelRequest {
    std::string text;
    ChartLabelClass klass = ChartLabelClass::ValueLabel;
    Point2Dd anchor;                 // the point or mark the label belongs to
    Size2Dd textSize;
    double anchorRadius = 0.0;       // size of the mark, so the label clears it
    Rect2Dd fixedBounds;             // obstacle / excluded classes: where it already is
    LabelSide preferredSide = LabelSide::Auto;
    double rotationDegrees = 0.0;
    int priority = 0;
    bool allowSuppress = false;
    bool wantLeaderLine = false;
    size_t userData = 0;             // caller's own index, echoed back untouched
};

struct PlacedChartLabel {
    std::string text;
    Rect2Dd bounds;
    ChartLabelClass klass = ChartLabelClass::ValueLabel;
    double rotationDegrees = 0.0;
    bool suppressed = false;
    bool hasLeader = false;
    Point2Dd leaderFrom;
    size_t userData = 0;
};

// The solved output. Built once and read on every redraw.
struct ChartLabelPlan {
    std::vector<PlacedChartLabel> labels;
    // Per axis, per tick: whether the tick label survived the 1-D declutter.
    std::vector<std::vector<bool>> tickVisible;
    uint64_t generation = 0;

    void Clear() { labels.clear(); tickVisible.clear(); }
    size_t DrawableCount() const;
};

// =============================================================================
// BROKER
// =============================================================================

// Routes label requests by class, runs the solver once, and hands back the plan.
// One broker per solve; the chart keeps the resulting plan, not the broker.
class ChartLabelBroker {
public:
    ChartLabelBroker(const ChartLabelPolicy& policy, const LabelPlacementOptions& options);

    // Geometry labels must not cover: the series lines themselves, connectors,
    // markers. Passed straight through to the placement session.
    void AddObstacleRect(const Rect2Dd& rect);
    void AddObstaclePolyline(const std::vector<Point2Dd>& points, double halfWidth);

    void Add(const ChartLabelRequest& request);
    void Add(const std::vector<ChartLabelRequest>& requests);

    // Runs the solver for the solved classes, passes the rest through, and
    // returns the plan. Requests added after this are not included.
    ChartLabelPlan Solve(uint64_t generation = 0);

private:
    ChartLabelPolicy policy;
    LabelPlacementOptions options;
    LabelPlacementSession session;
    std::vector<ChartLabelRequest> requests;
};

} // namespace UltraCanvas

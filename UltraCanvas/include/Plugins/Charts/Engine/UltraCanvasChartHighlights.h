// include/Plugins/Charts/Engine/UltraCanvasChartHighlights.h
// The chart engine's highlight layer - the file the engine proposal reserves
// (§5.2, §8.2): group washes drawn under the grid (slot 200) or as overlays
// above the content (slot 700). Seven shapes: explicit rectangles and
// ellipses in value space, value bands on one axis, and the computed group
// shapes - confidence ellipses (covariance eigen decomposition, the
// 50%/95%-around-grouped-dots convention), padded convex hulls, smoothed
// blobs, and per-point halos.
//
// The geometry helpers are UI-free and unit-tested (Tests/ChartEngineTest);
// the engine element maps value-space input through its axes and projection
// and draws the shapes (UltraCanvasChartEngineElement::AddHighlight).
//
// Version: 1.0.0
// Last Modified: 2026-08-20
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// SHAPES
// =============================================================================

enum class ChartHighlightShape {
    Rectangle,          // explicit rect in value space, translucent wash
    Ellipse,            // explicit rect's inscribed ellipse
    ConfidenceEllipse,  // computed from members: covariance ellipse at `confidence`
    Hull,               // computed: padded convex hull polygon of members
    Blob,               // computed: Chaikin-smoothed padded hull, cluster-hugging
    ValueBand,          // [bandLow, bandHigh] on one axis, full plot extent across
    PointHalo           // a translucent disc of `padding` px around each member
};

// One highlight. Computed shapes derive their geometry from `members` -
// value-space points, x mapped through the axis at `xAxisIndex` (the axis
// driving the projection's domain/u coordinate) and y through `yAxisIndex`
// (the value/v coordinate). Explicit shapes use `rectValueSpace` under the
// same mapping; ValueBand uses `axisIndex` + bandLow/bandHigh alone.
struct ChartHighlight {
    ChartHighlightShape shape = ChartHighlightShape::Rectangle;

    std::vector<Point2Dd> members;      // computed shapes
    Rect2Dd rectValueSpace;             // Rectangle / Ellipse
    size_t axisIndex = 0;               // ValueBand's axis
    double bandLow = 0.0;
    double bandHigh = 0.0;

    size_t xAxisIndex = 1;              // axis feeding the u coordinate
    size_t yAxisIndex = 0;              // axis feeding the v coordinate

    double confidence = 0.95;           // ConfidenceEllipse level in (0, 1)
    bool showMeanMarker = true;         // ConfidenceEllipse: dot at the mean
    double padding = 18.0;              // Hull/Blob outward pad, PointHalo radius (px)
    double smoothing = 0.6;             // Blob: 0..1 -> Chaikin iterations

    Color fill = Color(100, 140, 220, 40);
    Color stroke = Color(100, 140, 220, 180);
    float strokeWidth = 1.5f;
    bool dashed = false;

    std::string label;                  // rides the label plan as HighlightLabel
    Color labelColor = Color(80, 80, 80, 255);

    int zSlot = 200;                    // 200 = wash under the grid, 700 = overlay
};

// =============================================================================
// GEOMETRY (screen-space, UI-free, unit-tested)
// =============================================================================

struct ChartEllipseSpec {
    Point2Dd center;
    double rx = 0.0;                    // semi-axis along `rotationRadians`
    double ry = 0.0;
    double rotationRadians = 0.0;
    bool valid = false;
};

// The covariance ellipse containing `confidence` of a 2D Gaussian fitted to
// the points (chi-square quantile with 2 degrees of freedom). Needs at least
// three points; degenerate (collinear) sets yield a near-zero minor axis.
ChartEllipseSpec ComputeConfidenceEllipse(const std::vector<Point2Dd>& points,
                                          double confidence);

// Convex hull (monotone chain), counter-clockwise, no repeated last point.
// Fewer than three distinct points return the input unchanged.
std::vector<Point2Dd> ComputeConvexHull(std::vector<Point2Dd> points);

// Every vertex moved `padding` away from the polygon centroid, so the shape
// clears the marks it is hugging.
std::vector<Point2Dd> ExpandPolygon(const std::vector<Point2Dd>& polygon,
                                    double padding);

// Chaikin corner cutting on a closed polygon: each iteration replaces every
// edge with its 1/4 and 3/4 points, rounding the outline.
std::vector<Point2Dd> SmoothPolygonChaikin(const std::vector<Point2Dd>& polygon,
                                           int iterations);

} // namespace UltraCanvas

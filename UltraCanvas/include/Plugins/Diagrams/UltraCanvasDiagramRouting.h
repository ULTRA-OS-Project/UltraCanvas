// include/Plugins/Diagrams/UltraCanvasDiagramRouting.h
// Shared orthogonal connection routing for the diagram family
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework
//
// This header is the single home of the obstacle-aware orthogonal router that
// was developed inside UltraCanvasFlowChart (v2.1.4 cardinal L/Z routing,
// v2.2.0 A* pathfinding). It is UI-free — it depends only on
// UltraCanvasCommonTypes.h — so it can be unit-tested without a window and
// reused by every node-and-edge component (FlowChart, BlockDiagram,
// ClassDiagram, ...).
//
// The router knows nothing about nodes. Callers convert their own node type
// into a list of DiagramObstacle rectangles and pre-filter the ones a path is
// allowed to touch (normally the source and target nodes themselves).
//
// CHANGELOG 1.0.0:
//  - Extracted from UltraCanvasFlowChart.cpp:
//        ComputeCardinalPath, PathHasObstacles, RouteAStar,
//        ComputeOrthogonalPath, GetCardinalSide, GetCardinalPoint,
//        ComputeOrthogonalLabelAnchor (renamed ComputeLongestSegmentAnchor),
//        the arrow-angle logic of ComputeIncomingAngle.
//    Node lookups by id were replaced by an explicit obstacle list, and the
//    grid extent was made an explicit routing area instead of being read from
//    the element bounds.
//
//    The algorithm, its constants and its output are otherwise unchanged, with
//    three exceptions — all defects the extracted code shipped with, found by
//    the property tests in Tests/DiagramRoutingTest.cpp and confirmed against
//    the pre-extraction code over 12k randomised layouts (9.7% of paths differ;
//    99.8% of those differences are a case below):
//
//      1. USE-AFTER-FREE. RouteAStar held a reference into the `visited`
//         vector across a push_back into that same vector. Once the search
//         outgrew the reserved capacity the reference dangled and the search
//         read freed memory. Now a copy.
//      2. DIAGONAL SEGMENTS. The end-of-path bridging inserted a single
//         waypoint on the assumption that the preceding waypoint already
//         shared an axis with it; when the previous segment ran the other way
//         the "orthogonal" path contained a visible diagonal jog. Replaced by
//         a general orthogonalisation pass.
//      3. DEGENERATE WAYPOINTS. Endpoints that already shared an axis produced
//         zero-length segments, which make the arrow-angle computation
//         meaningless. Paths are now normalised: no duplicate waypoints, no
//         collinear redundancy, exact endpoints preserved.
//
//    Callers that depend on the exact old geometry in those cases do not
//    exist: all three produced visibly wrong output.

#pragma once

#include "UltraCanvasCommonTypes.h"
#include <vector>

namespace UltraCanvas {

// =============================================================================
// TYPES
// =============================================================================

// The side of a box a connection leaves from or arrives at.
enum class DiagramCardinalSide { Top, Right, Bottom, Left };

// A rectangle the router must route around. Callers build these from their own
// node type and omit the nodes the path is allowed to enter (source/target).
struct DiagramObstacle {
    Rect2Dd bounds;

    DiagramObstacle() = default;
    explicit DiagramObstacle(const Rect2Dd& r) : bounds(r) {}
    DiagramObstacle(double x, double y, double w, double h) : bounds(x, y, w, h) {}
};

// Tuning knobs. The defaults are exactly the values FlowChart 2.2.0 shipped.
struct DiagramRoutingOptions {
    // Cell size of the A* grid, in world units. Must be > 0.
    double gridSize = 20.0;

    // World-space region A* may use. Cells outside it are treated as blocked.
    // Callers normally pass Rect2Dd(0, 0, elementWidth, elementHeight).
    Rect2Dd routingArea = Rect2Dd(0.0, 0.0, 0.0, 0.0);

    // Extra g-cost charged for changing direction. Higher = fewer, longer
    // straight runs; 0 = shortest path regardless of how jagged it looks.
    double turnPenalty = 5.0;

    // Hard cap on A* node expansions so a pathological graph cannot stall a
    // render pass. On overrun the search gives up and the caller falls back.
    int maxExpansions = 20000;

    // Inset applied to an obstacle before testing a path against it, so a line
    // running exactly along a border is not treated as a collision.
    double obstacleInset = 1.0;

    // Padding around an obstacle when marking A* cells blocked. Expressed as a
    // fraction of gridSize.
    double obstaclePaddingCells = 0.5;
};

// =============================================================================
// ROUTER
// =============================================================================

// All members are static and stateless: routing is a pure function of its
// inputs, which is what makes it testable and shareable.
class UltraCanvasDiagramRouter {
public:
    // -------------------------------------------------------------------------
    // Top-level entry point
    // -------------------------------------------------------------------------

    // Returns an orthogonal (axis-aligned) waypoint list from `start` to `end`,
    // leaving perpendicular to `sourceSide` and arriving perpendicular to
    // `targetSide`.
    //
    // Strategy: try the cheap cardinal L/Z path first; only run A* if that path
    // collides with an obstacle. If A* finds nothing (target fully enclosed,
    // expansion cap hit), the cardinal path is returned anyway — a visible
    // connection beats no connection.
    //
    // The returned list always begins with `start` and ends with `end`, and
    // contains no duplicate consecutive points.
    static std::vector<Point2Dd> ComputeOrthogonalPath(
        const Point2Dd& start, const Point2Dd& end,
        DiagramCardinalSide sourceSide, DiagramCardinalSide targetSide,
        const std::vector<DiagramObstacle>& obstacles,
        const DiagramRoutingOptions& options);

    // -------------------------------------------------------------------------
    // Individual stages (public so callers can compose them, and so each stage
    // is directly testable)
    // -------------------------------------------------------------------------

    // Cardinal-aware L-shape (mixed axes) or Z-shape (same axis) routing.
    // Obstacle-unaware and allocation-cheap: 3 or 4 waypoints, always.
    static std::vector<Point2Dd> ComputeCardinalPath(
        const Point2Dd& start, const Point2Dd& end,
        DiagramCardinalSide sourceSide, DiagramCardinalSide targetSide);

    // True if any segment of `path` crosses an obstacle. Assumes every segment
    // is axis-aligned (which everything this header produces is).
    static bool PathHasObstacles(const std::vector<Point2Dd>& path,
                                 const std::vector<DiagramObstacle>& obstacles,
                                 double inset = 1.0);

    // A* over a uniform grid with a turn penalty. The search starts one cell
    // outside `start` in the direction of `sourceSide` and must reach the goal
    // travelling perpendicular into `targetSide`, so the first and last
    // segments always meet their boxes head-on.
    //
    // Returns an empty vector when no path exists — callers decide the
    // fallback.
    static std::vector<Point2Dd> RouteAStar(
        const Point2Dd& start, const Point2Dd& end,
        DiagramCardinalSide sourceSide, DiagramCardinalSide targetSide,
        const std::vector<DiagramObstacle>& obstacles,
        const DiagramRoutingOptions& options);

    // -------------------------------------------------------------------------
    // Geometry helpers
    // -------------------------------------------------------------------------

    // The side of the box centred at `boxCenter` that faces `otherCenter`.
    // The plane is split into four quadrants by the box diagonals.
    static DiagramCardinalSide GetCardinalSide(const Point2Dd& boxCenter,
                                               const Point2Dd& otherCenter);

    // Midpoint of the requested side of `box`. For a diamond drawn inside the
    // box these midpoints are the rhombus vertices, which is where lines should
    // visually attach.
    static Point2Dd GetCardinalPoint(const Rect2Dd& box, DiagramCardinalSide side);

    // True for Left/Right.
    static bool IsHorizontalSide(DiagramCardinalSide side);

    // Anchor point for a label on an orthogonal path: the midpoint of its
    // longest segment, so the label never lands on a corner. Falls back to the
    // geometric midpoint for a 2-point path.
    static Point2Dd ComputeLongestSegmentAnchor(const std::vector<Point2Dd>& waypoints);

    // Angle (radians, atan2 convention) at which a path arrives at its target.
    //
    // For an orthogonal path this is derived from `targetSide` alone — the last
    // segment is perpendicular to that side by construction, and using the side
    // keeps the arrowhead correct even for a degenerate 2-point path.
    static double ComputeApproachAngle(DiagramCardinalSide targetSide);

    // Angle of the final segment of an arbitrary polyline. Use for straight and
    // curved connections, where no cardinal side applies. Returns 0 for a path
    // with fewer than 2 points.
    static double ComputeFinalSegmentAngle(const std::vector<Point2Dd>& waypoints);

    // Distributes `count` connection anchors evenly along `side` of `box` and
    // returns the one at `index`. With count == 1 this is the side midpoint, so
    // it is a drop-in replacement for GetCardinalPoint; with count > 1 the
    // anchors are spread over the middle portion of the side so parallel
    // connections into one face do not overlap.
    //
    // (This is the BlockDiagram 2.3.0 CountFaceUsage behaviour, generalised.)
    static Point2Dd GetDistributedCardinalPoint(const Rect2Dd& box,
                                                DiagramCardinalSide side,
                                                int index, int count,
                                                double spreadFraction = 0.6);
};

} // namespace UltraCanvas

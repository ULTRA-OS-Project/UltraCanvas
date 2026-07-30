// Tests/DiagramRoutingTest.cpp
// Unit tests for the shared diagram router: cardinal L/Z routing, the obstacle
// test, A* pathfinding with turn penalty and side-locked endpoints, label
// anchoring, arrow angles and multi-edge anchor distribution.
//
// Exercises the real UltraCanvasDiagramRouter with no UI stack and no link
// dependencies beyond the one source file under test.
//
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasDiagramRouting.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }           \
    } while (0)

using Side = DiagramCardinalSide;

// Every segment of a routed path must be axis-aligned.
static bool IsOrthogonal(const std::vector<Point2Dd>& path) {
    for (size_t i = 1; i < path.size(); ++i) {
        bool horizontal = std::abs(path[i].y - path[i - 1].y) < 1e-6;
        bool vertical   = std::abs(path[i].x - path[i - 1].x) < 1e-6;
        if (!horizontal && !vertical) return false;
    }
    return true;
}

static DiagramRoutingOptions MakeOptions(double width = 800, double height = 600,
                                         double grid = 20.0) {
    DiagramRoutingOptions options;
    options.gridSize = grid;
    options.routingArea = Rect2Dd(0, 0, width, height);
    return options;
}

// =============================================================================

static void TestCardinalPath() {
    std::printf("Cardinal L/Z routing\n");

    // Mixed axes -> L-shape, exactly one corner.
    auto l = UltraCanvasDiagramRouter::ComputeCardinalPath(
        Point2Dd(100, 100), Point2Dd(300, 250), Side::Right, Side::Top);
    CHECK(l.size() == 3, "L-shape has 3 waypoints");
    CHECK(IsOrthogonal(l), "L-shape is orthogonal");
    CHECK(std::abs(l.front().x - 100) < 1e-9 && std::abs(l.back().x - 300) < 1e-9,
          "L-shape keeps the given endpoints");
    CHECK(std::abs(l[1].x - 300) < 1e-9 && std::abs(l[1].y - 100) < 1e-9,
          "L-shape corner follows the horizontal source side");

    // Same axis -> Z-shape with the bend at the midpoint.
    auto z = UltraCanvasDiagramRouter::ComputeCardinalPath(
        Point2Dd(100, 100), Point2Dd(300, 250), Side::Right, Side::Left);
    CHECK(z.size() == 4, "Z-shape has 4 waypoints");
    CHECK(IsOrthogonal(z), "Z-shape is orthogonal");
    CHECK(std::abs(z[1].x - 200) < 1e-9 && std::abs(z[2].x - 200) < 1e-9,
          "Z-shape bends at midX for horizontal sides");

    auto zv = UltraCanvasDiagramRouter::ComputeCardinalPath(
        Point2Dd(100, 100), Point2Dd(300, 250), Side::Bottom, Side::Top);
    CHECK(std::abs(zv[1].y - 175) < 1e-9 && std::abs(zv[2].y - 175) < 1e-9,
          "Z-shape bends at midY for vertical sides");
}

static void TestObstacleDetection() {
    std::printf("Obstacle test\n");

    std::vector<Point2Dd> path = {Point2Dd(0, 100), Point2Dd(400, 100)};

    std::vector<DiagramObstacle> blocking = {DiagramObstacle(150, 50, 100, 100)};
    CHECK(UltraCanvasDiagramRouter::PathHasObstacles(path, blocking),
          "horizontal segment through a box is detected");

    std::vector<DiagramObstacle> clear = {DiagramObstacle(150, 300, 100, 100)};
    CHECK(!UltraCanvasDiagramRouter::PathHasObstacles(path, clear),
          "box far from the segment is not a collision");

    // A line running exactly along the border is tolerated (1px inset).
    std::vector<DiagramObstacle> grazing = {DiagramObstacle(150, 100, 100, 100)};
    CHECK(!UltraCanvasDiagramRouter::PathHasObstacles(grazing[0].bounds.y == 100 ? path : path,
                                                      grazing),
          "segment grazing the top border is not a collision");

    std::vector<Point2Dd> empty = {Point2Dd(0, 0)};
    CHECK(!UltraCanvasDiagramRouter::PathHasObstacles(empty, blocking),
          "degenerate path has no obstacles");

    CHECK(!UltraCanvasDiagramRouter::PathHasObstacles(path, {}),
          "empty obstacle list never collides");
}

static void TestAStarAvoidsObstacles() {
    std::printf("A* obstacle avoidance\n");

    // A wall between source and target forces a detour.
    DiagramRoutingOptions options = MakeOptions(800, 600, 20.0);
    std::vector<DiagramObstacle> obstacles = {DiagramObstacle(300, 0, 80, 400)};

    Point2Dd start(200, 300);
    Point2Dd end(600, 300);

    auto cheap = UltraCanvasDiagramRouter::ComputeCardinalPath(start, end, Side::Right, Side::Left);
    CHECK(UltraCanvasDiagramRouter::PathHasObstacles(cheap, obstacles),
          "the direct path really is blocked");

    auto routed = UltraCanvasDiagramRouter::ComputeOrthogonalPath(
        start, end, Side::Right, Side::Left, obstacles, options);
    CHECK(routed.size() >= 2, "router returned a path");
    CHECK(IsOrthogonal(routed), "routed path is orthogonal");
    CHECK(std::abs(routed.front().x - start.x) < 1e-9 &&
          std::abs(routed.front().y - start.y) < 1e-9, "path starts at the source point");
    CHECK(std::abs(routed.back().x - end.x) < 1e-9 &&
          std::abs(routed.back().y - end.y) < 1e-9, "path ends at the target point");
    CHECK(!UltraCanvasDiagramRouter::PathHasObstacles(routed, obstacles),
          "routed path clears the wall");
    CHECK(routed.size() > cheap.size(), "detour is longer than the blocked direct path");
}

static void TestAStarFallback() {
    std::printf("A* fallback when no path exists\n");

    // Target boxed in on all sides: A* cannot reach it, so the router must
    // still return the cardinal path rather than nothing.
    DiagramRoutingOptions options = MakeOptions(400, 400, 20.0);
    std::vector<DiagramObstacle> cage = {
        DiagramObstacle(150, 100, 100, 20),
        DiagramObstacle(150, 220, 100, 20),
        DiagramObstacle(130, 100, 20, 140),
        DiagramObstacle(250, 100, 20, 140),
    };

    auto routed = UltraCanvasDiagramRouter::ComputeOrthogonalPath(
        Point2Dd(20, 170), Point2Dd(200, 170), Side::Right, Side::Left, cage, options);
    CHECK(!routed.empty(), "fallback still yields a visible connection");
    CHECK(IsOrthogonal(routed), "fallback path is orthogonal");
}

static void TestUnblockedPathIsCheap() {
    std::printf("Unblocked routing skips A*\n");

    DiagramRoutingOptions options = MakeOptions();
    auto cheap = UltraCanvasDiagramRouter::ComputeCardinalPath(
        Point2Dd(100, 100), Point2Dd(400, 300), Side::Right, Side::Left);
    auto routed = UltraCanvasDiagramRouter::ComputeOrthogonalPath(
        Point2Dd(100, 100), Point2Dd(400, 300), Side::Right, Side::Left, {}, options);

    bool identical = routed.size() == cheap.size();
    for (size_t i = 0; identical && i < routed.size(); ++i) {
        identical = std::abs(routed[i].x - cheap[i].x) < 1e-9 &&
                    std::abs(routed[i].y - cheap[i].y) < 1e-9;
    }
    CHECK(identical, "no obstacles -> exactly the cardinal path");
}

static void TestGeometryHelpers() {
    std::printf("Geometry helpers\n");

    Rect2Dd box(100, 100, 200, 100);
    CHECK(UltraCanvasDiagramRouter::GetCardinalSide(Point2Dd(0, 0), Point2Dd(100, 10)) == Side::Right,
          "cardinal side right");
    CHECK(UltraCanvasDiagramRouter::GetCardinalSide(Point2Dd(0, 0), Point2Dd(-100, 10)) == Side::Left,
          "cardinal side left");
    CHECK(UltraCanvasDiagramRouter::GetCardinalSide(Point2Dd(0, 0), Point2Dd(10, 100)) == Side::Bottom,
          "cardinal side bottom");
    CHECK(UltraCanvasDiagramRouter::GetCardinalSide(Point2Dd(0, 0), Point2Dd(10, -100)) == Side::Top,
          "cardinal side top");

    Point2Dd top = UltraCanvasDiagramRouter::GetCardinalPoint(box, Side::Top);
    CHECK(std::abs(top.x - 200) < 1e-9 && std::abs(top.y - 100) < 1e-9,
          "top cardinal point is the side midpoint");
    Point2Dd right = UltraCanvasDiagramRouter::GetCardinalPoint(box, Side::Right);
    CHECK(std::abs(right.x - 300) < 1e-9 && std::abs(right.y - 150) < 1e-9,
          "right cardinal point is the side midpoint");

    CHECK(UltraCanvasDiagramRouter::IsHorizontalSide(Side::Left) &&
          !UltraCanvasDiagramRouter::IsHorizontalSide(Side::Top),
          "horizontal side classification");

    // Label anchor: midpoint of the longest segment, never a corner.
    std::vector<Point2Dd> path = {Point2Dd(0, 0), Point2Dd(10, 0), Point2Dd(10, 200), Point2Dd(20, 200)};
    Point2Dd anchor = UltraCanvasDiagramRouter::ComputeLongestSegmentAnchor(path);
    CHECK(std::abs(anchor.x - 10) < 1e-9 && std::abs(anchor.y - 100) < 1e-9,
          "label anchors to the middle of the longest segment");

    std::vector<Point2Dd> twoPoint = {Point2Dd(0, 0), Point2Dd(100, 50)};
    Point2Dd mid = UltraCanvasDiagramRouter::ComputeLongestSegmentAnchor(twoPoint);
    CHECK(std::abs(mid.x - 50) < 1e-9 && std::abs(mid.y - 25) < 1e-9,
          "2-point path anchors at the geometric midpoint");

    // Arrow angles: arriving into a side means travelling towards it.
    CHECK(std::abs(UltraCanvasDiagramRouter::ComputeApproachAngle(Side::Top) - M_PI * 0.5) < 1e-9,
          "approach angle into the top side points down");
    CHECK(std::abs(UltraCanvasDiagramRouter::ComputeApproachAngle(Side::Left)) < 1e-9,
          "approach angle into the left side points right");

    std::vector<Point2Dd> straight = {Point2Dd(0, 0), Point2Dd(0, 100)};
    CHECK(std::abs(UltraCanvasDiagramRouter::ComputeFinalSegmentAngle(straight) - M_PI * 0.5) < 1e-9,
          "final segment angle of a downward line");
    CHECK(std::abs(UltraCanvasDiagramRouter::ComputeFinalSegmentAngle({Point2Dd(1, 1)})) < 1e-9,
          "degenerate path yields angle 0");
}

static void TestDistributedAnchors() {
    std::printf("Multi-edge anchor distribution\n");

    Rect2Dd box(0, 0, 200, 100);

    Point2Dd single = UltraCanvasDiagramRouter::GetDistributedCardinalPoint(box, Side::Right, 0, 1);
    Point2Dd midpoint = UltraCanvasDiagramRouter::GetCardinalPoint(box, Side::Right);
    CHECK(std::abs(single.x - midpoint.x) < 1e-9 && std::abs(single.y - midpoint.y) < 1e-9,
          "count == 1 reproduces the plain cardinal point");

    Point2Dd a = UltraCanvasDiagramRouter::GetDistributedCardinalPoint(box, Side::Right, 0, 3);
    Point2Dd b = UltraCanvasDiagramRouter::GetDistributedCardinalPoint(box, Side::Right, 1, 3);
    Point2Dd c = UltraCanvasDiagramRouter::GetDistributedCardinalPoint(box, Side::Right, 2, 3);
    CHECK(a.y < b.y && b.y < c.y, "three anchors are ordered down the side");
    CHECK(std::abs(b.y - midpoint.y) < 1e-9, "the middle anchor sits at the side midpoint");
    CHECK(a.y > box.y && c.y < box.y + box.height, "anchors stay clear of the corners");
    CHECK(std::abs(a.x - 200) < 1e-9 && std::abs(c.x - 200) < 1e-9,
          "anchors stay on the right face");

    Point2Dd outOfRange = UltraCanvasDiagramRouter::GetDistributedCardinalPoint(box, Side::Top, 5, 3);
    Point2Dd topMid = UltraCanvasDiagramRouter::GetCardinalPoint(box, Side::Top);
    CHECK(std::abs(outOfRange.x - topMid.x) < 1e-9,
          "out-of-range index falls back to the midpoint");
}

// Property test: whatever the layout, a routed path is orthogonal, starts and
// ends where it was told to, and never contains a duplicated waypoint.
static void TestRoutingInvariants() {
    std::printf("Routing invariants over random layouts\n");

    std::mt19937 rng(20260730);
    int checked = 0, orthogonalFailures = 0, endpointFailures = 0, duplicateFailures = 0;

    for (int trial = 0; trial < 2000; ++trial) {
        DiagramRoutingOptions options = MakeOptions(600 + rng() % 400, 400 + rng() % 300,
                                                    10.0 + static_cast<double>(rng() % 20));
        std::vector<DiagramObstacle> obstacles;
        int count = static_cast<int>(rng() % 6);
        for (int i = 0; i < count; ++i) {
            double w = 40 + static_cast<double>(rng() % 120);
            double h = 30 + static_cast<double>(rng() % 90);
            obstacles.emplace_back(static_cast<double>(rng() % 500),
                                   static_cast<double>(rng() % 300), w, h);
        }

        Point2Dd start(static_cast<double>(rng() % 500), static_cast<double>(rng() % 300));
        Point2Dd end(static_cast<double>(rng() % 500), static_cast<double>(rng() % 300));
        Side sourceSide = static_cast<Side>(rng() % 4);
        Side targetSide = static_cast<Side>(rng() % 4);

        auto path = UltraCanvasDiagramRouter::ComputeOrthogonalPath(
            start, end, sourceSide, targetSide, obstacles, options);
        ++checked;

        if (!IsOrthogonal(path)) ++orthogonalFailures;
        if (path.empty() ||
            std::abs(path.front().x - start.x) > 1e-9 || std::abs(path.front().y - start.y) > 1e-9 ||
            std::abs(path.back().x - end.x) > 1e-9 || std::abs(path.back().y - end.y) > 1e-9) {
            ++endpointFailures;
        }
        for (size_t i = 1; i < path.size(); ++i) {
            if (std::abs(path[i].x - path[i - 1].x) < 1e-9 &&
                std::abs(path[i].y - path[i - 1].y) < 1e-9) {
                ++duplicateFailures;
                break;
            }
        }
    }

    std::printf("  (%d random layouts routed)\n", checked);
    CHECK(orthogonalFailures == 0, "every routed path is axis-aligned");
    CHECK(endpointFailures == 0, "every routed path keeps its endpoints");
    CHECK(duplicateFailures == 0, "no duplicated consecutive waypoints");
}

// =============================================================================

int main() {
    std::printf("=== UltraCanvasDiagramRouter tests ===\n\n");

    TestCardinalPath();
    TestObstacleDetection();
    TestAStarAvoidsObstacles();
    TestAStarFallback();
    TestUnblockedPathIsCheap();
    TestGeometryHelpers();
    TestDistributedAnchors();
    TestRoutingInvariants();

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASSED" : "FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}

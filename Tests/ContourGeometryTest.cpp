// Tests/ContourGeometryTest.cpp
// Unit tests for the contour geometry pipeline: marching-squares isoline
// extraction, saddle disambiguation, isoband cell clipping, level generation,
// NaN handling, point gridding and polyline smoothing.
//
// Exercises the real ContourGrid / MarchingSquares code with no UI stack and no
// link dependencies beyond the two source files under test.
//
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasContourGrid.h"
#include "Plugins/Charts/UltraCanvasMarchingSquares.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }           \
    } while (0)

// Signed area of a polygon in grid units.
static double PolygonArea(const std::vector<Point2Dd>& poly) {
    double a = 0.0;
    for (size_t i = 0; i < poly.size(); ++i) {
        const Point2Dd& p = poly[i];
        const Point2Dd& q = poly[(i + 1) % poly.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return std::fabs(a) * 0.5;
}

// =============================================================================

static void TestRadialField() {
    std::printf("Radial field (a level must be one closed ring at the right radius)\n");

    const int N = 81;
    ContourGrid g;
    g.Resize(N, N);
    g.SetExtents(-2, 2, -2, 2);
    for (int r = 0; r < N; ++r) {
        for (int c = 0; c < N; ++c) {
            double x = g.XAt(c), y = g.YAt(r);
            g.Set(c, r, std::sqrt(x * x + y * y));
        }
    }

    std::vector<ContourPolyline> polys = ExtractContourPolylines(g, 1.0);
    CHECK(polys.size() == 1, "exactly one contour at level 1");
    if (polys.empty()) return;

    CHECK(polys[0].closed, "the contour is a closed loop");

    double maxErr = 0.0;
    for (const Point2Dd& p : polys[0].points) {
        double x = g.XAt(p.x), y = g.YAt(p.y);
        maxErr = std::max(maxErr, std::fabs(std::sqrt(x * x + y * y) - 1.0));
    }
    // The field is sampled on a lattice and interpolated linearly along cell
    // edges, so a circle is reproduced to within a fraction of a cell.
    CHECK(maxErr < 0.01, "every vertex lies on radius 1");
}

static void TestSaddleDisambiguation() {
    std::printf("Saddle (f = x*y at level 0 must give two separate branches)\n");

    const int N = 41;
    ContourGrid g;
    g.Resize(N, N);
    g.SetExtents(-1, 1, -1, 1);
    for (int r = 0; r < N; ++r) {
        for (int c = 0; c < N; ++c) {
            g.Set(c, r, g.XAt(c) * g.YAt(r));
        }
    }

    std::vector<ContourPolyline> polys = ExtractContourPolylines(g, 0.0);
    CHECK(polys.size() >= 2, "the two branches are not merged into one crossing line");

    bool allOpen = true;
    for (const ContourPolyline& p : polys) {
        if (p.closed) allOpen = false;
    }
    CHECK(allOpen, "both branches run off the grid as open lines");
}

static void TestPlanarField() {
    std::printf("Planar field (f = x must contour to an exact straight line)\n");

    ContourGrid g;
    g.Resize(21, 21);
    g.SetExtents(0, 10, 0, 10);
    for (int r = 0; r < 21; ++r) {
        for (int c = 0; c < 21; ++c) g.Set(c, r, g.XAt(c));
    }

    std::vector<ContourPolyline> polys = ExtractContourPolylines(g, 5.0);
    CHECK(polys.size() == 1, "one contour");
    if (polys.empty()) return;

    double maxErr = 0.0;
    for (const Point2Dd& p : polys[0].points) {
        maxErr = std::max(maxErr, std::fabs(g.XAt(p.x) - 5.0));
    }
    CHECK(maxErr < 1e-9, "the contour sits exactly on x = 5");
}

static void TestBandsTileEachCell() {
    std::printf("Isobands (the clipped pieces of a cell must tile it exactly)\n");

    const int N = 25;
    ContourGrid g;
    g.Resize(N, N);
    g.SetExtents(0, 1, 0, 1);
    for (int r = 0; r < N; ++r) {
        for (int c = 0; c < N; ++c) {
            g.Set(c, r, std::sin(3.0 * g.XAt(c)) * std::cos(3.0 * g.YAt(r)));
        }
    }

    std::vector<double> levels =
            GenerateContourLevels(g, ContourLevelMode::Count, 6, 0, 0, {}, false);
    CHECK(!levels.empty(), "count mode produced levels");

    std::vector<double> bounds;
    bounds.push_back(-std::numeric_limits<double>::infinity());
    for (double L : levels) bounds.push_back(L);
    bounds.push_back(std::numeric_limits<double>::infinity());

    double worst = 0.0;
    for (int r = 0; r < N - 1; ++r) {
        for (int c = 0; c < N - 1; ++c) {
            double sum = 0.0;
            for (size_t b = 0; b + 1 < bounds.size(); ++b) {
                sum += PolygonArea(ClipCellToBand(g, c, r, bounds[b], bounds[b + 1]));
            }
            worst = std::max(worst, std::fabs(sum - 1.0));   // each cell has unit area
        }
    }
    CHECK(worst < 1e-9, "bands partition every cell without gap or overlap");
}

static void TestNaNHoles() {
    std::printf("Missing samples (cells touching NaN are skipped cleanly)\n");

    const int N = 20;
    ContourGrid g;
    g.Resize(N, N);
    g.SetExtents(0, 1, 0, 1);
    for (int r = 0; r < N; ++r) {
        for (int c = 0; c < N; ++c) {
            bool hole = (c >= 8 && c <= 11 && r >= 8 && r <= 11);
            g.Set(c, r, hole ? std::numeric_limits<double>::quiet_NaN()
                             : static_cast<double>(c));
        }
    }

    std::vector<ContourPolyline> polys = ExtractContourPolylines(g, 9.5);
    CHECK(!polys.empty(), "a contour is still extracted around the hole");

    bool anyNaN = false;
    for (const ContourPolyline& p : polys) {
        for (const Point2Dd& pt : p.points) {
            if (std::isnan(pt.x) || std::isnan(pt.y)) anyNaN = true;
        }
    }
    CHECK(!anyNaN, "no NaN coordinates are emitted");

    CHECK(std::isnan(g.SampleBilinear(9.5, 9.5)), "sampling inside the hole yields NaN");
}

static void TestLevelGeneration() {
    std::printf("Level generation\n");

    ContourGrid g;
    g.Resize(10, 10);
    g.SetExtents(0, 1, 0, 1);
    for (int r = 0; r < 10; ++r) {
        for (int c = 0; c < 10; ++c) g.Set(c, r, c * 1.0);   // range [0, 9]
    }

    std::vector<double> step = GenerateContourLevels(g, ContourLevelMode::Step,
                                                     0, 2.0, 0.0, {}, false);
    // Strictly inside (0, 9): 2, 4, 6, 8.
    CHECK(step.size() == 4, "step mode yields the levels strictly inside the range");

    std::vector<double> explicitIn = {-5.0, 3.0, 6.0, 100.0};
    std::vector<double> ex = GenerateContourLevels(g, ContourLevelMode::Explicit,
                                                   0, 0, 0, explicitIn, false);
    CHECK(ex.size() == 2, "explicit levels outside the data range are dropped");

    CHECK(std::fabs(NiceContourStep(0.0178) - 0.02) < 1e-12, "nice step rounds 0.0178 to 0.02");
    CHECK(std::fabs(NiceContourStep(23.0) - 25.0) < 1e-12, "nice step rounds 23 to 25");

    std::vector<double> sorted = GenerateContourLevels(g, ContourLevelMode::Count,
                                                       5, 0, 0, {}, true);
    CHECK(std::is_sorted(sorted.begin(), sorted.end()), "levels come back sorted");
}

static void TestPointGridding() {
    std::printf("Point gridding\n");

    std::mt19937 rng(4242);
    std::normal_distribution<double> nx(0.0, 0.4), ny(0.0, 0.4);
    std::vector<ContourPoint> pts;
    for (int i = 0; i < 3000; ++i) pts.emplace_back(nx(rng), ny(rng), 1.0);

    ContourGriddingOptions opt;
    opt.method = ContourGridding::KDE;
    opt.cols = opt.rows = 64;
    ContourGrid g = BuildContourGridFromPoints(pts, opt);

    CHECK(g.Valid(), "KDE produced a valid grid");

    double lo, hi;
    bool finite = g.ValueRange(lo, hi);
    CHECK(finite && lo >= 0.0, "density is non-negative");
    CHECK(hi > lo, "density has spread");

    // The cloud is centred on the origin, so the middle must outweigh a corner.
    double centre = g.SampleBilinear(g.ColAtX(0.0), g.RowAtY(0.0));
    double corner = g.At(0, 0);
    CHECK(centre > corner, "density peaks where the points are");

    CHECK(ScottBandwidth(pts) > 0.0, "Scott's rule returns a usable bandwidth");

    // IDW reproduces the sample values it is given at those samples.
    std::vector<ContourPoint> known = {{0.0, 0.0, 10.0}, {1.0, 1.0, 20.0}};
    ContourGriddingOptions idw;
    idw.method = ContourGridding::IDW;
    idw.cols = idw.rows = 9;
    idw.useExplicitExtents = true;
    idw.xMin = 0.0; idw.xMax = 1.0; idw.yMin = 0.0; idw.yMax = 1.0;
    ContourGrid gi = BuildContourGridFromPoints(known, idw);
    CHECK(std::fabs(gi.At(0, 0) - 10.0) < 1e-6, "IDW honours the sample at a node");
    CHECK(std::fabs(gi.At(8, 8) - 20.0) < 1e-6, "IDW honours the far sample too");
}

static void TestPolylinePostProcessing() {
    std::printf("Polyline smoothing and culling\n");

    const int N = 41;
    ContourGrid g;
    g.Resize(N, N);
    g.SetExtents(-2, 2, -2, 2);
    for (int r = 0; r < N; ++r) {
        for (int c = 0; c < N; ++c) {
            double x = g.XAt(c), y = g.YAt(r);
            g.Set(c, r, x * x + y * y);
        }
    }

    std::vector<ContourPolyline> polys = ExtractContourPolylines(g, 1.0);
    CHECK(polys.size() == 1 && polys[0].closed, "a ring to work on");
    if (polys.empty()) return;

    size_t before = polys[0].points.size();
    double lengthBefore = polys[0].Length();
    SmoothContourPolyline(polys[0], 2);

    CHECK(polys[0].closed, "smoothing keeps a closed contour closed");
    CHECK(polys[0].points.size() > before, "smoothing refines the polyline");
    // Chaikin cuts corners, so the curve gets slightly shorter, never longer.
    CHECK(polys[0].Length() <= lengthBefore + 1e-9, "smoothing does not lengthen the curve");

    std::vector<ContourPolyline> many = ExtractContourPolylines(g, 1.0);
    CullShortPolylines(many, 1e9);
    CHECK(many.empty(), "culling removes contours below the length threshold");
}

static void TestSegmentsMatchPolylines() {
    std::printf("Raw segments and stitched polylines agree\n");

    const int N = 33;
    ContourGrid g;
    g.Resize(N, N);
    g.SetExtents(-2, 2, -2, 2);
    for (int r = 0; r < N; ++r) {
        for (int c = 0; c < N; ++c) {
            double x = g.XAt(c), y = g.YAt(r);
            g.Set(c, r, std::sin(x) * std::cos(y));
        }
    }

    const double level = 0.25;
    std::vector<ContourSegment> segs = ExtractContourSegments(g, level);
    std::vector<ContourPolyline> polys = ExtractContourPolylines(g, level);

    CHECK(!segs.empty(), "segments were produced");

    // Each polyline of n points carries n-1 segments, or n for a closed loop.
    size_t fromPolys = 0;
    for (const ContourPolyline& p : polys) {
        fromPolys += p.closed ? p.points.size() : p.points.size() - 1;
    }
    CHECK(fromPolys == segs.size(), "stitching preserves the segment count");

    bool allInRange = true;
    for (const ContourSegment& s : segs) {
        if (s.cellCol < 0 || s.cellCol >= g.cols - 1 ||
            s.cellRow < 0 || s.cellRow >= g.rows - 1) {
            allInRange = false;
        }
        if (s.level != level) allInRange = false;
    }
    CHECK(allInRange, "every segment is tagged with a valid cell and its level");
}

// =============================================================================

int main() {
    std::printf("=== ContourGeometryTest ===\n\n");

    TestRadialField();
    TestSaddleDisambiguation();
    TestPlanarField();
    TestBandsTileEachCell();
    TestNaNHoles();
    TestLevelGeneration();
    TestPointGridding();
    TestPolylinePostProcessing();
    TestSegmentsMatchPolylines();

    std::printf("\n%s (%d failure%s)\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}

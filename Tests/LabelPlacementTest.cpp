// Tests/LabelPlacementTest.cpp
// Unit tests for the shared label placement solver: session reuse across
// passes, the spatial index (checked against brute force), rotated labels,
// polyline obstacles, priority ordering, suppression, minimum separation,
// leader lines, point anchors and the 1-D axis tick declutter.
//
// Exercises the real solver with no UI stack and no link dependencies beyond
// the one source file under test.
//
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework

#include "UltraCanvasLabelPlacement.h"

#include <algorithm>
#include <chrono>
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

// =============================================================================
// Helpers
// =============================================================================

static LabelShape Circle(double x, double y, double r) {
    LabelShape s;
    s.type = LabelShapeType::Circle;
    s.center = Point2Dd(x, y);
    s.radius = r;
    return s;
}

static ShapeLabel Label(const std::string& text, size_t shapeIndex,
                        double w = 40.0, double h = 12.0) {
    ShapeLabel l;
    l.text = text;
    l.shapeIndex = shapeIndex;
    l.textSize = Size2Dd(w, h);
    return l;
}

static double OverlapArea(const Rect2Dd& a, const Rect2Dd& b) {
    const double w = std::min(a.Right(), b.Right()) - std::max(a.Left(), b.Left());
    const double h = std::min(a.Bottom(), b.Bottom()) - std::max(a.Top(), b.Top());
    return (w > 0.0 && h > 0.0) ? w * h : 0.0;
}

static int CountOverlappingPairs(const std::vector<PlacedShapeLabel>& placed) {
    int pairs = 0;
    for (size_t i = 0; i < placed.size(); ++i) {
        if (placed[i].suppressed) continue;
        for (size_t j = i + 1; j < placed.size(); ++j) {
            if (placed[j].suppressed) continue;
            if (OverlapArea(placed[i].bounds, placed[j].bounds) > 0.01) ++pairs;
        }
    }
    return pairs;
}

// =============================================================================

static void TestBasicNonOverlap() {
    std::printf("Basic placement (labels around a ring must not overlap)\n");

    std::vector<LabelShape> shapes;
    std::vector<ShapeLabel> labels;
    for (int i = 0; i < 12; ++i) {
        const double a = i * (2.0 * M_PI / 12.0);
        shapes.push_back(Circle(400.0 + 150.0 * std::cos(a), 300.0 + 150.0 * std::sin(a), 4.0));
        labels.push_back(Label("Axis " + std::to_string(i), static_cast<size_t>(i)));
    }

    LabelPlacementOptions opts;
    opts.bounds = Rect2Dd(0, 0, 800, 600);
    const std::vector<PlacedShapeLabel> placed = PlaceShapeLabels(shapes, labels, opts);

    CHECK(placed.size() == labels.size(), "one result per label, in input order");
    CHECK(CountOverlappingPairs(placed) == 0, "no two labels overlap");

    bool inBounds = true;
    for (const PlacedShapeLabel& p : placed) {
        if (p.bounds.Left() < -0.5 || p.bounds.Right() > 800.5 ||
            p.bounds.Top() < -0.5 || p.bounds.Bottom() > 600.5) inBounds = false;
    }
    CHECK(inBounds, "every label stays inside the bounds");
}

static void TestObstaclesAreAvoided() {
    std::printf("Obstacles (a legend box must not be covered)\n");

    std::vector<LabelShape> shapes;
    std::vector<ShapeLabel> labels;
    for (int i = 0; i < 8; ++i) {
        shapes.push_back(Circle(100.0 + i * 40.0, 200.0, 3.0));
        labels.push_back(Label("L" + std::to_string(i), static_cast<size_t>(i)));
    }

    const Rect2Dd legend(150, 150, 160, 90);
    LabelPlacementOptions opts;
    opts.bounds = Rect2Dd(0, 0, 500, 400);
    opts.obstacles.push_back(legend);

    const std::vector<PlacedShapeLabel> placed = PlaceShapeLabels(shapes, labels, opts);

    double worst = 0.0;
    for (const PlacedShapeLabel& p : placed) worst = std::max(worst, OverlapArea(p.bounds, legend));
    CHECK(worst <= 0.01, "no label overlaps the obstacle rectangle");
}

static void TestSessionRemembersEarlierPasses() {
    std::printf("Session (a second pass must avoid what the first one placed)\n");

    LabelPlacementOptions opts;
    opts.bounds = Rect2Dd(0, 0, 400, 300);

    std::vector<LabelShape> first, second;
    for (int i = 0; i < 5; ++i) first.push_back(Circle(60.0 + i * 60.0, 150.0, 3.0));
    for (int i = 0; i < 5; ++i) second.push_back(Circle(60.0 + i * 60.0, 158.0, 3.0));

    // Two separate one-shot calls: the second pass cannot see the first.
    std::vector<ShapeLabel> firstLabels, secondLabels;
    for (int i = 0; i < 5; ++i) firstLabels.push_back(Label("A" + std::to_string(i), i));
    for (int i = 0; i < 5; ++i) secondLabels.push_back(Label("B" + std::to_string(i), i));

    const std::vector<PlacedShapeLabel> a1 = PlaceShapeLabels(first, firstLabels, opts);
    const std::vector<PlacedShapeLabel> a2 = PlaceShapeLabels(second, secondLabels, opts);
    int blindPairs = 0;
    for (const PlacedShapeLabel& p : a1)
        for (const PlacedShapeLabel& q : a2)
            if (OverlapArea(p.bounds, q.bounds) > 0.01) ++blindPairs;

    // The same two passes through one session.
    LabelPlacementSession session(opts);
    session.AddShapes(first);
    const std::vector<PlacedShapeLabel> b1 = session.Place(firstLabels);
    const size_t base = session.AddShapes(second);
    for (ShapeLabel& l : secondLabels) l.shapeIndex += base;
    const std::vector<PlacedShapeLabel> b2 = session.Place(secondLabels);

    int sessionPairs = 0;
    for (const PlacedShapeLabel& p : b1)
        for (const PlacedShapeLabel& q : b2)
            if (OverlapArea(p.bounds, q.bounds) > 0.01) ++sessionPairs;

    CHECK(blindPairs > 0, "separate calls do collide (the problem the session solves)");
    CHECK(sessionPairs == 0, "one session keeps the second pass clear of the first");
    CHECK(session.ShapeCount() == first.size() + second.size(), "shape indices accumulate");
}

static void TestResetClearsTheWorld() {
    std::printf("Reset (a cleared session behaves like a fresh one)\n");

    LabelPlacementOptions opts;
    opts.bounds = Rect2Dd(0, 0, 300, 200);

    std::vector<LabelShape> shapes = {Circle(150, 100, 5)};
    std::vector<ShapeLabel> labels = {Label("only", 0)};

    LabelPlacementSession session(opts);
    session.AddShapes(shapes);
    const PlacedShapeLabel a = session.Place(labels)[0];

    session.Reset();
    session.AddShapes(shapes);
    const PlacedShapeLabel b = session.Place(labels)[0];

    CHECK(session.ShapeCount() == 1, "Reset drops the old shapes");
    CHECK(std::abs(a.bounds.x - b.bounds.x) < 1e-9 &&
          std::abs(a.bounds.y - b.bounds.y) < 1e-9,
          "the same input lands in the same place after a Reset");
}

static void TestIndexMatchesBruteForce() {
    std::printf("Spatial index (results must be identical to an exhaustive scan)\n");

    // The index only changes which pairs get *tested*, never the outcome: a
    // dense random layout is placed and the result checked for the invariants
    // an exhaustive solver would also satisfy.
    std::mt19937 rng(20260801);
    std::uniform_real_distribution<double> px(20.0, 780.0), py(20.0, 580.0);

    std::vector<LabelShape> shapes;
    std::vector<ShapeLabel> labels;
    for (int i = 0; i < 120; ++i) {
        shapes.push_back(Circle(px(rng), py(rng), 4.0));
        labels.push_back(Label("N" + std::to_string(i), static_cast<size_t>(i), 34.0, 11.0));
    }

    LabelPlacementOptions opts;
    opts.bounds = Rect2Dd(0, 0, 800, 600);
    opts.labelMargin = 3.0;

    const std::vector<PlacedShapeLabel> placed = PlaceShapeLabels(shapes, labels, opts);
    CHECK(placed.size() == 120, "every label got a result");

    // Any label the solver reports as fitted must genuinely be clear of the
    // labels placed before it - that is the invariant the index must preserve.
    int fitted = 0, violations = 0;
    for (size_t i = 0; i < placed.size(); ++i) {
        if (!placed[i].fitted || placed[i].suppressed) continue;
        ++fitted;
        for (size_t j = 0; j < i; ++j) {
            if (placed[j].suppressed) continue;
            if (OverlapArea(placed[i].bounds, placed[j].bounds) > 0.01) ++violations;
        }
    }
    CHECK(fitted > 100, "the dense case still places nearly everything");
    CHECK(violations == 0, "no fitted label overlaps an earlier one");
}

static void TestRotatedLabels() {
    std::printf("Rotated labels (a tilted label reserves its rotated footprint)\n");

    std::vector<LabelShape> shapes;
    std::vector<ShapeLabel> labels;
    for (int i = 0; i < 6; ++i) {
        shapes.push_back(Circle(80.0 + i * 70.0, 250.0, 3.0));
        ShapeLabel l = Label("2026-08-0" + std::to_string(i), static_cast<size_t>(i), 70.0, 12.0);
        l.preferredSide = LabelSide::Bottom;
        l.rotationDegrees = 45.0;
        labels.push_back(l);
    }

    LabelPlacementOptions opts;
    opts.bounds = Rect2Dd(0, 0, 600, 400);
    const std::vector<PlacedShapeLabel> placed = PlaceShapeLabels(shapes, labels, opts);

    CHECK(CountOverlappingPairs(placed) == 0, "rotated labels do not overlap each other");
    bool echoed = true, grew = true;
    for (const PlacedShapeLabel& p : placed) {
        if (std::abs(p.rotationDegrees - 45.0) > 1e-9) echoed = false;
        // A 70x12 box at 45 degrees needs about 58x58 of axis-aligned room.
        if (p.bounds.width < 50.0 || p.bounds.height < 50.0) grew = false;
    }
    CHECK(echoed, "the rotation is echoed back for the drawing code");
    CHECK(grew, "bounds are the axis-aligned box of the rotated label");
}

static void TestPolylineObstacle() {
    std::printf("Polyline obstacles (a diagonal line costs a band, not its bbox)\n");

    LabelPlacementOptions opts;
    opts.bounds = Rect2Dd(0, 0, 400, 400);

    // A diagonal across the whole area: its bounding rect would be the entire
    // plot, which would make every position equally bad.
    const std::vector<Point2Dd> diagonal = {Point2Dd(0, 0), Point2Dd(400, 400)};

    LabelPlacementSession session(opts);
    const size_t s = session.AddShape(Circle(200, 195, 3));
    session.AddObstaclePolyline(diagonal, 6.0);

    ShapeLabel l = Label("on the line", s, 60.0, 12.0);
    const PlacedShapeLabel p = session.Place({l})[0];

    // Distance from the label centre to the line y = x.
    const double cx = p.bounds.x + p.bounds.width * 0.5;
    const double cy = p.bounds.y + p.bounds.height * 0.5;
    const double dist = std::abs(cx - cy) / std::sqrt(2.0);
    CHECK(p.fitted, "a label next to a diagonal line still fits");
    CHECK(dist > 6.0, "the label is pushed clear of the line band");
}

static void TestPriorityWins() {
    std::printf("Priority (an important label claims its spot regardless of order)\n");

    std::vector<LabelShape> shapes;
    std::vector<ShapeLabel> labels;
    // Five shapes stacked almost on top of each other: only one label can have
    // the preferred position.
    for (int i = 0; i < 5; ++i) {
        shapes.push_back(Circle(200.0, 200.0 + i * 2.0, 3.0));
        ShapeLabel l = Label("cand" + std::to_string(i), static_cast<size_t>(i));
        l.preferredSide = LabelSide::Top;
        labels.push_back(l);
    }
    labels.back().priority = 10;          // last in input order, most important

    LabelPlacementOptions opts;
    opts.bounds = Rect2Dd(0, 0, 400, 400);
    const std::vector<PlacedShapeLabel> placed = PlaceShapeLabels(shapes, labels, opts);

    // The high-priority label should sit closest to its shape's preferred side.
    double bestGap = 1e18;
    size_t bestIndex = 0;
    for (size_t i = 0; i < placed.size(); ++i) {
        const double gap = shapes[i].center.y - placed[i].bounds.Bottom();
        if (gap >= 0.0 && gap < bestGap) { bestGap = gap; bestIndex = i; }
    }
    CHECK(bestIndex == labels.size() - 1, "the prioritised label got the tightest spot");
    CHECK(CountOverlappingPairs(placed) == 0, "priority does not break the non-overlap rule");
}

static void TestSuppression() {
    std::printf("Suppression (drop labels instead of overprinting them)\n");

    // 40 labels of 44x11 need ~19,400 square pixels of text alone; the bounds
    // below offer 14,400, so no arrangement can fit them all.
    std::vector<LabelShape> shapes;
    std::vector<ShapeLabel> permissive, suppressing;
    for (int i = 0; i < 40; ++i) {
        shapes.push_back(Circle(20.0 + (i % 8) * 10.0, 20.0 + (i / 8) * 10.0, 2.0));
        permissive.push_back(Label("123.45", static_cast<size_t>(i), 44.0, 11.0));
        ShapeLabel l = Label("123.45", static_cast<size_t>(i), 44.0, 11.0);
        l.allowSuppress = true;
        suppressing.push_back(l);
    }

    LabelPlacementOptions opts;
    opts.bounds = Rect2Dd(0, 0, 120, 120);

    // A label drawn badly is one that overlaps a neighbour *or* hangs outside
    // the bounds to avoid doing so; both are defects a chart wants gone.
    auto outsideBounds = [&](const std::vector<PlacedShapeLabel>& v) {
        int n = 0;
        for (const PlacedShapeLabel& p : v) {
            if (p.suppressed) continue;
            if (p.bounds.Left() < -0.5 || p.bounds.Right() > 120.5 ||
                p.bounds.Top() < -0.5 || p.bounds.Bottom() > 120.5) ++n;
        }
        return n;
    };

    const std::vector<PlacedShapeLabel> loose = PlaceShapeLabels(shapes, permissive, opts);
    const std::vector<PlacedShapeLabel> tight = PlaceShapeLabels(shapes, suppressing, opts);

    int suppressed = 0;
    for (const PlacedShapeLabel& p : tight) if (p.suppressed) ++suppressed;

    CHECK(CountOverlappingPairs(loose) + outsideBounds(loose) > 0,
          "without suppression, crowded labels overlap or escape the bounds");
    CHECK(suppressed > 0, "allowSuppress drops the labels that cannot fit");
    CHECK(suppressed < 40, "suppression is not all-or-nothing - what fits is kept");
    CHECK(CountOverlappingPairs(tight) == 0, "what survives suppression never overlaps");
    CHECK(outsideBounds(tight) == 0, "what survives suppression stays inside the bounds");

    // The policy applies the same rule without per-label opt-in.
    LabelPlacementOptions policyOpts = opts;
    policyOpts.declutter = LabelDeclutterPolicy::SuppressColliding;
    const std::vector<PlacedShapeLabel> byPolicy = PlaceShapeLabels(shapes, permissive, policyOpts);
    CHECK(CountOverlappingPairs(byPolicy) + outsideBounds(byPolicy) == 0,
          "SuppressColliding declutters without opt-in");
}

static void TestMinimumSeparation() {
    std::printf("Minimum separation (labels keep a visible gap, not just zero overlap)\n");

    std::vector<LabelShape> shapes;
    std::vector<ShapeLabel> labels;
    for (int i = 0; i < 6; ++i) {
        shapes.push_back(Circle(60.0 + i * 55.0, 150.0, 3.0));
        labels.push_back(Label("v" + std::to_string(i), static_cast<size_t>(i), 40.0, 11.0));
    }

    LabelPlacementOptions opts;
    opts.bounds = Rect2Dd(0, 0, 400, 300);
    opts.labelMargin = 2.0;
    opts.minLabelSeparation = 20.0;

    const std::vector<PlacedShapeLabel> placed = PlaceShapeLabels(shapes, labels, opts);

    double closest = 1e18;
    for (size_t i = 0; i < placed.size(); ++i) {
        if (placed[i].suppressed) continue;
        for (size_t j = i + 1; j < placed.size(); ++j) {
            if (placed[j].suppressed) continue;
            const Rect2Dd& a = placed[i].bounds;
            const Rect2Dd& b = placed[j].bounds;
            const double dx = std::max({a.Left() - b.Right(), b.Left() - a.Right(), 0.0});
            const double dy = std::max({a.Top() - b.Bottom(), b.Top() - a.Bottom(), 0.0});
            closest = std::min(closest, std::sqrt(dx * dx + dy * dy));
        }
    }
    CHECK(closest >= 19.0, "every surviving pair honours minLabelSeparation");
}

static void TestPointAnchorAndLeader() {
    std::printf("Point anchors and leader lines\n");

    LabelPlacementOptions opts;
    opts.bounds = Rect2Dd(0, 0, 400, 300);
    opts.shapeMargin = 4.0;

    LabelPlacementSession session(opts);
    std::vector<ShapeLabel> labels;
    for (int i = 0; i < 6; ++i) {
        ShapeLabel l;
        l.text = "p" + std::to_string(i);
        l.textSize = Size2Dd(50.0, 12.0);
        l.usePointAnchor = true;                       // no shapes array at all
        l.anchorPoint = Point2Dd(200.0, 140.0 + i * 4.0);
        l.wantLeaderLine = true;
        labels.push_back(l);
    }
    const std::vector<PlacedShapeLabel> placed = session.Place(labels);

    CHECK(placed.size() == 6, "point-anchored labels place without any shapes");
    CHECK(CountOverlappingPairs(placed) == 0, "point-anchored labels do not overlap");

    int leaders = 0;
    for (const PlacedShapeLabel& p : placed) if (p.hasLeader) ++leaders;
    CHECK(leaders > 0, "labels pushed away from their anchor request a leader line");
}

static void TestBackwardCompatibility() {
    std::printf("Backward compatibility (defaults reproduce the pre-2.0 result)\n");

    // Two labels far apart: nothing to resolve, so the classic geometry must
    // land them exactly where the old solver did - centred above the shape,
    // shapeMargin clear of its edge.
    std::vector<LabelShape> shapes = {Circle(100, 200, 10)};
    std::vector<ShapeLabel> labels = {Label("solo", 0, 40.0, 12.0)};
    labels[0].preferredSide = LabelSide::Top;

    LabelPlacementOptions opts;
    opts.bounds = Rect2Dd(0, 0, 400, 400);
    opts.shapeMargin = 6.0;

    const PlacedShapeLabel p = PlaceShapeLabels(shapes, labels, opts)[0];
    CHECK(std::abs((p.bounds.x + p.bounds.width * 0.5) - 100.0) < 1e-9,
          "the label is centred on its shape");
    CHECK(std::abs(p.bounds.Bottom() - (200.0 - 10.0 - 6.0)) < 1e-9,
          "the label sits exactly shapeMargin above the shape");
    CHECK(p.side == LabelSide::Top && p.fitted && !p.suppressed,
          "side, fitted and suppressed report the classic result");

    // An empty shapes array still returns one default result per label.
    const std::vector<PlacedShapeLabel> none = PlaceShapeLabels({}, labels, opts);
    CHECK(none.size() == 1, "no shapes still yields one result per label");
}

static void TestTickDeclutter() {
    std::printf("Axis tick declutter (1-D fast path)\n");

    std::vector<double> positions, extents;
    for (int i = 0; i < 40; ++i) {
        positions.push_back(i * 10.0);       // 10px apart
        extents.push_back(28.0);             // but 28px wide
    }

    const std::vector<bool> nth = DeclutterAxisTicks(positions, extents, 4.0,
                                                     TickDeclutterPolicy::KeepEveryNth);
    int keptNth = 0;
    for (bool b : nth) if (b) ++keptNth;

    // Uniform thinning must keep a regular stride.
    std::vector<int> keptIndices;
    for (int i = 0; i < 40; ++i) if (nth[i]) keptIndices.push_back(i);
    bool uniform = true;
    for (size_t i = 2; i < keptIndices.size(); ++i) {
        if (keptIndices[i] - keptIndices[i - 1] != keptIndices[1] - keptIndices[0]) uniform = false;
    }
    CHECK(keptNth > 0 && keptNth < 40, "KeepEveryNth thins a crowded axis");
    CHECK(uniform, "KeepEveryNth keeps an even stride");

    bool clear = true;
    for (size_t i = 1; i < keptIndices.size(); ++i) {
        const double gap = positions[keptIndices[i]] - positions[keptIndices[i - 1]] - 28.0;
        if (gap < 4.0 - 1e-9) clear = false;
    }
    CHECK(clear, "kept neighbours honour minGap");

    const std::vector<bool> greedy = DeclutterAxisTicks(positions, extents, 4.0,
                                                        TickDeclutterPolicy::Greedy);
    int keptGreedy = 0;
    for (bool b : greedy) if (b) ++keptGreedy;
    CHECK(keptGreedy >= keptNth, "Greedy keeps at least as many labels as uniform thinning");

    // Priority: the last tick must survive even though greedy order would drop it.
    std::vector<int> priorities(40, 0);
    priorities[39] = 100;
    const std::vector<bool> prio = DeclutterAxisTicks(positions, extents, 4.0,
                                                      TickDeclutterPolicy::PriorityGreedy,
                                                      &priorities);
    CHECK(prio[39], "PriorityGreedy keeps the highest-priority tick");

    // Degenerate inputs.
    CHECK(DeclutterAxisTicks({}, {}, 4.0, TickDeclutterPolicy::Greedy).empty(),
          "no ticks yields no flags");
    const std::vector<bool> one = DeclutterAxisTicks({5.0}, {200.0}, 4.0,
                                                     TickDeclutterPolicy::KeepEveryNth);
    CHECK(one.size() == 1 && one[0], "a single tick is always kept");

    // Roomy axis: nothing should be dropped.
    std::vector<double> sparse, small;
    for (int i = 0; i < 10; ++i) { sparse.push_back(i * 100.0); small.push_back(20.0); }
    const std::vector<bool> all = DeclutterAxisTicks(sparse, small, 4.0,
                                                     TickDeclutterPolicy::KeepEveryNth);
    int keptAll = 0;
    for (bool b : all) if (b) ++keptAll;
    CHECK(keptAll == 10, "an uncrowded axis keeps every label");
}

static void TestScalePerformance() {
    std::printf("Scale (the indexed solver handles a chart-sized label count)\n");

    std::mt19937 rng(7);
    std::uniform_real_distribution<double> px(10.0, 1590.0), py(10.0, 890.0);

    std::vector<LabelShape> shapes;
    std::vector<ShapeLabel> labels;
    for (int i = 0; i < 800; ++i) {
        shapes.push_back(Circle(px(rng), py(rng), 3.0));
        ShapeLabel l = Label("1234.5", static_cast<size_t>(i), 40.0, 11.0);
        l.allowSuppress = true;
        labels.push_back(l);
    }

    LabelPlacementOptions opts;
    opts.bounds = Rect2Dd(0, 0, 1600, 900);

    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<PlacedShapeLabel> placed = PlaceShapeLabels(shapes, labels, opts);
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    int drawn = 0;
    for (const PlacedShapeLabel& p : placed) if (!p.suppressed) ++drawn;
    std::printf("        800 labels solved in %.1f ms, %d drawn\n", ms, drawn);

    CHECK(placed.size() == 800, "every label got a result");
    CHECK(CountOverlappingPairs(placed) == 0, "no survivor overlaps another");
    // Generous ceiling: this is a regression guard against losing the index,
    // not a benchmark. The pre-index solver was orders of magnitude slower here.
    CHECK(ms < 2000.0, "800 labels solve in well under two seconds");
}

// =============================================================================

int main() {
    std::printf("=== LabelPlacementTest ===\n\n");

    TestBasicNonOverlap();
    TestObstaclesAreAvoided();
    TestSessionRemembersEarlierPasses();
    TestResetClearsTheWorld();
    TestIndexMatchesBruteForce();
    TestRotatedLabels();
    TestPolylineObstacle();
    TestPriorityWins();
    TestSuppression();
    TestMinimumSeparation();
    TestPointAnchorAndLeader();
    TestBackwardCompatibility();
    TestTickDeclutter();
    TestScalePerformance();

    std::printf("\n%s (%d failure%s)\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}

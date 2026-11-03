// Tests/ImportancePerformanceModelTest.cpp
// Unit tests for the importance-performance chart model.
//
// Covers what is pure data and therefore testable without a window: the
// geometry helpers (open Chaikin, point in polygon, pole of inaccessibility),
// zone classification - the chart's answer, which must agree with what it
// draws - level lookup, validation, and the Slack preset in both its forms.
//
// Version: 1.0.0
// Last Modified: 2026-10-24
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasImportancePerformanceModel.h"
#include "Plugins/Charts/Engine/UltraCanvasChartHighlights.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }               \
    } while (0)

static bool Near(double a, double b, double tolerance = 1e-9) {
    return std::fabs(a - b) < tolerance;
}

static std::vector<Point2Dd> Square(double x0, double y0, double x1, double y1) {
    return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

// =============================================================================
// GEOMETRY
// =============================================================================

static void TestOpenChaikin() {
    std::printf("\n[Open Chaikin]\n");
    const std::vector<Point2Dd> line = {{0, 0}, {4, 0}, {4, 4}, {8, 4}};
    const std::vector<Point2Dd> once = SmoothPolylineChaikin(line, 1);
    CHECK(once.size() == 2 + 2 * (line.size() - 1), "one pass: the ends plus two points per edge");
    CHECK(Near(once.front().x, 0) && Near(once.front().y, 0), "the first point stays");
    CHECK(Near(once.back().x, 8) && Near(once.back().y, 4), "the last point stays");
    CHECK(Near(once[3].x, 4) && Near(once[3].y, 1), "an interior cut lands a quarter along its edge");

    // The interior comes out as the closed version smooths it, so a band
    // laid along a zone edge follows the edge.
    const std::vector<Point2Dd> closed =
        SmoothPolygonChaikin({{0, 0}, {4, 0}, {4, 4}, {8, 4}, {8, -8}}, 1);
    bool shared = false;
    for (const Point2Dd& p : closed) shared |= Near(p.x, once[3].x) && Near(p.y, once[3].y);
    CHECK(shared, "an interior point of the open result is a point of the closed result");

    CHECK(SmoothPolylineChaikin(line, 0).size() == line.size(), "zero passes leave the line alone");
    CHECK(SmoothPolylineChaikin({{0, 0}, {1, 1}}, 3).size() == 2, "a two-point line is left alone");
}

static void TestChaikinAffine() {
    std::printf("\n[Chaikin commutes with a linear axis mapping]\n");
    // The chart smooths in value space and maps the result to the screen;
    // smoothing on screen would give the same shape for linear axes, which
    // is why classification in value space matches the drawing.
    const std::vector<Point2Dd> shape = {{1, 1}, {5, 1}, {5, 3}, {3, 6}, {1, 4}};
    auto map = [](const Point2Dd& p) { return Point2Dd(40.0 + 30.0 * p.x, 500.0 - 45.0 * p.y); };
    std::vector<Point2Dd> mapped;
    for (const Point2Dd& p : shape) mapped.push_back(map(p));
    const std::vector<Point2Dd> a = SmoothPolygonChaikin(mapped, 3);
    const std::vector<Point2Dd> valueSmoothed = SmoothPolygonChaikin(shape, 3);
    bool same = a.size() == valueSmoothed.size();
    for (size_t i = 0; same && i < a.size(); ++i) {
        const Point2Dd b = map(valueSmoothed[i]);
        same = Near(a[i].x, b.x, 1e-6) && Near(a[i].y, b.y, 1e-6);
    }
    CHECK(same, "smooth-then-map equals map-then-smooth");
}

static void TestPolygonContains() {
    std::printf("\n[Point in polygon]\n");
    const std::vector<Point2Dd> square = Square(0, 0, 10, 10);
    CHECK(PolygonContains(square, Point2Dd(5, 5)), "the centre is inside");
    CHECK(!PolygonContains(square, Point2Dd(11, 5)), "a point to the right is outside");
    CHECK(!PolygonContains(square, Point2Dd(5, -1)), "a point below is outside");
    // A concave L: the notch is outside.
    const std::vector<Point2Dd> l = {{0, 0}, {10, 0}, {10, 3}, {3, 3}, {3, 10}, {0, 10}};
    CHECK(PolygonContains(l, Point2Dd(1, 8)), "the L's upright is inside");
    CHECK(!PolygonContains(l, Point2Dd(7, 7)), "the L's notch is outside");
    CHECK(!PolygonContains({{0, 0}, {1, 1}}, Point2Dd(0.5, 0.5)), "two points enclose nothing");
}

static void TestPole() {
    std::printf("\n[Pole of inaccessibility]\n");
    const Rect2Dd clip(-100, -100, 400, 400);
    double radius = 0.0;
    const Point2Dd centre = PoleOfInaccessibility(Square(0, 0, 100, 100), {}, clip, &radius);
    CHECK(Near(centre.x, 50, 1.0) && Near(centre.y, 50, 1.0), "a square's pole is its centre");
    CHECK(Near(radius, 50, 1.0), "and the radius is half its side");

    // An L whose thick foot is at the bottom: the pole is in the foot, not at
    // the centroid (which falls in the notch for a thin L).
    const std::vector<Point2Dd> l = {{0, 0}, {100, 0}, {100, 60}, {20, 60}, {20, 100}, {0, 100}};
    const Point2Dd foot = PoleOfInaccessibility(l, {}, clip, &radius);
    CHECK(PolygonContains(l, foot) && foot.y < 60, "an L's pole is inside its foot");
    CHECK(radius > 25.0, "and well clear of the edges");

    // A hole covering the right half moves the pole left.
    const Point2Dd left = PoleOfInaccessibility(Square(0, 0, 100, 100), {Square(50, -10, 110, 110)},
                                                clip, &radius);
    CHECK(left.x < 50 && Near(radius, 25, 1.0), "a covering zone pushes the pole into what stays visible");

    // The clip is an edge too: only the visible part of the zone counts.
    const Point2Dd clipped = PoleOfInaccessibility(Square(0, 0, 100, 100), {}, Rect2Dd(0, 0, 40, 100), &radius);
    CHECK(clipped.x <= 40 && Near(radius, 20, 1.0), "a pole stays inside the clip");

    // A point to avoid counts as an edge: a mark at the centre moves the pole off it.
    const Point2Dd aside = PoleOfInaccessibility(Square(0, 0, 100, 100), {}, clip, &radius, {Point2Dd(50, 50)});
    CHECK(std::hypot(aside.x - 50, aside.y - 50) > 20.0 && radius > 15.0,
          "a pole keeps clear of a point it is told to avoid");

    PoleOfInaccessibility(Square(0, 0, 10, 10), {Square(-5, -5, 15, 15)}, clip, &radius);
    CHECK(radius == 0.0, "a zone covered entirely has nowhere to put its title");
}

// =============================================================================
// MODEL
// =============================================================================

static void TestZoneOrder() {
    std::printf("\n[Zone order]\n");
    ImportancePerformanceModel model;
    ImportancePerformanceZone back, front;
    back.outline = Square(0, 0, 10, 10);
    front.outline = Square(5, 5, 15, 15);
    model.zones = {back, front};
    CHECK(model.ZoneOf(2, 2) == 0, "a point only in the first zone is in it");
    CHECK(model.ZoneOf(7, 7) == 1, "where zones overlap, the later one wins, as in the drawing");
    CHECK(model.ZoneOf(20, 20) == -1, "a point in no zone is -1");
    CHECK(model.ZoneOf(std::nan(""), 2) == -1, "a missing score is -1");
}

static void TestLevels() {
    std::printf("\n[Levels]\n");
    ImportancePerformanceAxis axis;
    axis.levels = {{"Low", 0.5, 3.5}, {"Mid", 3.5, 6.5}, {"High", 6.5, 9.5}};
    CHECK(ImportancePerformanceModel::LevelOf(axis, 1.0) == 0, "1 is in the first level");
    CHECK(ImportancePerformanceModel::LevelOf(axis, 3.5) == 1, "a shared edge belongs to the upper level");
    CHECK(ImportancePerformanceModel::LevelOf(axis, 9.5) == 2, "the last level keeps its top edge");
    CHECK(ImportancePerformanceModel::LevelOf(axis, 10.0) == -1, "past the last level is -1");
    CHECK(Near(axis.levels[1].Centre(), 5.0), "a level's centre is the middle of its range");
}

static void TestValidation() {
    std::printf("\n[Validation]\n");
    ImportancePerformanceModel good = ImportancePerformanceSamples::SlackZones(false);
    good.factors = ImportancePerformanceSamples::SampleFactors();
    CHECK(good.Validate().empty(), "the stepped Slack preset with the sample factors is valid");
    ImportancePerformanceModel smooth = ImportancePerformanceSamples::SlackZones(true);
    smooth.factors = ImportancePerformanceSamples::SampleFactors();
    CHECK(smooth.Validate().empty(), "the smooth Slack preset with the sample factors is valid");

    ImportancePerformanceModel bad = good;
    bad.importance.max = bad.importance.min;
    CHECK(!bad.Validate().empty(), "an empty range is reported");

    bad = good;
    bad.performance.levels.push_back({"Overlap", 5.0, 8.0});
    CHECK(!bad.Validate().empty(), "overlapping levels are reported");

    bad = good;
    bad.importance.levels[0].to = 12.0;
    CHECK(!bad.Validate().empty(), "a level past the axis range is reported");

    bad = good;
    bad.zones[1].outline.resize(2);
    CHECK(!bad.Validate().empty(), "a zone with two points is reported");

    bad = good;
    bad.factors.push_back({"Off the chart", "Service", 12.0, 5.0});
    CHECK(!bad.Validate().empty(), "a factor outside the axis range is reported");

    bad = good;
    bad.factors.push_back({"Unscored", "Service", std::nan(""), 5.0});
    CHECK(!bad.Validate().empty(), "a factor without both scores is reported");
}

static void TestSlackPreset() {
    std::printf("\n[Slack preset]\n");
    for (bool smooth : {false, true}) {
        const ImportancePerformanceModel model = ImportancePerformanceSamples::SlackZones(smooth);
        CHECK(model.zones.size() == 4 && model.bands.size() == 1, "four zones and one band");
        CHECK(model.importance.inverted && model.performance.inverted,
              "both of Slack's scales put 1, the best grade, at the far end");
        CHECK(model.ZoneShape(1).size() >= model.zones[1].outline.size(), "a zone shape has at least its outline's points");
        // Points deep inside each zone, the same in both forms.
        CHECK(model.ZoneOf(2.0, 1.0) == 0, "important and far better than competitors: appropriate");
        CHECK(model.ZoneOf(8.0, 9.0) == 1, "unimportant and far worse: improve");
        CHECK(model.ZoneOf(1.0, 9.4) == 2, "very important and far worse: urgent action");
        CHECK(model.ZoneOf(9.4, 1.0) == 3, "unimportant and far better: excess");
        // Every point of the plot is in some zone, because the first one
        // covers the whole plane.
        bool covered = true;
        for (double i = 0.5; i <= 9.5; i += 0.25) {
            for (double p = 0.5; p <= 9.5; p += 0.25) covered &= model.ZoneOf(i, p) >= 0;
        }
        CHECK(covered, "every point of the plot falls in a zone");
    }
}

static void TestSampleClassification() {
    std::printf("\n[Sample factors]\n");
    ImportancePerformanceModel model = ImportancePerformanceSamples::SlackZones(false);
    model.factors = ImportancePerformanceSamples::SampleFactors();
    const std::vector<int> zones = model.ClassifyFactors();
    // appropriate 0, improve 1, urgent 2, excess 3 - read off the textbook staircase.
    const std::vector<int> expected = {2, 2, 2, 1, 1, 1, 0, 0, 0, 0, 3, 3};
    CHECK(zones == expected, "every sample factor lands in the zone the staircase puts it in");
    bool unique = true;
    for (size_t i = 0; i < model.factors.size(); ++i) {
        for (size_t j = i + 1; j < model.factors.size(); ++j) unique &= model.factors[i].id != model.factors[j].id;
    }
    CHECK(unique, "sample factor ids are unique");
    for (size_t i = 0; i < model.factors.size(); ++i) {
        CHECK(model.ZoneOf(model.factors[i].importance, model.factors[i].performance) == zones[i],
              "ClassifyFactors agrees with ZoneOf");
    }

    // The smooth form moves the boundaries a little; the factors chosen for
    // the demo stay in the same zones, so switching forms does not reshuffle
    // the list a reader is looking at.
    ImportancePerformanceModel smooth = ImportancePerformanceSamples::SlackZones(true);
    smooth.factors = model.factors;
    CHECK(smooth.ClassifyFactors() == expected, "the smooth form classifies the sample factors the same way");
}

int main() {
    std::printf("==========================================\n");
    std::printf("  UltraCanvas Importance-Performance Tests\n");
    std::printf("==========================================\n");

    TestOpenChaikin();
    TestChaikinAffine();
    TestPolygonContains();
    TestPole();
    TestZoneOrder();
    TestLevels();
    TestValidation();
    TestSlackPreset();
    TestSampleClassification();

    std::printf("\n==========================================\n");
    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}

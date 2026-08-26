// Tests/ToneCurveTest.cpp
// Unit tests for the tone curve model (include/UltraCanvasToneCurve.h): point
// management, monotone interpolation, lookup table generation and the
// four-curve set the Curves dialog edits.
//
// Pure model code — no UI stack, no image library.
//
// Version: 1.0.0
// Last Modified: 2026-08-25
// Author: UltraCanvas Framework

#include "UltraCanvasToneCurve.h"

#include <cmath>
#include <cstdio>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }           \
    } while (0)

static bool Near(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}

// =============================================================================

static void TestIdentity() {
    std::printf("Identity curve\n");
    UltraCanvasToneCurve c;
    CHECK(c.IsIdentity(), "a fresh curve is the identity");
    CHECK(c.GetPointCount() == 2, "it holds exactly the two endpoints");
    CHECK(Near(c.Evaluate(0.0f), 0.0f), "maps 0 to 0");
    CHECK(Near(c.Evaluate(0.5f), 0.5f), "maps 0.5 to 0.5");
    CHECK(Near(c.Evaluate(1.0f), 1.0f), "maps 1 to 1");

    std::array<uint8_t, 256> lut = c.BuildLut();
    bool exact = true;
    for (int i = 0; i < 256; ++i) exact = exact && (lut[i] == i);
    CHECK(exact, "the identity LUT maps every level onto itself");
}

static void TestAddMoveRemove() {
    std::printf("Point management\n");
    UltraCanvasToneCurve c;

    int mid = c.AddPoint(0.5f, 0.7f);
    CHECK(mid == 1, "a point added mid-range lands between the endpoints");
    CHECK(!c.IsIdentity(), "the curve is no longer the identity");
    CHECK(Near(c.Evaluate(0.5f), 0.7f), "the curve passes through the point");

    CHECK(c.AddPoint(0.5f, 0.2f) == -1, "a second point at the same input is refused");

    // Endpoints keep their input; only the output travels.
    c.MovePoint(0, 0.4f, 0.25f);
    CHECK(Near(c.GetPoints()[0].input, 0.0f), "the first point stays pinned at input 0");
    CHECK(Near(c.GetPoints()[0].output, 0.25f), "its output followed the drag (black point)");

    // Interior points stay strictly between their neighbours.
    c.MovePoint(1, 5.0f, 0.9f);
    CHECK(c.GetPoints()[1].input < 1.0f, "an interior point cannot pass the last point");
    CHECK(c.GetPoints()[1].input > 0.0f, "nor the first");

    CHECK(!c.RemovePoint(0), "endpoints cannot be removed");
    CHECK(!c.RemovePoint(c.GetPointCount() - 1), "including the last one");
    CHECK(c.RemovePoint(1), "an interior point can");
    CHECK(c.GetPointCount() == 2, "which leaves the endpoints");

    // Filling up: the curve refuses more than MaxPoints.
    UltraCanvasToneCurve full;
    int added = 0;
    for (int i = 1; i < 40; ++i) {
        if (full.AddPoint(i / 40.0f, 0.5f) >= 0) ++added;
    }
    CHECK(full.GetPointCount() <= UltraCanvasToneCurve::MaxPoints,
          "the point count never exceeds MaxPoints");
    CHECK(added > 0, "points were added up to that cap");
}

static void TestMonotonicity() {
    std::printf("Monotone interpolation\n");
    UltraCanvasToneCurve c;
    // A strong S-curve: the classic case where a plain cubic overshoots.
    c.AddPoint(0.25f, 0.05f);
    c.AddPoint(0.75f, 0.95f);

    float prev = -1.0f;
    bool monotone = true, inRange = true;
    for (int i = 0; i <= 1000; ++i) {
        float x = i / 1000.0f;
        float y = c.Evaluate(x);
        if (y < prev - 1e-5f) monotone = false;
        if (y < 0.0f || y > 1.0f) inRange = false;
        prev = y;
    }
    CHECK(monotone, "an S-curve never decreases");
    CHECK(inRange, "and never leaves 0..1");
    CHECK(Near(c.Evaluate(0.25f), 0.05f, 5e-3f), "it passes through its control points");
    CHECK(Near(c.Evaluate(0.75f), 0.95f, 5e-3f), "both of them");

    // A flat segment must stay flat (no bulge between equal outputs).
    UltraCanvasToneCurve flat;
    flat.AddPoint(0.3f, 0.5f);
    flat.AddPoint(0.6f, 0.5f);
    bool stayed = true;
    for (int i = 30; i <= 60; ++i) {
        stayed = stayed && Near(flat.Evaluate(i / 100.0f), 0.5f, 1e-3f);
    }
    CHECK(stayed, "a flat segment does not bulge");
}

static void TestLut() {
    std::printf("Lookup tables\n");
    UltraCanvasToneCurve invert;
    invert.MovePoint(0, 0.0f, 1.0f);
    invert.MovePoint(1, 1.0f, 0.0f);
    std::array<uint8_t, 256> lut = invert.BuildLut();
    CHECK(lut[0] == 255, "an inverted curve maps black to white");
    CHECK(lut[255] == 0, "and white to black");
    CHECK(lut[128] > 120 && lut[128] < 136, "with the midtone near the middle");

    bool nonIncreasing = true;
    for (int i = 1; i < 256; ++i) nonIncreasing = nonIncreasing && (lut[i] <= lut[i - 1]);
    CHECK(nonIncreasing, "the inverted table never rises");
}

static void TestSet() {
    std::printf("Curve set\n");
    ToneCurveSet set;
    CHECK(set.IsIdentity(), "a fresh set is the identity");

    auto luts = set.BuildChannelLuts();
    bool exact = true;
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 256; ++i) exact = exact && (luts[c][i] == i);
    CHECK(exact, "and produces pass-through tables");

    // The master curve composes on top of the per-channel one: red is halved by
    // its own curve, then everything is darkened to a quarter by the master.
    set.red.MovePoint(1, 1.0f, 0.5f);
    set.rgb.MovePoint(1, 1.0f, 0.5f);
    CHECK(!set.IsIdentity(), "a set with an edited curve is not the identity");
    luts = set.BuildChannelLuts();
    CHECK(luts[0][255] < luts[1][255], "red ends darker than green");
    CHECK(luts[1][255] == luts[2][255], "green and blue only see the master curve");
    CHECK(std::abs(static_cast<int>(luts[0][255]) - 64) <= 2,
          "red at full scale is halved twice (255 -> ~64)");
    CHECK(std::abs(static_cast<int>(luts[1][255]) - 128) <= 2,
          "green at full scale is halved once (255 -> ~128)");

    set.Reset();
    CHECK(set.IsIdentity(), "Reset() restores the identity");
}

static void TestSerialisation() {
    std::printf("Serialisation\n");
    UltraCanvasToneCurve c;
    c.AddPoint(0.25f, 0.4f);
    c.AddPoint(0.8f, 0.9f);
    std::string text = c.ToString();

    UltraCanvasToneCurve restored;
    CHECK(restored.FromString(text), "a serialised curve parses back");
    CHECK(restored.GetPointCount() == c.GetPointCount(), "with the same point count");
    bool same = true;
    for (int i = 0; i < restored.GetPointCount(); ++i) {
        same = same && Near(restored.GetPoints()[i].input,  c.GetPoints()[i].input,  2e-3f)
                    && Near(restored.GetPoints()[i].output, c.GetPoints()[i].output, 2e-3f);
    }
    CHECK(same, "and the same points");

    UltraCanvasToneCurve bad;
    CHECK(!bad.FromString("not a curve"), "malformed text is rejected");
    CHECK(bad.IsIdentity(), "and leaves the curve untouched");
}

int main() {
    std::printf("=== ToneCurveTest ===\n");
    TestIdentity();
    TestAddMoveRemove();
    TestMonotonicity();
    TestLut();
    TestSet();
    TestSerialisation();

    if (g_failures == 0) {
        std::printf("\nAll tone curve tests passed.\n");
        return 0;
    }
    std::printf("\n%d test(s) FAILED.\n", g_failures);
    return 1;
}

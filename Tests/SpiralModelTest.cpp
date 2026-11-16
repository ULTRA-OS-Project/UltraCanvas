// Tests/SpiralModelTest.cpp
// Unit tests for the spiral plot model: where a time lands on the spiral
// (turns, rings, angles, direction, the inner and outer edges), the value
// range, what each sample covers, the nearest sample, validation and the two
// samples. UI-free.
//
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasSpiralModel.h"

#include <cmath>
#include <cstdint>
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

static bool Near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

static const double kPi = 3.14159265358979323846;

// The same angle, whole turns apart.
static bool SameAngle(double a, double b) {
    const double d = std::remainder(a - b, 2.0 * kPi);
    return std::abs(d) < 1e-9;
}

static bool HasProblem(const std::vector<std::string>& problems, const std::string& part) {
    for (const std::string& p : problems) if (p.find(part) != std::string::npos) return true;
    return false;
}

static UltraCanvasSpiralModel Ten(double period) {
    UltraCanvasSpiralModel m;
    for (int i = 0; i < 10; ++i) m.samples.push_back({10.0 + i, static_cast<double>(i * i), ""});
    m.period = period;
    return m;
}

static void TestGeometry() {
    std::printf("\n[Geometry]\n");
    UltraCanvasSpiralModel m = Ten(4.0);
    CHECK(Near(m.Start(), 10.0, 1e-12), "the spiral starts at the first sample by default");
    CHECK(Near(m.TurnsOf(16.0), 1.5, 1e-12), "a time is (t - start) / period turns along");
    CHECK(m.TurnCount() == 3, "ten samples a unit apart, four per turn: three rings");
    const double w = m.RingWidth();
    CHECK(Near(w, (1.0 - 0.18) / 4.0, 1e-12), "each ring (1 - inner) / (rings + 1) wide");

    const SpiralPosition first = m.Position(10.0);
    CHECK(Near(first.radius, 0.18 + 0.5 * w, 1e-12) && Near(first.angle, kPi / 2.0, 1e-12),
          "the start: the first ring's middle, at 12 o'clock");
    const SpiralPosition next = m.Position(14.0);
    CHECK(Near(next.radius, first.radius + w, 1e-12) && SameAngle(next.angle, first.angle),
          "one period later: the same ray, one ring out");
    CHECK(SameAngle(m.Position(11.0).angle, 0.0), "a quarter turn, clockwise: 3 o'clock");
    m.clockwise = false;
    CHECK(SameAngle(m.Position(11.0).angle, kPi), "anticlockwise: 9 o'clock");
    m.startAngle = 0.0;
    CHECK(SameAngle(m.Position(10.0).angle, 0.0), "the start angle moves where turns begin");

    CHECK(Near(m.Position(10.0, -0.5).radius, 0.18, 1e-12), "the first ring's inner edge is the inner radius");
    CHECK(Near(m.Position(10.0 + 3 * 4.0, 0.5).radius, 1.0, 1e-12), "the last turn's outer edge is the outer radius");
    m.start = 8.0;
    CHECK(Near(m.TurnsOf(12.0), 1.0, 1e-12), "a set start wins");
}

static void TestValuesAndSpans() {
    std::printf("\n[Values and spans]\n");
    UltraCanvasSpiralModel m = Ten(4.0);
    CHECK(Near(m.ValueRange().first, 0.0, 1e-12) && Near(m.ValueRange().second, 81.0, 1e-12), "the data's range");
    m.valueMax = 50.0;
    CHECK(Near(m.Normalize(25.0), 0.5, 1e-12) && Near(m.Normalize(70.0), 1.0, 1e-12), "a set maximum; above it clamps");
    CHECK(std::isnan(m.Normalize(std::nan(""))), "a missing value stays missing");

    CHECK(Near(m.TypicalStep(), 1.0, 1e-12) && Near(m.SampleSpan(3), 1.0, 1e-12), "a sample covers the step to the next");
    CHECK(Near(m.SampleSpan(9), 1.0, 1e-12), "the last, a typical step");
    m.samples[5].t = 30.0;                             // a long gap before it
    for (size_t i = 6; i < m.samples.size(); ++i) m.samples[i].t = 25.0 + i;
    CHECK(Near(m.SampleSpan(4), 1.0, 1e-12), "a gap of more than three steps is not filled");

    UltraCanvasSpiralModel n = Ten(4.0);
    CHECK(n.NearestSample(13.4) == 3 && n.NearestSample(13.6) == 4 && n.NearestSample(-5.0) == 0 &&
              n.NearestSample(99.0) == 9,
          "the nearest sample");
    CHECK(UltraCanvasSpiralModel().NearestSample(1.0) == SIZE_MAX, "none without samples");
}

static void TestValidate() {
    std::printf("\n[Validation]\n");
    UltraCanvasSpiralModel m;
    CHECK(HasProblem(m.Validate(), "no samples"), "no samples");
    m = Ten(0.0);
    CHECK(HasProblem(m.Validate(), "period must be above 0"), "a period of 0");
    m = Ten(4.0);
    m.innerRadius = 0.9;
    CHECK(HasProblem(m.Validate(), "inner radius"), "an inner radius past 0.6");
    m = Ten(4.0);
    m.start = 12.0;
    CHECK(HasProblem(m.Validate(), "before the start"), "samples before the start");
    m = Ten(4.0);
    m.samples[3].t = 5.0;
    CHECK(HasProblem(m.Validate(), "times must increase"), "times that go back");
    CHECK(Ten(4.0).Validate().empty(), "otherwise fine");
}

static double MeanOf(const UltraCanvasSpiralModel& m, double from, double to) {
    double sum = 0.0;
    int n = 0;
    for (const SpiralSample& s : m.samples) {
        if (s.t >= from && s.t < to) { sum += s.value; ++n; }
    }
    return n > 0 ? sum / n : 0.0;
}

static void TestSamples() {
    std::printf("\n[Samples]\n");
    const UltraCanvasSpiralModel t = UltraCanvasSpiralModel::DailyTemperature();
    CHECK(t.samples.size() == 1095 && t.TurnCount() == 3 && t.Validate().empty(),
          "DailyTemperature: three years of days, three turns, valid");
    CHECK(MeanOf(t, 365, 396) + 10.0 < MeanOf(t, 546, 577), "January is much colder than July");
    CHECK(MeanOf(t, 730, 1095) > MeanOf(t, 0, 365), "the third year is warmer than the first");
    CHECK(SameAngle(t.Position(15.0).angle, t.Position(380.0).angle), "mid-January lands on one ray every year");
    CHECK(t.sectorLabels.size() == 12 && t.turnLabels.size() == 3, "month names, years");

    const UltraCanvasSpiralModel v = UltraCanvasSpiralModel::HourlyVisits();
    CHECK(v.samples.size() == 672 && v.TurnCount() == 4 && v.Validate().empty(),
          "HourlyVisits: four weeks of hours, four turns, valid");
    CHECK(MeanOf(v, 10, 12) > 3.0 * MeanOf(v, 154, 156), "Monday 10:00 is far busier than Sunday 10:00");
    const size_t sale = 2 * 168 + 3 * 24 + 15;
    CHECK(v.samples[sale].note == "The sale" && v.samples[sale].value > v.samples[sale - 168].value * 1.5,
          "the third Thursday's sale stands out");
}

int main() {
    std::printf("====================================\n");
    std::printf("  UltraCanvas Spiral Model\n");
    std::printf("====================================\n");
    TestGeometry();
    TestValuesAndSpans();
    TestValidate();
    TestSamples();
    std::printf("\n====================================\n");
    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}

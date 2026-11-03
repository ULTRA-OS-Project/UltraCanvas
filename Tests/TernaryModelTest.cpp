// Tests/TernaryModelTest.cpp
// Unit tests for the ternary chart model: shares from raw amounts, the
// points that have none, the reading direction's label edges, the ticks,
// validation and the shipped samples. UI-free.
//
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasTernaryModel.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <string>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }               \
    } while (0)

static bool Near(double a, double b, double tol = 1e-12) { return std::abs(a - b) <= tol; }

static void TestShares() {
    std::printf("\n[Shares]\n");
    double a = 0, b = 0, c = 0;
    CHECK(TernaryPoint(412, 300, 288).Shares(a, b, c) && Near(a, 0.412) && Near(b, 0.3) && Near(c, 0.288),
          "votes become shares of their sum");
    CHECK(Near(a + b + c, 1.0), "and the shares add up to 1");
    CHECK(TernaryPoint(33.4, 33.3, 33.0).Shares(a, b, c) && Near(a + b + c, 1.0),
          "a lab result summing to 99.7 is normalised, not refused");
    CHECK(TernaryPoint(0, 0, 5).Shares(a, b, c) && Near(c, 1.0) && Near(a, 0.0), "a pure part sits on its corner");

    a = b = c = -1.0;
    CHECK(!TernaryPoint(0, 0, 0).Shares(a, b, c) && a == -1.0, "parts summing to 0 have no shares, and leave them alone");
    CHECK(!TernaryPoint(-1, 2, 3).Shares(a, b, c), "a negative part has none");
    CHECK(!TernaryPoint(std::nan(""), 2, 3).Shares(a, b, c), "a missing part has none");
    CHECK(!TernaryPoint(std::numeric_limits<double>::infinity(), 2, 3).Shares(a, b, c), "nor an infinite one");
}

static void TestReading() {
    std::printf("\n[Reading direction]\n");
    TernaryModel model;
    model.reading = TernaryReading::Clockwise;
    CHECK(model.LabelEdgeStart(0) == 1 && model.LabelEdgeStart(1) == 2 && model.LabelEdgeStart(2) == 0,
          "clockwise: A reads from B, B from C, C from A (the USDA triangle)");
    model.reading = TernaryReading::Anticlockwise;
    CHECK(model.LabelEdgeStart(0) == 2 && model.LabelEdgeStart(1) == 0 && model.LabelEdgeStart(2) == 1,
          "anticlockwise: A reads from C, B from A, C from B");
}

static void TestTicks() {
    std::printf("\n[Ticks]\n");
    TernaryModel model;
    const std::vector<double> ten = model.Ticks(0);
    CHECK(ten.size() == 11 && ten.front() == 0.0 && ten.back() == 1.0, "every 10 %: eleven ticks from 0 to 1");
    CHECK(Near(ten[3], 0.3), "counted, not accumulated: the fourth tick is exactly 0.3");
    model.axes[1].tickStep = 0.25;
    CHECK(model.Ticks(1).size() == 5, "every 25 %: five ticks");
    model.axes[2].tickStep = 0.3;
    const std::vector<double> odd = model.Ticks(2);
    CHECK(odd.size() == 5 && odd.back() == 1.0, "a step that does not divide 1 still ends at 1");
    model.axes[2].tickStep = 0.0;
    CHECK(model.Ticks(2).size() == 2, "a bad step falls back to the two corners");
}

static void TestValidation() {
    std::printf("\n[Validation]\n");
    TernaryModel model = TernarySamples::ThreePartyVotes();
    CHECK(model.Validate().empty(), "the vote sample is valid");
    TernaryModel bad = model;
    bad.axes[1].title.clear();
    CHECK(!bad.Validate().empty(), "an axis without a title is reported");
    bad = model;
    bad.axes[0].tickStep = 0.8;
    CHECK(!bad.Validate().empty(), "a tick step past 0.5 is reported");
    bad = model;
    bad.series[0].points.emplace_back(0, 0, 0, "Nowhere");
    const std::vector<std::string> problems = bad.Validate();
    CHECK(problems.size() == 1 && problems[0].find("Nowhere") != std::string::npos,
          "a point with no shares is named");
}

static void TestSamples() {
    std::printf("\n[Samples]\n");
    const TernaryModel votes = TernarySamples::ThreePartyVotes();
    CHECK(votes.series.size() == 2 && votes.PointCount() == 24, "two elections of twelve regions");
    CHECK(votes.reading == TernaryReading::Anticlockwise, "the vote sample reads anticlockwise");
    const TernaryModel soil = TernarySamples::SoilSamples();
    CHECK(soil.Validate().empty() && soil.series.size() == 3, "the soil sample is valid, in three groups");
    CHECK(soil.axes[0].title == "Clay" && soil.axes[1].title == "Sand" && soil.axes[2].title == "Silt",
          "on the USDA layout: clay at the apex, sand left, silt right");
    CHECK(soil.reading == TernaryReading::Clockwise, "read clockwise, as the USDA triangle is");
    bool notAllHundred = false;
    for (const TernarySeries& s : soil.series) {
        for (const TernaryPoint& p : s.points) notAllHundred |= std::abs(p.Sum() - 100.0) > 1e-9;
    }
    CHECK(notAllHundred, "some samples sum to a little more or less than 100, as lab results do");
    std::set<std::string> labels;
    for (const TernarySeries& s : soil.series) for (const TernaryPoint& p : s.points) labels.insert(p.label);
    CHECK(labels.size() == soil.PointCount(), "every sample has its own id");
    CHECK(FormatTernaryShare(0.38125).find("38.1") == 0 || FormatTernaryShare(0.38125).find("38,1") == 0,
          "a share reads as a percentage with one decimal");
}

int main() {
    std::printf("====================================\n");
    std::printf("  UltraCanvas Ternary Model\n");
    std::printf("====================================\n");

    TestShares();
    TestReading();
    TestTicks();
    TestValidation();
    TestSamples();

    std::printf("\n====================================\n");
    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}

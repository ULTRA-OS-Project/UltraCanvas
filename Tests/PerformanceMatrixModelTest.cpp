// Tests/PerformanceMatrixModelTest.cpp
// Unit tests for the performance matrix model.
//
// Covers what is pure data and therefore testable without a window: sparse
// cell storage, the rank from the best corner that the DiagonalBands
// colouring reads, validation, and the shipped samples - their sizes, their
// codes counted from the best corner, and their badges.
//
// Version: 1.0.0
// Last Modified: 2026-10-27
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasPerformanceMatrixModel.h"

#include <cstdio>
#include <set>
#include <string>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }               \
    } while (0)

static PerformanceMatrixModel ThreeByThree() {
    PerformanceMatrixModel model;
    model.xAxis.levels = {{"Low"}, {"Mid"}, {"High"}};
    model.yAxis.levels = {{"Low"}, {"Mid"}, {"High"}};
    return model;
}

static void TestCells() {
    std::printf("\n[Cells]\n");
    PerformanceMatrixModel model = ThreeByThree();
    CHECK(model.Rows() == 3 && model.Columns() == 3, "a 3x3 model has three rows and three columns");
    CHECK(model.FindCell(1, 1) == nullptr, "a cell never set is not found");
    CHECK(model.Validate().empty(), "a grid with no cells is valid");

    PerformanceCell cell;
    cell.row = 1;
    cell.col = 2;
    cell.title = "First";
    model.SetCell(cell);
    cell.title = "Second";
    model.SetCell(cell);
    CHECK(model.cells.size() == 1, "setting a cell twice replaces it");
    CHECK(model.FindCell(1, 2) && model.FindCell(1, 2)->title == "Second", "and keeps the later content");
    CHECK(model.Contains(2, 2) && !model.Contains(3, 0) && !model.Contains(0, -1), "Contains knows the grid");
}

static void TestRank() {
    std::printf("\n[Rank from the best corner]\n");
    PerformanceMatrixModel model = ThreeByThree();
    CHECK(model.MaxRank() == 4, "a 3x3 grid has ranks 0..4");
    CHECK(model.Rank(2, 2) == 0, "high / high is the best corner by default");
    CHECK(model.Rank(0, 0) == 4, "low / low is the worst");
    CHECK(model.Rank(2, 0) == 2 && model.Rank(0, 2) == 2, "the other two corners sit in the middle band");
    CHECK(model.Rank(1, 2) == 1 && model.Rank(2, 1) == 1, "one step from the best is rank 1");
    model.highXIsBest = false;
    CHECK(model.Rank(2, 0) == 0 && model.Rank(0, 2) == 4, "moving the best corner moves the ranks");

    PerformanceMatrixModel wide;
    wide.xAxis.levels = {{"1"}, {"2"}, {"3"}, {"4"}};
    wide.yAxis.levels = {{"a"}, {"b"}};
    CHECK(wide.MaxRank() == 4 && wide.Rank(1, 3) == 0 && wide.Rank(0, 0) == 4, "a 2x4 grid ranks the same way");
}

static void TestValidation() {
    std::printf("\n[Validation]\n");
    PerformanceMatrixModel model = ThreeByThree();

    PerformanceMatrixModel noLevels = model;
    noLevels.xAxis.levels.clear();
    CHECK(!noLevels.Validate().empty(), "an axis without levels is reported");

    PerformanceMatrixModel tooMany = model;
    tooMany.yAxis.levels.assign(PerformanceMatrixModel::kMaxLevels + 1, PerformanceLevel("x"));
    CHECK(!tooMany.Validate().empty(), "an axis with more levels than fit is reported");

    PerformanceMatrixModel unlabelled = model;
    unlabelled.xAxis.levels[1].label.clear();
    CHECK(!unlabelled.Validate().empty(), "a level without a label is reported");

    PerformanceMatrixModel outside = model;
    PerformanceCell cell;
    cell.row = 3;
    outside.cells.push_back(cell);
    CHECK(!outside.Validate().empty(), "a cell outside the grid is reported");

    PerformanceMatrixModel twice = model;
    cell.row = 1;
    twice.cells.push_back(cell);
    twice.cells.push_back(cell);
    CHECK(!twice.Validate().empty(), "two cells in one place are reported");

    PerformanceMatrixModel badge = model;
    cell.badge = -1;
    badge.cells = {cell};
    CHECK(!badge.Validate().empty(), "a negative badge is reported");
}

static void TestSamples() {
    std::printf("\n[Samples]\n");
    const PerformanceMatrixModel samples[] = {
        PerformanceMatrixSamples::NineBox(), PerformanceMatrixSamples::PerformanceManagement(),
        PerformanceMatrixSamples::PerformanceVsPotential(), PerformanceMatrixSamples::TalentReview(),
        PerformanceMatrixSamples::SkillWill()};
    for (const PerformanceMatrixModel& model : samples) {
        const std::string what = "\"" + model.title + "\" is valid and fills every cell";
        CHECK(model.Validate().empty() &&
              static_cast<int>(model.cells.size()) == model.Rows() * model.Columns(), what.c_str());
    }

    const PerformanceMatrixModel codes = PerformanceMatrixSamples::PerformanceVsPotential();
    CHECK(codes.FindCell(2, 2)->code == "1A", "1A is high potential and outstanding performance");
    CHECK(codes.FindCell(0, 0)->code == "3C", "3C is the opposite corner");
    CHECK(codes.FindCell(2, 0)->code == "1C" && codes.FindCell(0, 2)->code == "3A", "letters run with performance, digits with potential");
    std::set<std::string> distinct;
    for (const PerformanceCell& c : codes.cells) distinct.insert(c.code);
    CHECK(distinct.size() == 9, "the nine codes are distinct");
    CHECK(codes.FindCell(1, 1)->description.find('\n') != std::string::npos, "descriptions are two lines");

    const PerformanceMatrixModel nine = PerformanceMatrixSamples::NineBox();
    CHECK(nine.FindCell(2, 2)->badge == 9 && nine.FindCell(0, 0)->badge == 1, "9-box badges count up to the best box");
    const PerformanceMatrixModel review = PerformanceMatrixSamples::TalentReview();
    bool threeBullets = true;
    for (const PerformanceCell& c : review.cells) threeBullets &= c.bullets.size() == 3;
    CHECK(threeBullets, "every talent review card has three bullets");
    CHECK(review.yAxis.levels[2].caption == "Move and Retain", "potential levels carry their action caption");
    CHECK(PerformanceMatrixSamples::PerformanceManagement().yAxis.levels[0].accent.a > 0,
          "the management matrix colours its header levels");
    CHECK(PerformanceMatrixSamples::SkillWill().Rows() == 2, "skill / will is a 2x2");
}

int main() {
    std::printf("====================================\n");
    std::printf("  UltraCanvas Performance Matrix Model\n");
    std::printf("====================================\n");

    TestCells();
    TestRank();
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

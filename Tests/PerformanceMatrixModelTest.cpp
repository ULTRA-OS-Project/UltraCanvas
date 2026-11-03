// Tests/PerformanceMatrixModelTest.cpp
// Unit tests for the performance matrix model.
//
// Covers what is pure data and therefore testable without a window: sparse
// cell storage, the rank from the best corner that the DiagonalBands
// colouring reads, validation, and the shipped samples - their sizes, their
// codes counted from the best corner, and their badges.
//
// Version: 1.1.0 - the population: score mapping, explicit cells winning, counts,
//   hidden groups, validation of items and targets, the sample team
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasPerformanceMatrixModel.h"

#include <cmath>
#include <cstdio>
#include <numeric>
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

static void TestScoreMapping() {
    std::printf("\n[Placement by score]\n");
    PerformanceAxis axis;
    axis.levels = {{"Low"}, {"Mid"}, {"High"}};
    axis.minScore = 1.0;
    axis.maxScore = 4.0;
    CHECK(PerformanceMatrixModel::LevelForScore(axis, 1.5) == 0, "evenly split: 1.5 of 1-4 is low");
    CHECK(PerformanceMatrixModel::LevelForScore(axis, 2.0) == 1, "a score on a bound belongs to the level above");
    CHECK(PerformanceMatrixModel::LevelForScore(axis, 3.99) == 2, "the top third is high");
    CHECK(PerformanceMatrixModel::LevelForScore(axis, 0.0) == 0 && PerformanceMatrixModel::LevelForScore(axis, 9.0) == 2,
          "scores outside the range land in the end levels");
    CHECK(PerformanceMatrixModel::LevelForScore(axis, std::nan("")) == -1, "a missing score has no level");
    axis.levels[0].upperBound = 3.0;
    axis.levels[1].upperBound = 3.5;
    CHECK(PerformanceMatrixModel::LevelForScore(axis, 2.9) == 0 && PerformanceMatrixModel::LevelForScore(axis, 3.2) == 1,
          "explicit bounds move the levels");

    PerformanceMatrixModel model = ThreeByThree();
    model.xAxis.minScore = model.yAxis.minScore = 0.0;
    model.xAxis.maxScore = model.yAxis.maxScore = 3.0;
    PerformanceMatrixItem scored("Scored", "A", 2.5, 0.5);
    CHECK(model.CellOf(scored) == PerformanceCellRef(0, 2), "an item's scores map to row by y, column by x");
    PerformanceMatrixItem moved = scored;
    moved.row = 1;
    moved.col = 1;
    CHECK(model.CellOf(moved) == PerformanceCellRef(1, 1), "an explicit cell wins over the scores");
    PerformanceMatrixItem nowhere;
    CHECK(!model.CellOf(nowhere).IsValid(), "an item with no cell and no scores has no place");
}

static void TestCounts() {
    std::printf("\n[Counts]\n");
    PerformanceMatrixModel model = ThreeByThree();
    model.items = {{"a", "Red", 2, 2}, {"b", "Red", 2, 2}, {"c", "Blue", 0, 0}, {"d", "Blue", 1, 2}, {"e", "", 1, 0}};
    const std::vector<int> cells = model.CountsPerCell();
    CHECK(cells.size() == 9 && cells[8] == 2 && cells[0] == 1 && cells[5] == 1 && cells[3] == 1, "counts per cell are row-major");
    CHECK(std::accumulate(cells.begin(), cells.end(), 0) == 5, "every placed item is counted once");
    const std::vector<int> rows = model.CountsPerRow(), cols = model.CountsPerColumn();
    CHECK(rows == std::vector<int>({1, 2, 2}), "counts per row");
    CHECK(cols == std::vector<int>({2, 0, 3}), "counts per column");
    const std::vector<int> noRed = model.CountsPerCell({"Red"});
    CHECK(noRed[8] == 0 && std::accumulate(noRed.begin(), noRed.end(), 0) == 3, "a hidden group is left out");
    CHECK(model.Groups() == std::vector<std::string>({"Red", "Blue", ""}), "groups in order of first appearance");
}

static void TestPopulationValidation() {
    std::printf("\n[Population validation]\n");
    PerformanceMatrixModel model = PerformanceMatrixSamples::NineBox();
    model.items = PerformanceMatrixSamples::SampleTeam();
    model.targetDistribution = PerformanceMatrixSamples::SampleTargetDistribution();
    CHECK(model.Validate().empty(), "the 9-box with the sample team and targets is valid");

    PerformanceMatrixModel bad = model;
    bad.items.push_back(PerformanceMatrixItem());
    CHECK(!bad.Validate().empty(), "an item without a place is reported");
    bad = model;
    bad.items.push_back(PerformanceMatrixItem("Off", "", 5, 0));
    CHECK(!bad.Validate().empty(), "an item outside the grid is reported");
    bad = model;
    bad.targetDistribution.pop_back();
    CHECK(!bad.Validate().empty(), "a target distribution of the wrong length is reported");
    bad = model;
    bad.targetDistribution[0] += 0.2;
    CHECK(!bad.Validate().empty(), "a target distribution not summing to 1 is reported");
    bad = model;
    bad.xAxis.levels[1].upperBound = 1.5;
    bad.xAxis.levels[0].upperBound = 2.0;
    CHECK(!bad.Validate().empty(), "level bounds out of order are reported");
    bad = model;
    bad.yAxis.maxScore = bad.yAxis.minScore;
    CHECK(!bad.Validate().empty(), "an empty score range is reported");
}

static void TestSampleTeam() {
    std::printf("\n[Sample team]\n");
    PerformanceMatrixModel model = PerformanceMatrixSamples::NineBox();
    model.items = PerformanceMatrixSamples::SampleTeam();
    CHECK(model.items.size() == 36, "36 people");
    CHECK(model.Groups().size() == 3, "in three departments");
    const std::vector<int> cells = model.CountsPerCell();
    bool everyCell = true;
    for (int n : cells) everyCell &= n > 0;
    CHECK(everyCell, "every box of the 9-box holds someone");
    CHECK(cells[4] == *std::max_element(cells.begin(), cells.end()), "the core box holds the most, as in a real team");
    std::set<std::string> ids;
    for (const PerformanceMatrixItem& item : model.items) ids.insert(item.id);
    CHECK(ids.size() == model.items.size(), "ids are distinct");
    const std::vector<double> target = PerformanceMatrixSamples::SampleTargetDistribution();
    CHECK(std::abs(std::accumulate(target.begin(), target.end(), 0.0) - 1.0) < 1e-9, "the sample target sums to 1");
}

int main() {
    std::printf("====================================\n");
    std::printf("  UltraCanvas Performance Matrix Model\n");
    std::printf("====================================\n");

    TestCells();
    TestRank();
    TestValidation();
    TestSamples();
    TestScoreMapping();
    TestCounts();
    TestPopulationValidation();
    TestSampleTeam();

    std::printf("\n====================================\n");
    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}

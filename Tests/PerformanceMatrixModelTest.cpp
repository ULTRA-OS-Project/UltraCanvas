// Tests/PerformanceMatrixModelTest.cpp
// Unit tests for the performance matrix model.
//
// Covers what is pure data and therefore testable without a window: sparse
// cell storage, the rank from the best corner that the DiagonalBands
// colouring reads, validation, and the shipped samples - their sizes, their
// codes counted from the best corner, and their badges.
//
// Version: 1.2.0 - interchange: the JSON round trip of every sample, the
//   reader's refusals, the CSV of the population both ways
// Version: 1.1.0 - the population: score mapping, explicit cells winning, counts,
//   hidden groups, validation of items and targets, the sample team
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasPerformanceMatrixModel.h"

#include <algorithm>
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

// Every field the JSON writes comes back as it went out.
static bool SameModel(const PerformanceMatrixModel& a, const PerformanceMatrixModel& b) {
    auto sameColor = [](const Color& x, const Color& y) { return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a; };
    auto sameNumber = [](double x, double y) { return (std::isnan(x) && std::isnan(y)) || std::abs(x - y) < 1e-9; };
    auto sameAxis = [&](const PerformanceAxis& x, const PerformanceAxis& y) {
        if (x.title != y.title || x.reversed != y.reversed || x.levels.size() != y.levels.size() ||
            !sameNumber(x.minScore, y.minScore) || !sameNumber(x.maxScore, y.maxScore)) return false;
        for (size_t i = 0; i < x.levels.size(); ++i) {
            const PerformanceLevel& p = x.levels[i];
            const PerformanceLevel& q = y.levels[i];
            if (p.id != q.id || p.label != q.label || p.caption != q.caption || p.iconPath != q.iconPath ||
                !sameColor(p.accent, q.accent) || !sameNumber(p.upperBound, q.upperBound)) return false;
        }
        return true;
    };
    if (a.title != b.title || a.subtitle != b.subtitle || a.highXIsBest != b.highXIsBest ||
        a.highYIsBest != b.highYIsBest || !sameAxis(a.xAxis, b.xAxis) || !sameAxis(a.yAxis, b.yAxis) ||
        a.cells.size() != b.cells.size() || a.items.size() != b.items.size() ||
        a.targetDistribution.size() != b.targetDistribution.size()) return false;
    for (size_t i = 0; i < a.cells.size(); ++i) {
        const PerformanceCell& p = a.cells[i];
        const PerformanceCell& q = b.cells[i];
        if (p.row != q.row || p.col != q.col || p.code != q.code || p.badge != q.badge || p.title != q.title ||
            p.description != q.description || p.bullets != q.bullets || p.action != q.action ||
            p.iconPath != q.iconPath || !sameColor(p.fill, q.fill) || p.note != q.note) return false;
    }
    for (size_t i = 0; i < a.items.size(); ++i) {
        const PerformanceMatrixItem& p = a.items[i];
        const PerformanceMatrixItem& q = b.items[i];
        if (p.id != q.id || p.label != q.label || p.group != q.group || !sameColor(p.color, q.color) ||
            p.note != q.note || p.row != q.row || p.col != q.col ||
            !sameNumber(p.xScore, q.xScore) || !sameNumber(p.yScore, q.yScore)) return false;
    }
    for (size_t i = 0; i < a.targetDistribution.size(); ++i) {
        if (!sameNumber(a.targetDistribution[i], b.targetDistribution[i])) return false;
    }
    return true;
}

static void TestJson() {
    std::printf("\n[JSON round trip]\n");
    PerformanceMatrixModel calibration = PerformanceMatrixSamples::NineBox();
    calibration.items = PerformanceMatrixSamples::SampleTeam();
    calibration.items[0].color = Color(10, 20, 30, 255);
    calibration.targetDistribution = PerformanceMatrixSamples::SampleTargetDistribution();
    calibration.highXIsBest = false;
    calibration.xAxis.reversed = true;
    calibration.xAxis.levels[0].upperBound = 2.5;
    calibration.xAxis.levels[0].id = "low";
    const PerformanceMatrixModel samples[] = {
        calibration,
        PerformanceMatrixSamples::PerformanceManagement(),
        PerformanceMatrixSamples::PerformanceVsPotential(),
        PerformanceMatrixSamples::TalentReview(),
        PerformanceMatrixSamples::SkillWill(),
    };
    bool all = true;
    for (const PerformanceMatrixModel& model : samples) {
        PerformanceMatrixModel back;
        std::string error;
        const std::string text = ExportPerformanceMatrixJson(model);
        all &= ImportPerformanceMatrixJson(text, back, &error) && SameModel(model, back);
        if (!error.empty()) std::printf("    (%s)\n", error.c_str());
    }
    CHECK(all, "every sample, and a calibration with people, targets and bounds, comes back unchanged");

    const std::string compact = ExportPerformanceMatrixJson(PerformanceMatrixSamples::SkillWill(), false);
    CHECK(compact.find('\n') == std::string::npos, "the compact form is one line");
    CHECK(compact.find("\"format\":\"ultracanvas-performance-matrix\"") != std::string::npos, "the format is named");
    CHECK(compact.find("\"accent\"") == std::string::npos && compact.find("\"fill\"") == std::string::npos,
          "transparent colours are left out");

    // Hand-written: levels by label and id, a cell and an item by name.
    const std::string hand = R"({
        "format": "ultracanvas-performance-matrix", "version": 1,
        "x": { "title": "Performance", "levels": [ { "label": "Low" }, { "id": "hi", "label": "High" } ] },
        "y": { "levels": [ { "label": "Low" }, { "label": "High" } ] },
        "cells": [ { "row": "high", "column": "HI", "title": "Star" } ],
        "items": [ { "label": "A. Person", "row": "Low", "column": 1 }, { "label": "B", "x": 0.9, "y": 0.1 } ]
    })";
    PerformanceMatrixModel read;
    std::string error;
    CHECK(ImportPerformanceMatrixJson(hand, read, &error), "a hand-written file naming levels by label and id reads");
    CHECK(read.FindCell(1, 1) && read.FindCell(1, 1)->title == "Star", "a cell named by label and id lands in place");
    CHECK(read.items.size() == 2 && read.CellOf(read.items[0]) == PerformanceCellRef(0, 1) &&
          read.CellOf(read.items[1]) == PerformanceCellRef(0, 1), "items by name, index and score land in place");

    auto refuses = [](const std::string& text, const char* expect) {
        PerformanceMatrixModel model = PerformanceMatrixSamples::SkillWill();
        const std::string before = ExportPerformanceMatrixJson(model);
        std::string why;
        const bool ok = ImportPerformanceMatrixJson(text, model, &why);
        return !ok && why.find(expect) != std::string::npos && ExportPerformanceMatrixJson(model) == before;
    };
    CHECK(refuses("{ not json", "not valid JSON"), "broken JSON is refused, the model untouched");
    CHECK(refuses(R"({"format":"ultracanvas-matrix","version":1})", "not a performance matrix"), "another format is refused");
    CHECK(refuses(R"({"format":"ultracanvas-performance-matrix","version":99})", "version 99"), "a newer version is refused");
    CHECK(refuses(R"({"format":"ultracanvas-performance-matrix","version":1,
                     "x":{"levels":[{"label":"A"}]},"y":{"levels":[{"label":"B"}]},
                     "cells":[{"row":"nope","column":0}]})", "cell 0"), "a cell naming no level is refused");
    CHECK(refuses(R"({"format":"ultracanvas-performance-matrix","version":1,
                     "x":{"levels":[{"label":"A"}]},"y":{"levels":[{"label":"B"}]},
                     "items":[{"label":"Nowhere"}]})", "Nowhere"), "a model that does not validate is refused");
}

static void TestCsv() {
    std::printf("\n[CSV of the population]\n");
    PerformanceMatrixModel model = PerformanceMatrixSamples::NineBox();
    model.items = PerformanceMatrixSamples::SampleTeam();
    model.items[1].note = "Said \"no\", then yes, twice";
    const std::string csv = ExportPerformanceItemsCsv(model);
    CHECK(csv.rfind("id,label,group,row,column,x,y,note\n", 0) == 0, "the header comes first");

    PerformanceMatrixModel back = PerformanceMatrixSamples::NineBox();
    std::string error;
    const bool read = ImportPerformanceItemsCsv(csv, back, &error);
    CHECK(read, "the export reads back");
    bool same = read && back.items.size() == model.items.size();
    for (size_t i = 0; same && i < model.items.size(); ++i) {
        const PerformanceMatrixItem& p = model.items[i];
        const PerformanceMatrixItem& q = back.items[i];
        same = p.id == q.id && p.label == q.label && p.group == q.group && p.note == q.note &&
               p.row == q.row && p.col == q.col &&
               (std::isnan(p.xScore) ? std::isnan(q.xScore) : std::abs(p.xScore - q.xScore) < 1e-6) &&
               (std::isnan(p.yScore) ? std::isnan(q.yScore) : std::abs(p.yScore - q.yScore) < 1e-6);
    }
    CHECK(same, "every person comes back in the same place, quotes and commas included");
    CHECK(back.CountsPerCell() == model.CountsPerCell(), "so the counts are the same");

    // A spreadsheet's own file: another column order, letter case, a BOM,
    // CRLF, levels by label and by index, a column the reader does not know.
    const std::string sheet = "\xEF\xBB\xBF" "Note,LABEL,Row,Column,Department\r\n"
                              "first,Ann,High,2,Sales\r\n"
                              ",Ben,0,low,Support\r\n\r\n";
    PerformanceMatrixModel target = PerformanceMatrixSamples::NineBox();
    CHECK(ImportPerformanceItemsCsv(sheet, target, &error), "a spreadsheet's export reads");
    CHECK(target.items.size() == 2 && target.items[0].label == "Ann" && target.items[0].note == "first" &&
          target.CellOf(target.items[0]) == PerformanceCellRef(2, 2) &&
          target.CellOf(target.items[1]) == PerformanceCellRef(0, 0), "by column name, levels by label and index");

    auto refuses = [](const std::string& text, const char* expect) {
        PerformanceMatrixModel m = PerformanceMatrixSamples::NineBox();
        m.items = PerformanceMatrixSamples::SampleTeam();
        const size_t before = m.items.size();
        std::string why;
        const bool ok = ImportPerformanceItemsCsv(text, m, &why);
        if (!ok) std::printf("    (%s)\n", why.c_str());
        return !ok && why.find(expect) != std::string::npos && m.items.size() == before;
    };
    CHECK(refuses("", "empty"), "an empty file is refused, the people untouched");
    CHECK(refuses("name,x,y\nAnn,1,2\n", "'label' or 'id'"), "a header without a label is refused");
    CHECK(refuses("label,x,y\nAnn,1\n", "line 2"), "a short line is refused with its number");
    CHECK(refuses("label,x,y\nAnn,1,2\nBen,3,two\n", "line 3: y score 'two'"), "a score that is not a number is refused");
    CHECK(refuses("label,x,y\nAnn,\"1,5\",2\n", "x score '1,5'"), "a comma decimal is refused, not misread");
    CHECK(refuses("label,row,column\nAnn,Stellar,High\n", "row 'Stellar'"), "a level the axis lacks is refused");
    CHECK(refuses("label,group\nAnn,Sales\n", "neither"), "a person with no place is refused");
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
    TestJson();
    TestCsv();

    std::printf("\n====================================\n");
    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}

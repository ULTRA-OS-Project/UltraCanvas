// Tests/MatrixModelTest.cpp
// Unit tests for the matrix diagram model.
//
// Covers the parts that are pure data and therefore testable without a window:
// the scale presets and their weights, sparse cell storage, the weighted
// roll-ups (which are the whole reason the model exists separately), column
// ranking, validation, and the CSV codec in both directions.
//
// The roll-up cases are built from the published "Application of improvement
// tools" matrix that drove the 5/3/1 preset: its printed row totals are the
// arithmetic these tests pin down.
//
// Version: 1.0.0
// Last Modified: 2026-08-11
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasMatrixModel.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }               \
    } while (0)

static bool NearlyEqual(double a, double b) {
    return std::fabs(a - b) < 1e-9;
}

// =============================================================================
// FIXTURE
// =============================================================================

// Departments x improvement tools, on the 5/3/1 scale.
static MatrixModel MakeToolsMatrix() {
    MatrixModel model;
    model.title = "Application of improvement tools";
    model.shape = MatrixShape::L;

    model.AddSet("departments", "Departments",
                 {"HR", "Finance", "Purchasing", "IT"});
    model.AddSet("tools", "Improvement tools",
                 {"Graphical Analysis", "SPC", "KPIs", "Data Collection",
                  "Cause and Effect", "Process Mapping", "Kaizen", "5S"});

    model.AddPanel("departments", "tools", MatrixScale::Strength531());

    // HR: 3 medium + 1 strong + 4 weak = 9 + 5 + 4 = 18
    model.SetCell(0, "HR", "Graphical Analysis", "medium");
    model.SetCell(0, "HR", "SPC",                "weak");
    model.SetCell(0, "HR", "KPIs",               "medium");
    model.SetCell(0, "HR", "Data Collection",    "strong");
    model.SetCell(0, "HR", "Cause and Effect",   "weak");
    model.SetCell(0, "HR", "Process Mapping",    "weak");
    model.SetCell(0, "HR", "Kaizen",             "medium");
    model.SetCell(0, "HR", "5S",                 "weak");

    // Finance: 1 strong + 1 medium + 1 weak = 5 + 3 + 1 = 9
    model.SetCell(0, "Finance", "Graphical Analysis", "strong");
    model.SetCell(0, "Finance", "KPIs",               "medium");
    model.SetCell(0, "Finance", "Process Mapping",    "weak");

    // Purchasing: 5 weak + 1 medium = 5 + 3 = 8
    model.SetCell(0, "Purchasing", "Graphical Analysis", "weak");
    model.SetCell(0, "Purchasing", "SPC",                "weak");
    model.SetCell(0, "Purchasing", "KPIs",               "weak");
    model.SetCell(0, "Purchasing", "Data Collection",    "medium");
    model.SetCell(0, "Purchasing", "Cause and Effect",   "weak");
    model.SetCell(0, "Purchasing", "Process Mapping",    "weak");

    // IT: left sparse on purpose - most cells in a real matrix are empty.
    model.SetCell(0, "IT", "KPIs", "strong");

    return model;
}

// =============================================================================

static void TestScalePresets() {
    std::printf("\n[scale presets]\n");

    MatrixScale qfd = MatrixScale::QFD();
    CHECK(qfd.levels.size() == 3, "QFD has three levels");
    CHECK(NearlyEqual(qfd.WeightOf("strong"), 9.0), "QFD strong weighs 9");
    CHECK(NearlyEqual(qfd.WeightOf("moderate"), 3.0), "QFD moderate weighs 3");
    CHECK(NearlyEqual(qfd.WeightOf("weak"), 1.0), "QFD weak weighs 1");

    MatrixScale s531 = MatrixScale::Strength531();
    CHECK(NearlyEqual(s531.WeightOf("strong"), 5.0), "5/3/1 strong weighs 5");
    CHECK(NearlyEqual(s531.WeightOf("medium"), 3.0), "5/3/1 medium weighs 3");
    CHECK(NearlyEqual(s531.WeightOf("weak"), 1.0), "5/3/1 weak weighs 1");

    CHECK(NearlyEqual(s531.WeightOf("nonesuch"), 0.0),
          "an unknown level weighs nothing rather than corrupting a total");
    CHECK(s531.Find("nonesuch") == nullptr, "an unknown level id resolves to nullptr");
    CHECK(s531.IndexOf("medium") == 1, "IndexOf reports scale order");

    MatrixScale hml = MatrixScale::HighMediumLow();
    CHECK(hml.levels.size() == 3, "high/medium/low has three levels");
    CHECK(hml.levels[2].shape == MatrixMarkShape::Ring,
          "the weakest high/medium/low level is hollow, so the scale survives greyscale");

    CHECK(MatrixScale::Presence().levels.size() == 1, "presence has a single level");
}

static void TestShapeArity() {
    std::printf("\n[shape arity]\n");

    CHECK(MatrixShapeSetCount(MatrixShape::L) == 2, "L crosses two sets");
    CHECK(MatrixShapeSetCount(MatrixShape::T) == 3, "T crosses three sets");
    CHECK(MatrixShapeSetCount(MatrixShape::X) == 4, "X crosses four sets");
    CHECK(MatrixShapePanelCount(MatrixShape::L) == 1, "L lays out one panel");
    CHECK(MatrixShapePanelCount(MatrixShape::T) == 2, "T lays out two panels");
    CHECK(MatrixShapePanelCount(MatrixShape::X) == 4, "X lays out four panels");
}

static void TestCellStorage() {
    std::printf("\n[cell storage]\n");

    MatrixModel model = MakeToolsMatrix();

    CHECK(model.LevelAt(0, 0, 3) == "strong", "a mark reads back by index");
    CHECK(model.LevelAt(0, 3, 0).empty(), "an unpopulated cell reads back empty");
    CHECK(model.FindCell(0, 3, 0) == nullptr, "an unpopulated cell has no record");

    const size_t before = model.cells.size();
    CHECK(model.SetCell(0, "HR", "SPC", "strong"),
          "re-marking an occupied cell succeeds");
    CHECK(model.cells.size() == before,
          "re-marking replaces rather than appending a second cell");
    CHECK(model.LevelAt(0, 0, 1) == "strong", "the replacement took effect");

    CHECK(!model.SetCell(0, "HR", "SPC", "nonesuch"),
          "an unknown level id is rejected");
    CHECK(model.LevelAt(0, 0, 1) == "strong",
          "a rejected mark leaves the previous one intact");
    CHECK(!model.SetCell(0, "Nonesuch", "SPC", "weak"),
          "an unknown row item id is rejected");
    CHECK(!model.SetCellAt(0, 99, 0, "weak"), "an out-of-range row is rejected");
    CHECK(!model.SetCellAt(9, 0, 0, "weak"), "an unknown panel is rejected");

    CHECK(model.ClearCell(0, 0, 1), "clearing a populated cell reports success");
    CHECK(model.LevelAt(0, 0, 1).empty(), "the cleared cell is empty");
    CHECK(!model.ClearCell(0, 0, 1), "clearing an empty cell reports nothing done");
}

static void TestRollUps() {
    std::printf("\n[roll-ups]\n");

    MatrixModel model = MakeToolsMatrix();

    // The published totals for the reference matrix, under 5/3/1.
    CHECK(NearlyEqual(model.RowScore(0, 0), 18.0), "HR totals 18");
    CHECK(NearlyEqual(model.RowScore(0, 1), 9.0),  "Finance totals 9");
    CHECK(NearlyEqual(model.RowScore(0, 2), 8.0),  "Purchasing totals 8");

    // Under QFD's 9/3/1 the same marks would give HR 9+9+9+9+1+1+1+1 = 22.
    // This is the check that the weights are data, not convention.
    MatrixModel qfdModel = MakeToolsMatrix();
    qfdModel.panels[0].scale = MatrixScale::QFD();
    qfdModel.ClearCells();
    qfdModel.SetCell(0, "HR", "Graphical Analysis", "moderate");
    qfdModel.SetCell(0, "HR", "SPC",                "weak");
    qfdModel.SetCell(0, "HR", "KPIs",               "moderate");
    qfdModel.SetCell(0, "HR", "Data Collection",    "strong");
    qfdModel.SetCell(0, "HR", "Cause and Effect",   "weak");
    qfdModel.SetCell(0, "HR", "Process Mapping",    "weak");
    qfdModel.SetCell(0, "HR", "Kaizen",             "moderate");
    qfdModel.SetCell(0, "HR", "5S",                 "weak");
    CHECK(NearlyEqual(qfdModel.RowScore(0, 0), 22.0),
          "the same marks total 22 on the 9/3/1 scale, not 18");

    // Column 2 (KPIs): HR medium 3 + Finance medium 3 + Purchasing weak 1 +
    // IT strong 5 = 12, every row at the default importance of 1.
    CHECK(NearlyEqual(model.ColumnScore(0, 2), 12.0), "KPIs totals 12 unweighted");

    // With importance the roll-up weights each row's contribution.
    MatrixModel weighted = MakeToolsMatrix();
    weighted.sets[0].items[3].importance = 3.0;   // IT matters three times as much
    CHECK(NearlyEqual(weighted.ColumnScore(0, 2), 3.0 + 3.0 + 1.0 + 15.0),
          "row importance scales that row's contribution to a column score");
    CHECK(NearlyEqual(weighted.ColumnScore(0, 7), 1.0),
          "a column touched by one default-importance row scores its plain weight");

    CHECK(NearlyEqual(model.ColumnScore(0, 99), 0.0),
          "an out-of-range column scores zero rather than reading past the end");
    CHECK(NearlyEqual(model.RowScore(0, 99), 0.0), "an out-of-range row scores zero");
}

static void TestRanking() {
    std::printf("\n[column ranking]\n");

    MatrixModel model = MakeToolsMatrix();
    std::vector<int> order = model.RankColumns(0);

    CHECK(order.size() == 8, "ranking covers every column");

    // Scores: GA 3+5+1=9, SPC 1+1=2, KPIs 3+3+1+5=12, Data 5+3=8,
    // C&E 1+1=2, PM 1+1+1=3, Kaizen 3, 5S 1.
    CHECK(order[0] == 2, "KPIs ranks first at 12");
    CHECK(order[1] == 0, "Graphical Analysis ranks second at 9");
    CHECK(order[2] == 3, "Data Collection ranks third at 8");

    // SPC, Cause and Effect both score 2; Process Mapping and Kaizen both 3.
    // Stable ranking must keep the lower column index first in each tie.
    auto positionOf = [&order](int column) {
        for (size_t i = 0; i < order.size(); ++i) {
            if (order[i] == column) return static_cast<int>(i);
        }
        return -1;
    };
    CHECK(positionOf(5) < positionOf(6), "ties keep column order (Process Mapping before Kaizen)");
    CHECK(positionOf(1) < positionOf(4), "ties keep column order (SPC before Cause and Effect)");

    MatrixModel empty;
    CHECK(empty.RankColumns(0).empty(), "ranking an absent panel yields nothing");
}

static void TestValidation() {
    std::printf("\n[validation]\n");

    MatrixModel model = MakeToolsMatrix();
    MatrixValidation ok = model.Validate();
    CHECK(ok.valid, "a well-formed L matrix validates");
    CHECK(ok.errors.empty(), "a well-formed matrix reports no errors");

    MatrixModel wrongArity = MakeToolsMatrix();
    wrongArity.shape = MatrixShape::X;
    CHECK(!wrongArity.Validate().valid,
          "an X shape with two sets fails validation");

    MatrixModel danglingPanel = MakeToolsMatrix();
    danglingPanel.panels[0].rowSetId = "nonesuch";
    CHECK(!danglingPanel.Validate().valid,
          "a panel referencing an unknown set fails validation");

    MatrixModel badSymmetry = MakeToolsMatrix();
    badSymmetry.panels[0].symmetric = true;
    CHECK(!badSymmetry.Validate().valid,
          "a symmetric panel crossing two different sets fails validation");

    // Reach past the guarded mutators to prove Validate() catches what
    // SetCellAt() would have refused.
    MatrixModel danglingCell = MakeToolsMatrix();
    MatrixCell rogue;
    rogue.panel = 0;
    rogue.row = 99;
    rogue.col = 0;
    rogue.levelId = "weak";
    danglingCell.cells.push_back(rogue);
    CHECK(!danglingCell.Validate().valid, "an out-of-range cell fails validation");

    MatrixModel duplicate = MakeToolsMatrix();
    duplicate.cells.push_back(duplicate.cells[0]);
    CHECK(!duplicate.Validate().valid, "duplicate cells fail validation");

    MatrixModel emptySet = MakeToolsMatrix();
    emptySet.sets[1].items.clear();
    MatrixValidation warned = emptySet.Validate();
    CHECK(!warned.warnings.empty(), "an empty item set warns");
}

static void TestCsvRoundTrip() {
    std::printf("\n[csv codec]\n");

    MatrixModel model = MakeToolsMatrix();
    const std::string csv = ExportMatrixCsv(model, 0);

    CHECK(!csv.empty(), "export produces output");
    CHECK(csv.find("Graphical Analysis") != std::string::npos,
          "the header carries the column labels");
    CHECK(csv.find("Purchasing") != std::string::npos,
          "each row is labelled");

    MatrixModel target = MakeToolsMatrix();
    target.ClearCells();
    CHECK(target.cells.empty(), "the import target starts empty");

    CHECK(ImportMatrixCsv(csv, target, 0), "import accepts what export wrote");
    CHECK(target.cells.size() == model.cells.size(),
          "the round trip preserves the mark count");
    CHECK(target.LevelAt(0, 0, 3) == "strong", "a strong mark survives the round trip");
    CHECK(target.LevelAt(0, 2, 3) == "medium", "a medium mark survives the round trip");
    CHECK(target.LevelAt(0, 3, 0).empty(), "an empty cell stays empty");
    CHECK(NearlyEqual(target.RowScore(0, 0), 18.0),
          "the round-tripped model computes the same totals");

    MatrixModel replaceTarget = MakeToolsMatrix();
    replaceTarget.SetCell(0, "IT", "5S", "strong");
    CHECK(ImportMatrixCsv(csv, replaceTarget, 0), "import into a populated panel succeeds");
    CHECK(replaceTarget.LevelAt(0, 3, 7).empty(),
          "import replaces the panel's marks rather than merging into them");

    // Rejections leave the target alone.
    MatrixModel untouched = MakeToolsMatrix();
    const size_t cellsBefore = untouched.cells.size();
    CHECK(!ImportMatrixCsv("Wrong,Header\n", untouched, 0),
          "a header that does not match the column labels is rejected");
    CHECK(untouched.cells.size() == cellsBefore,
          "a rejected import leaves the model untouched");
    CHECK(!ImportMatrixCsv("", untouched, 0), "empty input is rejected");

    std::string badLevel = csv;
    const size_t strongAt = badLevel.find("strong");
    if (strongAt != std::string::npos) badLevel.replace(strongAt, 6, "megalo");
    CHECK(!ImportMatrixCsv(badLevel, untouched, 0),
          "a level id the scale lacks is rejected");
    CHECK(untouched.cells.size() == cellsBefore,
          "the rejected level import left the model untouched");
}

static void TestCsvQuoting() {
    std::printf("\n[csv quoting]\n");

    MatrixModel model;
    model.shape = MatrixShape::L;
    model.AddSet("rows", "Rows", {"Plain", "Has, comma"});
    model.AddSet("cols", "Cols", {"Quote\"inside", "Plain"});
    model.AddPanel("rows", "cols", MatrixScale::Presence());
    model.SetCellAt(0, 1, 0, "yes");

    const std::string csv = ExportMatrixCsv(model, 0);
    CHECK(csv.find("\"Has, comma\"") != std::string::npos,
          "a label containing a comma is quoted");
    CHECK(csv.find("\"Quote\"\"inside\"") != std::string::npos,
          "an embedded quote is doubled and the field quoted");

    MatrixModel target = model;
    target.ClearCells();
    CHECK(ImportMatrixCsv(csv, target, 0), "quoted fields round-trip");
    CHECK(target.LevelAt(0, 1, 0) == "yes", "the mark survives quoted labels");
}

static void TestSamples() {
    std::printf("\n[samples]\n");

    MatrixModel tools = MatrixSamples::ImprovementTools();
    CHECK(tools.Validate().valid, "the improvement-tools sample validates");
    CHECK(tools.shape == MatrixShape::L, "the improvement-tools sample is L-shaped");
    CHECK(tools.sets.size() == 2, "it carries two item sets");
    CHECK(tools.sets[0].items.size() == 9, "nine departments");
    CHECK(tools.sets[1].items.size() == 8, "eight tools");
    CHECK(NearlyEqual(tools.RowScore(0, 0), 18.0),
          "the sample's HR row still totals the published 18");
    CHECK(tools.ColumnScore(0, 0) > 0.0, "column totals are non-zero");

    MatrixModel employees = MatrixSamples::EmployeeAllocation();
    CHECK(employees.Validate().valid, "the employee-allocation sample validates");
    CHECK(employees.shape == MatrixShape::T, "it is T-shaped");
    CHECK(employees.panels.size() == 2, "a T carries two panels");
    CHECK(employees.panels[0].rowSetId == employees.panels[1].rowSetId,
          "both wings of the T share the row axis");
    CHECK(employees.panels[0].scale.levels.size() !=
                  employees.panels[1].scale.levels.size(),
          "the two wings carry genuinely different scales");

    MatrixModel qfd = MatrixSamples::QualityFunctionDeployment();
    CHECK(qfd.Validate().valid, "the QFD sample validates");
    CHECK(NearlyEqual(qfd.sets[0].items[0].importance, 5.0),
          "the QFD sample carries real row importances");

    // Braking distance is touched by two importance-5 needs at strong (9) plus
    // one importance-4 need at moderate: 5*9 + 3*9 + 4*3 = 84.
    const int brakingCol = qfd.sets[1].IndexOf("Braking distance");
    CHECK(brakingCol >= 0, "the QFD sample has a braking-distance column");
    CHECK(NearlyEqual(qfd.ColumnScore(0, brakingCol), 84.0),
          "QFD column scores weight each row by its importance");

    std::vector<int> ranked = qfd.RankColumns(0);
    CHECK(!ranked.empty(), "the QFD sample ranks its requirements");
    CHECK(qfd.ColumnScore(0, ranked[0]) >= qfd.ColumnScore(0, ranked[1]),
          "the ranking is in descending score order");
}

// =============================================================================

int main() {
    std::printf("================================\n");
    std::printf("  UltraCanvas Matrix Model Tests\n");
    std::printf("================================\n");

    TestScalePresets();
    TestShapeArity();
    TestCellStorage();
    TestRollUps();
    TestRanking();
    TestValidation();
    TestCsvRoundTrip();
    TestCsvQuoting();
    TestSamples();

    std::printf("\n================================\n");
    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}

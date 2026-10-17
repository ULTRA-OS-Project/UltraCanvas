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
// Version: 1.4.0 - competitive assessment: ratings, their validation and their JSON
// Version: 1.3.0 - JSON interchange: every sample round-trips, failures leave the model untouched
// Version: 1.2.0 - symmetric panels, House validation, relative weights, the importance column, ImportMatrixTable, the House sample
// Version: 1.1.0 - X and Y panel validation, form header fields, the X and Y samples
// Version: 1.0.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasMatrixModel.h"
#include "DataFormats/UltraCanvasJSON.h"

#include <algorithm>
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

// Four sets in the order the X places them: left, top, right, bottom.
static MatrixModel MakeXModel() {
    MatrixModel model;
    model.shape = MatrixShape::X;
    model.AddSet("left", "Left", {"L1", "L2"});
    model.AddSet("top", "Top", {"T1", "T2", "T3"});
    model.AddSet("right", "Right", {"R1"});
    model.AddSet("bottom", "Bottom", {"B1", "B2"});
    return model;
}

static void TestXValidation() {
    std::printf("\n[X shape]\n");

    MatrixModel model = MakeXModel();
    model.AddPanel("top", "left", MatrixScale::Presence());
    model.AddPanel("top", "right", MatrixScale::Presence());
    model.AddPanel("bottom", "right", MatrixScale::Presence());
    model.AddPanel("bottom", "left", MatrixScale::Presence());
    CHECK(model.Validate().valid, "four panels, one per quadrant, validate");

    // Which set is the row set is the caller's choice: the layout turns the
    // panel to fit its quadrant, so the model must accept either order.
    MatrixModel turned = MakeXModel();
    turned.AddPanel("left", "top", MatrixScale::Presence());
    turned.AddPanel("top", "right", MatrixScale::Presence());
    turned.AddPanel("right", "bottom", MatrixScale::Presence());
    turned.AddPanel("bottom", "left", MatrixScale::Presence());
    CHECK(turned.Validate().valid, "a panel may name its side set as the row set");
    CHECK(turned.SetCell(0, "L2", "T3", "yes"), "a turned panel addresses its own rows");
    CHECK(turned.LevelAt(0, 1, 2) == "yes", "and stores the mark at (row, col) as given");

    MatrixModel sameAxis = MakeXModel();
    sameAxis.AddPanel("left", "right", MatrixScale::Presence());
    sameAxis.AddPanel("top", "right", MatrixScale::Presence());
    sameAxis.AddPanel("bottom", "right", MatrixScale::Presence());
    sameAxis.AddPanel("bottom", "left", MatrixScale::Presence());
    CHECK(!sameAxis.Validate().valid,
          "a panel crossing left with right has no quadrant and is rejected");

    MatrixModel twice = MakeXModel();
    twice.AddPanel("top", "left", MatrixScale::Presence());
    twice.AddPanel("left", "top", MatrixScale::Presence());
    twice.AddPanel("bottom", "right", MatrixScale::Presence());
    twice.AddPanel("bottom", "left", MatrixScale::Presence());
    CHECK(!twice.Validate().valid, "two panels claiming one quadrant are rejected");

    MatrixModel extra = MakeXModel();
    extra.AddSet("fifth", "Fifth", {"F1"});
    extra.AddPanel("top", "left", MatrixScale::Presence());
    extra.AddPanel("top", "right", MatrixScale::Presence());
    extra.AddPanel("bottom", "right", MatrixScale::Presence());
    extra.AddPanel("fifth", "left", MatrixScale::Presence());
    CHECK(!extra.Validate().valid, "a panel on a fifth set, which the X cannot place, is rejected");

    // The same panels under an L are not the X's business.
    sameAxis.shape = MatrixShape::L;
    CHECK(sameAxis.Validate().valid, "the X placement rules apply only to the X");
}

static void TestYValidation() {
    std::printf("\n[Y shape]\n");

    MatrixModel model;
    model.shape = MatrixShape::Y;
    model.AddSet("a", "A", {"a1", "a2"});
    model.AddSet("b", "B", {"b1"});
    model.AddSet("c", "C", {"c1", "c2", "c3"});
    model.AddPanel("a", "b", MatrixScale::QFD());
    model.AddPanel("b", "c", MatrixScale::QFD());
    model.AddPanel("c", "a", MatrixScale::QFD());
    CHECK(model.Validate().valid, "A x B, B x C, C x A validates");

    MatrixModel self = model;
    self.panels[2] = MatrixPanel("c", "c", MatrixScale::QFD());
    CHECK(!self.Validate().valid, "a Y panel crossing a set with itself is rejected");

    MatrixModel repeat = model;
    repeat.panels[2] = MatrixPanel("b", "a", MatrixScale::QFD());
    CHECK(!repeat.Validate().valid, "a Y relating the same two sets twice is rejected");
}

static void TestHeaderFields() {
    std::printf("\n[header fields]\n");

    MatrixModel model = MatrixSamples::SupplyChain();
    CHECK(model.headerFields.size() == 5, "the supply-chain sample fills the form header");
    CHECK(model.headerFields[0].label == "PROJECT", "fields keep their order");
    model.Clear();
    CHECK(model.headerFields.empty(), "Clear() drops the header fields too");
}

static void TestPhase2Samples() {
    std::printf("\n[X and Y samples]\n");

    MatrixModel x = MatrixSamples::SupplyChain();
    MatrixValidation xv = x.Validate();
    CHECK(xv.valid, "the supply-chain sample validates");
    CHECK(x.shape == MatrixShape::X, "it is X-shaped");
    CHECK(x.sets.size() == 4 && x.panels.size() == 4, "four sets, four panels");
    CHECK(x.panels[0].rowSetId == "suppliers",
          "one of its panels is turned, so the demo exercises both orientations");
    CHECK(x.cells.size() == 32, "every listed mark landed");

    MatrixModel y = MatrixSamples::DesignTraceability();
    CHECK(y.Validate().valid, "the design-traceability sample validates");
    CHECK(y.shape == MatrixShape::Y, "it is Y-shaped");
    CHECK(y.panels.size() == 3, "a Y carries three panels");
    CHECK(y.cells.size() == 22, "every listed mark landed");
    // The motor carries the quiet requirement strongly and battery life
    // moderately: 9 + 3.
    CHECK(NearlyEqual(y.RowScore(2, 0), 12.0), "the closing panel rolls up like any other");
}

static void TestSymmetricPanel() {
    std::printf("\n[symmetric panel]\n");

    MatrixModel model;
    model.AddSet("req", "Requirements", {"A", "B", "C", "D"});
    MatrixPanel& roof = model.AddPanel("req", "req", MatrixScale::Correlation());
    roof.symmetric = true;

    CHECK(model.SetCellAt(0, 3, 1, "positive"), "a lower-triangle pair is accepted");
    CHECK(model.cells.size() == 1 && model.cells[0].row == 1 && model.cells[0].col == 3,
          "and stored above the diagonal, as (1, 3)");
    CHECK(model.LevelAt(0, 3, 1) == "positive" && model.LevelAt(0, 1, 3) == "positive",
          "it reads back either way round");
    CHECK(model.SetCell(0, "B", "D", "negative") && model.cells.size() == 1,
          "setting the same pair the other way round replaces it");
    CHECK(!model.SetCellAt(0, 2, 2, "positive"), "the diagonal has no cell");
    CHECK(model.FindCell(0, 2, 2) == nullptr, "and finds none");
    CHECK(model.ClearCell(0, 3, 1) && model.cells.empty(), "clearing works either way round");

    MatrixModel handMade = model;
    MatrixCell below;
    below.panel = 0; below.row = 3; below.col = 1; below.levelId = "positive";
    handMade.cells.push_back(below);
    CHECK(!handMade.Validate().valid,
          "a hand-assembled cell below the diagonal is rejected by Validate()");

    CHECK(model.FindSymmetricPanel("req") == 0, "FindSymmetricPanel finds the roof");
    CHECK(model.FindSymmetricPanel("other") == -1, "and nothing for another set");

    MatrixScale c = MatrixScale::Correlation();
    CHECK(c.levels.size() == 4 && c.levels[0].shape == MatrixMarkShape::Text,
          "the Correlation preset has four text levels");
    CHECK(c.WeightOf("strongnegative") < 0.0, "its negative levels carry negative weights");
}

static void TestHouseValidation() {
    std::printf("\n[House shape]\n");

    MatrixModel house = MatrixSamples::HouseOfQuality();
    CHECK(house.Validate().valid, "the House of Quality sample validates");
    CHECK(house.shape == MatrixShape::House, "it is House-shaped");
    CHECK(house.panels.size() == 2 && house.panels[1].symmetric, "its second panel is the roof");
    CHECK(house.panels[1].rowSetId == house.panels[0].colSetId,
          "the roof correlates the body's columns");
    CHECK(house.LevelAt(1, 1, 2) == "strongpositive",
          "tyre width strongly helps braking distance");
    const MatrixItemSet* reqs = house.FindSet("requirements");
    CHECK(reqs && reqs->items[0].improvement == MatrixImprovement::Minimize &&
          reqs->items[0].target == "< 11 kg",
          "columns carry a direction of improvement and a target");

    MatrixModel symmetricBody = house;
    symmetricBody.panels[0].symmetric = true;
    CHECK(!symmetricBody.Validate().valid, "a House whose body is symmetric is rejected");

    MatrixModel wrongRoof = house;
    wrongRoof.panels[1].rowSetId = wrongRoof.panels[1].colSetId = "needs";
    CHECK(!wrongRoof.Validate().valid, "a roof over the rows rather than the columns is rejected");

    MatrixModel noRoof = house;
    noRoof.panels.pop_back();
    noRoof.cells.erase(std::remove_if(noRoof.cells.begin(), noRoof.cells.end(),
                                      [](const MatrixCell& c) { return c.panel == 1; }),
                       noRoof.cells.end());
    CHECK(noRoof.Validate().valid, "a House without a roof is still a valid House");

    MatrixModel plainRoof = house;
    plainRoof.panels[1].symmetric = false;
    CHECK(!plainRoof.Validate().valid, "a roof that is not symmetric is rejected");
}

static void TestRelativeWeights() {
    std::printf("\n[relative weights]\n");

    MatrixModel qfd = MatrixSamples::QualityFunctionDeployment();
    std::vector<double> weights = qfd.RelativeColumnWeights(0);
    double sum = 0.0;
    for (double w : weights) sum += w;
    CHECK(weights.size() == 7, "one weight per column");
    CHECK(std::fabs(sum - 100.0) < 1e-9, "the weights add up to 100 percent");

    double total = 0.0;
    for (int c = 0; c < 7; ++c) total += qfd.ColumnScore(0, c);
    const int braking = qfd.sets[1].IndexOf("Braking distance");
    CHECK(NearlyEqual(weights[static_cast<size_t>(braking)], 84.0 * 100.0 / total),
          "each is its column's share of the total score");

    MatrixModel empty = MakeToolsMatrix();
    empty.ClearCells();
    std::vector<double> none = empty.RelativeColumnWeights(0);
    bool allZero = true;
    for (double w : none) allZero = allZero && w == 0.0;
    CHECK(!none.empty() && allZero, "a panel with no score gives zeros, not NaN");
}

static void TestImportanceColumn() {
    std::printf("\n[importance column]\n");

    MatrixModel qfd = MatrixSamples::QualityFunctionDeployment();
    const std::string csv = ExportMatrixCsv(qfd, 0, true);
    CHECK(csv.rfind(",Importance,", 0) == 0, "the header names the importance column");
    CHECK(csv.find("\nLight to carry,5,") != std::string::npos,
          "importances are written dot-decimal next to their row");

    MatrixModel copy = qfd;
    for (MatrixItem& item : copy.sets[0].items) item.importance = 1.0;
    copy.ClearCells();
    CHECK(ImportMatrixCsv(csv, copy, 0), "ImportMatrixCsv reads the importance column");
    CHECK(NearlyEqual(copy.sets[0].items[0].importance, 5.0) &&
          NearlyEqual(copy.sets[0].items[1].importance, 4.0),
          "and restores the importances");
    CHECK(NearlyEqual(copy.ColumnScore(0, 2), qfd.ColumnScore(0, 2)),
          "so the weighted scores survive the round trip");
}

static void TestImportTable() {
    std::printf("\n[ImportMatrixTable]\n");

    // Marks typed four ways: id, label in another case, glyph-less weight,
    // and blank. Quoted fields and CRLF line ends, as a spreadsheet writes.
    const std::string sheet =
            ",Importance,Frame mass,\"Tyre, width\",Braking\r\n"
            "Light,5,strong,3,\r\n"
            "Safe,4.5,,Moderate,9\r\n"
            "Cheap,,weak,,1\r\n";
    MatrixModel model;
    std::string error;
    CHECK(ImportMatrixTable(sheet, MatrixScale::QFD(), model, "Needs", "Requirements", &error),
          "a spreadsheet-style table imports");
    CHECK(model.Validate().valid, "into a valid model");
    CHECK(model.shape == MatrixShape::L && model.sets.size() == 2 && model.panels.size() == 1,
          "an L with two sets and one panel");
    CHECK(model.sets[0].title == "Needs" && model.sets[1].title == "Requirements",
          "titled as asked");
    CHECK(model.sets[1].Count() == 3 && model.sets[1].items[1].label == "Tyre, width",
          "quoted column labels keep their commas");
    CHECK(model.LevelAt(0, 0, 0) == "strong", "a mark by id");
    CHECK(model.LevelAt(0, 1, 1) == "moderate", "a mark by label, any case");
    CHECK(model.LevelAt(0, 0, 1) == "moderate" && model.LevelAt(0, 1, 2) == "strong",
          "a mark by weight");
    CHECK(model.LevelAt(0, 0, 2).empty(), "a blank cell stays empty");
    CHECK(NearlyEqual(model.sets[0].items[1].importance, 4.5),
          "importances read dot-decimal");
    CHECK(NearlyEqual(model.sets[0].items[2].importance, 1.0),
          "a blank importance defaults to 1");

    MatrixModel before = model;
    CHECK(!ImportMatrixTable(",A,B\nRow,strong,huge\n", MatrixScale::QFD(), model, "R", "C", &error),
          "an unknown mark fails");
    CHECK(error.find("huge") != std::string::npos, "and the error names it");
    CHECK(model.sets[0].Count() == before.sets[0].Count(), "leaving the model untouched");
    CHECK(!ImportMatrixTable(",A,B\nRow,strong\n", MatrixScale::QFD(), model, "R", "C", &error),
          "a ragged row fails");
    CHECK(!ImportMatrixTable(",Importance,A\nRow,five,strong\n", MatrixScale::QFD(), model,
                             "R", "C", &error),
          "an importance that is not a number fails");
    CHECK(!ImportMatrixTable(",A,A\nRow,strong,weak\n", MatrixScale::QFD(), model, "R", "C", &error),
          "a repeated column label fails");

    // Excel writes a UTF-8 byte-order mark first; the sheet must read the same.
    MatrixModel withBom;
    CHECK(ImportMatrixTable("\xEF\xBB\xBF,Importance,A\nRow,2,strong\n", MatrixScale::QFD(),
                            withBom, "R", "C", &error) &&
          withBom.sets[1].items[0].label == "A" &&
          NearlyEqual(withBom.sets[0].items[0].importance, 2.0),
          "a sheet starting with a byte-order mark imports unchanged");

    // The tabular form is what ExportMatrixCsv writes, so the two round-trip.
    MatrixModel tools = MakeToolsMatrix();
    MatrixModel reread;
    CHECK(ImportMatrixTable(ExportMatrixCsv(tools, 0), MatrixScale::Strength531(), reread),
          "an exported panel imports as a table");
    CHECK(NearlyEqual(reread.RowScore(0, 0), 18.0), "with the same totals");
}

static void TestJsonRoundTrip() {
    std::printf("\n[JSON]\n");

    // Every sample, every shape: written, read back, written again - the two
    // documents must be identical, which compares every set, item, panel,
    // level, cell and header field at once.
    struct Sample { const char* name; MatrixModel (*build)(); };
    const Sample samples[] = {
            {"L (improvement tools)", &MatrixSamples::ImprovementTools},
            {"T (employee allocation)", &MatrixSamples::EmployeeAllocation},
            {"L with importances (QFD)", &MatrixSamples::QualityFunctionDeployment},
            {"X (supply chain)", &MatrixSamples::SupplyChain},
            {"Y (design traceability)", &MatrixSamples::DesignTraceability},
            {"House of Quality", &MatrixSamples::HouseOfQuality},
    };
    for (const Sample& sample : samples) {
        const MatrixModel original = sample.build();
        const std::string written = ExportMatrixJson(original);
        MatrixModel reread;
        std::string error;
        const bool ok = ImportMatrixJson(written, reread, &error);
        if (!ok) std::printf("    %s\n", error.c_str());
        CHECK(ok && ExportMatrixJson(reread) == written,
              (std::string(sample.name) + " round-trips unchanged").c_str());
    }

    MatrixModel house = MatrixSamples::HouseOfQuality();
    MatrixModel reread;
    CHECK(ImportMatrixJson(ExportMatrixJson(house, false), reread),
          "the compact form reads back too");
    CHECK(reread.shape == MatrixShape::House && reread.panels[1].symmetric,
          "the shape and the roof survive");
    CHECK(reread.FindSet("requirements")->items[0].improvement == MatrixImprovement::Minimize &&
          reread.FindSet("requirements")->items[0].target == "< 11 kg",
          "directions and targets survive");
    CHECK(NearlyEqual(reread.ColumnScore(0, 2), house.ColumnScore(0, 2)),
          "importances survive, so the scores do");
    CHECK(reread.headerFields.size() == 3, "the form header survives");

    // Cells are written by item id, so a file whose items come in another
    // order still puts every mark where it was.
    JSONValue doc = MatrixModelToJson(MatrixSamples::ImprovementTools());
    JSONParseResult parsed;
    JSONValue items = JSON::Parse(JSON::Serialize(doc.Get("sets").At(1).Get("items")), &parsed);
    JSONValue reversed = JSONValue::MakeArray();
    for (size_t i = items.GetSize(); i-- > 0;) reversed.Append(items.At(i));
    JSONValue sets = JSONValue::MakeArray();
    sets.Append(doc.Get("sets").At(0));
    JSONValue tools = doc.Get("sets").At(1);
    tools.Set("items", reversed);
    sets.Append(tools);
    doc.Set("sets", sets);
    MatrixModel reordered;
    CHECK(MatrixModelFromJson(doc, reordered), "a file with its columns reordered reads");
    CHECK(reordered.sets[1].items[0].label == "5S" &&
          reordered.LevelAt(0, 0, 7) == MatrixSamples::ImprovementTools().LevelAt(0, 0, 0),
          "and each mark follows its item, not its old position");

    // A roof pair written below the diagonal lands above it.
    JSONValue roofDoc = MatrixModelToJson(MatrixSamples::HouseOfQuality());
    JSONValue cells = JSONValue::MakeArray();
    cells.Append(JSONValue::MakeObject().Set("panel", 1).Set("row", "Gear range")
                 .Set("column", "Frame mass").Set("level", "positive"));
    roofDoc.Set("cells", cells);
    MatrixModel roofModel;
    CHECK(MatrixModelFromJson(roofDoc, roofModel) && roofModel.cells.size() == 1 &&
          roofModel.cells[0].row < roofModel.cells[0].col,
          "a roof pair written either way round is stored above the diagonal");

    // Failures name the problem and leave the caller's model alone.
    MatrixModel keep = MatrixSamples::ImprovementTools();
    const size_t keepCells = keep.cells.size();
    std::string error;
    CHECK(!ImportMatrixJson("{ \"format\": ", keep, &error) && error.find("line") != std::string::npos,
          "malformed JSON fails with a line number");
    CHECK(!ImportMatrixJson("{\"format\":\"something-else\",\"version\":1}", keep, &error),
          "another format is refused");
    CHECK(!ImportMatrixJson("{\"format\":\"ultracanvas-matrix\",\"version\":99}", keep, &error) &&
          error.find("99") != std::string::npos,
          "a newer version is refused, by number");

    JSONValue badLevel = MatrixModelToJson(MatrixSamples::ImprovementTools());
    JSONValue badCells = JSONValue::MakeArray();
    badCells.Append(JSONValue::MakeObject().Set("panel", 0).Set("row", "HR")
                    .Set("column", "SPC").Set("level", "enormous"));
    badLevel.Set("cells", badCells);
    CHECK(!MatrixModelFromJson(badLevel, keep, &error) && error.find("enormous") != std::string::npos,
          "a cell with an unknown level is refused, by name");

    JSONValue badShape = MatrixModelToJson(MatrixSamples::ImprovementTools());
    badShape.Set("shape", "Z");
    CHECK(!MatrixModelFromJson(badShape, keep, &error), "an unknown shape is refused");

    JSONValue undrawable = MatrixModelToJson(MatrixSamples::ImprovementTools());
    undrawable.Set("shape", "X");
    CHECK(!MatrixModelFromJson(undrawable, keep, &error), "a model its shape cannot draw is refused");
    CHECK(keep.cells.size() == keepCells && keep.shape == MatrixShape::L,
          "after every failure the model is untouched");
}

static void TestCompetitiveAssessment() {
    std::printf("\n[competitive assessment]\n");

    MatrixModel house = MatrixSamples::HouseOfQuality();
    CHECK(house.competitors.size() == 3 && house.ratings.size() == 18,
          "the House sample rates three products on six needs");
    double v = 0.0;
    CHECK(house.RatingOf("safe", "urban", v) && v == 5.0, "a rating reads back by need and competitor");
    CHECK(!house.RatingOf("safe", "nobody", v), "an unknown competitor has no rating");

    CHECK(house.SetRating("safe", "ours", 4.0) && house.RatingOf("safe", "ours", v) && v == 4.0 &&
          house.ratings.size() == 18,
          "setting a rating again replaces it");
    CHECK(!house.SetRating("Frame mass", "ours", 3.0),
          "a column item is not a need and cannot be rated");
    CHECK(!house.SetRating("safe", "nobody", 3.0), "nor can an unknown competitor");
    CHECK(!house.SetRating("safe", "ours", 0.0) && !house.SetRating("safe", "ours", 6.0),
          "ratings outside 1..5 are refused");
    CHECK(!house.SetRating("safe", "ours", std::nan("")), "and so is NaN");
    house.ratingMax = 10.0;
    CHECK(house.SetRating("safe", "ours", 8.0), "a wider scale takes wider values");

    MatrixModel bad = MatrixSamples::HouseOfQuality();
    bad.ratings.push_back({"safe", "ghost", 3.0});
    CHECK(!bad.Validate().valid, "a hand-made rating of an unknown competitor fails validation");
    MatrixModel twice = MatrixSamples::HouseOfQuality();
    twice.AddCompetitor("ours", "Copy", Color(0, 0, 0, 255));
    CHECK(!twice.Validate().valid, "a competitor id used twice fails validation");
    MatrixModel range = MatrixSamples::HouseOfQuality();
    range.ratings[0].value = 9.0;
    CHECK(!range.Validate().valid, "a hand-made rating past ratingMax fails validation");

    MatrixModel reread;
    std::string error;
    CHECK(ImportMatrixJson(ExportMatrixJson(MatrixSamples::HouseOfQuality()), reread, &error) &&
          reread.competitors.size() == 3 && reread.ratings.size() == 18 &&
          reread.competitors[1].mark == MatrixMarkShape::Square,
          "competitors and ratings survive JSON");
    CHECK(reread.RatingOf("light", "cruiser", v) && v == 4.0, "with their values");

    JSONValue doc = MatrixModelToJson(MatrixSamples::HouseOfQuality());
    JSONValue ratings = JSONValue::MakeArray();
    ratings.Append(JSONValue::MakeObject().Set("item", "light").Set("competitor", "ours").Set("value", 7.0));
    doc.Set("ratings", ratings);
    MatrixModel keep = MatrixSamples::ImprovementTools();
    CHECK(!MatrixModelFromJson(doc, keep, &error) && error.find("light") != std::string::npos &&
          keep.competitors.empty(),
          "a file with an out-of-range rating is refused, by name, model untouched");

    keep.Clear();
    CHECK(keep.competitors.empty() && keep.ratings.empty() && keep.ratingMax == 5.0,
          "Clear() drops the assessment too");
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
    TestXValidation();
    TestYValidation();
    TestHeaderFields();
    TestPhase2Samples();
    TestSymmetricPanel();
    TestHouseValidation();
    TestRelativeWeights();
    TestImportanceColumn();
    TestImportTable();
    TestJsonRoundTrip();
    TestCompetitiveAssessment();

    std::printf("\n================================\n");
    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}

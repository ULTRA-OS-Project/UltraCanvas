// Plugins/Diagrams/UltraCanvasPerformanceMatrixModel.cpp
// The UI-free model of the performance matrix and its sample models.
// Version: 1.1.0 - the population: placement, counts, groups, targets, SampleTeam
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasPerformanceMatrixModel.h"
#include <cmath>
#include <set>
#include <utility>

namespace UltraCanvas {

// =============================================================================
// MODEL
// =============================================================================

    const PerformanceCell* PerformanceMatrixModel::FindCell(int row, int col) const {
        for (const PerformanceCell& cell : cells) {
            if (cell.row == row && cell.col == col) return &cell;
        }
        return nullptr;
    }

    void PerformanceMatrixModel::SetCell(const PerformanceCell& cell) {
        for (PerformanceCell& existing : cells) {
            if (existing.row == cell.row && existing.col == cell.col) {
                existing = cell;
                return;
            }
        }
        cells.push_back(cell);
    }

    int PerformanceMatrixModel::Rank(int row, int col) const {
        const int fromX = highXIsBest ? Columns() - 1 - col : col;
        const int fromY = highYIsBest ? Rows() - 1 - row : row;
        return fromX + fromY;
    }

    int PerformanceMatrixModel::LevelForScore(const PerformanceAxis& axis, double score) {
        const int n = static_cast<int>(axis.levels.size());
        if (n == 0 || !std::isfinite(score)) return -1;
        const double step = (axis.maxScore - axis.minScore) / n;
        for (int i = 0; i + 1 < n; ++i) {
            const double own = axis.levels[static_cast<size_t>(i)].upperBound;
            const double bound = std::isfinite(own) ? own : axis.minScore + step * (i + 1);
            // A score on a bound belongs to the level above it.
            if (score < bound) return i;
        }
        return n - 1;
    }

    PerformanceCellRef PerformanceMatrixModel::CellOf(const PerformanceMatrixItem& item) const {
        if (item.row >= 0 && item.col >= 0) return PerformanceCellRef(item.row, item.col);
        const int row = LevelForScore(yAxis, item.yScore);
        const int col = LevelForScore(xAxis, item.xScore);
        if (row < 0 || col < 0) return PerformanceCellRef();
        return PerformanceCellRef(row, col);
    }

    namespace {
        bool Hidden(const std::vector<std::string>& hiddenGroups, const std::string& group) {
            return std::find(hiddenGroups.begin(), hiddenGroups.end(), group) != hiddenGroups.end();
        }
    }

    std::vector<int> PerformanceMatrixModel::CountsPerCell(const std::vector<std::string>& hiddenGroups) const {
        std::vector<int> counts(static_cast<size_t>(Rows() * Columns()), 0);
        for (const PerformanceMatrixItem& item : items) {
            if (Hidden(hiddenGroups, item.group)) continue;
            const PerformanceCellRef cell = CellOf(item);
            if (Contains(cell.row, cell.col)) ++counts[static_cast<size_t>(cell.row * Columns() + cell.col)];
        }
        return counts;
    }

    std::vector<int> PerformanceMatrixModel::CountsPerRow(const std::vector<std::string>& hiddenGroups) const {
        std::vector<int> counts(static_cast<size_t>(Rows()), 0);
        const std::vector<int> cells = CountsPerCell(hiddenGroups);
        for (int r = 0; r < Rows(); ++r) {
            for (int c = 0; c < Columns(); ++c) counts[static_cast<size_t>(r)] += cells[static_cast<size_t>(r * Columns() + c)];
        }
        return counts;
    }

    std::vector<int> PerformanceMatrixModel::CountsPerColumn(const std::vector<std::string>& hiddenGroups) const {
        std::vector<int> counts(static_cast<size_t>(Columns()), 0);
        const std::vector<int> cells = CountsPerCell(hiddenGroups);
        for (int r = 0; r < Rows(); ++r) {
            for (int c = 0; c < Columns(); ++c) counts[static_cast<size_t>(c)] += cells[static_cast<size_t>(r * Columns() + c)];
        }
        return counts;
    }

    std::vector<std::string> PerformanceMatrixModel::Groups() const {
        std::vector<std::string> groups;
        for (const PerformanceMatrixItem& item : items) {
            if (std::find(groups.begin(), groups.end(), item.group) == groups.end()) groups.push_back(item.group);
        }
        return groups;
    }

    std::vector<std::string> PerformanceMatrixModel::Validate() const {
        std::vector<std::string> problems;
        auto checkAxis = [&](const PerformanceAxis& axis, const std::string& name) {
            if (axis.levels.empty()) {
                problems.push_back(name + " axis has no levels");
            } else if (static_cast<int>(axis.levels.size()) > kMaxLevels) {
                problems.push_back(name + " axis has " + std::to_string(axis.levels.size()) +
                                   " levels; at most " + std::to_string(kMaxLevels) + " fit");
            }
            for (size_t i = 0; i < axis.levels.size(); ++i) {
                if (axis.levels[i].label.empty()) {
                    problems.push_back(name + " axis level " + std::to_string(i) + " has no label");
                }
            }
        };
        checkAxis(xAxis, "X");
        checkAxis(yAxis, "Y");
        auto checkScores = [&](const PerformanceAxis& axis, const std::string& name) {
            if (!(axis.minScore < axis.maxScore)) {
                problems.push_back(name + " axis score range is empty or reversed");
                return;
            }
            double previous = axis.minScore;
            for (size_t i = 0; i + 1 < axis.levels.size(); ++i) {
                const double bound = axis.levels[i].upperBound;
                if (!std::isfinite(bound)) continue;
                if (bound <= previous || bound >= axis.maxScore) {
                    problems.push_back(name + " axis level \"" + axis.levels[i].label +
                                       "\" ends out of order or outside the score range");
                }
                previous = std::max(previous, bound);
            }
        };
        checkScores(xAxis, "X");
        checkScores(yAxis, "Y");

        std::set<std::pair<int, int>> seen;
        for (const PerformanceCell& cell : cells) {
            const std::string where = "(" + std::to_string(cell.row) + ", " + std::to_string(cell.col) + ")";
            if (!Contains(cell.row, cell.col)) {
                problems.push_back("Cell " + where + " is outside the " + std::to_string(Rows()) + "x" +
                                   std::to_string(Columns()) + " grid");
            }
            if (!seen.insert({cell.row, cell.col}).second) {
                problems.push_back("Two cells at " + where);
            }
            if (cell.badge < 0) problems.push_back("Cell " + where + " has a negative badge");
        }

        for (const PerformanceMatrixItem& item : items) {
            const std::string name = "Item \"" + (item.label.empty() ? item.id : item.label) + "\"";
            const PerformanceCellRef cell = CellOf(item);
            if (!cell.IsValid()) problems.push_back(name + " has neither a cell nor two scores");
            else if (!Contains(cell.row, cell.col)) problems.push_back(name + " is outside the grid");
        }

        if (!targetDistribution.empty()) {
            if (static_cast<int>(targetDistribution.size()) != Rows() * Columns()) {
                problems.push_back("The target distribution has " + std::to_string(targetDistribution.size()) +
                                   " shares for " + std::to_string(Rows() * Columns()) + " cells");
            }
            double sum = 0.0;
            bool negative = false;
            for (double share : targetDistribution) {
                negative |= !(share >= 0.0);
                sum += share;
            }
            if (negative) problems.push_back("The target distribution has a negative share");
            if (std::abs(sum - 1.0) > 1e-6) problems.push_back("The target distribution does not sum to 1");
        }
        return problems;
    }

// =============================================================================
// SAMPLES
// =============================================================================

    namespace PerformanceMatrixSamples {

        namespace {
            PerformanceCell Cell(int row, int col, std::string title, std::string description = std::string(),
                                 std::string action = std::string()) {
                PerformanceCell cell;
                cell.row = row;
                cell.col = col;
                cell.title = std::move(title);
                cell.description = std::move(description);
                cell.action = std::move(action);
                return cell;
            }
        }

        PerformanceMatrixModel NineBox() {
            PerformanceMatrixModel model;
            model.title = "9-box talent grid";
            model.xAxis.title = "Performance";
            model.xAxis.levels = {{"Low"}, {"Moderate"}, {"High"}};
            model.yAxis.title = "Potential";
            model.yAxis.levels = {{"Low"}, {"Moderate"}, {"High"}};
            for (PerformanceAxis* axis : {&model.xAxis, &model.yAxis}) {
                axis->minScore = 1.0;     // a 1-5 review scale
                axis->maxScore = 5.0;
            }
            model.cells = {
                Cell(2, 0, "Enigma", "High potential, not yet delivering", "Coach and clarify the role"),
                Cell(2, 1, "Growth Employee", "High potential, solid results", "Stretch assignments"),
                Cell(2, 2, "Future Leader", "Top results and room to grow", "Promote and retain"),
                Cell(1, 0, "Dilemma", "Some potential, weak results", "Find the blocker"),
                Cell(1, 1, "Core Employee", "Reliable and steady", "Develop in place"),
                Cell(1, 2, "High Impact", "Strong results, some growth", "Broaden the role"),
                Cell(0, 0, "Under-performer", "Below expectations", "Improve, move or exit"),
                Cell(0, 1, "Effective", "Meets expectations", "Keep engaged"),
                Cell(0, 2, "Trusted Professional", "Expert in the current role", "Recognise and reward"),
            };
            for (PerformanceCell& cell : model.cells) cell.badge = cell.row * 3 + cell.col + 1;
            return model;
        }

        PerformanceMatrixModel PerformanceManagement() {
            PerformanceMatrixModel model;
            model.title = "Performance management matrix";
            model.xAxis.title = "Potential";
            model.xAxis.levels = {{"Potential Engagement", "", Color(226, 61, 61, 255)},
                                  {"Potential Ability", "", Color(46, 164, 79, 255)},
                                  {"Potential Aspiration", "", Color(243, 146, 32, 255)}};
            model.yAxis.title = "Performance";
            model.yAxis.levels = {{"Performance Not Meeting", "", Color(226, 61, 61, 255)},
                                  {"Performance Meeting", "", Color(243, 146, 32, 255)},
                                  {"Performance Exceeding", "", Color(46, 164, 79, 255)}};
            model.cells = {
                Cell(2, 0, "Expert"),
                Cell(2, 1, "Consistent Exceptional Delivery"),
                Cell(2, 2, "Star with Potential"),
                Cell(1, 0, "Sound Performance"),
                Cell(1, 1, "Contributor"),
                Cell(1, 2, "Rising & Developing Contributor & Performer"),
                Cell(0, 0, "Questionable Fit / Poor Performance"),
                Cell(0, 1, "Not Yet Performing at the Required Level"),
                Cell(0, 2, "Needs Development"),
            };
            return model;
        }

        PerformanceMatrixModel PerformanceVsPotential() {
            PerformanceMatrixModel model;
            model.title = "Performance vs potential";
            model.xAxis.title = "Performance";
            model.xAxis.levels = {{"Needs Development"}, {"Meets Expectations"}, {"Exceeds Expectations"}};
            model.yAxis.title = "Leadership Potential";
            model.yAxis.levels = {{"Limited"}, {"Moderate"}, {"High"}};
            const char* performance[] = {"Poor Performance", "Good Performance", "Outstanding Performance"};
            const char* potential[] = {"Limited Potential", "Moderate Potential", "High Potential"};
            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 3; ++col) {
                    PerformanceCell cell;
                    cell.row = row;
                    cell.col = col;
                    // Counted from the best corner: 1A is high potential and
                    // outstanding performance.
                    cell.code = std::to_string(3 - row) + static_cast<char>('A' + (2 - col));
                    cell.description = std::string(performance[col]) + "\n" + potential[row];
                    model.cells.push_back(cell);
                }
            }
            return model;
        }

        PerformanceMatrixModel TalentReview() {
            PerformanceMatrixModel model;
            model.title = "9 box matrix for talent management and performance review";
            model.xAxis.title = "Performance";
            model.xAxis.levels = {{"Low"}, {"Med"}, {"High"}};
            model.yAxis.title = "Potential";
            model.yAxis.levels = {{"Little / None", "Contribute and Utilize", Color(232, 120, 160, 255)},
                                  {"Average", "Develop", Color(240, 150, 60, 255)},
                                  {"Lots", "Move and Retain", Color(232, 190, 40, 255)}};
            const std::vector<std::vector<std::string>> bullets = {
                {"Below expectations", "Low growth potential", "Improve, move or exit"},
                {"Meets expectations", "Little growth expected", "Utilise in current role"},
                {"Expert in current role", "Limited wish to move", "Keep engaged as specialist"},
                {"Inconsistent results", "Clarify expectations", "Agree a performance plan"},
                {"Core contributor", "Steady performance", "Develop current skills"},
                {"Strong performer", "Deepen expertise", "Recognise and reward"},
                {"New in role or misplaced", "Potential not yet shown", "Coach closely, review fit"},
                {"Solid performer", "High potential", "Stretch assignments"},
                {"Top performer", "Ready for a bigger role", "Retain: succession candidate"},
            };
            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 3; ++col) {
                    PerformanceCell cell;
                    cell.row = row;
                    cell.col = col;
                    cell.badge = row * 3 + col + 1;
                    cell.bullets = bullets[static_cast<size_t>(row * 3 + col)];
                    model.cells.push_back(cell);
                }
            }
            return model;
        }

        PerformanceMatrixModel SkillWill() {
            PerformanceMatrixModel model;
            model.title = "Skill / will matrix";
            model.xAxis.title = "Skill";
            model.xAxis.levels = {{"Low skill"}, {"High skill"}};
            model.yAxis.title = "Will";
            model.yAxis.levels = {{"Low will"}, {"High will"}};
            model.cells = {
                Cell(1, 0, "Guide", "Willing but not yet able", "Teach, coach, build confidence"),
                Cell(1, 1, "Delegate", "Able and willing", "Give freedom and responsibility"),
                Cell(0, 0, "Direct", "Neither able nor motivated", "Set clear tasks, supervise closely"),
                Cell(0, 1, "Excite", "Able but not motivated", "Find out why, re-engage"),
            };
            return model;
        }

        std::vector<PerformanceMatrixItem> SampleTeam() {
            // Invented people. Scores are (performance, potential) on 1-5.
            struct Scored { const char* name; const char* group; double x, y; };
            const Scored scored[] = {
                {"Avery Lund", "Engineering", 4.6, 4.5},   {"Bo Hartmann", "Engineering", 3.9, 4.1},
                {"Cai Moreno", "Engineering", 3.2, 3.0},   {"Dana Whitlock", "Engineering", 2.9, 3.4},
                {"Eli Novak", "Engineering", 3.5, 2.6},    {"Fen Okafor", "Engineering", 4.2, 3.2},
                {"Gale Sutter", "Engineering", 2.2, 2.0},  {"Hollis Brandt", "Engineering", 3.0, 3.7},
                {"Ira Castell", "Engineering", 3.6, 3.5},  {"Jun Ferreira", "Engineering", 1.9, 3.9},
                {"Kit Larsen", "Engineering", 3.3, 3.8},   {"Lior Becker", "Engineering", 4.4, 2.2},
                {"Mika Daly", "Sales", 4.7, 3.9},          {"Noor Sandoval", "Sales", 3.1, 3.2},
                {"Oak Pellegrini", "Sales", 2.6, 2.7},     {"Pax Hendry", "Sales", 3.8, 3.3},
                {"Quin Adeyemi", "Sales", 3.4, 4.3},       {"Rin Vasquez", "Sales", 2.4, 3.1},
                {"Sol Ambrose", "Sales", 3.2, 3.6},        {"Tam Kowalski", "Sales", 4.1, 2.1},
                {"Uri Lindqvist", "Sales", 1.8, 1.9},      {"Val Osei", "Sales", 3.5, 3.1},
                {"Wren Takahashi", "Support", 2.8, 2.0},   {"Xan Delacroix", "Support", 3.7, 3.8},
                {"Yael Brook", "Support", 2.8, 3.3},       {"Zev Marchetti", "Support", 2.0, 2.9},
                {"Ash Kincaid", "Support", 4.0, 3.5},      {"Blair Oyelaran", "Support", 2.5, 2.3},
                {"Cove Ritter", "Support", 3.3, 3.4},      {"Drew Halvorsen", "Support", 3.9, 3.0},
            };
            std::vector<PerformanceMatrixItem> team;
            for (const Scored& s : scored) team.emplace_back(s.name, s.group, s.x, s.y);
            // Moved in calibration: an explicit cell overrides the scores.
            struct Placed { const char* name; const char* group; int row, col; double x, y; };
            const Placed placed[] = {
                {"Emery Voss", "Engineering", 2, 2, 3.9, 4.0},
                {"Finch Abara", "Sales", 1, 1, 2.9, 3.6},
                {"Glen Ostrowski", "Support", 0, 1, 3.1, 2.9},
                {"Harper Nyland", "Sales", 2, 1, 3.4, 3.3},
                {"Indy Rasmussen", "Support", 1, 2, 3.6, 3.2},
                {"Jules Amari", "Engineering", 1, 0, 2.1, 3.3},
            };
            for (const Placed& p : placed) {
                PerformanceMatrixItem item(p.name, p.group, p.row, p.col);
                item.xScore = p.x;
                item.yScore = p.y;
                item.note = "Moved in calibration";
                team.push_back(item);
            }
            for (size_t i = 0; i < team.size(); ++i) team[i].id = "p" + std::to_string(i + 1);
            return team;
        }

        std::vector<double> SampleTargetDistribution() {
            // Row-major from row 0 (low potential): low / moderate / high performance.
            return {0.05, 0.10, 0.05,
                    0.10, 0.30, 0.15,
                    0.05, 0.10, 0.10};
        }

    } // namespace PerformanceMatrixSamples

} // namespace UltraCanvas

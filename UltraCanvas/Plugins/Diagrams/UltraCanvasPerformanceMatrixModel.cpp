// Plugins/Diagrams/UltraCanvasPerformanceMatrixModel.cpp
// The UI-free model of the performance matrix and its sample models.
// Version: 1.0.0
// Last Modified: 2026-10-27
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasPerformanceMatrixModel.h"
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

    } // namespace PerformanceMatrixSamples

} // namespace UltraCanvas

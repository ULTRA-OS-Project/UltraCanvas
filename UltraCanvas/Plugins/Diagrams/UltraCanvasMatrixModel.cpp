// Plugins/Diagrams/UltraCanvasMatrixModel.cpp
// Matrix diagram model: item sets, panels, cell marks and the weighted roll-ups
// Version: 1.0.0
// Last Modified: 2026-08-11
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasMatrixModel.h"
#include <algorithm>
#include <numeric>
#include <sstream>

namespace UltraCanvas {

// =============================================================================
// SHAPE
// =============================================================================

    int MatrixShapeSetCount(MatrixShape shape) {
        switch (shape) {
            case MatrixShape::L:     return 2;
            case MatrixShape::T:     return 3;
            case MatrixShape::Y:     return 3;
            case MatrixShape::X:     return 4;
            case MatrixShape::House: return 2;
        }
        return 0;
    }

    int MatrixShapePanelCount(MatrixShape shape) {
        switch (shape) {
            case MatrixShape::L:     return 1;
            case MatrixShape::T:     return 2;
            case MatrixShape::Y:     return 3;
            case MatrixShape::X:     return 4;
            case MatrixShape::House: return 2;   // the body plus the correlation roof
        }
        return 0;
    }

// =============================================================================
// SCALE
// =============================================================================

    int MatrixScale::IndexOf(const std::string& levelId) const {
        for (size_t i = 0; i < levels.size(); ++i) {
            if (levels[i].id == levelId) return static_cast<int>(i);
        }
        return -1;
    }

    const MatrixLevel* MatrixScale::Find(const std::string& levelId) const {
        int idx = IndexOf(levelId);
        return idx < 0 ? nullptr : &levels[static_cast<size_t>(idx)];
    }

    double MatrixScale::WeightOf(const std::string& levelId) const {
        const MatrixLevel* level = Find(levelId);
        return level ? level->weight : 0.0;
    }

    MatrixScale MatrixScale::QFD() {
        MatrixScale s;
        s.title = "Relationship";
        s.levels.emplace_back("strong",   "Strong",   Color(200,  40,  40, 255), 9.0, MatrixMarkShape::Disc);
        s.levels.emplace_back("moderate", "Moderate", Color( 40,  90, 190, 255), 3.0, MatrixMarkShape::Ring);
        s.levels.emplace_back("weak",     "Weak",     Color(120, 125, 135, 255), 1.0, MatrixMarkShape::Triangle);
        return s;
    }

    MatrixScale MatrixScale::Strength531() {
        MatrixScale s;
        s.title = "Relationship";
        s.levels.emplace_back("strong", "Strong", Color(214,  45,  45, 255), 5.0, MatrixMarkShape::Disc);
        s.levels.emplace_back("medium", "Medium", Color( 35,  70, 180, 255), 3.0, MatrixMarkShape::Disc);
        s.levels.emplace_back("weak",   "Weak",   Color(150, 155, 165, 255), 1.0, MatrixMarkShape::Disc);
        return s;
    }

    MatrixScale MatrixScale::HighMediumLow() {
        MatrixScale s;
        s.title = "Amount";
        s.levels.emplace_back("high",   "High",   Color( 35,  35,  40, 255), 3.0, MatrixMarkShape::Disc);
        s.levels.emplace_back("medium", "Medium", Color(235, 190,  20, 255), 2.0, MatrixMarkShape::Disc);
        // Low is hollow rather than pale so the three levels stay apart in
        // greyscale and for readers who cannot separate the two hues.
        s.levels.emplace_back("low",    "Low",    Color( 80,  85,  95, 255), 1.0, MatrixMarkShape::Ring);
        return s;
    }

    MatrixScale MatrixScale::Presence() {
        MatrixScale s;
        s.title = "Applies";
        s.levels.emplace_back("yes", "Yes", Color(35, 35, 40, 255), 1.0, MatrixMarkShape::Disc);
        return s;
    }

// =============================================================================
// ITEM SET
// =============================================================================

    int MatrixItemSet::IndexOf(const std::string& itemId) const {
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].id == itemId) return static_cast<int>(i);
        }
        return -1;
    }

// =============================================================================
// MODEL - SETS
// =============================================================================

    int MatrixModel::IndexOfSet(const std::string& setId) const {
        for (size_t i = 0; i < sets.size(); ++i) {
            if (sets[i].id == setId) return static_cast<int>(i);
        }
        return -1;
    }

    MatrixItemSet* MatrixModel::FindSet(const std::string& setId) {
        int idx = IndexOfSet(setId);
        return idx < 0 ? nullptr : &sets[static_cast<size_t>(idx)];
    }

    const MatrixItemSet* MatrixModel::FindSet(const std::string& setId) const {
        int idx = IndexOfSet(setId);
        return idx < 0 ? nullptr : &sets[static_cast<size_t>(idx)];
    }

    MatrixItemSet& MatrixModel::AddSet(const std::string& setId, const std::string& title,
                                       const std::vector<std::string>& itemLabels) {
        MatrixItemSet set(setId, title);
        set.items.reserve(itemLabels.size());
        for (const std::string& label : itemLabels) {
            // The label doubles as the id when the caller does not need to
            // address items separately, which is the common case for a demo or
            // a one-off diagram.
            set.items.emplace_back(label, label);
        }
        sets.push_back(std::move(set));
        return sets.back();
    }

// =============================================================================
// MODEL - PANELS
// =============================================================================

    MatrixPanel& MatrixModel::AddPanel(const std::string& rowSetId,
                                       const std::string& colSetId,
                                       const MatrixScale& scale) {
        panels.emplace_back(rowSetId, colSetId, scale);
        return panels.back();
    }

    const MatrixItemSet* MatrixModel::RowSet(int panel) const {
        if (panel < 0 || panel >= static_cast<int>(panels.size())) return nullptr;
        return FindSet(panels[static_cast<size_t>(panel)].rowSetId);
    }

    const MatrixItemSet* MatrixModel::ColSet(int panel) const {
        if (panel < 0 || panel >= static_cast<int>(panels.size())) return nullptr;
        return FindSet(panels[static_cast<size_t>(panel)].colSetId);
    }

// =============================================================================
// MODEL - CELLS
// =============================================================================

    bool MatrixModel::SetCell(int panel, const std::string& rowItemId,
                              const std::string& colItemId, const std::string& levelId) {
        const MatrixItemSet* rows = RowSet(panel);
        const MatrixItemSet* cols = ColSet(panel);
        if (!rows || !cols) return false;

        int row = rows->IndexOf(rowItemId);
        int col = cols->IndexOf(colItemId);
        if (row < 0 || col < 0) return false;

        return SetCellAt(panel, row, col, levelId);
    }

    bool MatrixModel::SetCellAt(int panel, int row, int col, const std::string& levelId,
                                const std::string& note) {
        if (panel < 0 || panel >= static_cast<int>(panels.size())) return false;

        const MatrixItemSet* rows = RowSet(panel);
        const MatrixItemSet* cols = ColSet(panel);
        if (!rows || !cols) return false;
        if (row < 0 || row >= rows->Count()) return false;
        if (col < 0 || col >= cols->Count()) return false;

        // Reject an unknown level rather than storing a mark nothing can draw.
        if (panels[static_cast<size_t>(panel)].scale.IndexOf(levelId) < 0) return false;

        for (MatrixCell& cell : cells) {
            if (cell.panel == panel && cell.row == row && cell.col == col) {
                cell.levelId = levelId;
                cell.note = note;
                return true;
            }
        }

        MatrixCell cell;
        cell.panel = panel;
        cell.row = row;
        cell.col = col;
        cell.levelId = levelId;
        cell.note = note;
        cells.push_back(cell);
        return true;
    }

    bool MatrixModel::ClearCell(int panel, int row, int col) {
        auto it = std::find_if(cells.begin(), cells.end(), [&](const MatrixCell& c) {
            return c.panel == panel && c.row == row && c.col == col;
        });
        if (it == cells.end()) return false;
        cells.erase(it);
        return true;
    }

    void MatrixModel::ClearCells() {
        cells.clear();
    }

    const MatrixCell* MatrixModel::FindCell(int panel, int row, int col) const {
        for (const MatrixCell& cell : cells) {
            if (cell.panel == panel && cell.row == row && cell.col == col) return &cell;
        }
        return nullptr;
    }

    std::string MatrixModel::LevelAt(int panel, int row, int col) const {
        const MatrixCell* cell = FindCell(panel, row, col);
        return cell ? cell->levelId : std::string();
    }

// =============================================================================
// MODEL - ROLL-UPS
// =============================================================================

    double MatrixModel::ColumnScore(int panel, int col) const {
        const MatrixItemSet* rows = RowSet(panel);
        if (!rows) return 0.0;
        const MatrixScale& scale = panels[static_cast<size_t>(panel)].scale;

        double total = 0.0;
        for (const MatrixCell& cell : cells) {
            if (cell.panel != panel || cell.col != col) continue;
            if (cell.row < 0 || cell.row >= rows->Count()) continue;
            total += rows->items[static_cast<size_t>(cell.row)].importance
                     * scale.WeightOf(cell.levelId);
        }
        return total;
    }

    double MatrixModel::RowScore(int panel, int row) const {
        const MatrixItemSet* rows = RowSet(panel);
        if (!rows || row < 0 || row >= rows->Count()) return 0.0;
        const MatrixScale& scale = panels[static_cast<size_t>(panel)].scale;

        double total = 0.0;
        for (const MatrixCell& cell : cells) {
            if (cell.panel != panel || cell.row != row) continue;
            total += scale.WeightOf(cell.levelId);
        }
        return total * rows->items[static_cast<size_t>(row)].importance;
    }

    std::vector<int> MatrixModel::RankColumns(int panel) const {
        const MatrixItemSet* cols = ColSet(panel);
        if (!cols) return {};

        std::vector<int> order(static_cast<size_t>(cols->Count()));
        std::iota(order.begin(), order.end(), 0);

        std::vector<double> scores(order.size());
        for (size_t i = 0; i < order.size(); ++i) {
            scores[i] = ColumnScore(panel, static_cast<int>(i));
        }

        // stable_sort keeps equal scores in column order, so the ranking does
        // not shuffle between renders.
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return scores[static_cast<size_t>(a)] > scores[static_cast<size_t>(b)];
        });
        return order;
    }

// =============================================================================
// MODEL - VALIDATION
// =============================================================================

    MatrixValidation MatrixModel::Validate() const {
        MatrixValidation result;

        auto fail = [&result](const std::string& message) {
            result.errors.push_back(message);
            result.valid = false;
        };

        const int wantedSets = MatrixShapeSetCount(shape);
        if (static_cast<int>(sets.size()) < wantedSets) {
            fail("shape needs " + std::to_string(wantedSets) + " item sets, model has "
                 + std::to_string(sets.size()));
        }

        const int wantedPanels = MatrixShapePanelCount(shape);
        if (static_cast<int>(panels.size()) < wantedPanels) {
            fail("shape needs " + std::to_string(wantedPanels) + " panels, model has "
                 + std::to_string(panels.size()));
        }

        for (size_t s = 0; s < sets.size(); ++s) {
            if (sets[s].id.empty()) fail("set " + std::to_string(s) + " has an empty id");
            if (sets[s].items.empty()) {
                result.warnings.push_back("set '" + sets[s].id + "' has no items");
            }
            for (size_t i = 0; i < sets[s].items.size(); ++i) {
                for (size_t j = i + 1; j < sets[s].items.size(); ++j) {
                    if (sets[s].items[i].id == sets[s].items[j].id) {
                        fail("set '" + sets[s].id + "' has a duplicate item id '"
                             + sets[s].items[i].id + "'");
                    }
                }
            }
        }

        for (size_t p = 0; p < panels.size(); ++p) {
            const std::string where = "panel " + std::to_string(p);
            if (!FindSet(panels[p].rowSetId)) {
                fail(where + " references unknown row set '" + panels[p].rowSetId + "'");
            }
            if (!FindSet(panels[p].colSetId)) {
                fail(where + " references unknown column set '" + panels[p].colSetId + "'");
            }
            if (panels[p].scale.levels.empty()) {
                fail(where + " has an empty relationship scale");
            }
            if (panels[p].symmetric && panels[p].rowSetId != panels[p].colSetId) {
                fail(where + " is symmetric but crosses two different sets");
            }
        }

        for (size_t c = 0; c < cells.size(); ++c) {
            const MatrixCell& cell = cells[c];
            const std::string where = "cell " + std::to_string(c);

            if (cell.panel < 0 || cell.panel >= static_cast<int>(panels.size())) {
                fail(where + " references unknown panel " + std::to_string(cell.panel));
                continue;
            }
            const MatrixItemSet* rows = RowSet(cell.panel);
            const MatrixItemSet* cols = ColSet(cell.panel);
            if (!rows || !cols) continue;   // already reported against the panel

            if (cell.row < 0 || cell.row >= rows->Count()) {
                fail(where + " row " + std::to_string(cell.row) + " is out of range");
            }
            if (cell.col < 0 || cell.col >= cols->Count()) {
                fail(where + " column " + std::to_string(cell.col) + " is out of range");
            }
            if (panels[static_cast<size_t>(cell.panel)].scale.IndexOf(cell.levelId) < 0) {
                fail(where + " uses level '" + cell.levelId + "' which its panel's scale lacks");
            }
        }

        for (size_t a = 0; a < cells.size(); ++a) {
            for (size_t b = a + 1; b < cells.size(); ++b) {
                if (cells[a].panel == cells[b].panel &&
                    cells[a].row == cells[b].row &&
                    cells[a].col == cells[b].col) {
                    fail("duplicate cells at panel " + std::to_string(cells[a].panel) +
                         " (" + std::to_string(cells[a].row) + ", " +
                         std::to_string(cells[a].col) + ")");
                }
            }
        }

        return result;
    }

    void MatrixModel::Clear() {
        title.clear();
        subtitle.clear();
        sets.clear();
        panels.clear();
        cells.clear();
    }

// =============================================================================
// INTERCHANGE
// =============================================================================

    namespace {

        std::string CsvEscape(const std::string& field) {
            const bool needsQuotes =
                    field.find(',') != std::string::npos ||
                    field.find('"') != std::string::npos ||
                    field.find('\n') != std::string::npos;
            if (!needsQuotes) return field;

            std::string out = "\"";
            for (char c : field) {
                if (c == '"') out += '"';   // doubled, per RFC 4180
                out += c;
            }
            out += '"';
            return out;
        }

        // Split one CSV line, honouring quoted fields and doubled quotes.
        std::vector<std::string> CsvSplit(const std::string& line) {
            std::vector<std::string> fields;
            std::string current;
            bool inQuotes = false;

            for (size_t i = 0; i < line.size(); ++i) {
                const char c = line[i];
                if (inQuotes) {
                    if (c == '"') {
                        if (i + 1 < line.size() && line[i + 1] == '"') {
                            current += '"';
                            ++i;
                        } else {
                            inQuotes = false;
                        }
                    } else {
                        current += c;
                    }
                } else if (c == '"') {
                    inQuotes = true;
                } else if (c == ',') {
                    fields.push_back(current);
                    current.clear();
                } else if (c != '\r') {
                    current += c;
                }
            }
            fields.push_back(current);
            return fields;
        }

    } // namespace

    std::string ExportMatrixCsv(const MatrixModel& model, int panel) {
        const MatrixItemSet* rows = model.RowSet(panel);
        const MatrixItemSet* cols = model.ColSet(panel);
        if (!rows || !cols) return std::string();

        std::ostringstream out;

        // Header: an empty corner cell, then the column labels.
        out << "";
        for (const MatrixItem& col : cols->items) {
            out << ',' << CsvEscape(col.label);
        }
        out << '\n';

        for (int r = 0; r < rows->Count(); ++r) {
            out << CsvEscape(rows->items[static_cast<size_t>(r)].label);
            for (int c = 0; c < cols->Count(); ++c) {
                out << ',' << CsvEscape(model.LevelAt(panel, r, c));
            }
            out << '\n';
        }
        return out.str();
    }

    bool ImportMatrixCsv(const std::string& csv, MatrixModel& model, int panel) {
        const MatrixItemSet* rows = model.RowSet(panel);
        const MatrixItemSet* cols = model.ColSet(panel);
        if (!rows || !cols) return false;

        std::istringstream in(csv);
        std::string line;
        if (!std::getline(in, line)) return false;

        std::vector<std::string> header = CsvSplit(line);
        if (header.size() != static_cast<size_t>(cols->Count()) + 1) return false;
        for (int c = 0; c < cols->Count(); ++c) {
            if (header[static_cast<size_t>(c) + 1] != cols->items[static_cast<size_t>(c)].label) {
                return false;
            }
        }

        // Parse everything before touching the model, so a malformed file
        // leaves the caller's marks alone.
        struct PendingCell { int row; int col; std::string levelId; };
        std::vector<PendingCell> pending;
        int rowIndex = 0;

        while (std::getline(in, line)) {
            if (line.empty()) continue;
            if (rowIndex >= rows->Count()) return false;

            std::vector<std::string> fields = CsvSplit(line);
            if (fields.size() != static_cast<size_t>(cols->Count()) + 1) return false;
            if (fields[0] != rows->items[static_cast<size_t>(rowIndex)].label) return false;

            for (int c = 0; c < cols->Count(); ++c) {
                const std::string& levelId = fields[static_cast<size_t>(c) + 1];
                if (levelId.empty()) continue;
                if (model.panels[static_cast<size_t>(panel)].scale.IndexOf(levelId) < 0) {
                    return false;
                }
                pending.push_back({rowIndex, c, levelId});
            }
            ++rowIndex;
        }

        // Drop this panel's existing marks, then apply.
        model.cells.erase(std::remove_if(model.cells.begin(), model.cells.end(),
                                         [panel](const MatrixCell& c) { return c.panel == panel; }),
                          model.cells.end());
        for (const PendingCell& cell : pending) {
            model.SetCellAt(panel, cell.row, cell.col, cell.levelId);
        }
        return true;
    }

// =============================================================================
// SAMPLES
// =============================================================================

    namespace MatrixSamples {

        MatrixModel ImprovementTools() {
            MatrixModel model;
            model.title = "Application of improvement tools";
            model.subtitle = "Which quality tools each department applies";
            model.shape = MatrixShape::L;

            model.AddSet("departments", "Departments",
                         {"HR", "Finance", "Purchasing", "IT", "Quality",
                          "Marketing", "Maintenance", "Production", "Store"});
            model.AddSet("tools", "Improvement tools",
                         {"Graphical Analysis", "SPC", "KPIs", "Data Collection Methods",
                          "Cause and Effect", "Process Mapping", "Kaizen", "5S"});
            model.AddPanel("departments", "tools", MatrixScale::Strength531());

            // Rows as (department, one level id per tool; empty = no relationship).
            struct Row { const char* dept; const char* marks[8]; };
            static const Row rows[] = {
                {"HR",          {"medium", "weak",   "medium", "strong", "weak",   "weak",   "medium", "weak"}},
                {"Finance",     {"strong", "",       "medium", "",       "",       "weak",   "",       ""}},
                {"Purchasing",  {"weak",   "weak",   "weak",   "medium", "weak",   "weak",   "",       ""}},
                {"IT",          {"weak",   "weak",   "weak",   "weak",   "medium", "",      "weak",   "medium"}},
                {"Quality",     {"strong", "strong", "medium", "strong", "strong", "strong", "medium", "weak"}},
                {"Marketing",   {"medium", "",       "strong", "strong", "weak",   "weak",   "",       ""}},
                {"Maintenance", {"strong", "strong", "medium", "strong", "strong", "strong", "medium", "medium"}},
                {"Production",  {"strong", "strong", "medium", "strong", "strong", "strong", "strong", "strong"}},
                {"Store",       {"weak",   "",       "weak",   "weak",   "weak",   "",       "weak",   "strong"}},
            };

            for (const Row& row : rows) {
                for (int c = 0; c < 8; ++c) {
                    if (row.marks[c][0] == '\0') continue;
                    model.SetCell(0, row.dept,
                                  model.sets[1].items[static_cast<size_t>(c)].id,
                                  row.marks[c]);
                }
            }
            return model;
        }

        MatrixModel EmployeeAllocation() {
            MatrixModel model;
            model.title = "Allocating employees to improvement projects";
            model.subtitle = "Projects on the left, skills on the right, employees down the spine";
            model.shape = MatrixShape::T;

            model.AddSet("employees", "Employees",
                         {"Harvey", "Sami", "Emir", "Zakaria", "Shadi", "Peter", "Sara"});
            model.AddSet("projects", "Projects",
                         {"Stretch wrap usage", "Paper usage", "Visual management",
                          "Safety management", "Spoilage reduction", "Energy saving"});
            model.AddSet("skills", "Skills",
                         {"SPC", "5S", "SOP", "Sampling", "FMEA", "Graphical methods"});

            // The left wing records assignment (present or absent); the right
            // wing records how strong each skill is. Different questions, so
            // different scales - which is why the scale lives on the panel.
            model.AddPanel("employees", "projects", MatrixScale::Presence());
            model.AddPanel("employees", "skills", MatrixScale::HighMediumLow());

            struct Assignment { const char* who; const char* projects[6]; };
            static const Assignment projectRows[] = {
                {"Harvey",  {"yes", "",    "yes", "",    "yes", ""}},
                {"Sami",    {"",    "yes", "",    "yes", "",    "yes"}},
                {"Emir",    {"yes", "yes", "",    "",    "yes", ""}},
                {"Zakaria", {"",    "",    "yes", "yes", "",    "yes"}},
                {"Shadi",   {"yes", "",    "yes", "",    "",    "yes"}},
                {"Peter",   {"",    "yes", "",    "yes", "yes", ""}},
                {"Sara",    {"yes", "",    "",    "yes", "",    "yes"}},
            };
            for (const Assignment& row : projectRows) {
                for (int c = 0; c < 6; ++c) {
                    if (row.projects[c][0] == '\0') continue;
                    model.SetCell(0, row.who,
                                  model.sets[1].items[static_cast<size_t>(c)].id,
                                  row.projects[c]);
                }
            }

            struct SkillRow { const char* who; const char* skills[6]; };
            static const SkillRow skillRows[] = {
                {"Harvey",  {"high",   "medium", "high",   "low",    "medium", "high"}},
                {"Sami",    {"medium", "high",   "medium", "high",   "low",    "medium"}},
                {"Emir",    {"low",    "high",   "medium", "medium", "",       "low"}},
                {"Zakaria", {"high",   "medium", "low",    "high",   "high",   "medium"}},
                {"Shadi",   {"medium", "low",    "high",   "",       "medium", "high"}},
                {"Peter",   {"high",   "high",   "medium", "medium", "high",   "low"}},
                {"Sara",    {"low",    "medium", "medium", "high",   "low",    "high"}},
            };
            for (const SkillRow& row : skillRows) {
                for (int c = 0; c < 6; ++c) {
                    if (row.skills[c][0] == '\0') continue;
                    model.SetCell(1, row.who,
                                  model.sets[2].items[static_cast<size_t>(c)].id,
                                  row.skills[c]);
                }
            }
            return model;
        }

        MatrixModel QualityFunctionDeployment() {
            MatrixModel model;
            model.title = "House of Quality - commuter bicycle";
            model.subtitle = "Customer needs weighted by importance against technical requirements";
            model.shape = MatrixShape::L;

            MatrixItemSet needs("needs", "Customer needs");
            // The importance column is what turns a relationship grid into a
            // ranking: the column totals below are weighted by these.
            needs.items.emplace_back("light",     "Light to carry",          5.0);
            needs.items.emplace_back("comfort",   "Comfortable over 10 km",  4.0);
            needs.items.emplace_back("safe",      "Safe in traffic",         5.0);
            needs.items.emplace_back("weather",   "Usable in the wet",       3.0);
            needs.items.emplace_back("lowmaint",  "Little maintenance",      4.0);
            needs.items.emplace_back("secure",    "Hard to steal",           2.0);
            model.sets.push_back(needs);

            model.AddSet("requirements", "Technical requirements",
                         {"Frame mass", "Tyre width", "Braking distance", "Gear range",
                          "Mudguard coverage", "Lighting output", "Lock mounting"});

            model.AddPanel("needs", "requirements", MatrixScale::QFD());

            struct Row { const char* need; const char* marks[7]; };
            static const Row rows[] = {
                {"light",    {"strong",   "moderate", "",         "weak",     "moderate", "",         "weak"}},
                {"comfort",  {"moderate", "strong",   "",         "strong",   "",         "",         ""}},
                {"safe",     {"",         "moderate", "strong",   "",         "",         "strong",   ""}},
                {"weather",  {"",         "moderate", "strong",   "",         "strong",   "moderate", ""}},
                {"lowmaint", {"weak",     "moderate", "moderate", "moderate", "weak",     "weak",     ""}},
                {"secure",   {"weak",     "",         "",         "",         "",         "",         "strong"}},
            };
            for (const Row& row : rows) {
                for (int c = 0; c < 7; ++c) {
                    if (row.marks[c][0] == '\0') continue;
                    model.SetCell(0, row.need,
                                  model.sets[1].items[static_cast<size_t>(c)].id,
                                  row.marks[c]);
                }
            }
            return model;
        }

    } // namespace MatrixSamples

} // namespace UltraCanvas

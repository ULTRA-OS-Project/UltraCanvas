// Plugins/Diagrams/UltraCanvasMatrixModel.cpp
// Matrix diagram model: item sets, panels, cell marks and the weighted roll-ups
// Version: 1.2.0 - House of Quality: symmetric (roof) panels, improvement direction and target per item, Correlation scale, relative weights, table import with importances
// Version: 1.1.0 - form header fields; X and Y panel checks in Validate(); X and Y samples
// Version: 1.0.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasMatrixModel.h"
#include "UltraCanvasTextUtils.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>
#include <sstream>
#include <utility>

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
            case MatrixShape::House: return 1;   // the body; the correlation roof is optional
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

    MatrixScale MatrixScale::Correlation() {
        MatrixScale s;
        s.title = "Correlation";
        // U+2212 MINUS SIGN rather than a hyphen: the same width as the plus,
        // and in every UI font.
        s.levels.emplace_back("strongpositive", "Strong positive", Color( 20, 110,  60, 255), 2.0, MatrixMarkShape::Text);
        s.levels.back().glyph = "++";
        s.levels.emplace_back("positive",       "Positive",        Color( 60, 150,  90, 255), 1.0, MatrixMarkShape::Text);
        s.levels.back().glyph = "+";
        s.levels.emplace_back("negative",       "Negative",        Color(200,  80,  60, 255), -1.0, MatrixMarkShape::Text);
        s.levels.back().glyph = "\xE2\x88\x92";
        s.levels.emplace_back("strongnegative", "Strong negative", Color(160,  30,  30, 255), -2.0, MatrixMarkShape::Text);
        s.levels.back().glyph = "\xE2\x88\x92\xE2\x88\x92";
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

    namespace {

        // A symmetric panel keeps one cell per pair, above the diagonal.
        // Returns false for the diagonal, which relates an item to itself and
        // so has no cell.
        bool NormalizeSymmetric(const std::vector<MatrixPanel>& panels, int panel,
                                int& row, int& col) {
            if (panel < 0 || panel >= static_cast<int>(panels.size())) return true;
            if (!panels[static_cast<size_t>(panel)].symmetric) return true;
            if (row == col) return false;
            if (row > col) std::swap(row, col);
            return true;
        }

    } // namespace

    bool MatrixModel::SetCellAt(int panel, int row, int col, const std::string& levelId,
                                const std::string& note) {
        if (panel < 0 || panel >= static_cast<int>(panels.size())) return false;
        if (!NormalizeSymmetric(panels, panel, row, col)) return false;

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
        if (!NormalizeSymmetric(panels, panel, row, col)) return false;
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
        if (!NormalizeSymmetric(panels, panel, row, col)) return nullptr;
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

    std::vector<double> MatrixModel::RelativeColumnWeights(int panel) const {
        const MatrixItemSet* cols = ColSet(panel);
        if (!cols) return {};

        std::vector<double> weights(static_cast<size_t>(cols->Count()), 0.0);
        double total = 0.0;
        for (int c = 0; c < cols->Count(); ++c) {
            weights[static_cast<size_t>(c)] = ColumnScore(panel, c);
            total += weights[static_cast<size_t>(c)];
        }
        // A panel with no score has no shares; dividing would give NaN.
        if (total == 0.0) return std::vector<double>(weights.size(), 0.0);
        for (double& w : weights) w = w * 100.0 / total;
        return weights;
    }

    int MatrixModel::FindSymmetricPanel(const std::string& setId) const {
        for (size_t p = 0; p < panels.size(); ++p) {
            if (panels[p].symmetric && panels[p].rowSetId == setId &&
                panels[p].colSetId == setId) {
                return static_cast<int>(p);
            }
        }
        return -1;
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

        // An X places sets[0] left, sets[1] top, sets[2] right, sets[3]
        // bottom, so a panel is drawable only when it pairs a left/right set
        // with a top/bottom one - that pair names its quadrant, and no two
        // panels may claim the same quadrant. A Y has a panel between each two
        // of its three seams, so it needs two different sets among the first
        // three, each pair at most once.
        if ((shape == MatrixShape::X && sets.size() >= 4) ||
            (shape == MatrixShape::Y && sets.size() >= 3)) {
            const int placed = (shape == MatrixShape::X) ? 4 : 3;
            std::vector<std::pair<int, int>> claimed;
            for (size_t p = 0; p < panels.size(); ++p) {
                const std::string where = "panel " + std::to_string(p);
                const int r = IndexOfSet(panels[p].rowSetId);
                const int c = IndexOfSet(panels[p].colSetId);
                if (r < 0 || c < 0) continue;   // already reported above
                if (r >= placed || c >= placed) {
                    fail(where + " uses a set beyond the " + std::to_string(placed)
                         + " this shape places");
                    continue;
                }
                if (shape == MatrixShape::X && (r % 2) == (c % 2)) {
                    fail(where + " crosses two sets on the same axis of the X; pair a "
                         "left/right set (0 or 2) with a top/bottom one (1 or 3)");
                    continue;
                }
                if (shape == MatrixShape::Y && r == c) {
                    fail(where + " crosses a set with itself, which a Y cannot place");
                    continue;
                }
                const std::pair<int, int> key(std::min(r, c), std::max(r, c));
                if (std::find(claimed.begin(), claimed.end(), key) != claimed.end()) {
                    fail(where + " relates the same two sets as an earlier panel");
                    continue;
                }
                claimed.push_back(key);
            }
        }

        // A House is an L with a roof: panel 0 the body, crossing two
        // different sets, and any roof a symmetric panel over the body's
        // columns - the technical requirements it correlates.
        if (shape == MatrixShape::House && !panels.empty()) {
            const MatrixPanel& body = panels[0];
            if (body.symmetric || body.rowSetId == body.colSetId) {
                fail("panel 0 of a House must cross two different sets");
            }
            for (size_t p = 1; p < panels.size(); ++p) {
                if (!panels[p].symmetric || panels[p].rowSetId != body.colSetId) {
                    fail("panel " + std::to_string(p) + " of a House must be the symmetric "
                         "roof over the body's column set '" + body.colSetId + "'");
                }
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
            if (panels[static_cast<size_t>(cell.panel)].symmetric && cell.row >= cell.col) {
                fail(where + " lies on or below the diagonal of a symmetric panel");
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
        headerFields.clear();
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

    namespace {

        std::string Lower(std::string text) {
            for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }

        // The header a column of row importances goes under.
        bool IsImportanceHeader(const std::string& field) {
            const std::string h = Lower(Trim(field));
            return h == "importance" || h == "weight";
        }

    } // namespace

    std::string ExportMatrixCsv(const MatrixModel& model, int panel, bool withImportance) {
        const MatrixItemSet* rows = model.RowSet(panel);
        const MatrixItemSet* cols = model.ColSet(panel);
        if (!rows || !cols) return std::string();

        std::ostringstream out;

        // Header: an empty corner cell, then the column labels.
        out << "";
        if (withImportance) out << ",Importance";
        for (const MatrixItem& col : cols->items) {
            out << ',' << CsvEscape(col.label);
        }
        out << '\n';

        for (int r = 0; r < rows->Count(); ++r) {
            out << CsvEscape(rows->items[static_cast<size_t>(r)].label);
            if (withImportance) {
                out << ',' << FormatFloatClassic(rows->items[static_cast<size_t>(r)].importance);
            }
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
        // An "Importance" column after the row labels, as ExportMatrixCsv
        // writes with withImportance, shifts the marks one field right.
        const size_t first = (header.size() == static_cast<size_t>(cols->Count()) + 2 &&
                              IsImportanceHeader(header[1])) ? 2 : 1;
        if (header.size() != static_cast<size_t>(cols->Count()) + first) return false;
        for (int c = 0; c < cols->Count(); ++c) {
            if (header[static_cast<size_t>(c) + first] != cols->items[static_cast<size_t>(c)].label) {
                return false;
            }
        }
        std::vector<double> importances;

        // Parse everything before touching the model, so a malformed file
        // leaves the caller's marks alone.
        struct PendingCell { int row; int col; std::string levelId; };
        std::vector<PendingCell> pending;
        int rowIndex = 0;

        while (std::getline(in, line)) {
            if (line.empty()) continue;
            if (rowIndex >= rows->Count()) return false;

            std::vector<std::string> fields = CsvSplit(line);
            if (fields.size() != static_cast<size_t>(cols->Count()) + first) return false;
            if (fields[0] != rows->items[static_cast<size_t>(rowIndex)].label) return false;
            if (first == 2) {
                double importance = 0.0;
                if (!TryParseFloat(Trim(fields[1]), importance)) return false;
                importances.push_back(importance);
            }

            for (int c = 0; c < cols->Count(); ++c) {
                const std::string& levelId = fields[static_cast<size_t>(c) + first];
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
        if (MatrixItemSet* rowSet = model.FindSet(model.panels[static_cast<size_t>(panel)].rowSetId)) {
            for (size_t r = 0; r < importances.size() && r < rowSet->items.size(); ++r) {
                rowSet->items[r].importance = importances[r];
            }
        }
        return true;
    }

    bool ImportMatrixTable(const std::string& csv, const MatrixScale& scale, MatrixModel& out,
                           const std::string& rowTitle, const std::string& colTitle,
                           std::string* error) {
        auto failWith = [error](const std::string& message) {
            if (error) *error = message;
            return false;
        };
        if (scale.levels.empty()) return failWith("the scale has no levels");

        // A mark is matched by id, then label (any case), then glyph, then
        // weight: the forms people actually type into a sheet.
        auto levelFor = [&scale](const std::string& text, std::string& levelId) {
            const std::string want = Lower(text);
            for (const MatrixLevel& level : scale.levels) {
                if (level.id == text || Lower(level.label) == want ||
                    (!level.glyph.empty() && level.glyph == text)) {
                    levelId = level.id;
                    return true;
                }
            }
            double weight = 0.0;
            if (TryParseFloat(text, weight)) {
                for (const MatrixLevel& level : scale.levels) {
                    if (std::fabs(level.weight - weight) < 1e-9) {
                        levelId = level.id;
                        return true;
                    }
                }
            }
            return false;
        };

        std::istringstream in(csv);
        std::string line;
        if (!std::getline(in, line)) return failWith("the table is empty");

        const std::vector<std::string> header = CsvSplit(line);
        const size_t first = (header.size() >= 3 && IsImportanceHeader(header[1])) ? 2 : 1;
        if (header.size() <= first) return failWith("the header row names no columns");

        MatrixModel model;
        model.shape = MatrixShape::L;
        MatrixItemSet rows("rows", rowTitle);
        MatrixItemSet cols("columns", colTitle);
        for (size_t c = first; c < header.size(); ++c) {
            const std::string label = Trim(header[c]);
            if (label.empty()) return failWith("column " + std::to_string(c) + " has no label");
            if (cols.IndexOf(label) >= 0) return failWith("column '" + label + "' appears twice");
            cols.items.emplace_back(label, label);
        }

        struct PendingCell { int row; int col; std::string levelId; };
        std::vector<PendingCell> pending;
        int lineNumber = 1;
        while (std::getline(in, line)) {
            ++lineNumber;
            if (Trim(line).empty()) continue;
            const std::string where = "line " + std::to_string(lineNumber);

            const std::vector<std::string> fields = CsvSplit(line);
            if (fields.size() != header.size()) {
                return failWith(where + " has " + std::to_string(fields.size()) + " fields, the header "
                                + std::to_string(header.size()));
            }
            const std::string label = Trim(fields[0]);
            if (label.empty()) return failWith(where + " has no row label");
            if (rows.IndexOf(label) >= 0) return failWith("row '" + label + "' appears twice");

            double importance = 1.0;
            if (first == 2 && !Trim(fields[1]).empty() &&
                !TryParseFloat(Trim(fields[1]), importance)) {
                return failWith(where + ": importance '" + fields[1] + "' is not a number");
            }
            rows.items.emplace_back(label, label, importance);

            const int row = rows.Count() - 1;
            for (size_t c = first; c < fields.size(); ++c) {
                const std::string mark = Trim(fields[c]);
                if (mark.empty()) continue;
                std::string levelId;
                if (!levelFor(mark, levelId)) {
                    return failWith(where + ": '" + mark + "' is not a level of the scale");
                }
                pending.push_back({row, static_cast<int>(c - first), levelId});
            }
        }
        if (rows.items.empty()) return failWith("the table has no rows");

        model.sets.push_back(rows);
        model.sets.push_back(cols);
        model.AddPanel("rows", "columns", scale);
        for (const PendingCell& cell : pending) {
            model.SetCellAt(0, cell.row, cell.col, cell.levelId);
        }
        out = model;
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

        MatrixModel SupplyChain() {
            MatrixModel model;
            model.title = "Supply chain matrix";
            model.subtitle = "Who supplies which store, which store serves which customer, "
                             "and which line builds for whom";
            model.shape = MatrixShape::X;

            model.headerFields = {
                    {"PROJECT", "Warehouse consolidation"},
                    {"AREA",    "Logistics"},
                    {"TITLE",   "Material flow review"},
                    {"TEAM",    "Ops planning"},
                    {"DATE",    "2026-10-13"},
            };

            // Left, top, right, bottom - the order the X places them in.
            model.AddSet("suppliers", "Suppliers",
                         {"Supplier A", "Supplier B", "Supplier C", "Supplier D", "Supplier E"});
            model.AddSet("storage", "Storage areas",
                         {"W/H 1", "W/H 2", "W/H 3", "W/H 4"});
            model.AddSet("customers", "Customers",
                         {"P-1042", "P-1187", "P-2210", "P-3005", "P-3311"});
            model.AddSet("lines", "Production lines",
                         {"Line 1", "Line 2", "Line 3", "Line 4"});

            // A x B, B x C, C x D, D x A. Each panel names a left/right set
            // and a top/bottom one; which of the two is the row set is up to
            // the caller - the layout turns the panel to fit its quadrant.
            model.AddPanel("suppliers", "storage", MatrixScale::HighMediumLow());   // turned: rows run across
            model.AddPanel("storage", "customers", MatrixScale::HighMediumLow());
            model.AddPanel("lines", "customers", MatrixScale::HighMediumLow());
            model.AddPanel("lines", "suppliers", MatrixScale::HighMediumLow());

            struct Mark { int panel; const char* row; const char* col; const char* level; };
            static const Mark marks[] = {
                {0, "Supplier A", "W/H 1", "high"},   {0, "Supplier C", "W/H 1", "low"},
                {0, "Supplier B", "W/H 2", "high"},   {0, "Supplier D", "W/H 2", "medium"},
                {0, "Supplier C", "W/H 3", "medium"}, {0, "Supplier E", "W/H 3", "high"},
                {0, "Supplier A", "W/H 4", "low"},    {0, "Supplier D", "W/H 4", "high"},

                {1, "W/H 1", "P-1042", "high"},   {1, "W/H 1", "P-2210", "medium"},
                {1, "W/H 2", "P-1187", "high"},   {1, "W/H 2", "P-3311", "low"},
                {1, "W/H 3", "P-3005", "high"},   {1, "W/H 3", "P-2210", "low"},
                {1, "W/H 4", "P-3311", "medium"}, {1, "W/H 4", "P-1187", "low"},

                {2, "Line 1", "P-1042", "high"},   {2, "Line 1", "P-1187", "medium"},
                {2, "Line 2", "P-2210", "high"},   {2, "Line 2", "P-3005", "low"},
                {2, "Line 3", "P-3005", "high"},   {2, "Line 3", "P-3311", "medium"},
                {2, "Line 4", "P-3311", "high"},   {2, "Line 4", "P-1042", "low"},

                {3, "Line 1", "Supplier A", "high"},   {3, "Line 1", "Supplier B", "low"},
                {3, "Line 2", "Supplier C", "high"},   {3, "Line 2", "Supplier D", "medium"},
                {3, "Line 3", "Supplier E", "high"},   {3, "Line 3", "Supplier B", "medium"},
                {3, "Line 4", "Supplier D", "high"},   {3, "Line 4", "Supplier A", "low"},
            };
            for (const Mark& m : marks) model.SetCell(m.panel, m.row, m.col, m.level);
            return model;
        }

        MatrixModel DesignTraceability() {
            MatrixModel model;
            model.title = "Design traceability";
            model.subtitle = "Requirements to functions to components, and back to requirements";
            model.shape = MatrixShape::Y;

            model.AddSet("requirements", "Requirements",
                         {"Quiet", "Long battery life", "Easy to clean", "Compact"});
            model.AddSet("functions", "Functions",
                         {"Generate suction", "Store energy", "Filter air", "Collect dust"});
            model.AddSet("components", "Components",
                         {"Motor", "Battery pack", "HEPA filter", "Dust bin", "Housing"});

            model.AddPanel("requirements", "functions", MatrixScale::QFD());
            model.AddPanel("functions", "components", MatrixScale::QFD());
            model.AddPanel("components", "requirements", MatrixScale::QFD());

            struct Mark { int panel; const char* row; const char* col; const char* level; };
            static const Mark marks[] = {
                {0, "Quiet", "Generate suction", "strong"},
                {0, "Quiet", "Filter air", "weak"},
                {0, "Long battery life", "Store energy", "strong"},
                {0, "Long battery life", "Generate suction", "moderate"},
                {0, "Easy to clean", "Collect dust", "strong"},
                {0, "Easy to clean", "Filter air", "moderate"},
                {0, "Compact", "Store energy", "moderate"},
                {0, "Compact", "Collect dust", "weak"},

                {1, "Generate suction", "Motor", "strong"},
                {1, "Generate suction", "Housing", "weak"},
                {1, "Store energy", "Battery pack", "strong"},
                {1, "Filter air", "HEPA filter", "strong"},
                {1, "Filter air", "Housing", "moderate"},
                {1, "Collect dust", "Dust bin", "strong"},

                {2, "Motor", "Quiet", "strong"},
                {2, "Motor", "Long battery life", "moderate"},
                {2, "Battery pack", "Long battery life", "strong"},
                {2, "Battery pack", "Compact", "moderate"},
                {2, "HEPA filter", "Easy to clean", "weak"},
                {2, "Dust bin", "Easy to clean", "strong"},
                {2, "Housing", "Compact", "strong"},
                {2, "Housing", "Quiet", "moderate"},
            };
            for (const Mark& m : marks) model.SetCell(m.panel, m.row, m.col, m.level);
            return model;
        }

        MatrixModel HouseOfQuality() {
            MatrixModel model = QualityFunctionDeployment();
            model.title = "House of Quality - commuter bicycle";
            model.subtitle = "Needs against requirements, with how the requirements help or "
                             "hinder each other in the roof";
            model.shape = MatrixShape::House;
            model.headerFields = {
                    {"PRODUCT", "Commuter bicycle"},
                    {"TEAM",    "Product planning"},
                    {"DATE",    "2026-10-14"},
            };

            struct Column { const char* id; MatrixImprovement direction; const char* target; };
            static const Column columns[] = {
                {"Frame mass",        MatrixImprovement::Minimize, "< 11 kg"},
                {"Tyre width",        MatrixImprovement::Target,   "35 mm"},
                {"Braking distance",  MatrixImprovement::Minimize, "< 4 m at 25 km/h"},
                {"Gear range",        MatrixImprovement::Maximize, "> 400 %"},
                {"Mudguard coverage", MatrixImprovement::Maximize, "full length"},
                {"Lighting output",   MatrixImprovement::Maximize, "> 70 lux"},
                {"Lock mounting",     MatrixImprovement::Target,   "frame lock"},
            };
            MatrixItemSet* requirements = model.FindSet("requirements");
            for (const Column& c : columns) {
                const int i = requirements->IndexOf(c.id);
                if (i < 0) continue;
                requirements->items[static_cast<size_t>(i)].improvement = c.direction;
                requirements->items[static_cast<size_t>(i)].target = c.target;
            }

            MatrixPanel& roof = model.AddPanel("requirements", "requirements",
                                               MatrixScale::Correlation());
            roof.symmetric = true;

            // Wider tyres and fuller mudguards add mass; a wider gear range
            // needs a heavier drivetrain; wider tyres shorten braking.
            struct Pair { const char* a; const char* b; const char* level; };
            static const Pair pairs[] = {
                {"Frame mass",       "Tyre width",        "negative"},
                {"Frame mass",       "Gear range",        "strongnegative"},
                {"Frame mass",       "Mudguard coverage", "negative"},
                {"Frame mass",       "Lighting output",   "negative"},
                {"Tyre width",       "Braking distance",  "strongpositive"},
                {"Tyre width",       "Mudguard coverage", "negative"},
                {"Braking distance", "Gear range",        "positive"},
                {"Gear range",       "Lock mounting",     "negative"},
                {"Lighting output",  "Lock mounting",     "positive"},
            };
            for (const Pair& pair : pairs) model.SetCell(1, pair.a, pair.b, pair.level);
            return model;
        }

    } // namespace MatrixSamples

} // namespace UltraCanvas

// Plugins/Diagrams/UltraCanvasPerformanceMatrixModel.cpp
// The UI-free model of the performance matrix and its sample models.
// Version: 1.2.0 - JSON interchange of the whole model, CSV of the population
// Version: 1.1.0 - the population: placement, counts, groups, targets, SampleTeam
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasPerformanceMatrixModel.h"
#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasCSVImport.h"
#include "UltraCanvasTextUtils.h"
#include <cctype>
#include <cmath>
#include <map>
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

    namespace {
        std::string Lower(const std::string& text) {
            std::string lower = text;
            for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return lower;
        }

        std::string TrimSpace(const std::string& text) {
            size_t first = 0, last = text.size();
            while (first < last && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
            while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
            return text.substr(first, last - first);
        }

        // A whole field as a number: "3.5" yes, "3.5 pts" or "" no. Dot-decimal
        // whatever the locale (AGENTS.md rule 2).
        bool ParseWholeNumber(const std::string& text, double& out) {
            const std::string trimmed = TrimSpace(text);
            if (trimmed.empty()) return false;
            double value = 0.0;
            const char* end = ParseFloatClassic(trimmed.data(), trimmed.data() + trimmed.size(), value);
            if (end != trimmed.data() + trimmed.size() || !std::isfinite(value)) return false;
            out = value;
            return true;
        }

        bool ParseIndex(const std::string& text, int& out) {
            const std::string trimmed = TrimSpace(text);
            if (trimmed.empty() || trimmed.size() > 4) return false;
            int value = 0;
            for (char c : trimmed) {
                if (!std::isdigit(static_cast<unsigned char>(c))) return false;
                value = value * 10 + (c - '0');
            }
            out = value;
            return true;
        }
    }

    int PerformanceMatrixModel::FindLevel(const PerformanceAxis& axis, const std::string& reference) {
        const std::string key = Lower(TrimSpace(reference));
        if (key.empty()) return -1;
        for (size_t i = 0; i < axis.levels.size(); ++i) {
            if (!axis.levels[i].id.empty() && Lower(axis.levels[i].id) == key) return static_cast<int>(i);
        }
        for (size_t i = 0; i < axis.levels.size(); ++i) {
            if (Lower(axis.levels[i].label) == key) return static_cast<int>(i);
        }
        int index = -1;
        if (ParseIndex(key, index) && index < static_cast<int>(axis.levels.size())) return index;
        return -1;
    }

// =============================================================================
// JSON (X1)
// =============================================================================

    namespace {
        const char* kPerformanceMatrixFormat = "ultracanvas-performance-matrix";
        constexpr int64_t kPerformanceMatrixFormatVersion = 1;

        // Set a member only when it says something.
        void SetText(JSONValue& object, const char* key, const std::string& text) {
            if (!text.empty()) object.Set(key, text);
        }
        void SetColor(JSONValue& object, const char* key, const Color& color) {
            if (color.a != 0) object.Set(key, JSON::FromColor(color));
        }

        JSONValue AxisToJson(const PerformanceAxis& axis) {
            JSONValue a = JSONValue::MakeObject();
            SetText(a, "title", axis.title);
            if (axis.reversed) a.Set("reversed", true);
            a.Set("min", axis.minScore);
            a.Set("max", axis.maxScore);
            JSONValue levels = JSONValue::MakeArray();
            for (const PerformanceLevel& level : axis.levels) {
                JSONValue l = JSONValue::MakeObject();
                SetText(l, "id", level.id);
                l.Set("label", level.label);
                SetText(l, "caption", level.caption);
                SetText(l, "icon", level.iconPath);
                SetColor(l, "accent", level.accent);
                if (std::isfinite(level.upperBound)) l.Set("upperBound", level.upperBound);
                levels.Append(l);
            }
            a.Set("levels", levels);
            return a;
        }

        // A row or column in a cell or item: an index, or a level id or label.
        // Returns -1 when the member is absent, -2 when it names no level.
        int ReadLevel(const JSONValue& value, const PerformanceAxis& axis) {
            if (value.IsNull()) return -1;
            if (value.IsNumber()) {
                const int64_t index = value.GetInteger(-1);
                return index >= 0 && index < static_cast<int64_t>(axis.levels.size())
                       ? static_cast<int>(index) : -2;
            }
            if (value.IsString()) {
                const int level = PerformanceMatrixModel::FindLevel(axis, value.GetString());
                return level >= 0 ? level : -2;
            }
            return -2;
        }

        bool ReadColor(const JSONValue& object, const char* key, Color& out) {
            if (!object.Contains(key)) return true;
            return JSON::ToColor(object.Get(key), out);
        }

        bool AxisFromJson(const JSONValue& a, PerformanceAxis& axis, const std::string& name, std::string& problem) {
            if (!a.IsObject()) {
                problem = "the " + name + " axis is missing";
                return false;
            }
            axis.title = a.Get("title").GetString();
            axis.reversed = a.Get("reversed").GetBoolean(false);
            axis.minScore = a.Get("min").GetNumber(0.0);
            axis.maxScore = a.Get("max").GetNumber(1.0);
            for (const JSONValue& l : a.Get("levels").GetElements()) {
                PerformanceLevel level;
                level.id = l.Get("id").GetString();
                level.label = l.Get("label").GetString();
                level.caption = l.Get("caption").GetString();
                level.iconPath = l.Get("icon").GetString();
                if (!ReadColor(l, "accent", level.accent)) {
                    problem = "the " + name + " axis level '" + level.label + "' has an unreadable accent colour";
                    return false;
                }
                if (l.Get("upperBound").IsNumber()) level.upperBound = l.Get("upperBound").GetNumber();
                axis.levels.push_back(level);
            }
            return true;
        }
    }

    JSONValue PerformanceMatrixToJson(const PerformanceMatrixModel& model) {
        JSONValue doc = JSONValue::MakeObject();
        doc.Set("format", kPerformanceMatrixFormat);
        doc.Set("version", kPerformanceMatrixFormatVersion);
        SetText(doc, "title", model.title);
        SetText(doc, "subtitle", model.subtitle);
        if (!model.highXIsBest) doc.Set("highXIsBest", false);
        if (!model.highYIsBest) doc.Set("highYIsBest", false);
        doc.Set("x", AxisToJson(model.xAxis));
        doc.Set("y", AxisToJson(model.yAxis));

        JSONValue cells = JSONValue::MakeArray();
        for (const PerformanceCell& cell : model.cells) {
            JSONValue c = JSONValue::MakeObject();
            c.Set("row", static_cast<int64_t>(cell.row));
            c.Set("column", static_cast<int64_t>(cell.col));
            SetText(c, "code", cell.code);
            if (cell.badge != 0) c.Set("badge", static_cast<int64_t>(cell.badge));
            SetText(c, "title", cell.title);
            SetText(c, "description", cell.description);
            if (!cell.bullets.empty()) {
                JSONValue bullets = JSONValue::MakeArray();
                for (const std::string& bullet : cell.bullets) bullets.Append(JSONValue(bullet));
                c.Set("bullets", bullets);
            }
            SetText(c, "action", cell.action);
            SetText(c, "icon", cell.iconPath);
            SetColor(c, "fill", cell.fill);
            SetText(c, "note", cell.note);
            cells.Append(c);
        }
        doc.Set("cells", cells);

        JSONValue items = JSONValue::MakeArray();
        for (const PerformanceMatrixItem& item : model.items) {
            JSONValue i = JSONValue::MakeObject();
            SetText(i, "id", item.id);
            SetText(i, "label", item.label);
            SetText(i, "group", item.group);
            SetColor(i, "color", item.color);
            SetText(i, "note", item.note);
            if (item.row >= 0) i.Set("row", static_cast<int64_t>(item.row));
            if (item.col >= 0) i.Set("column", static_cast<int64_t>(item.col));
            if (std::isfinite(item.xScore)) i.Set("x", item.xScore);
            if (std::isfinite(item.yScore)) i.Set("y", item.yScore);
            items.Append(i);
        }
        doc.Set("items", items);

        if (!model.targetDistribution.empty()) {
            JSONValue target = JSONValue::MakeArray();
            for (double share : model.targetDistribution) target.Append(JSONValue(share));
            doc.Set("target", target);
        }
        return doc;
    }

    std::string ExportPerformanceMatrixJson(const PerformanceMatrixModel& model, bool pretty) {
        JSONSerializeOptions options;
        options.pretty = pretty;
        options.indentWidth = 2;
        return JSON::Serialize(PerformanceMatrixToJson(model), options);
    }

    bool PerformanceMatrixFromJson(const JSONValue& json, PerformanceMatrixModel& out, std::string* error) {
        auto failWith = [error](const std::string& message) {
            if (error) *error = message;
            return false;
        };

        if (!json.IsObject()) return failWith("the document is not a JSON object");
        if (json.Get("format").GetString() != kPerformanceMatrixFormat) {
            return failWith("not a performance matrix (format is not \"" +
                            std::string(kPerformanceMatrixFormat) + "\")");
        }
        const int64_t version = json.Get("version").GetInteger(0);
        if (version < 1 || version > kPerformanceMatrixFormatVersion) {
            return failWith("format version " + std::to_string(version) + " is not one this build reads");
        }

        PerformanceMatrixModel model;
        model.title = json.Get("title").GetString();
        model.subtitle = json.Get("subtitle").GetString();
        model.highXIsBest = json.Get("highXIsBest").GetBoolean(true);
        model.highYIsBest = json.Get("highYIsBest").GetBoolean(true);
        std::string problem;
        if (!AxisFromJson(json.Get("x"), model.xAxis, "x", problem)) return failWith(problem);
        if (!AxisFromJson(json.Get("y"), model.yAxis, "y", problem)) return failWith(problem);

        const std::vector<JSONValue>& cells = json.Get("cells").GetElements();
        for (size_t n = 0; n < cells.size(); ++n) {
            const JSONValue& c = cells[n];
            PerformanceCell cell;
            cell.row = ReadLevel(c.Get("row"), model.yAxis);
            cell.col = ReadLevel(c.Get("column"), model.xAxis);
            if (cell.row < 0 || cell.col < 0) {
                return failWith("cell " + std::to_string(n) + " names a row or column the axes do not have");
            }
            cell.code = c.Get("code").GetString();
            cell.badge = static_cast<int>(c.Get("badge").GetInteger(0));
            cell.title = c.Get("title").GetString();
            cell.description = c.Get("description").GetString();
            for (const JSONValue& bullet : c.Get("bullets").GetElements()) cell.bullets.push_back(bullet.GetString());
            cell.action = c.Get("action").GetString();
            cell.iconPath = c.Get("icon").GetString();
            if (!ReadColor(c, "fill", cell.fill)) {
                return failWith("cell " + std::to_string(n) + " has an unreadable fill colour");
            }
            cell.note = c.Get("note").GetString();
            model.cells.push_back(cell);
        }

        const std::vector<JSONValue>& items = json.Get("items").GetElements();
        for (size_t n = 0; n < items.size(); ++n) {
            const JSONValue& i = items[n];
            PerformanceMatrixItem item;
            item.id = i.Get("id").GetString();
            item.label = i.Get("label").GetString();
            item.group = i.Get("group").GetString();
            item.note = i.Get("note").GetString();
            const std::string name = "item " + std::to_string(n) +
                                     (item.label.empty() ? std::string() : " ('" + item.label + "')");
            if (!ReadColor(i, "color", item.color)) return failWith(name + " has an unreadable colour");
            item.row = ReadLevel(i.Get("row"), model.yAxis);
            item.col = ReadLevel(i.Get("column"), model.xAxis);
            if (item.row == -2 || item.col == -2) {
                return failWith(name + " names a row or column the axes do not have");
            }
            if (i.Get("x").IsNumber()) item.xScore = i.Get("x").GetNumber();
            if (i.Get("y").IsNumber()) item.yScore = i.Get("y").GetNumber();
            model.items.push_back(item);
        }

        for (const JSONValue& share : json.Get("target").GetElements()) {
            model.targetDistribution.push_back(share.GetNumber(0.0));
        }

        const std::vector<std::string> problems = model.Validate();
        if (!problems.empty()) return failWith(problems.front());
        out = model;
        return true;
    }

    bool ImportPerformanceMatrixJson(const std::string& text, PerformanceMatrixModel& out, std::string* error) {
        JSONParseResult parsed;
        const JSONValue json = JSON::Parse(text, &parsed);
        if (!parsed.success) {
            if (error) {
                *error = "not valid JSON: " + parsed.errorMessage + " at line "
                         + std::to_string(parsed.errorLine) + ", column " + std::to_string(parsed.errorColumn);
            }
            return false;
        }
        return PerformanceMatrixFromJson(json, out, error);
    }

// =============================================================================
// CSV (X2)
// =============================================================================

    namespace {
        const char* const kCsvColumns[] = {"id", "label", "group", "row", "column", "x", "y", "note"};

        // How a level is written: its id, else its label when no other level
        // of the axis shares it, else its index.
        std::string LevelReference(const PerformanceAxis& axis, int index) {
            if (index < 0 || index >= static_cast<int>(axis.levels.size())) return std::string();
            const PerformanceLevel& level = axis.levels[static_cast<size_t>(index)];
            if (!level.id.empty()) return level.id;
            if (!level.label.empty() && PerformanceMatrixModel::FindLevel(axis, level.label) == index) {
                return level.label;
            }
            return std::to_string(index);
        }
    }

    std::string ExportPerformanceItemsCsv(const PerformanceMatrixModel& model) {
        CSVExportOptions options;
        std::string csv = CSVFormatRecord(std::vector<std::string>(std::begin(kCsvColumns), std::end(kCsvColumns)),
                                          options);
        for (const PerformanceMatrixItem& item : model.items) {
            const bool placed = item.row >= 0 && item.col >= 0;
            csv += CSVFormatRecord({
                item.id, item.label, item.group,
                placed ? LevelReference(model.yAxis, item.row) : std::string(),
                placed ? LevelReference(model.xAxis, item.col) : std::string(),
                std::isfinite(item.xScore) ? FormatFloatClassic(item.xScore) : std::string(),
                std::isfinite(item.yScore) ? FormatFloatClassic(item.yScore) : std::string(),
                item.note}, options);
        }
        return csv;
    }

    bool ImportPerformanceItemsCsv(const std::string& csv, PerformanceMatrixModel& model, std::string* error) {
        auto failWith = [error](const std::string& message) {
            if (error) *error = message;
            return false;
        };

        CSVImportOptions options;
        options.detectNumbers = false;
        const CSVGrid grid = CSVParse(CSVDecodeToUtf8(csv, CSVImportOptions::Encoding::UTF8), options);
        auto blank = [](const CSVRow& row) { return row.size() == 1 && TrimSpace(row[0].text).empty(); };

        size_t line = 0;
        while (line < grid.size() && blank(grid[line])) ++line;
        if (line == grid.size()) return failWith("the file is empty");

        // The header: known columns by name, in any order and letter case.
        std::map<std::string, size_t> columns;
        const CSVRow& header = grid[line];
        for (size_t c = 0; c < header.size(); ++c) {
            const std::string name = Lower(TrimSpace(header[c].text));
            if (name.empty()) continue;
            if (columns.count(name)) return failWith("the header names column '" + name + "' twice");
            columns[name] = c;
        }
        if (!columns.count("label") && !columns.count("id")) {
            return failWith("the header row has no 'label' or 'id' column");
        }
        auto field = [&](const CSVRow& row, const char* name) -> std::string {
            const auto it = columns.find(name);
            return it == columns.end() ? std::string() : TrimSpace(row[it->second].text);
        };

        std::vector<PerformanceMatrixItem> items;
        for (++line; line < grid.size(); ++line) {
            const CSVRow& row = grid[line];
            if (blank(row)) continue;
            // Line numbers count from 1 and assume one record per line, as a
            // spreadsheet shows them.
            const std::string where = "line " + std::to_string(line + 1);
            if (row.size() != header.size()) {
                return failWith(where + " has " + std::to_string(row.size()) + " fields, the header "
                                + std::to_string(header.size()));
            }
            PerformanceMatrixItem item;
            item.id = field(row, "id");
            item.label = field(row, "label");
            if (item.label.empty()) item.label = item.id;
            item.group = field(row, "group");
            item.note = field(row, "note");

            const std::string rowRef = field(row, "row");
            const std::string colRef = field(row, "column");
            if (!rowRef.empty() || !colRef.empty()) {
                item.row = PerformanceMatrixModel::FindLevel(model.yAxis, rowRef);
                item.col = PerformanceMatrixModel::FindLevel(model.xAxis, colRef);
                if (item.row < 0) return failWith(where + ": row '" + rowRef + "' is not a level of the y axis");
                if (item.col < 0) return failWith(where + ": column '" + colRef + "' is not a level of the x axis");
            }
            const std::string x = field(row, "x");
            const std::string y = field(row, "y");
            if (!x.empty() && !ParseWholeNumber(x, item.xScore)) {
                return failWith(where + ": x score '" + x + "' is not a number");
            }
            if (!y.empty() && !ParseWholeNumber(y, item.yScore)) {
                return failWith(where + ": y score '" + y + "' is not a number");
            }
            if (!model.CellOf(item).IsValid()) {
                return failWith(where + " ('" + item.label + "') has neither a row and column nor two scores");
            }
            items.push_back(item);
        }

        model.items = std::move(items);
        return true;
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

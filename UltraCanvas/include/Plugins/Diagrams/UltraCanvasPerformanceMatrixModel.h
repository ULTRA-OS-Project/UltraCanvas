// include/Plugins/Diagrams/UltraCanvasPerformanceMatrixModel.h
// The UI-free model of the performance matrix: two ordinal axes of named
// levels (performance, potential, a facet of potential...) whose crossings
// are named categories - the 9-box talent grid, the performance management
// matrix, performance-vs-potential codes, skill / will.
//
// Levels are stored low -> high on both axes, always; which end is drawn
// first is a display flag (`reversed`), so row 0 means the same thing in
// every picture. A cell is sparse: a grid with no cells is valid and draws
// empty boxes under its headers.
//
// The population (`items`) is the people - or products, or teams - being
// placed. An item sits in a cell given explicitly, or is placed by two scores
// mapped through each axis's level bounds. An explicit cell wins: it is the
// calibrated override, and the scores stay in the data beside it.
//
// Design: Docs/UltraCanvas/UltraCanvasPerformanceMatrixProposal.md
// Guide:  Docs/UltraCanvas/UltraCanvasPerformanceMatrix.md
//
// Version: 1.2.0 - interchange: the whole model as JSON
//   ("ultracanvas-performance-matrix"), the population as CSV
// Version: 1.1.0 - the population: items placed by cell or by score, counts per
//   cell, row and column, groups, a target distribution
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace UltraCanvas {

    class JSONValue;   // DataFormats/UltraCanvasJSON.h - only the JSON functions need it

    // ===== AXES =====

    struct PerformanceLevel {
        std::string id;          // "exceeds"; a stable key
        std::string label;       // "Exceeds Expectations"
        std::string caption;     // a second line under the label: "Move and Retain"
        std::string iconPath;    // drawn in the header tab (HeaderTabs design)
        Color       accent = Color(0, 0, 0, 0);   // transparent = from the palette

        // The upper end of this level on the axis's score range, for items
        // placed by score. Left at NaN on every level, the range is split
        // evenly. The last level always ends at the axis maximum.
        double      upperBound = std::numeric_limits<double>::quiet_NaN();

        PerformanceLevel() = default;
        PerformanceLevel(std::string levelLabel, std::string levelCaption = std::string(),
                         const Color& levelAccent = Color(0, 0, 0, 0))
            : label(std::move(levelLabel)), caption(std::move(levelCaption)), accent(levelAccent) {}
    };

    struct PerformanceAxis {
        std::string title;                     // "Performance", "Leadership Potential"
        std::vector<PerformanceLevel> levels;  // low -> high
        // false: the x axis runs low -> high left to right and the y axis
        // low -> high bottom to top, as on a chart. true: the other way round.
        bool reversed = false;
        // The score range items are rated on (a 1-5 review scale, 0-100 %).
        double minScore = 0.0;
        double maxScore = 1.0;
    };

    // ===== CELLS =====

    struct PerformanceCell {
        int         row = 0;           // index into yAxis.levels (0 = low)
        int         col = 0;           // index into xAxis.levels (0 = low)
        std::string code;              // "1A"
        int         badge = 0;         // a number in a circle; 0 = none
        std::string title;             // "Future Star"
        std::string description;       // wrapped; "\n" starts a new line
        std::vector<std::string> bullets;
        std::string action;            // "Develop", "Move and retain"
        std::string iconPath;          // drawn above the text
        Color       fill = Color(0, 0, 0, 0);   // PerformanceCellColoring::Explicit
        std::string note;              // tooltip only
    };

    // One cell, by row and column; -1 = none.
    struct PerformanceCellRef {
        int row = -1;
        int col = -1;

        PerformanceCellRef() = default;
        PerformanceCellRef(int r, int c) : row(r), col(c) {}
        bool IsValid() const { return row >= 0 && col >= 0; }
        bool operator==(const PerformanceCellRef& o) const { return row == o.row && col == o.col; }
        bool operator!=(const PerformanceCellRef& o) const { return !(*this == o); }
    };

    // ===== POPULATION =====

    struct PerformanceMatrixItem {
        std::string id;
        std::string label;                 // "J. Smith"
        std::string group;                 // team or department: legend and filter
        Color       color = Color(0, 0, 0, 0);   // transparent = the group's colour
        std::string note;                  // tooltip only

        // Placement: a cell...
        int row = -1;
        int col = -1;
        // ...or two scores on the axes' score ranges. A cell set explicitly
        // wins over the scores.
        double xScore = std::numeric_limits<double>::quiet_NaN();
        double yScore = std::numeric_limits<double>::quiet_NaN();

        PerformanceMatrixItem() = default;
        PerformanceMatrixItem(std::string itemLabel, std::string itemGroup, int itemRow, int itemCol)
            : label(std::move(itemLabel)), group(std::move(itemGroup)), row(itemRow), col(itemCol) {}
        PerformanceMatrixItem(std::string itemLabel, std::string itemGroup, double x, double y)
            : label(std::move(itemLabel)), group(std::move(itemGroup)), xScore(x), yScore(y) {}
    };

    // ===== MODEL =====

    struct PerformanceMatrixModel {
        static constexpr int kMaxLevels = 6;

        std::string title;
        std::string subtitle;
        PerformanceAxis xAxis;             // usually performance
        PerformanceAxis yAxis;             // usually potential
        std::vector<PerformanceCell> cells;   // at most one per (row, col)
        std::vector<PerformanceMatrixItem> items;

        // Optional: the share of the population each cell should hold,
        // row-major (row 0 first), summing to 1 - a forced distribution in a
        // calibration. Empty = no target.
        std::vector<double> targetDistribution;

        // Which corner is best: the high end of each axis by default. The
        // DiagonalBands colouring ranks cells by their distance from it.
        bool highXIsBest = true;
        bool highYIsBest = true;

        int Rows() const    { return static_cast<int>(yAxis.levels.size()); }
        int Columns() const { return static_cast<int>(xAxis.levels.size()); }
        bool Contains(int row, int col) const { return row >= 0 && col >= 0 && row < Rows() && col < Columns(); }

        const PerformanceCell* FindCell(int row, int col) const;
        // Adds the cell, or replaces the one already at its row and column.
        void SetCell(const PerformanceCell& cell);

        // 0 for the best corner, growing by one per step away from it in either
        // direction, up to MaxRank() for the worst corner.
        int Rank(int row, int col) const;
        int MaxRank() const { return std::max(0, Rows() - 1) + std::max(0, Columns() - 1); }

        // ---- the population ------------------------------------------------
        // The level a score falls in: below the first bound is level 0, past
        // the last is the last level; -1 for NaN or an axis without levels.
        static int LevelForScore(const PerformanceAxis& axis, double score);
        // Where an item sits: its explicit cell, else the cell its scores map
        // to; invalid when it has neither.
        PerformanceCellRef CellOf(const PerformanceMatrixItem& item) const;
        // How many items sit in each cell (row-major), row and column. Items
        // whose group is in `hiddenGroups` are left out, as the legend hides
        // them.
        std::vector<int> CountsPerCell(const std::vector<std::string>& hiddenGroups = {}) const;
        std::vector<int> CountsPerRow(const std::vector<std::string>& hiddenGroups = {}) const;
        std::vector<int> CountsPerColumn(const std::vector<std::string>& hiddenGroups = {}) const;
        // The distinct groups, in order of first appearance; "" when an item
        // has none.
        std::vector<std::string> Groups() const;

        // Problems that make the model unusable or misleading: an axis with
        // no levels or more than kMaxLevels, a level without a label, level
        // bounds out of order or outside the score range, a cell outside the
        // grid or two cells in one place, a negative badge, an item outside
        // the grid or without any placement, a target distribution of the
        // wrong length, with a negative share or not summing to 1.
        // Empty = valid.
        std::vector<std::string> Validate() const;

        // The level of `axis` a reference names: a level id, else a label
        // (both case-insensitive), else a 0-based index. -1 when none does.
        static int FindLevel(const PerformanceAxis& axis, const std::string& reference);
    };

    // ===== INTERCHANGE =====
    // The model functions take and return text; reading and writing the files
    // is UltraCanvasFileLoader's job (CLAUDE.md).
    //
    // JSON (X1) - the whole model: title, axes with their levels and score
    // ranges, cells, the population and the target distribution, as
    //   { "format": "ultracanvas-performance-matrix", "version": 1, ... }
    // A cell or item names its row and column by level index; the reader also
    // takes a level id or label there, for files written by hand. Empty
    // strings, default numbers and transparent colours are left out.
    JSONValue PerformanceMatrixToJson(const PerformanceMatrixModel& model);
    std::string ExportPerformanceMatrixJson(const PerformanceMatrixModel& model, bool pretty = true);
    // Reads a document written by the above. Fails - leaving `out` untouched
    // and saying why in `error` - on another format, a newer version, an
    // unreadable colour, a row or column that names no level, or a model that
    // does not Validate().
    bool PerformanceMatrixFromJson(const JSONValue& json, PerformanceMatrixModel& out,
                                   std::string* error = nullptr);
    bool ImportPerformanceMatrixJson(const std::string& text, PerformanceMatrixModel& out,
                                     std::string* error = nullptr);

    // CSV (X2) - the population only, one person per line under a header row:
    //   id,label,group,row,column,x,y,note
    // `row` and `column` are the explicit cell (a level id, label or index;
    // empty = placed by score), `x` and `y` the scores, written dot-decimal
    // whatever the locale. On import the header's columns may come in any
    // order and any letter case, and only `label` (or `id`) is required.
    // Exporting writes a level's id, else its label, else its index; an
    // item's colour is not part of the CSV.
    std::string ExportPerformanceItemsCsv(const PerformanceMatrixModel& model);
    // Replaces `model.items` with the people in `csv`, placed on the model's
    // axes. Fails - leaving the model untouched and saying why, with the line
    // number, in `error` - on a missing header, a line with more or fewer
    // fields than the header, a score that is not a number, a level the axis
    // does not have, or a person who ends up with no cell.
    bool ImportPerformanceItemsCsv(const std::string& csv, PerformanceMatrixModel& model,
                                   std::string* error = nullptr);

    // ===== PRESETS =====

    namespace PerformanceMatrixSamples {
        // The classic 9-box: performance x potential, a name and an action per
        // box, badges 1-9 counting up to the best box (high / high). Both
        // axes score 1-5, the scale SampleTeam() is rated on.
        PerformanceMatrixModel NineBox();
        // A performance management reference card: performance levels as rows,
        // three facets of potential as columns, a phrase per cell.
        PerformanceMatrixModel PerformanceManagement();
        // Performance vs leadership potential with codes 1A-3C counted from the
        // best corner and a two-line description per tile.
        PerformanceMatrixModel PerformanceVsPotential();
        // A talent review: potential levels with an action caption each,
        // badges 1-9 and three bullets per box.
        PerformanceMatrixModel TalentReview();
        // Skill / will: a 2x2 with the management style each box calls for.
        PerformanceMatrixModel SkillWill();
        // An invented team of 36 people in three departments for NineBox():
        // most placed by review scores on a 1-5 scale (set on both axes), a
        // few by a calibrated cell. Every name is made up.
        std::vector<PerformanceMatrixItem> SampleTeam();
        // A forced distribution for a 3x3 grid, as calibration guides set
        // one: 10 % in the top box, 30 % in the core box, the rest spread
        // around them.
        std::vector<double> SampleTargetDistribution();
    }

} // namespace UltraCanvas

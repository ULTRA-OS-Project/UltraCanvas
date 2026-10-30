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
// Design: Docs/UltraCanvas/UltraCanvasPerformanceMatrixProposal.md
// Guide:  Docs/UltraCanvas/UltraCanvasPerformanceMatrix.md
//
// Version: 1.0.0
// Last Modified: 2026-10-27
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <algorithm>
#include <string>
#include <vector>

namespace UltraCanvas {

    // ===== AXES =====

    struct PerformanceLevel {
        std::string id;          // "exceeds"; a stable key
        std::string label;       // "Exceeds Expectations"
        std::string caption;     // a second line under the label: "Move and Retain"
        std::string iconPath;    // drawn in the header tab (HeaderTabs design)
        Color       accent = Color(0, 0, 0, 0);   // transparent = from the palette

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

    // ===== MODEL =====

    struct PerformanceMatrixModel {
        static constexpr int kMaxLevels = 6;

        std::string title;
        std::string subtitle;
        PerformanceAxis xAxis;             // usually performance
        PerformanceAxis yAxis;             // usually potential
        std::vector<PerformanceCell> cells;   // at most one per (row, col)

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

        // Problems that make the model unusable or misleading: an axis with
        // no levels or more than kMaxLevels, a level without a label, a cell
        // outside the grid or two cells in one place, a negative badge.
        // Empty = valid.
        std::vector<std::string> Validate() const;
    };

    // ===== PRESETS =====

    namespace PerformanceMatrixSamples {
        // The classic 9-box: performance x potential, a name and an action per
        // box, badges 1-9 counting up to the best box (high / high).
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
    }

} // namespace UltraCanvas

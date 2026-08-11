// include/Plugins/Diagrams/UltraCanvasMatrixModel.h
// Matrix diagram model: item sets, panels, cell marks and the weighted roll-ups
// Version: 1.0.0
// Last Modified: 2026-08-11
// Author: UltraCanvas Framework
//
// This is the semantic model behind the matrix diagram feature. Like
// UltraCanvasFishboneModel and UltraCanvasSequenceModel it is deliberately
// UI-free - it includes nothing from the widget or render stack - so that:
//   * the roll-up arithmetic is a pure function of the model,
//   * it can be unit-tested without a window (Tests/MatrixModelTest.cpp),
//   * the renderer element (UltraCanvasMatrixDiagram) owns only geometry.
//
// A matrix diagram relates two or more ordered lists. The three primitives are
// ITEM SETS (the lists), PANELS (one set crossed with another) and CELL MARKS
// (a symbol at an intersection, drawn from a small ordinal RELATIONSHIP SCALE).
// The shape - L, T, Y, X - says only how the panels are placed around the sets;
// it never changes what the data means.
//
// Two things separate this from UltraCanvasHeatmapChartElement, which also
// draws a labelled grid: the cell value here is an ordinal level with a glyph
// and a weight rather than a continuous scalar, and the weights are meant to be
// summed (see ColumnScore / RowScore). Use the heatmap when the cell value is a
// number; use this when it is a named relationship.
//
// Room-adjacency matrices are NOT this element's data even though they look the
// part - UltraCanvasAdjacencyDiagram owns rooms and their adjacency links, and
// draws its own matrix view of them. See
// Docs/UltraCanvas/UltraCanvasMatrixDiagramProposal.md section 4.2.
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// SHAPE
// =============================================================================

    // How the panels are placed around the item sets. Every shape stores the
    // same data; only the layout differs.
    enum class MatrixShape {
        L,      // A x B                                  (2 sets, 1 panel)
        T,      // B x A and A x C, A is the shared spine (3 sets, 2 panels)
        Y,      // A x B, B x C, C x A                    (3 sets, 3 panels)
        X,      // A x B, B x C, C x D, D x A             (4 sets, 4 panels)
        House   // L + correlation roof + targets         (QFD composite)
    };
    // There is no standalone symmetric ("roof") shape: the only symmetric half
    // matrix this element draws is the QFD correlation roof, which is internal
    // to House. Room adjacency - the other symmetric case - belongs to
    // UltraCanvasAdjacencyDiagram.

    // How many item sets a shape requires. Returns 0 for an unknown shape.
    int MatrixShapeSetCount(MatrixShape shape);

    // How many panels a shape lays out.
    int MatrixShapePanelCount(MatrixShape shape);

// =============================================================================
// RELATIONSHIP SCALE
// =============================================================================

    // The mark drawn in a populated cell. Text draws MatrixLevel::glyph
    // instead of a shape, which is what lets a scale mix coloured dots with
    // letter codes under one legend.
    enum class MatrixMarkShape {
        Disc,
        Ring,        // hollow - keeps a scale readable in greyscale
        Triangle,
        Square,
        Diamond,
        Cross,
        Text
    };

    // One level of the relationship scale.
    struct MatrixLevel {
        std::string     id;                                 // "strong"
        std::string     label;                              // "Strong relationship"
        std::string     glyph;                              // drawn when shape == Text
        MatrixMarkShape shape  = MatrixMarkShape::Disc;
        Color           color  = Color(200, 40, 40, 255);
        double          weight = 1.0;                       // feeds the roll-ups
        double          sizeScale = 1.0;                    // per-level mark size multiplier

        MatrixLevel() = default;
        MatrixLevel(const std::string& levelId, const std::string& levelLabel,
                    const Color& c, double w,
                    MatrixMarkShape s = MatrixMarkShape::Disc)
                : id(levelId), label(levelLabel), shape(s), color(c), weight(w) {}
    };

    // An ordered scale, strongest level first.
    struct MatrixScale {
        std::string              title;     // legend heading, e.g. "Relationship"
        std::vector<MatrixLevel> levels;

        // Index of a level id, or -1 when absent.
        int IndexOf(const std::string& levelId) const;

        // Level by id, or nullptr when absent.
        const MatrixLevel* Find(const std::string& levelId) const;

        // Weight of a level id, or 0.0 when absent (an unknown level
        // contributes nothing rather than corrupting a total).
        double WeightOf(const std::string& levelId) const;

        // ---- Presets ----
        // Weights are data, not convention: QFD's 9/3/1 is the best known scale
        // but 5/3/1 is at least as common in practice, so both ship.
        static MatrixScale QFD();            // strong 9 / moderate 3 / weak 1
        static MatrixScale Strength531();    // strong 5 / medium 3 / weak 1
        static MatrixScale HighMediumLow();  // high 3 / medium 2 / low 1, low hollow
        static MatrixScale Presence();       // one level, weight 1
    };

// =============================================================================
// ITEM SETS
// =============================================================================

    struct MatrixItem {
        std::string id;
        std::string label;

        // Row weighting for the roll-ups. Defaults to 1.0 so that an
        // unweighted matrix gets a plain sum of cell weights out of the same
        // formula; set real importances for QFD.
        double      importance = 1.0;

        // Fully transparent = inherit the set accent.
        Color       accent = Color(0, 0, 0, 0);

        std::string note;   // shown in the tooltip

        MatrixItem() = default;
        MatrixItem(const std::string& itemId, const std::string& itemLabel,
                   double itemImportance = 1.0)
                : id(itemId), label(itemLabel), importance(itemImportance) {}
    };

    struct MatrixItemSet {
        std::string             id;      // "departments"
        std::string             title;   // axis caption
        Color                   accent = Color(90, 130, 200, 255);
        std::vector<MatrixItem> items;

        MatrixItemSet() = default;
        MatrixItemSet(const std::string& setId, const std::string& setTitle)
                : id(setId), title(setTitle) {}

        // Index of an item id, or -1 when absent.
        int IndexOf(const std::string& itemId) const;

        int Count() const { return static_cast<int>(items.size()); }
    };

// =============================================================================
// PANELS AND CELLS
// =============================================================================

    struct MatrixPanel {
        std::string rowSetId;
        std::string colSetId;
        MatrixScale scale;                  // per-panel: a T's two wings often differ
        bool        symmetric = false;      // upper triangle only (the House roof)

        MatrixPanel() = default;
        MatrixPanel(const std::string& rows, const std::string& cols,
                    const MatrixScale& s)
                : rowSetId(rows), colSetId(cols), scale(s) {}
    };

    // Cells are stored sparsely: real matrices are a small fraction populated,
    // and the API is "set the marks you have", not "fill the grid".
    struct MatrixCell {
        int         panel = 0;
        int         row   = 0;
        int         col   = 0;
        std::string levelId;
        std::string note;   // appended to the tooltip
    };

    // Identifies one cell for hit testing and selection. row or col of -1 means
    // "the whole row" / "the whole column"; panel of -1 means "nothing".
    struct MatrixRef {
        int panel = -1;
        int row   = -1;
        int col   = -1;

        bool IsValid() const { return panel >= 0; }
        bool IsCell()  const { return panel >= 0 && row >= 0 && col >= 0; }
        bool operator==(const MatrixRef& o) const {
            return panel == o.panel && row == o.row && col == o.col;
        }
        bool operator!=(const MatrixRef& o) const { return !(*this == o); }
    };

// =============================================================================
// VALIDATION
// =============================================================================

    struct MatrixValidation {
        bool                     valid = true;
        std::vector<std::string> errors;     // structural: the model cannot draw
        std::vector<std::string> warnings;   // suspicious but drawable
    };

// =============================================================================
// MODEL
// =============================================================================

    struct MatrixModel {
        std::string                title;
        std::string                subtitle;
        MatrixShape                shape = MatrixShape::L;
        std::vector<MatrixItemSet> sets;
        std::vector<MatrixPanel>   panels;
        std::vector<MatrixCell>    cells;

        // ---- Sets ----
        MatrixItemSet*       FindSet(const std::string& setId);
        const MatrixItemSet* FindSet(const std::string& setId) const;
        int                  IndexOfSet(const std::string& setId) const;

        // Convenience: append a set built from id/label pairs.
        MatrixItemSet& AddSet(const std::string& setId, const std::string& title,
                              const std::vector<std::string>& itemLabels);

        // ---- Panels ----
        MatrixPanel& AddPanel(const std::string& rowSetId,
                              const std::string& colSetId,
                              const MatrixScale& scale);

        const MatrixItemSet* RowSet(int panel) const;
        const MatrixItemSet* ColSet(int panel) const;

        // ---- Cells ----
        // Set a mark by item ids. Replaces any existing mark at that
        // intersection. Returns false when a panel index, an item id or the
        // level id is unknown - the model is never left holding a dangling ref.
        bool SetCell(int panel, const std::string& rowItemId,
                     const std::string& colItemId, const std::string& levelId);

        // Set a mark by index. Same contract.
        bool SetCellAt(int panel, int row, int col, const std::string& levelId,
                       const std::string& note = std::string());

        bool ClearCell(int panel, int row, int col);
        void ClearCells();

        const MatrixCell* FindCell(int panel, int row, int col) const;

        // Level id at a cell, or an empty string when unpopulated.
        std::string LevelAt(int panel, int row, int col) const;

        // ---- Derived ----
        // Sum over the panel's rows of importance * cellWeight. With the
        // default importance of 1.0 this is a plain sum of cell weights, which
        // is what an unweighted matrix wants.
        double ColumnScore(int panel, int col) const;

        // Sum over the panel's columns of cellWeight, scaled by this row's own
        // importance.
        double RowScore(int panel, int row) const;

        // Column indices ordered by descending ColumnScore. Ties keep their
        // natural order, so the ranking is stable.
        std::vector<int> RankColumns(int panel) const;

        // ---- Validation ----
        // Checks set count against the shape, panel set references, cell
        // indices and level ids. Cheap enough to call before every render.
        MatrixValidation Validate() const;

        void Clear();
    };

// =============================================================================
// INTERCHANGE
// =============================================================================

    // One panel as CSV: a header row of column labels, then one row per row
    // item whose first field is the row label and whose cells hold level ids.
    // Empty cells are empty fields.
    std::string ExportMatrixCsv(const MatrixModel& model, int panel = 0);

    // Parse the format ExportMatrixCsv writes back into `panel` of `model`.
    // The sets and the scale must already exist - this fills in marks, it does
    // not invent items. Returns false and leaves the model untouched when the
    // header or a row label does not match.
    bool ImportMatrixCsv(const std::string& csv, MatrixModel& model, int panel = 0);

// =============================================================================
// SAMPLES
// =============================================================================

    // Ready-made models for demos, documentation and manual testing.
    namespace MatrixSamples {

        // L-shaped: which improvement tools each department applies, on the
        // 5/3/1 strength scale, with totals on both axes.
        MatrixModel ImprovementTools();

        // T-shaped: employees down the spine, projects on the left wing and
        // skills on the right. The two wings carry different scales, which is
        // the case that puts the scale on the panel rather than the model.
        MatrixModel EmployeeAllocation();

        // L-shaped QFD: customer needs weighted by importance against
        // technical requirements, on the 9/3/1 scale. The column totals are
        // the ranking a QFD exercise exists to produce.
        MatrixModel QualityFunctionDeployment();

    } // namespace MatrixSamples

} // namespace UltraCanvas

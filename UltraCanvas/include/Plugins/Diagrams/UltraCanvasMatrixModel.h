// include/Plugins/Diagrams/UltraCanvasMatrixModel.h
// Matrix diagram model: item sets, panels, cell marks and the weighted roll-ups
// Version: 1.4.0 - competitive assessment: competitors and their per-need ratings
// Version: 1.3.0 - JSON interchange of the whole model (MatrixModelToJson / MatrixModelFromJson)
// Version: 1.2.0 - House of Quality: symmetric (roof) panels, improvement direction and target per item, Correlation scale, relative weights, table import with importances
// Version: 1.1.0 - form header fields; X and Y panel checks in Validate(); X and Y samples
// Version: 1.0.0
// Last Modified: 2026-10-14
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

    class JSONValue;   // DataFormats/UltraCanvasJSON.h - only the JSON functions need it

// =============================================================================
// SHAPE
// =============================================================================

    // How the panels are placed around the item sets. Every shape stores the
    // same data; only the layout differs.
    enum class MatrixShape {
        L,      // A x B                                  (2 sets, 1 panel)
        T,      // B x A and A x C, A is the shared spine (3 sets, 2 panels)
        Y,      // A x B, B x C, C x A                    (3 sets, 3 panels)
                // sets[0..2] run up, down-right and down-left from the centre
        X,      // A x B, B x C, C x D, D x A             (4 sets, 4 panels)
                // sets[0..3] go left, top, right, bottom; each panel pairs a
                // left/right set with a top/bottom one
        House   // L + correlation roof + targets         (QFD composite)
                // panels[0] crosses sets[0] (needs) with sets[1] (technical
                // requirements); panels[1], when present, is the symmetric
                // roof over sets[1]
    };
    // There is no standalone symmetric ("roof") shape: the only symmetric half
    // matrix this element draws is the QFD correlation roof, which is internal
    // to House. Room adjacency - the other symmetric case - belongs to
    // UltraCanvasAdjacencyDiagram.

    // How many item sets a shape requires. Returns 0 for an unknown shape.
    int MatrixShapeSetCount(MatrixShape shape);

    // How many panels a shape needs at least. A House needs only its body;
    // the roof is a second, optional panel.
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

        // The House of Quality roof: how two technical requirements interact.
        // Strong positive ++ / positive + / negative - / strong negative --,
        // drawn as text. The weights are signed (2 / 1 / -1 / -2) so a sum
        // reads as net support or conflict; the roll-ups never use them.
        static MatrixScale Correlation();
    };

// =============================================================================
// ITEM SETS
// =============================================================================

    // Which way a technical requirement should move: the direction-of-
    // improvement row of a House of Quality.
    enum class MatrixImprovement {
        NoDirection,    // named to avoid the X11 'None' macro
        Maximize,       // more is better
        Minimize,       // less is better
        Target          // hit a nominal value
    };

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

        // House of Quality columns: the direction of improvement and the
        // target value ("< 11 kg", "35 mm"). Free text: a target is read by
        // people, never parsed.
        MatrixImprovement improvement = MatrixImprovement::NoDirection;
        std::string       target;

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

        // One set against itself, upper triangle only (the House roof).
        // Cells are stored with row < col: SetCellAt swaps a lower-triangle
        // pair round and refuses the diagonal, and FindCell looks a pair up
        // either way round.
        bool        symmetric = false;

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
// FORM HEADER
// =============================================================================

    // One box of the form-style header band printed above many matrix
    // diagrams: PROJECT / AREA / TITLE / TEAM / DATE and their values.
    struct MatrixHeaderField {
        std::string label;
        std::string value;

        MatrixHeaderField() = default;
        MatrixHeaderField(const std::string& fieldLabel, const std::string& fieldValue)
                : label(fieldLabel), value(fieldValue) {}
    };

// =============================================================================
// COMPETITIVE ASSESSMENT
// =============================================================================

    // A product the House of Quality's customers compare - your own as well
    // as the competition's. Each is drawn in the competitive-assessment block
    // as one marker per need, joined row to row.
    struct MatrixCompetitor {
        std::string     id;
        std::string     name;
        Color           color = Color(60, 66, 76, 255);
        MatrixMarkShape mark  = MatrixMarkShape::Disc;

        MatrixCompetitor() = default;
        MatrixCompetitor(const std::string& competitorId, const std::string& competitorName,
                         const Color& c, MatrixMarkShape m = MatrixMarkShape::Disc)
                : id(competitorId), name(competitorName), color(c), mark(m) {}
    };

    // How customers rate one competitor on one need, from 1 to the model's
    // ratingMax (5 by convention). Keyed by ids, like the JSON cells.
    struct MatrixRating {
        std::string itemId;         // an item of the rows of panel 0 - the needs
        std::string competitorId;
        double      value = 0.0;
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
        std::vector<MatrixHeaderField> headerFields;   // empty = no header band

        // The House of Quality's competitive assessment. Empty = no block.
        std::vector<MatrixCompetitor> competitors;
        std::vector<MatrixRating>     ratings;
        double                        ratingMax = 5.0;

        // ---- Competitive assessment ----
        MatrixCompetitor& AddCompetitor(const std::string& id, const std::string& name,
                                        const Color& color,
                                        MatrixMarkShape mark = MatrixMarkShape::Disc);
        int IndexOfCompetitor(const std::string& id) const;

        // Rate a competitor on a need - an item of panel 0's row set. Replaces
        // an earlier rating. False for an unknown need or competitor, or a
        // value outside 1..ratingMax.
        bool SetRating(const std::string& itemId, const std::string& competitorId, double value);
        // The rating, or false when there is none.
        bool RatingOf(const std::string& itemId, const std::string& competitorId,
                      double& value) const;
        void ClearRatings();

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

        // Each column's share of the panel's total score, in percent - the
        // "relative weight" row of a House of Quality. All zero when the
        // panel has no score at all.
        std::vector<double> RelativeColumnWeights(int panel) const;

        // The symmetric panel over `setId`, or -1. A House's roof.
        int FindSymmetricPanel(const std::string& setId) const;

        // ---- Validation ----
        // Checks set count against the shape, panel set references, cell
        // indices and level ids, that an X or Y panel crosses two sets the
        // shape can actually place side by side, and that every rating names a
        // known need and competitor and lies in 1..ratingMax. Cheap enough to call before
        // every render.
        MatrixValidation Validate() const;

        void Clear();
    };

// =============================================================================
// INTERCHANGE
// =============================================================================

    // One panel as CSV: a header row of column labels, then one row per row
    // item whose first field is the row label and whose cells hold level ids.
    // Empty cells are empty fields. With `withImportance`, a second column
    // headed "Importance" carries each row's importance, written dot-decimal.
    std::string ExportMatrixCsv(const MatrixModel& model, int panel = 0,
                                bool withImportance = false);

    // Parse the format ExportMatrixCsv writes back into `panel` of `model`.
    // The sets and the scale must already exist - this fills in marks, it does
    // not invent items. An "Importance" second column, when present, sets the
    // row importances too. Returns false and leaves the model untouched when
    // the header or a row label does not match.
    bool ImportMatrixCsv(const std::string& csv, MatrixModel& model, int panel = 0);

    // Build a new L-shaped model from the tabular form a spreadsheet holds: a
    // header row of column labels, a first column of row labels, and a mark
    // in each cell. Unlike ImportMatrixCsv this invents the items - nothing
    // need exist beforehand. A cell may name its level by id, by label (any
    // case), by glyph, or by weight ("9"), so a QFD sheet typed with numbers
    // reads as it is. An optional second column headed "Importance" or
    // "Weight" sets the row importances. Numbers are dot-decimal whatever the
    // locale. On any unknown mark, unparsable importance or ragged row, it
    // returns false, puts the reason in `error` when given, and leaves `out`
    // untouched.
    bool ImportMatrixTable(const std::string& csv, const MatrixScale& scale, MatrixModel& out,
                           const std::string& rowTitle = "Rows",
                           const std::string& colTitle = "Columns",
                           std::string* error = nullptr);

    // The whole model as JSON - every set, panel, scale, cell and header field,
    // where the CSV forms carry one panel's marks. Cells are keyed by item id,
    // not index, so a file survives its items being reordered:
    //
    //   { "format": "ultracanvas-matrix", "version": 1, "title": ..., "shape": "House",
    //     "header": [ {"label", "value"} ],
    //     "sets":   [ {"id", "title", "accent": [r,g,b,a], "items": [ {"id", "label",
    //                 "importance", "note", "improvement", "target", "accent"} ]} ],
    //     "panels": [ {"rows", "columns", "symmetric", "scale": {"title", "levels":
    //                 [ {"id", "label", "glyph", "shape", "color", "weight", "size"} ]}} ],
    //     "cells":  [ {"panel", "row", "column", "level", "note"} ] }
    //
    // Members that hold their default are left out.
    JSONValue MatrixModelToJson(const MatrixModel& model);
    std::string ExportMatrixJson(const MatrixModel& model, bool pretty = true);

    // Read that form back. The result must pass Validate(): on an unknown
    // format or a newer version, a cell naming an item or level that does not
    // exist, or a model the shape cannot draw, it returns false, puts the
    // reason in `error` when given, and leaves `out` untouched.
    bool MatrixModelFromJson(const JSONValue& json, MatrixModel& out,
                             std::string* error = nullptr);
    bool ImportMatrixJson(const std::string& text, MatrixModel& out,
                          std::string* error = nullptr);

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

        // X-shaped: suppliers, storage areas, customers and production lines
        // pinwheeled round a hub, with the form header band filled in.
        MatrixModel SupplyChain();

        // Y-shaped: customer requirements, product functions and components,
        // each related to the other two - a design traceability loop.
        MatrixModel DesignTraceability();

        // House of Quality: the QualityFunctionDeployment() matrix with its
        // correlation roof, directions of improvement and target values.
        MatrixModel HouseOfQuality();

    } // namespace MatrixSamples

} // namespace UltraCanvas

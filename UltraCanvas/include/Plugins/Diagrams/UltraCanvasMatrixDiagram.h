// include/Plugins/Diagrams/UltraCanvasMatrixDiagram.h
// Matrix diagram element: item sets crossed in L, T, X, Y and House arrangements, cell
// marks drawn from an ordinal relationship scale, and weighted roll-up gutters.
// Version: 1.4.0 - the House of Quality's competitive-assessment block
// Version: 1.3.0 - Phase 4: pan and zoom through the shared diagram viewport, keyboard navigation, cycle-on-click editing
// Version: 1.2.0 - Phase 3: the House of Quality (roof, importance column, direction and target rows), roll-up bar strip with ranks
// Version: 1.1.1 - rotated column headers get room on the right instead of being clipped; SetDarkTheme(false) keeps non-colour settings
// Version: 1.1.0 - Phase 2: the X shape (blank or captioned hub), the Y shape, the form header band, cell tint by level, hover bands across panels
// Version: 1.0.1 - InvalidateLayout() overrides the layout engine's and calls it; the cache alone is DropLayoutCache()
// Version: 1.0.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework
//
// The matrix diagram is one of the Seven Management and Planning Tools: it
// makes the relationships between two or more lists visible and countable.
// Rows and columns are things; each intersection carries a symbol from a small
// named scale ("strong", "medium", "weak"), and the weights behind those
// symbols roll up into per-row and per-column totals.
//
// The semantic model lives in UltraCanvasMatrixModel.h, which is UI-free and
// unit-tested. This element owns only geometry, styling and interaction.
//
// WHICH ELEMENT DO I WANT?
//   * Continuous numeric cell values, a colour map, a colour bar
//       -> UltraCanvasHeatmapChartElement
//   * Items positioned at continuous (x, y) in a 2x2 space (BCG, Eisenhower)
//       -> UltraCanvasQuadrantChart
//   * Rooms and their required adjacencies
//       -> UltraCanvasAdjacencyDiagram (it draws its own matrix view)
//   * Named relationship levels between two or three ordered lists, with
//     totals -> this element.
//
// Implemented: the L, T, X, Y and House (QFD) shapes; see
// Docs/UltraCanvas/UltraCanvasMatrixDiagramProposal.md for what remains.
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include "Plugins/Charts/UltraCanvasChartLegend.h"
#include "Plugins/Diagrams/UltraCanvasMatrixModel.h"
#include "Plugins/Diagrams/UltraCanvasDiagramViewport.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// ENUMERATIONS
// =============================================================================

    // How column headers are fitted into the header band. Auto measures the
    // labels once and picks the cheapest form that fits - horizontal if the
    // labels are short enough, then wrapped, then rotated. It never iterates.
    enum class MatrixHeaderFit {
        Auto,
        Horizontal,
        Wrapped,
        Rotated45,
        Rotated90
    };

    // Which roll-up gutters are drawn. Row totals sit to the right of the
    // grid, column totals underneath it.
    enum class MatrixTotals {
        NoTotals,       // named to avoid the X11 'None' macro
        Rows,
        Columns,
        Both
    };

    // How the column totals are presented: as numbers in a gutter, or as a
    // strip of bars with each column's rank - the QFD presentation.
    enum class MatrixRollUp {
        Numbers,
        Bars
    };

    // What sits in the middle of an X-shaped diagram: nothing, or the four set
    // titles in boxes with arrows out to the bands that list their items.
    enum class MatrixHubMode {
        Blank,
        Captions
    };

// =============================================================================
// STYLE
// =============================================================================

    struct MatrixDiagramStyle {
        // ---- Grid ----
        // Cells shrink to fit the widget down to minCellSize; past that the
        // grid overflows and is clipped to the content area rather than
        // shrinking into illegibility.
        double  minCellSize         = 16.0;
        double  maxCellSize         = 64.0;
        float   gridLineWidth       = 1.0f;
        Color   gridLineColor       = Color(206, 212, 220, 255);
        Color   cellBackground      = Color(255, 255, 255, 255);
        Color   cellAlternateBackground = Color(248, 249, 251, 255);
        bool    alternateRowBands   = false;

        // ---- Marks ----
        double  markSize            = 0.44;     // fraction of the cell's short side
        float   markStrokeWidth     = 2.0f;     // for Ring and Cross
        Color   markHighlightRing   = Color(255, 170, 0, 255);

        // ---- Labels ----
        float   rowLabelFontSize    = 11.0f;
        float   colLabelFontSize    = 10.5f;
        float   axisTitleFontSize   = 11.0f;
        Color   labelColor          = Color(45, 50, 58, 255);
        Color   axisTitleColor      = Color(90, 96, 106, 255);
        double  labelPadding        = 8.0;
        double  maxRowLabelWidth    = 190.0;    // wider labels are ellipsised
        double  maxHeaderBandHeight = 160.0;    // longer labels are ellipsised

        // Row and column label bands can carry a solid fill, as the printed
        // examples of this diagram usually do.
        bool    fillLabelBands      = true;
        Color   labelBandColor      = Color(60, 122, 130, 255);
        Color   labelBandTextColor  = Color(255, 255, 255, 255);

        // ---- Totals gutters ----
        float   totalsFontSize      = 11.0f;
        Color   totalsBackground    = Color(232, 236, 241, 255);
        Color   totalsTextColor     = Color(55, 60, 70, 255);
        double  totalsGutterSize    = 46.0;
        int     totalsDecimals      = 0;

        // ---- Highlight ----
        Color   hoverBandColor      = Color(70, 130, 200, 40);
        Color   selectedCellColor   = Color(70, 130, 200, 70);

        // ---- Titles ----
        float   titleFontSize       = 17.0f;
        float   subtitleFontSize    = 11.5f;
        Color   titleColor          = Color(28, 32, 38, 255);
        Color   subtitleColor       = Color(96, 102, 112, 255);

        // ---- T shape ----
        double  panelGap            = 26.0;     // between a T's two wings

        // ---- X hub ----
        Color   hubBackground       = Color(241, 243, 246, 255);
        float   hubCaptionFontSize  = 11.0f;
        Color   hubCaptionTextColor = Color(255, 255, 255, 255);
        Color   hubArrowColor       = Color(90, 96, 106, 255);

        // ---- Form header band ----
        // Drawn above the grid when the model carries header fields
        // (PROJECT / AREA / TEAM / DATE ...).
        float   headerFieldLabelFontSize = 8.5f;
        float   headerFieldValueFontSize = 11.5f;
        Color   headerFieldLabelColor    = Color(110, 116, 126, 255);
        Color   headerFieldValueColor    = Color(30, 34, 40, 255);
        Color   headerBandBackground     = Color(255, 255, 255, 255);
        Color   headerBandBorderColor    = Color(190, 196, 205, 255);

        // ---- Roll-up bar strip ----
        double  barStripHeight      = 84.0;     // replaces totalsGutterSize for column bars
        Color   rollUpBarColor      = Color(70, 122, 190, 255);
        Color   rollUpTopBarColor   = Color(214, 110, 40, 255);   // the top three ranks
        Color   rankTextColor       = Color(96, 102, 112, 255);

        // ---- House of Quality ----
        double  importanceColumnWidth = 44.0;
        double  improvementRowHeight  = 22.0;
        double  targetRowHeight       = 32.0;   // two lines of target text
        float   targetFontSize        = 9.0f;
        double  maxRoofShare          = 0.38;   // of the content height, at most
        double  minRowAspect          = 0.6;    // rows at least this fraction of the cell width
        Color   improvementColor      = Color(60, 66, 76, 255);

        // ---- Competitive assessment (House) ----
        double  competitiveWidth      = 150.0;  // the block right of the grid
        double  ratingMarkerRadius    = 4.5;
        float   competitiveLineWidth  = 1.6f;
        Color   competitiveBackground = Color(250, 251, 252, 255);
        Color   competitiveTickColor  = Color(218, 222, 228, 255);

        // ---- Cell tint ----
        // Fill each populated cell with its level's colour at this alpha,
        // under the mark - a heatmap look for readers who want one. Off by
        // default: the mark alone is the datum.
        bool    tintCellsByLevel    = false;
        uint8_t cellTintAlpha       = 56;

        // Apply the dark or the light palette in place. Only colours change:
        // sizes, switches and fonts are left as the caller set them. Kept as
        // methods rather than second structs so callers can tweak a field
        // afterwards.
        void ApplyDarkTheme();
        void ApplyLightTheme();
    };

// =============================================================================
// MATRIX DIAGRAM ELEMENT
// =============================================================================

    class UltraCanvasMatrixDiagram : public UltraCanvasChartElementBase {
    public:
        UltraCanvasMatrixDiagram(const std::string& id, int x, int y, int width, int height);

        // ===== MODEL =====
        void SetModel(const MatrixModel& m);
        const MatrixModel& GetModel() const { return model; }

        // Mutating access. Every edit through this handle must be followed by
        // InvalidateLayout(), so prefer the convenience mutators below.
        MatrixModel& MutableModel() { return model; }

        // ---- Convenience mutators (invalidate the layout for you) ----
        MatrixItemSet& AddSet(const std::string& setId, const std::string& title,
                              const std::vector<std::string>& itemLabels);
        MatrixPanel&   AddPanel(const std::string& rowSetId, const std::string& colSetId,
                                const MatrixScale& scale);
        bool SetCell(int panel, const std::string& rowItemId,
                     const std::string& colItemId, const std::string& levelId);
        bool SetCellAt(int panel, int row, int col, const std::string& levelId,
                       const std::string& note = std::string());
        bool ClearCell(int panel, int row, int col);
        void SetShape(MatrixShape shape);
        void SetSubtitle(const std::string& text);
        void Clear();

        // ===== PRESENTATION =====
        void SetStyle(const MatrixDiagramStyle& s);
        const MatrixDiagramStyle& GetStyle() const { return style; }

        void SetDarkTheme(bool dark);
        bool GetDarkTheme() const { return darkTheme; }

        void SetHeaderFit(MatrixHeaderFit fit);
        MatrixHeaderFit GetHeaderFit() const { return headerFit; }

        void SetTotals(MatrixTotals which);
        MatrixTotals GetTotals() const { return totals; }

        // Column totals as numbers, or as bars ranked by score. Row totals
        // stay numbers either way.
        void SetRollUp(MatrixRollUp presentation);
        MatrixRollUp GetRollUp() const { return rollUp; }

        // House only: the column of row importances between the row labels
        // and the grid. On by default.
        void SetShowImportance(bool on);
        bool GetShowImportance() const { return showImportance; }

        // House only: the competitive-assessment block right of the grid -
        // each competitor's rating of every need, 1..ratingMax, as markers
        // joined row to row. On by default; it draws nothing while the model
        // has no competitors. A competitor's legend entry highlights it.
        void SetShowCompetitiveAssessment(bool on);
        bool GetShowCompetitiveAssessment() const { return showCompetitive; }

        void SetShowAxisTitles(bool on);

        // X shape only: a blank hub, or the four set titles in captioned
        // boxes. With captions the outer axis titles are left out - the hub
        // already names every band.
        void SetHubMode(MatrixHubMode mode);
        MatrixHubMode GetHubMode() const { return hubMode; }

        // The form header band of MatrixModel::headerFields. On by default; it
        // draws nothing while the model has no fields.
        void SetShowHeaderBand(bool on);
        bool GetShowHeaderBand() const { return showHeaderBand; }
        void SetShowLegend(bool on);
        void SetLegendPosition(ChartLegendPosition position);

        // Draw each cell's weight as text under the mark. Off by default: it
        // is useful when checking a total, noisy the rest of the time.
        void SetShowCellValues(bool on);

        // ===== INTERACTION =====
        MatrixRef GetHoveredCell() const { return hovered; }
        MatrixRef GetSelectedCell() const { return selected; }
        void ClearSelection();

        // Dim every mark whose level is not `levelId`; an empty string clears
        // the filter. Clicking a legend entry drives this.
        void SetLevelFilter(const std::string& levelId);
        const std::string& GetLevelFilter() const { return levelFilter; }

        // Select a cell (or clear the selection with an invalid ref), as a
        // click or the keyboard would. Fires onSelectionChanged.
        void SelectCell(const MatrixRef& ref);

        // ===== PAN AND ZOOM =====
        // Off by default, so a diagram that fits its element behaves exactly
        // as before. When on: the wheel zooms about the pointer, dragging
        // empty space (or with the middle button) pans, Ctrl + / Ctrl - zoom
        // and Ctrl 0 resets. Titles, the form header and the legend stay put;
        // only the matrix moves. The minimap and the zoom-button overlay are
        // the shared diagram viewport's - turn them on through Viewport().
        void SetPanZoomEnabled(bool on);
        bool GetPanZoomEnabled() const { return panZoomEnabled; }
        void ZoomViewIn();
        void ZoomViewOut();
        void ResetView();               // zoom 1, no pan: the laid-out diagram
        double GetViewZoom() const { return viewport.GetZoomLevel(); }
        UltraCanvasDiagramViewport& Viewport() { return viewport; }

        // ===== EDITING =====
        // Off by default. When on, a click on a cell - or Space / Enter on the
        // selected one - steps it to the next level of its panel's scale,
        // strongest first, then back to empty; Shift steps the other way, and
        // Delete clears it. Anything richer than cycling (a level picker)
        // belongs in real elements beside the diagram, not painted into it.
        void SetEditable(bool on);
        bool GetEditable() const { return editable; }

        // Step one cell's level as a click would. Returns false for a ref
        // that is not a cell. Fires onCellEdited.
        bool CycleCell(const MatrixRef& ref, bool backwards = false);

        // ===== CALLBACKS =====
        // Fired for a populated cell and for an empty one alike; check
        // GetModel().LevelAt(...) to tell them apart.
        std::function<void(const MatrixRef&)> onCellClick;
        std::function<void(const MatrixRef&)> onCellHover;
        std::function<void(int panel, int index, const MatrixItem&)> onRowClick;
        std::function<void(int panel, int index, const MatrixItem&)> onColumnClick;
        // The new level id, or an empty string when the cell was cleared.
        std::function<void(const MatrixRef&, const std::string& levelId)> onCellEdited;
        std::function<void(const MatrixRef&)> onSelectionChanged;

        // ===== OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

        // Keyboard navigation needs the focus; the framework only hands it
        // to elements that ask.
        bool AcceptsFocus() const override { return true; }

    private:
        // ===== DATA =====
        MatrixModel        model;
        MatrixDiagramStyle style;

        // ===== OPTIONS =====
        bool            darkTheme       = false;
        MatrixHeaderFit headerFit       = MatrixHeaderFit::Auto;
        MatrixTotals    totals          = MatrixTotals::NoTotals;
        MatrixRollUp    rollUp          = MatrixRollUp::Numbers;
        bool            showImportance  = true;
        bool            showCompetitive = true;
        std::string     competitorFilter;   // a highlighted competitor's id, or empty
        bool            showAxisTitles  = true;
        bool            showLegend      = true;
        bool            showCellValues  = false;
        bool            showHeaderBand  = true;
        MatrixHubMode   hubMode         = MatrixHubMode::Blank;
        std::string     levelFilter;

        std::unique_ptr<ChartLegend> legend;

        // ===== INTERACTION STATE =====
        MatrixRef hovered;
        MatrixRef selected;
        bool      editable = false;

        // ===== VIEWPORT =====
        UltraCanvasDiagramViewport viewport;
        bool      panZoomEnabled = false;
        bool      panDragging = false;
        bool      minimapDragging = false;
        Point2Di  lastDragPoint;
        int       hoveredControlButton = -1;

        // ===== LAYOUT CACHE (element-local coordinates) =====
        // One entry per model panel. A T shape produces two, an X four, a Y
        // three.
        //
        // A panel is a lattice: cell (row, col) of the MODEL's row and column
        // sets has its corner at origin + row * rowStep + col * colStep. An L
        // or T steps down the rows and across the columns; an X panel whose
        // model rows lie along a horizontal arm steps the other way round
        // (transposed); a Y panel steps along two seams 120 degrees apart, so
        // its cells are rhombi. Everything below - grid, marks, highlight,
        // hit testing - goes through the lattice, so no shape needs its own
        // copy of them.
        struct PanelLayout {
            int     panelIndex   = 0;
            Rect2Dd grid;                       // bounding box of the cell area
            Point2Dd origin;
            Point2Dd rowStep;
            Point2Dd colStep;
            bool    axisAligned  = true;
            bool    upperTriangle = false;              // a symmetric roof: cells with col > row only
            Rect2Dd rowLabels;                  // band left of the grid
            Rect2Dd colLabels;                  // band above the grid
            Rect2Dd rowTotals;                  // gutter right of the grid
            Rect2Dd colTotals;                  // gutter below the grid
            double  cellWidth    = 0.0;
            double  cellHeight   = 0.0;
            int     rows         = 0;
            int     cols         = 0;
            MatrixHeaderFit fit  = MatrixHeaderFit::Horizontal;
            int     headerLines  = 1;           // for MatrixHeaderFit::Wrapped
            bool    rowLabelsOnRight = false;   // the left wing of a T

            // House of Quality bands, empty elsewhere.
            Rect2Dd importance;                 // row importances, left of the grid
            Rect2Dd improvementRow;             // direction arrows, above the grid
            Rect2Dd targetRow;                  // target values, below the grid
            Rect2Dd competitive;                // the competitive assessment, right of it
            bool    colTitleInCorner = false;   // the roof takes the space above the header
        };

        // A band listing one item set, used by the X and Y shapes, where the
        // labels of a set serve two panels and so belong to neither. Item i
        // occupies origin + dir * [i, i+1] * itemSize, across * [0, bandWidth].
        struct ArmLayout {
            int      setIndex   = -1;
            Point2Dd origin;
            Point2Dd dir;                       // unit: the way the items run
            Point2Dd across;                    // unit: across the band
            double   itemSize   = 0.0;
            double   bandWidth  = 0.0;
            int      count      = 0;
            bool     itemsAreColumns = false;   // X's left/right arms
            MatrixHeaderFit fit = MatrixHeaderFit::Horizontal;
            int      headerLines = 1;
            Point2Dd titleAnchor;               // centre of the set title
            bool     titleVertical = false;
        };

        // A caption box in an X hub, with its arrow out to the band it names.
        struct HubCaption {
            int      setIndex = -1;
            Rect2Dd  box;
            Point2Dd arrowFrom;
            Point2Dd arrowTo;
        };

        struct HeaderField {
            Rect2Dd box;
            int     field = 0;
        };

        struct Layout {
            bool                     valid = false;
            Rect2Dd                  headerBand;  // the form header, empty when absent
            std::vector<HeaderField> headerFields;
            Rect2Dd                  legendArea;  // after the title, BEFORE the legend's bite
            Rect2Dd                  content;     // what the panels get, after both
            std::vector<PanelLayout> panels;
            std::vector<ArmLayout>   arms;        // X and Y only
            Rect2Dd                  hub;         // X only
            std::vector<HubCaption>  hubCaptions;
        };
        Layout layout;

        // ===== LAYOUT =====
    public:
        // The framework's invalidation - a new size, visibility, style - drops
        // the chart's own layout cache and lets the layout engine re-measure.
        // A cache-only InvalidateLayout() overrode the engine's and kept the
        // change from reaching the parent.
        void InvalidateLayout() override { DropLayoutCache(); UltraCanvasChartElementBase::InvalidateLayout(); }
    private:
        void DropLayoutCache();
        void UpdateLayout(IRenderContext* ctx);
        void LayoutL(IRenderContext* ctx, const Rect2Dd& area);
        void LayoutT(IRenderContext* ctx, const Rect2Dd& area);
        bool LayoutX(IRenderContext* ctx, const Rect2Dd& area);
        bool LayoutY(IRenderContext* ctx, const Rect2Dd& area);
        bool LayoutHouse(IRenderContext* ctx, const Rect2Dd& area);
        double ColumnGutterHeight() const;
        void LayoutHeaderBand(IRenderContext* ctx, Rect2Dd& area);
        void LayoutHubCaptions(IRenderContext* ctx, double& minWidth, double& minHeight);
        void PlaceHubCaptions();

        // Fills in an axis-aligned panel's lattice from its grid rectangle.
        static void SetAxisLattice(PanelLayout& pl, bool transposed);

        // Measures one panel's label bands and fills in everything but the
        // grid origin, which the caller places.
        PanelLayout MeasurePanel(IRenderContext* ctx, int panelIndex,
                                 bool rowLabelsOnRight) const;

        // Fits `count` cells of `desired` size into `available`, clamped to the
        // style's min/max.
        double FitCellSize(double available, int count) const;

        MatrixHeaderFit ResolveHeaderFit(IRenderContext* ctx, const MatrixItemSet* cols,
                                         double cellWidth, double bandAllowance) const;

        // Height the header band needs to show every column label in full,
        // given a settled fit and cell width. Clamped to the style maximum.
        double MeasureHeaderBand(IRenderContext* ctx, const MatrixItemSet* cols,
                                 MatrixHeaderFit fit, double cellWidth) const;

        // How far each column label of `cols` reaches right of its column's
        // centre when drawn at `fit` in a band of `bandHeight`. Empty unless
        // the fit is rotated: only a rotated label leans past its column.
        std::vector<double> RotatedHeaderReach(IRenderContext* ctx, const MatrixItemSet* cols,
                                               MatrixHeaderFit fit, double bandHeight) const;

        double MeasureWidestLabel(IRenderContext* ctx, const MatrixItemSet* set,
                                  float fontSize) const;

        // ===== RENDERING =====
        void RenderTitles(IRenderContext* ctx);
        void RenderPanel(IRenderContext* ctx, const PanelLayout& pl);
        void RenderGrid(IRenderContext* ctx, const PanelLayout& pl);
        void RenderRowLabels(IRenderContext* ctx, const PanelLayout& pl);
        void RenderColumnLabels(IRenderContext* ctx, const PanelLayout& pl);
        void RenderAxisTitles(IRenderContext* ctx, const PanelLayout& pl);
        void RenderMarks(IRenderContext* ctx, const PanelLayout& pl);
        void RenderTotals(IRenderContext* ctx, const PanelLayout& pl);
        void RenderHighlight(IRenderContext* ctx, const PanelLayout& pl);
        void RenderArm(IRenderContext* ctx, const ArmLayout& arm);
        void RenderArmTitle(IRenderContext* ctx, const ArmLayout& arm);
        void RenderHub(IRenderContext* ctx);
        void RenderHeaderBand(IRenderContext* ctx);
        void RenderHouseBands(IRenderContext* ctx, const PanelLayout& pl);
        void RenderColumnBars(IRenderContext* ctx, const PanelLayout& pl);
        void RenderCompetitive(IRenderContext* ctx, const PanelLayout& pl);
        bool CompetitiveShown() const;

        // One column label inside a header band of the given fit, centred on
        // `centerX` and sitting on `bandBottom`.
        void DrawColumnLabel(IRenderContext* ctx, const std::string& label,
                             double centerX, double bandBottom, double bandHeight,
                             MatrixHeaderFit fit, double cellWidth, int lines);

        void DrawMark(IRenderContext* ctx, const MatrixLevel& level,
                      const Rect2Dd& cell, bool dimmed) const;

        // ===== LEGEND =====
        void RebuildLegend();

        // ===== HIT TESTING =====
        // Cell under a point, or an invalid ref. Also resolves the label bands,
        // returning row-only or column-only refs.
        // `point` is element-local; the viewport maps it into the layout's
        // coordinates first, and nothing outside the content area is a hit.
        MatrixRef HitTest(const Point2Di& point) const;
        MatrixRef HitTestLayout(double px, double py) const;
        const PanelLayout* PanelAt(int panelIndex) const;

        // ===== HELPERS =====
        // Corners of the block of cells [row, row + rows) x [col, col + cols).
        std::vector<Point2Dd> CellQuad(const PanelLayout& pl, int row, int col,
                                       int rows = 1, int cols = 1) const;
        void FillCells(IRenderContext* ctx, const PanelLayout& pl, int row, int col,
                       int rows, int cols) const;
        // The box a mark is drawn in: the cell itself when axis-aligned, the
        // square inscribed in the rhombus otherwise.
        Rect2Dd MarkBox(const PanelLayout& pl, int row, int col) const;
        MatrixRef ArmRef(int setIndex, int item) const;

        // ===== KEYBOARD =====
        bool HandleKeyDown(const UCEvent& event);
        // Moves the selection to the neighbouring cell lying furthest in the
        // screen direction (dx, dy) - whatever way the panel's lattice runs.
        bool MoveSelection(double dx, double dy);
        bool SelectPanel(int step);
        bool CellExists(const PanelLayout& pl, int row, int col) const;
        void KeepSelectionVisible();
        std::string FormatTotal(double value) const;
        std::string EllipsizeToWidth(IRenderContext* ctx, const std::string& text,
                                     double maxWidth) const;
        std::vector<std::string> WrapToLines(IRenderContext* ctx, const std::string& text,
                                             double maxWidth, int maxLines) const;
        std::string BuildTooltip(const MatrixRef& ref) const;
        Color EffectiveLabelColor() const;
        bool  IsDimmed(const std::string& levelId) const;
    };

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    std::shared_ptr<UltraCanvasMatrixDiagram> CreateMatrixDiagramElement(
            const std::string& id, int x, int y, int width, int height);

    std::shared_ptr<UltraCanvasMatrixDiagram> CreateMatrixDiagramElement(
            const std::string& id, int x, int y, int width, int height,
            const MatrixModel& model);

} // namespace UltraCanvas

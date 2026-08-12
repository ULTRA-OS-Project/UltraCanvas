// include/Plugins/Diagrams/UltraCanvasMatrixDiagram.h
// Matrix diagram element: item sets crossed in L and T arrangements, cell marks
// drawn from an ordinal relationship scale, and weighted roll-up gutters.
// Version: 1.0.0
// Last Modified: 2026-08-11
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
// Phase 1 implements the L and T shapes. Y, X and the QFD House composite are
// planned; see Docs/UltraCanvas/UltraCanvasMatrixDiagramProposal.md.
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include "Plugins/Charts/UltraCanvasChartLegend.h"
#include "Plugins/Diagrams/UltraCanvasMatrixModel.h"
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

        // Applies the dark palette in place. Kept as a method rather than a
        // second struct so callers can tweak a field afterwards.
        void ApplyDarkTheme();
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

        void SetShowAxisTitles(bool on);
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

        // ===== CALLBACKS =====
        // Fired for a populated cell and for an empty one alike; check
        // GetModel().LevelAt(...) to tell them apart.
        std::function<void(const MatrixRef&)> onCellClick;
        std::function<void(const MatrixRef&)> onCellHover;
        std::function<void(int panel, int index, const MatrixItem&)> onRowClick;
        std::function<void(int panel, int index, const MatrixItem&)> onColumnClick;

        // ===== OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        // ===== DATA =====
        MatrixModel        model;
        MatrixDiagramStyle style;

        // ===== OPTIONS =====
        bool            darkTheme       = false;
        MatrixHeaderFit headerFit       = MatrixHeaderFit::Auto;
        MatrixTotals    totals          = MatrixTotals::NoTotals;
        bool            showAxisTitles  = true;
        bool            showLegend      = true;
        bool            showCellValues  = false;
        std::string     levelFilter;

        std::unique_ptr<ChartLegend> legend;

        // ===== INTERACTION STATE =====
        MatrixRef hovered;
        MatrixRef selected;

        // ===== LAYOUT CACHE (element-local coordinates) =====
        // One entry per model panel. A T shape produces two.
        struct PanelLayout {
            int     panelIndex   = 0;
            Rect2Dd grid;                       // the cell area only
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
        };

        struct Layout {
            bool                     valid = false;
            Rect2Dd                  legendArea;  // after the title, BEFORE the legend's bite
            Rect2Dd                  content;     // what the panels get, after both
            std::vector<PanelLayout> panels;
        };
        Layout layout;

        // ===== LAYOUT =====
        void InvalidateLayout();
        void UpdateLayout(IRenderContext* ctx);
        void LayoutL(IRenderContext* ctx, const Rect2Dd& area);
        void LayoutT(IRenderContext* ctx, const Rect2Dd& area);

        // Measures one panel's label bands and fills in everything but the
        // grid origin, which the caller places.
        PanelLayout MeasurePanel(IRenderContext* ctx, int panelIndex,
                                 bool rowLabelsOnRight) const;

        // Fits `count` cells of `desired` size into `available`, clamped to the
        // style's min/max.
        double FitCellSize(double available, int count) const;

        MatrixHeaderFit ResolveHeaderFit(IRenderContext* ctx, int panelIndex,
                                         double cellWidth, double bandAllowance) const;

        // Height the header band needs to show every column label in full,
        // given a settled fit and cell width. Clamped to the style maximum.
        double MeasureHeaderBand(IRenderContext* ctx, int panelIndex,
                                 MatrixHeaderFit fit, double cellWidth) const;

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

        void DrawMark(IRenderContext* ctx, const MatrixLevel& level,
                      const Rect2Dd& cell, bool dimmed) const;

        // ===== LEGEND =====
        void RebuildLegend();

        // ===== HIT TESTING =====
        // Cell under a point, or an invalid ref. Also resolves the label bands,
        // returning row-only or column-only refs.
        MatrixRef HitTest(const Point2Di& point) const;
        const PanelLayout* PanelAt(int panelIndex) const;

        // ===== HELPERS =====
        Rect2Dd CellRect(const PanelLayout& pl, int row, int col) const;
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

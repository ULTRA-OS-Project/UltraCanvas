// include/Plugins/Diagrams/UltraCanvasPerformanceMatrix.h
// The performance matrix: a grid of named categories where two rated axes
// cross - the 9-box talent grid, the performance management matrix,
// performance-vs-potential codes, skill / will. The data is a
// PerformanceMatrixModel (UltraCanvasPerformanceMatrixModel.h); this element
// draws it in one of four designs:
//
//   Grid        contiguous cells, row and column labels, axis arrows and titles
//   Tiles       separated rounded tiles, a large code over a description
//   HeaderTabs  coloured header tabs with icons above and left of plain cells
//   Cards       white cards with a coloured border, a number badge and bullets
//
// The palette is separate from the design, so one design reads in traffic
// light colours, in one hue or in pastels. All cells share one font size: the
// largest at which every cell's text fits, down to a floor; below it the
// least important content (extra bullets, then the description, then the
// action) is dropped from the cell and stays in its tooltip.
//
// Design: Docs/UltraCanvas/UltraCanvasPerformanceMatrixProposal.md
// Guide:  Docs/UltraCanvas/UltraCanvasPerformanceMatrix.md
//
// Version: 1.0.0
// Last Modified: 2026-10-27
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include "Plugins/Diagrams/UltraCanvasPerformanceMatrixModel.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    enum class PerformanceMatrixDesign {
        Grid,
        Tiles,
        HeaderTabs,
        Cards
    };

    enum class PerformanceCellColoring {
        Plain,          // plain cells; the colour lives in the headers
        Explicit,       // each PerformanceCell::fill
        DiagonalBands,  // by distance from the best corner (PerformanceMatrixModel::Rank)
        ByRow,          // the row level's colour
        ByColumn        // the column level's colour
    };

    // Where the column labels go. Auto: above the grid for Tiles and
    // HeaderTabs, below it for Grid and Cards.
    enum class PerformanceColumnLabels { Auto, Top, Bottom };

    struct PerformanceMatrixPalette {
        std::vector<Color> rankColors;    // best -> worst, for DiagonalBands
        std::vector<Color> levelColors;   // low -> high, for levels without an accent
        bool  forceLevelColors = false;   // ignore the levels' own accents (one-hue palettes)
        Color background      = Color(255, 255, 255, 255);
        Color panelBackground = Color(244, 246, 249, 255);   // the container panel
        Color cellBackground  = Color(255, 255, 255, 255);
        Color gridLine        = Color(208, 213, 221, 255);
        Color text            = Color(38, 42, 50, 255);
        Color mutedText       = Color(102, 108, 118, 255);

        // Blue, green, yellow, orange, red from the best corner (codes on tiles).
        static PerformanceMatrixPalette TrafficLight();
        // Saturated red / orange / green levels on white (header tabs).
        static PerformanceMatrixPalette Vivid();
        // Every colour a shade of one hue.
        static PerformanceMatrixPalette Monochrome(const Color& hue);
        // Soft pink / orange / yellow levels (talent review cards).
        static PerformanceMatrixPalette Pastel();
    };

    class UltraCanvasPerformanceMatrix : public UltraCanvasChartElementBase {
    public:
        UltraCanvasPerformanceMatrix(const std::string& id, int x, int y, int w, int h);

        // ===== DATA =====
        void SetModel(const PerformanceMatrixModel& model);
        const PerformanceMatrixModel& GetModel() const { return model; }

        // ===== LOOK =====
        void SetDesign(PerformanceMatrixDesign d);
        PerformanceMatrixDesign GetDesign() const { return design; }
        void SetCellColoring(PerformanceCellColoring c);
        PerformanceCellColoring GetCellColoring() const { return coloring; }
        void SetPalette(const PerformanceMatrixPalette& p);
        const PerformanceMatrixPalette& GetPalette() const { return palette; }
        void SetColumnLabels(PerformanceColumnLabels placement);
        void SetShowAxisArrows(bool show);           // Grid, Tiles, Cards
        void SetShowContainerPanel(bool show);       // a rounded panel behind the matrix
        // The cell text size the fitting starts from (13); it never goes below 9.
        void SetCellFontSize(float size);

        // ===== SELECTION =====
        void SetSelectedCell(const PerformanceCellRef& ref);
        PerformanceCellRef GetSelectedCell() const { return selectedCell; }
        PerformanceCellRef GetHoveredCell() const { return hoveredCell; }
        void ClearSelection() { SetSelectedCell(PerformanceCellRef()); }

        std::function<void(const PerformanceCellRef&)> onCellClick;
        std::function<void(const PerformanceCellRef&)> onCellHover;   // invalid ref = left the cells

        // ===== GEOMETRY (valid after the first render) =====
        Rect2Dd GetCellRect(int row, int col) const;
        PerformanceCellRef CellAt(const Point2Di& local) const;
        // The size the cell text was fitted to, and whether any cell had to
        // drop content to fit (it is still in the tooltip).
        float GetFittedFontSize() const { return layout.fontSize; }
        bool AnyCellTruncated() const;
        std::string BuildCellTooltip(const PerformanceCellRef& ref) const;

        // ===== OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;
        void InvalidateLayout() override { DropLayoutCache(); UltraCanvasChartElementBase::InvalidateLayout(); }

    private:
        // One line of a cell's text, ready to draw.
        struct TextLine {
            std::string text;
            float size = 12.0f;
            bool bold = false;
            bool bullet = false;     // drawn with a bullet mark and indented
            bool muted = false;
            bool continuation = false;   // a wrapped bullet's later line: indented, no mark
        };
        struct CellLayout {
            PerformanceCellRef ref;
            Rect2Dd rect;            // the drawn cell / tile / card
            Rect2Dd content;         // inside the padding
            std::vector<TextLine> lines;
            double textHeight = 0.0;
            bool truncated = false;
        };
        struct HeaderLayout {
            Rect2Dd rect;
            int level = 0;
        };
        struct Layout {
            bool valid = false;
            Rect2Dd panel;
            Rect2Dd grid;
            std::vector<CellLayout> cells;     // every (row, col), row-major
            std::vector<HeaderLayout> rowHeaders, columnHeaders;
            bool columnLabelsOnTop = false;
            double rowLabelWidth = 0.0;
            Rect2Dd yTitleBox, xTitleBox;
            double yArrowX = 0.0, xArrowY = 0.0;
            float fontSize = 13.0f;
        };

        PerformanceMatrixModel model;
        PerformanceMatrixDesign design = PerformanceMatrixDesign::Grid;
        PerformanceCellColoring coloring = PerformanceCellColoring::DiagonalBands;
        PerformanceMatrixPalette palette = PerformanceMatrixPalette::TrafficLight();
        PerformanceColumnLabels columnLabels = PerformanceColumnLabels::Auto;
        bool showAxisArrows = true;
        bool showContainerPanel = false;
        float cellFontSize = 13.0f;
        PerformanceCellRef hoveredCell, selectedCell;
        Layout layout;

        void DropLayoutCache() { layout.valid = false; }
        void UpdateLayout(IRenderContext* ctx);
        // The title as drawn: 18 px, smaller down to 11 px to fit the width,
        // then cut with an ellipsis. Leaves the font set for drawing it.
        std::string FitTitle(IRenderContext* ctx) const;
        double HeaderBlockHeight(IRenderContext* ctx, const PerformanceLevel& level, double width) const;
        std::vector<TextLine> BuildLines(IRenderContext* ctx, const PerformanceCell& cell, double width,
                                         float size, int drop, size_t bulletLimit) const;
        double LinesHeight(const std::vector<TextLine>& lines) const;
        double FixedContentHeight(const PerformanceCell& cell, float size) const;
        std::vector<std::string> Wrap(IRenderContext* ctx, const std::string& text, double width,
                                      float size, bool bold) const;
        void FitCell(IRenderContext* ctx, CellLayout& cell, float size) const;

        int DisplayRow(int row) const;
        int DisplayColumn(int col) const;
        Color LevelColor(const PerformanceAxis& axis, int level) const;
        Color CellAccent(int row, int col) const;
        Color CellFill(int row, int col) const;
        const CellLayout* FindLayout(const PerformanceCellRef& ref) const;

        void DrawCells(IRenderContext* ctx);
        void DrawCellText(IRenderContext* ctx, const CellLayout& cell, const Color& textColor,
                          const Color& accent) const;
        void DrawHeaders(IRenderContext* ctx);
        void DrawLevelLabel(IRenderContext* ctx, const PerformanceLevel& level, const Rect2Dd& box,
                            const Color& textColor, bool withIcon) const;
        void DrawAxes(IRenderContext* ctx);
    };

    std::shared_ptr<UltraCanvasPerformanceMatrix> CreatePerformanceMatrix(
            const std::string& id, int x, int y, int width, int height);

} // namespace UltraCanvas

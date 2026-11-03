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
// The population - the model's items - is shown in each cell as a count, a
// percentage (against a target when the model has one), dots or names, with
// "+N" where a cell runs out of room. A legend of the groups filters it, and
// an anonymised mode shows counts only.
//
// For a calibration meeting the people can be moved: dragged to another cell
// or, once selected, moved with Ctrl+arrows (both off until
// SetAllowItemDrag(true)), with onItemMove able to veto each move. Arrows
// move the cell selection, Enter activates it, and a "+N more" mark hands
// everyone in its cell to onOverflowClick for a full list.
//
// Design: Docs/UltraCanvas/UltraCanvasPerformanceMatrixProposal.md
// Guide:  Docs/UltraCanvas/UltraCanvasPerformanceMatrix.md
//
// Version: 1.2.0 - calibration: drag an item to another cell with a veto, the
//   keyboard (arrows, Enter, Escape, Ctrl+arrows), the selected item, the
//   overflow list callback
// Version: 1.1.0 - the population: Count / Percent / Dots / Names, targets, the
//   group legend, the anonymised mode, ByCount, level bars, row and column
//   totals, item tooltips and clicks
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include "Plugins/Charts/UltraCanvasChartLegend.h"
#include "Plugins/Charts/UltraCanvasColormap.h"
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
        ByColumn,       // the column level's colour
        ByCount         // how many items the cell holds, through a colour map
    };

    // How a cell shows the items in it. Count is the default: it is honest at
    // any size and shows no names.
    enum class PerformancePopulationDisplay {
        Hidden,
        Count,      // the number, in a chip in the cell's corner
        Percent,    // the share of the population, and the target share when there is one
        Dots,       // one dot per item in its group's colour, "+N" when they do not fit
        Names       // the items' labels in columns, "+N more" when they do not fit
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
        // The population's groups, in order; colour-blind-safe by default.
        std::vector<Color> groupColors = {Color(0, 114, 178, 255), Color(213, 94, 0, 255),
                                          Color(0, 158, 115, 255), Color(204, 121, 167, 255),
                                          Color(230, 159, 0, 255), Color(86, 180, 233, 255)};

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
        // Coloured strips along the axes in the level colours.
        void SetShowLevelBars(bool show);
        // The colour map of the ByCount colouring (Blues).
        void SetCountColormap(HeatmapColormap map);

        // ===== POPULATION =====
        void SetPopulationDisplay(PerformancePopulationDisplay display);
        PerformancePopulationDisplay GetPopulationDisplay() const { return populationDisplay; }
        // For a room that should not see names: Names show as counts, and
        // items answer neither the pointer nor clicks.
        void SetAnonymised(bool on);
        bool IsAnonymised() const { return anonymised; }
        void SetShowLegend(bool show);              // the groups; a click hides one
        void SetShowTotals(bool show);              // "n = 12" under each level label
        void SetGroupVisible(const std::string& group, bool visible);
        bool IsGroupVisible(const std::string& group) const;
        // Items in the cell, leaving out hidden groups.
        int GetCellCount(const PerformanceCellRef& ref) const;

        std::function<void(const PerformanceMatrixItem&)> onItemClick;
        // The visible items in a cell, in model order: indices into
        // GetModel().items.
        std::vector<size_t> GetCellItems(const PerformanceCellRef& ref) const;
        // A click on a cell's "+N" / "+N more" mark: everyone in that cell
        // (GetCellItems), for the application to list. Not called when
        // anonymised.
        std::function<void(const PerformanceCellRef&, const std::vector<size_t>&)> onOverflowClick;

        // ===== MOVING ITEMS (calibration) =====
        // Off by default. When on, an item drawn as a dot or name can be
        // dragged to another cell, and the selected item moves with
        // Ctrl+arrows. Never while anonymised.
        void SetAllowItemDrag(bool allow);
        bool GetAllowItemDrag() const { return allowItemDrag; }
        // Asked before every move, by drag, key or MoveItem; return false to
        // veto. The model changes only after it returns true: the item gets
        // the target as its explicit cell, and its scores stay as they were.
        std::function<bool(const PerformanceMatrixItem&, const PerformanceCellRef& from,
                           const PerformanceCellRef& to)> onItemMove;
        // Moves an item as a drag would, whatever SetAllowItemDrag says.
        // False when the index or cell is invalid, the item is already
        // there, or onItemMove vetoed.
        bool MoveItem(size_t index, const PerformanceCellRef& to);
        // The item shown selected (a ring); -1 for none. Clicking an item
        // selects it.
        void SetSelectedItem(int index);
        int GetSelectedItem() const { return selectedItem; }
        // While a drag is under way: the item and the cell under the pointer.
        bool IsDraggingItem() const { return dragging; }
        PerformanceCellRef GetDropTarget() const { return dropTarget; }

        // ===== SELECTION =====
        void SetSelectedCell(const PerformanceCellRef& ref);
        PerformanceCellRef GetSelectedCell() const { return selectedCell; }
        PerformanceCellRef GetHoveredCell() const { return hoveredCell; }
        void ClearSelection() { SetSelectedCell(PerformanceCellRef()); SetSelectedItem(-1); }

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
        // The item drawn at a point (its dot or name), -1 for none or when
        // anonymised; its index in GetModel().items.
        int ItemAt(const Point2Di& local) const;
        // Where an item is drawn; empty when it is not (Count / Percent, a
        // hidden group, or cut by "+N").
        Rect2Dd GetItemRect(size_t index) const;
        int GetHoveredItem() const { return hoveredItem; }
        std::string BuildItemTooltip(size_t index) const;
        // The legend entry at a point, SIZE_MAX for none.
        size_t LegendEntryAt(const Point2Di& local) const;
        // The cell whose "+N" mark is at a point; invalid for none.
        PerformanceCellRef OverflowAt(const Point2Di& local) const;

        // ===== OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;
        // Arrows, Enter and Escape need the focus, which a click gives it.
        bool AcceptsFocus() const override { return true; }
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
        // One dot or name, or the "+N" that stands in for the rest.
        struct ItemMark {
            int item = -1;           // index into model.items; -1 for "+N"
            Rect2Dd rect;
            int more = 0;            // items the "+N" stands for
        };
        struct CellLayout {
            PerformanceCellRef ref;
            Rect2Dd rect;            // the drawn cell / tile / card
            Rect2Dd content;         // inside the padding, minus the population
            Rect2Dd population;      // dots or names; empty otherwise
            std::vector<TextLine> lines;
            double textHeight = 0.0;
            bool truncated = false;
            int count = 0;
            std::vector<ItemMark> marks;
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
            Rect2Dd legendArea;
            bool legendShown = false;
            std::vector<int> rowCounts, columnCounts;
            int total = 0, maxCount = 0;
            std::vector<Rect2Dd> itemRects;   // per model item; empty = not drawn
        };

        PerformanceMatrixModel model;
        PerformanceMatrixDesign design = PerformanceMatrixDesign::Grid;
        PerformanceCellColoring coloring = PerformanceCellColoring::DiagonalBands;
        PerformanceMatrixPalette palette = PerformanceMatrixPalette::TrafficLight();
        PerformanceColumnLabels columnLabels = PerformanceColumnLabels::Auto;
        bool showAxisArrows = true;
        bool showContainerPanel = false;
        float cellFontSize = 13.0f;
        bool showLevelBars = false;
        HeatmapColormap countColormap = HeatmapColormap::Blues;
        PerformancePopulationDisplay populationDisplay = PerformancePopulationDisplay::Count;
        bool anonymised = false;
        bool showLegend = true;
        bool showTotals = false;
        std::vector<std::string> hiddenGroups;
        PerformanceCellRef hoveredCell, selectedCell;
        int hoveredItem = -1;
        int selectedItem = -1;
        // A press on an item that may become a drag.
        bool allowItemDrag = false;
        int pressedItem = -1;
        Point2Di pressPoint, dragPoint;
        bool dragging = false;
        PerformanceCellRef dropTarget;
        ChartLegend legend;
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
        PerformancePopulationDisplay EffectiveDisplay() const;
        bool HasPopulation() const { return !model.items.empty(); }
        Color GroupColor(const std::string& group) const;
        Color ItemColor(const PerformanceMatrixItem& item) const;
        std::string TotalsText(int count) const;
        void LayoutPopulation(IRenderContext* ctx, CellLayout& cell);
        void DrawPopulation(IRenderContext* ctx, const CellLayout& cell, const Color& fill) const;
        void DrawLevelBars(IRenderContext* ctx);

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
                            const Color& textColor, bool withIcon, const std::string& extra) const;
        void DrawAxes(IRenderContext* ctx);
        void DrawDrag(IRenderContext* ctx) const;

        bool CanMoveItems() const { return allowItemDrag && !anonymised; }
        void EndDrag();
        bool HandleKeyDown(const UCEvent& event);
        // The cell one step from `from` in a direction on screen; `from`
        // itself at the edge.
        PerformanceCellRef Neighbour(const PerformanceCellRef& from, int dx, int dy) const;
    };

    std::shared_ptr<UltraCanvasPerformanceMatrix> CreatePerformanceMatrix(
            const std::string& id, int x, int y, int width, int height);

} // namespace UltraCanvas

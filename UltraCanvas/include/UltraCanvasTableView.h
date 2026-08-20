// include/UltraCanvasTableView.h
// Styled table widget: typed cells (text, numbers, check/cross glyphs,
// badges, buttons), section and total rows, a highlighted column with a
// badge, sorting, selection, and region-based design presets from
// UltraCanvasTableStyle.h. Presentation-first (read-only + callbacks);
// see Docs/UltraCanvas/UltraCanvasTableView.md.
// Version: 2.0.0
// Last Modified: 2026-08-20
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasScrollbar.h"
#include "UltraCanvasTableStyle.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// ============================================================================
// CELLS
// ============================================================================

enum class TableCellType {
    Text,
    Number,     // Formatted from `number` (decimals/thousands separator)
    Check,      // Included: check-mark glyph
    Cross,      // Not included: circled-slash glyph
    Minus,      // Not applicable: short dash
    Badge,      // Chip with tinted background and `text`
    Button,     // Rounded action button labelled `text` -> onCellAction
    Custom      // customRenderer draws into the cell rect
};

struct TableViewCell {
    TableCellType type = TableCellType::Text;
    std::string text;                       // Text/Badge/Button label
    double number = 0.0;                    // Number value
    int decimals = 2;                       // Number formatting
    bool thousandsSeparator = true;
    std::optional<Color> textColor;         // Per-cell overrides (rare)
    std::optional<Color> background;
    std::optional<TextAlignment> hAlign;
    // Custom cells: draws inside `rect` (element-local, already clipped).
    std::function<void(IRenderContext*, const Rect2Df& rect,
                       const TableViewCell&)> customRenderer;

    TableViewCell() = default;
    TableViewCell(const std::string& t) : text(t) {}    // Implicit: Text cell

    static TableViewCell MakeText(const std::string& t) { return TableViewCell(t); }
    static TableViewCell MakeNumber(double value, int decimals = 2,
                                    bool thousands = true) {
        TableViewCell c;
        c.type = TableCellType::Number;
        c.number = value;
        c.decimals = decimals;
        c.thousandsSeparator = thousands;
        return c;
    }
    static TableViewCell MakeCheck()  { TableViewCell c; c.type = TableCellType::Check; return c; }
    static TableViewCell MakeCross()  { TableViewCell c; c.type = TableCellType::Cross; return c; }
    static TableViewCell MakeMinus()  { TableViewCell c; c.type = TableCellType::Minus; return c; }
    static TableViewCell MakeBadge(const std::string& t) {
        TableViewCell c; c.type = TableCellType::Badge; c.text = t; return c;
    }
    static TableViewCell MakeButton(const std::string& t) {
        TableViewCell c; c.type = TableCellType::Button; c.text = t; return c;
    }
};

// ============================================================================
// ROWS AND COLUMNS
// ============================================================================

enum class TableRowKind {
    Data,
    Section,    // Full-width group band; cells[0].text is the caption
    Total       // Footer/total band (styled by TableRegion::TotalRow)
};

struct TableViewRow {
    TableRowKind kind = TableRowKind::Data;
    std::vector<TableViewCell> cells;
    void* userData = nullptr;
};

struct TableViewColumn {
    std::string title;
    float width = 0.0f;         // Fixed pixel width; 0 = stretch
    float stretch = 1.0f;       // Weight for stretch columns (width == 0)
    float minWidth = 40.0f;
    std::optional<TextAlignment> alignment;  // Unset = from the style regions
    bool sortable = false;

    TableViewColumn() = default;
    TableViewColumn(const std::string& t, float w = 0.0f, float stretchWeight = 1.0f)
        : title(t), width(w), stretch(stretchWeight) {}
};

// Values avoid bare None/Row/Cell: X11 headers #define None.
enum class TableSelectionMode { NoSelection, RowSelection, CellSelection };

// ============================================================================
// TABLE VIEW
// ============================================================================

class UltraCanvasTableView : public UltraCanvasUIElement {
public:
    // ===== CALLBACKS =====
    std::function<void(int row)> onRowClick;                // Data-row index in `rows`
    std::function<void(int row, int col)> onCellClick;
    std::function<void(int row)> onRowDoubleClick;
    std::function<void(int row)> onRowActivate;             // Return/Space
    std::function<void(int row, int col)> onCellAction;     // Button cells
    std::function<void(int col, bool ascending)> onColumnSort;
    std::function<void(int row, int col)> onSelectionChange; // (-1,-1) = cleared

    // ===== CONSTRUCTION =====
    UltraCanvasTableView(const std::string& identifier,
                         float x, float y, float w, float h);
    UltraCanvasTableView(const std::string& identifier, float w, float h)
        : UltraCanvasTableView(identifier, -1, -1, w, h) {}
    explicit UltraCanvasTableView(const std::string& identifier)
        : UltraCanvasTableView(identifier, -1, -1, -1, -1) {}
    ~UltraCanvasTableView() override = default;

    // ===== COLUMNS =====
    void SetColumns(const std::vector<TableViewColumn>& cols);
    void AddColumn(const TableViewColumn& column);
    void AddColumn(const std::string& title, float width = 0.0f);
    int GetColumnCount() const { return static_cast<int>(columns.size()); }
    TableViewColumn& EditColumn(int col) { return columns[col]; }
    const TableViewColumn& GetColumn(int col) const { return columns[col]; }

    // Highlighted (recommended/current) column, with an optional badge chip
    // drawn in a band above the header ("Aktuelle Version"). -1 = none.
    void SetHighlightedColumn(int col, const std::string& badgeText = "");
    int GetHighlightedColumn() const { return highlightedColumn; }
    void ClearHighlightedColumn() { SetHighlightedColumn(-1); }

    // ===== ROWS =====
    int AddRow(const std::vector<TableViewCell>& cells, void* userData = nullptr);
    int AddRow(const std::vector<std::string>& texts, void* userData = nullptr);
    int AddSectionRow(const std::string& caption);
    int AddTotalRow(const std::vector<TableViewCell>& cells);
    void ClearRows();
    int GetRowCount() const { return static_cast<int>(rows.size()); }
    TableViewRow& EditRow(int row) { return rows[row]; }
    const TableViewRow& GetRow(int row) const { return rows[row]; }
    void SetCell(int row, int col, const TableViewCell& cell);
    const TableViewCell* GetCell(int row, int col) const;

    // ===== STYLE =====
    void SetDesign(TableDesign design);
    void SetDesign(TableDesign design, const TablePalette& palette);
    void SetStyle(const TableStyleSheet& sheet);
    const TableStyleSheet& GetStyle() const { return style; }
    // Mutate the style in place, then call StyleChanged().
    TableStyleSheet& EditStyle() { return style; }
    void StyleChanged();

    void SetShowHeader(bool show);
    bool GetShowHeader() const { return showHeader; }

    // ===== SELECTION =====
    void SetSelectionMode(TableSelectionMode mode);
    TableSelectionMode GetSelectionMode() const { return selectionMode; }
    void SelectRow(int row);                 // -1 clears
    void SelectCell(int row, int col);
    int GetSelectedRow() const { return selectedRow; }
    int GetSelectedColumn() const { return selectedCol; }
    void ClearSelection() { SelectRow(-1); }

    // ===== SORTING =====
    // Stable sort of Data rows by `col` (numeric when both parse, else
    // lexicographic). Section rows keep their place and each section's rows
    // sort within their own block; Total rows stay put. Header clicks toggle
    // this for columns with sortable = true.
    void SortByColumn(int col, bool ascending = true);
    int GetSortColumn() const { return sortColumn; }
    bool IsSortAscending() const { return sortAscending; }

    // ===== SCROLLING =====
    void ScrollToRow(int row);
    void EnsureRowVisible(int row);

    // Full content height (badge band + header + all rows + frame), for
    // sizing the element to fit without scrolling.
    float GetContentHeight() const;

    // ===== NUMBER FORMATTING =====
    static std::string FormatNumber(double value, int decimals, bool thousands);

    // ===== UIELEMENT INTERFACE =====
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    void Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) override;
    void SetBounds(const Rect2Df& bounds) override;
    void SetWindow(UltraCanvasWindowBase* win) override;
    bool AcceptsFocus() const override { return true; }

private:
    // Data
    std::vector<TableViewColumn> columns;
    std::vector<TableViewRow> rows;

    // Style
    TableStyleSheet style;
    bool showHeader = true;

    // Highlight column
    int highlightedColumn = -1;
    std::string highlightBadgeText;

    // Selection / interaction
    TableSelectionMode selectionMode = TableSelectionMode::RowSelection;
    int selectedRow = -1;
    int selectedCol = -1;
    int hoveredRow = -1;
    int pressedButtonRow = -1;
    int pressedButtonCol = -1;

    // Sorting
    int sortColumn = -1;
    bool sortAscending = true;

    // Scrolling
    std::shared_ptr<UltraCanvasScrollbar> verticalScrollbar;
    int scrollOffsetY = 0;
    int maxScrollY = 0;

    // Cached row geometry (prefix sums; rows differ in height by kind)
    mutable std::vector<float> rowTops;      // Size rows+1; content-space tops
    mutable std::vector<int> dataOrdinals;   // Data-row ordinal per row (bands)
    mutable bool rowGeometryValid = false;

    // Cached column geometry for the current width
    mutable std::vector<float> columnLefts;  // Size columns+1; viewport-local
    mutable float columnGeometryWidth = -1.0f;

    // ----- Geometry -----
    Rect2Df GetViewportRect() const;         // Rows area (element-local)
    float GetChromeOffset() const;           // Badge band + header height
    float GetBadgeBandHeight() const;
    float RowHeightFor(const TableViewRow& row) const;
    float RowTopOffset(int row) const;       // Content-space top of `row`
    float RowsContentHeight() const;
    int RowAtContentY(float contentY) const; // -1 when outside
    int GetRowAtY(float y) const;            // Element-local y -> row
    int GetColumnAtX(float x) const;         // Element-local x -> column
    Rect2Df GetCellRect(int row, int col) const;   // Element-local
    Rect2Df ButtonRectInCell(const Rect2Df& cellRect,
                             const std::string& label,
                             IRenderContext* ctx) const;
    void RebuildRowGeometryIfNeeded() const;
    void RebuildColumnGeometryIfNeeded() const;
    void InvalidateGeometry();

    // ----- Style resolution -----
    TableResolvedCellStyle ResolveCellStyle(int row, int col) const;
    TableResolvedCellStyle ResolveHeaderStyle(int col) const;

    // ----- Rendering -----
    void RenderBadgeBand(IRenderContext* ctx, const Rect2Df& contentRect);
    void RenderHeader(IRenderContext* ctx, const Rect2Df& contentRect);
    void RenderRows(IRenderContext* ctx, const Rect2Df& contentRect);
    void RenderRowBorders(IRenderContext* ctx, const TableResolvedCellStyle& st,
                          const Rect2Df& rect);
    void RenderCellContent(IRenderContext* ctx, int row, int col,
                           const TableViewCell& cell,
                           const TableResolvedCellStyle& st,
                           const Rect2Df& cellRect);
    void RenderCheckGlyph(IRenderContext* ctx, const Rect2Df& cellRect);
    void RenderCrossGlyph(IRenderContext* ctx, const Rect2Df& cellRect);
    void RenderMinusGlyph(IRenderContext* ctx, const Rect2Df& cellRect);
    void RenderBadgeCell(IRenderContext* ctx, const TableViewCell& cell,
                         const Rect2Df& cellRect);
    void RenderButtonCell(IRenderContext* ctx, int row, int col,
                          const TableViewCell& cell, const Rect2Df& cellRect);
    void RenderSortIndicator(IRenderContext* ctx, const Rect2Df& headerCell,
                             bool ascending, const Color& color);

    // ----- Scrollbar -----
    void CreateScrollbar();
    void UpdateScrollbar();
    void ClampScrollOffset();

    // ----- Events -----
    bool HandleMouseDown(const UCEvent& event);
    bool HandleMouseUp(const UCEvent& event);
    bool HandleMouseMove(const UCEvent& event);
    bool HandleMouseDoubleClick(const UCEvent& event);
    bool HandleMouseWheel(const UCEvent& event);
    bool HandleKeyDown(const UCEvent& event);
    void HandleHeaderClick(int col);
    void MoveSelection(int delta);           // Skips Section rows
    int NextSelectableRow(int from, int direction) const;
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

inline std::shared_ptr<UltraCanvasTableView> CreateTableView(
        const std::string& identifier, float x, float y, float w, float h) {
    return UltraCanvasUIElementFactory::Create<UltraCanvasTableView>(
            identifier, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasTableView> CreateTableView(
        const std::string& identifier, float w, float h) {
    return UltraCanvasUIElementFactory::Create<UltraCanvasTableView>(
            identifier, w, h);
}

inline std::shared_ptr<UltraCanvasTableView> CreateTableView(
        const std::string& identifier, TableDesign design,
        float x = -1, float y = -1, float w = -1, float h = -1) {
    auto table = CreateTableView(identifier, x, y, w, h);
    table->SetDesign(design);
    return table;
}

} // namespace UltraCanvas

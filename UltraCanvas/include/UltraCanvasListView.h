// include/UltraCanvasListView.h
// Model-View-Delegate ListView widget
// Last Modified: 2026-09-19
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasScrollbar.h"
#include "UltraCanvasListModel.h"
#include "UltraCanvasListDelegate.h"
#include "UltraCanvasListSelection.h"
#include <vector>
#include <string>
#include <memory>
#include <functional>

namespace UltraCanvas {


    // ===== VIEW STYLE =====

    struct ListViewStyle {
        Color backgroundColor = Colors::White;
        Color headerBackgroundColor = Color(240, 240, 240);
        Color headerTextColor = Colors::Black;
        Color gridLineColor = Color(220, 220, 220);

        float headerFontSize = 10;
        // Width (px) of the sort-direction triangle drawn in the sorted
        // column's header cell (see SetSortIndicator). It is half as tall as it
        // is wide and painted in headerTextColor.
        int sortIndicatorSize = 8;

        int rowHeight = 24;
        int headerHeight = 26;
        bool showHeader = false;
        bool showGridLines = false;
        bool alternateRowColors = false;
        Color alternateRowColor = Color(248, 248, 248);

        Color selectionBackgroundColor = Colors::Selection;
        Color hoverBackgroundColor = Colors::SelectionHover;

        ScrollbarStyle scrollbarStyle = GetDefaultScrollbarStyleOr(ScrollbarStyle::Modern());
    };

    // ===== LIST VIEW WIDGET =====

    class UltraCanvasListView : public UltraCanvasUIElement {
    public:
        // Callbacks
        std::function<void(int row)> onItemClicked;
        std::function<void(int row)> onItemDoubleClicked;
        std::function<void(int row)> onItemActivated;
        std::function<void(const std::vector<int>&)> onSelectionChanged;
        std::function<void(int row)> onItemHovered;

        // Cell-level callbacks (multi-column aware). posInCell is relative to the
        // cell's top-left corner, i.e. the same space as the rects a delegate
        // computes from ListItemStyleOption (x - columnX, y - row top). Hover is
        // reported on every mouse move; (row=-1, column=-1) means the pointer
        // left the rows area.
        std::function<void(int row, int column, const Point2Di& posInCell)> onCellClicked;
        std::function<void(int row, int column, const Point2Di& posInCell)> onCellHovered;

        // A click (press and release in the same cell) on a column header, when
        // the header is shown. Fires after a press that did not start a column
        // resize. The usual handler re-sorts the model by `column`, toggling the
        // direction when it is already the sort column, then calls
        // SetSortIndicator so the header shows the new order.
        std::function<void(int column)> onHeaderClicked;

        // Optional tooltip source, consulted before the model's ToolTipRole.
        // Called with the hovered cell; row == -1 means the pointer is over the
        // header cell of `column`. Returning an empty string falls back to the
        // model tooltip (ToolTipRole) or, for the header, to
        // ListColumnDef::tooltip.
        std::function<std::string(int row, int column)> tooltipProvider;

        // Constructor
        UltraCanvasListView(const std::string& identifier,
                            float x, float y, float w, float h);

        UltraCanvasListView(const std::string& identifier, float w, float h)
            : UltraCanvasListView(identifier, -1, -1, w, h) {}

        explicit UltraCanvasListView(const std::string& identifier)
            : UltraCanvasListView(identifier, -1, -1, -1, -1) {}

        virtual ~UltraCanvasListView() = default;

        // === Model / Delegate / Selection wiring ===
        void SetModel(std::shared_ptr<IListModel> model);
        IListModel* GetModel() const;

        void SetDelegate(std::shared_ptr<IItemDelegate> delegate);
        IItemDelegate* GetDelegate() const;

        void SetSelection(std::shared_ptr<IListSelection> selection);
        IListSelection* GetSelection() const;
        // Clear the selection and reset keyboard focus to "no row" (so the next NavigateDown/Up
        // starts from the first row). Used when the list contents change under an open popup.
        void ResetSelection();

        void SetStyle(const ListViewStyle& style);
        const ListViewStyle& GetStyle() const;

        void SetRowHeight(int height);
        int GetRowHeight() const;

        // Variable row heights. When enabled the height of each row comes from
        // the delegate (IItemDelegate::GetRowHeight(model, row)) instead of the
        // single viewStyle.rowHeight, so rows can differ in height. Off by
        // default (uniform rows, unchanged fast path). A custom delegate that
        // returns per-row heights is expected; the default delegate reports one
        // constant height (its own SetRowHeight value).
        void SetVariableRowHeights(bool enabled);
        bool GetVariableRowHeights() const;

        // Recompute the cached per-row offsets on the next layout/paint. Call
        // when a custom delegate's row-height results change without the model
        // firing a data-changed signal (e.g. an async delegate finished sizing
        // a row from a decoded thumbnail).
        void InvalidateRowHeights();

        void SetShowHeader(bool show);
        bool GetShowHeader() const;

        // Sort indicator: a small triangle in the header cell of `column`,
        // apex up for ascending, apex down for descending, drawn in
        // headerTextColor so it follows the header theme. The view only shows
        // it; ordering the rows is the model's / caller's job (see
        // onHeaderClicked). -1 (the default) shows none.
        void SetSortIndicator(int column, bool ascending);
        void ClearSortIndicator() { SetSortIndicator(-1, true); }
        int  GetSortColumn() const { return sortColumn; }
        bool GetSortAscending() const { return sortAscending; }

        // Per-view column widths (multi-column). A column's width normally comes
        // from the model (ListColumnDef::width); SetColumnWidth overrides it for
        // this view only — used by interactive resize and by callers that fit a
        // column to the viewport. GetColumnWidth returns the effective width.
        void SetColumnWidth(int column, int width);
        int  GetColumnWidth(int column) const;

        // Interactive column resizing by dragging the header column borders
        // (on by default; needs the header shown and >= 2 columns).
        void SetColumnsResizable(bool resizable) { columnsResizable = resizable; }
        bool GetColumnsResizable() const { return columnsResizable; }
        // True once the user has dragged a column border, so a caller's auto-fit
        // can stop overriding the user's chosen widths.
        bool ColumnsUserAdjusted() const { return userAdjustedColumns; }

        // Hover tooltips. On by default: resting the pointer on a row shows the
        // cell's ToolTipRole text (per-cell, falling back to the row tooltip),
        // and resting it on a column header shows that column's
        // ListColumnDef::tooltip. `tooltipProvider` overrides both.
        void SetShowItemTooltips(bool enable);
        bool GetShowItemTooltips() const;

        // === Scrolling ===
        void ScrollToRow(int row);
        void EnsureRowVisible(int row);

        // === Hit testing ===
        // Column under an element-local x coordinate (-1 if outside the rows
        // viewport). columnStartX receives the column's element-local left edge.
        int GetColumnAt(int x, int* columnStartX = nullptr) const;

        // Header column under an element-local point (-1 when the header is
        // hidden or the point is outside it). columnStartX receives the
        // column's element-local left edge.
        int GetHeaderColumnAt(int x, int y, int* columnStartX = nullptr) const;

        // Column index whose right border is under an element-local point in the
        // header band (within ~4px), for resize hit-testing; -1 otherwise.
        int ColumnBoundaryAt(int x, int y) const;

        // Tooltip text for a cell (row >= 0) or a column header (row == -1),
        // as the hover tooltip would show it. Empty when there is none.
        std::string GetTooltipTextAt(int row, int column) const;

        // === Core overrides ===
        // ListView is externally sized (explicit size or parent stretch); the base
        // block measure is sufficient. We hook Arrange to recompute the scrollbar
        // against the resolved finalBounds. SetBounds is kept because dropdown/
        // autocomplete popups size this view imperatively (SetSize -> SetBounds) and
        // the window popup pass does not run Measure/Arrange on popups.
        void Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) override;
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;
        void SetBounds(const Rect2Df& bounds) override;
        void SetWindow(UltraCanvasWindowBase* win) override;
        bool AcceptsFocus() const override { return true; }

    private:
        // Model / Delegate / Selection
        std::shared_ptr<IListModel> model;
        std::shared_ptr<IItemDelegate> delegate;
        std::shared_ptr<IListSelection> selection;

        // View state
        ListViewStyle viewStyle;

        // Scrollbar
        std::shared_ptr<UltraCanvasScrollbar> verticalScrollbar;
        int scrollOffsetY = 0;
        int maxScrollY = 0;

        // Variable row heights. When useVariableRowHeights is set the per-row
        // height is taken from the delegate; otherwise every row is
        // viewStyle.rowHeight tall. rowTops is a cached prefix-sum (size
        // rowCount+1) of the element-local content Y of each row top, rebuilt
        // lazily by RebuildRowGeometryIfNeeded(). In uniform mode the table is
        // not used at all (offsets are plain arithmetic).
        bool useVariableRowHeights = false;
        mutable std::vector<int> rowTops;
        mutable bool rowGeometryValid = false;

        // Interaction state
        int hoveredRow = -1;
        int hoveredColumn = -1;
        int hoveredHeaderColumn = -1;
        int focusedRow = -1;

        bool showItemTooltips = true;

        // Column resizing. columnWidthOverrides[col] >= 0 overrides the model's
        // width for this view (index = column; entries default to -1 = use the
        // model). resizeCol >= 0 while a header border is being dragged.
        std::vector<int> columnWidthOverrides;
        bool columnsResizable = true;
        bool userAdjustedColumns = false;
        int  resizeCol = -1;
        int  resizeStartX = 0;
        int  resizeStartW = 0;

        // Sort indicator (SetSortIndicator); sortColumn == -1 shows none.
        int  sortColumn = -1;
        bool sortAscending = true;
        // Header column under the last press, so a release in the same cell
        // counts as a click (onHeaderClicked); -1 when no header press is live.
        int  pressedHeaderColumn = -1;

        // Internal methods
        void CreateScrollbar();
        void UpdateScrollbar();
        void ClampScrollOffset();

        // Geometry
        int GetTotalContentHeight() const;
        int GetHeaderOffset() const;
        Rect2Di GetViewportRect() const;
        int GetRowAtY(int y) const;
        Rect2Di GetRowRect(int row) const;

        // Per-row geometry helpers (uniform fast path, or variable prefix-sum).
        int RowHeightForRow(int row) const;        // height of a single row
        int RowTopOffset(int row) const;           // content-space Y of row top
        int RowsContentHeight() const;             // summed height of all rows
        int ClampRowIndexAtContentY(int contentY) const;  // row at a content Y
        void RebuildRowGeometryIfNeeded() const;
        void InvalidateRowGeometry();

        // Act on a click in the header band: cycle the sort of that column and
        // tell whoever is listening.

        // Rendering
        void RenderHeader(IRenderContext* ctx, const Rect2Di& contentRect);
        void RenderSortIndicator(IRenderContext* ctx, const Rect2Di& cell);
        void RenderRows(IRenderContext* ctx, const Rect2Di& contentRect);

        // Tooltips
        void UpdateHoverTooltip(const UCEvent& event, int row, int column, int headerColumn);
        void HideHoverTooltip();

        // Event handlers
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);
        bool HandleMouseDoubleClick(const UCEvent& event);
        bool HandleMouseWheel(const UCEvent& event);
        bool HandleKeyDown(const UCEvent& event);

        // Keyboard navigation
        void NavigateUp();
        void NavigateDown();
        void NavigatePageUp();
        void NavigatePageDown();
        void NavigateHome();
        void NavigateEnd();

        // Model connection
        void ConnectModelSignals();
        void DisconnectModelSignals();
    };

} // namespace UltraCanvas

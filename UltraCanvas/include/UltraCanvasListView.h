// include/UltraCanvasListView.h
// Model-View-Delegate ListView widget
// Version: 1.7.0 - a horizontal scrollbar when the columns are wider than the
//                  view: the header and the rows scroll sideways together,
//                  Shift+wheel and the Left / Right keys move them,
//                  ScrollToColumn / EnsureColumnVisible from code
// Version: 1.6.0 - onCellDoubleClicked: the cell a double-click hit; GetCellRect: where a
//                  cell is, for an editor laid over it
// Version: 1.5.0 - onKeyDown: a key reaches the owner first (a dropdown's
//                  type-ahead); SetFocusedRow: the arrow keys' move, from code
// Version: 1.4.0 - SetAcceptsFocus: a list inside another element (a dropdown's
//                  popup) leaves the keyboard focus with that element
// Version: 1.3.0 - the hover follows the rows when they scroll under a still
//                  pointer (a wheel scroll, EnsureRowVisible): hoveredRow,
//                  onItemHovered and onCellHovered are re-reported
// Version: 1.2.0 - the callbacks the view puts on its model do nothing once the
//                  view is gone (a model may outlive it); OnModelChanged for
//                  subclasses, instead of wrapping those callbacks
// Version: 1.1.0 - MeasureHeaderWidth / MeasureColumnTextWidth: what fitting a
//                  column to its title and its rows' text takes
// Last Modified: 2026-10-23
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
#include <unordered_map>

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
        // ===== ACCESSIBILITY =====
        // A list; its rows are drawn, not elements, so a reader hears the list
        // and its name (SetAccessibleName()), not each row yet.
        AccessibleRole GetAccessibleRole() const override { return AccessibleRole::List; }

        // Callbacks
        std::function<void(int row)> onItemClicked;
        std::function<void(int row)> onItemDoubleClicked;
        std::function<void(int row)> onItemActivated;
        std::function<void(const std::vector<int>&)> onSelectionChanged;
        std::function<void(int row)> onItemHovered;
        // A key pressed in the view reaches this before the view's own
        // handling (the arrows, Return): true consumes it. A dropdown's open
        // list takes typed letters this way, to go to the item starting with them.
        std::function<bool(const UCEvent&)> onKeyDown;

        // Cell-level callbacks (multi-column aware). posInCell is relative to the
        // cell's top-left corner, i.e. the same space as the rects a delegate
        // computes from ListItemStyleOption (x - columnX, y - row top). Hover is
        // reported on every mouse move; (row=-1, column=-1) means the pointer
        // left the rows area.
        std::function<void(int row, int column, const Point2Di& posInCell)> onCellClicked;
        std::function<void(int row, int column, const Point2Di& posInCell)> onCellHovered;
        // A double-click on a cell, after onItemDoubleClicked: which column
        // it hit, for an owner that edits the cell in place.
        std::function<void(int row, int column, const Point2Di& posInCell)> onCellDoubleClicked;

        // A click (press and release in the same cell) on a column header, when
        // the header is shown. Fires after a press that did not start a column
        // resize. The usual handler re-sorts the model by `column`, toggling the
        // direction when it is already the sort column, then calls
        // SetSortIndicator so the header shows the new order.
        std::function<void(int column)> onHeaderClicked;

        // A right-button press in the rows area. `row` is the row under the
        // pointer, -1 for the empty space below the rows; a row under the
        // pointer is selected first (alone), as every desktop does, so the
        // handler's menu acts on what the user pointed at. The usual handler
        // builds an UltraCanvasMenu of type PopupMenu and calls OpenMenu with
        // event.pointerWindow. When set, the press is consumed here; when not,
        // a right press is handled like a left one.
        std::function<void(int row, const UCEvent& event)> onContextMenu;

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

        // What the scrollbars are computed from, as of now: the rows and
        // their total height, the columns and their total width, the viewport
        // the rows get (less whichever scrollbar shows), the resulting ranges,
        // and where each scrollbar sits (element-local) and whether it shows.
        // For diagnostics and tests; every value is what UpdateScrollbar
        // would use if it ran at this moment.
        struct ScrollMetrics {
            int     rows = 0;
            int     rowHeight = 0;         // uniform rows; 0 when the delegate sizes them
            int     contentHeight = 0;     // every row
            int     viewportHeight = 0;    // the rows area: header and horizontal scrollbar excluded
            int     maxScroll = 0;         // contentHeight - viewportHeight, floored at 0
            int     scrollOffset = 0;
            bool    scrollbarVisible = false;
            Rect2Di scrollbarBounds;
            // Sideways: the columns' total width (0 in single-column mode,
            // where the row is as wide as the view), the width the rows get
            // beside the vertical scrollbar, and the horizontal scrollbar.
            int     contentWidth = 0;
            int     viewportWidth = 0;
            int     maxScrollX = 0;        // contentWidth - viewportWidth, floored at 0
            int     scrollOffsetX = 0;
            bool    horizontalScrollbarVisible = false;
            Rect2Di horizontalScrollbarBounds;
            float   width = 0;             // the element's bounds
            float   height = 0;
        };
        ScrollMetrics GetScrollMetrics() const;

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

        // What fitting a column to its content takes (multi-column), measured
        // on `ctx` - the context the view is painted on - without a layout.
        //  - MeasureHeaderWidth: the narrowest width at which the column's
        //    header shows its whole title, in headerFontSize, with room for the
        //    sort triangle whether or not it is the sorted column - so a fitted
        //    column keeps its width when the order changes, and a translated
        //    title is measured as it reads.
        //  - MeasureColumnTextWidth: the widest DisplayRole text of the
        //    column's rows in `font`, without the delegate's padding. Each
        //    distinct text is measured once and remembered (a date column of
        //    thousands of rows holds a few hundred dates); another font starts
        //    over.
        int MeasureHeaderWidth(IRenderContext* ctx, int column) const;
        int MeasureColumnTextWidth(IRenderContext* ctx, int column, const FontStyle& font) const;

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
        // Before the view has a size, the row is revealed once it has one.
        void EnsureRowVisible(int row);
        // Puts the keyboard's row - and the selection - on `row` and scrolls
        // it into view, as the arrow keys do; Return then activates it.
        void SetFocusedRow(int row);

        // Sideways (multi-column). The columns scroll under the header and the
        // rows together when they are wider than the view: a horizontal
        // scrollbar below the rows, Shift+wheel, and the Left / Right keys
        // (a step each; the view consumes them, so a focused table never
        // switches the tab around it). ScrollToColumn puts `column`'s left
        // edge at the view's left; EnsureColumnVisible scrolls only as far as
        // it takes to show the whole column, or its left edge when it is wider
        // than the view. GetHorizontalScrollOffset is the pixels scrolled.
        void ScrollToColumn(int column);
        void EnsureColumnVisible(int column);
        int  GetHorizontalScrollOffset() const { return scrollOffsetX; }

        // === Hit testing ===
        // Column under an element-local x coordinate (-1 if outside the rows
        // viewport). columnStartX receives the column's element-local left edge.
        int GetColumnAt(int x, int* columnStartX = nullptr) const;

        // Where a cell is, element-local, as it is drawn now (scrolled):
        // its column's left edge and width, its row's top and height. An
        // owner lays an editor over it (GetPositionInWindow for the
        // window's coordinates). Empty for a row or column that does not
        // exist; a row scrolled out of view lies outside the viewport.
        Rect2Di GetCellRect(int row, int column) const;

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
        // A view takes the keyboard focus (Tab, a click) by default. A list that
        // is part of another element - a dropdown's popup list - turns it off,
        // so the focus stays with that element while the list is open and a
        // click on a row does not leave it on a list that then closes.
        bool AcceptsFocus() const override { return acceptsFocus; }
        void SetAcceptsFocus(bool accept) { acceptsFocus = accept; }

    protected:
        // After the view has taken in a change its model signalled - the rows
        // replaced, one changed, inserted or removed. A subclass that keeps
        // something derived from the rows (a column fitted to their text)
        // marks it stale here, rather than wrapping the model's callbacks,
        // which would outlive it.
        virtual void OnModelChanged() {}

    private:
        // Model / Delegate / Selection
        std::shared_ptr<IListModel> model;
        std::shared_ptr<IItemDelegate> delegate;
        std::shared_ptr<IListSelection> selection;

        // View state
        ListViewStyle viewStyle;
        bool acceptsFocus = true;

        // Scrollbars. The vertical one takes the right edge of the rows area,
        // the horizontal one its bottom edge; each is shown only while there
        // is something to scroll in its direction, and when both show, each
        // stops short of the other's track.
        std::shared_ptr<UltraCanvasScrollbar> verticalScrollbar;
        std::shared_ptr<UltraCanvasScrollbar> horizontalScrollbar;
        int scrollOffsetY = 0;
        int maxScrollY = 0;
        int scrollOffsetX = 0;
        int maxScrollX = 0;

        // The scrollbars' geometry, worked out together from the rows, the
        // columns and the bounds: whether each shows, its range, its bounds
        // (element-local) and the viewport the rows keep beside them.
        struct ScrollLayout {
            bool    showVertical = false;
            bool    showHorizontal = false;
            int     maxY = 0;
            int     maxX = 0;
            int     viewportWidth = 0;
            int     viewportHeight = 0;
            Rect2Di verticalBounds;
            Rect2Di horizontalBounds;
        };
        ScrollLayout ComputeScrollLayout() const;

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
        // Where the pointer last was (element and window coordinates) and
        // whether it is still over the view: the rows move under a still
        // pointer when the view scrolls, so the hover is worked out again
        // from here (UpdateHover) and re-reported, as a mouse move would.
        Point2Di lastPointer;
        Point2Di lastPointerWindow;
        bool pointerOver = false;
        // A scroll under the pointer changed the hovered cell without a
        // mouse move; the tooltip is left hidden then (as after any wheel
        // scroll) and shown again by the next move, wherever it lands.
        bool hoverTooltipStale = false;

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

        // MeasureColumnTextWidth's widths, per text, in the font textWidthFont
        // names.
        mutable std::unordered_map<std::string, int> textWidths;
        mutable std::string textWidthFont;

        // Sort indicator (SetSortIndicator); sortColumn == -1 shows none.
        int  sortColumn = -1;
        bool sortAscending = true;
        // Header column under the last press, so a release in the same cell
        // counts as a click (onHeaderClicked); -1 when no header press is live.
        int  pressedHeaderColumn = -1;
        // A row EnsureRowVisible was asked for before the view had a size:
        // revealed once it has one (RevealPendingRow); -1 when none.
        int  pendingVisibleRow = -1;
        void RevealPendingRow();

        // The selection changed (any source): the keyboard focus follows its
        // current row, then onSelectionChanged.
        void HandleSelectionChanged(const std::vector<int>& rows);

        // Internal methods
        void CreateScrollbar();
        void UpdateScrollbar();
        void SyncScrollbarBeforePaint();
        void HideScrollbar(UltraCanvasScrollbar& scrollbar);
        void ClampScrollOffset();
        // The scrollbar under `event.pointer`, or the one being dragged, takes
        // the event; true when it did.
        bool DispatchToScrollbar(UltraCanvasScrollbar& scrollbar, const UCEvent& event);
        // A scrollbar moved to `position`: `offset` (scrollOffsetY or
        // scrollOffsetX) follows it, and so does the hover, when the rows or
        // the columns moved under a still pointer.
        void HandleScrollbarMoved(int& offset, int position);

        // Geometry
        int GetTotalContentHeight() const;
        int GetHeaderOffset() const;
        // The columns' total width (multi-column); 0 in single-column mode,
        // where the one column is as wide as the viewport.
        int ColumnsContentWidth() const;
        // Where column 0 starts, element-local, as scrolled now: the viewport's
        // left edge less the horizontal scroll.
        int ColumnsStartX() const;
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

        // Rendering
        void RenderHeader(IRenderContext* ctx, const Rect2Di& contentRect);
        void RenderSortIndicator(IRenderContext* ctx, const Rect2Di& cell);
        void RenderRows(IRenderContext* ctx, const Rect2Di& contentRect);

        // Tooltips
        void UpdateHoverTooltip(const Point2Di& pointerWindow, int row, int column, int headerColumn);
        void HideHoverTooltip();

        // The hover for the cell under `pointer` (element coordinates):
        // hoveredRow / hoveredColumn / hoveredHeaderColumn, onCellHovered,
        // onItemHovered and - unless `tooltip` is false - the tooltip. True
        // when the hovered row changed. A mouse move and a scroll under a
        // still pointer both come here.
        bool UpdateHover(const Point2Di& pointer, const Point2Di& pointerWindow, bool tooltip);

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
        // The Left / Right keys: the columns move by this many pixels.
        void ScrollHorizontallyBy(int delta);

        // Model connection. The callbacks put on the model hold a weak handle
        // to signalToken and do nothing once it is gone: the model can outlive
        // the view (an application keeps it), and a proxy chained onto it
        // keeps a copy of them (UltraCanvasListSortFilterProxy), so they are
        // made harmless rather than removed - removing them from the model
        // would cut off whoever chained on after the view.
        std::shared_ptr<char> signalToken = std::make_shared<char>(0);
        void ConnectModelSignals();
        void DisconnectModelSignals();
    };

} // namespace UltraCanvas

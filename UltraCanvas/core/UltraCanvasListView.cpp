// core/UltraCanvasListView.cpp
// Model-View-Delegate ListView widget implementation
// Version: 1.6.0 - SetColumnFitting
// Version: 1.5.0 - a horizontal scrollbar when the columns are wider than the view
// Version: 1.4.0 - onCellDoubleClicked, GetCellRect
// Version: 1.3.0 - the hover follows the rows when they scroll under a still pointer
// Version: 1.2.0 - model callbacks inert once the view is destroyed; OnModelChanged
// Version: 1.1.0 - MeasureHeaderWidth / MeasureColumnTextWidth
// Last Modified: 2026-10-23
#include "UltraCanvasListView.h"
#include "UltraCanvasListSortFilterProxy.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasTooltipManager.h"
#include "UltraCanvasDebug.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

    namespace {
        // A header cell's title starts and ends this far inside the cell, and
        // the sort triangle's strip is the triangle plus this gap
        // (RenderHeader draws them, MeasureHeaderWidth counts them).
        constexpr int kHeaderInset   = 4;
        constexpr int kSortStripGap  = 6;
        // How far the columns move for one Left / Right key, and for one
        // line of a Shift+wheel notch (wheelScrollLines of them per notch).
        constexpr int kHorizontalStep = 40;

        // `text` on one line in `font`, in whole pixels rounded up, so a
        // column that width shows all of it.
        int TextLineWidth(IRenderContext* ctx, const std::string& text, const FontStyle& font) {
            if (!ctx || text.empty()) return 0;
            auto layout = ctx->CreateTextLayout(text, false);
            if (!layout) return 0;
            layout->SetFontStyle(font);
            layout->SetWrap(TextWrap::WrapNone);
            return static_cast<int>(std::ceil(layout->GetLayoutWidth()));
        }
    } // namespace

    // ===== CONSTRUCTOR =====

    UltraCanvasListView::UltraCanvasListView(const std::string& identifier,
                                              float x, float y, float w, float h)
        : UltraCanvasUIElement(identifier, x, y, w, h) {

        // Default delegate and selection
        delegate = std::make_shared<UltraCanvasDefaultListDelegate>();
        selection = std::make_shared<UltraCanvasSingleSelection>();
        selection->onSelectionChanged = [this](const std::vector<int>& rows) {
            HandleSelectionChanged(rows);
        };

        // Scrolling defaults
        scrollOffsetY = 0;
        maxScrollY = 0;
        scrollOffsetX = 0;
        maxScrollX = 0;
        CreateScrollbar();

        SetBackgroundColor(Colors::White);
        SetBorders(1, Colors::Gray);
    }

    // ===== MODEL / DELEGATE / SELECTION WIRING =====

    void UltraCanvasListView::SetModel(std::shared_ptr<IListModel> newModel) {
        DisconnectModelSignals();
        model = newModel;
        if (model) {
            ConnectModelSignals();
        }
        scrollOffsetY = 0;
        scrollOffsetX = 0;
        hoveredRow = -1;
        focusedRow = -1;
        // A new model means new columns; drop any per-view width overrides so a
        // stale override can't mis-size a different column set.
        columnWidthOverrides.clear();
        userAdjustedColumns = false;
        columnFitStale = true;
        if (selection) selection->Clear();
        InvalidateRowGeometry();
        UpdateScrollbar();
        RequestRedraw();
    }

    // ===== COLUMN WIDTHS =====
    int UltraCanvasListView::GetColumnWidth(int column) const {
        if (column >= 0 && column < static_cast<int>(columnWidthOverrides.size()) &&
            columnWidthOverrides[column] >= 0)
            return columnWidthOverrides[column];
        return model ? model->GetColumnDef(column).width : 0;
    }

    void UltraCanvasListView::SetColumnWidth(int column, int width) {
        if (column < 0) return;
        static constexpr int kMinColumnWidth = 24;
        if (width < kMinColumnWidth) width = kMinColumnWidth;
        if (static_cast<int>(columnWidthOverrides.size()) <= column)
            columnWidthOverrides.resize(column + 1, -1);
        if (columnWidthOverrides[column] == width) return;
        columnWidthOverrides[column] = width;
        InvalidateRowGeometry();
        UpdateScrollbar();
        RequestRedraw();
    }

    int UltraCanvasListView::ColumnBoundaryAt(int x, int y) const {
        if (!model || !viewStyle.showHeader || !columnsResizable) return -1;
        int colCount = model->GetColumnCount();
        if (colCount < 2) return -1;
        int localContentY = GetBorderTopWidth() + GetPaddingTop();
        if (y < localContentY || y >= localContentY + viewStyle.headerHeight) return -1;
        int colX = ColumnsStartX();
        for (int col = 0; col + 1 < colCount; ++col) {
            colX += GetColumnWidth(col);
            if (x >= colX - 4 && x <= colX + 4) return col;   // ~4px hot zone
        }
        return -1;
    }

    IListModel* UltraCanvasListView::GetModel() const {
        return model.get();
    }

    void UltraCanvasListView::SetDelegate(std::shared_ptr<IItemDelegate> newDelegate) {
        delegate = newDelegate ? newDelegate : std::make_shared<UltraCanvasDefaultListDelegate>();
        InvalidateRowGeometry();
        UpdateScrollbar();
        RequestRedraw();
    }

    IItemDelegate* UltraCanvasListView::GetDelegate() const {
        return delegate.get();
    }

    void UltraCanvasListView::SetSelection(std::shared_ptr<IListSelection> newSelection) {
        selection = newSelection;
        if (selection) {
            selection->onSelectionChanged = [this](const std::vector<int>& rows) {
                HandleSelectionChanged(rows);
            };
        }
        RequestRedraw();
    }

    void UltraCanvasListView::HandleSelectionChanged(const std::vector<int>& rows) {
        // The keyboard goes on from the selected row, whoever selected it: a
        // click, the arrow keys - or the application, after it rebuilt or
        // re-sorted the rows and selected the one being read. Before, a row
        // selected in code left the focus where it was (or at "none"), and
        // the next arrow key jumped from there instead of from the selection.
        // Set before the handlers run, so one that selects again wins.
        if (selection) {
            const int current = selection->GetCurrentRow();
            if (current >= 0) focusedRow = current;
        }
        if (onSelectionChanged) onSelectionChanged(rows);
        RequestRedraw();
    }

    IListSelection* UltraCanvasListView::GetSelection() const {
        return selection.get();
    }

    void UltraCanvasListView::ResetSelection() {
        focusedRow = -1;
        if (selection) selection->Clear();
        RequestRedraw();
    }

    // ===== VIEW CONFIGURATION =====
    void UltraCanvasListView::SetStyle(const ListViewStyle& style) {
        viewStyle = style;
        SetBackgroundColor(viewStyle.backgroundColor);
        if (verticalScrollbar) verticalScrollbar->SetStyle(viewStyle.scrollbarStyle);
        if (horizontalScrollbar) horizontalScrollbar->SetStyle(viewStyle.scrollbarStyle);
        InvalidateRowGeometry();
        UpdateScrollbar();
        RequestRedraw();
    }

    const ListViewStyle& UltraCanvasListView::GetStyle() const {
        return viewStyle;
    }

    void UltraCanvasListView::SetColumnFitting(const ColumnFitting& fitting) {
        columnFitting = fitting;
        columnFitStale = fitting.enabled;
        RequestRedraw();
    }

    void UltraCanvasListView::FitColumnsToContent(IRenderContext* ctx) {
        columnFitStale = false;
        if (!ctx || !model || model->GetColumnCount() <= 1 || userAdjustedColumns) return;
        const int colCount = model->GetColumnCount();
        for (int col = 0; col < colCount; ++col) {
            const int text = MeasureColumnTextWidth(ctx, col, columnFitting.font);
            int width = std::max(text > 0 ? text + columnFitting.padding : 0, MeasureHeaderWidth(ctx, col));
            width = std::max(width, columnFitting.minWidth);
            if (columnFitting.maxWidth > 0) width = std::min(width, columnFitting.maxWidth);
            SetColumnWidth(col, width);
        }
    }

    void UltraCanvasListView::SetRowHeight(int height) {
        viewStyle.rowHeight = height;
        InvalidateRowGeometry();
        UpdateScrollbar();
        RequestRedraw();
    }

    int UltraCanvasListView::GetRowHeight() const {
        return viewStyle.rowHeight;
    }

    void UltraCanvasListView::SetVariableRowHeights(bool enabled) {
        if (useVariableRowHeights == enabled) return;
        useVariableRowHeights = enabled;
        InvalidateRowGeometry();
        UpdateScrollbar();
        RequestRedraw();
    }

    bool UltraCanvasListView::GetVariableRowHeights() const {
        return useVariableRowHeights;
    }

    void UltraCanvasListView::InvalidateRowHeights() {
        InvalidateRowGeometry();
        UpdateScrollbar();
        RequestRedraw();
    }

    void UltraCanvasListView::SetShowHeader(bool show) {
        viewStyle.showHeader = show;
        UpdateScrollbar();
        RequestRedraw();
    }

    bool UltraCanvasListView::GetShowHeader() const {
        return viewStyle.showHeader;
    }

    void UltraCanvasListView::SetSortIndicator(int column, bool ascending) {
        if (column < 0) column = -1;
        if (sortColumn == column && sortAscending == ascending) return;
        sortColumn = column;
        sortAscending = ascending;
        RequestRedraw();
    }

    void UltraCanvasListView::SetShowItemTooltips(bool enable) {
        if (showItemTooltips == enable) return;
        showItemTooltips = enable;
        if (!showItemTooltips) HideHoverTooltip();
    }

    bool UltraCanvasListView::GetShowItemTooltips() const {
        return showItemTooltips;
    }

    // ===== SCROLLING =====

    void UltraCanvasListView::ScrollToRow(int row) {
        if (!model || row < 0 || row >= model->GetRowCount()) return;
        scrollOffsetY = RowTopOffset(row);
        ClampScrollOffset();
        RequestRedraw();
    }

    void UltraCanvasListView::EnsureRowVisible(int row) {
        if (!model || row < 0 || row >= model->GetRowCount()) return;

        auto viewport = GetViewportRect();
        // Not laid out yet: no room for the row is known. Measuring against a
        // zero (or, less the header, negative) viewport scrolled the row below
        // the top - row 0 by two rows, and the list then opened with its
        // first rows hidden. Done once the view has its size (Arrange).
        if (viewport.height <= 0) {
            pendingVisibleRow = row;
            return;
        }
        pendingVisibleRow = -1;
        int rowTop = RowTopOffset(row);
        int rowBottom = rowTop + RowHeightForRow(row);

        if (rowTop < scrollOffsetY) {
            scrollOffsetY = rowTop;
        } else if (rowBottom > scrollOffsetY + viewport.height) {
            scrollOffsetY = rowBottom - viewport.height;
        }
        ClampScrollOffset();
    }

    void UltraCanvasListView::ScrollToColumn(int column) {
        if (!model || column < 0 || column >= model->GetColumnCount() || model->GetColumnCount() <= 1) return;
        int left = 0;
        for (int col = 0; col < column; ++col) left += GetColumnWidth(col);
        scrollOffsetX = left;
        ClampScrollOffset();
        RequestRedraw();
    }

    void UltraCanvasListView::EnsureColumnVisible(int column) {
        if (!model || column < 0 || column >= model->GetColumnCount() || model->GetColumnCount() <= 1) return;
        const auto viewport = GetViewportRect();
        if (viewport.width <= 0) return;
        int left = 0;
        for (int col = 0; col < column; ++col) left += GetColumnWidth(col);
        const int right = left + GetColumnWidth(column);
        if (left < scrollOffsetX) {
            scrollOffsetX = left;
        } else if (right > scrollOffsetX + viewport.width) {
            // Just far enough to show the whole column - or, when the column
            // is wider than the view, its left edge.
            scrollOffsetX = std::min(left, right - viewport.width);
        } else {
            return;
        }
        ClampScrollOffset();
        RequestRedraw();
    }

    void UltraCanvasListView::ScrollHorizontallyBy(int delta) {
        if (maxScrollX <= 0) return;
        scrollOffsetX += delta;
        ClampScrollOffset();
        RequestRedraw();
    }

    // ===== SCROLLBAR INTERNALS =====

    void UltraCanvasListView::CreateScrollbar() {
        verticalScrollbar = std::make_shared<UltraCanvasScrollbar>(
            GetIdentifier() + "_vscroll", 0, 0,
            viewStyle.scrollbarStyle.trackSize, 100,
            ScrollbarOrientation::Vertical);
        verticalScrollbar->onScrollChange = [this](int pos) { HandleScrollbarMoved(scrollOffsetY, pos); };
        verticalScrollbar->SetStyle(viewStyle.scrollbarStyle);
        verticalScrollbar->SetVisible(false);

        horizontalScrollbar = std::make_shared<UltraCanvasScrollbar>(
            GetIdentifier() + "_hscroll", 0, 0,
            100, viewStyle.scrollbarStyle.trackSize,
            ScrollbarOrientation::Horizontal);
        horizontalScrollbar->onScrollChange = [this](int pos) { HandleScrollbarMoved(scrollOffsetX, pos); };
        horizontalScrollbar->SetStyle(viewStyle.scrollbarStyle);
        horizontalScrollbar->SetVisible(false);
    }

    void UltraCanvasListView::HandleScrollbarMoved(int& offset, int position) {
        if (position != offset && pointerOver) {
            // The rows (or the columns) moved under a still pointer: the
            // hover - and whatever a host hangs on onCellHovered, such as a
            // preview of the row's thumbnail - follows the cell now under it.
            offset = position;
            UpdateHover(lastPointer, lastPointerWindow, false);
            hoverTooltipStale = true;
        }
        offset = position;
        RequestRedraw();
    }

    UltraCanvasListView::ScrollLayout UltraCanvasListView::ComputeScrollLayout() const {
        ScrollLayout layout;
        const int rows = model ? model->GetRowCount() : 0;
        const int headerOff = GetHeaderOffset();
        const int crWidth = GetWidth() - GetTotalBorderHorizontal() - GetTotalPaddingHorizontal();
        const int crHeight = GetHeight() - GetTotalBorderVertical() - GetTotalPaddingVertical();
        const int rowsHeight = rows > 0 ? RowsContentHeight() : 0;
        const int columnsWidth = ColumnsContentWidth();
        const int vTrack = verticalScrollbar->GetStyle().trackSize;
        const int hTrack = horizontalScrollbar->GetStyle().trackSize;

        // Each scrollbar takes a track off the other's viewport, so one
        // that is needed only because the other shows is decided second.
        layout.viewportWidth = crWidth;
        layout.viewportHeight = crHeight - headerOff;
        layout.showVertical = rowsHeight > layout.viewportHeight;
        layout.showHorizontal = columnsWidth > layout.viewportWidth;
        if (layout.showVertical && !layout.showHorizontal) {
            layout.showHorizontal = columnsWidth > layout.viewportWidth - vTrack;
        } else if (layout.showHorizontal && !layout.showVertical) {
            layout.showVertical = rowsHeight > layout.viewportHeight - hTrack;
        }
        if (layout.showVertical) layout.viewportWidth -= vTrack;
        if (layout.showHorizontal) layout.viewportHeight -= hTrack;
        layout.maxY = std::max(0, rowsHeight - layout.viewportHeight);
        layout.maxX = std::max(0, columnsWidth - layout.viewportWidth);

        // Scrollbar bounds in element-local space (within the padding box):
        // the vertical one beside the rows, below the header; the horizontal
        // one along the bottom; each stops short of the other's track.
        const int localPaddingX = GetBorderLeftWidth();
        const int localPaddingY = GetBorderTopWidth();
        const int paddingW = GetWidth() - GetTotalBorderHorizontal();
        const int paddingH = GetHeight() - GetTotalBorderVertical();
        if (layout.showVertical) {
            layout.verticalBounds = Rect2Di(localPaddingX + paddingW - vTrack, localPaddingY + headerOff, vTrack,
                                            paddingH - headerOff - (layout.showHorizontal ? hTrack : 0));
        }
        if (layout.showHorizontal) {
            layout.horizontalBounds = Rect2Di(localPaddingX, localPaddingY + paddingH - hTrack,
                                              paddingW - (layout.showVertical ? vTrack : 0), hTrack);
        }
        return layout;
    }

    void UltraCanvasListView::UpdateScrollbar() {
        const ScrollLayout layout = ComputeScrollLayout();
        maxScrollY = layout.maxY;
        maxScrollX = layout.maxX;

        if (!layout.showVertical) {
            HideScrollbar(*verticalScrollbar);
        } else {
            verticalScrollbar->SetVisible(true);
            verticalScrollbar->SetBounds(Rect2Df(layout.verticalBounds));
            verticalScrollbar->SetViewportSize(layout.viewportHeight);
            verticalScrollbar->SetContentSize(RowsContentHeight());
        }
        if (!layout.showHorizontal) {
            HideScrollbar(*horizontalScrollbar);
        } else {
            horizontalScrollbar->SetVisible(true);
            horizontalScrollbar->SetBounds(Rect2Df(layout.horizontalBounds));
            horizontalScrollbar->SetViewportSize(layout.viewportWidth);
            horizontalScrollbar->SetContentSize(ColumnsContentWidth());
        }
        ClampScrollOffset();
    }

    // A hidden scrollbar keeps no bounds: the last visible ones were computed
    // for an earlier size (a list arranged at its parent's full width before
    // the split pane sized it), and a rectangle outside the list must never
    // survive to be painted or hit-tested once the bar shows again without a
    // fresh UpdateScrollbar.
    void UltraCanvasListView::HideScrollbar(UltraCanvasScrollbar& scrollbar) {
        scrollbar.SetVisible(false);
        scrollbar.SetBounds(Rect2Df(0, 0, 0, 0));
    }

    void UltraCanvasListView::SyncScrollbarBeforePaint() {
        const ScrollLayout layout = ComputeScrollLayout();
        const Rect2Di vertical(verticalScrollbar->GetBounds());
        const Rect2Di horizontal(horizontalScrollbar->GetBounds());
        const bool stale = layout.maxY != maxScrollY || layout.maxX != maxScrollX ||
                           layout.showVertical != verticalScrollbar->IsVisible() ||
                           layout.showHorizontal != horizontalScrollbar->IsVisible() ||
                           (layout.showVertical && vertical != layout.verticalBounds) ||
                           (layout.showHorizontal && horizontal != layout.horizontalBounds);
        if (!stale) return;
        debugOutput << "UltraCanvasListView " << GetIdentifier() << ": scrollbar was stale at paint (rows "
                    << (model ? model->GetRowCount() : 0) << ", content " << RowsContentHeight() << "x"
                    << ColumnsContentWidth() << ", viewport " << layout.viewportWidth << "x" << layout.viewportHeight
                    << ", range " << maxScrollY << " -> " << layout.maxY << " / " << maxScrollX << " -> "
                    << layout.maxX << ", shown " << verticalScrollbar->IsVisible() << " -> " << layout.showVertical
                    << " / " << horizontalScrollbar->IsVisible() << " -> " << layout.showHorizontal << ")"
                    << std::endl;
        UpdateScrollbar();
    }

    UltraCanvasListView::ScrollMetrics UltraCanvasListView::GetScrollMetrics() const {
        const ScrollLayout layout = ComputeScrollLayout();
        ScrollMetrics m;
        m.rows = model ? model->GetRowCount() : 0;
        m.rowHeight = (useVariableRowHeights && delegate) ? 0 : viewStyle.rowHeight;
        m.contentHeight = RowsContentHeight();
        m.viewportHeight = layout.viewportHeight;
        m.maxScroll = layout.maxY;
        m.scrollOffset = scrollOffsetY;
        m.scrollbarVisible = verticalScrollbar->IsVisible();
        m.scrollbarBounds = Rect2Di(verticalScrollbar->GetBounds());
        m.contentWidth = ColumnsContentWidth();
        m.viewportWidth = layout.viewportWidth;
        m.maxScrollX = layout.maxX;
        m.scrollOffsetX = scrollOffsetX;
        m.horizontalScrollbarVisible = horizontalScrollbar->IsVisible();
        m.horizontalScrollbarBounds = Rect2Di(horizontalScrollbar->GetBounds());
        m.width = GetWidth();
        m.height = GetHeight();
        return m;
    }

    void UltraCanvasListView::ClampScrollOffset() {
        scrollOffsetY = std::max(0, std::min(scrollOffsetY, maxScrollY));
        if (verticalScrollbar->IsVisible()) {
            verticalScrollbar->SetScrollPosition(scrollOffsetY);
        }
        scrollOffsetX = std::max(0, std::min(scrollOffsetX, maxScrollX));
        if (horizontalScrollbar->IsVisible()) {
            horizontalScrollbar->SetScrollPosition(scrollOffsetX);
        }
    }

    // ===== GEOMETRY =====

    int UltraCanvasListView::GetTotalContentHeight() const {
        if (!model) return 0;
        return RowsContentHeight() + GetHeaderOffset();
    }

    // ===== PER-ROW GEOMETRY =====

    void UltraCanvasListView::InvalidateRowGeometry() {
        rowGeometryValid = false;
    }

    void UltraCanvasListView::RebuildRowGeometryIfNeeded() const {
        if (rowGeometryValid) return;
        rowGeometryValid = true;
        rowTops.clear();
        // The prefix-sum table is only needed for variable rows; uniform rows
        // derive every offset arithmetically, so skip the allocation entirely.
        if (!(useVariableRowHeights && delegate)) return;

        int n = model ? model->GetRowCount() : 0;
        rowTops.resize(static_cast<size_t>(n) + 1);
        int y = 0;
        for (int i = 0; i < n; ++i) {
            rowTops[i] = y;
            int h = delegate->GetRowHeight(model.get(), i);
            if (h < 1) h = 1;
            y += h;
        }
        rowTops[n] = y;
    }

    int UltraCanvasListView::RowHeightForRow(int row) const {
        if (useVariableRowHeights && delegate) {
            RebuildRowGeometryIfNeeded();
            int n = model ? model->GetRowCount() : 0;
            if (row < 0 || row >= n) return viewStyle.rowHeight;
            return rowTops[row + 1] - rowTops[row];
        }
        return viewStyle.rowHeight;
    }

    int UltraCanvasListView::RowTopOffset(int row) const {
        if (useVariableRowHeights && delegate) {
            RebuildRowGeometryIfNeeded();
            int n = model ? model->GetRowCount() : 0;
            if (row < 0) return 0;
            if (row > n) row = n;
            return rowTops[row];
        }
        if (row < 0) return 0;
        return row * viewStyle.rowHeight;
    }

    int UltraCanvasListView::RowsContentHeight() const {
        if (!model) return 0;
        if (useVariableRowHeights && delegate) {
            RebuildRowGeometryIfNeeded();
            return rowTops.empty() ? 0 : rowTops[model->GetRowCount()];
        }
        return model->GetRowCount() * viewStyle.rowHeight;
    }

    int UltraCanvasListView::ClampRowIndexAtContentY(int contentY) const {
        int n = model ? model->GetRowCount() : 0;
        if (n <= 0) return 0;
        if (contentY < 0) contentY = 0;
        if (useVariableRowHeights && delegate) {
            RebuildRowGeometryIfNeeded();
            // Largest row index with rowTops[row] <= contentY (search the row
            // tops only, excluding the trailing content-height sentinel).
            auto begin = rowTops.begin();
            auto it = std::upper_bound(begin, begin + n, contentY);
            int row = static_cast<int>(it - begin) - 1;
            if (row < 0) row = 0;
            if (row >= n) row = n - 1;
            return row;
        }
        int row = contentY / std::max(1, viewStyle.rowHeight);
        if (row >= n) row = n - 1;
        return row;
    }

    int UltraCanvasListView::GetHeaderOffset() const {
        if (viewStyle.showHeader)
            return viewStyle.headerHeight;
        return 0;
    }

    int UltraCanvasListView::ColumnsContentWidth() const {
        if (!model) return 0;
        const int colCount = model->GetColumnCount();
        if (colCount <= 1) return 0;
        int width = 0;
        for (int col = 0; col < colCount; ++col) width += GetColumnWidth(col);
        return width;
    }

    int UltraCanvasListView::ColumnsStartX() const {
        return GetBorderLeftWidth() + GetPaddingLeft() - scrollOffsetX;
    }

    Rect2Di UltraCanvasListView::GetViewportRect() const {
        // Returns element-local coordinates (ctx is translated to element origin)
        int localContentX = GetBorderLeftWidth() + GetPaddingLeft();
        int localContentY = GetBorderTopWidth() + GetPaddingTop();
        int crWidth = GetWidth() - GetTotalBorderHorizontal() - GetTotalPaddingHorizontal();
        int crHeight = GetHeight() - GetTotalBorderVertical() - GetTotalPaddingVertical();
        int sbWidth = verticalScrollbar->IsVisible() ? verticalScrollbar->GetStyle().trackSize : 0;
        int sbHeight = horizontalScrollbar->IsVisible() ? horizontalScrollbar->GetStyle().trackSize : 0;
        int headerOff = GetHeaderOffset();
        return Rect2Di(localContentX, localContentY + headerOff, crWidth - sbWidth, crHeight - headerOff - sbHeight);
    }

    int UltraCanvasListView::GetColumnAt(int x, int* columnStartX) const {
        if (!model) return -1;
        auto viewport = GetViewportRect();
        if (x < viewport.x || x >= viewport.x + viewport.width) return -1;

        int colCount = model->GetColumnCount();
        if (colCount <= 1) {
            if (columnStartX) *columnStartX = viewport.x;
            return 0;
        }
        int colX = ColumnsStartX();
        for (int col = 0; col < colCount; col++) {
            int colW = GetColumnWidth(col);
            if (x < colX + colW) {
                if (columnStartX) *columnStartX = colX;
                return col;
            }
            colX += colW;
        }
        return -1;
    }

    Rect2Di UltraCanvasListView::GetCellRect(int row, int column) const {
        if (!model || row < 0 || row >= model->GetRowCount() || column < 0) return Rect2Di();
        const int colCount = model->GetColumnCount();
        if (column >= std::max(colCount, 1)) return Rect2Di();
        const Rect2Di rowRect = GetRowRect(row);
        if (colCount <= 1) return rowRect;
        int colX = ColumnsStartX();
        for (int col = 0; col < column; ++col) colX += GetColumnWidth(col);
        return Rect2Di(colX, rowRect.y, GetColumnWidth(column), rowRect.height);
    }

    int UltraCanvasListView::GetHeaderColumnAt(int x, int y, int* columnStartX) const {
        if (!model || !viewStyle.showHeader) return -1;

        // The header sits at the top of the content rect and spans its full
        // width (see RenderHeader), above the rows viewport.
        int localContentX = GetBorderLeftWidth() + GetPaddingLeft();
        int localContentY = GetBorderTopWidth() + GetPaddingTop();
        int crWidth = GetWidth() - GetTotalBorderHorizontal() - GetTotalPaddingHorizontal();
        if (y < localContentY || y >= localContentY + viewStyle.headerHeight) return -1;
        if (x < localContentX || x >= localContentX + crWidth) return -1;

        int colX = ColumnsStartX();
        int colCount = model->GetColumnCount();
        for (int col = 0; col < colCount; col++) {
            int colW = GetColumnWidth(col);
            if (x < colX + colW) {
                if (columnStartX) *columnStartX = colX;
                return col;
            }
            colX += colW;
        }
        return -1;
    }

    std::string UltraCanvasListView::GetTooltipTextAt(int row, int column) const {
        if (!model) return {};
        if (row < -1 || row >= model->GetRowCount()) return {};
        if (column < 0) column = 0;

        if (tooltipProvider) {
            std::string custom = tooltipProvider(row, column);
            if (!custom.empty()) return custom;
        }
        if (row < 0) {
            // Header cell
            return model->GetColumnDef(column).tooltip;
        }
        return GetStringValue(model->GetData(ListIndex{row, column}, ListDataRole::ToolTipRole));
    }

    int UltraCanvasListView::GetRowAtY(int y) const {
        if (!model) return -1;
        auto viewport = GetViewportRect();
        // Above the rows viewport (the header, when shown) is never a row --
        // without this a scrolled list would report one for header hits.
        if (y < viewport.y) return -1;
        int relativeY = y - viewport.y + scrollOffsetY;
        if (relativeY < 0 || relativeY >= RowsContentHeight()) return -1;
        int row = ClampRowIndexAtContentY(relativeY);
        if (row < 0 || row >= model->GetRowCount()) return -1;
        return row;
    }

    Rect2Di UltraCanvasListView::GetRowRect(int row) const {
        auto viewport = GetViewportRect();
        int rowY = viewport.y + RowTopOffset(row) - scrollOffsetY;
        return Rect2Di(viewport.x, rowY, viewport.width, RowHeightForRow(row));
    }

    // ===== RENDERING =====

    void UltraCanvasListView::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        // Draw background and border
        UltraCanvasUIElement::Render(ctx, dirtyRect);

        // The columns' widths follow their content, where asked: measured
        // here, on the context they are painted with, once per change.
        if (columnFitting.enabled && columnFitStale) FitColumnsToContent(ctx);

        // The scrollbar is recomputed whenever the model, the style or the
        // bounds change - but only if that change reached this view. A paint
        // is the one moment the rows, the bounds and the scrollbar are all
        // read together, so it is where a stale state is cheapest to catch:
        // recompute the range from what is about to be painted and, when the
        // scrollbar disagrees, bring it up to date before drawing.
        SyncScrollbarBeforePaint();

        // Element-local content rect (ctx is translated to element origin)
        int localContentX = GetBorderLeftWidth() + GetPaddingLeft();
        int localContentY = GetBorderTopWidth() + GetPaddingTop();
        int crWidth = GetWidth() - GetTotalBorderHorizontal() - GetTotalPaddingHorizontal();
        int crHeight = GetHeight() - GetTotalBorderVertical() - GetTotalPaddingVertical();
        Rect2Di contentRect(localContentX, localContentY, crWidth, crHeight);

        // Clip to content area
        ctx->ClipRect(contentRect);

        // Draw column header (multi-column mode only)
        if (viewStyle.showHeader && model) {
            RenderHeader(ctx, contentRect);
        }

        // Draw visible rows
        RenderRows(ctx, contentRect);

        // Draw the scrollbars on top (bounds are stored in element-local
        // space; translate ctx so a scrollbar can draw from (0,0))
        for (UltraCanvasScrollbar* scrollbar : { verticalScrollbar.get(), horizontalScrollbar.get() }) {
            if (!scrollbar->IsVisible()) continue;
            ctx->PushState();
            auto sbB = scrollbar->GetBounds();
            ctx->Translate(Point2Di(sbB.x, sbB.y));
            scrollbar->Render(ctx, dirtyRect);
            ctx->PopState();
        }
        // The square where the two tracks would meet, in the track's colour.
        if (verticalScrollbar->IsVisible() && horizontalScrollbar->IsVisible()) {
            const Rect2Df v = verticalScrollbar->GetBounds();
            const Rect2Df h = horizontalScrollbar->GetBounds();
            ctx->SetFillPaint(viewStyle.scrollbarStyle.trackColor);
            ctx->FillRectangle(Rect2Dd(v.x, h.y, v.width, h.height));
        }
    }

    void UltraCanvasListView::RenderHeader(IRenderContext* ctx, const Rect2Di& contentRect) {
        if (!model) return;

        // The header spans the full content width. The scrollbar occupies only
        // the rows area below the header (see UpdateScrollbar), so reserving
        // scrollbar width here would leave an unpainted white box in the
        // top-right corner between the header and the scrollbar track.
        Rect2Di headerRect(contentRect.x, contentRect.y, contentRect.width, viewStyle.headerHeight);

        // Header background
        ctx->SetFillPaint(viewStyle.headerBackgroundColor);
        ctx->FillRectangle(headerRect);

        ctx->SetFontSize(viewStyle.headerFontSize);
        ctx->SetTextPaint(viewStyle.headerTextColor);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->SetTextWrap(TextWrap::WrapNone);

        // The header cells scroll sideways with the columns under them.
        int colX = headerRect.x - scrollOffsetX;
        int colCount = model->GetColumnCount();
        for (int col = 0; col < colCount; col++) {
            auto colDef = model->GetColumnDef(col);
            int colW = GetColumnWidth(col);
            if (colX >= headerRect.Right()) break;
            if (colX + colW <= headerRect.x) { colX += colW; continue; }
            ctx->SetTextAlignment(colDef.alignment);
            ctx->SetTextPaint(viewStyle.headerTextColor);

            // The sorted column gives up a strip at the end of its cell to the
            // direction triangle - after the title for left/centre-aligned
            // columns, before it for right-aligned ones, so the title keeps
            // its edge and the two never overlap.
            Rect2Dd titleRect(colX + kHeaderInset, headerRect.y, colW - 2 * kHeaderInset, headerRect.height);
            if (col == sortColumn && viewStyle.sortIndicatorSize > 0) {
                int strip = viewStyle.sortIndicatorSize + kSortStripGap;
                if (strip < colW - 2 * kHeaderInset) {
                    Rect2Di stripRect(colX + colW - kHeaderInset - strip, headerRect.y, strip,
                                      headerRect.height);
                    if (colDef.alignment == TextAlignment::Right) {
                        stripRect.x = colX + kHeaderInset;
                        titleRect.x += strip;
                    }
                    titleRect.width -= strip;
                    RenderSortIndicator(ctx, stripRect);
                }
            }
            ctx->DrawTextInRect(colDef.title, titleRect);

            // Separator between columns: the resize boundary. Drawn for every
            // interior border (not only when showGridLines) so the draggable
            // edge is discoverable in the header.
            if (col < colCount - 1) {
                ctx->SetStrokePaint(viewStyle.gridLineColor);
                ctx->DrawLine({colX + colW, headerRect.y}, {colX + colW, headerRect.Bottom()});
            }

            colX += colW;
        }

        // Bottom border of header
        ctx->SetStrokePaint(viewStyle.gridLineColor);
        ctx->DrawLine(headerRect.BottomLeft(), headerRect.BottomRight());
    }

    int UltraCanvasListView::MeasureHeaderWidth(IRenderContext* ctx, int column) const {
        if (!ctx || !model || column < 0 || column >= model->GetColumnCount()) return 0;
        FontStyle font;
        font.fontSize = viewStyle.headerFontSize;
        const int title = TextLineWidth(ctx, model->GetColumnDef(column).title, font);
        const int strip = viewStyle.sortIndicatorSize > 0 ? viewStyle.sortIndicatorSize + kSortStripGap : 0;
        return 2 * kHeaderInset + title + strip;
    }

    int UltraCanvasListView::MeasureColumnTextWidth(IRenderContext* ctx, int column,
                                                    const FontStyle& font) const {
        if (!ctx || !model || column < 0 || column >= model->GetColumnCount()) return 0;
        // The font as a key: the remembered widths hold for it alone.
        const std::string fontKey = font.fontFamily + "|" +
            std::to_string(std::lround(font.fontSize * 100.0)) + "|" +
            std::to_string(static_cast<int>(font.fontWeight)) + "|" +
            std::to_string(static_cast<int>(font.fontSlant));
        if (fontKey != textWidthFont) {
            textWidths.clear();
            textWidthFont = fontKey;
        }
        // A grid paged through a large table sees a new text in every cell
        // of every page: the memory starts over rather than growing with it.
        constexpr size_t kRememberedTexts = 20000;
        if (textWidths.size() > kRememberedTexts) textWidths.clear();
        int widest = 0;
        const int rows = model->GetRowCount();
        for (int row = 0; row < rows; ++row) {
            const std::string text =
                GetStringValue(model->GetData(ListIndex{row, column}, ListDataRole::DisplayRole));
            if (text.empty()) continue;
            auto it = textWidths.find(text);
            if (it == textWidths.end()) it = textWidths.emplace(text, TextLineWidth(ctx, text, font)).first;
            widest = std::max(widest, it->second);
        }
        return widest;
    }

    // The sort direction as geometry rather than a text glyph: a filled
    // triangle stays crisp at any DPI and takes headerTextColor, whereas a
    // font's U+25B2/U+25BC would depend on the header font carrying them. Apex
    // up means ascending, apex down descending, centred in `cell`.
    void UltraCanvasListView::RenderSortIndicator(IRenderContext* ctx, const Rect2Di& cell) {
        const double w = viewStyle.sortIndicatorSize;
        const double h = w * 0.5;
        const double cx = cell.x + cell.width * 0.5;
        const double cy = cell.y + cell.height * 0.5;
        const double left = cx - w * 0.5, right = cx + w * 0.5;
        const double top = cy - h * 0.5, bottom = cy + h * 0.5;

        std::vector<Point2Dd> triangle;
        if (sortAscending) {
            triangle = { Point2Dd(cx, top), Point2Dd(right, bottom), Point2Dd(left, bottom) };
        } else {
            triangle = { Point2Dd(left, top), Point2Dd(right, top), Point2Dd(cx, bottom) };
        }
        ctx->SetFillPaint(viewStyle.headerTextColor);
        ctx->FillLinePath(triangle);
    }

    void UltraCanvasListView::RenderRows(IRenderContext* ctx, const Rect2Di& contentRect) {
        if (!model || !delegate) return;

        auto viewport = GetViewportRect();
        int rowCount = model->GetRowCount();
        int colCount = model->GetColumnCount();

        // Clip rows to the viewport (the area below the header). Rows are drawn
        // after the header, and a scrolled row's top slides up into the header
        // band; without this clip that content would paint over the header,
        // making the title row look transparent behind the table content.
        ctx->PushState();
        ctx->ClipRect(Rect2Dd(viewport.x, viewport.y, viewport.width, viewport.height));

        // Calculate the first visible row (culling). With variable heights this
        // is a search over the row-top table; uniform rows divide. The loop then
        // walks forward accumulating each row's own height and stops once a row
        // starts below the viewport.
        int firstVisible = std::max(0, ClampRowIndexAtContentY(scrollOffsetY));

        for (int row = firstVisible; row < rowCount; row++) {
            int rowH = RowHeightForRow(row);
            int rowY = viewport.y + RowTopOffset(row) - scrollOffsetY;

            // Stop once the row starts past the viewport bottom; skip the rare
            // row entirely above it (can only precede the first visible one).
            if (rowY > viewport.Bottom()) break;
            if (rowY + rowH < viewport.y) continue;

            // Alternate row background
            if (viewStyle.alternateRowColors && row % 2 == 1) {
                ctx->SetFillPaint(viewStyle.alternateRowColor);
                ctx->FillRectangle(Rect2Dd(viewport.x, rowY, viewport.width, rowH));
            }

            bool isSelected = selection && selection->IsSelected(row);
            bool isHovered = (row == hoveredRow);
            bool isFocused = (row == focusedRow) && IsFocused();

            // Draw full-row selection/hover background (before any column clipping)
            if (isSelected) {
                ctx->SetFillPaint(viewStyle.selectionBackgroundColor);
                ctx->FillRectangle(Rect2Dd(viewport.x, rowY, viewport.width, rowH));
            } else if (isHovered) {
                ctx->SetFillPaint(viewStyle.hoverBackgroundColor);
                ctx->FillRectangle(Rect2Dd(viewport.x, rowY, viewport.width, rowH));
            }

            if (colCount <= 1) {
                // Single-column mode: delegate renders entire row
                ListItemStyleOption opt;
                opt.rect = Rect2Di(viewport.x, rowY, viewport.width, rowH);
                opt.isSelected = isSelected;
                opt.isHovered = isHovered;
                opt.isFocused = isFocused;
                opt.isDisabled = IsDisabled();
                opt.row = row;
                opt.column = 0;
                opt.columnCount = 1;
                opt.columnX = viewport.x;
                opt.columnWidth = viewport.width;

                delegate->RenderItem(ctx, model.get(), row, 0, opt);
            } else {
                // Multi-column mode: iterate the columns, scrolled sideways,
                // painting only those that reach into the viewport.
                int colX = viewport.x - scrollOffsetX;
                for (int col = 0; col < colCount; col++) {
                    auto colDef = model->GetColumnDef(col);
                    int colW = GetColumnWidth(col);
                    if (colX >= viewport.Right()) break;
                    if (colX + colW <= viewport.x) { colX += colW; continue; }

                    ListItemStyleOption opt;
                    opt.rect = Rect2Di(viewport.x, rowY, viewport.width, rowH);
                    opt.isSelected = isSelected;
                    opt.isHovered = isHovered;
                    opt.isFocused = isFocused;
                    opt.isDisabled = IsDisabled();
                    opt.row = row;
                    opt.column = col;
                    opt.columnCount = colCount;
                    opt.columnX = colX;
                    opt.columnWidth = colW;
                    opt.columnAlignment = colDef.alignment;

                    ctx->PushState();
                    ctx->ClipRect({colX, rowY, colW, rowH});
                    delegate->RenderItem(ctx, model.get(), row, col, opt);
                    ctx->PopState();

                    // Grid line between columns
                    if (viewStyle.showGridLines && col < colCount - 1) {
                        ctx->SetStrokePaint(viewStyle.gridLineColor);
                        ctx->DrawLine({colX + colW, rowY},
                                      {colX + colW, rowY + rowH});
                    }

                    colX += colW;
                }
            }
        }

        ctx->PopState();
    }

    // ===== EVENT HANDLING =====

    bool UltraCanvasListView::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        // The scrollbars get mouse events first
        if (DispatchToScrollbar(*verticalScrollbar, event) || DispatchToScrollbar(*horizontalScrollbar, event)) {
            return true;
        }

        // Interactive column resize (multi-column header). Handled before row
        // hit-testing so grabbing a border never selects a row. A drag keeps the
        // mouse captured, so moves/ups arrive even outside the element.
        if (columnsResizable) {
            switch (event.type) {
                case UCEventType::MouseDown: {
                    int col = Contains(event.pointer)
                                  ? ColumnBoundaryAt(event.pointer.x, event.pointer.y) : -1;
                    if (col >= 0) {
                        resizeCol = col;
                        resizeStartX = event.pointer.x;
                        resizeStartW = GetColumnWidth(col);
                        if (auto* app = UltraCanvasApplication::GetInstance()) app->CaptureMouse(this);
                        return true;
                    }
                    break;
                }
                case UCEventType::MouseMove:
                    if (resizeCol >= 0) {
                        SetColumnWidth(resizeCol, resizeStartW + (event.pointer.x - resizeStartX));
                        userAdjustedColumns = true;
                        return true;
                    }
                    // Hover affordance over a resizable border.
                    SetMouseCursor(
                        (Contains(event.pointer) &&
                         ColumnBoundaryAt(event.pointer.x, event.pointer.y) >= 0)
                            ? UCMouseCursor::SizeWE : UCMouseCursor::Default);
                    break;
                case UCEventType::MouseUp:
                    if (resizeCol >= 0) {
                        resizeCol = -1;
                        if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
                        return true;
                    }
                    break;
                default:
                    break;
            }
        }

        // Header click (press and release in the same header cell). Handled
        // before row hit-testing: a press on the header is not a click on "no
        // row" and must not clear the selection. A press on a resize border
        // was consumed above, so a drag never reads as a click.
        if (viewStyle.showHeader) {
            switch (event.type) {
                case UCEventType::MouseDown:
                    if (Contains(event.pointer)) {
                        int col = GetHeaderColumnAt(event.pointer.x, event.pointer.y);
                        if (col >= 0) {
                            pressedHeaderColumn = col;
                            SetFocus(true);
                            return true;
                        }
                    }
                    break;
                case UCEventType::MouseUp:
                    if (pressedHeaderColumn >= 0) {
                        int col = Contains(event.pointer)
                                      ? GetHeaderColumnAt(event.pointer.x, event.pointer.y) : -1;
                        bool clicked = (col == pressedHeaderColumn);
                        pressedHeaderColumn = -1;
                        if (clicked && onHeaderClicked) onHeaderClicked(col);
                        return true;
                    }
                    break;
                default:
                    break;
            }
        }

        // A right press asks for a context menu, when a handler wants one:
        // select what is under the pointer, then hand over. Before the row
        // handler, which would treat the press as a left click.
        if (onContextMenu && event.type == UCEventType::MouseDown &&
            event.button == UCMouseButton::Right && Contains(event.pointer)) {
            SetFocus(true);
            const int row = GetRowAtY(event.pointer.y);
            if (row >= 0) {
                focusedRow = row;
                if (selection && !selection->IsSelected(row)) selection->Select(row, false);
                RequestRedraw();
            }
            onContextMenu(row, event);
            return true;
        }

        switch (event.type) {
            case UCEventType::MouseDown:
                return HandleMouseDown(event);
            case UCEventType::MouseMove:
                return HandleMouseMove(event);
            case UCEventType::MouseLeave:
                pointerOver = false;
                hoverTooltipStale = false;
                if (onCellHovered) onCellHovered(-1, -1, Point2Di(-1, -1));
                HideHoverTooltip();
                pressedHeaderColumn = -1;
                hoveredColumn = -1;
                hoveredHeaderColumn = -1;
                if (hoveredRow != -1) {
                    hoveredRow = -1;
                    RequestRedraw();
                }
                return false;
            case UCEventType::MouseUp:
                return HandleMouseUp(event);
            case UCEventType::MouseDoubleClick:
                return HandleMouseDoubleClick(event);
            case UCEventType::MouseWheel:
                return HandleMouseWheel(event);
            case UCEventType::KeyDown:
                return HandleKeyDown(event);
            default:
                break;
        }
        return false;
    }

    bool UltraCanvasListView::DispatchToScrollbar(UltraCanvasScrollbar& scrollbar, const UCEvent& event) {
        if (!scrollbar.IsVisible() || !event.IsMouseEvent()) return false;
        const Rect2Df sbB = scrollbar.GetBounds();
        if (!sbB.Contains(event.pointer) && !scrollbar.IsDragging()) return false;
        // Translate the pointer from listview-local to scrollbar-local.
        UCEvent localEvent = event;
        localEvent.pointer = event.pointer - sbB.TopLeft();
        if (!scrollbar.OnEvent(localEvent)) return false;
        if (event.type == UCEventType::MouseMove) {
            // The pointer is on the scrollbar, not on a cell, and a drag
            // moves the rows (or the columns) under it.
            HideHoverTooltip();
            hoveredColumn = -1;
            hoveredHeaderColumn = -1;
        }
        return true;
    }

    bool UltraCanvasListView::HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.pointer)) return false;

        SetFocus(true);

        int row = GetRowAtY(event.pointer.y);
        if (row < 0) {
            if (selection) selection->Clear();
            focusedRow = -1;
            RequestRedraw();
            return true;
        }

        focusedRow = row;

        if (selection) {
            auto* multiSel = dynamic_cast<UltraCanvasMultiSelection*>(selection.get());
            if (event.shift && multiSel) {
                int anchor = selection->GetCurrentRow();
                if (anchor >= 0) {
                    selection->SelectRange(anchor, row);
                } else {
                    selection->Select(row, false);
                }
            } else if (event.ctrl && multiSel) {
                selection->Select(row, true);
            } else {
                selection->Select(row, false);
            }
        }

        if (onItemClicked) onItemClicked(row);
        if (onCellClicked) {
            int colX = 0;
            int col = GetColumnAt(event.pointer.x, &colX);
            if (col >= 0) {
                auto rowRect = GetRowRect(row);
                onCellClicked(row, col, Point2Di(event.pointer.x - colX,
                                                 event.pointer.y - rowRect.y));
            }
        }
        RequestRedraw();
        return true;
    }

    bool UltraCanvasListView::HandleMouseMove(const UCEvent& event) {
        lastPointer = event.pointer;
        lastPointerWindow = event.pointerWindow;
        pointerOver = Contains(event.pointer);
        return UpdateHover(event.pointer, event.pointerWindow, true);
    }

    bool UltraCanvasListView::UpdateHover(const Point2Di& pointer, const Point2Di& pointerWindow, bool tooltip) {
        bool inside = Contains(pointer);
        int newHovered = inside ? GetRowAtY(pointer.y) : -1;

        int colX = 0;
        int newColumn = (newHovered >= 0) ? GetColumnAt(pointer.x, &colX) : -1;
        int newHeaderColumn = (inside && newHovered < 0)
                                  ? GetHeaderColumnAt(pointer.x, pointer.y)
                                  : -1;

        if (onCellHovered) {
            if (newHovered >= 0 && newColumn >= 0) {
                auto rowRect = GetRowRect(newHovered);
                onCellHovered(newHovered, newColumn, Point2Di(pointer.x - colX, pointer.y - rowRect.y));
            } else {
                onCellHovered(-1, -1, Point2Di(-1, -1));
            }
        }

        // A tooltip belongs to one cell (or one header cell), so it is refreshed
        // whenever the hovered cell changes -- not only on a row change. After
        // a scroll under the pointer the cell changed without a move; the next
        // move refreshes it wherever it lands.
        if (tooltip && (hoverTooltipStale || newHovered != hoveredRow || newColumn != hoveredColumn ||
                        newHeaderColumn != hoveredHeaderColumn)) {
            UpdateHoverTooltip(pointerWindow, newHovered, newColumn, newHeaderColumn);
        }
        if (tooltip) hoverTooltipStale = false;
        hoveredColumn = newColumn;
        hoveredHeaderColumn = newHeaderColumn;

        if (newHovered != hoveredRow) {
            hoveredRow = newHovered;
            if (onItemHovered && hoveredRow >= 0) onItemHovered(hoveredRow);
            RequestRedraw();
            return true;
        }
        return false;
    }

    // ===== TOOLTIPS =====

    void UltraCanvasListView::UpdateHoverTooltip(const Point2Di& pointerWindow, int row, int column,
                                                 int headerColumn) {
        if (!showItemTooltips || !GetWindow()) return;

        std::string tip;
        if (headerColumn >= 0) {
            tip = GetTooltipTextAt(-1, headerColumn);
        } else if (row >= 0) {
            tip = GetTooltipTextAt(row, column);
        }

        if (tip.empty()) {
            HideHoverTooltip();
            return;
        }
        UltraCanvasTooltipManager::UpdateAndShowTooltip(GetWindow(), tip, pointerWindow);
    }

    void UltraCanvasListView::HideHoverTooltip() {
        UltraCanvasTooltipManager::HideTooltip();
    }

    bool UltraCanvasListView::HandleMouseUp(const UCEvent& /*event*/) {
        return false;
    }

    bool UltraCanvasListView::HandleMouseDoubleClick(const UCEvent& event) {
        int row = GetRowAtY(event.pointer.y);
        if (row >= 0) {
            if (onItemDoubleClicked) onItemDoubleClicked(row);
            if (onCellDoubleClicked) {
                int colX = 0;
                const int col = GetColumnAt(event.pointer.x, &colX);
                if (col >= 0) {
                    onCellDoubleClicked(row, col, Point2Di(event.pointer.x - colX,
                                                           event.pointer.y - GetRowRect(row).y));
                }
            }
            RequestRedraw();
            return true;
        }
        return false;
    }

    bool UltraCanvasListView::HandleMouseWheel(const UCEvent& event) {
        // Rows move out from under the pointer, so any cell tooltip is stale.
        HideHoverTooltip();
        hoveredColumn = -1;
        hoveredHeaderColumn = -1;
        if (event.shift && horizontalScrollbar->IsVisible()) {
            // Shift+wheel moves the columns, as it does in every desktop's
            // tables; one line step = kHorizontalStep px.
            horizontalScrollbar->GetStyleRef().scrollSpeed = kHorizontalStep;
            return horizontalScrollbar->ScrollByWheel(event.wheelDelta);
        }
        if (verticalScrollbar->IsVisible()) {
            // Route through the scrollbar so wheel scrolling is smoothly
            // animated; one line step = one row, wheelScrollLines rows/notch.
            verticalScrollbar->GetStyleRef().scrollSpeed = std::max(1, viewStyle.rowHeight);
            return verticalScrollbar->ScrollByWheel(event.wheelDelta);
        }
        return false;
    }

    bool UltraCanvasListView::HandleKeyDown(const UCEvent& event) {
        if (onKeyDown && onKeyDown(event)) return true;
        if (!model) return false;

        // The columns move under the view - with or without rows, since the
        // header scrolls too. Consumed even when they fit: a key pressed in
        // a focused table belongs to the table, and bubbling it on switched
        // the tab the table sat in.
        if (event.virtualKey == UCKeys::Left || event.virtualKey == UCKeys::Right) {
            ScrollHorizontallyBy(event.virtualKey == UCKeys::Left ? -kHorizontalStep : kHorizontalStep);
            return true;
        }
        if (model->GetRowCount() == 0) return false;

        switch (event.virtualKey) {
            case UCKeys::Up:       NavigateUp(); return true;
            case UCKeys::Down:     NavigateDown(); return true;
            case UCKeys::PageUp:   NavigatePageUp(); return true;
            case UCKeys::PageDown: NavigatePageDown(); return true;
            case UCKeys::Home:     NavigateHome(); return true;
            case UCKeys::End:      NavigateEnd(); return true;
            case UCKeys::Space:
            case UCKeys::Return:
                if (focusedRow >= 0) {
                    if (selection) selection->Select(focusedRow);
                    if (onItemActivated) onItemActivated(focusedRow);
                    RequestRedraw();
                }
                return true;
            default:
                break;
        }
        return false;
    }

    // ===== KEYBOARD NAVIGATION =====

    void UltraCanvasListView::NavigateUp() {
        if (!model) return;
        if (focusedRow <= 0) {
            focusedRow = 0;
        } else {
            focusedRow--;
        }
        if (selection) selection->Select(focusedRow);
        EnsureRowVisible(focusedRow);
        RequestRedraw();
    }

    void UltraCanvasListView::SetFocusedRow(int row) {
        if (!model || row < 0 || row >= model->GetRowCount()) return;
        focusedRow = row;
        if (selection) selection->Select(focusedRow);
        EnsureRowVisible(focusedRow);
        RequestRedraw();
    }

    void UltraCanvasListView::NavigateDown() {
        if (!model) return;
        int lastRow = model->GetRowCount() - 1;
        if (focusedRow >= lastRow) {
            focusedRow = lastRow;
        } else {
            focusedRow++;
        }
        if (selection) selection->Select(focusedRow);
        EnsureRowVisible(focusedRow);
        RequestRedraw();
    }

    void UltraCanvasListView::NavigatePageUp() {
        if (!model || model->GetRowCount() == 0) return;
        auto viewport = GetViewportRect();
        // Move up one viewport's worth of pixels (variable-height aware), always
        // advancing by at least one row.
        int cur = std::max(0, focusedRow);
        int targetRow = ClampRowIndexAtContentY(RowTopOffset(cur) - viewport.height);
        focusedRow = std::min(targetRow, cur - 1);
        if (focusedRow < 0) focusedRow = 0;
        if (selection) selection->Select(focusedRow);
        EnsureRowVisible(focusedRow);
        RequestRedraw();
    }

    void UltraCanvasListView::NavigatePageDown() {
        if (!model || model->GetRowCount() == 0) return;
        auto viewport = GetViewportRect();
        int lastRow = model->GetRowCount() - 1;
        int cur = std::max(0, focusedRow);
        int targetRow = ClampRowIndexAtContentY(RowTopOffset(cur) + viewport.height);
        focusedRow = std::max(targetRow, cur + 1);
        if (focusedRow > lastRow) focusedRow = lastRow;
        if (selection) selection->Select(focusedRow);
        EnsureRowVisible(focusedRow);
        RequestRedraw();
    }

    void UltraCanvasListView::NavigateHome() {
        if (!model || model->GetRowCount() == 0) return;
        focusedRow = 0;
        if (selection) selection->Select(focusedRow);
        EnsureRowVisible(focusedRow);
        RequestRedraw();
    }

    void UltraCanvasListView::NavigateEnd() {
        if (!model || model->GetRowCount() == 0) return;
        focusedRow = model->GetRowCount() - 1;
        if (selection) selection->Select(focusedRow);
        EnsureRowVisible(focusedRow);
        RequestRedraw();
    }

    // ===== MODEL SIGNALS =====

    void UltraCanvasListView::ConnectModelSignals() {
        if (!model) return;
        // Each callback first asks whether the view still exists (see
        // signalToken): the model, or a proxy's copy, may call it later.
        std::weak_ptr<char> alive = signalToken;
        model->onDataChanged = [this, alive]() {
            if (alive.expired()) return;
            InvalidateRowGeometry();
            columnFitStale = true;
            UpdateScrollbar();
            RequestRedraw();
            OnModelChanged();
        };
        model->onRowChanged = [this, alive](int /*row*/) {
            if (alive.expired()) return;
            // A row's content may change its variable height.
            InvalidateRowGeometry();
            columnFitStale = true;
            UpdateScrollbar();
            RequestRedraw();
            OnModelChanged();
        };
        model->onRowInserted = [this, alive](int row) {
            if (alive.expired()) return;
            // The rows from `row` on moved down one: the selection, the
            // keyboard focus and the hover stay on the items they were on.
            if (selection) selection->ShiftRows(row, 1);
            if (focusedRow >= row) focusedRow++;
            if (hoveredRow >= row) hoveredRow++;
            InvalidateRowGeometry();
            columnFitStale = true;
            UpdateScrollbar();
            RequestRedraw();
            OnModelChanged();
        };
        model->onRowRemoved = [this, alive](int row) {
            if (alive.expired()) return;
            // The removed row leaves the selection (notified); the rows below
            // it moved up one, and the selection moves with them.
            if (selection) selection->ShiftRows(row, -1);
            if (focusedRow == row) focusedRow = -1;
            else if (focusedRow > row) focusedRow--;
            if (hoveredRow == row) hoveredRow = -1;
            else if (hoveredRow > row) hoveredRow--;
            InvalidateRowGeometry();
            columnFitStale = true;
            UpdateScrollbar();
            RequestRedraw();
            OnModelChanged();
        };
    }

    void UltraCanvasListView::DisconnectModelSignals() {
        if (!model) return;
        model->onDataChanged = nullptr;
        model->onRowChanged = nullptr;
        model->onRowInserted = nullptr;
        model->onRowRemoved = nullptr;
    }

    // ===== OVERRIDES =====

    void UltraCanvasListView::Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) {
        // The engine has resolved our final bounds (explicit size or parent stretch).
        UltraCanvasUIElement::Arrange(finalRect, ctx);   // sets finalBounds + damage
        // finalBounds is now valid — recompute scrollbar visibility/geometry against it.
        // (Previously UpdateScrollbar only ran from mutators / SetBounds, never from the
        // engine's resize, so an in-tree ListView could reserve scrollbar space wrongly.)
        UpdateScrollbar();
        RevealPendingRow();
    }

    void UltraCanvasListView::SetBounds(const Rect2Df& bounds) {
        if (bounds != GetBounds()) {
            UltraCanvasUIElement::SetBounds(bounds);
            UpdateScrollbar();
            RevealPendingRow();
            RequestRedraw();
        }
    }

    void UltraCanvasListView::RevealPendingRow() {
        if (pendingVisibleRow < 0 || GetViewportRect().height <= 0) return;
        const int row = pendingVisibleRow;
        pendingVisibleRow = -1;
        EnsureRowVisible(row);
        UpdateScrollbar();
    }

    void UltraCanvasListView::SetWindow(UltraCanvasWindowBase* win) {
        UltraCanvasUIElement::SetWindow(win);
        if (verticalScrollbar) verticalScrollbar->SetWindow(win);
        if (horizontalScrollbar) horizontalScrollbar->SetWindow(win);
    }

} // namespace UltraCanvas

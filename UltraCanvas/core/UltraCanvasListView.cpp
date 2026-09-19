// core/UltraCanvasListView.cpp
// Model-View-Delegate ListView widget implementation
// Last Modified: 2026-07-22
#include "UltraCanvasListView.h"
#include "UltraCanvasListSortFilterProxy.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasTooltipManager.h"
#include <algorithm>

namespace UltraCanvas {

    // ===== CONSTRUCTOR =====

    UltraCanvasListView::UltraCanvasListView(const std::string& identifier,
                                              float x, float y, float w, float h)
        : UltraCanvasUIElement(identifier, x, y, w, h) {

        // Default delegate and selection
        delegate = std::make_shared<UltraCanvasDefaultListDelegate>();
        selection = std::make_shared<UltraCanvasSingleSelection>();
        selection->onSelectionChanged = [this](const std::vector<int>& rows) {
            if (onSelectionChanged) onSelectionChanged(rows);
            RequestRedraw();
        };

        // Scrolling defaults
        scrollOffsetY = 0;
        maxScrollY = 0;
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
        hoveredRow = -1;
        focusedRow = -1;
        // A new model means new columns; drop any per-view width overrides so a
        // stale override can't mis-size a different column set.
        columnWidthOverrides.clear();
        userAdjustedColumns = false;
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
        int localContentX = GetBorderLeftWidth() + GetPaddingLeft();
        int localContentY = GetBorderTopWidth() + GetPaddingTop();
        if (y < localContentY || y >= localContentY + viewStyle.headerHeight) return -1;
        int colX = localContentX;
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
                if (onSelectionChanged) onSelectionChanged(rows);
                RequestRedraw();
            };
        }
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
        if (verticalScrollbar) {
            verticalScrollbar->SetStyle(viewStyle.scrollbarStyle);
        }
        InvalidateRowGeometry();
        UpdateScrollbar();
        RequestRedraw();
    }

    const ListViewStyle& UltraCanvasListView::GetStyle() const {
        return viewStyle;
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
        int rowTop = RowTopOffset(row);
        int rowBottom = rowTop + RowHeightForRow(row);

        if (rowTop < scrollOffsetY) {
            scrollOffsetY = rowTop;
        } else if (rowBottom > scrollOffsetY + viewport.height) {
            scrollOffsetY = rowBottom - viewport.height;
        }
        ClampScrollOffset();
    }

    // ===== SCROLLBAR INTERNALS =====

    void UltraCanvasListView::CreateScrollbar() {
        verticalScrollbar = std::make_shared<UltraCanvasScrollbar>(
            GetIdentifier() + "_vscroll", 0, 0,
            viewStyle.scrollbarStyle.trackSize, 100,
            ScrollbarOrientation::Vertical);

        verticalScrollbar->onScrollChange = [this](int pos) {
            scrollOffsetY = pos;
            RequestRedraw();
        };
        verticalScrollbar->SetStyle(viewStyle.scrollbarStyle);
        verticalScrollbar->SetVisible(false);
    }

    void UltraCanvasListView::UpdateScrollbar() {
        if (!model || model->GetRowCount() == 0) {
            maxScrollY = 0;
            verticalScrollbar->SetVisible(false);
            return;
        }

        int headerOff = GetHeaderOffset();
        int rowsContentHeight = RowsContentHeight();
        int crHeight = GetHeight() - GetTotalBorderVertical() - GetTotalPaddingVertical();
        int rowsViewportHeight = crHeight - headerOff;

        maxScrollY = std::max(0, rowsContentHeight - rowsViewportHeight);
        bool hasVerticalScrollbar = maxScrollY > 0;

        verticalScrollbar->SetVisible(hasVerticalScrollbar);

        if (hasVerticalScrollbar) {
            // Scrollbar bounds in element-local space (within listview padding rect)
            int localPaddingX = GetBorderLeftWidth();
            int localPaddingY = GetBorderTopWidth();
            int paddingW = GetWidth() - GetTotalBorderHorizontal();
            int paddingH = GetHeight() - GetTotalBorderVertical();
            int scrollbarWidth = verticalScrollbar->GetStyle().trackSize;
            int sbX = localPaddingX + paddingW - scrollbarWidth;
            int sbY = localPaddingY + headerOff;
            int sbHeight = paddingH - headerOff;

            verticalScrollbar->SetPosition(sbX, sbY);
            verticalScrollbar->SetSize(scrollbarWidth, sbHeight);
            verticalScrollbar->SetViewportSize(rowsViewportHeight);
            verticalScrollbar->SetContentSize(rowsContentHeight);
        }
        ClampScrollOffset();
    }

    void UltraCanvasListView::ClampScrollOffset() {
        scrollOffsetY = std::max(0, std::min(scrollOffsetY, maxScrollY));
        if (verticalScrollbar->IsVisible()) {
            verticalScrollbar->SetScrollPosition(scrollOffsetY);
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

    Rect2Di UltraCanvasListView::GetViewportRect() const {
        // Returns element-local coordinates (ctx is translated to element origin)
        int localContentX = GetBorderLeftWidth() + GetPaddingLeft();
        int localContentY = GetBorderTopWidth() + GetPaddingTop();
        int crWidth = GetWidth() - GetTotalBorderHorizontal() - GetTotalPaddingHorizontal();
        int crHeight = GetHeight() - GetTotalBorderVertical() - GetTotalPaddingVertical();
        int sbWidth = verticalScrollbar->IsVisible() ? verticalScrollbar->GetStyle().trackSize : 0;
        int headerOff = GetHeaderOffset();
        return Rect2Di(localContentX, localContentY + headerOff, crWidth - sbWidth, crHeight - headerOff);
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
        int colX = viewport.x;
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

    int UltraCanvasListView::GetHeaderColumnAt(int x, int y, int* columnStartX) const {
        if (!model || !viewStyle.showHeader) return -1;

        // The header sits at the top of the content rect and spans its full
        // width (see RenderHeader), above the rows viewport.
        int localContentX = GetBorderLeftWidth() + GetPaddingLeft();
        int localContentY = GetBorderTopWidth() + GetPaddingTop();
        int crWidth = GetWidth() - GetTotalBorderHorizontal() - GetTotalPaddingHorizontal();
        if (y < localContentY || y >= localContentY + viewStyle.headerHeight) return -1;
        if (x < localContentX || x >= localContentX + crWidth) return -1;

        int colX = localContentX;
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

        // Draw scrollbar on top (bounds are stored in element-local space;
        // translate ctx so scrollbar can draw from (0,0))
        if (verticalScrollbar->IsVisible()) {
            ctx->PushState();
            auto sbB = verticalScrollbar->GetBounds();
            ctx->Translate(Point2Di(sbB.x, sbB.y));
            verticalScrollbar->Render(ctx, dirtyRect);
            ctx->PopState();
        }
    }

    // ===== SORTING (display and interaction only) =====

    void UltraCanvasListView::SetSortingEnabled(bool enabled) {
        if (sortingEnabled == enabled) return;
        sortingEnabled = enabled;
        RequestRedraw();
    }

    bool UltraCanvasListView::GetSortingEnabled() const { return sortingEnabled; }

    void UltraCanvasListView::SetSortIndicator(int column, ListSortOrder order) {
        sortIndicatorColumn = column < 0 ? -1 : column;
        sortIndicatorOrder = order;
        RequestRedraw();
    }

    void UltraCanvasListView::ClearSortIndicator() { SetSortIndicator(-1); }

    int UltraCanvasListView::GetSortIndicatorColumn() const { return sortIndicatorColumn; }

    ListSortOrder UltraCanvasListView::GetSortIndicatorOrder() const { return sortIndicatorOrder; }

    void UltraCanvasListView::SetSortProxy(std::shared_ptr<UltraCanvasListSortFilterProxy> proxy) {
        sortProxy = proxy;
        if (!proxy) return;
        SetSortingEnabled(true);
        // Adopt whatever the proxy is already sorted by, so attaching one that
        // has a sort does not make the header disagree with the rows.
        SetSortIndicator(proxy->GetSortColumn(), proxy->GetSortOrder());
    }

    bool UltraCanvasListView::HandleHeaderClick(int column) {
        if (!sortingEnabled || column < 0) return false;
        if (!model || column >= model->GetColumnCount()) return false;

        // The same column turns round; a different one starts ascending.
        ListSortOrder order = ListSortOrder::Ascending;
        if (column == sortIndicatorColumn && sortIndicatorOrder == ListSortOrder::Ascending)
            order = ListSortOrder::Descending;

        SetSortIndicator(column, order);

        if (auto proxy = sortProxy.lock()) proxy->SortByColumn(column, order);
        if (onSortRequested) onSortRequested(column, order);

        // Sorting moves every row, so a row index that meant something a moment
        // ago now points at a different record: the keyboard focus row is
        // dropped rather than left pointing somewhere arbitrary. The selection
        // is the caller's to map (UltraCanvasListSortFilterProxy::MapToSource).
        focusedRow = -1;
        RequestRedraw();
        return true;
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

        int colX = headerRect.x;
        int colCount = model->GetColumnCount();
        for (int col = 0; col < colCount; col++) {
            auto colDef = model->GetColumnDef(col);
            int colW = GetColumnWidth(col);
            ctx->SetTextAlignment(colDef.alignment);
            ctx->SetTextPaint(viewStyle.headerTextColor);

            // The sorted column keeps room at its right edge for the arrow, so
            // a long title is elided before it rather than drawn underneath it.
            // The width is the *current* one, so the indicator follows a column
            // the user has resized.
            const bool sorted = (col == sortIndicatorColumn);
            const int indicatorWidth = sorted ? 12 : 0;
            ctx->DrawTextInRect(colDef.title,
                                Rect2Dd(colX + 4, headerRect.y,
                                        colW - 8 - indicatorWidth, headerRect.height));

            if (sorted) {
                // A small solid triangle: up for ascending, down for descending.
                const double cx = colX + colW - 9.0;
                const double cy = headerRect.y + headerRect.height * 0.5;
                const double half = 3.5;
                std::vector<Point2Dd> arrow;
                if (sortIndicatorOrder == ListSortOrder::Ascending) {
                    arrow = { { cx, cy - half }, { cx + half, cy + half }, { cx - half, cy + half } };
                } else {
                    arrow = { { cx, cy + half }, { cx + half, cy - half }, { cx - half, cy - half } };
                }
                ctx->SetFillPaint(viewStyle.headerTextColor);
                ctx->FillLinePath(arrow);
            }

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
                // Multi-column mode: iterate columns
                int colX = viewport.x;
                for (int col = 0; col < colCount; col++) {
                    auto colDef = model->GetColumnDef(col);
                    int colW = GetColumnWidth(col);

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

        // Scrollbar gets events first (translate pointer from listview-local to scrollbar-local)
        if (verticalScrollbar->IsVisible()) {
            auto sbB = verticalScrollbar->GetBounds();
            if (sbB.Contains(event.pointer) || verticalScrollbar->IsDragging()) {
                UCEvent localEvent = event;
                localEvent.pointer = event.pointer - sbB.TopLeft();
                if (verticalScrollbar->OnEvent(localEvent)) {
                    if (event.type == UCEventType::MouseMove) {
                        // The pointer is on the scrollbar, not on a cell, and a
                        // drag moves the rows under it.
                        HideHoverTooltip();
                        hoveredColumn = -1;
                        hoveredHeaderColumn = -1;
                    }
                    return true;
                }
            }
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

        switch (event.type) {
            case UCEventType::MouseDown:
                return HandleMouseDown(event);
            case UCEventType::MouseMove:
                return HandleMouseMove(event);
            case UCEventType::MouseLeave:
                if (onCellHovered) onCellHovered(-1, -1, Point2Di(-1, -1));
                HideHoverTooltip();
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

    bool UltraCanvasListView::HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.pointer)) return false;

        SetFocus(true);

        int row = GetRowAtY(event.pointer.y);
        if (row < 0) {
            // A click in the header band sorts instead of clearing the
            // selection - losing what you had selected because you sorted the
            // table is not what anybody asks for.
            if (viewStyle.showHeader && sortingEnabled) {
                const int headerColumn = GetHeaderColumnAt(event.pointer.x, event.pointer.y);
                if (headerColumn >= 0) return HandleHeaderClick(headerColumn);
            }
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
        bool inside = Contains(event.pointer);
        int newHovered = inside ? GetRowAtY(event.pointer.y) : -1;

        int colX = 0;
        int newColumn = (newHovered >= 0) ? GetColumnAt(event.pointer.x, &colX) : -1;
        int newHeaderColumn = (inside && newHovered < 0)
                                  ? GetHeaderColumnAt(event.pointer.x, event.pointer.y)
                                  : -1;

        if (onCellHovered) {
            if (newHovered >= 0 && newColumn >= 0) {
                auto rowRect = GetRowRect(newHovered);
                onCellHovered(newHovered, newColumn, Point2Di(event.pointer.x - colX,
                                                              event.pointer.y - rowRect.y));
            } else {
                onCellHovered(-1, -1, Point2Di(-1, -1));
            }
        }

        // A tooltip belongs to one cell (or one header cell), so it is refreshed
        // whenever the hovered cell changes -- not only on a row change.
        if (newHovered != hoveredRow || newColumn != hoveredColumn ||
            newHeaderColumn != hoveredHeaderColumn) {
            UpdateHoverTooltip(event, newHovered, newColumn, newHeaderColumn);
        }
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

    void UltraCanvasListView::UpdateHoverTooltip(const UCEvent& event, int row, int column,
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
        UltraCanvasTooltipManager::UpdateAndShowTooltip(
                GetWindow(), tip, Point2Di(event.pointerWindow.x, event.pointerWindow.y));
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
        if (verticalScrollbar->IsVisible()) {
            // Route through the scrollbar so wheel scrolling is smoothly
            // animated; one line step = one row, wheelScrollLines rows/notch.
            verticalScrollbar->GetStyleRef().scrollSpeed = std::max(1, viewStyle.rowHeight);
            return verticalScrollbar->ScrollByWheel(event.wheelDelta);
        }
        return false;
    }

    bool UltraCanvasListView::HandleKeyDown(const UCEvent& event) {
        if (!model || model->GetRowCount() == 0) return false;

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
        model->onDataChanged = [this]() {
            InvalidateRowGeometry();
            UpdateScrollbar();
            RequestRedraw();
        };
        model->onRowChanged = [this](int /*row*/) {
            // A row's content may change its variable height.
            InvalidateRowGeometry();
            UpdateScrollbar();
            RequestRedraw();
        };
        model->onRowInserted = [this](int /*row*/) {
            InvalidateRowGeometry();
            UpdateScrollbar();
            RequestRedraw();
        };
        model->onRowRemoved = [this](int row) {
            if (selection && selection->IsSelected(row)) {
                selection->Deselect(row);
            }
            if (focusedRow == row) focusedRow = -1;
            else if (focusedRow > row) focusedRow--;
            if (hoveredRow == row) hoveredRow = -1;
            else if (hoveredRow > row) hoveredRow--;
            InvalidateRowGeometry();
            UpdateScrollbar();
            RequestRedraw();
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
    }

    void UltraCanvasListView::SetBounds(const Rect2Df& bounds) {
        if (bounds != GetBounds()) {
            UltraCanvasUIElement::SetBounds(bounds);
            UpdateScrollbar();
            RequestRedraw();
        }
    }

    void UltraCanvasListView::SetWindow(UltraCanvasWindowBase* win) {
        UltraCanvasUIElement::SetWindow(win);
        if (verticalScrollbar) {
            verticalScrollbar->SetWindow(win);
        }
    }

} // namespace UltraCanvas

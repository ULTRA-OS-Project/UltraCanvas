// core/UltraCanvasListView.cpp
// Model-View-Delegate ListView widget implementation
#include "UltraCanvasListView.h"
#include "UltraCanvasApplication.h"
#include <algorithm>

namespace UltraCanvas {

    // ===== CONSTRUCTOR =====

    UltraCanvasListView::UltraCanvasListView(const std::string& identifier, long id,
                                              int x, int y, int w, int h)
        : UltraCanvasUIElement(identifier, id, x, y, w, h) {

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

    void UltraCanvasListView::SetModel(IListModel* newModel) {
        DisconnectModelSignals();
        model = newModel;
        if (model) {
            ConnectModelSignals();
        }
        scrollOffsetY = 0;
        hoveredRow = -1;
        focusedRow = -1;
        if (selection) selection->Clear();
        UpdateScrollbar();
        RequestRedraw();
    }

    IListModel* UltraCanvasListView::GetModel() const {
        return model;
    }

    void UltraCanvasListView::SetDelegate(std::shared_ptr<IItemDelegate> newDelegate) {
        delegate = newDelegate ? newDelegate : std::make_shared<UltraCanvasDefaultListDelegate>();
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

    // ===== VIEW CONFIGURATION =====
    void UltraCanvasListView::SetStyle(const ListViewStyle& style) {
        viewStyle = style;
        SetBackgroundColor(viewStyle.backgroundColor);
        if (verticalScrollbar) {
            verticalScrollbar->SetStyle(viewStyle.scrollbarStyle);
        }
        UpdateScrollbar();
        RequestRedraw();
    }

    const ListViewStyle& UltraCanvasListView::GetStyle() const {
        return viewStyle;
    }

    void UltraCanvasListView::SetRowHeight(int height) {
        viewStyle.rowHeight = height;
        UpdateScrollbar();
        RequestRedraw();
    }

    int UltraCanvasListView::GetRowHeight() const {
        return viewStyle.rowHeight;
    }

    void UltraCanvasListView::SetShowHeader(bool show) {
        viewStyle.showHeader = show;
        UpdateScrollbar();
        RequestRedraw();
    }

    bool UltraCanvasListView::GetShowHeader() const {
        return viewStyle.showHeader;
    }

    // ===== SCROLLING =====

    void UltraCanvasListView::ScrollToRow(int row) {
        if (!model || row < 0 || row >= model->GetRowCount()) return;
        scrollOffsetY = row * viewStyle.rowHeight;
        ClampScrollOffset();
        RequestRedraw();
    }

    void UltraCanvasListView::EnsureRowVisible(int row) {
        if (!model || row < 0 || row >= model->GetRowCount()) return;

        auto viewport = GetViewportRect();
        int rowTop = row * viewStyle.rowHeight;
        int rowBottom = rowTop + viewStyle.rowHeight;

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
            GetIdentifier() + "_vscroll", 0, 0, 0,
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
        int rowsContentHeight = model->GetRowCount() * viewStyle.rowHeight;
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
        return model->GetRowCount() * viewStyle.rowHeight + GetHeaderOffset();
    }

    int UltraCanvasListView::GetHeaderOffset() const {
        if (viewStyle.showHeader)
            return viewStyle.headerHeight;
        return 0;
    }

    Rect2Di UltraCanvasListView::GetViewportRect() const {
        // Returns element-local coordinates (ctx is translated to element origin)
        int localContentX = GetBorderLeftWidth() + padding.left;
        int localContentY = GetBorderTopWidth() + padding.top;
        int crWidth = GetWidth() - GetTotalBorderHorizontal() - GetTotalPaddingHorizontal();
        int crHeight = GetHeight() - GetTotalBorderVertical() - GetTotalPaddingVertical();
        int sbWidth = verticalScrollbar->IsVisible() ? verticalScrollbar->GetStyle().trackSize : 0;
        int headerOff = GetHeaderOffset();
        return Rect2Di(localContentX, localContentY + headerOff, crWidth - sbWidth, crHeight - headerOff);
    }

    int UltraCanvasListView::GetRowAtY(int y) const {
        if (!model) return -1;
        auto viewport = GetViewportRect();
        int relativeY = y - viewport.y + scrollOffsetY;
        int row = relativeY / viewStyle.rowHeight;
        if (row < 0 || row >= model->GetRowCount()) return -1;
        return row;
    }

    Rect2Di UltraCanvasListView::GetRowRect(int row) const {
        auto viewport = GetViewportRect();
        int rowY = viewport.y + (row * viewStyle.rowHeight) - scrollOffsetY;
        return Rect2Di(viewport.x, rowY, viewport.width, viewStyle.rowHeight);
    }

    // ===== RENDERING =====

    void UltraCanvasListView::Render(IRenderContext* ctx, const Rect2Di& dirtyRect) {
        // Draw background and border
        UltraCanvasUIElement::Render(ctx, dirtyRect);

        // Element-local content rect (ctx is translated to element origin)
        int localContentX = GetBorderLeftWidth() + padding.left;
        int localContentY = GetBorderTopWidth() + padding.top;
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

    void UltraCanvasListView::RenderHeader(IRenderContext* ctx, const Rect2Di& contentRect) {
        if (!model) return;

        int sbWidth = verticalScrollbar->IsVisible() ? verticalScrollbar->GetStyle().trackSize : 0;
        Rect2Di headerRect(contentRect.x, contentRect.y, contentRect.width - sbWidth, viewStyle.headerHeight);

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
            ctx->SetTextAlignment(colDef.alignment);
            ctx->DrawTextInRect(colDef.title, Rect2Df(colX + 4, headerRect.y, colDef.width - 8, headerRect.height));

            // Grid line between columns
            if (viewStyle.showGridLines && col < colCount - 1) {
                ctx->SetStrokePaint(viewStyle.gridLineColor);
                ctx->DrawLine({colX + colDef.width, headerRect.y}, {colX + colDef.width, headerRect.Bottom()});
            }

            colX += colDef.width;
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

        // Calculate visible range (culling)
        int firstVisible = scrollOffsetY / viewStyle.rowHeight;
        int lastVisible = (scrollOffsetY + viewport.height) / viewStyle.rowHeight;
        firstVisible = std::max(0, firstVisible);
        lastVisible = std::min(rowCount - 1, lastVisible);

        for (int row = firstVisible; row <= lastVisible; row++) {
            int rowY = viewport.y + (row * viewStyle.rowHeight) - scrollOffsetY;

            // Skip if completely outside viewport
            if (rowY + viewStyle.rowHeight < viewport.y) continue;
            if (rowY > viewport.Bottom()) break;

            // Alternate row background
            if (viewStyle.alternateRowColors && row % 2 == 1) {
                ctx->SetFillPaint(viewStyle.alternateRowColor);
                ctx->FillRectangle(Rect2Df(viewport.x, rowY, viewport.width, viewStyle.rowHeight));
            }

            bool isSelected = selection && selection->IsSelected(row);
            bool isHovered = (row == hoveredRow);
            bool isFocused = (row == focusedRow) && IsFocused();

            // Draw full-row selection/hover background (before any column clipping)
            if (isSelected) {
                ctx->SetFillPaint(viewStyle.selectionBackgroundColor);
                ctx->FillRectangle(Rect2Df(viewport.x, rowY, viewport.width, viewStyle.rowHeight));
            } else if (isHovered) {
                ctx->SetFillPaint(viewStyle.hoverBackgroundColor);
                ctx->FillRectangle(Rect2Df(viewport.x, rowY, viewport.width, viewStyle.rowHeight));
            }

            if (colCount <= 1) {
                // Single-column mode: delegate renders entire row
                ListItemStyleOption opt;
                opt.rect = Rect2Di(viewport.x, rowY, viewport.width, viewStyle.rowHeight);
                opt.isSelected = isSelected;
                opt.isHovered = isHovered;
                opt.isFocused = isFocused;
                opt.isDisabled = IsDisabled();
                opt.row = row;
                opt.column = 0;
                opt.columnCount = 1;
                opt.columnX = viewport.x;
                opt.columnWidth = viewport.width;

                delegate->RenderItem(ctx, model, row, 0, opt);
            } else {
                // Multi-column mode: iterate columns
                int colX = viewport.x;
                for (int col = 0; col < colCount; col++) {
                    auto colDef = model->GetColumnDef(col);

                    ListItemStyleOption opt;
                    opt.rect = Rect2Di(viewport.x, rowY, viewport.width, viewStyle.rowHeight);
                    opt.isSelected = isSelected;
                    opt.isHovered = isHovered;
                    opt.isFocused = isFocused;
                    opt.isDisabled = IsDisabled();
                    opt.row = row;
                    opt.column = col;
                    opt.columnCount = colCount;
                    opt.columnX = colX;
                    opt.columnWidth = colDef.width;
                    opt.columnAlignment = colDef.alignment;

                    ctx->PushState();
                    ctx->ClipRect({colX, rowY, colDef.width, viewStyle.rowHeight});
                    delegate->RenderItem(ctx, model, row, col, opt);
                    ctx->PopState();

                    // Grid line between columns
                    if (viewStyle.showGridLines && col < colCount - 1) {
                        ctx->SetStrokePaint(viewStyle.gridLineColor);
                        ctx->DrawLine({colX + colDef.width, rowY},
                                      {colX + colDef.width, rowY + viewStyle.rowHeight});
                    }

                    colX += colDef.width;
                }
            }
        }
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
                    return true;
                }
            }
        }

        switch (event.type) {
            case UCEventType::MouseDown:
                return HandleMouseDown(event);
            case UCEventType::MouseMove:
                return HandleMouseMove(event);
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
        RequestRedraw();
        return true;
    }

    bool UltraCanvasListView::HandleMouseMove(const UCEvent& event) {
        int newHovered = Contains(event.pointer) ? GetRowAtY(event.pointer.y) : -1;
        if (newHovered != hoveredRow) {
            hoveredRow = newHovered;
            if (onItemHovered && hoveredRow >= 0) onItemHovered(hoveredRow);
            RequestRedraw();
            return true;
        }
        return false;
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
        if (verticalScrollbar->IsVisible()) {
            int scrollAmount = event.wheelDelta * viewStyle.rowHeight;
            scrollOffsetY -= scrollAmount;
            ClampScrollOffset();
            RequestRedraw();
            return true;
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
        if (!model) return;
        auto viewport = GetViewportRect();
        int pageRows = std::max(1, viewport.height / viewStyle.rowHeight);
        focusedRow = std::max(0, focusedRow - pageRows);
        if (selection) selection->Select(focusedRow);
        EnsureRowVisible(focusedRow);
        RequestRedraw();
    }

    void UltraCanvasListView::NavigatePageDown() {
        if (!model) return;
        auto viewport = GetViewportRect();
        int pageRows = std::max(1, viewport.height / viewStyle.rowHeight);
        focusedRow = std::min(model->GetRowCount() - 1, focusedRow + pageRows);
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
            UpdateScrollbar();
            RequestRedraw();
        };
        model->onRowChanged = [this](int /*row*/) {
            RequestRedraw();
        };
        model->onRowInserted = [this](int /*row*/) {
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

    void UltraCanvasListView::UpdateGeometry(IRenderContext* ctx) {
        if (verticalScrollbar) {
            verticalScrollbar->UpdateGeometry(ctx);
        }
    }

    void UltraCanvasListView::SetBounds(const Rect2Di& bounds) {
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

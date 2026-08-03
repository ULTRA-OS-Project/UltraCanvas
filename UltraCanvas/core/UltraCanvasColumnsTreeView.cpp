// core/UltraCanvasColumnsTreeView.cpp
// Columnar tree view built on UltraCanvasTreeView: an arbitrary, user-defined set
// of columns (SetColumns), an optional fixed header band, and draggable column
// widths. One column is the "tree column" (hosts the indent/expander/icon and draws
// node->data.text); every other column draws its cell from node->data.cells.
// Last Modified: 2026-07-27
#include "UltraCanvasColumnsTreeView.h"
#include "UltraCanvasApplication.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

    void UltraCanvasColumnsTreeView::SetDisplayMode(TreeDisplayMode mode) {
        if (displayMode == mode) return;
        displayMode = mode;
        // Toggling Columns/Classic changes whether the header band is present, which
        // changes the scrollable viewport height, so recompute the scrollbar.
        UpdateScrollbars();
        RequestRedraw();
    }

    void UltraCanvasColumnsTreeView::SetColumns(std::vector<TreeViewColumn> cols) {
        columns_ = std::move(cols);
        UpdateScrollbars();
        RequestRedraw();
    }

    void UltraCanvasColumnsTreeView::SetShowColumnHeader(bool show) {
        if (showHeader_ == show) return;
        showHeader_ = show;
        // The header band reserves vertical space above the rows.
        UpdateScrollbars();
        RequestRedraw();
    }

    int UltraCanvasColumnsTreeView::TreeColumnIndex() const {
        for (int i = 0; i < static_cast<int>(columns_.size()); ++i) {
            if (columns_[i].isTreeColumn) return i;
        }
        return 0;
    }

    int UltraCanvasColumnsTreeView::GetHeaderHeight() const {
        return (showHeader_ && displayMode == TreeDisplayMode::Columns) ? columnStyle.headerHeight : 0;
    }

    void UltraCanvasColumnsTreeView::EnsureDefaultColumns() {
        if (!columns_.empty()) return;
        // Reproduce the historical Name / Type / Value layout (Type fixed + orange
        // accent; Name and Value flexible) for views that never call SetColumns.
        columns_ = {
            { "name",  "Name",  0,   0,  1.0f, TextAlignment::Left, GetTextColor(),  Colors::Transparent, 0, true  },
            { "type",  "Type",  128, 0,  1.0f, TextAlignment::Left, Color(40,40,40), Color(255,190,130),  4, false },
            { "value", "Value", 0,   48, 1.0f, TextAlignment::Left, Color(40,40,40), Colors::Transparent, 0, false },
        };
    }

    int UltraCanvasColumnsTreeView::BaseTreeColumnLeft() const {
        Rect2Di cr = GetLocalContentRect();
        return cr.x + 1 + GetTextPadding();
    }

    std::vector<UltraCanvasColumnsTreeView::ColLayout>
    UltraCanvasColumnsTreeView::LayoutColumns(int rowLeft, int rowWidth, int textX) const {
        std::vector<ColLayout> out;
        const int n = static_cast<int>(columns_.size());
        if (n == 0) return out;
        out.resize(n);

        const int textPadding = GetTextPadding();
        const int gap = columnStyle.columnGap;
        const int treeIdx = TreeColumnIndex();
        const int rowRight = rowLeft + rowWidth - textPadding;

        // Fixed widths consume space up front; flexible columns share the remainder.
        long sumFixed = 0;
        float sumFlex = 0.0f;
        for (const auto& c : columns_) {
            if (c.width > 0) sumFixed += c.width;
            else if (c.flexWeight > 0.0f) sumFlex += c.flexWeight;
        }
        const int totalGaps = (n - 1) * gap;
        int flexAvail = rowRight - rowLeft - static_cast<int>(sumFixed) - totalGaps;
        if (flexAvail < 0) flexAvail = 0;

        std::vector<int> widths(n);
        for (int i = 0; i < n; ++i) {
            const TreeViewColumn& c = columns_[i];
            if (c.width > 0) {
                widths[i] = c.width;
            } else {
                float share = (sumFlex > 0.0f && c.flexWeight > 0.0f)
                                  ? (flexAvail * c.flexWeight / sumFlex) : 0.0f;
                int iw = static_cast<int>(share + 0.5f);
                if (iw < c.minWidth) iw = c.minWidth;
                widths[i] = iw;
            }
        }

        // Walk left-to-right assigning each column's slot (indent-independent). The
        // tree column draws from textX but keeps its slot, so the next column stays
        // aligned across rows regardless of indent.
        int x = rowLeft;
        for (int i = 0; i < n; ++i) {
            const int slotLeft = x;
            const int w = widths[i];
            out[i].slotWidth = w;
            if (i == treeIdx) {
                const int slotRight = slotLeft + w;
                out[i].x = textX;
                out[i].w = std::max(0, slotRight - textX);
            } else {
                out[i].x = slotLeft;
                out[i].w = w;
            }
            x += w + gap;
        }
        return out;
    }

    bool UltraCanvasColumnsTreeView::RenderNodeFullRow(IRenderContext *ctx, TreeNode *node, int nodeY,
                                                       const Rect2Di &contentRect, int rowWidth) {
        // Only Columns mode owns whole rows, and only for group-header nodes.
        if (displayMode != TreeDisplayMode::Columns || !node->data.isGroupHeader) {
            return false;
        }

        const int rowHeight = GetRowHeight();
        ctx->DrawFilledRectangle(Rect2Di(contentRect.x + 1, nodeY, rowWidth - 2, rowHeight),
                                 columnStyle.groupHeaderBackground);
        ctx->SetFontSize(GetFontSize());
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(columnStyle.groupHeaderTextColor);
        ctx->SetTextAlignment(TextAlignment::Center);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        auto layout = ctx->GetOrCreateTextLayout(node->data.text, Size2Di(rowWidth - 2, rowHeight), true);
        if (layout) {
            ctx->DrawTextLayout(*layout, Point2Dd(contentRect.x + 1, nodeY));
        }
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetTextAlignment(TextAlignment::Left);
        return true;
    }

    void UltraCanvasColumnsTreeView::RenderNodeLabel(IRenderContext *ctx, TreeNode *node, int nodeY,
                                                     int textX, int nodeWidth, int sbWidth,
                                                     const Rect2Di &contentRect) {
        if (displayMode == TreeDisplayMode::Columns) {
            RenderNodeColumns(ctx, node, nodeY, textX, contentRect.x + 1, nodeWidth - 2);
        } else {
            UltraCanvasTreeView::RenderNodeLabel(ctx, node, nodeY, textX, nodeWidth, sbWidth, contentRect);
        }
    }

    void UltraCanvasColumnsTreeView::RenderNodeColumns(IRenderContext *ctx, TreeNode *node, int nodeY,
                                                       int textX, int rowLeft, int rowWidth) {
        EnsureDefaultColumns();
        const TreeColumnStyle &cs = columnStyle;
        const int rowHeight = GetRowHeight();
        const int gap = cs.columnGap;
        const int treeIdx = TreeColumnIndex();

        auto layout = LayoutColumns(rowLeft, rowWidth, textX);

        ctx->SetFontSize(GetFontSize());
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);

        for (size_t i = 0; i < columns_.size() && i < layout.size(); ++i) {
            const TreeViewColumn &col = columns_[i];
            const int colX = layout[i].x;
            const int colW = layout[i].w;
            const bool isTree = (static_cast<int>(i) == treeIdx);

            // A non-tree column that starts left of the indented tree text has no room
            // on this (narrow / deeply-indented) row — skip it.
            if (!isTree && colX < textX) continue;
            if (colW <= 0) continue;

            // Resolve the cell's text + colour.
            std::string text;
            Color color;
            if (isTree) {
                text = node->data.text;
                color = node->data.textColor != Colors::Black ? node->data.textColor : GetTextColor();
            } else {
                const TreeCellData *cell = node->data.GetCell(col.id);
                if (!cell || cell->text.empty()) continue;
                text = cell->text;
                color = cell->textColor != Colors::Transparent ? cell->textColor : col.textColor;
            }

            // Optional accent fill behind the cell (e.g. the Type column).
            if (col.accentBackground != Colors::Transparent) {
                ctx->DrawFilledRectangle(
                    Rect2Di(colX - col.accentPadding, nodeY, colW + 2 * col.accentPadding, rowHeight),
                    col.accentBackground);
            }
            // Optional separator on this column's left edge.
            if (cs.showColumnSeparators && i > 0) {
                double sx = colX - gap / 2.0;
                ctx->DrawLine(Point2Dd(sx, nodeY), Point2Dd(sx, nodeY + rowHeight), cs.columnSeparatorColor);
            }

            ctx->SetTextAlignment(col.alignment);
            ctx->SetTextPaint(color);
            auto l = ctx->GetOrCreateTextLayout(text, Size2Di(colW, rowHeight), true);
            if (l) {
                ctx->DrawTextLayout(*l, Point2Dd(colX, nodeY));
            }
        }
        ctx->SetTextAlignment(TextAlignment::Left);
    }

    void UltraCanvasColumnsTreeView::RenderHeader(IRenderContext *ctx, const Rect2Di &headerRect) {
        EnsureDefaultColumns();
        const TreeColumnStyle &cs = columnStyle;

        // Band background + bottom border.
        ctx->DrawFilledRectangle(headerRect, cs.headerBackground);
        ctx->DrawLine(Point2Dd(headerRect.x, headerRect.Bottom() - 0.5),
                      Point2Dd(headerRect.Right(), headerRect.Bottom() - 0.5), cs.headerBorderColor);

        const int sbW = GetVerticalScrollbarWidth();
        const int rowLeft = headerRect.x + 1;
        const int rowWidth = headerRect.width - sbW - 2;
        const int textX = rowLeft + GetTextPadding();
        auto layout = LayoutColumns(rowLeft, rowWidth, textX);

        ctx->SetFontSize(GetFontSize());
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(cs.headerTextColor);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        for (size_t i = 0; i < columns_.size() && i < layout.size(); ++i) {
            const TreeViewColumn &col = columns_[i];
            if (layout[i].w <= 0 || col.title.empty()) continue;
            ctx->SetTextAlignment(col.alignment);
            auto l = ctx->GetOrCreateTextLayout(col.title, Size2Di(layout[i].w, headerRect.height), true);
            if (l) {
                ctx->DrawTextLayout(*l, Point2Dd(layout[i].x, headerRect.y));
            }
        }
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetTextAlignment(TextAlignment::Left);

        // Grip lines at the draggable interior boundaries.
        if (resizable_) {
            const int gap = cs.columnGap;
            for (size_t i = 0; i + 1 < layout.size(); ++i) {
                double gx = layout[i].x + layout[i].w + gap / 2.0;
                ctx->DrawLine(Point2Dd(gx, headerRect.y + 4), Point2Dd(gx, headerRect.Bottom() - 4),
                              cs.headerBorderColor);
            }
        }
    }

    int UltraCanvasColumnsTreeView::BoundaryAtX(int x, int *outX) const {
        if (!resizable_ || displayMode != TreeDisplayMode::Columns || columns_.size() < 2) {
            return -1;
        }
        Rect2Di cr = GetLocalContentRect();
        const int sbW = GetVerticalScrollbarWidth();
        const int rowLeft = cr.x + 1;
        const int rowWidth = cr.width - sbW - 2;
        const int textX = rowLeft + GetTextPadding();
        auto layout = LayoutColumns(rowLeft, rowWidth, textX);
        const int gap = columnStyle.columnGap;
        for (int i = 0; i + 1 < static_cast<int>(layout.size()); ++i) {
            const int boundary = layout[i].x + layout[i].w + gap / 2;  // middle of the gap after column i
            if (std::abs(x - boundary) <= 4) {
                if (outX) *outX = boundary;
                return i;
            }
        }
        return -1;
    }

    bool UltraCanvasColumnsTreeView::OnEvent(const UCEvent &event) {
        if (displayMode == TreeDisplayMode::Columns && resizable_) {
            switch (event.type) {
                case UCEventType::MouseDown: {
                    int bx = 0;
                    int col = Contains(event.pointer) ? BoundaryAtX(event.pointer.x, &bx) : -1;
                    if (col >= 0) {
                        EnsureDefaultColumns();
                        // Capture the resized column's current allocated width so the
                        // drag tracks the mouse 1:1 (converting a flex column to fixed).
                        Rect2Di cr = GetLocalContentRect();
                        const int rowLeft = cr.x + 1;
                        const int rowWidth = cr.width - GetVerticalScrollbarWidth() - 2;
                        auto layout = LayoutColumns(rowLeft, rowWidth, BaseTreeColumnLeft());
                        dragCol_ = col;
                        dragStartX_ = event.pointer.x;
                        dragStartW_ = (col < static_cast<int>(layout.size())) ? layout[col].slotWidth
                                                                              : columns_[col].width;
                        if (auto *app = UltraCanvasApplication::GetInstance()) app->CaptureMouse(this);
                        return true;
                    }
                    break;
                }
                case UCEventType::MouseMove: {
                    if (dragCol_ >= 0) {
                        const int delta = event.pointer.x - dragStartX_;
                        int minW = columns_[dragCol_].minWidth > 0 ? columns_[dragCol_].minWidth : 16;
                        columns_[dragCol_].width = std::max(minW, dragStartW_ + delta);
                        RequestRedraw();
                        return true;
                    }
                    // Hover affordance over a resizable boundary; otherwise let the
                    // base update the row hover highlight.
                    int bx = 0;
                    SetMouseCursor(BoundaryAtX(event.pointer.x, &bx) >= 0 ? UCMouseCursor::SizeWE
                                                                          : UCMouseCursor::Default);
                    break;
                }
                case UCEventType::MouseUp: {
                    if (dragCol_ >= 0) {
                        dragCol_ = -1;
                        if (auto *app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
                        return true;
                    }
                    break;
                }
                default:
                    break;
            }
        }
        return UltraCanvasTreeView::OnEvent(event);
    }

} // namespace UltraCanvas

// core/UltraCanvasColumnsTreeView.cpp
// Columnar (Name / Type / Value) tree view built on UltraCanvasTreeView.
// Last Modified: 2026-07-26
#include "UltraCanvasColumnsTreeView.h"
#include <algorithm>

namespace UltraCanvas {

    void UltraCanvasColumnsTreeView::SetDisplayMode(TreeDisplayMode mode) {
        if (displayMode == mode) return;
        displayMode = mode;
        // Row height and the set of visible rows are identical in both modes, so the
        // scrollbar geometry does not change; a redraw is enough.
        RequestRedraw();
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
        ctx->SetFontSize(12);
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
        const TreeColumnStyle &cs = columnStyle;
        const int rowHeight = GetRowHeight();
        const int textPadding = GetTextPadding();
        int rowRight = rowLeft + rowWidth - textPadding;

        // Column geometry (left -> right): Name | Type | Value.
        int valW = cs.valueColumnWidth > 0
                       ? cs.valueColumnWidth
                       : std::max(48, static_cast<int>(rowWidth * 0.28f));
        int typeW = cs.typeColumnWidth;
        int gap = cs.columnGap;

        int valueX = rowRight - valW;
        int typeX = valueX - gap - typeW;
        int nameRight = typeX - gap;
        int nameWidth = std::max(0, nameRight - textX);

        // Type accent background (only when there is a type to show).
        if (!node->data.typeText.empty() && typeX >= textX) {
            ctx->DrawFilledRectangle(
                Rect2Di(typeX - cs.typeColumnPadding, nodeY,
                        typeW + 2 * cs.typeColumnPadding, rowHeight),
                cs.typeColumnBackground);
        }

        // Optional thin separators between columns.
        if (cs.showColumnSeparators && typeX >= textX) {
            ctx->DrawLine(Point2Dd(typeX - gap / 2.0, nodeY),
                          Point2Dd(typeX - gap / 2.0, nodeY + rowHeight), cs.columnSeparatorColor);
            ctx->DrawLine(Point2Dd(valueX - gap / 2.0, nodeY),
                          Point2Dd(valueX - gap / 2.0, nodeY + rowHeight), cs.columnSeparatorColor);
        }

        ctx->SetFontSize(12);
        ctx->SetTextAlignment(TextAlignment::Left);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);

        // Name column
        Color nameColor = node->data.textColor != Colors::Black ? node->data.textColor : GetTextColor();
        ctx->SetTextPaint(nameColor);
        auto layoutName = ctx->GetOrCreateTextLayout(node->data.text, Size2Di(nameWidth, rowHeight), true);
        if (layoutName) {
            ctx->DrawTextLayout(*layoutName, Point2Dd(textX, nodeY));
        }

        if (typeX < textX) return;  // row too narrow for the remaining columns

        // Type column
        if (!node->data.typeText.empty()) {
            Color typeCol = node->data.typeColor != Colors::Transparent
                                ? node->data.typeColor
                                : cs.typeTextColor;
            ctx->SetTextPaint(typeCol);
            auto layoutType = ctx->GetOrCreateTextLayout(node->data.typeText, Size2Di(typeW, rowHeight), true);
            if (layoutType) {
                ctx->DrawTextLayout(*layoutType, Point2Dd(typeX, nodeY));
            }
        }

        // Value column
        if (!node->data.valueText.empty()) {
            ctx->SetTextPaint(cs.valueTextColor);
            auto layoutValue = ctx->GetOrCreateTextLayout(node->data.valueText, Size2Di(valW, rowHeight), true);
            if (layoutValue) {
                ctx->DrawTextLayout(*layoutValue, Point2Dd(valueX, nodeY));
            }
        }
    }

} // namespace UltraCanvas

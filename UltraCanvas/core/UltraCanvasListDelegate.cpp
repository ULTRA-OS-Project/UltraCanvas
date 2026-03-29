// core/UltraCanvasListDelegate.cpp
// Default item delegate rendering for ListView
#include "UltraCanvasListDelegate.h"
#include "UltraCanvasListModel.h"

namespace UltraCanvas {

    void UltraCanvasDefaultListDelegate::RenderItem(IRenderContext* ctx, const IListModel* model,
                                                     int row, int column,
                                                     const ListItemStyleOption& option) {
        if (!ctx || !model) return;

        int cellX = option.columnX;
        int cellW = option.columnWidth;
        int cellY = option.rect.y;
        int cellH = option.rect.height;

        // Selection/hover background is drawn by the view (ListViewStyle)

        // 1. Determine text start position
        int textX = cellX + textPadding;
        int availableWidth = cellW - textPadding * 2;

        // 3. Draw icon if present
        ListIndex idx{row, column};
        std::string iconPath = GetStringValue(model->GetData(idx, ListDataRole::DecorationRole));
        if (!iconPath.empty()) {
            int iconY = cellY + (cellH - iconSize) / 2;
            ctx->DrawImage(iconPath, static_cast<float>(textX), static_cast<float>(iconY),
                          static_cast<float>(iconSize), static_cast<float>(iconSize),
                          ImageFitMode::Contain);
            textX += iconSize + iconSpacing;
            availableWidth -= (iconSize + iconSpacing);
        }

        // 4. Draw text
        std::string text = GetStringValue(model->GetData(idx, ListDataRole::DisplayRole));
        if (!text.empty() && availableWidth > 0) {
            ctx->SetFontSize(fontSize);
            ctx->SetTextWrap(TextWrap::WrapNone);
            ctx->SetTextAlignment(option.columnAlignment);
            ctx->SetTextVerticalAlignment(TextVerticalAlignment::Middle);

            if (option.isSelected) {
                ctx->SetTextPaint(selectedTextColor);
            } else {
                ctx->SetTextPaint(textColor);
            }

            ctx->DrawTextInRect(text,
                static_cast<float>(textX), static_cast<float>(cellY),
                static_cast<float>(availableWidth), static_cast<float>(cellH));
        }
    }

    int UltraCanvasDefaultListDelegate::GetRowHeight(const IListModel* /*model*/, int /*row*/) const {
        return rowHeight;
    }

} // namespace UltraCanvas

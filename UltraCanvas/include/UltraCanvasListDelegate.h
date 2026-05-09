// include/UltraCanvasListDelegate.h
// Item delegate interfaces and default delegate for ListView
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include <string>
#include <memory>

namespace UltraCanvas {

    class IListModel;

    // Style information passed to the delegate for rendering a single item/cell
    struct ListItemStyleOption {
        Rect2Di rect;                   // Full row rectangle
        bool isSelected = false;
        bool isHovered = false;
        bool isFocused = false;
        bool isDisabled = false;
        int row = -1;
        int column = 0;
        int columnCount = 1;

        // Column layout info (for multi-column rendering)
        int columnX = 0;
        int columnWidth = 0;
        TextAlignment columnAlignment = TextAlignment::Left;
    };

    // Abstract delegate interface
    class IItemDelegate {
    public:
        virtual ~IItemDelegate() = default;

        // Render a single item (or cell in multi-column mode)
        virtual void RenderItem(IRenderContext* ctx, const IListModel* model,
                               int row, int column,
                               const ListItemStyleOption& option) = 0;

        // Size hint for a row (height is the key value)
        virtual int GetRowHeight(const IListModel* model, int row) const {
            return 24;
        }
    };

    // Default delegate: renders selection/hover background, optional icon, then text
    class UltraCanvasDefaultListDelegate : public IItemDelegate {
    public:
        void RenderItem(IRenderContext* ctx, const IListModel* model,
                       int row, int column,
                       const ListItemStyleOption& option) override;

        int GetRowHeight(const IListModel* model, int row) const override;

        // Styling
        void SetFontSize(float size) { fontSize = size; }
        void SetIconSize(int size) { iconSize = size; }
        void SetIconSpacing(int spacing) { iconSpacing = spacing; }
        void SetTextPadding(int padding) { textPadding = padding; }
        void SetRowHeight(int height) { rowHeight = height; }

        void SetTextColor(const Color& color) { textColor = color; }
        void SetSelectedTextColor(const Color& color) { selectedTextColor = color; }

    private:
        float fontSize = 12.0f;
        int iconSize = 16;
        int iconSpacing = 4;
        int textPadding = 6;
        int rowHeight = 24;
        Color textColor = Colors::TextDefault;
        Color selectedTextColor = Colors::White;
    };

} // namespace UltraCanvas

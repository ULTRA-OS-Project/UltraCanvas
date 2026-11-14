// include/UltraCanvasListDelegate.h
// Item delegate interfaces and default delegate for ListView
// Version: 1.1.0 - IItemDelegate::ApplyTheme: the list hands its theme on to its delegate; the default
//                  delegate takes the ink, the text on the selection and the body size, except what
//                  was set by hand (SetTextColor / SetSelectedTextColor, SetFontSize)
// Version: 1.0.0 - the history starts here; earlier changes are in the framework changelog
// Last Modified: 2026-11-14
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include <string>
#include <memory>

namespace UltraCanvas {
    class UltraCanvasTheme;

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
            (void)model; (void)row;
            return 24;
        }

        // The list's theme, handed on: a list that follows the theme calls
        // it when the delegate is set (SetDelegate) and on every theme change
        // after. A delegate with colours of its own takes them from the
        // theme here; the default does nothing.
        virtual void ApplyTheme(const UltraCanvasTheme& theme) { (void)theme; }
    };

    // Default delegate: renders selection/hover background, optional icon, then text
    class UltraCanvasDefaultListDelegate : public IItemDelegate {
    public:
        void RenderItem(IRenderContext* ctx, const IListModel* model,
                       int row, int column,
                       const ListItemStyleOption& option) override;

        int GetRowHeight(const IListModel* model, int row) const override;

        // Styling. A font size or a text colour set here is the
        // application's: ApplyTheme leaves it as it is.
        void SetFontSize(float size) { fontSize = size; fontSizeByHand = true; }
        void SetIconSize(int size) { iconSize = size; }
        void SetIconSpacing(int spacing) { iconSpacing = spacing; }
        void SetTextPadding(int padding) { textPadding = padding; }
        void SetRowHeight(int height) { rowHeight = height; }

        void SetTextColor(const Color& color) { textColor = color; textColorByHand = true; }
        void SetSelectedTextColor(const Color& color) { selectedTextColor = color; selectedTextColorByHand = true; }
        float GetFontSize() const { return fontSize; }
        const Color& GetTextColor() const { return textColor; }
        const Color& GetSelectedTextColor() const { return selectedTextColor; }

        // The theme's ink for the text, the text on the accent for a selected
        // row and the body size - each only while the application has not
        // set it itself.
        void ApplyTheme(const UltraCanvasTheme& theme) override;

    private:
        bool fontSizeByHand = false;
        bool textColorByHand = false;
        bool selectedTextColorByHand = false;
        float fontSize = 12.0f;
        int iconSize = 16;
        int iconSpacing = 4;
        int textPadding = 6;
        int rowHeight = 24;
        Color textColor = Colors::TextDefault;
        Color selectedTextColor = Colors::White;
    };

} // namespace UltraCanvas

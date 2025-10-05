// include/UltraCanvasDropdown.h
// Interactive dropdown/combobox component with styling options
// Version: 1.2.4
// Last Modified: 2025-01-17
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <algorithm>

namespace UltraCanvas {

// ===== DROPDOWN ITEM DATA =====
    struct DropdownItem {
        std::string text;
        std::string value;
        std::string iconPath;
        bool enabled = true;
        bool separator = false;
        void* userData = nullptr;

        DropdownItem() = default;
        DropdownItem(const std::string& itemText) : text(itemText), value(itemText) {}
        DropdownItem(const std::string& itemText, const std::string& itemValue)
                : text(itemText), value(itemValue) {}
    };

// ===== DROPDOWN STYLING =====
    struct DropdownStyle {
        // Button appearance
        Color normalColor = Colors::White;
        Color hoverColor = Color(240, 245, 255, 255);
        Color pressedColor = Color(225, 235, 255, 255);
        Color disabledColor = Color(245, 245, 245, 255);
        Color borderColor = Color(180, 180, 180, 255);
        Color focusBorderColor = Color(100, 150, 255, 255);

        // Text colors
        Color normalTextColor = Colors::Black;
        Color disabledTextColor = Color(128, 128, 128, 255);

        // List appearance
        Color listBackgroundColor = Colors::White;
        Color listBorderColor = Color(180, 180, 180, 255);
        Color itemHoverColor = Color(240, 245, 255, 255);
        Color itemSelectedColor = Color(225, 235, 255, 255);

        // Dimensions
        float borderWidth = 1.0f;
        float cornerRadius = 2.0f;
        float paddingLeft = 8.0f;
        float paddingRight = 20.0f;
        float itemHeight = 24.0f;
        int maxItemWidth = 400;
        int maxVisibleItems = 8;
        float arrowSize = 8.0f;

        // Shadow
        bool hasShadow = true;
        Color shadowColor = Color(0, 0, 0, 80);
        Point2Di shadowOffset = Point2Di(2, 2);

        // Font
        std::string fontFamily = "Arial";
        float fontSize = 12.0f;
    };

// ===== DROPDOWN COMPONENT =====
    class UltraCanvasDropdown : public UltraCanvasUIElement {
    public:
        // ===== CALLBACKS =====
        std::function<void(int, const DropdownItem&)> onSelectionChanged;
        std::function<void(int, const DropdownItem&)> onItemHovered;
        std::function<void()> onDropdownOpened;
        std::function<void()> onDropdownClosed;

    private:
        std::vector<DropdownItem> items;
        int selectedIndex = -1;
        int hoveredIndex = -1;
        bool dropdownOpen = false;
        bool buttonPressed = false;
        int scrollOffset = 0;

        DropdownStyle style;
        int dropdownHeight = 0;
        int dropdownWidth = 0;
        int maxDropdownHeight = 0;
        bool needScrollbar = false;
        bool needCalculateDimensions = false;

    public:
        UltraCanvasDropdown(const std::string& identifier, long id, long x, long y, long w, long h = 24);

        virtual ~UltraCanvasDropdown() = default;

        // ===== ITEM MANAGEMENT =====
        void AddItem(const std::string& text);
        void AddItem(const std::string& text, const std::string& value);
        void AddItem(const DropdownItem& item);

        void AddSeparator();

        void ClearItems();
        void RemoveItem(int index);

        // ===== SELECTION MANAGEMENT =====
        void SetSelectedIndex(int index);

        int GetSelectedIndex() const {
            return selectedIndex;
        }

        const DropdownItem* GetSelectedItem() const;

        Rect2Di GetActualBounds() override;

        bool Contains(int px, int py) override {
            return GetActualBounds().Contains(px, py);
        }

        // ===== DROPDOWN STATE =====
        void OpenDropdown();
        void CloseDropdown();
        bool IsDropdownOpen() const { return dropdownOpen; }

        // ===== STYLING =====
        void SetStyle(const DropdownStyle& newStyle);
        const DropdownStyle& GetStyle() const { return style; }

        // ===== ACCESSORS =====
        const std::vector<DropdownItem>& GetItems() const {
            return items;
        }

        int GetItemCount() const { return (int)items.size(); }
        const DropdownItem* GetItem(int index) const;

        // ===== RENDERING =====
        void Render() override;

        void RenderPopupContent() override;

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override;

    private:
        void CalculateDropdownDimensions();
        Rect2Di CalculatePopupPosition();

        void RenderButton(IRenderContext* ctx);

        void RenderDropdownArrow(const Rect2Di& buttonRect, const Color& color, IRenderContext* ctx);

//        void RenderDropdownList(IRenderContext* ctx) {
//            if (items.empty()) return;
//
//            ctx->PushState();
//
//            Rect2Di buttonRect = GetBounds();
//            Rect2Di listRect(buttonRect.x, buttonRect.y + buttonRect.height,
//                             buttonRect.width, dropdownHeight);
//
//            // Draw shadow
//            if (style.hasShadow) {
//                ctx->DrawShadow(listRect, style.shadowColor, style.shadowOffset);
//            }
//
//            // Draw list background
//            std::cout << "RenderDropdownList UltraCanvas::DrawFilledRect" << std::endl;
//            ctx->DrawFilledRectangle(listRect, style.listBackgroundColor, 1.0, style.listBorderColor);
//
//            // Render visible items
//            int visibleItems = std::min((int)items.size(), style.maxVisibleItems);
//            int startIndex = scrollOffset;
//            int endIndex = std::min(startIndex + visibleItems, (int)items.size());
//
//            for (int i = startIndex; i < endIndex; i++) {
//                RenderDropdownItem(i, listRect, i - startIndex, ctx);
//            }
//
//            // Draw scrollbar if needed
//            if (needScrollbar) {
//                RenderScrollbar(listRect, ctx);
//            }
//
//            ctx->PopState();
//        }

        void RenderDropdownItem(int itemIndex, const Rect2Di& listRect, int visualIndex, IRenderContext* ctx);

        void RenderScrollbar(const Rect2Di& listRect, IRenderContext* ctx);

        std::string GetDisplayText() const;

        // FIXED: Complete EnsureItemVisible implementation
        void EnsureItemVisible(int index);

        int GetItemAtPosition(int x, int y);

        // ===== EVENT HANDLERS =====
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
        void HandleMouseLeave(const UCEvent& event);
        void HandleKeyDown(const UCEvent& event);
        bool HandleMouseWheel(const UCEvent& event);
        void NavigateItem(int direction);
        void NavigateSelection(int direction);
        int FindFirstEnabledItem() const;

        int FindLastEnabledItem() const;
    };

// ===== DROPDOWN BUILDER =====
    class DropdownBuilder {
    private:
        std::shared_ptr<UltraCanvasDropdown> dropdown;

    public:
        DropdownBuilder(const std::string& identifier, long x, long y, long w = 150, long h = 24) {
            dropdown = std::make_shared<UltraCanvasDropdown>(identifier, 0, x, y, w, h);
        }

        DropdownBuilder& AddItem(const std::string& text) {
            dropdown->AddItem(text);
            return *this;
        }

        DropdownBuilder& AddItem(const std::string& text, const std::string& value) {
            dropdown->AddItem(text, value);
            return *this;
        }

        DropdownBuilder& AddItems(const std::vector<std::string>& items) {
            for (const std::string& item : items) {
                dropdown->AddItem(item);
            }
            return *this;
        }

        DropdownBuilder& AddSeparator() {
            dropdown->AddSeparator();
            return *this;
        }

        DropdownBuilder& SetStyle(const DropdownStyle& style) {
            dropdown->SetStyle(style);
            return *this;
        }

        DropdownBuilder& SetSelectedIndex(int index) {
            dropdown->SetSelectedIndex(index);
            return *this;
        }

        DropdownBuilder& OnSelectionChanged(std::function<void(int, const DropdownItem&)> callback) {
            dropdown->onSelectionChanged = callback;
            return *this;
        }

        DropdownBuilder& OnDropdownOpened(std::function<void()> callback) {
            dropdown->onDropdownOpened = callback;
            return *this;
        }

        DropdownBuilder& OnDropdownClosed(std::function<void()> callback) {
            dropdown->onDropdownClosed = callback;
            return *this;
        }

        std::shared_ptr<UltraCanvasDropdown> Build() {
            return dropdown;
        }
    };

// ===== PREDEFINED DROPDOWN STYLES =====
    namespace DropdownStyles {
        inline DropdownStyle Default() {
            return DropdownStyle(); // Uses default values
        }

        inline DropdownStyle Flat() {
            DropdownStyle style;
            style.normalColor = Colors::White;
            style.hoverColor = Color(240, 240, 240, 255);
            style.pressedColor = Color(230, 230, 230, 255);
            style.borderColor = Color(200, 200, 200, 255);
            style.cornerRadius = 0.0f;
            style.hasShadow = false;
            return style;
        }

        inline DropdownStyle Modern() {
            DropdownStyle style;
            style.normalColor = Colors::White;
            style.hoverColor = Color(245, 245, 245, 255);
            style.pressedColor = Color(235, 235, 235, 255);
            style.borderColor = Color(180, 180, 180, 255);
            style.focusBorderColor = Color(100, 150, 255, 255);
            style.cornerRadius = 4.0f;
            style.paddingLeft = 12.0f;
            style.paddingRight = 30.0f;
            style.fontSize = 13.0f;
            return style;
        }

        inline DropdownStyle Dark() {
            DropdownStyle style;
            style.normalColor = Color(45, 45, 45, 255);
            style.hoverColor = Color(55, 55, 55, 255);
            style.pressedColor = Color(35, 35, 35, 255);
            style.disabledColor = Color(60, 60, 60, 255);
            style.borderColor = Color(80, 80, 80, 255);
            style.focusBorderColor = Color(100, 150, 255, 255);
            style.normalTextColor = Colors::White;
            style.disabledTextColor = Color(128, 128, 128, 255);
            style.listBackgroundColor = Color(40, 40, 40, 255);
            style.listBorderColor = Color(80, 80, 80, 255);
            style.itemHoverColor = Color(60, 60, 60, 255);
            style.itemSelectedColor = Color(50, 50, 50, 255);
            return style;
        }
    }

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasDropdown> CreateDropdown(
            const std::string& identifier, long id, long x, long y, long w, long h = 24) {
        return std::make_shared<UltraCanvasDropdown>(identifier, id, x, y, w, h);
    }

    inline std::shared_ptr<UltraCanvasDropdown> CreateAutoDropdown(
            const std::string& identifier, long x, long y, const std::vector<std::string>& items) {
        auto dropdown = std::make_shared<UltraCanvasDropdown>(identifier, 0, x, y, 150, 24);

        for (const std::string& item : items) {
            dropdown->AddItem(item);
        }

        return dropdown;
    }

} // namespace UltraCanvas
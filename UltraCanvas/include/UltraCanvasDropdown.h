// include/UltraCanvasDropdown.h
// Interactive dropdown/combobox component with icon support and multi-selection
// Version: 2.0.0
// Last Modified: 2025-10-30
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
#include <set>

namespace UltraCanvas {

// ===== DROPDOWN ITEM DATA =====
    struct DropdownItem {
        std::string text;
        std::string value;
        std::string iconPath;
        bool enabled = true;
        bool separator = false;
        bool selected = false;  // NEW: for multi-selection support
        void* userData = nullptr;

        DropdownItem() = default;
        DropdownItem(const std::string& itemText) : text(itemText), value(itemText) {}
        DropdownItem(const std::string& itemText, const std::string& itemValue)
                : text(itemText), value(itemValue) {}
        DropdownItem(const std::string& itemText, const std::string& itemValue, const std::string& icon)
                : text(itemText), value(itemValue), iconPath(icon) {}
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

        // Multi-selection colors
        Color checkboxBorderColor = Color(180, 180, 180, 255);
        Color checkboxCheckedColor = Color(100, 150, 255, 255);
        Color checkmarkColor = Colors::White;

        // Dimensions
        float borderWidth = 1.0f;
        float cornerRadius = 2.0f;
        float paddingLeft = 8.0f;
        float paddingRight = 20.0f;
        float itemHeight = 24.0f;
        int maxItemWidth = 400;
        int maxVisibleItems = 8;
        float arrowSize = 8.0f;

        // Icon dimensions
        float iconSize = 16.0f;
        float iconPadding = 4.0f;

        // Checkbox dimensions (for multi-select)
        float checkboxSize = 14.0f;
        float checkboxPadding = 6.0f;

        // Shadow
//        bool hasShadow = true;
//        Color shadowColor = Color(0, 0, 0, 80);
//        float shadowOffset = 2;

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

        // NEW: Multi-selection callbacks
        std::function<void(const std::vector<int>&)> onMultiSelectionChanged;
        std::function<void(const std::vector<DropdownItem>&)> onSelectedItemsChanged;

    private:
        std::vector<DropdownItem> items;
        int selectedIndex = -1;
        int hoveredIndex = -1;
        bool dropdownOpen = false;
        bool buttonPressed = false;
        int scrollOffset = 0;

        // NEW: Multi-selection support
        bool multiSelectEnabled = false;
        std::set<int> selectedIndices;

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
        void AddItem(const std::string& text, const std::string& value, const std::string& iconPath);
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

        // NEW: Multi-selection management
        void SetMultiSelectEnabled(bool enabled);
        bool IsMultiSelectEnabled() const { return multiSelectEnabled; }

        void SetItemSelected(int index, bool selected);
        bool IsItemSelected(int index) const;

        void SelectAll();
        void DeselectAll();

        std::vector<int> GetSelectedIndices() const;
        std::vector<DropdownItem> GetSelectedItems() const;
        int GetSelectedCount() const { return static_cast<int>(selectedIndices.size()); }

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
        void Render(IRenderContext* ctx) override;

        void RenderPopupContent(IRenderContext* ctx) override;

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override;

    private:
        void CalculateDropdownDimensions();
        Rect2Di CalculatePopupPosition();

        void RenderButton(IRenderContext* ctx);

        void RenderDropdownArrow(const Rect2Di& buttonRect, const Color& color, IRenderContext* ctx);

        void RenderDropdownItem(int itemIndex, const Rect2Di& listRect, int visualIndex, IRenderContext* ctx);

        // NEW: Icon rendering
        void RenderItemIcon(const std::string& iconPath, const Rect2Di& iconRect, IRenderContext* ctx);

        // NEW: Checkbox rendering for multi-select
        void RenderCheckbox(bool checked, const Rect2Di& checkboxRect, IRenderContext* ctx);

        void RenderScrollbar(const Rect2Di& listRect, IRenderContext* ctx);

        std::string GetDisplayText() const;

        void EnsureItemVisible(int index);

        int GetItemAtPosition(int x, int y);

        // ===== EVENT HANDLERS =====
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
        void HandleMouseLeave(const UCEvent& event);
        void HandleKeyDown(const UCEvent& event);
        bool HandleMouseWheel(const UCEvent& event);
        void HandleFocusLost();
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasDropdown> CreateDropdown(
            const std::string& identifier, long id, long x, long y, long w, long h = 24) {
        return std::make_shared<UltraCanvasDropdown>(identifier, id, x, y, w, h);
    }
} // namespace UltraCanvas
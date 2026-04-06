// include/UltraCanvasDropdown.h
// Interactive dropdown/combobox component with icon support and multi-selection
// Uses ListView popup for rendering dropdown items
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasListModel.h"
#include "UltraCanvasListDelegate.h"
#include "UltraCanvasListSelection.h"
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <algorithm>
#include <set>

namespace UltraCanvas {

// ===== DROPDOWN-SPECIFIC DATA ROLES =====
    constexpr auto DropdownValueRole     = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 0);
    constexpr auto DropdownEnabledRole   = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 1);
    constexpr auto DropdownSeparatorRole = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 2);
    constexpr auto DropdownSelectedRole  = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 3);

// ===== DROPDOWN ITEM DATA =====
    struct DropdownItem {
        std::string text;
        std::string value;
        std::string iconPath;
        bool enabled = true;
        bool separator = false;
        bool selected = false;  // for multi-selection support
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

        // Font
        std::string fontFamily;
        float fontSize = 11.0f;

        // Scrollbar style
        ScrollbarStyle scrollbarStyle;
    };

// ===== DROPDOWN LIST MODEL (adapter over vector<DropdownItem>) =====
    class DropdownListModel : public IListModel {
    public:
        void SetItems(const std::vector<DropdownItem>* itemsPtr) { items = itemsPtr; }
        void DataChanged() { NotifyDataChanged(); }

        int GetRowCount() const override;
        int GetColumnCount() const override { return 1; }
        ListDataValue GetData(const ListIndex& index, ListDataRole role) const override;
        bool SetData(const ListIndex& index, ListDataRole role, const ListDataValue& value) override { return false; }

    private:
        const std::vector<DropdownItem>* items = nullptr;
    };

// ===== DROPDOWN ITEM DELEGATE =====
    class DropdownItemDelegate : public IItemDelegate {
    public:
        void SetStyle(const DropdownStyle* s) { style = s; }
        void SetMultiSelectEnabled(bool enabled) { multiSelectEnabled = enabled; }

        void RenderItem(IRenderContext* ctx, const IListModel* model,
                       int row, int column,
                       const ListItemStyleOption& option) override;

        int GetRowHeight(const IListModel* model, int row) const override;

    private:
        void RenderCheckbox(IRenderContext* ctx, bool checked, const Rect2Di& rect) const;
        void RenderIcon(IRenderContext* ctx, const std::string& iconPath, const Rect2Di& rect) const;

        const DropdownStyle* style = nullptr;
        bool multiSelectEnabled = false;
    };

// ===== DROPDOWN COMPONENT =====
    class UltraCanvasDropdown : public UltraCanvasUIElement {
    public:
        // ===== CALLBACKS =====
        std::function<void(int, const DropdownItem&)> onSelectionChanged;
        std::function<void(int, const DropdownItem&)> onItemHovered;
        std::function<void()> onDropdownOpened;
        std::function<void()> onDropdownClosed;
        std::function<bool(const UCEvent&)> onKeyDown;

        // Multi-selection callbacks
        std::function<void(const std::vector<int>&)> onMultiSelectionChanged;
        std::function<void(const std::vector<DropdownItem>&)> onSelectedItemsChanged;

    private:
        std::vector<DropdownItem> items;
        int selectedIndex = -1;
        bool dropdownOpen = false;
        bool buttonPressed = false;

        // Multi-selection support
        bool multiSelectEnabled = false;
        std::set<int> selectedIndices;

        DropdownStyle style;

        // ListView popup components
        std::shared_ptr<UltraCanvasListView> popupListView;
        DropdownListModel dropdownModel;
        std::shared_ptr<DropdownItemDelegate> dropdownDelegate;

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
        void SetSelectedIndex(int index, bool runNotifications = true);

        int GetSelectedIndex() const {
            return selectedIndex;
        }

        const DropdownItem* GetSelectedItem() const;

        // Multi-selection management
        void SetMultiSelectEnabled(bool enabled);
        bool IsMultiSelectEnabled() const { return multiSelectEnabled; }

        void SetItemSelected(int index, bool selected);
        bool IsItemSelected(int index) const;

        void SelectAll();
        void DeselectAll();

        std::vector<int> GetSelectedIndices() const;
        std::vector<DropdownItem> GetSelectedItems() const;
        int GetSelectedCount() const { return static_cast<int>(selectedIndices.size()); }

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

        void SetWindow(UltraCanvasWindowBase* win) override;
        // ===== RENDERING =====
        void Render(IRenderContext* ctx) override;
        void UpdateGeometry(IRenderContext *ctx) override;

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override;

    private:
        void CreatePopupListView();
        void WireListViewCallbacks();
        void ApplyStyleToListView();
        void CalculateAndSetPopupSize();
        Point2Di CalculatePopupPosition();

        void RenderButton(IRenderContext* ctx);
        void RenderDropdownArrow(const Rect2Di& buttonRect, const Color& color, IRenderContext* ctx);

        std::string GetDisplayText() const;

        // ===== EVENT HANDLERS =====
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
        void HandleMouseLeave(const UCEvent& event);
        bool HandleKeyDown(const UCEvent& event);
        void HandleFocusLost();
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasDropdown> CreateDropdown(
            const std::string& identifier, long id, long x, long y, long w, long h = 24) {
        return std::make_shared<UltraCanvasDropdown>(identifier, id, x, y, w, h);
    }
} // namespace UltraCanvas

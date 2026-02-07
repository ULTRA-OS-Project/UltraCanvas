// include/UltraCanvasToolbar.h
// Comprehensive cross-platform toolbar component with advanced features
// Version: 1.1.0
// Last Modified: 2025-11-13
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_TOOLBAR_H
#define ULTRACANVAS_TOOLBAR_H

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasBoxLayout.h"
#include "UltraCanvasGridLayout.h"
#include "UltraCanvasFlexLayout.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace UltraCanvas {

// ===== FORWARD DECLARATIONS =====
    class UltraCanvasToolbar;
    class UltraCanvasToolbarItem;
    class UltraCanvasToolbarSeparator;
    class UltraCanvasToolbarButton;
    class UltraCanvasToolbarDropdown;
    class UltraCanvasToolbarLabel;

// ===== TOOLBAR ENUMERATIONS =====

    enum class ToolbarOrientation {
        Horizontal = 0,
        Vertical = 1
    };

    enum class ToolbarPosition {
        Top = 0,
        Bottom = 1,
        Left = 2,
        Right = 3,
        Floating = 4
    };

    enum class ToolbarStyle {
        Standard = 0,      // Classic toolbar with buttons
        Flat = 1,          // Flat design without borders
        Raised = 2,        // Raised with shadows
        Docked = 3,        // Docked style (like macOS dock)
        Ribbon = 4,        // Ribbon-style with multiple rows
        StatusBar = 5,     // Status bar at bottom
        Sidebar = 6        // Vertical sidebar
    };

    enum class ToolbarItemType {
        Button = 0,
        ToggleButton = 1,
        DropdownButton = 2,
        SplitButton = 3,
        Separator = 4,
        Spacer = 5,
        Label = 6,
        TextInput = 7,
        Dropdown = 8,
        Checkbox = 9,
        RadioButton = 10,
        CustomWidget = 11,
        ButtonGroup = 12,
        SearchBox = 13
    };

    enum class ToolbarOverflowMode {
        OverflowNone = 0,           // No overflow handling
        Wrap = 1,           // Wrap items to new line
        Menu = 2,           // Move to overflow menu
        Scroll = 3,         // Allow scrolling
        Hide = 4            // Hide overflow items
    };

    enum class ToolbarIconSize {
        Small = 0,          // 16x16
        Medium = 1,         // 24x24
        Large = 2,          // 32x32
        ExtraLarge = 3,     // 48x48
        Huge = 4,           // 64x64
        Custom = 5          // User-defined size
    };

    enum class ToolbarVisibility {
        AlwaysVisible = 0,
        AutoHide = 1,
        OnHover = 2,
        OnDemand = 3
    };

    enum class ToolbarDragMode {
        DragNone = 0,
        Movable = 1,
        ReorderItems = 2,
        Both = 3
    };

// ===== TOOLBAR APPEARANCE CONFIGURATION =====

    struct ToolbarAppearance {
        // Colors
        ToolbarStyle style = ToolbarStyle::Standard;

        Color backgroundColor = Color(245, 245, 245, 255);
        Color separatorColor = Color(200, 200, 200, 255);
        Color hoverColor = Color(225, 235, 255, 255);
        Color activeColor = Color(204, 228, 247, 255);
        Color disabledColor = Color(220, 220, 220, 255);

        // Spacing
        float itemSpacing = 4.0f;
        float groupSpacing = 8.0f;

        // Shadow (for Docked style)
        bool hasShadow = false;
        Color shadowColor = Color(0, 0, 0, 60);
        Point2Di shadowOffset = Point2Di(0, 2);
        float shadowBlur = 4.0f;

        // Animation
        bool enableAnimations = true;
        float animationDuration = 0.2f; // seconds

        // Icon appearance
        ToolbarIconSize iconSize = ToolbarIconSize::Medium;
        int customIconWidth = 24;
        int customIconHeight = 24;
        bool showIconLabels = true;
        bool centerIcons = false;

        // Dock-style effects (for ToolbarStyle::Docked)
        bool enableMagnification = false;
        float magnificationScale = 1.5f;
        float magnificationRadius = 100.0f;

        static ToolbarAppearance Default() {
            return ToolbarAppearance();
        }

        static ToolbarAppearance Flat() {
            ToolbarAppearance app;
            app.style = ToolbarStyle::Flat;
            app.hasShadow = false;
            app.backgroundColor = Color(240, 240, 240, 255);
            app.hoverColor = Color(220, 220, 255);
            return app;
        }

        static ToolbarAppearance MacOSDock() {
            ToolbarAppearance app;
            app.style = ToolbarStyle::Docked;
            app.backgroundColor = Color(255, 255, 255, 200);
            app.hasShadow = true;
            app.shadowColor = Color(0, 0, 0, 100);
            app.shadowOffset = Point2Di(0, 4);
            app.shadowBlur = 8.0f;
            app.enableMagnification = true;
            app.magnificationScale = 1.8f;
            app.showIconLabels = false;
            app.centerIcons = true;
            app.iconSize = ToolbarIconSize::Large;
            return app;
        }

        static ToolbarAppearance Ribbon() {
            ToolbarAppearance app;
            app.style = ToolbarStyle::Ribbon;
            app.backgroundColor = Color(248, 248, 248, 255);
            app.groupSpacing = 16.0f;
            return app;
        }

        static ToolbarAppearance StatusBar() {
            ToolbarAppearance app;
            app.style = ToolbarStyle::StatusBar;
            return app;
        }

        static ToolbarAppearance Sidebar() {
            ToolbarAppearance app;
            app.style = ToolbarStyle::Sidebar;
            return app;
        }
    };

// ===== TOOLBAR ITEM DESCRIPTOR =====

    struct ToolbarItemDescriptor {
        ToolbarItemType type = ToolbarItemType::Button;
        std::string identifier;
        std::string text;
        std::string iconPath;
        std::string tooltip;

        // Button properties
        bool isToggle = false;
        bool isChecked = false;
        bool isEnabled = true;
        bool isVisible = true;

        // Priority for overflow handling
        int visibilityPriority = 0; // Higher = stays visible longer

        // Dropdown items (for dropdown buttons/menus)
        std::vector<std::string> dropdownItems;

        // Callbacks
        std::function<void()> onClick;
        std::function<void(bool)> onToggle;
        std::function<void(const std::string&)> onDropdownSelect;
        std::function<void(const std::string&)> onTextChange;

        // Layout constraints
        int minWidth = 0;
        int maxWidth = 0;
        int fixedWidth = 0;
        float stretch = 0.0f;

        // Badge/notification
        bool hasBadge = false;
        std::string badgeText;
        Color badgeColor = Color(255, 0, 0, 255);

        // Factory methods
        static ToolbarItemDescriptor CreateButton(
                const std::string& id,
                const std::string& text,
                const std::string& icon = "",
                std::function<void()> onClick = nullptr
        ) {
            ToolbarItemDescriptor desc;
            desc.type = ToolbarItemType::Button;
            desc.identifier = id;
            desc.text = text;
            desc.iconPath = icon;
            desc.onClick = onClick;
            return desc;
        }

        static ToolbarItemDescriptor CreateToggleButton(
                const std::string& id,
                const std::string& text,
                const std::string& icon = "",
                std::function<void(bool)> onToggle = nullptr
        ) {
            ToolbarItemDescriptor desc;
            desc.type = ToolbarItemType::ToggleButton;
            desc.identifier = id;
            desc.text = text;
            desc.iconPath = icon;
            desc.isToggle = true;
            desc.onToggle = onToggle;
            return desc;
        }

        static ToolbarItemDescriptor CreateDropdown(
                const std::string& id,
                const std::string& text,
                const std::vector<std::string>& items,
                std::function<void(const std::string&)> onSelect = nullptr
        ) {
            ToolbarItemDescriptor desc;
            desc.type = ToolbarItemType::Dropdown;
            desc.identifier = id;
            desc.text = text;
            desc.dropdownItems = items;
            desc.onDropdownSelect = onSelect;
            return desc;
        }

        static ToolbarItemDescriptor CreateSeparator(const std::string& id = "") {
            ToolbarItemDescriptor desc;
            desc.type = ToolbarItemType::Separator;
            desc.identifier = id.empty() ? "sep_" + std::to_string(rand()) : id;
            return desc;
        }

        static ToolbarItemDescriptor CreateSpacer(int size = 8) {
            ToolbarItemDescriptor desc;
            desc.type = ToolbarItemType::Spacer;
            desc.identifier = "spacer_" + std::to_string(rand());
            desc.fixedWidth = size;
            return desc;
        }

        static ToolbarItemDescriptor CreateFlexSpacer(float stretch = 1.0f) {
            ToolbarItemDescriptor desc;
            desc.type = ToolbarItemType::Spacer;
            desc.identifier = "flexspacer_" + std::to_string(rand());
            desc.stretch = stretch;
            return desc;
        }

        static ToolbarItemDescriptor CreateLabel(
                const std::string& id,
                const std::string& text
        ) {
            ToolbarItemDescriptor desc;
            desc.type = ToolbarItemType::Label;
            desc.identifier = id;
            desc.text = text;
            return desc;
        }
    };

// ===== TOOLBAR ITEM BASE CLASS =====

    class UltraCanvasToolbarItem {
    friend UltraCanvasToolbar;
    protected:
        ToolbarItemType itemType;
        std::string identifier;
        std::shared_ptr<UltraCanvasUIElement> widget;
        bool isEnabled = true;
        bool isVisible = true;
        int visibilityPriority = 0;

    public:
        UltraCanvasToolbarItem(ToolbarItemType type, const std::string& id)
                : itemType(type), identifier(id) {}

        virtual ~UltraCanvasToolbarItem() = default;

        ToolbarItemType GetType() const { return itemType; }
        const std::string& GetIdentifier() const { return identifier; }
        std::shared_ptr<UltraCanvasUIElement> GetWidget() { return widget; }

        bool IsEnabled() const { return isEnabled; }
        void SetEnabled(bool enabled) {
            isEnabled = enabled;
            if (widget) {
                widget->SetDisabled(!enabled);
            }
        }

        bool IsVisible() const { return isVisible; }
        void SetVisible(bool visible) {
            isVisible = visible;
            if (widget) {
                widget->SetVisible(visible);
            }
        }

        int GetVisibilityPriority() const { return visibilityPriority; }
        void SetVisibilityPriority(int priority) { visibilityPriority = priority; }

        virtual void UpdateAppearance(const ToolbarAppearance& appearance) = 0;
        virtual int GetPreferredWidth() const = 0;
        virtual int GetPreferredHeight() const = 0;
    };

// ===== TOOLBAR SEPARATOR =====
// Simplified to use UltraCanvasUIElement directly instead of UltraCanvasContainer

    class UltraCanvasToolbarSeparator : public UltraCanvasToolbarItem {
    private:
        bool isVertical = true;
        Color color = Color(200, 200, 200, 255);
        int thickness = 1;
        int length = 24;

    public:
        UltraCanvasToolbarSeparator(const std::string& id, bool vertical);

        void SetColor(const Color& c) { color = c; }
        void SetThickness(int t) { thickness = t; }
        void SetLength(int l) { length = l; }

        void UpdateAppearance(const ToolbarAppearance& appearance) override;
        int GetPreferredWidth() const override;
        int GetPreferredHeight() const override;
    };

// ===== TOOLBAR BUTTON =====

    class UltraCanvasToolbarButton : public UltraCanvasToolbarItem {
    private:
        std::string text;
        std::string iconPath;
        std::string tooltip;
        bool isToggle = false;
        bool isChecked = false;

        // Badge
        bool hasBadge = false;
        std::string badgeText;
        Color badgeColor = Color(255, 0, 0, 255);

        // Callbacks
        std::function<void()> onClickCallback;
        std::function<void(bool)> onToggleCallback;

    public:
        UltraCanvasToolbarButton(const std::string& id, const std::string& txt,
                                 const std::string& icon = "");

        void SetText(const std::string& txt);
        void SetIcon(const std::string& icon);
        void SetTooltip(const std::string& tip);
        void SetToggleMode(bool toggle);
        void SetChecked(bool checked);
        void SetOnClick(std::function<void()> callback);
        void SetOnToggle(std::function<void(bool)> callback);
        void SetBadge(const std::string& text, const Color& color = Color(255, 0, 0, 255));
        void ClearBadge();

        const std::string& GetText() const { return text; }
        bool IsToggle() const { return isToggle; }
        bool IsChecked() const { return isChecked; }
        bool HasBadge() const { return hasBadge; }

        void UpdateAppearance(const ToolbarAppearance& appearance) override;
        int GetPreferredWidth() const override;
        int GetPreferredHeight() const override;
    };

// ===== TOOLBAR DROPDOWN =====

    class UltraCanvasToolbarDropdown : public UltraCanvasToolbarItem {
    private:
        std::string text;
        std::vector<std::string> items;
        int selectedIndex = -1;
        std::function<void(const std::string&)> onSelectCallback;

    public:
        UltraCanvasToolbarDropdown(const std::string& id, const std::string& txt);

        void SetText(const std::string& txt);
        void AddItem(const std::string& item);
        void SetItems(const std::vector<std::string>& itemList);
        void SetSelectedIndex(int index);
        void SetOnSelect(std::function<void(const std::string&)> callback);

        const std::vector<std::string>& GetItems() const { return items; }
        int GetSelectedIndex() const { return selectedIndex; }

        void UpdateAppearance(const ToolbarAppearance& appearance) override;
        int GetPreferredWidth() const override;
        int GetPreferredHeight() const override;
    };

// ===== TOOLBAR LABEL =====

    class UltraCanvasToolbarLabel : public UltraCanvasToolbarItem {
    private:
        std::string text;
        TextAlignment alignment = TextAlignment::Left;
        Color textColor = Color(0, 0, 0, 255);
        float fontSize = 12.0f;
        FontWeight fontWeight = FontWeight::Normal;

    public:
        UltraCanvasToolbarLabel(const std::string& id, const std::string& txt);

        void SetText(const std::string& txt);
        void SetAlignment(TextAlignment align);
        void SetTextColor(const Color& color);
        void SetFontSize(float size);
        void SetFontWeight(FontWeight weight);

        const std::string& GetText() const { return text; }

        void UpdateAppearance(const ToolbarAppearance& appearance) override;
        int GetPreferredWidth() const override;
        int GetPreferredHeight() const override;
    };

// ===== MAIN TOOLBAR CLASS =====

    class UltraCanvasToolbar : public UltraCanvasContainer {
    private:
        // Configuration
        ToolbarOrientation orientation = ToolbarOrientation::Horizontal;
        ToolbarPosition position = ToolbarPosition::Top;
        ToolbarAppearance appearance;
        ToolbarOverflowMode overflowMode = ToolbarOverflowMode::OverflowNone;
        ToolbarVisibility visibility = ToolbarVisibility::AlwaysVisible;
        ToolbarDragMode dragMode = ToolbarDragMode::DragNone;

        // Layout management
        UltraCanvasBoxLayout* boxLayout = nullptr;

        // Items
        std::vector<std::shared_ptr<UltraCanvasToolbarItem>> items;
        std::unordered_map<std::string, std::shared_ptr<UltraCanvasToolbarItem>> itemMap;

        // Overflow management
        std::shared_ptr<UltraCanvasMenu> overflowMenu;
        std::shared_ptr<UltraCanvasButton> overflowButton;
        std::vector<std::shared_ptr<UltraCanvasToolbarItem>> overflowItems;

        // Auto-hide state
        bool isAutoHidden = false;
        bool isHovered = false;
        float autoHideDelay = 0.5f; // seconds

        // Drag state
        bool isDragging = false;
        Point2Di dragStartPos;
        Point2Di originalPos;

        // Magnification (for dock-style toolbars)
        int hoveredItemIndex = -1;
        Point2Di mousePosition;

    public:
        UltraCanvasToolbar(const std::string& identifier, long id, long x, long y,
                           long width, long height);
        virtual ~UltraCanvasToolbar() = default;

        // ===== CONFIGURATION =====
        void SetOrientation(ToolbarOrientation orient);
        void SetToolbarPosition(ToolbarPosition pos);
        void SetAppearance(const ToolbarAppearance& app);
        void SetOverflowMode(ToolbarOverflowMode mode);
        void SetVisibility(ToolbarVisibility vis);
        void SetDragMode(ToolbarDragMode mode);

        ToolbarOrientation GetOrientation() const { return orientation; }
        ToolbarPosition GetPosition() const { return position; }
        const ToolbarAppearance& GetAppearance() const { return appearance; }

        // ===== ITEM MANAGEMENT =====
        void AddItem(const ToolbarItemDescriptor& descriptor);
        void AddItem(std::shared_ptr<UltraCanvasToolbarItem> item);
        void InsertItem(int index, const ToolbarItemDescriptor& descriptor);
        void InsertItem(int index, std::shared_ptr<UltraCanvasToolbarItem> item);
        void RemoveItem(const std::string& identifier);
        void RemoveItemAt(int index);
        void ClearItems();

        // Item access
        std::shared_ptr<UltraCanvasToolbarItem> GetItem(const std::string& identifier);
        std::shared_ptr<UltraCanvasToolbarItem> GetItemAt(int index);
        int GetItemCount() const { return static_cast<int>(items.size()); }

        // Convenience methods
        void AddButton(const std::string& id, const std::string& text,
                       const std::string& icon = "", std::function<void()> onClick = nullptr);
        void AddToggleButton(const std::string& id, const std::string& text,
                             const std::string& icon = "", std::function<void(bool)> onToggle = nullptr);
        void AddDropdownButton(const std::string& id, const std::string& text,
                               const std::vector<std::string>& items,
                               std::function<void(const std::string&)> onSelect = nullptr);
        void AddSeparator(const std::string& id = "");
        void AddSpacer(int size = 8);
        void AddStretch(float stretch = 1.0f);
        void AddLabel(const std::string& id, const std::string& text);
        void AddSearchBox(const std::string& id, const std::string& placeholder = "Search...",
                          std::function<void(const std::string&)> onTextChange = nullptr);

        // ===== LAYOUT =====
        void HandleOverflow();

        // ===== RENDERING =====
        void Render(IRenderContext* ctx) override;
        bool OnEvent(const UCEvent& event) override;

        // ===== AUTO-HIDE =====
        void SetAutoHideDelay(float delay) { autoHideDelay = delay; }
        void ShowToolbar();
        void HideToolbar();
        bool IsAutoHidden() const { return isAutoHidden; }

        // ===== DRAG & DROP =====
        void EnableItemReordering(bool enable);
        void BeginDrag(const Point2Di& startPos);
        void UpdateDrag(const Point2Di& currentPos);
        void EndDrag();

        // ===== CALLBACKS =====
        std::function<void()> onToolbarShow;
        std::function<void()> onToolbarHide;
        std::function<void(const std::string&)> onItemAdded;
        std::function<void(const std::string&)> onItemRemoved;
        std::function<void(int, int)> onItemReordered;
        std::function<void(ToolbarPosition)> onPositionChanged;

    private:
        // Internal helpers
        void CreateLayout();
        void UpdateItemAppearances();
        void CreateOverflowMenu();
        void UpdateOverflowButton();
        void CalculateMagnification();
        void RenderDockMagnification(IRenderContext* ctx);
        void RenderShadow(IRenderContext* ctx);

        std::shared_ptr<UltraCanvasToolbarItem> CreateToolbarItem(
                const ToolbarItemDescriptor& descriptor);
    };

// ===== TOOLBAR BUILDER =====

    class UltraCanvasToolbarBuilder {
    private:
        std::shared_ptr<UltraCanvasToolbar> toolbar;

    public:
        UltraCanvasToolbarBuilder(const std::string& identifier, long id = 0);

        UltraCanvasToolbarBuilder& SetOrientation(ToolbarOrientation orient);
        UltraCanvasToolbarBuilder& SetToolbarPosition(ToolbarPosition pos);
        UltraCanvasToolbarBuilder& SetAppearance(const ToolbarAppearance& app);
        UltraCanvasToolbarBuilder& SetOverflowMode(ToolbarOverflowMode mode);
        UltraCanvasToolbarBuilder& SetDimensions(int x, int y, int width, int height);

        UltraCanvasToolbarBuilder& AddButton(const std::string& id, const std::string& text,
                                             const std::string& icon = "",
                                             std::function<void()> onClick = nullptr);
        UltraCanvasToolbarBuilder& AddToggleButton(const std::string& id, const std::string& text,
                                                   const std::string& icon = "",
                                                   std::function<void(bool)> onToggle = nullptr);
        UltraCanvasToolbarBuilder& AddDropdownButton(const std::string& id, const std::string& text,
                                                     const std::vector<std::string>& items,
                                                     std::function<void(const std::string&)> onSelect = nullptr);
        UltraCanvasToolbarBuilder& AddSeparator(const std::string& id = "");
        UltraCanvasToolbarBuilder& AddSpacer(int size = 8);
        UltraCanvasToolbarBuilder& AddStretch(float stretch = 1.0f);
        UltraCanvasToolbarBuilder& AddLabel(const std::string& id, const std::string& text);

        std::shared_ptr<UltraCanvasToolbar> Build();
    };

// ===== PRESET TOOLBAR FACTORIES =====

    namespace ToolbarPresets {
        // Standard horizontal toolbar
        std::shared_ptr<UltraCanvasToolbar> CreateStandardToolbar(
                const std::string& identifier = "Toolbar"
        );

        // macOS-style dock
        std::shared_ptr<UltraCanvasToolbar> CreateDockStyleToolbar(
                const std::string& identifier = "Dock"
        );

        // Ribbon-style toolbar with multiple rows
        std::shared_ptr<UltraCanvasToolbar> CreateRibbonToolbar(
                const std::string& identifier = "Ribbon"
        );

        // Vertical sidebar
        std::shared_ptr<UltraCanvasToolbar> CreateSidebarToolbar(
                const std::string& identifier = "Sidebar"
        );

        // Status bar
        std::shared_ptr<UltraCanvasToolbar> CreateStatusBar(
                const std::string& identifier = "StatusBar"
        );
    }

} // namespace UltraCanvas

#endif // ULTRACANVAS_TOOLBAR_H
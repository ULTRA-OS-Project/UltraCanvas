// UltraCanvasTabbedContainer.h
// Enhanced tabbed container component with overflow dropdown and search functionality
// Version: 1.7.0
// Last Modified: 2025-11-19
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUtils.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <algorithm>
#include <cctype>

namespace UltraCanvas {

// ===== TAB POSITIONS =====
    enum class TabPosition {
        Top,
        Bottom,
        Left,
        Right
    };

// ===== TAB STYLES =====
    enum class TabStyle {
        Classic,        // Traditional rectangular tabs
        Modern,         // Flat with subtle borders
        Flat,          // Minimal style, no borders
        Rounded,       // Browser-style rounded tops
        Custom         // User-defined rendering
    };

// ===== TAB CLOSE BEHAVIOR =====
    enum class TabCloseMode {
        NoClose,           // No close buttons
        Closable,          // Close buttons on all tabs
        ClosableExceptFirst // Close buttons on all except first tab
    };

// ===== OVERFLOW DROPDOWN POSITION =====
    enum class OverflowDropdownPosition {
        Off,        // No dropdown shown
        Left,       // Dropdown at left side of tabs
        Right       // Dropdown at right side of tabs
    };

// ===== NEW TAB BUTTON STYLE =====
    enum class NewTabButtonStyle {
        NoButton,              // No new tab button
        PlusIcon,          // Simple "+" button (Style A)
        RoundedWithIcon,   // Rounded button with "+" icon (Style B)
        Custom             // User-defined button
    };

// ===== NEW TAB BUTTON POSITION =====
    enum class NewTabButtonPosition {
        AfterTabs,         // Right after the last tab
        FarRight,          // At the far right of the tab bar
        BeforeTabs         // Left of the first tab
    };

// ===== TAB DATA =====
    struct TabData {
        std::string title;
        std::string tooltip;
        std::string iconPath;           // Path to tab icon (16x16 recommended)
        std::string badgeText;
        int badgeWidth = 0;
        int badgeHeight = 0;
        bool enabled = true;
        bool visible = true;
        bool closable = true;
        bool hasIcon = false;
        bool hasBadge = false;
        bool showBadge = false;
        Color textColor = Colors::Black;
        Color backgroundColor = Color(240, 240, 240);
        std::shared_ptr<UltraCanvasUIElement> content = nullptr;
        void* userData = nullptr;

        TabData(const std::string& tabTitle) : title(tabTitle) {}
    };

// ===== TABBED CONTAINER COMPONENT =====
    class UltraCanvasTabbedContainer : public UltraCanvasContainer {
    public:
        // ===== TAB MANAGEMENT =====
        std::vector<std::unique_ptr<TabData>> tabs;
        int activeTabIndex = -1;
        int hoveredTabIndex = -1;
        int hoveredCloseButtonIndex = -1;

        // ===== TAB BAR LAYOUT =====
        TabPosition tabPosition = TabPosition::Top;
        TabStyle tabStyle = TabStyle::Rounded;
        TabCloseMode closeMode = TabCloseMode::NoClose;
        int tabHeight = 32;
        int tabMinWidth = 80;
        int tabMaxWidth = 200;
        int tabSpacing = 2;
        int tabPadding = 12;
        bool tabbarLayoutDirty = true;

        // ===== TAB STYLING =====
        float tabCornerRadius = 8.0f;
        float tabElevation = 1.0f;
        int fontSize = 11;
        int iconSize = 16;
        int iconPadding = 4;
        int closeButtonSize = 16;
        int closeButtonMargin = 4;
        bool showTabSeparators = false;

        // ===== COLORS =====
        Color tabBarColor = Color(230, 230, 230);
        Color activeTabColor = Color(255, 255, 255);
        Color inactiveTabColor = Color(240, 240, 240);
        Color hoveredTabColor = Color(250, 250, 250);
        Color disabledTabColor = Color(200, 200, 200);
        Color tabBorderColor = Color(180, 180, 180);
        Color activeTabTextColor = Colors::Black;
        Color inactiveTabTextColor = Color(80, 80, 80);
        Color disabledTabTextColor = Color(150, 150, 150);
        Color closeButtonColor = Color(120, 120, 120);
        Color closeButtonHoverColor = Color(200, 50, 50);
        Color contentAreaColor = Color(255, 255, 255);
        Color badgeBackgroundColor = Color(220, 50, 50);
        Color badgeTextColor = Colors::White;
        Color tabSeparatorColor = Color(200, 200, 200);

        // ===== OVERFLOW DROPDOWN =====
        OverflowDropdownPosition overflowDropdownPosition = OverflowDropdownPosition::Off;
        bool showOverflowDropdown = false;
        bool overflowDropdownVisible = false;
        int overflowDropdownWidth = 24;
        std::shared_ptr<UltraCanvasDropdown> overflowDropdown = nullptr;

        // ===== DROPDOWN SEARCH =====
        bool enableDropdownSearch = true;
        int dropdownSearchThreshold = 5;
        bool dropdownSearchActive = false;
        std::string dropdownSearchText = "";

        // ===== NEW TAB BUTTON =====
        NewTabButtonStyle newTabButtonStyle = NewTabButtonStyle::NoButton;
        NewTabButtonPosition newTabButtonPosition = NewTabButtonPosition::AfterTabs;
        bool showNewTabButton = false;
        int newTabButtonWidth = 32;
        int newTabButtonHeight = 28;
        bool hoveredNewTabButton = false;
        Color newTabButtonColor = Color(240, 240, 240);
        Color newTabButtonHoverColor = Color(220, 220, 220);
        Color newTabButtonIconColor = Color(100, 100, 100);
        std::function<void()> onNewTabRequest = nullptr;

        // ===== SCROLLING =====
        bool enableTabScrolling = true;
        int tabScrollOffset = 0;
        int maxVisibleTabs = 0;
        bool showScrollButtons = false;

        // ===== DRAG AND DROP =====
        bool allowTabReordering = false;
        bool allowTabDragOut = false;
        int draggingTabIndex = -1;
        Point2Di dragStartPosition;
        bool isDraggingTab = false;

        // ===== CALLBACKS (USING CORRECT BASE VERB FORMS) =====
        std::function<void(int, int)> onTabChange;           // (oldIndex, newIndex)
        std::function<void(int)> onTabSelect;                // (tabIndex)
        std::function<void(int)> onTabCloseRequest;          // (tabIndex)
        std::function<bool(int)> onTabClose;                 // (tabIndex) - return false to prevent
        std::function<void(int, int)> onTabReorder;          // (fromIndex, toIndex)
        std::function<void(int, const std::string&)> onTabRename; // (tabIndex, newTitle)
        std::function<void()> onTabBarRightClick;

        UltraCanvasTabbedContainer(const std::string& elementId, long uniqueId, long posX, long posY, long w, long h);

        void InvalidateTabbar() { tabbarLayoutDirty = true; RequestRedraw(); }

        void SetTabHeight(int th);
        int GetTabHeight() const { return tabHeight; }
        void SetTabMinWidth(int w);
        int GetTabMinWidth() const { return tabMinWidth; }
        void SetTabMaxWidth(int w);
        int GetTabMaxWidth() const { return tabMaxWidth; }
        void SetTabCornerRadius(float radius) { tabCornerRadius = radius; InvalidateTabbar(); }
        float GetTabCornerRadius() const { return tabCornerRadius; }
        void SetTabElevation(float elevation) { tabElevation = elevation; InvalidateTabbar(); }
        float GetTabElevation() const { return tabElevation; }
        void SetIconSize(int size) { iconSize = size; InvalidateTabbar(); }
        int GetIconSize() const { return iconSize; }
        bool CalcBadgeDimensions(TabData* tabData);

        void SetNewTabButtonWidth(int w) { newTabButtonWidth = w; }
        void SetInactiveTabBackgroundColor(const Color& c) {
            inactiveTabColor = c;
        }
        void SetInactiveTabTextColor(const Color& c) {
            inactiveTabTextColor = c;
        }

        // ===== OVERFLOW DROPDOWN CONFIGURATION =====
        void SetOverflowDropdownPosition(OverflowDropdownPosition position);
        OverflowDropdownPosition GetOverflowDropdownPosition() const { return overflowDropdownPosition; }
        void SetOverflowDropdownWidth(int width);

        // ===== DROPDOWN SEARCH CONFIGURATION =====
        void SetDropdownSearchEnabled(bool enabled);
        bool IsDropdownSearchEnabled() const { return enableDropdownSearch; }
        void SetDropdownSearchThreshold(int threshold);
        int GetDropdownSearchThreshold() const { return dropdownSearchThreshold; }
        void ClearDropdownSearch();
        std::string GetDropdownSearchText() const { return dropdownSearchText; }

        // ===== NEW TAB BUTTON CONFIGURATION =====
        void SetNewTabButtonStyle(NewTabButtonStyle style);
        NewTabButtonStyle GetNewTabButtonStyle() const { return newTabButtonStyle; }
        void SetNewTabButtonPosition(NewTabButtonPosition position);
        NewTabButtonPosition GetNewTabButtonPosition() const { return newTabButtonPosition; }
        void SetShowNewTabButton(bool show);
        bool GetShowNewTabButton() const { return showNewTabButton; }
        void SetNewTabButtonSize(int width, int height);

        // ===== TAB MANAGEMENT =====
        int AddTab(const std::string& title, std::shared_ptr<UltraCanvasUIElement> content = nullptr);
        void RemoveTab(int index);
        void SetActiveTab(int index);

        // ===== TAB ICON AND BADGE METHODS =====
        void SetTabIcon(int index, const std::string& iconPath);
        std::string GetTabIcon(int index) const;
        void SetTabBadge(int index, const std::string & text, bool show);
        void ClearTabBadge(int index);
        std::string GetTabBadgeText(int index);
        bool IsTabBadgeVisible(int index) const;

        // ===== OVERFLOW DROPDOWN METHODS =====
        void InitializeOverflowDropdown();
        void UpdateOverflowDropdown();
        void UpdateOverflowDropdownVisibility();
        bool CheckIfOverflowDropdownNeeded();
        void PositionOverflowDropdown();

        // ===== RENDERING =====
        void Render(IRenderContext* ctx) override;
        void RenderTabBar(IRenderContext* ctx);
        void RenderTab(int index, IRenderContext* ctx);
        void RenderTabIcon(int index, IRenderContext* ctx);
        void RenderTabBadge(int index, IRenderContext* ctx);
        void RenderCloseButton(int index, IRenderContext* ctx);
        void RenderScrollButtons(IRenderContext* ctx) const;
        void RenderContentArea(IRenderContext* ctx);
        void RenderNewTabButton(IRenderContext* ctx);

        // ===== EVENT HANDLING (CORRECTED TO RETURN BOOL) =====
        bool OnEvent(const UCEvent& event) override;
        bool HandleDropdownSearchInput(const UCEvent& event);
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
        bool HandleKeyDown(const UCEvent& event);

        // ===== DROPDOWN SEARCH UTILITY METHODS =====
        std::vector<int> GetFilteredTabIndices() const;

        // ===== UTILITY METHODS =====
        int CalculateTabWidth(int index);
        std::string GetTruncatedTabText(IRenderContext* ctx, const std::string& text, int maxWidth) const;
        Rect2Di GetTabAreaBounds() const;
        Rect2Di GetTabBarBounds() const;
        Rect2Di GetContentAreaBounds() const;
        Rect2Di GetTabBounds(int index);
        Rect2Di GetCloseButtonBounds(int index);
        Rect2Di GetNewTabButtonBounds();
        int GetTabAtPosition(int x, int y);
        bool ShouldShowCloseButton(const TabData* tab);
        void CalculateLayout();
        void ScrollTabs(int direction);
        void ReorderTabs(int fromIndex, int toIndex);
        void EnsureTabVisible(int index);
        void PositionTabContent(int index);
        void UpdateContentVisibility();

        // ===== GETTERS AND SETTERS =====
        int GetActiveTab() const { return activeTabIndex; }
        int GetTabCount() const { return (int)tabs.size(); }

        void SetTabTitle(int index, const std::string& title);
        std::string GetTabTitle(int index) const;

        void SetTabEnabled(int index, bool enabled);
        bool IsTabEnabled(int index) const;

        void SetTabPosition(TabPosition position);
        TabPosition GetTabPosition() const { return tabPosition; }

        void SetTabStyle(TabStyle style) { tabStyle = style; InvalidateTabbar(); }
        TabStyle GetTabStyle() const { return tabStyle; }

        void SetCloseMode(TabCloseMode mode);
        TabCloseMode GetCloseMode() const { return closeMode; }

        // ===== PER-TAB STYLING =====
        void SetShowTabSeparators(bool show);
        bool GetShowTabSeparators() const { return showTabSeparators; }
        void SetTabSeparatorColor(const Color& color);
        Color GetTabSeparatorColor() const { return tabSeparatorColor; }

        void SetTabBackgroundColor(int index, const Color& color);
        Color GetTabBackgroundColor(int index) const;
        void SetTabTextColor(int index, const Color& color);
        Color GetTabTextColor(int index) const;
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasTabbedContainer> CreateTabbedContainerWithDropdown(
            const std::string& id, long uid, long x, long y, long width, long height,
            OverflowDropdownPosition dropdownPos = OverflowDropdownPosition::Left,
            bool enableSearch = true, int searchThreshold = 5) {

        auto container = std::make_shared<UltraCanvasTabbedContainer>(id, uid, x, y, width, height);
        container->SetOverflowDropdownPosition(dropdownPos);
        container->SetDropdownSearchEnabled(enableSearch);
        container->SetDropdownSearchThreshold(searchThreshold);
        return container;
    }

    inline std::shared_ptr<UltraCanvasTabbedContainer> CreateTabbedContainer(
            const std::string& id, long uid, long x, long y, long width, long height) {
        return std::make_shared<UltraCanvasTabbedContainer>(id, uid, x, y, width, height);
    }

} // namespace UltraCanvas
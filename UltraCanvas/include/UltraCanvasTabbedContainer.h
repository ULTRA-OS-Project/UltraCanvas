// include/UltraCanvasTabbedContainer.h
// Enhanced tabbed container component with overflow dropdown and search functionality
// Version: 1.6.0
// Last Modified: 2025-09-14
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
        Classic,
        Modern,
        Flat,
        Rounded,
        Custom
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

// ===== TAB DATA =====
    struct TabData {
        std::string title;
        std::string tooltip;
        bool enabled = true;
        bool visible = true;
        bool closable = true;
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

        // ===== APPEARANCE =====
        TabPosition tabPosition = TabPosition::Top;
        TabStyle tabStyle = TabStyle::Classic;
        TabCloseMode closeMode = TabCloseMode::NoClose;

        // ===== OVERFLOW DROPDOWN SETTINGS =====
        OverflowDropdownPosition overflowDropdownPosition = OverflowDropdownPosition::Left;
        bool showOverflowDropdown = true;
        int overflowDropdownWidth = 26;
        bool overflowDropdownVisible = false;
        std::shared_ptr<UltraCanvasDropdown> overflowDropdown;

        // ===== DROPDOWN SEARCH SETTINGS =====
        bool enableDropdownSearch = false;
        int dropdownSearchThreshold = 15;
        std::string dropdownSearchText = "";
        bool dropdownSearchActive = false;

        // ===== COLORS =====
        Color tabBarColor = Color(230, 230, 230);
        Color activeTabColor = Colors::White;
        Color inactiveTabColor = Color(240, 240, 240);
        Color hoveredTabColor = Color(250, 250, 250);
        Color disabledTabColor = Color(200, 200, 200);
        Color tabBorderColor = Colors::Gray;
        Color contentAreaColor = Colors::White;
        Color closeButtonColor = Color(150, 150, 150);
        Color closeButtonHoverColor = Color(255, 100, 100);
        Color overflowDropdownColor = Color(200, 200, 200);
        Color overflowDropdownHoverColor = Color(180, 180, 180);

        // ===== TEXT PROPERTIES =====
        Color activeTabTextColor = Colors::Black;
        Color inactiveTabTextColor = Color(100, 100, 100);
        Color disabledTabTextColor = Color(150, 150, 150);
        std::string fontFamily = "Arial";
        float fontSize = 12;

        // ===== LAYOUT =====
        int tabHeight = 30;
        int tabMinWidth = 60;
        int tabMaxWidth = 200;
        int tabPadding = 10;
        int tabSpacing = 0;
        int closeButtonSize = 16;
        int closeButtonMargin = 4;
        bool autoSizeTab = true;
        bool showTabTooltips = true;

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

        void SetTabHeight(int th);
        int GetTabHeight() const { return tabHeight; }
        void SetTabMinWidth(int w);
        int GetTabMinWidth() const { return tabMinWidth; }
        void SetTabMaxWidth(int w);
        int GetTabMaxWidth() const { return tabMaxWidth; }

        // ===== OVERFLOW DROPDOWN CONFIGURATION =====
        void SetOverflowDropdownPosition(OverflowDropdownPosition position);

        OverflowDropdownPosition GetOverflowDropdownPosition() const { return overflowDropdownPosition; }

        void SetOverflowDropdownWidth(int width);

        // ===== DROPDOWN SEARCH CONFIGURATION =====
        void SetDropdownSearchEnabled(bool enabled);

        bool IsDropdownSearchEnabled() const {
            return enableDropdownSearch;
        }

        void SetDropdownSearchThreshold(int threshold);

        int GetDropdownSearchThreshold() const {
            return dropdownSearchThreshold;
        }

        void ClearDropdownSearch();

        std::string GetDropdownSearchText() const {
            return dropdownSearchText;
        }

        // ===== TAB MANAGEMENT =====
        int AddTab(const std::string& title, std::shared_ptr<UltraCanvasUIElement> content = nullptr);
        void RemoveTab(int index);
        void SetActiveTab(int index);

        // ===== OVERFLOW DROPDOWN METHODS =====
        void InitializeOverflowDropdown();
        void UpdateOverflowDropdown();
        void UpdateOverflowDropdownVisibility();
        bool CheckIfOverflowDropdownNeeded() const;
        void PositionOverflowDropdown();

        // ===== RENDERING =====
        void Render() override;
        void RenderTabBar(IRenderContext* ctx);
        void RenderTab(int index, IRenderContext* ctx);
        void RenderCloseButton(int index, IRenderContext* ctx);
        void RenderScrollButtons(IRenderContext* ctx) const;
        void RenderContentArea(IRenderContext* ctx);

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
        int CalculateTabWidth(int index) const;
        std::string GetTruncatedTabText(IRenderContext* ctx, const std::string& text, int maxWidth) const;
        Rect2Di GetTabAreaBounds() const;
        Rect2Di GetTabBarBounds() const;
        Rect2Di GetContentAreaBounds() const;
        Rect2Di GetTabBounds(int index) const;
        Rect2Di GetCloseButtonBounds(int index) const;
        int GetTabAtPosition(int x, int y) const;
        bool ShouldShowCloseButton(const TabData* tab) const;
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

        void SetTabStyle(TabStyle style) {
            tabStyle = style;
        }
        TabStyle GetTabStyle() const { return tabStyle; }

        void SetCloseMode(TabCloseMode mode);
        TabCloseMode GetCloseMode() const { return closeMode; }
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasTabbedContainer> CreateTabbedContainerWithDropdown(
            const std::string& id, long uid, long x, long y, long width, long height,
            OverflowDropdownPosition dropdownPos = OverflowDropdownPosition::Left,
            bool enableSearch = true, int searchThreshold = 15) {

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
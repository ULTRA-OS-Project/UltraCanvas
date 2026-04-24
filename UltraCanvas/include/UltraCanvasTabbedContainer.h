// include/UltraCanvasTabbedContainer.h
// Enhanced tabbed container component with overflow dropdown, search, drag-out, drag-in
// Version: 2.0.2
// Last Modified: 2026-04-24
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasAutoComplete.h"
#include "UltraCanvasMenu.h"
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
        Flat,           // Minimal style, no borders
        Rounded,        // Browser-style rounded tops
        Custom          // User-defined rendering
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
        bool showBadge = false;
        bool showMarker = false;            // Show dot marker on tab
        Color markerColor = Colors::Transparent;  // Per-tab marker color (transparent = use container default)
        Color textColor = Colors::Black;
        Color backgroundColor = Color(240, 240, 240);
        Color badgeBackgroundColor = Color(220, 50, 50);
        std::shared_ptr<UltraCanvasUIElement> content = nullptr;
        void* userData = nullptr;

        TabData(const std::string& tabTitle) : title(tabTitle) {}
    };

// ===== V2.0.0: TAB TRANSFER DATA (for drag-out / drag-in between containers) =====
    struct TabTransferData {
        std::string title;
        std::shared_ptr<UltraCanvasUIElement> content;
        std::string iconPath;
        bool showMarker = false;
        bool closable = true;
        void* userData = nullptr;     // Application-defined data (e.g., DocumentTab*)
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
        int tabSpacing = 0;
        int tabPadding = 12;
        bool tabbarLayoutDirty = true;
        int contentTopPadding = 0;
        int contentLeftPadding = 0;

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
        Color tabBarColor = Colors::Transparent;
        Color activeTabColor = Color(255, 255, 255);
        Color inactiveTabColor = Color(236, 236, 236);
        Color hoveredTabColor = Color(240, 240, 255);
        Color disabledTabColor = Color(200, 200, 200);
        Color tabBorderColor = Colors::Gray;
        Color tabContentBorderColor = Colors::Gray;
        Color activeTabTextColor = Colors::Black;
        Color inactiveTabTextColor = Color(80, 80, 80);
        Color disabledTabTextColor = Color(150, 150, 150);
        Color closeButtonColor = Color(120, 120, 120);
        Color closeButtonHoverColor = Color(200, 50, 50);
        Color contentAreaColor = Color(255, 255, 255);
        Color badgeTextColor = Colors::White;
        Color tabSeparatorColor = Color(200, 200, 200);
        Color navButtonBackgroundColor = Color(220, 220, 220);
        Color navButtonIconColor = Colors::Black;

        Color modifiedMarkerColor = Color(195, 30, 3);    // #C31E03
        int modifiedMarkerRadius = 4;

        // ===== OVERFLOW DROPDOWN =====
        OverflowDropdownPosition overflowDropdownPosition = OverflowDropdownPosition::Off;
        bool showOverflowDropdown = false;
        bool overflowDropdownVisible = false;
        int overflowDropdownWidth = 24;
        std::shared_ptr<UltraCanvasButton> overflowButton = nullptr;
        std::shared_ptr<UltraCanvasAutoComplete> searchAutoComplete = nullptr;

        // ===== DROPDOWN SEARCH =====
        bool enableDropdownSearch = true;
        int dropdownSearchThreshold = 5;
        bool dropdownSearchActive = false;
        std::string dropdownSearchText = "";

        // ===== NEW TAB BUTTON =====
        NewTabButtonPosition newTabButtonPosition = NewTabButtonPosition::AfterTabs;
        bool showNewTabButton = false;
        int newTabButtonWidth = 32;
        bool hoveredNewTabButton = false;
        Color newTabButtonColor = Color(240, 240, 240);
        Color newTabButtonHoverColor = Color(220, 220, 255);
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

        // ===== V2.0.0: ENHANCED DRAG STATE =====
        int dragInsertionIndex = -1;          // Where the insertion line is drawn (-1 = none)
        int dragOutThreshold = 40;            // Pixels beyond tab bar to trigger drag-out
        bool dragOutTriggered = false;        // True once drag-out callback has fired for this drag
        Point2Di dragCurrentPosition;         // Current mouse position during drag (for ghost rendering)
        Point2Di dragGlobalAnchor;            // Screen coords at drag start (for cross-window delta)
        Color dragInsertionColor = Color(0, 120, 215, 230);   // Blue insertion indicator
        Color dragGhostBorderColor = Color(0, 120, 215, 120); // Ghost tab border

        // ===== TAB CONTEXT MENU =====
        std::shared_ptr<UltraCanvasMenu> tabContextMenu;
        int contextMenuTabIndex = -1;  // Index of the tab that was right-clicked

        // ===== CALLBACKS (USING CORRECT BASE VERB FORMS) =====
        std::function<void(int, int)> onTabChange;           // (oldIndex, newIndex)
        std::function<void(int)> onTabSelect;                // (tabIndex)
        std::function<bool(int)> onTabClose;                 // (tabIndex) - return false to prevent
        std::function<void(int, int)> onTabReorder;          // (fromIndex, toIndex)
        std::function<void(int, const std::string&)> onTabRename; // (tabIndex, newTitle)
        std::function<void()> onTabBarRightClick;
        std::function<void(int)> onTabContextMenu;  // Called before context menu shown (tabIndex)

        // ===== V2.0.0: DRAG-OUT / DRAG-IN CALLBACKS =====
        /// Tab dragged out of the tab bar beyond threshold.
        /// @param tabIndex Index of the dragged tab
        /// @param screenX Screen X coordinate of mouse at drag-out
        /// @param screenY Screen Y coordinate of mouse at drag-out
        /// @return true if the handler removed the tab (e.g., moved to new window)
        std::function<bool(int tabIndex, int screenX, int screenY)> onTabDragOut;

        /// External tab offered for insertion via AcceptTabTransfer().
        /// @param data Transfer data for the incoming tab
        /// @param insertionIndex Suggested insertion position
        /// @return New tab index if accepted, -1 to reject
        std::function<int(const TabTransferData& data, int insertionIndex)> onTabDragIn;

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
        void SetContentTopPadding(int padding) { if (contentTopPadding != padding) { contentTopPadding = padding; InvalidateTabbar(); } }
        int GetContentTopPadding() const { return contentTopPadding; }
        void SetContentLeftPadding(int padding) { if (contentLeftPadding != padding) { contentLeftPadding = padding; InvalidateTabbar(); } }
        int GetContentLeftPadding() const { return contentLeftPadding; }
        void SetIconSize(int size) { iconSize = size; InvalidateTabbar(); }
        int GetIconSize() const { return iconSize; }
        bool CalcBadgeDimensions(TabData* tabData);

        void SetNewTabButtonWidth(int w) { newTabButtonWidth = w; }
        void SetInactiveTabBackgroundColor(const Color& c) { inactiveTabColor = c; }
        void SetActiveTabBackgroundColor(const Color& c) { activeTabColor = c; }
        void SetInactiveTabTextColor(const Color& c) { inactiveTabTextColor = c; }
        void SetNewButtonColor(const Color& c) { newTabButtonColor = c; }

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
        void SetNewTabButtonPosition(NewTabButtonPosition position);
        NewTabButtonPosition GetNewTabButtonPosition() const { return newTabButtonPosition; }
        void SetShowNewTabButton(bool show);
        bool GetShowNewTabButton() const { return showNewTabButton; }

        // ===== TAB CONTEXT MENU =====
        void SetTabContextMenu(std::shared_ptr<UltraCanvasMenu> menu) { tabContextMenu = menu; }
        std::shared_ptr<UltraCanvasMenu> GetTabContextMenu() const { return tabContextMenu; }
        int GetContextMenuTabIndex() const { return contextMenuTabIndex; }

        // ===== TAB MANAGEMENT =====
        int AddTab(const std::string& title, std::shared_ptr<UltraCanvasUIElement> content = nullptr);
        void RemoveTab(int index);
        void SetActiveTab(int index);

        // ===== TAB ICON AND BADGE METHODS =====
        void SetTabIcon(int index, const std::string& iconPath);
        std::string GetTabIcon(int index) const;
        void SetTabBadge(int index, const std::string & text, bool show = true);
        void SetTabBadgeColor(int index, const Color& color);
        void ClearTabBadge(int index);
        std::string GetTabBadgeText(int index);
        bool IsTabBadgeVisible(int index) const;

        // ===== TAB MODIFIED MARKER =====
        void SetTabShowMarker(int index, bool show);
        bool IsTabShowMarker(int index) const;
        void SetTabMarkerColor(int index, const Color& color);
        Color GetTabMarkerColor(int index) const;
        void SetModifiedMarkerColor(const Color& color) { modifiedMarkerColor = color; InvalidateTabbar(); }
        Color GetModifiedMarkerColor() const { return modifiedMarkerColor; }
        void SetModifiedMarkerRadius(int radius) { modifiedMarkerRadius = radius; InvalidateTabbar(); }
        int GetModifiedMarkerRadius() const { return modifiedMarkerRadius; }

        // ===== OVERFLOW DROPDOWN METHODS =====
        void InitializeOverflowDropdown();
        void UpdateOverflowDropdown();
        void UpdateOverflowDropdownVisibility();
        bool CheckIfOverflowDropdownNeeded();
        void PositionOverflowDropdown();
        void ShowSearchAutoComplete();
        void HideSearchAutoComplete();
        void PopulateSearchAutoComplete();

        // ===== LAYOUT =====
        void SetBounds(const Rect2Di& b) override;
        void UpdateGeometry(IRenderContext* ctx) override;

        // ===== RENDERING =====
        void Render(IRenderContext* ctx) override;
        void RenderTabBar(IRenderContext* ctx);
        void RenderTab(int index, IRenderContext* ctx);
        void RenderTabIcon(int index, IRenderContext* ctx);
        void RenderTabBadge(int index, IRenderContext* ctx);
        void RenderCloseButton(int index, IRenderContext* ctx);
        void RenderModifiedMarker(int index, IRenderContext* ctx);
        void RenderScrollButtons(IRenderContext* ctx);
        void RenderContentArea(IRenderContext* ctx);
        void RenderNewTabButton(IRenderContext* ctx);
        void RenderOverflowButton(IRenderContext* ctx);

        // ===== V2.0.0: DRAG VISUAL FEEDBACK RENDERING =====
        void RenderDragInsertionIndicator(IRenderContext* ctx);
        void RenderDraggedTabGhost(IRenderContext* ctx);

        // ===== EVENT HANDLING (CORRECTED TO RETURN BOOL) =====
        bool OnEvent(const UCEvent& event) override;
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
        bool HandleKeyDown(const UCEvent& event);

        // ===== UTILITY METHODS =====
        int CalculateTabWidth(int index);
        int CalculateMaxTabWidth();
        std::string GetTruncatedTabText(IRenderContext* ctx, const std::string& text, int maxWidth) const;
        Rect2Di GetTabAreaBounds();
        Rect2Di GetTabBarBounds();
        Rect2Di GetContentAreaBounds();
        Rect2Di GetTabBounds(int index);
        Rect2Di GetCloseButtonBounds(int index);
        Rect2Di GetNewTabButtonBounds();
        int GetTabAtPosition(int x, int y);
        bool ShouldShowCloseButton(const TabData* tab);
        void CalculateLayout();
        void ScrollTabs(int direction);
        void ReorderTabs(int fromIndex, int toIndex);
        void EnsureTabVisible(int index);
        void PackTabBar();
        void PositionTabContent(int index);
        void UpdateContentVisibility();

        // ===== V2.0.0: DRAG-OUT / DRAG-IN METHODS =====

        /// Set pixel threshold beyond tab bar that triggers drag-out (default: 40)
        void SetDragOutThreshold(int pixels) { dragOutThreshold = pixels; }
        int GetDragOutThreshold() const { return dragOutThreshold; }

        /// Enable/disable drag-out support
        void SetAllowTabDragOut(bool allow) { allowTabDragOut = allow; }
        bool GetAllowTabDragOut() const { return allowTabDragOut; }

        /// Accept an external tab transfer into this container at the given position.
        /// If onTabDragIn callback is set, it is called first and can reject by returning -1.
        /// @param data Tab transfer data
        /// @param insertionIndex Position to insert (-1 = append at end)
        /// @return New tab index, or -1 if rejected
        int AcceptTabTransfer(const TabTransferData& data, int insertionIndex = -1);

        /// Extract transfer data from an existing tab (for use when dragging out)
        /// @param index Tab index to extract data from
        /// @return TabTransferData populated from the tab
        TabTransferData GetTabTransferData(int index) const;

        // ===== GETTERS AND SETTERS =====
        int GetActiveTab() const { return activeTabIndex; }
        int GetTabCount() const { return (int)tabs.size(); }

        void SetTabTitle(int index, const std::string& title);
        std::string GetTabTitle(int index) const;

        void SetTabTooltip(int index, const std::string& tooltip);
        std::string GetTabTooltip(int index) const;

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

    private:
        std::string ToLowerCase(const std::string& str) const {
            std::string result = str;
            std::transform(result.begin(), result.end(), result.begin(), ::tolower);
            return result;
        }
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
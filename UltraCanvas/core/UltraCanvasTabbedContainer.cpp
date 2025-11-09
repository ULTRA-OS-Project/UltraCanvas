// core/UltraCanvasTabbedContainer.cpp
// Enhanced tabbed container component with overflow dropdown and search functionality
// Version: 1.6.0
// Last Modified: 2025-09-14
// Author: UltraCanvas Framework
#include "UltraCanvasTabbedContainer.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <algorithm>
#include <cctype>

namespace UltraCanvas {

    UltraCanvasTabbedContainer::UltraCanvasTabbedContainer(const std::string &elementId, long uniqueId, long posX,
                                                           long posY, long w, long h)
            : UltraCanvasContainer(elementId, uniqueId, posX, posY, w, h) {
        InitializeOverflowDropdown();
        CalculateLayout();
    }

    void UltraCanvasTabbedContainer::SetTabHeight(int th) {
        tabHeight = th;
        CalculateLayout();
    }

    void UltraCanvasTabbedContainer::SetTabMinWidth(int w) {
        tabMinWidth = w;
        CalculateLayout();
    }

    void UltraCanvasTabbedContainer::SetTabMaxWidth(int w) {
        tabMaxWidth = w;
        CalculateLayout();
    }

    void UltraCanvasTabbedContainer::SetOverflowDropdownPosition(OverflowDropdownPosition position) {
        overflowDropdownPosition = position;
        showOverflowDropdown = (position != OverflowDropdownPosition::Off);
        CalculateLayout();
        UpdateOverflowDropdownVisibility();
    }

    void UltraCanvasTabbedContainer::SetOverflowDropdownWidth(int width) {
        overflowDropdownWidth = std::max(16, width);
        CalculateLayout();
    }

    void UltraCanvasTabbedContainer::SetDropdownSearchEnabled(bool enabled) {
        enableDropdownSearch = enabled;
        if (!enabled) {
            ClearDropdownSearch();
        }
        UpdateOverflowDropdown();
    }

    void UltraCanvasTabbedContainer::SetDropdownSearchThreshold(int threshold) {
        dropdownSearchThreshold = std::max(1, threshold);
        UpdateOverflowDropdown();
    }

    void UltraCanvasTabbedContainer::ClearDropdownSearch() {
        dropdownSearchText.clear();
        dropdownSearchActive = false;
        UpdateOverflowDropdown();
    }

    int UltraCanvasTabbedContainer::AddTab(const std::string &title, std::shared_ptr<UltraCanvasUIElement> content) {
        auto tabData = std::make_unique<TabData>(title);
        if (content) {
            tabData->content = content;
            // Add content as child to container
            AddChild(tabData->content);
        }

        tabs.push_back(std::move(tabData));
        int newIndex = (int)tabs.size() - 1;

        // Set as active if first tab
        if (activeTabIndex == -1) {
            activeTabIndex = newIndex;
        }

        CalculateLayout();
        UpdateOverflowDropdown();

        if (content) {
            PositionTabContent(newIndex);
            UpdateContentVisibility();
        }

        return newIndex;
    }

    void UltraCanvasTabbedContainer::RemoveTab(int index) {
        if (index < 0 || index >= (int)tabs.size()) return;

        // Check if removal is allowed
        if (onTabClose && !onTabClose(index)) return;

        // Remove tab content from container
        if (tabs[index]->content.get()) {
            RemoveChild(tabs[index]->content);
        }

        tabs.erase(tabs.begin() + index);

        // Update active tab index
        if (activeTabIndex >= (int)tabs.size()) {
            activeTabIndex = (int)tabs.size() - 1;
        } else if (activeTabIndex > index) {
            activeTabIndex--;
        } else if (activeTabIndex == index && !tabs.empty()) {
            // Find next enabled tab
            activeTabIndex = -1;
            for (int i = 0; i < (int)tabs.size(); i++) {
                if (tabs[i]->enabled) {
                    activeTabIndex = i;
                    break;
                }
            }
        }

        if (onTabCloseRequest) onTabCloseRequest(index);

        CalculateLayout();
        UpdateOverflowDropdown();
        UpdateContentVisibility();
    }

    void UltraCanvasTabbedContainer::SetActiveTab(int index) {
        if (index < 0 || index >= (int)tabs.size() || !tabs[index]->enabled) return;

        int oldIndex = activeTabIndex;
        activeTabIndex = index;

        EnsureTabVisible(index);
        UpdateContentVisibility();

        if (onTabChange) onTabChange(oldIndex, index);
        if (onTabSelect) onTabSelect(index);
    }

    void UltraCanvasTabbedContainer::InitializeOverflowDropdown() {
        // Create overflow dropdown using existing UltraCanvasDropdown
        overflowDropdown = std::make_shared<UltraCanvasDropdown>(
                GetIdentifier() + "_overflow", 0, 0, 0, overflowDropdownWidth, tabHeight
        );
        DropdownStyle st;
        st.hasShadow = false;
        st.borderWidth = 1;
        overflowDropdown->SetStyle(st);
        // Add dropdown as child to container
        AddChild(overflowDropdown);
        overflowDropdown->SetVisible(false);

        // Set up dropdown callback with proper base verb form
        overflowDropdown->onSelectionChanged = [this](int selectedIndex, const DropdownItem& item) {
            if (selectedIndex == -1) {
                // Search field selected - activate search mode
                dropdownSearchActive = true;
                RequestRedraw();
                return; // Keep dropdown open for search input
            }

            // Use item value to get actual tab index
            int tabIndex = item.value.empty() ? selectedIndex : std::stoi(item.value);
            if (tabIndex >= 0 && tabIndex < (int)tabs.size()) {
                SetActiveTab(tabIndex);
//                    overflowDropdown->CloseDropdown();
                ClearDropdownSearch(); // Clear search when tab is selected
            }
        };
    }

    void UltraCanvasTabbedContainer::UpdateOverflowDropdown() {
        if (!showOverflowDropdown || !overflowDropdown) return;

        // Clear existing items
        overflowDropdown->ClearItems();

        // Check if dropdown should be visible
        bool needsDropdown = CheckIfOverflowDropdownNeeded();
        overflowDropdownVisible = needsDropdown;
        overflowDropdown->SetVisible(needsDropdown);

        if (!needsDropdown) return;

        // Count visible tabs for search threshold
        int visibleTabCount = 0;
        for (int i = 0; i < (int)tabs.size(); i++) {
            if (tabs[i]->visible) {
                visibleTabCount++;
            }
        }

        // Add search field if we have more than threshold tabs
        bool shouldShowSearch = enableDropdownSearch && visibleTabCount > dropdownSearchThreshold;

        if (shouldShowSearch) {
            // Add search field as first item with search icon
            std::string searchDisplayText = "🔍 " + (dropdownSearchText.empty() ? "Search tabs..." : dropdownSearchText);
            overflowDropdown->AddItem(searchDisplayText, "-1"); // Special value "-1" for search
            overflowDropdown->AddSeparator(); // Add separator after search field
        }

        // Filter and add tab titles to dropdown
        std::string searchLower = ToLowerCase(dropdownSearchText);

        for (int i = 0; i < (int)tabs.size(); i++) {
            if (!tabs[i]->visible) continue;

            // Apply search filter if search is active
            if (shouldShowSearch && !dropdownSearchText.empty()) {
                std::string titleLower = ToLowerCase(tabs[i]->title);
                if (titleLower.find(searchLower) == std::string::npos) {
                    continue; // Skip tabs that don't match search
                }
            }

            std::string displayTitle = tabs[i]->title;

            // Mark active tab
            if (i == activeTabIndex) {
                displayTitle = "● " + displayTitle;
            }

            // Mark disabled tabs
            if (!tabs[i]->enabled) {
                displayTitle = "[" + displayTitle + "]";
            }

            overflowDropdown->AddItem(displayTitle, std::to_string(i));
        }

        // Position dropdown
        PositionOverflowDropdown();
    }

    void UltraCanvasTabbedContainer::UpdateOverflowDropdownVisibility() {
        if (!showOverflowDropdown) {
            overflowDropdownVisible = false;
            if (overflowDropdown) {
                overflowDropdown->SetVisible(false);
            }
            return;
        }

        UpdateOverflowDropdown();
    }

    bool UltraCanvasTabbedContainer::CheckIfOverflowDropdownNeeded() const {
        if (!showOverflowDropdown || overflowDropdownPosition == OverflowDropdownPosition::Off) {
            return false;
        }

        // Check if any tabs are cut off or scrolled out of view
        if (enableTabScrolling && tabScrollOffset > 0) {
            return true;
        }

        // Check if tab text is truncated
        Rect2Di tabBarBounds = GetTabBarBounds();
        int availableWidth = tabBarBounds.width;

        // Reserve space for dropdown if positioned on left
        if (overflowDropdownPosition == OverflowDropdownPosition::Left) {
            availableWidth -= overflowDropdownWidth + tabSpacing;
        }

        // Reserve space for scroll buttons if shown
        if (showScrollButtons) {
            availableWidth -= 40; // Left and right scroll buttons
        }

        int totalTabWidth = 0;
        int visibleTabs = 0;

        for (int i = 0; i < (int)tabs.size(); i++) {
            if (!tabs[i]->visible) continue;

            int tabWidth = CalculateTabWidth(i);
            totalTabWidth += tabWidth + tabSpacing;
            visibleTabs++;

            // Check if this tab would be cut off
            if (totalTabWidth > availableWidth) {
                return true;
            }
        }

        return false;
    }

    void UltraCanvasTabbedContainer::PositionOverflowDropdown() {
        if (!overflowDropdown || !overflowDropdownVisible) return;

        Rect2Di tabBarBounds = GetTabBarBounds();

        switch (overflowDropdownPosition) {
            case OverflowDropdownPosition::Left:
                overflowDropdown->SetPosition(
                        0,
                        0
                );
                break;

            case OverflowDropdownPosition::Right:
                overflowDropdown->SetPosition(
                        tabBarBounds.width - overflowDropdownWidth,
                        0
                );
                break;

            default:
                break;
        }

        overflowDropdown->SetSize(overflowDropdownWidth, tabBarBounds.height);
    }

    void UltraCanvasTabbedContainer::Render(IRenderContext* ctx) {
        if (!IsVisible()) return;

        ctx->PushState();
        RenderTabBar(ctx);
        if (showOverflowDropdown && overflowDropdown) {
            ctx->PushState();
            ctx->Translate(GetX(), GetY());
            overflowDropdown->Render(ctx);
            ctx->PopState();
        }
        RenderContentArea(ctx);

        // Render overflow dropdown (handled automatically by base class)
        // Note: Dropdown is now a child element, so base class will render it

        ctx->PopState();
    }

    void UltraCanvasTabbedContainer::RenderTabBar(IRenderContext *ctx) {
        Rect2Di tabBarBounds = GetTabBarBounds();

        // Fill tab bar background
        ctx->DrawFilledRectangle(tabBarBounds, tabBarColor);

        // Calculate available space for tabs
        Rect2Di tabAreaBounds = GetTabAreaBounds();

        // Render visible tabs
        for (int i = tabScrollOffset; i < std::min(tabScrollOffset + maxVisibleTabs, (int)tabs.size()); i++) {
            if (!tabs[i]->visible) continue;
            RenderTab(i, ctx);
        }

        // Render scroll buttons if needed
        if (showScrollButtons) {
            RenderScrollButtons(ctx);
        }

        // Draw tab bar border
        ctx->SetStrokePaint(tabBorderColor);
        ctx->DrawRectangle(tabBarBounds);
    }

    void UltraCanvasTabbedContainer::RenderTab(int index, IRenderContext *ctx) {
        if (index < 0 || index >= (int)tabs.size()) return;

        Rect2Di tabBounds = GetTabBounds(index);
        if (tabBounds.width <= 0) return;

        TabData* tab = tabs[index].get();

        // Determine tab colors
        Color bgColor = inactiveTabColor;
        Color textColor = inactiveTabTextColor;

        if (!tab->enabled) {
            bgColor = disabledTabColor;
            textColor = disabledTabTextColor;
        } else if (index == activeTabIndex) {
            bgColor = activeTabColor;
            textColor = activeTabTextColor;
        } else if (index == hoveredTabIndex) {
            bgColor = hoveredTabColor;
            textColor = inactiveTabTextColor;
        }

        // Use custom colors if set
        if (tab->backgroundColor != Color(240, 240, 240)) {
            bgColor = tab->backgroundColor;
        }
        if (tab->textColor != Colors::Black) {
            textColor = tab->textColor;
        }

        // Draw tab background
        ctx->DrawFilledRectangle(tabBounds, bgColor, 1.0, tabBorderColor);
        // Calculate text area (reserve space for close button if needed)
        Rect2Di textArea = tabBounds;
        textArea.x += tabPadding;
        textArea.width -= tabPadding * 2;

        if (ShouldShowCloseButton(tab)) {
            textArea.width -= closeButtonSize + closeButtonMargin;
        }

        // Draw tab text (with truncation if needed)
        if (textArea.width > 0) {
            std::string displayText = GetTruncatedTabText(ctx, tab->title, textArea.width);

            ctx->SetTextPaint(textColor);
            ctx->SetFontSize(fontSize);
            int txtW, txtH;
            ctx->GetTextLineDimensions(displayText, txtW, txtH);
            int textY = tabBounds.y + (tabBounds.height - txtH) / 2;
            ctx->DrawText(displayText, Point2Di(textArea.x, textY));
        }

        // Draw close button if needed
        if (ShouldShowCloseButton(tab)) {
            RenderCloseButton(index, ctx);
        }
    }

    void UltraCanvasTabbedContainer::RenderCloseButton(int index, IRenderContext *ctx) {
        Rect2Di closeBounds = GetCloseButtonBounds(index);
        if (closeBounds.width <= 0) return;

        Color buttonColor = (index == hoveredCloseButtonIndex) ?
                            closeButtonHoverColor : closeButtonColor;

        // Draw close button background
        Point2Di center(closeBounds.x + closeBounds.width / 2,
                        closeBounds.y + closeBounds.height / 2);

        // Draw X symbol
        int halfSize = closeButtonSize / 4;
        ctx->SetStrokePaint(buttonColor);
        ctx->DrawLine(Point2Di(center.x - halfSize, center.y - halfSize),
                      Point2Di(center.x + halfSize, center.y + halfSize));
        ctx->DrawLine(Point2Di(center.x + halfSize, center.y - halfSize),
                      Point2Di(center.x - halfSize, center.y + halfSize));
    }

    void UltraCanvasTabbedContainer::RenderScrollButtons(IRenderContext *ctx) const {
        if (!showScrollButtons) return;

        Rect2Di tabBarBounds = GetTabBarBounds();
        Rect2Di leftButton(tabBarBounds.x + tabBarBounds.width - 40, tabBarBounds.y, 20, tabBarBounds.height);
        Rect2Di rightButton(tabBarBounds.x + tabBarBounds.width - 20, tabBarBounds.y, 20, tabBarBounds.height);

        // Draw button backgrounds and borders
        ctx->DrawFilledRectangle(leftButton, Color(220, 220, 220), 1.0, tabBorderColor);
        ctx->DrawFilledRectangle(rightButton, Color(220, 220, 220), 1.0, tabBorderColor);

        // Draw arrows
        ctx->SetStrokePaint(Colors::Black);
        Point2Di leftCenter(leftButton.x + leftButton.width / 2, leftButton.y + leftButton.height / 2);
        ctx->DrawLine(Point2Di(leftCenter.x - 3, leftCenter.y), Point2Di(leftCenter.x + 3, leftCenter.y - 3));
        ctx->DrawLine(Point2Di(leftCenter.x - 3, leftCenter.y), Point2Di(leftCenter.x + 3, leftCenter.y + 3));

        Point2Di rightCenter(rightButton.x + rightButton.width / 2, rightButton.y + rightButton.height / 2);
        ctx->DrawLine(Point2Di(rightCenter.x - 3, rightCenter.y - 3), Point2Di(rightCenter.x + 3, rightCenter.y));
        ctx->DrawLine(Point2Di(rightCenter.x - 3, rightCenter.y + 3), Point2Di(rightCenter.x + 3, rightCenter.y));
    }

    void UltraCanvasTabbedContainer::RenderContentArea(IRenderContext *ctx) {
        Rect2Di contentBounds = GetContentAreaBounds();
        ctx->DrawFilledRectangle(contentBounds, contentAreaColor, 1.0, tabBorderColor);

        ctx->PushState();

        Rect2Di bounds = GetBounds();
        ctx->SetClipRect(contentBounds);
        ctx->Translate(bounds.x - scrollState.horizontalPosition, bounds.y - scrollState.verticalPosition);
        // Render active tab content
        if (activeTabIndex >= 0 && activeTabIndex < (int)tabs.size()) {
            auto content = tabs[activeTabIndex]->content.get();
            if (content && content->IsVisible()) {
                content->Render(ctx);
            }
        }
        ctx->PopState();
    }

    bool UltraCanvasTabbedContainer::OnEvent(const UCEvent &event) {
        // Handle overflow dropdown events first
        if (overflowDropdownVisible && overflowDropdown) {
            if (overflowDropdown->OnEvent(event)) {
                return true; // Event consumed by dropdown
            }

            // Special handling for search mode
            if (dropdownSearchActive && overflowDropdown->IsDropdownOpen()) {
                return HandleDropdownSearchInput(event);
            }
        }

        switch (event.type) {
            case UCEventType::MouseDown:
                return HandleMouseDown(event);

            case UCEventType::MouseUp:
                return HandleMouseUp(event);

            case UCEventType::MouseMove:
                return HandleMouseMove(event);

            case UCEventType::KeyDown:
                return HandleKeyDown(event);

            default:
                break;
        }

        // Forward events to base container (handles children automatically)
        return UltraCanvasContainer::OnEvent(event);
    }

    bool UltraCanvasTabbedContainer::HandleDropdownSearchInput(const UCEvent &event) {
        if (event.type == UCEventType::KeyDown) {
            switch (event.virtualKey) {
                case UCKeys::Escape:
                    ClearDropdownSearch();
                    overflowDropdown->CloseDropdown();
                    return true;

                case UCKeys::Enter: {
                    // If there's exactly one search result, select it
                    auto filteredTabs = GetFilteredTabIndices();
                    if (filteredTabs.size() == 1) {
                        SetActiveTab(filteredTabs[0]);
                        overflowDropdown->CloseDropdown();
                        ClearDropdownSearch();
                    }
                    return true;
                }
                case UCKeys::Backspace: {
                    if (!dropdownSearchText.empty()) {
                        dropdownSearchText.pop_back();
                        UpdateOverflowDropdown();
                    }
                    return true;
                }
                default:
                    // Handle character input
                    if (event.character >= 32 && event.character <= 126) { // Printable ASCII
                        dropdownSearchText += static_cast<char>(event.character);
                        UpdateOverflowDropdown();
                    }
                    return true;
            }
        }
        return false;
    }

    bool UltraCanvasTabbedContainer::HandleMouseDown(const UCEvent &event) {
        Rect2Di tabBarBounds = GetTabBarBounds();

        if (!tabBarBounds.Contains(event.x, event.y)) return false;

        // Check overflow dropdown first
        if (overflowDropdownVisible && overflowDropdown) {
            Rect2Di dropdownBounds = overflowDropdown->GetBounds();
            if (dropdownBounds.Contains(event.x, event.y)) {
                return false; // Let dropdown handle it
            }
        }

        // Check scroll buttons
        if (showScrollButtons) {
            Rect2Di leftButton(tabBarBounds.x + tabBarBounds.width - 40, tabBarBounds.y, 20, tabBarBounds.height);
            Rect2Di rightButton(tabBarBounds.x + tabBarBounds.width - 20, tabBarBounds.y, 20, tabBarBounds.height);

            if (leftButton.Contains(event.x, event.y)) {
                ScrollTabs(-1);
                return true;
            } else if (rightButton.Contains(event.x, event.y)) {
                ScrollTabs(1);
                return true;
            }
        }

        // Check close buttons
        for (int i = tabScrollOffset; i < std::min(tabScrollOffset + maxVisibleTabs, (int)tabs.size()); i++) {
            if (!tabs[i]->visible) continue;

            Rect2Di closeBounds = GetCloseButtonBounds(i);
            if (closeBounds.width > 0 && closeBounds.Contains(event.x, event.y)) {
                RemoveTab(i);
                return true;
            }
        }

        // Check tab selection
        int clickedTab = GetTabAtPosition(event.x, event.y);
        if (clickedTab >= 0) {
            if (allowTabReordering && event.button == UCMouseButton::Left) {
                draggingTabIndex = clickedTab;
                dragStartPosition = Point2Di(event.x, event.y);
                isDraggingTab = false;
            }

            SetActiveTab(clickedTab);
            return true;
        }

        return false;
    }

    bool UltraCanvasTabbedContainer::HandleMouseUp(const UCEvent &event) {
        if (draggingTabIndex >= 0) {
            draggingTabIndex = -1;
            isDraggingTab = false;
            return true;
        }
        return false;
    }

    bool UltraCanvasTabbedContainer::HandleMouseMove(const UCEvent &event) {
        Rect2Di tabBarBounds = GetTabBarBounds();

        if (tabBarBounds.Contains(event.x, event.y)) {
            int newHoveredTab = GetTabAtPosition(event.x, event.y);
            if (newHoveredTab != hoveredTabIndex) {
                hoveredTabIndex = newHoveredTab;
                RequestRedraw();
            }

            // Handle tab dragging
            if (draggingTabIndex >= 0 && allowTabReordering) {
                float dragDistance = std::sqrt(
                        std::pow(event.x - dragStartPosition.x, 2) +
                        std::pow(event.y - dragStartPosition.y, 2)
                );

                if (!isDraggingTab && dragDistance > 5) {
                    isDraggingTab = true;
                }

                if (isDraggingTab) {
                    int targetTab = GetTabAtPosition(event.x, event.y);
                    if (targetTab >= 0 && targetTab != draggingTabIndex) {
                        ReorderTabs(draggingTabIndex, targetTab);
                        draggingTabIndex = targetTab;
                    }
                }
                RequestRedraw();
            }
            return true;
        } else {
            if (hoveredTabIndex != -1) {
                RequestRedraw();
            }
            hoveredTabIndex = -1;
            hoveredCloseButtonIndex = -1;
        }
        return false;
    }

    bool UltraCanvasTabbedContainer::HandleKeyDown(const UCEvent &event) {
        switch (event.virtualKey) {
            case UCKeys::LeftArrow:
                if (activeTabIndex > 0) {
                    SetActiveTab(activeTabIndex - 1);
                    return true;
                }
                break;

            case UCKeys::RightArrow:
                if (activeTabIndex < (int)tabs.size() - 1) {
                    SetActiveTab(activeTabIndex + 1);
                    return true;
                }
                break;

            case UCKeys::W:
                if (event.ctrl) {
                    if (activeTabIndex >= 0 && ShouldShowCloseButton(tabs[activeTabIndex].get())) {
                        RemoveTab(activeTabIndex);
                        return true;
                    }
                }
                break;

            default:
                break;
        }
        return false;
    }

    std::vector<int> UltraCanvasTabbedContainer::GetFilteredTabIndices() const {
        std::vector<int> filtered;

        if (dropdownSearchText.empty()) {
            // Return all visible tabs if no search text
            for (int i = 0; i < (int)tabs.size(); i++) {
                if (tabs[i]->visible) {
                    filtered.push_back(i);
                }
            }
        } else {
            // Return tabs matching search
            std::string searchLower = ToLowerCase(dropdownSearchText);
            for (int i = 0; i < (int)tabs.size(); i++) {
                if (!tabs[i]->visible) continue;

                std::string titleLower = ToLowerCase(tabs[i]->title);
                if (titleLower.find(searchLower) != std::string::npos) {
                    filtered.push_back(i);
                }
            }
        }

        return filtered;
    }

    int UltraCanvasTabbedContainer::CalculateTabWidth(int index) const {
        if (!window || index < 0 || index >= (int)tabs.size()) return tabMinWidth;

        if (!autoSizeTab) return tabMaxWidth;

        auto ctx = GetRenderContext();
        ctx->SetFontStyle({.fontFamily=fontFamily, .fontSize=fontSize});
        const std::string& title = tabs[index]->title;
        int textWidth = ctx->GetTextLineWidth(title);
        int width = textWidth + tabPadding * 2;

        if (ShouldShowCloseButton(tabs[index].get())) {
            width += closeButtonSize + closeButtonMargin;
        }

        return std::max(tabMinWidth, std::min(width, tabMaxWidth));
    }

    std::string UltraCanvasTabbedContainer::GetTruncatedTabText(IRenderContext* ctx, const std::string &text, int maxWidth) const {
        int textWidth, txtH;
        ctx->SetFontStyle({.fontFamily=fontFamily, .fontSize=fontSize});
        ctx->GetTextLineDimensions(text, textWidth, txtH);
        if (textWidth <= maxWidth) return text;

        // Truncate with ellipsis
        std::string truncated = text;
        while (!truncated.empty() && ctx->GetTextLineWidth(truncated + "...") > maxWidth) {
            truncated.pop_back();
        }

        return truncated + "...";
    }

    Rect2Di UltraCanvasTabbedContainer::GetTabAreaBounds() const {
        Rect2Di tabBarBounds = GetTabBarBounds();

        // Adjust for overflow dropdown
        if (overflowDropdownVisible) {
            switch (overflowDropdownPosition) {
                case OverflowDropdownPosition::Left:
                    tabBarBounds.x += overflowDropdownWidth + tabSpacing;
                    tabBarBounds.width -= overflowDropdownWidth + tabSpacing;
                    break;
                case OverflowDropdownPosition::Right:
                    tabBarBounds.width -= overflowDropdownWidth + tabSpacing;
                    break;
                default:
                    break;
            }
        }

        // Adjust for scroll buttons
        if (showScrollButtons) {
            tabBarBounds.width -= 40; // Left and right scroll buttons
        }

        return tabBarBounds;
    }

    Rect2Di UltraCanvasTabbedContainer::GetTabBarBounds() const {
        Rect2Di bounds = GetBounds();

        switch (tabPosition) {
            case TabPosition::Top:
                return Rect2Di(bounds.x, bounds.y, bounds.width, tabHeight);
            case TabPosition::Bottom:
                return Rect2Di(bounds.x, bounds.y + bounds.height - tabHeight, bounds.width, tabHeight);
            case TabPosition::Left:
                return Rect2Di(bounds.x, bounds.y, tabHeight, bounds.height);
            case TabPosition::Right:
                return Rect2Di(bounds.x + bounds.width - tabHeight, bounds.y, tabHeight, bounds.height);
            default:
                return Rect2Di(bounds.x, bounds.y, bounds.width, tabHeight);
        }
    }

    Rect2Di UltraCanvasTabbedContainer::GetContentAreaBounds() const {
        Rect2Di bounds = GetBounds();

        switch (tabPosition) {
            case TabPosition::Top:
                return Rect2Di(bounds.x, bounds.y + tabHeight, bounds.width, bounds.height - tabHeight);
            case TabPosition::Bottom:
                return Rect2Di(bounds.x, bounds.y, bounds.width, bounds.height - tabHeight);
            case TabPosition::Left:
                return Rect2Di(bounds.x + tabHeight, bounds.y, bounds.width - tabHeight, bounds.height);
            case TabPosition::Right:
                return Rect2Di(bounds.x, bounds.y, bounds.width - tabHeight, bounds.height);
            default:
                return Rect2Di(bounds.x, bounds.y + tabHeight, bounds.width, bounds.height - tabHeight);
        }
    }

    Rect2Di UltraCanvasTabbedContainer::GetTabBounds(int index) const {
        if (index < tabScrollOffset || index >= tabScrollOffset + maxVisibleTabs) {
            return Rect2Di(0, 0, 0, 0);
        }

        Rect2Di tabAreaBounds = GetTabAreaBounds();
        int tabWidth = CalculateTabWidth(index);

        int x = tabAreaBounds.x;
        for (int i = tabScrollOffset; i < index; i++) {
            if (tabs[i]->visible) {
                x += CalculateTabWidth(i) + tabSpacing;
            }
        }

        if (tabPosition == TabPosition::Top || tabPosition == TabPosition::Bottom) {
            return Rect2Di(x, tabAreaBounds.y, tabWidth, tabAreaBounds.height);
        } else {
            return Rect2Di(tabAreaBounds.x, x, tabAreaBounds.width, tabWidth);
        }
    }

    Rect2Di UltraCanvasTabbedContainer::GetCloseButtonBounds(int index) const {
        if (!ShouldShowCloseButton(tabs[index].get())) {
            return Rect2Di(0, 0, 0, 0);
        }

        Rect2Di tabBounds = GetTabBounds(index);
        if (tabBounds.width == 0) return Rect2Di(0, 0, 0, 0);

        return Rect2Di(
                tabBounds.x + tabBounds.width - closeButtonSize - closeButtonMargin,
                tabBounds.y + (tabBounds.height - closeButtonSize) / 2,
                closeButtonSize,
                closeButtonSize
        );
    }

    int UltraCanvasTabbedContainer::GetTabAtPosition(int x, int y) const {
        for (int i = tabScrollOffset; i < std::min(tabScrollOffset + maxVisibleTabs, (int)tabs.size()); i++) {
            if (!tabs[i]->visible) continue;

            Rect2Di tabBounds = GetTabBounds(i);
            if (tabBounds.Contains(x, y)) {
                return i;
            }
        }
        return -1;
    }

    bool UltraCanvasTabbedContainer::ShouldShowCloseButton(const TabData *tab) const {
        if (!tab || !tab->closable) return false;

        switch (closeMode) {
            case TabCloseMode::NoClose:
                return false;
            case TabCloseMode::Closable:
                return true;
            case TabCloseMode::ClosableExceptFirst:
                return (tabs[0].get() != tab);
            default:
                return false;
        }
    }

    void UltraCanvasTabbedContainer::CalculateLayout() {
        Rect2Di tabBarBounds = GetTabBarBounds();
        int availableWidth = tabBarBounds.width;

        // Reserve space for overflow dropdown
        if (overflowDropdownVisible) {
            availableWidth -= overflowDropdownWidth + tabSpacing;
        }

        // Reserve space for scroll buttons
        if (showScrollButtons) {
            availableWidth -= 40;
        }

        // Calculate how many tabs can fit
        maxVisibleTabs = 0;
        int totalWidth = 0;

        for (int i = tabScrollOffset; i < (int)tabs.size(); i++) {
            if (!tabs[i]->visible) continue;

            int tabWidth = CalculateTabWidth(i);
            if (totalWidth + tabWidth > availableWidth) {
                break;
            }

            totalWidth += tabWidth + tabSpacing;
            maxVisibleTabs++;
        }

        // Update scrolling state
        showScrollButtons = enableTabScrolling && (tabScrollOffset > 0 ||
                                                   tabScrollOffset + maxVisibleTabs < (int)tabs.size());

        // Update overflow dropdown
        UpdateOverflowDropdownVisibility();

        // Position all tab content
        for (int i = 0; i < (int)tabs.size(); i++) {
            PositionTabContent(i);
        }
    }

    void UltraCanvasTabbedContainer::ScrollTabs(int direction) {
        if (!enableTabScrolling) return;

        int oldOffset = tabScrollOffset;
        tabScrollOffset += direction;
        tabScrollOffset = std::max(0, std::min(tabScrollOffset, (int)tabs.size() - maxVisibleTabs));

        if (tabScrollOffset != oldOffset) {
            CalculateLayout();
        }
    }

    void UltraCanvasTabbedContainer::ReorderTabs(int fromIndex, int toIndex) {
        if (fromIndex == toIndex || fromIndex < 0 || toIndex < 0 ||
            fromIndex >= (int)tabs.size() || toIndex >= (int)tabs.size()) {
            return;
        }

        auto tab = std::move(tabs[fromIndex]);
        tabs.erase(tabs.begin() + fromIndex);
        tabs.insert(tabs.begin() + toIndex, std::move(tab));

        // Update active index
        if (activeTabIndex == fromIndex) {
            activeTabIndex = toIndex;
        } else if (fromIndex < activeTabIndex && toIndex >= activeTabIndex) {
            activeTabIndex--;
        } else if (fromIndex > activeTabIndex && toIndex <= activeTabIndex) {
            activeTabIndex++;
        }

        if (onTabReorder) onTabReorder(fromIndex, toIndex);
        UpdateOverflowDropdown();
    }

    void UltraCanvasTabbedContainer::EnsureTabVisible(int index) {
        if (!enableTabScrolling || index < 0 || index >= (int)tabs.size()) return;

        if (index < tabScrollOffset) {
            tabScrollOffset = index;
        } else if (index >= tabScrollOffset + maxVisibleTabs) {
            tabScrollOffset = index - maxVisibleTabs + 1;
        }

        tabScrollOffset = std::max(0, std::min(tabScrollOffset, (int)tabs.size() - maxVisibleTabs));
        CalculateLayout();
    }

    void UltraCanvasTabbedContainer::PositionTabContent(int index) {
        if (index < 0 || index >= (int)tabs.size()) return;

        auto content = tabs[index]->content.get();
        if (!content) return;

        Rect2Di contentBounds = GetContentAreaBounds();
        content->SetBounds(0, tabHeight, contentBounds.width, contentBounds.height);
    }

    void UltraCanvasTabbedContainer::UpdateContentVisibility() {
        for (int i = 0; i < (int)tabs.size(); i++) {
            auto content = tabs[i]->content.get();
            if (content) {
                content->SetVisible(i == activeTabIndex);
            }
        }
    }

    void UltraCanvasTabbedContainer::SetTabTitle(int index, const std::string &title) {
        if (index >= 0 && index < (int)tabs.size()) {
            tabs[index]->title = title;
            if (onTabRename) onTabRename(index, title);
            CalculateLayout();
            UpdateOverflowDropdown();
        }
    }

    std::string UltraCanvasTabbedContainer::GetTabTitle(int index) const {
        if (index >= 0 && index < (int)tabs.size()) {
            return tabs[index]->title;
        }
        return "";
    }

    void UltraCanvasTabbedContainer::SetTabEnabled(int index, bool enabled) {
        if (index >= 0 && index < (int)tabs.size()) {
            tabs[index]->enabled = enabled;

            if (!enabled && index == activeTabIndex) {
                for (int i = 0; i < (int)tabs.size(); i++) {
                    if (i != index && tabs[i]->enabled) {
                        SetActiveTab(i);
                        break;
                    }
                }
            }
            UpdateOverflowDropdown();
        }
    }

    void UltraCanvasTabbedContainer::SetTabPosition(TabPosition position) {
        tabPosition = position;
        CalculateLayout();
    }

    bool UltraCanvasTabbedContainer::IsTabEnabled(int index) const {
        if (index >= 0 && index < (int)tabs.size()) {
            return tabs[index]->enabled;
        }
        return false;
    }

    void UltraCanvasTabbedContainer::SetCloseMode(TabCloseMode mode) {
        closeMode = mode;
        CalculateLayout();
    }
}
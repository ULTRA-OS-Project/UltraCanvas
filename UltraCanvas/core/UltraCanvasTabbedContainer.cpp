// core/UltraCanvasTabbedContainer.cpp
// Enhanced tabbed container component with overflow dropdown and search functionality
// Version: 1.8.0
// Last Modified: 2025-11-21
// Author: UltraCanvas Framework
#include "UltraCanvasTabbedContainer.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace UltraCanvas {

    UltraCanvasTabbedContainer::UltraCanvasTabbedContainer(const std::string &elementId, long uniqueId, long posX,
                                                           long posY, long w, long h)
            : UltraCanvasContainer(elementId, uniqueId, posX, posY, w, h) {
        InitializeOverflowDropdown();
    }

    void UltraCanvasTabbedContainer::SetTabHeight(int th) {
        tabHeight = th;
        InvalidateTabbar();
    }

    void UltraCanvasTabbedContainer::SetTabMinWidth(int w) {
        tabMinWidth = w;
        InvalidateTabbar();
    }

    void UltraCanvasTabbedContainer::SetTabMaxWidth(int w) {
        tabMaxWidth = w;
        InvalidateTabbar();
    }

    void UltraCanvasTabbedContainer::SetOverflowDropdownPosition(OverflowDropdownPosition position) {
        overflowDropdownPosition = position;
        showOverflowDropdown = (position != OverflowDropdownPosition::Off);
        InvalidateTabbar();
    }

    void UltraCanvasTabbedContainer::SetOverflowDropdownWidth(int width) {
        overflowDropdownWidth = std::max(16, width);
        InvalidateTabbar();
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
    }

    void UltraCanvasTabbedContainer::ClearDropdownSearch() {
        dropdownSearchText = "";
        dropdownSearchActive = false;
        UpdateOverflowDropdown();
    }

    void UltraCanvasTabbedContainer::SetShowNewTabButton(bool show) {
        showNewTabButton = show;
        InvalidateTabbar();
    }

    void UltraCanvasTabbedContainer::SetNewTabButtonPosition(NewTabButtonPosition position) {
        newTabButtonPosition = position;
        InvalidateTabbar();
    }

    int UltraCanvasTabbedContainer::AddTab(const std::string &title, std::shared_ptr<UltraCanvasUIElement> content) {
        auto tab = std::make_unique<TabData>(title);
        tab->content = content;

        if (content) {
            AddChild(content);
            content->SetVisible(false);
        }

        tabs.push_back(std::move(tab));

        if (activeTabIndex == -1) {
            activeTabIndex = 0;
            UpdateContentVisibility();
        }

        InvalidateTabbar();
        UpdateOverflowDropdown();
        return (int)tabs.size() - 1;
    }

    void UltraCanvasTabbedContainer::RemoveTab(int index) {
        if (index < 0 || index >= (int)tabs.size()) return;

        if (onTabCloseRequest) {
            onTabCloseRequest(index);
        }

        if (onTabClose && !onTabClose(index)) {
            return;
        }

        if (tabs[index]->content) {
            RemoveChild(tabs[index]->content);
        }

        tabs.erase(tabs.begin() + index);

        if (activeTabIndex >= (int)tabs.size()) {
            activeTabIndex = (int)tabs.size() - 1;
        }

        if (activeTabIndex >= 0) {
            for (int i = activeTabIndex; i >= 0; i--) {
                if (tabs[i]->enabled) {
                    activeTabIndex = i;
                    break;
                }
            }
        }

        InvalidateTabbar();
        UpdateOverflowDropdown();
    }

    void UltraCanvasTabbedContainer::SetActiveTab(int index) {
        if (index < 0 || index >= (int)tabs.size() || !tabs[index]->enabled) return;

        int oldIndex = activeTabIndex;
        activeTabIndex = index;

        if (!tabbarLayoutDirty) {
            EnsureTabVisible(index);
            CalculateLayout();
            UpdateContentVisibility();
        }

        if (onTabChange) onTabChange(oldIndex, index);
        if (onTabSelect) onTabSelect(index);
    }

    void UltraCanvasTabbedContainer::SetTabIcon(int index, const std::string& iconPath) {
        if (index >= 0 && index < (int)tabs.size()) {
            tabs[index]->iconPath = iconPath;
            tabs[index]->hasIcon = !iconPath.empty();
            InvalidateTabbar();
        }
    }

    std::string UltraCanvasTabbedContainer::GetTabIcon(int index) const {
        if (index >= 0 && index < (int)tabs.size()) {
            return tabs[index]->iconPath;
        }
        return "";
    }

    void UltraCanvasTabbedContainer::SetTabBadge(int index, const std::string& text, bool show) {
        if (index >= 0 && index < (int)tabs.size()) {
            tabs[index]->badgeText = text;
            tabs[index]->showBadge = show && (!text.empty());
            tabs[index]->hasBadge = tabs[index]->showBadge;
            InvalidateTabbar();
        }
    }

    void UltraCanvasTabbedContainer::ClearTabBadge(int index) {
        if (index >= 0 && index < (int)tabs.size()) {
            tabs[index]->badgeText = "";
            tabs[index]->showBadge = false;
            tabs[index]->hasBadge = false;
            InvalidateTabbar();
        }
    }

    std::string UltraCanvasTabbedContainer::GetTabBadgeText(int index) {
        if (index >= 0 && index < (int)tabs.size()) {
            return tabs[index]->badgeText;
        }
        return std::string();
    }

    bool UltraCanvasTabbedContainer::IsTabBadgeVisible(int index) const {
        if (index >= 0 && index < (int)tabs.size()) {
            return tabs[index]->showBadge;
        }
        return false;
    }

    void UltraCanvasTabbedContainer::InitializeOverflowDropdown() {
        overflowDropdown = std::make_shared<UltraCanvasDropdown>(
                GetIdentifier() + "_overflow", 0, 0, 0, overflowDropdownWidth, tabHeight
        );
        DropdownStyle st;
        st.hasShadow = false;
        st.borderWidth = 1;
        overflowDropdown->SetStyle(st);
        AddChild(overflowDropdown);
        overflowDropdown->SetVisible(false);

        overflowDropdown->onSelectionChanged = [this](int selectedIndex, const DropdownItem& item) {
            if (selectedIndex == -1) {
                dropdownSearchActive = true;
                RequestRedraw();
                return;
            }

            int tabIndex = item.value.empty() ? selectedIndex : std::stoi(item.value);
            if (tabIndex >= 0 && tabIndex < (int)tabs.size()) {
                SetActiveTab(tabIndex);
//                overflowDropdownVisible = false;
//                overflowDropdown->SetVisible(false);
                ClearDropdownSearch();
            }
        };
    }

    void UltraCanvasTabbedContainer::UpdateOverflowDropdown() {
        if (!overflowDropdown) return;

        overflowDropdown->ClearItems();

        bool shouldShowSearch = enableDropdownSearch && ((int)tabs.size() >= dropdownSearchThreshold);

        if (shouldShowSearch) {
            std::string searchDisplayText = (dropdownSearchText.empty() ? "Search tabs..." : dropdownSearchText);
            overflowDropdown->AddItem(searchDisplayText, "-1");
            overflowDropdown->AddSeparator();
        }

        std::string searchLower = ToLowerCase(dropdownSearchText);

        for (int i = 0; i < (int)tabs.size(); i++) {
            if (!tabs[i]->visible) continue;

            if (shouldShowSearch && !dropdownSearchText.empty()) {
                std::string titleLower = ToLowerCase(tabs[i]->title);
                if (titleLower.find(searchLower) == std::string::npos) {
                    continue;
                }
            }

            std::string displayTitle = tabs[i]->title;

            if (i == activeTabIndex) {
                displayTitle = "● " + displayTitle;
            }

            if (!tabs[i]->enabled) {
                displayTitle = "[" + displayTitle + "]";
            }

            overflowDropdown->AddItem(displayTitle, std::to_string(i));
        }

        PositionOverflowDropdown();
    }

    void UltraCanvasTabbedContainer::UpdateOverflowDropdownVisibility() {
        bool needed = CheckIfOverflowDropdownNeeded();
        overflowDropdownVisible = showOverflowDropdown && needed;

        if (overflowDropdown) {
            overflowDropdown->SetVisible(overflowDropdownVisible);
        }

        if (overflowDropdownVisible) {
            UpdateOverflowDropdown();
        }
    }

    bool UltraCanvasTabbedContainer::CheckIfOverflowDropdownNeeded() {
        if (!showOverflowDropdown || overflowDropdownPosition == OverflowDropdownPosition::Off) {
            return false;
        }

        if (enableTabScrolling && tabScrollOffset > 0) {
            return true;
        }

        Rect2Di tabBarBounds = GetTabBarBounds();
        bool isVertical = (tabPosition == TabPosition::Left || tabPosition == TabPosition::Right);
        int availableSpace = isVertical ? tabBarBounds.height : tabBarBounds.width;

        if (overflowDropdownPosition == OverflowDropdownPosition::Left) {
            availableSpace -= overflowDropdownWidth + tabSpacing;
        }

        if (showScrollButtons) {
            availableSpace -= 40;
        }

        if (showNewTabButton) {
            availableSpace -= newTabButtonWidth + tabSpacing;
        }

        int totalTabSpace = 0;

        for (int i = 0; i < (int)tabs.size(); i++) {
            if (!tabs[i]->visible) continue;

            // For vertical tabs, each tab takes fixed tabHeight
            // For horizontal tabs, each tab takes calculated tabWidth
            int tabSize = isVertical ? tabHeight : CalculateTabWidth(i);
            totalTabSpace += tabSize + tabSpacing;

            if (totalTabSpace > availableSpace) {
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
                overflowDropdown->SetPosition(0, 0);
                break;

            case OverflowDropdownPosition::Right:
                overflowDropdown->SetPosition(tabBarBounds.width - overflowDropdownWidth, 0);
                break;

            default:
                break;
        }

        overflowDropdown->SetSize(overflowDropdownWidth, tabBarBounds.height);
    }

    void UltraCanvasTabbedContainer::Render(IRenderContext* ctx) {
        if (!IsVisible()) return;

        if (tabbarLayoutDirty) {
            CalculateLayout();
            UpdateOverflowDropdown();
            CalculateLayout();
            EnsureTabVisible(activeTabIndex);
            UpdateContentVisibility();
            tabbarLayoutDirty = false;
        }

        ctx->PushState();
        ctx->Translate(bounds.x,
                       bounds.y);

        RenderContentArea(ctx);
        RenderTabBar(ctx);
        ctx->PopState();

        //UltraCanvasContainer::Render(ctx);
    }

    void UltraCanvasTabbedContainer::RenderTabBar(IRenderContext *ctx) {
        Rect2Di tabBarBounds = GetTabBarBounds();

        if (tabBarColor.a > 0) {
            ctx->DrawFilledRectangle(tabBarBounds, tabBarColor);
        }
//        if (tabContentBorderColor.a > 0) {
//            ctx->SetStrokePaint(tabContentBorderColor);
//            ctx->SetStrokeWidth(1);
//            ctx->DrawLine(tabBarBounds.BottomLeft(), tabBarBounds.BottomRight());
//        }


        Rect2Di tabAreaBounds = GetTabAreaBounds();

        for (int i = tabScrollOffset; i < std::min(tabScrollOffset + maxVisibleTabs, (int)tabs.size()); i++) {
            if (!tabs[i]->visible) continue;
            RenderTab(i, ctx);
        }

        if (showScrollButtons) {
            RenderScrollButtons(ctx);
        }

        if (showNewTabButton) {
            RenderNewTabButton(ctx);
        }

        ctx->PushState();
        if (overflowDropdownVisible) {
            overflowDropdown->Render(ctx);
        }
        ctx->PopState();

//        if (tabBorderColor.a > 0) {
//            ctx->SetStrokePaint(tabBorderColor);
//            ctx->DrawRectangle(tabBarBounds);
//        }
    }

    void UltraCanvasTabbedContainer::RenderTabIcon(int index, IRenderContext *ctx) {
        if (index < 0 || index >= (int)tabs.size()) return;

        TabData* tab = tabs[index].get();
        if (!tab->hasIcon || tab->iconPath.empty()) return;

        Rect2Di tabBounds = GetTabBounds(index);
        int iconX = tabBounds.x + tabPadding;
        int iconY = tabBounds.y + (tabBounds.height - iconSize) / 2;

        ctx->DrawImage(tab->iconPath, iconX, iconY, iconSize, iconSize, ImageFitMode::Contain);
    }

    void UltraCanvasTabbedContainer::RenderTabBadge(int index, IRenderContext *ctx) {
        if (index < 0 || index >= (int)tabs.size()) return;

        TabData* tab = tabs[index].get();
        if (!tab->hasBadge || !tab->showBadge || tab->badgeText.empty()) return;
        Rect2Di tabBounds = GetTabBounds(index);

        int badgeX = tabBounds.x + tabBounds.width - tabPadding - closeButtonSize - closeButtonMargin - tab->badgeWidth;
        if (!ShouldShowCloseButton(tab)) {
            badgeX = tabBounds.x + tabBounds.width - tabPadding - tab->badgeWidth;
        }
        int badgeY = tabBounds.y + (tabBounds.height - tab->badgeHeight) / 2;

        ctx->DrawFilledRectangle(Rect2Di(badgeX, badgeY, tab->badgeWidth, tab->badgeHeight), badgeBackgroundColor, 0, Colors::Transparent, std::min(tab->badgeHeight, tab->badgeWidth)/2);

//        std::string badgeText = (tab->badgeCount > 99) ? "99+" : std::to_string(tab->badgeCount);
        ctx->PushState();
        ctx->SetFontSize(9);
        ctx->SetTextPaint(badgeTextColor);
        //      int txtW, txtH;
//        ctx->GetTextLineDimensions(badgeText, txtW, txtH);
        ctx->SetTextAlignment(TextAlignment::Center);
        ctx->SetTextVerticalAlignment(TextVerticalAlignement::Middle);
        ctx->DrawTextInRect(tab->badgeText, Rect2Di(badgeX, badgeY, tab->badgeWidth, tab->badgeHeight));
        ctx->PopState();
    }

    void UltraCanvasTabbedContainer::RenderCloseButton(int index, IRenderContext *ctx) {
        Rect2Di closeBounds = GetCloseButtonBounds(index);
        if (closeBounds.width <= 0) return;

        Color buttonColor = (index == hoveredCloseButtonIndex) ? closeButtonHoverColor : closeButtonColor;

        Point2Di center(closeBounds.x + closeBounds.width / 2, closeBounds.y + closeBounds.height / 2);

        int halfSize = closeButtonSize / 4;
        ctx->SetStrokePaint(buttonColor);
        ctx->DrawLine(Point2Di(center.x - halfSize, center.y - halfSize),
                      Point2Di(center.x + halfSize, center.y + halfSize));
        ctx->DrawLine(Point2Di(center.x + halfSize, center.y - halfSize),
                      Point2Di(center.x - halfSize, center.y + halfSize));
    }

    void UltraCanvasTabbedContainer::RenderScrollButtons(IRenderContext *ctx) {
        if (!showScrollButtons) return;

        Rect2Di tabBarBounds = GetTabBarBounds();
        Rect2Di leftButton, rightButton;

        switch (tabPosition) {
            case TabPosition::Top:
            case TabPosition::Bottom: {
                // Horizontal tabs - buttons at right edge
                leftButton = Rect2Di(tabBarBounds.x + tabBarBounds.width - 40, tabBarBounds.y, 20, tabBarBounds.height);
                rightButton = Rect2Di(tabBarBounds.x + tabBarBounds.width - 20, tabBarBounds.y, 20, tabBarBounds.height);

                ctx->DrawFilledRectangle(leftButton, Color(220, 220, 220), 1.0, tabBorderColor);
                ctx->DrawFilledRectangle(rightButton, Color(220, 220, 220), 1.0, tabBorderColor);

                ctx->SetStrokePaint(Colors::Black);
                Point2Di leftCenter(leftButton.x + leftButton.width / 2, leftButton.y + leftButton.height / 2);
                ctx->DrawLine(Point2Di(leftCenter.x - 3, leftCenter.y), Point2Di(leftCenter.x + 3, leftCenter.y - 3));
                ctx->DrawLine(Point2Di(leftCenter.x - 3, leftCenter.y), Point2Di(leftCenter.x + 3, leftCenter.y + 3));

                Point2Di rightCenter(rightButton.x + rightButton.width / 2, rightButton.y + rightButton.height / 2);
                ctx->DrawLine(Point2Di(rightCenter.x - 3, rightCenter.y - 3), Point2Di(rightCenter.x + 3, rightCenter.y));
                ctx->DrawLine(Point2Di(rightCenter.x - 3, rightCenter.y + 3), Point2Di(rightCenter.x + 3, rightCenter.y));
                break;
            }

            case TabPosition::Left:
            case TabPosition::Right: {
                // Vertical tabs - buttons at bottom edge
                leftButton = Rect2Di(tabBarBounds.x, tabBarBounds.y + tabBarBounds.height - 40, tabBarBounds.width, 20);
                rightButton = Rect2Di(tabBarBounds.x, tabBarBounds.y + tabBarBounds.height - 20, tabBarBounds.width, 20);

                ctx->DrawFilledRectangle(leftButton, Color(220, 220, 220), 1.0, tabBorderColor);
                ctx->DrawFilledRectangle(rightButton, Color(220, 220, 220), 1.0, tabBorderColor);

                ctx->SetStrokePaint(Colors::Black);
                Point2Di leftCenter(leftButton.x + leftButton.width / 2, leftButton.y + leftButton.height / 2);
                ctx->DrawLine(Point2Di(leftCenter.x, leftCenter.y - 3), Point2Di(leftCenter.x - 3, leftCenter.y + 3));
                ctx->DrawLine(Point2Di(leftCenter.x, leftCenter.y - 3), Point2Di(leftCenter.x + 3, leftCenter.y + 3));

                Point2Di rightCenter(rightButton.x + rightButton.width / 2, rightButton.y + rightButton.height / 2);
                ctx->DrawLine(Point2Di(rightCenter.x - 3, rightCenter.y - 3), Point2Di(rightCenter.x, rightCenter.y + 3));
                ctx->DrawLine(Point2Di(rightCenter.x + 3, rightCenter.y - 3), Point2Di(rightCenter.x, rightCenter.y + 3));
                break;
            }
        }
    }

    void UltraCanvasTabbedContainer::RenderContentArea(IRenderContext *ctx) {

        ctx->PushState();
        Rect2Di contentBounds = GetContentAreaBounds();
        if (tabStyle != TabStyle::Flat) {
            if (tabStyle == TabStyle::Modern) {
                ctx->DrawFilledRectangle(contentBounds, contentAreaColor, 1.0, tabContentBorderColor);
            } else {
                ctx->DrawFilledRectangle(contentBounds, contentAreaColor, 1.0, tabContentBorderColor);
            }
        } else {
            ctx->DrawFilledRectangle(contentBounds, contentAreaColor);
        }

        ctx->ClipRect(contentBounds);
        if (scrollState.horizontalPosition != 0 || scrollState.verticalPosition != 0) {
            ctx->Translate(- scrollState.horizontalPosition, - scrollState.verticalPosition);
        }

        if (activeTabIndex >= 0 && activeTabIndex < (int)tabs.size()) {
            auto content = tabs[activeTabIndex]->content.get();
            if (content && content->IsVisible()) {
                content->Render(ctx);
            }
        }

        ctx->PopState();
    }

    void UltraCanvasTabbedContainer::RenderNewTabButton(IRenderContext *ctx) {
        if (!showNewTabButton) return;

        Rect2Di buttonBounds = GetNewTabButtonBounds();
        if (buttonBounds.width <= 0) return;

        Color bgColor = hoveredNewTabButton ? newTabButtonHoverColor : newTabButtonColor;

        ctx->DrawFilledRectangle(buttonBounds, bgColor);

        Point2Di center(buttonBounds.x + buttonBounds.width / 2, buttonBounds.y + buttonBounds.height / 2);
        int size = 8;

        ctx->SetStrokePaint(newTabButtonIconColor);
        ctx->DrawLine(Point2Di(center.x - size/2, center.y), Point2Di(center.x + size/2, center.y));
        ctx->DrawLine(Point2Di(center.x, center.y - size/2), Point2Di(center.x, center.y + size/2));
    }

    bool UltraCanvasTabbedContainer::OnEvent(const UCEvent &event) {
        if (!IsVisible() || IsDisabled()) return false;

        switch (event.type) {
            case UCEventType::KeyDown:
                if (HandleDropdownSearchInput(event)) return true;
                if (HandleKeyDown(event)) return true;
                break;

            case UCEventType::MouseDown:
                if (HandleMouseDown(event)) return true;
                break;

            case UCEventType::MouseUp:
                if (HandleMouseUp(event)) return true;
                break;

            case UCEventType::MouseMove:
                if (HandleMouseMove(event)) return true;
                break;
            case UCEventType::MouseLeave:
                if (HandleMouseMove(event)) return true;
                break;
            default:
                break;
        }

        return UltraCanvasContainer::OnEvent(event);
    }

    bool UltraCanvasTabbedContainer::HandleDropdownSearchInput(const UCEvent &event) {
        if (!dropdownSearchActive || event.type != UCEventType::KeyDown) {
            return false;
        }

        if (event.virtualKey == UCKeys::Escape) {
            ClearDropdownSearch();
            return true;
        }

        if (event.virtualKey == UCKeys::Backspace) {
            if (!dropdownSearchText.empty()) {
                dropdownSearchText.pop_back();
                UpdateOverflowDropdown();
            }
            return true;
        }

        if (event.virtualKey == UCKeys::Return) {
            auto filtered = GetFilteredTabIndices();
            if (!filtered.empty()) {
                SetActiveTab(filtered[0]);
                ClearDropdownSearch();
            }
            return true;
        }

        if (event.character >= 32 && event.character <= 126) {
            dropdownSearchText += static_cast<char>(event.character);
            UpdateOverflowDropdown();
            return true;
        }

        return false;
    }

    bool UltraCanvasTabbedContainer::HandleMouseDown(const UCEvent &event) {
        int x = event.x - bounds.x;
        int y = event.y - bounds.y;
        Rect2Di tabBarBounds = GetTabBarBounds();

        if (!tabBarBounds.Contains(x, y)) return false;

        if (overflowDropdownVisible && overflowDropdown) {
            Rect2Di dropdownBounds = overflowDropdown->GetBounds();
            if (dropdownBounds.Contains(x, y)) {
                return false;
            }
        }

        if (showNewTabButton) {
            Rect2Di newTabBounds = GetNewTabButtonBounds();
            if (newTabBounds.Contains(x, y)) {
                if (onNewTabRequest) {
                    onNewTabRequest();
                }
                return true;
            }
        }

        if (showScrollButtons) {
            Rect2Di leftButton(tabBarBounds.x + tabBarBounds.width - 40, tabBarBounds.y, 20, tabBarBounds.height);
            Rect2Di rightButton(tabBarBounds.x + tabBarBounds.width - 20, tabBarBounds.y, 20, tabBarBounds.height);

            if (leftButton.Contains(x, y)) {
                ScrollTabs(-1);
                return true;
            } else if (rightButton.Contains(x, y)) {
                ScrollTabs(1);
                return true;
            }
        }

        for (int i = tabScrollOffset; i < std::min(tabScrollOffset + maxVisibleTabs, (int)tabs.size()); i++) {
            if (!tabs[i]->visible) continue;

            Rect2Di closeBounds = GetCloseButtonBounds(i);
            if (closeBounds.width > 0 && closeBounds.Contains(x, y)) {
                RemoveTab(i);
                return true;
            }
        }

        int clickedTab = GetTabAtPosition(x, y);
        if (clickedTab >= 0) {
            if (allowTabReordering && event.button == UCMouseButton::Left) {
                draggingTabIndex = clickedTab;
                dragStartPosition = Point2Di(x, y);
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
        int x = event.x - bounds.x;
        int y = event.y - bounds.y;
        Rect2Di tabBarBounds = GetTabBarBounds();

        if (tabBarBounds.Contains(x, y)) {
            int newHoveredTab = GetTabAtPosition(x, y);
            bool needsRedraw = false;

            if (newHoveredTab != hoveredTabIndex) {
                hoveredTabIndex = newHoveredTab;
                needsRedraw = true;
            }

            if (showNewTabButton) {
                Rect2Di newTabBounds = GetNewTabButtonBounds();
                bool wasHovered = hoveredNewTabButton;
                hoveredNewTabButton = newTabBounds.Contains(x, y);
                if (wasHovered != hoveredNewTabButton) {
                    needsRedraw = true;
                }
            }

            if (draggingTabIndex >= 0 && allowTabReordering) {
                float dragDistance = std::sqrt(
                        std::pow(x - dragStartPosition.x, 2) +
                        std::pow(y - dragStartPosition.y, 2)
                );

                if (!isDraggingTab && dragDistance > 5) {
                    isDraggingTab = true;
                }

                if (isDraggingTab) {
                    int targetTab = GetTabAtPosition(x, y);
                    if (targetTab >= 0 && targetTab != draggingTabIndex) {
                        ReorderTabs(draggingTabIndex, targetTab);
                        draggingTabIndex = targetTab;
                    }
                }
                needsRedraw = true;
            }

            if (needsRedraw) {
                RequestRedraw();
            }
            return true;
        } else {
            if (hoveredTabIndex != -1 || hoveredNewTabButton) {
                hoveredTabIndex = -1;
                hoveredCloseButtonIndex = -1;
                hoveredNewTabButton = false;
                RequestRedraw();
            }
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
        std::vector<int> result;
        std::string searchLower = ToLowerCase(dropdownSearchText);

        for (int i = 0; i < (int)tabs.size(); i++) {
            if (!tabs[i]->visible) continue;

            std::string titleLower = ToLowerCase(tabs[i]->title);
            if (titleLower.find(searchLower) != std::string::npos) {
                result.push_back(i);
            }
        }

        return result;
    }

    int UltraCanvasTabbedContainer::CalculateMaxTabWidth() {
        int w = 0;
        for(int i = 0; i < (int)tabs.size(); i++) {
            w = std::min(tabMaxWidth, std::max(w, CalculateTabWidth(i)));
        }
        return w;
    }

    int UltraCanvasTabbedContainer::CalculateTabWidth(int index) {
        if (index < 0 || index >= (int)tabs.size()) return tabMinWidth;

        TabData* tab = tabs[index].get();
        int width = tabPadding * 2;

        if (tab->hasIcon) {
            width += iconSize + iconPadding;
        }

        auto ctx = GetRenderContext();
        if (ctx) {
            ctx->SetFontSize(fontSize);
            int textWidth, textHeight;
            ctx->GetTextLineDimensions(tab->title, textWidth, textHeight);
            width += textWidth;
        } else {
            width += (int)tab->title.length() * 8;
        }

        if (tab->hasBadge && tab->showBadge) {
            CalcBadgeDimensions(tab);
            width += tab->badgeWidth + iconPadding;
        }

        if (ShouldShowCloseButton(tab)) {
            width += closeButtonSize + closeButtonMargin;
        }

        return std::clamp(width, tabMinWidth, tabMaxWidth);
    }

    std::string UltraCanvasTabbedContainer::GetTruncatedTabText(IRenderContext *ctx, const std::string &text,
                                                                int maxWidth) const {
        if (!ctx) return text;

        ctx->SetFontSize(fontSize);
        int textWidth, textHeight;
        ctx->GetTextLineDimensions(text, textWidth, textHeight);

        if (textWidth <= maxWidth) {
            return text;
        }

        std::string truncated = text;
        while (textWidth > maxWidth - 20 && truncated.length() > 1) {
            truncated.pop_back();
            ctx->GetTextLineDimensions(truncated + "...", textWidth, textHeight);
        }

        return truncated + "...";
    }

//    Rect2Di UltraCanvasTabbedContainer::GetTabAreaBounds() const {
//        Rect2Di bounds = GetTabBarBounds();
//
//        if (overflowDropdownVisible && overflowDropdownPosition == OverflowDropdownPosition::Left) {
//            bounds.x += overflowDropdownWidth + tabSpacing;
//            bounds.width -= overflowDropdownWidth + tabSpacing;
//        }
//
//        if (showScrollButtons) {
//            bounds.width -= 40;
//        }
//
//        if (showNewTabButton && newTabButtonPosition == NewTabButtonPosition::FarRight) {
//            bounds.width -= newTabButtonWidth + tabSpacing;
//        }
//
//        return bounds;
//    }

//    Rect2Di UltraCanvasTabbedContainer::GetTabBarBounds() const {
//        Rect2Di bounds = GetBounds();
//        return Rect2Di(0, 0, bounds.width, tabHeight);
//    }
//
//    Rect2Di UltraCanvasTabbedContainer::GetContentAreaBounds() const {
//        Rect2Di bounds = GetBounds();
//        return Rect2Di(0, tabHeight, bounds.width, bounds.height - tabHeight);
//    }

//    Rect2Di UltraCanvasTabbedContainer::GetTabBounds(int index) {
//        if (index < tabScrollOffset || index >= tabScrollOffset + maxVisibleTabs) {
//            return Rect2Di(0, 0, 0, 0);
//        }
//
//        Rect2Di tabArea = GetTabAreaBounds();
//        int xOffset = tabArea.x;
//
//        for (int i = tabScrollOffset; i < index; i++) {
//            if (!tabs[i]->visible) continue;
//            xOffset += CalculateTabWidth(i) + tabSpacing;
//        }
//
//        int tabWidth = CalculateTabWidth(index);
//        return Rect2Di(xOffset, 0, tabWidth, tabHeight);
//    }

    Rect2Di UltraCanvasTabbedContainer::GetCloseButtonBounds(int index) {
        if (!ShouldShowCloseButton(tabs[index].get())) {
            return Rect2Di(0, 0, 0, 0);
        }

        Rect2Di tabBounds = GetTabBounds(index);
        int closeX = tabBounds.x + tabBounds.width - tabPadding - closeButtonSize;
        int closeY = tabBounds.y + (tabBounds.height - closeButtonSize) / 2;

        return Rect2Di(closeX, closeY, closeButtonSize, closeButtonSize);
    }

    Rect2Di UltraCanvasTabbedContainer::GetNewTabButtonBounds() {
        if (!showNewTabButton) {
            return Rect2Di(0, 0, 0, 0);
        }

        Rect2Di tabBarBounds = GetTabBarBounds();
        Rect2Di tabAreaBounds = GetTabAreaBounds();

        int xPos = 0;

        switch (newTabButtonPosition) {
            case NewTabButtonPosition::AfterTabs: {
                xPos = tabAreaBounds.x;
                for (int i = tabScrollOffset; i < std::min(tabScrollOffset + maxVisibleTabs, (int)tabs.size()); i++) {
                    if (!tabs[i]->visible) continue;
                    xPos += CalculateTabWidth(i) + tabSpacing;
                }
                break;
            }

            case NewTabButtonPosition::FarRight:
                xPos = tabBarBounds.width - newTabButtonWidth - (showScrollButtons ? 40 : 0);
                break;

            case NewTabButtonPosition::BeforeTabs:
                xPos = tabAreaBounds.x;
                break;
        }

        return Rect2Di(xPos, 0, newTabButtonWidth, tabBarBounds.height - 1);
    }

    int UltraCanvasTabbedContainer::GetTabAtPosition(int x, int y) {
        for (int i = tabScrollOffset; i < std::min(tabScrollOffset + maxVisibleTabs, (int)tabs.size()); i++) {
            if (!tabs[i]->visible) continue;

            Rect2Di tabBounds = GetTabBounds(i);
            if (tabBounds.Contains(x, y)) {
                return i;
            }
        }
        return -1;
    }

    bool UltraCanvasTabbedContainer::ShouldShowCloseButton(const TabData *tab) {
        if (closeMode == TabCloseMode::NoClose) return false;
        if (!tab->closable) return false;

        if (closeMode == TabCloseMode::ClosableExceptFirst) {
            for (int i = 0; i < (int)tabs.size(); i++) {
                if (tabs[i].get() == tab) {
                    return i > 0;
                }
            }
        }

        return true;
    }

    void UltraCanvasTabbedContainer::CalculateLayout() {
        UpdateOverflowDropdownVisibility();

        Rect2Di tabAreaBounds = GetTabAreaBounds();
        int availableSpace = 0;
        bool isVertical = (tabPosition == TabPosition::Left || tabPosition == TabPosition::Right);

        if (isVertical) {
            // For vertical tabs, calculate based on height (number of tabs that fit vertically)
            availableSpace = tabAreaBounds.height;

            if (showNewTabButton && newTabButtonPosition != NewTabButtonPosition::FarRight) {
                availableSpace -= newTabButtonWidth + tabSpacing;
            }

            if (overflowDropdownVisible && overflowDropdownPosition == OverflowDropdownPosition::Right) {
                availableSpace -= overflowDropdownWidth + tabSpacing;
            }

            if (showScrollButtons) {
                availableSpace -= 40;
            }

            maxVisibleTabs = 0;
            int totalSpace = 0;

            for (int i = tabScrollOffset; i < (int)tabs.size(); i++) {
                if (!tabs[i]->visible) continue;

                // For vertical tabs, each tab takes up tabHeight + spacing
                if (totalSpace + tabHeight > availableSpace) {
                    break;
                }

                totalSpace += tabHeight + tabSpacing;
                maxVisibleTabs++;
            }
        } else {
            // For horizontal tabs, calculate based on width
            availableSpace = tabAreaBounds.width;

            if (showNewTabButton && newTabButtonPosition != NewTabButtonPosition::FarRight) {
                availableSpace -= newTabButtonWidth + tabSpacing;
            }

            if (overflowDropdownVisible && overflowDropdownPosition == OverflowDropdownPosition::Right) {
                availableSpace -= overflowDropdownWidth + tabSpacing;
            }

            if (showScrollButtons) {
                availableSpace -= 40;
            }

            maxVisibleTabs = 0;
            int totalSpace = 0;

            for (int i = tabScrollOffset; i < (int)tabs.size(); i++) {
                if (!tabs[i]->visible) continue;

                int tabWidth = CalculateTabWidth(i);
                if (totalSpace + tabWidth > availableSpace) {
                    break;
                }

                totalSpace += tabWidth + tabSpacing;
                maxVisibleTabs++;
            }
        }

        showScrollButtons = enableTabScrolling && (tabScrollOffset > 0 ||
                                                   tabScrollOffset + maxVisibleTabs < (int)tabs.size());

        if (!showOverflowDropdown) {
            overflowDropdownVisible = false;
            if (overflowDropdown) {
                overflowDropdown->SetVisible(false);
            }
        }

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
            RequestRedraw();
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
    }

    void UltraCanvasTabbedContainer::PositionTabContent(int index) {
        if (index < 0 || index >= (int)tabs.size()) return;

        auto content = tabs[index]->content.get();
        if (!content) return;

        Rect2Di contentBounds = GetContentAreaBounds();
        content->SetBounds(contentBounds.x, contentBounds.y, contentBounds.width, contentBounds.height);
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
            InvalidateTabbar();
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
            InvalidateTabbar();
        }
    }

    void UltraCanvasTabbedContainer::SetTabPosition(TabPosition position) {
        tabPosition = position;
        InvalidateTabbar();
    }

    bool UltraCanvasTabbedContainer::IsTabEnabled(int index) const {
        if (index >= 0 && index < (int)tabs.size()) {
            return tabs[index]->enabled;
        }
        return false;
    }

    void UltraCanvasTabbedContainer::SetCloseMode(TabCloseMode mode) {
        closeMode = mode;
        InvalidateTabbar();
    }

    void UltraCanvasTabbedContainer::SetShowTabSeparators(bool show) {
        showTabSeparators = show;
        InvalidateTabbar();
    }

    void UltraCanvasTabbedContainer::SetTabSeparatorColor(const Color& color) {
        tabSeparatorColor = color;
        InvalidateTabbar();
    }

    void UltraCanvasTabbedContainer::SetTabBackgroundColor(int index, const Color& color) {
        if (index >= 0 && index < (int)tabs.size()) {
            tabs[index]->backgroundColor = color;
            InvalidateTabbar();
        }
    }

    Color UltraCanvasTabbedContainer::GetTabBackgroundColor(int index) const {
        if (index >= 0 && index < (int)tabs.size()) {
            return tabs[index]->backgroundColor;
        }
        return Color(240, 240, 240);
    }

    void UltraCanvasTabbedContainer::SetTabTextColor(int index, const Color& color) {
        if (index >= 0 && index < (int)tabs.size()) {
            tabs[index]->textColor = color;
            InvalidateTabbar();
        }
    }

    Color UltraCanvasTabbedContainer::GetTabTextColor(int index) const {
        if (index >= 0 && index < (int)tabs.size()) {
            return tabs[index]->textColor;
        }
        return Colors::Black;
    }

    bool UltraCanvasTabbedContainer::CalcBadgeDimensions(TabData* tab) {
        if (!tab->badgeText.empty()) {
            auto ctx = GetRenderContext();
            ctx->SetFontSize(9);
            ctx->GetTextLineDimensions(tab->badgeText, tab->badgeWidth, tab->badgeHeight);
            tab->badgeWidth = std::max(tab->badgeWidth, 12);
            tab->badgeWidth += 6;
            tab->badgeHeight += 6;
            return true;
        } else {
            tab->badgeWidth = tab->badgeHeight = 0;
            return false;
        }
    }



    Rect2Di UltraCanvasTabbedContainer::GetTabBarBounds() {
        Rect2Di bounds = GetBounds();
        int verticalTabWidth;
        switch (tabPosition) {
            case TabPosition::Top:
                return Rect2Di(0, 0, bounds.width, tabHeight);

            case TabPosition::Bottom:
                return Rect2Di(0, bounds.height - tabHeight, bounds.width, tabHeight);

            case TabPosition::Left:
                return Rect2Di(0, 0, CalculateMaxTabWidth(), bounds.height);

            case TabPosition::Right:
                verticalTabWidth = CalculateMaxTabWidth();
                return Rect2Di(bounds.width - verticalTabWidth, 0, verticalTabWidth, bounds.height);

            default:
                return Rect2Di(0, 0, bounds.width, tabHeight);
        }
    }

    Rect2Di UltraCanvasTabbedContainer::GetContentAreaBounds() {
        Rect2Di bounds = GetBounds();
        int verticalTabWidth;

        switch (tabPosition) {
            case TabPosition::Top:
                return Rect2Di(0, tabHeight, bounds.width, bounds.height - tabHeight);

            case TabPosition::Bottom:
                return Rect2Di(0, 0, bounds.width, bounds.height - tabHeight);

            case TabPosition::Left:
                verticalTabWidth = CalculateMaxTabWidth();
                return Rect2Di(verticalTabWidth, 0, bounds.width - verticalTabWidth, bounds.height);

            case TabPosition::Right:
                return Rect2Di(0, 0, bounds.width - CalculateMaxTabWidth(), bounds.height);

            default:
                return Rect2Di(0, tabHeight, bounds.width, bounds.height - tabHeight);
        }
    }

    Rect2Di UltraCanvasTabbedContainer::GetTabBounds(int index) {
        if (index < tabScrollOffset || index >= tabScrollOffset + maxVisibleTabs) {
            return Rect2Di(0, 0, 0, 0);
        }

        Rect2Di tabArea = GetTabAreaBounds();

        switch (tabPosition) {
            case TabPosition::Top:
            case TabPosition::Bottom: {
                // Horizontal tab layout
                int tabWidth = CalculateTabWidth(index);
                int xOffset = tabArea.x;
                for (int i = tabScrollOffset; i < index; i++) {
                    if (!tabs[i]->visible) continue;
                    xOffset += CalculateTabWidth(i) + tabSpacing;
                }

                if (tabPosition == TabPosition::Top) {
                    return Rect2Di(xOffset, 0, tabWidth, tabHeight);
                } else {
                    Rect2Di bounds = GetBounds();
                    return Rect2Di(xOffset, bounds.height - tabHeight, tabWidth, tabHeight);
                }
            }

            case TabPosition::Left:
            case TabPosition::Right: {
                // Vertical tab layout - tabs stacked vertically with horizontal text
                // Each tab has fixed height (tabHeight) and variable width (tabWidth)
                int verticalTabWidth = CalculateMaxTabWidth();
                int yOffset = tabArea.y;
                for (int i = tabScrollOffset; i < index; i++) {
                    if (!tabs[i]->visible) continue;
                    yOffset += tabHeight + tabSpacing;  // Changed from CalculateTabWidth to tabHeight
                }

                if (tabPosition == TabPosition::Left) {
                    return Rect2Di(0, yOffset, verticalTabWidth, tabHeight);  // Swapped: width first, then height
                } else {
                    Rect2Di bounds = GetBounds();
                    return Rect2Di(bounds.width - verticalTabWidth, yOffset, verticalTabWidth, tabHeight);
                }
            }

            default: {
                // Fallback to top position
                int tabWidth = CalculateTabWidth(index);
                int xOffset = tabArea.x;
                for (int i = tabScrollOffset; i < index; i++) {
                    if (!tabs[i]->visible) continue;
                    xOffset += CalculateTabWidth(i) + tabSpacing;
                }
                return Rect2Di(xOffset, 0, tabWidth, tabHeight);
            }
        }
    }

    Rect2Di UltraCanvasTabbedContainer::GetTabAreaBounds() {
        Rect2Di bounds = GetTabBarBounds();

        switch (tabPosition) {
            case TabPosition::Top:
            case TabPosition::Bottom: {
                // Horizontal tab bar - adjust width
                if (overflowDropdownVisible && overflowDropdownPosition == OverflowDropdownPosition::Left) {
                    bounds.x += overflowDropdownWidth + tabSpacing;
                    bounds.width -= overflowDropdownWidth + tabSpacing;
                }

                if (showScrollButtons) {
                    bounds.width -= 40;
                }

                if (showNewTabButton && newTabButtonPosition == NewTabButtonPosition::FarRight) {
                    bounds.width -= newTabButtonWidth + tabSpacing;
                }
                break;
            }

            case TabPosition::Left:
            case TabPosition::Right: {
                // Vertical tab bar - adjust height
                if (overflowDropdownVisible && overflowDropdownPosition == OverflowDropdownPosition::Left) {
                    bounds.y += overflowDropdownWidth + tabSpacing;
                    bounds.height -= overflowDropdownWidth + tabSpacing;
                }

                if (showScrollButtons) {
                    bounds.height -= 40;
                }

                if (showNewTabButton && newTabButtonPosition == NewTabButtonPosition::FarRight) {
                    bounds.height -= newTabButtonWidth + tabSpacing;
                }
                break;
            }
        }

        return bounds;
    }

    void UltraCanvasTabbedContainer::RenderTab(int index, IRenderContext *ctx) {
        if (index < 0 || index >= (int)tabs.size()) return;

        Rect2Di tabBounds = GetTabBounds(index);
        if (tabBounds.width <= 0 || tabBounds.height <= 0) return;

        TabData* tab = tabs[index].get();

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

        if (tab->backgroundColor != Color(240, 240, 240)) {
            bgColor = tab->backgroundColor;
        }
        if (tab->textColor != Colors::Black) {
            textColor = tab->textColor;
        }
        ctx->PushState();

        // Render tab background based on style
        switch (tabStyle) {
            case TabStyle::Rounded: {
                if (tabCornerRadius > 0) {
                    // Browser-style rounded tabs with custom path
                    ctx->ClearPath();

                    float x = static_cast<float>(tabBounds.x);
                    float y = static_cast<float>(tabBounds.y);
                    float w = static_cast<float>(tabBounds.width);
                    float h = static_cast<float>(tabBounds.height);
                    float radius = tabCornerRadius;

                    // Adjust based on tab position
                    switch (tabPosition) {
                        case TabPosition::Top:
                            // Rounded top corners, square bottom corners
                            // Start at bottom-left
                            ctx->MoveTo(x, y + h);
                            // Line to top-left, then arc for top-left corner
                            ctx->LineTo(x, y + radius);
                            ctx->Arc(x + radius, y + radius, radius, M_PI, 3 * M_PI / 2);
                            // Line across top to top-right arc
                            ctx->Arc(x + w - radius, y + radius, radius, 3 * M_PI / 2, 2 * M_PI);
                            // Line down to bottom-right
                            ctx->LineTo(x + w, y + h);
                            // Close path back to start
                            ctx->LineTo(x, y + h);
                            break;

                        case TabPosition::Bottom:
                            // Square top corners, rounded bottom corners
                            ctx->MoveTo(x, y);
                            ctx->LineTo(x, y + h - radius);
                            ctx->Arc(x + radius, y + h - radius, radius, M_PI, M_PI / 2);
                            ctx->Arc(x + w - radius, y + h - radius, radius, M_PI / 2, 0);
                            ctx->LineTo(x + w, y);
                            ctx->LineTo(x, y);
                            break;

                        case TabPosition::Left:
                            // Rounded left corners, square right corners
                            ctx->MoveTo(x + w, y);
                            ctx->LineTo(x + radius, y);
                            ctx->Arc(x + radius, y + radius, radius, 3 * M_PI / 2, M_PI);
                            ctx->Arc(x + radius, y + h - radius, radius, M_PI, M_PI / 2);
                            ctx->LineTo(x + w, y + h);
                            ctx->LineTo(x + w, y);
                            break;

                        case TabPosition::Right:
                            // Square left corners, rounded right corners
                            ctx->MoveTo(x, y);
                            ctx->LineTo(x + w - radius, y);
                            ctx->Arc(x + w - radius, y + radius, radius, 3 * M_PI / 2, 0);
                            ctx->Arc(x + w - radius, y + h - radius, radius, 0, M_PI / 2);
                            ctx->LineTo(x, y + h);
                            ctx->LineTo(x, y);
                            break;
                    }

                    ctx->ClosePath();

                    // Fill and stroke
                    ctx->SetFillPaint(bgColor);
                    ctx->FillPathPreserve();
                    if (tabBorderColor.a > 0) {
                        ctx->SetStrokePaint(tabBorderColor);
                        ctx->SetStrokeWidth(1.0f);
                        ctx->StrokePathPreserve();
                    }
                    ctx->ClearPath();

                } else {
                    ctx->DrawFilledRectangle(tabBounds, bgColor, 1.0, tabBorderColor);
                }
                break;
            }

            case TabStyle::Classic: {
                // Classic style: 3D raised effect with shadows
                ctx->DrawFilledRectangle(tabBounds, bgColor, 1.0, tabBorderColor);
                break;
//                // Add 3D effect for active tab
//                if (index == activeTabIndex) {
//                    // Light edge on top and left (highlight)
//                    Color highlightColor = Color(255, 255, 255, 180);
//                    ctx->SetStrokePaint(highlightColor);
//
//                    if (tabPosition == TabPosition::Top) {
//                        ctx->DrawLine(Point2Di(tabBounds.x, tabBounds.y + tabBounds.height - 1),
//                                      Point2Di(tabBounds.x, tabBounds.y));
//                        ctx->DrawLine(Point2Di(tabBounds.x, tabBounds.y),
//                                      Point2Di(tabBounds.x + tabBounds.width - 1, tabBounds.y));
//                    } else {
//                        ctx->DrawLine(Point2Di(tabBounds.x, tabBounds.y),
//                                      Point2Di(tabBounds.x, tabBounds.y + tabBounds.height - 1));
//                        ctx->DrawLine(Point2Di(tabBounds.x, tabBounds.y),
//                                      Point2Di(tabBounds.x + tabBounds.width - 1, tabBounds.y));
//                    }
//
//                    // Dark edge (shadow)
//                    Color shadowColor = Color(128, 128, 128, 180);
//                    ctx->SetStrokePaint(shadowColor);
//
//                    if (tabPosition == TabPosition::Top || tabPosition == TabPosition::Bottom) {
//                        ctx->DrawLine(Point2Di(tabBounds.x + tabBounds.width - 1, tabBounds.y),
//                                      Point2Di(tabBounds.x + tabBounds.width - 1, tabBounds.y + tabBounds.height - 1));
//                    } else {
//                        ctx->DrawLine(Point2Di(tabBounds.x, tabBounds.y + tabBounds.height - 1),
//                                      Point2Di(tabBounds.x + tabBounds.width - 1, tabBounds.y + tabBounds.height - 1));
//                    }
//                } else {
//                    // Subtle shadow for inactive tabs
//                    Color shadowColor = Color(100, 100, 100, 80);
//                    ctx->SetStrokePaint(shadowColor);
//
//                    if (tabPosition == TabPosition::Top || tabPosition == TabPosition::Bottom) {
//                        ctx->DrawLine(Point2Di(tabBounds.x + tabBounds.width - 1, tabBounds.y + 2),
//                                      Point2Di(tabBounds.x + tabBounds.width - 1, tabBounds.y + tabBounds.height - 1));
//                    } else {
//                        ctx->DrawLine(Point2Di(tabBounds.x + 2, tabBounds.y + tabBounds.height - 1),
//                                      Point2Di(tabBounds.x + tabBounds.width - 1, tabBounds.y + tabBounds.height - 1));
//                    }
//                }
//                break;
            }

            case TabStyle::Modern: {
                // Modern style: Flat with indicator line for active tab
                ctx->DrawFilledRectangle(tabBounds, bgColor, 1, bgColor);

                // Draw indicator for active tab based on position
                if (index == activeTabIndex) {
                    int indicatorHeight = 2;
                    Color indicatorColor = Color(33, 150, 243); // Material Blue
                    Rect2Di indicatorRect;

                    switch (tabPosition) {
                        case TabPosition::Top:
                            indicatorRect = Rect2Di(
                                    tabBounds.x,
                                    tabBounds.y + tabBounds.height - indicatorHeight,
                                    tabBounds.width,
                                    indicatorHeight
                            );
                            break;

                        case TabPosition::Bottom:
                            indicatorRect = Rect2Di(
                                    tabBounds.x,
                                    tabBounds.y,
                                    tabBounds.width,
                                    indicatorHeight
                            );
                            break;

                        case TabPosition::Left:
                            indicatorRect = Rect2Di(
                                    tabBounds.x + tabBounds.width - indicatorHeight,
                                    tabBounds.y,
                                    indicatorHeight,
                                    tabBounds.height
                            );
                            break;

                        case TabPosition::Right:
                            indicatorRect = Rect2Di(
                                    tabBounds.x,
                                    tabBounds.y,
                                    indicatorHeight,
                                    tabBounds.height
                            );
                            break;
                    }

                    ctx->DrawFilledRectangle(indicatorRect, indicatorColor, 1, indicatorColor);
                }
                break;
            }

            case TabStyle::Flat: {
                // Flat style: Minimal design, no borders except for active tab background
                ctx->SetFillPaint(bgColor);
                ctx->FillRectangle(tabBounds);
                break;
            }

            default:
                // Fallback to simple rectangle
                ctx->DrawFilledRectangle(tabBounds, bgColor, 1.0, tabBorderColor);
                break;
        }

        if (index == activeTabIndex && tabStyle != TabStyle::Modern) {
            switch (tabPosition) {
                case TabPosition::Top:
                    ctx->SetStrokePaint(activeTabColor);
                    ctx->SetStrokeWidth(3.0f);
                    ctx->DrawLine(tabBounds.BottomLeft(), tabBounds.BottomRight());
                    break;
                case TabPosition::Bottom:
                    ctx->SetStrokePaint(activeTabColor);
                    ctx->SetStrokeWidth(3.0f);
                    ctx->DrawLine(tabBounds.TopLeft(), tabBounds.TopRight());
                    break;
                default:
                    break;
            }
            ctx->SetStrokeWidth(1.0f);
        }

        // Render tab content (text, icons, badges, close button)
        Rect2Di contentArea = tabBounds;
        contentArea.x += tabPadding;
        contentArea.width -= tabPadding * 2;

        int xOffset = contentArea.x;

        if (tab->hasIcon && !tab->iconPath.empty()) {
            RenderTabIcon(index, ctx);
            xOffset += iconSize + iconPadding;
            contentArea.width -= (iconSize + iconPadding);
        }

        if (ShouldShowCloseButton(tab)) {
            contentArea.width -= (closeButtonSize + closeButtonMargin);
        }

        if (tab->hasBadge && tab->showBadge) {
            contentArea.width -= (tab->badgeWidth + iconPadding);
        }

        if (contentArea.width > 0) {
            std::string displayText = GetTruncatedTabText(ctx, tab->title, contentArea.width);

            ctx->SetTextPaint(textColor);
            ctx->SetFontSize(fontSize);
            int txtW, txtH;
            ctx->GetTextLineDimensions(displayText, txtW, txtH);
            int textY = tabBounds.y + (tabBounds.height - txtH) / 2;
            ctx->DrawText(displayText, Point2Di(xOffset, textY));

            xOffset += txtW + iconPadding;
        }

        if (tab->hasBadge && tab->showBadge) {
            RenderTabBadge(index, ctx);
        }

        if (ShouldShowCloseButton(tab)) {
            RenderCloseButton(index, ctx);
        }

        // Draw tab separator if enabled
        if (showTabSeparators && index < (int)tabs.size() - 1) {
            ctx->SetStrokePaint(tabSeparatorColor);
            ctx->DrawLine(Point2Di(tabBounds.x + tabBounds.width - 1, tabBounds.y + 4),
                          Point2Di(tabBounds.x + tabBounds.width - 1, tabBounds.y + tabBounds.height - 4));
        }
        ctx->PopState();
    }
} // namespace UltraCanvas
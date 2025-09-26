// UltraCanvasMenu.cpp
// Interactive menu component with styling options and submenu support
// Version: 1.2.5
// Last Modified: 2025-01-08
// Author: UltraCanvas Framework

#include <vector>
#include "UltraCanvasMenu.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasApplication.h"
//#include "UltraCanvasZOrderManager.h"
#include "UltraCanvasWindow.h"

namespace UltraCanvas {

    void UltraCanvasMenu::Show() {
        // Ensure menu is always on top when shown
//        SetZIndex(UltraCanvas::ZLayers::Menus + 100);

        // Existing Show() implementation continues...
        if (currentState != MenuState::Visible && currentState != MenuState::Opening) {
            currentState = style.enableAnimations ? MenuState::Opening : MenuState::Visible;
            SetVisible(true);
            if (style.enableAnimations) {
                StartAnimation();
            }
            AddThisPopupElementToWindow();

            hoveredIndex = -1;
            keyboardIndex = -1;
            keyboardNavigation = false;
            needCalculateSize = true;

            if (onMenuOpened) onMenuOpened();

            // Force immediate redraw with z-order update
            RequestRedraw();

            std::cout << "Menu '" << GetIdentifier()
                      << "' shown with Z=" << GetZIndex() << std::endl;
        }
    }

    void UltraCanvasMenu::Hide() {
        if (currentState != MenuState::Hidden && currentState != MenuState::Closing) {
            // Always hide immediately for dropdown menus without animation
            currentState = style.enableAnimations ? MenuState::Closing : MenuState::Hidden;

            if (style.enableAnimations) {
                StartAnimation();
            } else {
                SetVisible(false);
            }

            //UltraCanvasMenuManager::UnregisterActiveMenu(shared_from_this());

            // Close all submenus
            CloseAllSubmenus();
            RemoveThisPopupElementFromWindow();
            needCalculateSize = true;
            if (onMenuClosed) onMenuClosed();
//            RequestRedraw();

            std::cout << "Menu '" << GetIdentifier() << "' hidden. State: " << (int)currentState
                      << " Visible: " << IsVisible() << std::endl;
        }
    }

    void UltraCanvasMenu::Render() {
        IRenderContext *ctx = GetRenderContext();
        // FIX: Simplified visibility check - if not visible at all, don't render
        if (!IsVisible() || !ctx) return;
        if (menuType == MenuType::Menubar) {
            if (needCalculateSize) {
                CalculateAndUpdateSize();
            }

            // Render background
            Rect2Di bounds = GetBounds();
            ctx->SetFillColor(style.backgroundColor);
            ctx->FillRectangle(bounds);

            // Draw border
            if (style.borderWidth > 0) {
                ctx->SetStrokeColor(style.borderColor);
                ctx->SetStrokeWidth(style.borderWidth);
                ctx->DrawRectangle(bounds);
            }

            // Render items
            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                if (items[i].visible) {
                    RenderItem(i, items[i], ctx);
                }
            }

            // Render keyboard navigation highlight
            if (keyboardNavigation && keyboardIndex >= 0 && keyboardIndex < static_cast<int>(items.size())) {
                RenderKeyboardHighlight(GetItemBounds(keyboardIndex), ctx);
            }
        }
    }

    void UltraCanvasMenu::RenderPopupContent() {
        IRenderContext *ctx = GetRenderContext();
        // FIX: Simplified visibility check - if not visible at all, don't render
        if (!IsVisible() || currentState == MenuState::Hidden || !ctx) return;

        if (needCalculateSize) {
            CalculateAndUpdateSize();
        }

        // Apply animation if active
        if (style.enableAnimations && (currentState == MenuState::Opening || currentState == MenuState::Closing)) {
            UpdateAnimation();
        }

        // Render shadow first (for dropdown menus)
        if (style.showShadow && (menuType == MenuType::PopupMenu || menuType == MenuType::SubmenuMenu)) {
            RenderShadow(ctx);
        }

        // Render background
        Rect2Di bounds = GetBounds();
        ctx->SetFillColor(style.backgroundColor);
        ctx->FillRectangle(bounds);

        // Draw border
        if (style.borderWidth > 0) {
            ctx->SetStrokeColor(style.borderColor);
            ctx->SetStrokeWidth(style.borderWidth);
            ctx->DrawRectangle(bounds);
        }

        // Render items
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (items[i].visible) {
                RenderItem(i, items[i], ctx);
            }
        }

        // Render keyboard navigation highlight
        if (keyboardNavigation && keyboardIndex >= 0 && keyboardIndex < static_cast<int>(items.size())) {
            RenderKeyboardHighlight(GetItemBounds(keyboardIndex), ctx);
        }
    }

    bool UltraCanvasMenu::OnEvent(const UCEvent &event) {
        // Debug output for dropdown menus
//        if ((menuType == MenuType::DropdownMenu || menuType == MenuType::SubmenuMenu) &&
//            (event.type == UCEventType::MouseDown || event.type == UCEventType::MouseUp)) {
//            std::cout << "Dropdown Menu '" << GetIdentifier() << "' received event type: " << (int)event.type
//                      << " at (" << event.x << "," << event.y << ")"
//                      << " Menu bounds: (" << GetX() << "," << GetY()
//                      << "," << GetWidth() << "," << GetHeight() << ")"
//                      << " Visible: " << IsVisible()
//                      << " State: " << (int)currentState << std::endl;
//        }

        // Handle the event
        bool handled = HandleEvent(event);

        // If not handled, pass to base class
        if (!handled) {
            return UltraCanvasUIElement::OnEvent(event);
        }
        return true;
    }

    bool UltraCanvasMenu::HandleEvent(const UCEvent &event) {
        // FIX: Check menu state for dropdown menus
        if (menuType == MenuType::PopupMenu || menuType == MenuType::SubmenuMenu) {
            if (currentState == MenuState::Hidden) {
                return false;
            }
        }

        if (!IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseMove:
                return HandleMouseMove(event);

            case UCEventType::MouseDown:
                return HandleMouseDown(event);

            case UCEventType::MouseUp:
                return HandleMouseUp(event);

            case UCEventType::KeyDown:
                return HandleKeyDown(event);

            case UCEventType::MouseLeave:
                hoveredIndex = -1;
                break;

            default:
                break;
        }

        return false;
    }

    void UltraCanvasMenu::SetMenuType(MenuType type) {
        menuType = type;

        // Adjust default properties based on type
        switch (type) {
            case MenuType::Menubar:
                orientation = MenuOrientation::Horizontal;
                style.itemHeight = 32.0f;
                break;
            case MenuType::PopupMenu:
                orientation = MenuOrientation::Vertical;
                style.showShadow = true;
                SetVisible(false);
                break;
            case MenuType::SubmenuMenu:
                SetVisible(false);
                orientation = MenuOrientation::Vertical;
                break;
        }
    }

    void UltraCanvasMenu::OpenSubmenu(int itemIndex) {
        if (itemIndex < 0 || itemIndex >= static_cast<int>(items.size())) return;

        const MenuItemData& item = items[itemIndex];
        if (item.subItems.empty()) return;

        // Close existing submenu
        CloseActiveSubmenu();

        // Create and show new submenu
        activeSubmenu = std::make_shared<UltraCanvasMenu>(
                GetIdentifier() + "_submenu_" + std::to_string(itemIndex),
                GetIdentifierID() + 1000 + itemIndex,
                0, 0, 150, 100
        );

        activeSubmenu->SetMenuType(MenuType::SubmenuMenu);
        activeSubmenu->SetStyle(style);
        activeSubmenu->parentMenu = std::static_pointer_cast<UltraCanvasMenu>(shared_from_this());

        auto parentWindow = GetWindow();

        // Add items to submenu
        for (const auto& subItem : item.subItems) {
            activeSubmenu->AddItem(subItem);
        }

        // Position submenu
        PositionSubmenu(activeSubmenu, itemIndex);

        if (parentWindow) {
            // Add the submenu to the window's element collection so it gets rendered
//            parentWindow->AddChild(activeSubmenu);
            std::cout << "Added submenu '" << activeSubmenu->GetIdentifier()
                      << "' to window for rendering" << std::endl;
            activeSubmenu->SetWindow(parentWindow);
//            parentWindow->AddPopupElement(activeSubmenu.get());
        }

        // Show submenu
        activeSubmenu->Show();
        childMenus.push_back(activeSubmenu);
    }

    void UltraCanvasMenu::CloseActiveSubmenu() {
        if (activeSubmenu) {
            activeSubmenu->CloseAllSubmenus();
            activeSubmenu->Hide();

            auto parentWindow = GetWindow();
            if (parentWindow) {
                parentWindow->RemoveChild(activeSubmenu);
//                parentWindow->RemovePopupElement(activeSubmenu.get());
//                parentWindow->CleanupRemovedPopupElements();
                std::cout << "Removed submenu '" << activeSubmenu->GetIdentifier()
                          << "' from window" << std::endl;
            }

            // Remove from child menus
            auto it = std::find(childMenus.begin(), childMenus.end(), activeSubmenu);
            if (it != childMenus.end()) {
                childMenus.erase(it);
            }

            activeSubmenu.reset();
        }
    }

    void UltraCanvasMenu::CloseAllSubmenus() {
        for (auto& child : childMenus) {
            if (child) {
                child->Hide();
                child->CloseAllSubmenus();

                auto parentWindow = GetWindow();
                if (parentWindow) {
                    parentWindow->RemoveChild(child);
                    parentWindow->RemovePopupElement(child.get());
                }
            }
        }
        childMenus.clear();
        activeSubmenu.reset();
    }

    void UltraCanvasMenu::CloseMenutree() {
        auto parentMnu = parentMenu.lock();
        if (!parentMnu || parentMnu->menuType == MenuType::Menubar) {
            Hide();
            return;
        }

        auto mnu = parentMnu;
        while(parentMnu && parentMnu->menuType != MenuType::Menubar) {
            mnu = parentMnu;
            parentMnu = parentMnu->parentMenu.lock();
        }
        if (mnu) {
            mnu->Hide();
        }
    }

    int UltraCanvasMenu::GetItemX(int index) const {
        if (orientation == MenuOrientation::Vertical) {
            return 0.0f;
        }

        // FIXED: Calculate proper X position for horizontal items
        int x = style.paddingLeft;

        for (int i = 0; i < index && i < static_cast<int>(items.size()); ++i) {
            if (!items[i].visible) continue;
            x += CalculateItemWidth(items[i]) + style.paddingLeft + style.paddingRight;
            if (items[i].type != MenuItemType::Separator) {
                x += style.iconSpacing; // Add spacing between items
            }
        }

        return x;
    }

    int UltraCanvasMenu::GetItemY(int index) const {
        int y = style.paddingTop;

        for (int i = 0; i < index && i < static_cast<int>(items.size()); ++i) {
            if (!items[i].visible) continue;

            if (items[i].type == MenuItemType::Separator) {
                y += style.separatorHeight;
            } else {
                y += style.itemHeight;
            }
        }

        return y;
    }

    bool UltraCanvasMenu::Contains(int x, int y) {
        if (menuType == MenuType::PopupMenu || menuType == MenuType::SubmenuMenu) {
            // Only check bounds if menu is actually visible
            if (!IsVisible() || currentState == MenuState::Hidden) {
                return false;
            }
        }

        return x >= GetX() && x < GetX() + GetWidth() &&
               y >= GetY() && y < GetY() + GetHeight();
    }

    void UltraCanvasMenu::CalculateAndUpdateSize() {
        needCalculateSize = false;
        if (items.empty()) {
            SetWidth(100);
            SetHeight(style.itemHeight);
            return;
        }

        if (orientation == MenuOrientation::Horizontal) {
            // FIXED: Horizontal layout calculation
            int totalWidth = 0;
            int maxHeight = style.itemHeight;

            for (const auto& item : items) {
                if (!item.visible) continue;
                totalWidth += CalculateItemWidth(item) + style.paddingLeft + style.paddingRight;
//                if (item.type != MenuItemType::Separator) {
//                    totalWidth += style.iconSpacing; // Add spacing between items
//                }
            }

            // Set dimensions for horizontal menu
            SetWidth(totalWidth);
            SetHeight(maxHeight);

        } else {
            // Vertical layout calculation
            int maxWidth = 0;
            int totalHeight = 0;

            for (const auto& item : items) {
                if (!item.visible) continue;

                int itemWidth = CalculateItemWidth(item);
                maxWidth = std::max(maxWidth, itemWidth);

                if (item.type == MenuItemType::Separator) {
                    totalHeight += style.separatorHeight;
                } else {
                    totalHeight += style.itemHeight;
                }
            }

            SetWidth(maxWidth + style.paddingLeft + style.paddingRight);
            SetHeight(totalHeight);
        }
    }

    void UltraCanvasMenu::RenderItem(int index, const MenuItemData &item, IRenderContext *ctx) {
        if (!item.visible) return;

        Rect2Di itemBounds = GetItemBounds(index);

        // Draw item background
        Color bgColor = GetItemBackgroundColor(index, item);
        if (bgColor.a > 0) {
            ctx->SetFillColor(bgColor);
            ctx->FillRectangle(itemBounds);
        }

        // Handle separator
        if (item.type == MenuItemType::Separator) {
            RenderSeparator(itemBounds, ctx);
            return;
        }

        Point2Di textSize = ctx->MeasureText(item.label);
        int fontHeight = textSize.y;
        int currentX = itemBounds.x + style.paddingLeft;
        int textY = itemBounds.y + (itemBounds.height - fontHeight) / 2;

        // Render checkbox/radio
        if (item.type == MenuItemType::Checkbox || item.type == MenuItemType::Radio) {
            RenderCheckbox(item, Point2Di(currentX, textY), ctx);
            currentX += style.iconSize + style.iconSpacing;
        }

        // Render icon
        if (!item.iconPath.empty()) {
            int iconY = itemBounds.y + (itemBounds.height - style.iconSize) / 2;
            RenderIcon(item.iconPath, Point2Di(currentX, iconY), ctx);
            currentX += style.iconSize + style.iconSpacing;
        }

        // Render text
        if (!item.label.empty()) {
            Color textColor = item.enabled ?
                              (index == hoveredIndex ? style.hoverTextColor : style.textColor) :
                              style.disabledTextColor;

            ctx->SetTextColor(textColor);
            ctx->DrawText(item.label, Point2Di(currentX, textY));
        }

        // Render shortcut (for vertical menus)
        if (!item.shortcut.empty() && orientation == MenuOrientation::Vertical) {
            int shortcutX = itemBounds.x + itemBounds.width - style.paddingRight -
                              ctx->GetTextWidth(item.shortcut.c_str());
            ctx->SetTextColor(style.shortcutColor);
            ctx->DrawText(item.shortcut, Point2Di(shortcutX, textY));
        }

        // Render submenu arrow (for vertical menus)
        if (!item.subItems.empty() && orientation == MenuOrientation::Vertical) {
            RenderSubmenuArrow(Point2Di(itemBounds.x + itemBounds.width - style.paddingRight - 2,
                                       itemBounds.y + itemBounds.height / 2), ctx);
        }
    }

    Rect2Di UltraCanvasMenu::GetItemBounds(int index) const {
        Rect2Di bounds;

        if (orientation == MenuOrientation::Horizontal) {
            int currentX = GetX();

            for (int i = 0; i < index && i < static_cast<int>(items.size()); ++i) {
                if (items[i].visible) {
                    currentX += CalculateItemWidth(items[i]) + style.paddingLeft + style.paddingRight;
                }
            }

            bounds.x = currentX;
            bounds.y = GetY();
            bounds.width = CalculateItemWidth(items[index]) + style.paddingLeft + style.paddingRight;
            bounds.height = style.itemHeight;
        } else {
            int currentY = GetY();

            for (int i = 0; i < index && i < static_cast<int>(items.size()); ++i) {
                if (items[i].visible) {
                    currentY += (items[i].type == MenuItemType::Separator) ?
                                style.separatorHeight : style.itemHeight;
                }
            }

            bounds.x = GetX();
            bounds.y = currentY;
            bounds.width = GetWidth();
            bounds.height = (items[index].type == MenuItemType::Separator) ?
                            style.separatorHeight : style.itemHeight;
        }

        return bounds;
    }

    int UltraCanvasMenu::CalculateItemWidth(const MenuItemData &item) const {
        int width = 0;
        auto ctx = GetRenderContext();
        // Icon space
        if (!item.iconPath.empty()) {
            width += style.iconSize + style.iconSpacing;
        }

        // Text width - FIX: Convert std::string to const char*
        if (!item.label.empty()) {
            width += ctx->GetTextWidth(item.label.c_str());
        }

        // Shortcut width - FIX: Convert std::string to const char*
        if (!item.shortcut.empty()) {
            width += style.shortcutSpacing + ctx->GetTextWidth(item.shortcut.c_str());
        }

        // Submenu arrow
        if (!item.subItems.empty()) {
            width += 20;  // Arrow space
        }

        // Checkbox/radio space
        if (item.type == MenuItemType::Checkbox || item.type == MenuItemType::Radio) {
            width += style.iconSize + style.iconSpacing;
        }

        return width;
    }

    void UltraCanvasMenu::PositionSubmenu(std::shared_ptr<UltraCanvasMenu> submenu, int itemIndex) {
        if (!submenu) return;

        int submenuX, submenuY;

        if (orientation == MenuOrientation::Vertical) {
            // Position to the right of the item
            submenuX = GetXInWindow() + GetWidth() + style.submenuOffset;
            submenuY = GetYInWindow() + GetItemY(itemIndex) - style.paddingTop;
        } else {
            // Position below the item
            submenuX = GetXInWindow() + GetItemX(itemIndex);
            submenuY = GetYInWindow() + GetHeight() + style.submenuOffset;
        }

        // Adjust for screen boundaries (simplified)
        // In a complete implementation, you'd check screen bounds

        submenu->SetPosition(submenuX, submenuY);
    }

    void UltraCanvasMenu::RenderSeparator(const Rect2Di &bounds, IRenderContext* ctx) {
        int centerY = bounds.y + bounds.height / 2;
        int startX = bounds.x + style.paddingLeft;
        int endX = bounds.x + bounds.width - style.paddingRight;

        ctx->SetStrokeColor(style.separatorColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(Point2Di(startX, centerY), Point2Di(endX, centerY));
    }

    void UltraCanvasMenu::RenderCheckbox(const MenuItemData &item, const Point2Di &position, IRenderContext* ctx) {
        Rect2Di checkRect(position.x, position.y, style.iconSize, style.iconSize);

        ctx->SetStrokeColor(style.borderColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(checkRect);

        if (item.checked) {
            ctx->SetStrokeColor(style.textColor);
            ctx->SetStrokeWidth(2.0f);

            if (item.type == MenuItemType::Checkbox) {
                // Draw checkmark
                Point2Di p1(position.x + 3, position.y + style.iconSize / 2);
                Point2Di p2(position.x + style.iconSize / 2, position.y + style.iconSize - 3);
                Point2Di p3(position.x + style.iconSize - 3, position.y + 3);
                ctx->DrawLine(p1, p2);
                ctx->DrawLine(p2, p3);
            } else {
                // Draw radio dot
                int centerX = position.x + style.iconSize / 2;
                int centerY = position.y + style.iconSize / 2;
                ctx->DrawCircle(Point2Di(centerX, centerY), style.iconSize / 4);
            }
        }
    }

    void UltraCanvasMenu::RenderSubmenuArrow(const Point2Di &position, IRenderContext* ctx) {
        ctx->SetStrokeColor(style.textColor);
        ctx->SetStrokeWidth(1.5f);

        if (orientation == MenuOrientation::Vertical) {
            // Right arrow
            Point2Di p1(position.x - 3, position.y - 4);
            Point2Di p2(position.x + 3, position.y);
            Point2Di p3(position.x - 3, position.y + 4);
            ctx->DrawLine(p1, p2);
            ctx->DrawLine(p2, p3);
        } else {
            // Down arrow
            Point2Di p1(position.x - 4, position.y - 3);
            Point2Di p2(position.x, position.y + 3);
            Point2Di p3(position.x + 4, position.y - 3);
            ctx->DrawLine(p1, p2);
            ctx->DrawLine(p2, p3);
        }
    }

    void UltraCanvasMenu::RenderIcon(const std::string &iconPath, const Point2Di &position, IRenderContext* ctx) {
        // Would implement icon rendering based on file type
        ctx->DrawImage(iconPath, position.x, position.y, style.iconSize, style.iconSize);
    }

    void UltraCanvasMenu::RenderKeyboardHighlight(const Rect2Di &bounds, IRenderContext* ctx) {
        ctx->SetStrokeColor(style.selectedColor);
        ctx->SetStrokeWidth(2.0f);
        ctx->DrawRectangle(bounds);
    }

    void UltraCanvasMenu::RenderShadow(IRenderContext *ctx) {
        Rect2Di bounds = GetBounds();
        ctx->SetFillColor(style.shadowColor);
        ctx->DrawRectangle(bounds.x + style.shadowOffset.x, bounds.y + style.shadowOffset.y, bounds.width, bounds.height);
    }

    int UltraCanvasMenu::GetItemAtPosition(int x, int y) const {
        // Check if position is within menu bounds
        if (x < GetX() || x > GetX() + GetWidth() ||
            y < GetY() || y > GetY() + GetHeight()) {
            return -1;
        }

        // For horizontal menus, iterate through items by X position
        if (orientation == MenuOrientation::Horizontal) {
            int currentX = GetX();

            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                if (!items[i].visible) continue;

                int itemWidth = CalculateItemWidth(items[i]) + style.paddingLeft + style.paddingRight;
                if (items[i].type != MenuItemType::Separator) {
                    itemWidth += style.iconSpacing;
                }

                if (x >= currentX && x < currentX + itemWidth) {
                    return i;
                }

                currentX += itemWidth;
            }
        } else {
            // For vertical menus, iterate through items by Y position
            int currentY = GetY();

            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                if (!items[i].visible) continue;

                int itemHeight = items[i].type == MenuItemType::Separator ?
                                   style.separatorHeight : style.itemHeight;

                if (y >= currentY && y < currentY + itemHeight) {
                    return i;
                }

                currentY += itemHeight;
            }
        }

        return -1;
    }

    bool UltraCanvasMenu::HandleMouseMove(const UCEvent &event) {
        int newHoveredIndex = GetItemAtPosition(event.x, event.y);

        if (newHoveredIndex != hoveredIndex) {
            hoveredIndex = newHoveredIndex;
            keyboardNavigation = false;

            if (onItemHovered && hoveredIndex >= 0) {
                onItemHovered(hoveredIndex);
            }
            RequestRedraw();

            // Auto-open submenu on hover (with delay)
            if (hoveredIndex >= 0 && hoveredIndex < static_cast<int>(items.size())) {
                const MenuItemData& item = items[hoveredIndex];
                if (!item.subItems.empty()) {
                    // In a complete implementation, you'd add a timer for submenu delay
                    OpenSubmenu(hoveredIndex);
                }
//            } else {
//                CloseActiveSubmenu();
            }
        }
        return (newHoveredIndex >= 0);
    }

    bool UltraCanvasMenu::HandleMouseDown(const UCEvent &event) {
        if (!Contains(event.x, event.y) && menuType != MenuType::Menubar) {
            // Click outside menu - close if context menu
            bool clickOutside = true;
            if (activeSubmenu) {
                for(auto sm = activeSubmenu; sm != nullptr; sm = sm->activeSubmenu) {
                    if (sm->Contains(event.x, event.y)) {
                        clickOutside = false;
                        break;
                    }
                }
//                if (clickOutside) {
//                    CloseActiveSubmenu();
//                }
            }
            if (clickOutside) {
                auto pm = parentMenu.lock();
                if (!pm || pm->menuType == MenuType::Menubar) {
                    Hide();
                    return true;
                }
            }
            return false;
        }

        int clickedIndex = GetItemAtPosition(event.x, event.y);
        if (clickedIndex >= 0 && clickedIndex < static_cast<int>(items.size())) {
            selectedIndex = clickedIndex;
            RequestRedraw();
        }
        return true;
    }

    bool UltraCanvasMenu::HandleMouseUp(const UCEvent &event) {
        if (!Contains(event.x, event.y)) return false;

        int clickedIndex = GetItemAtPosition(event.x, event.y);

        if (clickedIndex >= 0 && clickedIndex < static_cast<int>(items.size()) && clickedIndex == selectedIndex) {
            ExecuteItem(clickedIndex);
        }

        selectedIndex = -1;
        RequestRedraw();
        return true;
    }

    bool UltraCanvasMenu::HandleKeyDown(const UCEvent &event) {
        keyboardNavigation = true;

        switch (event.virtualKey) {
            case UCKeys::Up:
                NavigateUp();
                return true;

            case UCKeys::Down:
                NavigateDown();
                return true;

            case UCKeys::Left:
                if (orientation == MenuOrientation::Horizontal) {
                    NavigateLeft();
                } else {
                    CloseSubmenu();
                }
                return true;

            case UCKeys::Right:
                if (orientation == MenuOrientation::Horizontal) {
                    NavigateRight();
                } else {
                    OpenSubmenuFromKeyboard();
                }
                return true;

            case UCKeys::Return:
            case UCKeys::Space:
                if (keyboardIndex >= 0) {
                    ExecuteItem(keyboardIndex);
                }
                return true;

            case UCKeys::Escape:
                Hide();
                return true;

            default:
                break;
        }

        return false;
    }

    void UltraCanvasMenu::NavigateUp() {
        if (items.empty()) return;

        do {
            keyboardIndex = (keyboardIndex <= 0) ? static_cast<int>(items.size()) - 1 : keyboardIndex - 1;
        } while (keyboardIndex >= 0 &&
                 (!items[keyboardIndex].visible ||
                  items[keyboardIndex].type == MenuItemType::Separator ||
                  !items[keyboardIndex].enabled));
    }

    void UltraCanvasMenu::NavigateDown() {
        if (items.empty()) return;

        do {
            keyboardIndex = (keyboardIndex >= static_cast<int>(items.size()) - 1) ? 0 : keyboardIndex + 1;
        } while (keyboardIndex < static_cast<int>(items.size()) &&
                 (!items[keyboardIndex].visible ||
                  items[keyboardIndex].type == MenuItemType::Separator ||
                  !items[keyboardIndex].enabled));
    }

    void UltraCanvasMenu::OpenSubmenuFromKeyboard() {
        if (keyboardIndex >= 0 && keyboardIndex < static_cast<int>(items.size())) {
            const MenuItemData& item = items[keyboardIndex];
            if (!item.subItems.empty()) {
                OpenSubmenu(keyboardIndex);
                if (activeSubmenu) {
                    activeSubmenu->keyboardNavigation = true;
                    activeSubmenu->keyboardIndex = 0;
                }
            }
        }
    }

    void UltraCanvasMenu::CloseSubmenu() {
        CloseActiveSubmenu();
        if (auto parent = parentMenu.lock()) {
            parent->keyboardNavigation = true;
        }
    }

    void UltraCanvasMenu::ExecuteItem(int index) {
        if (index < 0 || index >= static_cast<int>(items.size())) return;

        MenuItemData& item = items[index];
        if (!item.enabled) return;

        // Handle different item types
        switch (item.type) {
            case MenuItemType::Action: {
                if (item.onClick) {
                    item.onClick();
                }
                UCEvent ev;
                ev.type = UCEventType::MenuClick;
                ev.targetElement = this;
                ev.userDataPtr = &item;
                UltraCanvasApplication::GetInstance()->PushEvent(ev);
                break;
            }

            case MenuItemType::Checkbox: {
                item.checked = !item.checked;
                if (item.onToggle) {
                    item.onToggle(item.checked);
                }
                UCEvent ev;
                ev.type = UCEventType::MenuClick;
                ev.targetElement = this;
                ev.userDataPtr = &item;
                UltraCanvasApplication::GetInstance()->PushEvent(ev);
                break;
            }

            case MenuItemType::Radio: {
                // Uncheck other radio items in the same group
                for (auto &otherItem: items) {
                    if (otherItem.type == MenuItemType::Radio &&
                        otherItem.radioGroup == item.radioGroup) {
                        otherItem.checked = false;
                    }
                }
                item.checked = true;
                if (item.onClick) {
                    item.onClick();
                }
                UCEvent ev;
                ev.type = UCEventType::MenuClick;
                ev.targetElement = this;
                ev.userDataPtr = &item;
                UltraCanvasApplication::GetInstance()->PushEvent(ev);
                break;
            }

            case MenuItemType::Submenu:
                OpenSubmenu(index);
                break;

            default:
                break;
        }

        if (onItemSelected) {
            onItemSelected(index);
        }

        if (item.type == MenuItemType::Action && menuType != MenuType::Menubar) {
            CloseMenutree();
        }
    }

    Color UltraCanvasMenu::GetItemBackgroundColor(int index, const MenuItemData &item) const {
        if (!item.enabled) return Colors::Transparent;

        if (index == hoveredIndex || index == keyboardIndex) {
            return style.hoverColor;
        }

        if (index == selectedIndex) {
            return style.pressedColor;
        }

        return Colors::Transparent;
    }
}

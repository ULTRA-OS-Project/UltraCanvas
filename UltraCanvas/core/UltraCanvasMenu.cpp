// UltraCanvasMenu.cpp
// Interactive menu component with styling options and submenu support
// Version: 1.2.5
// Last Modified: 2025-01-08
// Author: UltraCanvas Framework

#include <vector>
#include "UltraCanvasMenu.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasZOrderManager.h"
#include "UltraCanvasWindow.h"

namespace UltraCanvas {

    void UltraCanvasMenu::Show() {
        // Ensure menu is always on top when shown
        SetZIndex(UltraCanvas::ZLayers::Menus + 100);

        // Existing Show() implementation continues...
        if (currentState != MenuState::Visible && currentState != MenuState::Opening) {
            if (menuType == MenuType::DropdownMenu || menuType == MenuType::SubmenuMenu) {
                currentState = MenuState::Visible;
                SetVisible(true);
                //UltraCanvasMenuManager::RegisterActiveMenu(shared_from_this());
            } else {
                currentState = style.enableAnimations ? MenuState::Opening : MenuState::Visible;
                SetVisible(true);
                if (style.enableAnimations) {
                    StartAnimation();
                }
            }

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
            if (menuType == MenuType::DropdownMenu || menuType == MenuType::SubmenuMenu) {
                currentState = MenuState::Hidden;
                SetVisible(false);
            } else {
                currentState = style.enableAnimations ? MenuState::Closing : MenuState::Hidden;

                if (style.enableAnimations) {
                    StartAnimation();
                } else {
                    SetVisible(false);
                }
            }

            //UltraCanvasMenuManager::UnregisterActiveMenu(shared_from_this());

            // Close all submenus
            CloseAllSubmenus();

            needCalculateSize = true;
            if (onMenuClosed) onMenuClosed();
            RequestRedraw();

            std::cout << "Menu '" << GetIdentifier() << "' hidden. State: " << (int)currentState
                      << " Visible: " << IsVisible() << std::endl;
        }
    }

    void UltraCanvasMenu::Render() {
        // FIX: Simplified visibility check - if not visible at all, don't render
        if (!IsVisible()) return;

        // FIX: For dropdown/submenu menus, check if they should be rendered
        if (menuType == MenuType::DropdownMenu || menuType == MenuType::SubmenuMenu) {
            // Only skip rendering if explicitly hidden
            if (currentState == MenuState::Hidden) {
                return;
            }
        }

        if (needCalculateSize) {
            CalculateSize();
        }

        // Apply animation if active
        if (style.enableAnimations && (currentState == MenuState::Opening || currentState == MenuState::Closing)) {
            UpdateAnimation();
        }

        // Get render context
        auto context = GetRenderContext();
        if (!context) return;

        // Render shadow first (for dropdown menus)
        if (style.showShadow && (menuType == MenuType::DropdownMenu || menuType == MenuType::SubmenuMenu)) {
            RenderShadow();
        }

        // Render background
        Rect2D bounds = GetBounds();
        context->SetFillColor(style.backgroundColor);
        context->FillRectangle(bounds);

        // Draw border
        if (style.borderWidth > 0) {
            context->SetStrokeColor(style.borderColor);
            context->SetStrokeWidth(style.borderWidth);
            context->DrawRectangle(bounds);
        }

        // Render items
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (items[i].visible) {
                RenderItem(i, items[i]);
            }
        }

        // Render keyboard navigation highlight
        if (keyboardNavigation && keyboardIndex >= 0 && keyboardIndex < static_cast<int>(items.size())) {
            RenderKeyboardHighlight(GetItemBounds(keyboardIndex));
        }
    }

    bool UltraCanvasMenu::OnEvent(const UCEvent &event) {
        // Debug output for dropdown menus
        if ((menuType == MenuType::DropdownMenu || menuType == MenuType::SubmenuMenu) &&
            (event.type == UCEventType::MouseDown || event.type == UCEventType::MouseUp)) {
            std::cout << "Dropdown Menu '" << GetIdentifier() << "' received event type: " << (int)event.type
                      << " at (" << event.x << "," << event.y << ")"
                      << " Menu bounds: (" << GetX() << "," << GetY()
                      << "," << GetWidth() << "," << GetHeight() << ")"
                      << " Visible: " << IsVisible()
                      << " State: " << (int)currentState << std::endl;
        }

        // Handle the event
        bool handled = HandleEvent(event);

        // If not handled, pass to base class
        if (!handled) {
            return UltraCanvasElement::OnEvent(event);
        }
        return true;
    }

    bool UltraCanvasMenu::HandleEvent(const UCEvent &event) {
        // FIX: Check menu state for dropdown menus
        if (menuType == MenuType::DropdownMenu || menuType == MenuType::SubmenuMenu) {
            if (currentState == MenuState::Hidden) {
                return false;
            }
        }

        if (!IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseMove:
                HandleMouseMove(event);
                break;

            case UCEventType::MouseDown:
                HandleMouseDown(event);
                break;

            case UCEventType::MouseUp:
                HandleMouseUp(event);
                break;

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
            case MenuType::MainMenu:
                orientation = MenuOrientation::Horizontal;
                style.itemHeight = 32.0f;
                break;
            case MenuType::ContextMenu:
            case MenuType::PopupMenu:
                orientation = MenuOrientation::Vertical;
                style.showShadow = true;
                break;
            case MenuType::DropdownMenu:
            case MenuType::SubmenuMenu:
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
        if (parentWindow) {
            activeSubmenu->SetWindow(parentWindow);
        }

        // Add items to submenu
        for (const auto& subItem : item.subItems) {
            activeSubmenu->AddItem(subItem);
        }

        // Position submenu
        PositionSubmenu(activeSubmenu, itemIndex);

        if (parentWindow) {
            // Add the submenu to the window's element collection so it gets rendered
            parentWindow->AddChild(activeSubmenu);
            std::cout << "Added submenu '" << activeSubmenu->GetIdentifier()
                      << "' to window for rendering" << std::endl;
        }

        // Show submenu
        activeSubmenu->Show();
        childMenus.push_back(activeSubmenu);
    }

    void UltraCanvasMenu::CloseActiveSubmenu() {
        if (activeSubmenu) {
            activeSubmenu->Hide();
            activeSubmenu->CloseAllSubmenus();

            auto parentWindow = GetWindow();
            if (parentWindow) {
                parentWindow->RemoveChild(activeSubmenu);
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
                }
            }
        }
        childMenus.clear();
        activeSubmenu.reset();
    }

    float UltraCanvasMenu::GetItemX(int index) const {
        if (orientation == MenuOrientation::Vertical) {
            return 0.0f;
        }

        // FIXED: Calculate proper X position for horizontal items
        float x = style.paddingLeft;

        for (int i = 0; i < index && i < static_cast<int>(items.size()); ++i) {
            if (!items[i].visible) continue;
            x += CalculateItemWidth(items[i]) + style.paddingLeft + style.paddingRight;
            if (items[i].type != MenuItemType::Separator) {
                x += style.iconSpacing; // Add spacing between items
            }
        }

        return x;
    }

    float UltraCanvasMenu::GetItemY(int index) const {
        float y = style.paddingTop;

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

    bool UltraCanvasMenu::Contains(float x, float y) const {
        // For main menu, always check bounds
        if (menuType == MenuType::MainMenu) {
            return x >= GetX() && x < GetX() + GetWidth() &&
                   y >= GetY() && y < GetY() + GetHeight();
        }

        // FIX: For dropdown menus, check visibility and state
        if (menuType == MenuType::DropdownMenu || menuType == MenuType::SubmenuMenu) {
            // Only check bounds if menu is actually visible
            if (!IsVisible() || currentState == MenuState::Hidden) {
                return false;
            }
        }

        return x >= GetX() && x < GetX() + GetWidth() &&
               y >= GetY() && y < GetY() + GetHeight();
    }

    void UltraCanvasMenu::CalculateSize() {
        needCalculateSize = false;
        if (items.empty()) {
            SetWidth(100);
            SetHeight(static_cast<long>(style.itemHeight));
            return;
        }

        if (orientation == MenuOrientation::Horizontal) {
            // FIXED: Horizontal layout calculation
            float totalWidth = 0;
            float maxHeight = style.itemHeight;

            for (const auto& item : items) {
                if (!item.visible) continue;
                totalWidth += CalculateItemWidth(item) + style.paddingLeft + style.paddingRight;
//                if (item.type != MenuItemType::Separator) {
//                    totalWidth += style.iconSpacing; // Add spacing between items
//                }
            }

            // Set dimensions for horizontal menu
            SetWidth(static_cast<long>(totalWidth));
            SetHeight(static_cast<long>(maxHeight));

        } else {
            // Vertical layout calculation
            float maxWidth = 0.0f;
            float totalHeight = 0.0f;

            for (const auto& item : items) {
                if (!item.visible) continue;

                float itemWidth = CalculateItemWidth(item);
                maxWidth = std::max(maxWidth, itemWidth);

                if (item.type == MenuItemType::Separator) {
                    totalHeight += style.separatorHeight;
                } else {
                    totalHeight += style.itemHeight;
                }
            }

            SetWidth(static_cast<long>(maxWidth + style.paddingLeft + style.paddingRight));
            SetHeight(static_cast<long>(totalHeight));
        }
    }

    void UltraCanvasMenu::RenderItem(int index, const MenuItemData &item) {
        if (!item.visible) return;

        Rect2D itemBounds = GetItemBounds(index);
        auto context = GetRenderContext();
        if (!context) return;

        // Draw item background
        Color bgColor = GetItemBackgroundColor(index, item);
        if (bgColor.a > 0) {
            context->SetFillColor(bgColor);
            context->FillRectangle(itemBounds);
        }

        // Handle separator
        if (item.type == MenuItemType::Separator) {
            RenderSeparator(itemBounds);
            return;
        }

        Point2D textSize = GetRenderContext()->MeasureText(item.label);
        float fontHeight = textSize.y;
        float currentX = itemBounds.x + style.paddingLeft;
        float textY = itemBounds.y + (itemBounds.height - fontHeight) / 2;

        // Render checkbox/radio
        if (item.type == MenuItemType::Checkbox || item.type == MenuItemType::Radio) {
            RenderCheckbox(item, Point2D(currentX, textY));
            currentX += style.iconSize + style.iconSpacing;
        }

        // Render icon
        if (!item.iconPath.empty()) {
            float iconY = itemBounds.y + (itemBounds.height - style.iconSize) / 2;
            RenderIcon(item.iconPath, Point2D(currentX, iconY));
            currentX += style.iconSize + style.iconSpacing;
        }

        // Render text
        if (!item.label.empty()) {
            Color textColor = item.enabled ?
                              (index == hoveredIndex ? style.hoverTextColor : style.textColor) :
                              style.disabledTextColor;

            context->SetTextColor(textColor);
            context->DrawText(item.label, Point2D(currentX, textY));
        }

        // Render shortcut (for vertical menus)
        if (!item.shortcut.empty() && orientation == MenuOrientation::Vertical) {
            float shortcutX = itemBounds.x + itemBounds.width - style.paddingRight -
                              context->GetTextWidth(item.shortcut.c_str());
            context->SetTextColor(style.shortcutColor);
            context->DrawText(item.shortcut, Point2D(shortcutX, textY));
        }

        // Render submenu arrow (for vertical menus)
        if (!item.subItems.empty() && orientation == MenuOrientation::Vertical) {
            RenderSubmenuArrow(Point2D(itemBounds.x + itemBounds.width - style.paddingRight - 2,
                                       itemBounds.y + itemBounds.height / 2));
        }
    }

    Rect2D UltraCanvasMenu::GetItemBounds(int index) const {
        Rect2D bounds;

        if (orientation == MenuOrientation::Horizontal) {
            float currentX = GetX();

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
            float currentY = GetY();

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

    float UltraCanvasMenu::CalculateItemWidth(const MenuItemData &item) const {
        float width = 0.0f;

        // Icon space
        if (!item.iconPath.empty()) {
            width += style.iconSize + style.iconSpacing;
        }

        // Text width - FIX: Convert std::string to const char*
        if (!item.label.empty()) {
            width += GetTextWidth(item.label.c_str());
        }

        // Shortcut width - FIX: Convert std::string to const char*
        if (!item.shortcut.empty()) {
            width += style.shortcutSpacing + GetTextWidth(item.shortcut.c_str());
        }

        // Submenu arrow
        if (!item.subItems.empty()) {
            width += 20.0f;  // Arrow space
        }

        // Checkbox/radio space
        if (item.type == MenuItemType::Checkbox || item.type == MenuItemType::Radio) {
            width += style.iconSize + style.iconSpacing;
        }

        return width;
    }

    void UltraCanvasMenu::PositionSubmenu(std::shared_ptr<UltraCanvasMenu> submenu, int itemIndex) {
        if (!submenu) return;

        float itemY = GetItemY(itemIndex);
        float submenuX, submenuY;

        if (orientation == MenuOrientation::Vertical) {
            // Position to the right of the item
            submenuX = GetX() + GetWidth() + style.submenuOffset;
            submenuY = GetY() + itemY - style.paddingTop;
        } else {
            // Position below the item
            submenuX = GetX() + GetItemX(itemIndex);
            submenuY = GetY() + GetHeight() + style.submenuOffset;
        }

        // Adjust for screen boundaries (simplified)
        // In a complete implementation, you'd check screen bounds

        submenu->SetPosition(static_cast<long>(submenuX), static_cast<long>(submenuY));
    }

    void UltraCanvasMenu::RenderSeparator(const Rect2D &bounds) {
        float centerY = bounds.y + bounds.height / 2;
        float startX = bounds.x + style.paddingLeft;
        float endX = bounds.x + bounds.width - style.paddingRight;

        SetStrokeColor(style.separatorColor);
        SetStrokeWidth(1.0f);
        DrawLine(Point2D(startX, centerY), Point2D(endX, centerY));
    }

    void UltraCanvasMenu::RenderCheckbox(const MenuItemData &item, const Point2D &position) {
        Rect2D checkRect(position.x, position.y, style.iconSize, style.iconSize);

        SetStrokeColor(style.borderColor);
        SetStrokeWidth(1.0f);
        DrawRect(checkRect);

        if (item.checked) {
            SetStrokeColor(style.textColor);
            SetStrokeWidth(2.0f);

            if (item.type == MenuItemType::Checkbox) {
                // Draw checkmark
                Point2D p1(position.x + 3, position.y + style.iconSize / 2);
                Point2D p2(position.x + style.iconSize / 2, position.y + style.iconSize - 3);
                Point2D p3(position.x + style.iconSize - 3, position.y + 3);
                DrawLine(p1, p2);
                DrawLine(p2, p3);
            } else {
                // Draw radio dot
                float centerX = position.x + style.iconSize / 2;
                float centerY = position.y + style.iconSize / 2;
                DrawCircle(Point2D(centerX, centerY), style.iconSize / 4);
            }
        }
    }

    void UltraCanvasMenu::RenderSubmenuArrow(const Point2D &position) {
        SetStrokeColor(style.textColor);
        SetStrokeWidth(1.5f);

        if (orientation == MenuOrientation::Vertical) {
            // Right arrow
            Point2D p1(position.x - 3, position.y - 4);
            Point2D p2(position.x + 3, position.y);
            Point2D p3(position.x - 3, position.y + 4);
            DrawLine(p1, p2);
            DrawLine(p2, p3);
        } else {
            // Down arrow
            Point2D p1(position.x - 4, position.y - 3);
            Point2D p2(position.x, position.y + 3);
            Point2D p3(position.x + 4, position.y - 3);
            DrawLine(p1, p2);
            DrawLine(p2, p3);
        }
    }

    void UltraCanvasMenu::RenderIcon(const std::string &iconPath, const Point2D &position) {
        // Would implement icon rendering based on file type
        DrawImage(iconPath, Rect2D(position.x, position.y, style.iconSize, style.iconSize));
    }

    void UltraCanvasMenu::RenderKeyboardHighlight(const Rect2D &bounds) {
        SetStrokeColor(style.selectedColor);
        SetStrokeWidth(2.0f);
        DrawRect(bounds);
    }

    void UltraCanvasMenu::RenderShadow() {
        Rect2D bounds = GetBounds();
        Rect2D shadowBounds(
                bounds.x + style.shadowOffset.x,
                bounds.y + style.shadowOffset.y,
                bounds.width,
                bounds.height
        );

        SetFillColor(style.shadowColor);
        DrawRect(shadowBounds);
    }

    int UltraCanvasMenu::GetItemAtPosition(const Point2D &position) const {
        // Check if position is within menu bounds
        if (position.x < GetX() || position.x > GetX() + GetWidth() ||
            position.y < GetY() || position.y > GetY() + GetHeight()) {
            return -1;
        }

        // For horizontal menus, iterate through items by X position
        if (orientation == MenuOrientation::Horizontal) {
            float currentX = GetX();

            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                if (!items[i].visible) continue;

                float itemWidth = CalculateItemWidth(items[i]) + style.paddingLeft + style.paddingRight;
                if (items[i].type != MenuItemType::Separator) {
                    itemWidth += style.iconSpacing;
                }

                if (position.x >= currentX && position.x < currentX + itemWidth) {
                    return i;
                }

                currentX += itemWidth;
            }
        } else {
            // For vertical menus, iterate through items by Y position
            float currentY = GetY();

            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                if (!items[i].visible) continue;

                float itemHeight = items[i].type == MenuItemType::Separator ?
                                   style.separatorHeight : style.itemHeight;

                if (position.y >= currentY && position.y < currentY + itemHeight) {
                    return i;
                }

                currentY += itemHeight;
            }
        }

        return -1;
    }

    void UltraCanvasMenu::HandleMouseMove(const UCEvent &event) {
        Point2D mousePos(static_cast<float>(event.x), static_cast<float>(event.y));
        int newHoveredIndex = GetItemAtPosition(mousePos);

        if (newHoveredIndex != hoveredIndex) {
            hoveredIndex = newHoveredIndex;
            keyboardNavigation = false;

            if (onItemHovered && hoveredIndex >= 0) {
                onItemHovered(hoveredIndex);
            }

            // Auto-open submenu on hover (with delay)
            if (hoveredIndex >= 0 && hoveredIndex < static_cast<int>(items.size())) {
                const MenuItemData& item = items[hoveredIndex];
                if (!item.subItems.empty()) {
                    // In a complete implementation, you'd add a timer for submenu delay
                    OpenSubmenu(hoveredIndex);
                }
            } else {
                CloseActiveSubmenu();
            }
        }
    }

    void UltraCanvasMenu::HandleMouseDown(const UCEvent &event) {
        if (!Contains(event.x, event.y)) {
            // Click outside menu - close if context menu
            if (menuType == MenuType::ContextMenu || menuType == MenuType::PopupMenu) {
                Hide();
            } else if (activeSubmenu) {
                bool clickOutside = true;
                for(auto sm = activeSubmenu; sm != nullptr; sm = sm->activeSubmenu) {
                    if (sm->Contains(event.x, event.y)) {
                        clickOutside = false;
                        break;
                    }
                }
                if (clickOutside) {
                    CloseActiveSubmenu();
                }
            }
            return;
        }

        Point2D mousePos(static_cast<float>(event.x), static_cast<float>(event.y));
        int clickedIndex = GetItemAtPosition(mousePos);

        if (clickedIndex >= 0 && clickedIndex < static_cast<int>(items.size())) {
            selectedIndex = clickedIndex;
            RequestRedraw();
        }
    }

    void UltraCanvasMenu::HandleMouseUp(const UCEvent &event) {
        if (!Contains(event.x, event.y)) return;

        Point2D mousePos(static_cast<float>(event.x), static_cast<float>(event.y));
        int clickedIndex = GetItemAtPosition(mousePos);

        if (clickedIndex >= 0 && clickedIndex < static_cast<int>(items.size()) && clickedIndex == selectedIndex) {
            ExecuteItem(clickedIndex);
        }

        selectedIndex = -1;
        RequestRedraw();
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
            case MenuItemType::Action:
                if (item.onClick) {
                    item.onClick();
                }
                // FIX: Don't auto-hide dropdown menus on item click
                // Let the application decide when to hide them
                break;

            case MenuItemType::Checkbox:
                item.checked = !item.checked;
                if (item.onToggle) {
                    item.onToggle(item.checked);
                }
                break;

            case MenuItemType::Radio:
                // Uncheck other radio items in the same group
                for (auto& otherItem : items) {
                    if (otherItem.type == MenuItemType::Radio &&
                        otherItem.radioGroup == item.radioGroup) {
                        otherItem.checked = false;
                    }
                }
                item.checked = true;
                if (item.onClick) {
                    item.onClick();
                }
                break;

            case MenuItemType::Submenu:
                OpenSubmenu(index);
                break;

            default:
                break;
        }

        if (onItemSelected) {
            onItemSelected(index);
        }
    }
}

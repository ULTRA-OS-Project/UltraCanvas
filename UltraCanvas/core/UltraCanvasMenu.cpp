// UltraCanvasMenu.cpp
// Interactive menu component with styling options and submenu support
// Version: 1.2.6
// Last Modified: 2026-02-05
// Author: UltraCanvas Framework

#include <vector>
#include "UltraCanvasMenu.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasApplication.h"
//#include "UltraCanvasZOrderManager.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

    void UltraCanvasMenu::OpenMenu(const Point2Di &pos, UltraCanvasWindowBase &win,
                                   const PopupElementSettings &popupSettings) {
        if (!IsVisible() || !isPopup) {
            menuPopupSettings = popupSettings;

            win.OpenPopup(pos, *this, menuPopupSettings);

            if (style.enableAnimations) {
                StartAnimation();
            }

            activeIndex = -1;
            needCalculateSize = true;
            scrollOffsetPixels = 0;
            needsScrollbar = false;

            if (onMenuOpened) onMenuOpened();

            // Force immediate redraw with z-order update
            RequestRedraw();

            debugOutput << "Menu '" << GetIdentifier()
                        << "' shown with Z=" << GetZOrder() << std::endl;
        }
    }

    void UltraCanvasMenu::CloseMenu() {
        window->ClosePopup(*this, ClosePopupReason::Manual);
        debugOutput << "Menu '" << GetIdentifier() << "' hidden.  Visible: " << IsVisible() << std::endl;
    }

    void UltraCanvasMenu::UpdateGeometry(IRenderContext* ctx) {
        if (needCalculateSize) {
            ctx->PushState();
            CalculateAndUpdateSize(ctx);
            ctx->PopState();
        }
        UltraCanvasUIElement::UpdateGeometry(ctx);
    }

    void UltraCanvasMenu::Render(IRenderContext *ctx) {
        // FIX: Simplified visibility check - if not visible at all, don't render
        if (menuType == MenuType::Menubar) {

            // Render background
            Rect2Di bounds = GetBounds();
            ctx->DrawFilledRectangle(bounds, style.backgroundColor, style.borderWidth, style.borderColor);

            // Draw border
            // Render items
            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                if (items[i].visible) {
                    RenderItem(i, items[i], ctx);
                }
            }

        } else { // submenu or popup
//            if (style.enableAnimations &&
//                (currentState == MenuState::Opening || currentState == MenuState::Closing)) {
//                UpdateAnimation();
//            }

            // Shadow draws intentionally outside bounds — must be before clip is set
            if (style.showShadow &&
                (menuType == MenuType::PopupMenu || menuType == MenuType::SubmenuMenu)) {
                RenderShadow(ctx);
            }

            Rect2Di bounds = GetBounds();

            // FIX 1: Ensure background is painted with fully opaque solid color
            // so no underlying window content bleeds through the menu body or border.
            ctx->DrawFilledRectangle(bounds, style.backgroundColor,
                                     style.borderWidth, style.borderColor);

            // FIX 2: Clip all item rendering strictly to the inner popup bounds.
            // This prevents item text (especially the title/first row) from
            // rendering outside the menu rectangle and showing gray fringe
            // artifacts or overlapping the background window content.
            // We inset by borderWidth on all sides so the border stroke itself
            // is not clipped away.
            ctx->PushState();
            int bw = static_cast<int>(style.borderWidth);
            int sbWidth = needsScrollbar ? static_cast<int>(style.scrollbarStyle.trackSize) : 0;
            ctx->ClipRect(Rect2Df(bounds.x + bw,
                                  bounds.y + bw,
                                  bounds.width - bw * 2 - sbWidth,
                                  bounds.height - bw * 2));

            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                if (items[i].visible) {
                    RenderItem(i, items[i], ctx);
                }
            }


            ctx->PopState();  // releases the clip region

            // Render scrollbar for overflow menus
            if (needsScrollbar && menuScrollbar) {
                int scrollbarWidth = static_cast<int>(style.scrollbarStyle.trackSize);
                menuScrollbar->SetBounds(Rect2Di(
                        bounds.x + bounds.width - scrollbarWidth - bw,
                        bounds.y + bw,
                        scrollbarWidth,
                        bounds.height - bw * 2));
                menuScrollbar->SetScrollPosition(scrollOffsetPixels);
                menuScrollbar->Render(ctx);
            }
        }
    }

    // Handle the event
    bool UltraCanvasMenu::OnEvent(const UCEvent &event) {
        if (!IsVisible()) return false;

//        if (menuType == MenuType::PopupMenu || menuType == MenuType::SubmenuMenu) {
//            if (currentState == MenuState::Hidden) {
//                return false;
//            }
//        }

        switch (event.type) {
            case UCEventType::MouseMove:
                return HandleMouseMove(event);

            case UCEventType::MouseDown:
                return HandleMouseDown(event);

            case UCEventType::MouseUp:
                return HandleMouseUp(event);

            case UCEventType::KeyDown:
                return HandleKeyDown(event);

            case UCEventType::MouseWheel:
                return HandleMouseWheel(event);

            case UCEventType::MouseLeave:
                activeIndex = -1;
                break;

            default:
                break;
        }

        if (UltraCanvasUIElement::OnEvent(event)) {
            return true;
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
                style.paddingLeft = 10.0f;
                style.paddingRight = 10.0f;
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
        auto parentWindow = GetWindow();
        if (!parentWindow || itemIndex < 0 || itemIndex >= static_cast<int>(items.size())) return;
        if (activeSubmenu && activeSubmenu->parentItemIndex == itemIndex) return;

        const MenuItemData &item = items[itemIndex];
        if (item.subItems.empty()) return;

        // Close existing submenu
        CloseActiveSubmenu();

        // Create and show new submenu
        activeSubmenu = std::make_shared<UltraCanvasMenu>(
                GetIdentifier() + "_submenu_" + std::to_string(itemIndex),
                0,
                0, 0, 150, 100
        );

        activeSubmenu->SetMenuType(MenuType::SubmenuMenu);
        activeSubmenu->SetStyle(style);
        activeSubmenu->parentMenu = std::static_pointer_cast<UltraCanvasMenu>(shared_from_this());
        activeSubmenu->parentItemIndex = itemIndex;

        // Add items to submenu
        for (const auto &subItem: item.subItems) {
            activeSubmenu->AddItem(subItem);
        }

        // Position submenu
        Point2Di pos = GetPositionSubmenu(*activeSubmenu, itemIndex);

        // Show submenu
        activeSubmenu->OpenMenu(pos, *parentWindow, menuPopupSettings);
        childMenus.push_back(activeSubmenu);
    }

    void UltraCanvasMenu::CloseActiveSubmenu() {
        if (activeSubmenu) {
//            activeSubmenu->CloseAllSubmenus();
            activeSubmenu->CloseMenu();

            // Remove from child menus
            auto it = std::find(childMenus.begin(), childMenus.end(), activeSubmenu);
            if (it != childMenus.end()) {
                childMenus.erase(it);
            }

            activeSubmenu.reset();
        }
    }

    void UltraCanvasMenu::CloseAllSubmenus() {
        for (auto &child: childMenus) {
            if (child) {
                child->CloseMenu();
//                child->CloseAllSubmenus();
            }
        }
        childMenus.clear();
        activeSubmenu.reset();
    }

    void UltraCanvasMenu::CloseMenutree() {
        auto parentMnu = parentMenu.lock();
        if (!parentMnu) {
            CloseMenu();
        } else if (parentMnu->menuType == MenuType::Menubar) {
            parentMnu->CloseActiveSubmenu();
        } else {
            auto mnu = parentMnu;
            while (parentMnu && parentMnu->menuType != MenuType::Menubar) {
                mnu = parentMnu;
                parentMnu = parentMnu->parentMenu.lock();
            }
            if (parentMnu) {
                parentMnu->CloseActiveSubmenu();
            } else {
                mnu->CloseMenu();
            }
        }
    }

    int UltraCanvasMenu::GetItemX(int index) const {
        if (orientation == MenuOrientation::Vertical) {
            return 0.0f;
        }

        int x = style.paddingLeft;

        for (int i = 0; i < index && i < static_cast<int>(items.size()); ++i) {
            if (!items[i].visible) continue;
            x += CalculateItemWidth(items[i]) + style.paddingRight + style.paddingLeft;
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
            if (!IsVisible()) {
                return false;
            }
        }

        if (GetBounds().Contains(x, y)) {
            return true;
        }

        // look bottom on tree
        auto mnu = activeSubmenu;
        while (mnu) {
            if (mnu->GetBounds().Contains(x, y)) return true;
            mnu = mnu->activeSubmenu;
        }

        // look top on tree
        mnu = parentMenu.lock();
        while (mnu) {
            if (mnu->GetBoundsInWindow().Contains(x, y)) return true;
            mnu = mnu->parentMenu.lock();
        }

        return false;
    }

    void UltraCanvasMenu::CalculateAndUpdateSize(IRenderContext *ctx) {
        ctx->SetFontStyle(style.font);

        needCalculateSize = false;
        if (items.empty()) {
            SetWidth(100);
            SetHeight(style.itemHeight);
            return;
        }

        if (orientation == MenuOrientation::Horizontal) {
            // Horizontal layout calculation (unchanged)
            int totalWidth = 0;
            int maxHeight = style.itemHeight;

            for (const auto &item: items) {
                if (!item.visible) continue;
                ctx->SetFontStyle(item.font.value_or(style.font));
                totalWidth += CalculateItemWidth(item) + style.paddingLeft + style.paddingRight;
            }
            if (GetWidth() <= 0) {
                SetWidth(totalWidth);
            }
            SetHeight(maxHeight);

        } else {
            // ============================================================
            // VERTICAL LAYOUT - Column-based width calculation
            // ============================================================
            // Menu item layout (left to right):
            // | paddingLeft | [checkbox] | [icon] | label | gap | [shortcut] | [arrow] | paddingRight |
            // ============================================================

            auto ctx = GetRenderContext();

            // Column width accumulators - find MAX for each column across ALL items
            bool hasAnyCheckboxOrRadio = false;
            bool hasAnyIcon = false;
            bool hasAnyShortcut = false;
            bool hasAnySubmenu = false;

            int maxLabelWidth = 0;
            int maxShortcutWidth = 0;
            int totalHeight = 0;

            // First pass: scan all items to determine column requirements
            for (const auto &item: items) {
                if (!item.visible) continue;

                // Apply per-item font for accurate measurement
                ctx->SetFontStyle(item.font.value_or(style.font));

                // Check for checkbox/radio column
                if (item.type == MenuItemType::Checkbox || item.type == MenuItemType::Radio) {
                    hasAnyCheckboxOrRadio = true;
                }

                // Check for icon column
                if (!item.iconPath.empty()) {
                    hasAnyIcon = true;
                }

                // Find maximum label width
                if (!item.label.empty()) {
                    int labelWidth = ctx->GetTextLineWidth(item.label.c_str());
                    maxLabelWidth = std::max(maxLabelWidth, labelWidth);
                }

                // Find maximum shortcut width
                if (!item.shortcut.empty()) {
                    hasAnyShortcut = true;
                    int shortcutWidth = ctx->GetTextLineWidth(item.shortcut.c_str());
                    maxShortcutWidth = std::max(maxShortcutWidth, shortcutWidth);
                }

                // Check for submenu arrow column
                if (!item.subItems.empty()) {
                    hasAnySubmenu = true;
                }

                // Calculate height
                if (item.type == MenuItemType::Separator) {
                    totalHeight += style.separatorHeight;
                } else {
                    totalHeight += style.itemHeight;
                }
            }

            // ============================================================
            // Calculate total width from columns
            // ============================================================
            int totalWidth = 0;

            // Left padding
            totalWidth += style.paddingLeft;

            // Checkbox/Radio column (reserve space if ANY item has it)
            if (hasAnyCheckboxOrRadio) {
                totalWidth += style.iconSize + style.iconSpacing;
            } else if (hasAnyIcon) {
                totalWidth += style.iconSize + style.iconSpacing;
            }

            // Label column (maximum label width)
            totalWidth += maxLabelWidth;

            // Gap + Shortcut column (only if ANY item has shortcut)
            if (hasAnyShortcut) {
                // Calculate 3-character minimum gap using 'M' width (widest character)
                int threeCharGap = ctx->GetTextLineWidth("MMM");
                int effectiveGap = std::max(style.shortcutSpacing, threeCharGap);

                totalWidth += effectiveGap + maxShortcutWidth;
            }

            // Submenu arrow column (reserve space if ANY item has submenu)
            if (hasAnySubmenu) {
                totalWidth += 20;  // Arrow space
            }

            // Right padding
            totalWidth += style.paddingRight;

            SetWidth(totalWidth);
            if (style.minWidth > 0) {
                SetWidth(std::max(GetWidth(), style.minWidth));
            }
            SetHeight(totalHeight);

            // Clamp to window bounds and add scrollbar if needed
            ClampMenuToWindow();
            ClampMenuToWindowHorizontal();
        }
    }

    void UltraCanvasMenu::RenderItem(int index, const MenuItemData &item, IRenderContext *ctx) {
        if (!item.visible) return;
        Rect2Di itemBounds = GetItemBounds(index);

        // Draw item background
        Color bgColor = GetItemBackgroundColor(index, item);
        if (bgColor.a > 0) {
            ctx->DrawFilledRectangle(itemBounds, bgColor);
        }

        // Handle separator
        if (item.type == MenuItemType::Separator) {
            RenderSeparator(itemBounds, ctx);
            return;
        }

        // Handle header
        if (item.type == MenuItemType::Header) {
            RenderHeader(item, itemBounds, ctx);
            return;
        }

        ctx->SetFontStyle(item.font.value_or(style.font));

        Point2Di textSize = ctx->GetTextDimension(item.label);
        int fontHeight = textSize.y;
        int currentX = itemBounds.x + style.paddingLeft;
        int textY = itemBounds.y + (itemBounds.height - fontHeight) / 2;

        // Render checkbox/radio
        if (item.type == MenuItemType::Checkbox || item.type == MenuItemType::Radio) {
            int checkboxY = itemBounds.y + (itemBounds.height - style.iconSize) / 2 - 1;
            RenderCheckbox(item, Point2Di(currentX, checkboxY), ctx);
            currentX += style.iconSize + style.iconSpacing;
        }
        // if (item.type == MenuItemType::Checkbox || item.type == MenuItemType::Radio) {

        //     RenderCheckbox(item, Point2Di(currentX, textY), ctx);
        //     currentX += style.iconSize + style.iconSpacing;
        // }

        // Render icon
        if (!item.iconPath.empty()) {
            int iconY = itemBounds.y + (itemBounds.height - style.iconSize) / 2;
            RenderIcon(item.iconPath, Point2Di(currentX, iconY), ctx);
            currentX += style.iconSize + style.iconSpacing;
        }

        // Render text
        if (!item.label.empty()) {
            Color textColor = item.enabled ?
                              (index == activeIndex ? style.hoverTextColor : style.textColor) :
                              style.disabledTextColor;

            ctx->SetTextPaint(textColor);
            ctx->DrawText(item.label, Point2Di(currentX, textY));
        }

        // Render shortcut (for vertical menus)
        if (!item.shortcut.empty() && orientation == MenuOrientation::Vertical) {
            std::string displayShortcut = GetDisplayShortcut(item.shortcut);
            int shortcutX = itemBounds.x + itemBounds.width - style.paddingRight -
                            ctx->GetTextLineWidth(displayShortcut.c_str());
            ctx->SetTextPaint(style.shortcutColor);
            ctx->DrawText(displayShortcut, Point2Di(shortcutX, textY));
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
            int currentY = GetY() - scrollOffsetPixels;

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

        // Text width
        if (!item.label.empty()) {
            width += ctx->GetTextLineWidth(item.label.c_str());
        }

        // The following elements are only rendered in vertical menus
        // (dropdowns/popups/submenus), never in the horizontal menubar
        if (orientation == MenuOrientation::Vertical) {
            // Shortcut width
            if (!item.shortcut.empty()) {
                std::string displayShortcut = GetDisplayShortcut(item.shortcut);
                width += style.shortcutSpacing + ctx->GetTextLineWidth(displayShortcut);
            }

            // Submenu arrow
            if (!item.subItems.empty()) {
                width += 20;  // Arrow space
            }

            // Checkbox/radio space
            if (item.type == MenuItemType::Checkbox || item.type == MenuItemType::Radio) {
                width += style.iconSize + style.iconSpacing;
            }
        }

        return width;
    }

    Point2Di UltraCanvasMenu::GetPositionSubmenu(const UltraCanvasMenu &submenu, int itemIndex) {
        Point2Di submenuPos;

        if (orientation == MenuOrientation::Vertical) {
            // Position to the right of the item
            submenuPos.x = GetXInWindow() + GetWidth() + style.submenuOffset;
            submenuPos.y = GetYInWindow() + GetItemY(itemIndex) - style.paddingTop;
        } else {
            // Position below the item
            submenuPos.x = GetXInWindow() + GetItemX(itemIndex);
            submenuPos.y = GetYInWindow() + GetHeight() + style.submenuOffset;
        }

        // Adjust for window boundaries
        auto win = GetWindow();
        if (win) {
            int windowWidth = win->GetWidth();
            int windowHeight = win->GetHeight();
            int submenuWidth = submenu.GetWidth();
            int submenuHeight = submenu.GetHeight();

            // Horizontal: flip to left side if overflows right edge
            if (submenuPos.x + submenuWidth > windowWidth) {
                if (orientation == MenuOrientation::Vertical) {
                    submenuPos.x = GetXInWindow() - submenuWidth - style.submenuOffset;
                } else {
                    submenuPos.x = windowWidth - submenuWidth;
                }
                if (submenuPos.x < 0) submenuPos.x = 0;
            }

            // Vertical: shift up if overflows bottom edge
            if (submenuPos.y + submenuHeight > windowHeight) {
                submenuPos.y = windowHeight - submenuHeight;
                if (submenuPos.y < 0) submenuPos.y = 0;
            }
        }

        return submenuPos;
    }

    void UltraCanvasMenu::RenderSeparator(const Rect2Di &bounds, IRenderContext *ctx) {
        int centerY = bounds.y + bounds.height / 2;
        int startX = bounds.x + style.paddingLeft;
        int endX = bounds.x + bounds.width - style.paddingRight;

        ctx->SetStrokePaint(style.separatorColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(Point2Df(startX, centerY), Point2Df(endX, centerY));
    }

    void UltraCanvasMenu::RenderHeader(const MenuItemData &item, const Rect2Di &bounds, IRenderContext *ctx) {
        FontStyle headerFont = item.font.value_or(style.font);
        headerFont.fontWeight = FontWeight::Bold;
        ctx->SetFontStyle(headerFont);

        Point2Di textSize = ctx->GetTextDimension(item.label);
        int fontHeight = textSize.y;
        int textX = bounds.x + style.paddingLeft;
        int textY = bounds.y + (bounds.height - fontHeight) / 2;

        ctx->SetTextPaint(style.headerTextColor);
        ctx->DrawText(item.label, Point2Di(textX, textY));
    }

    void UltraCanvasMenu::RenderCheckbox(const MenuItemData &item, const Point2Di &position, IRenderContext *ctx) {
        Rect2Di checkRect(position.x, position.y, style.iconSize, style.iconSize);

        ctx->DrawFilledRectangle(checkRect, Colors::Transparent, 1, style.borderColor);

        if (item.checked) {
            ctx->SetStrokePaint(style.textColor);
            ctx->SetStrokeWidth(2.0f);

            if (item.type == MenuItemType::Checkbox) {
                // Draw checkmark
                Point2Df p1(position.x + 3, position.y + style.iconSize / 2);
                Point2Df p2(position.x + style.iconSize / 2, position.y + style.iconSize - 3);
                Point2Df p3(position.x + style.iconSize - 3, position.y + 3);
                ctx->DrawLine(p1, p2);
                ctx->DrawLine(p2, p3);
            } else {
                // Draw radio dot
                Point2Df center = {position.x + static_cast<double>(style.iconSize) / 2.0,
                                    position.y + static_cast<double>(style.iconSize) / 2.0};
                ctx->DrawCircle(center, static_cast<double>(style.iconSize) / 4.0);
            }
        }
    }

    void UltraCanvasMenu::RenderSubmenuArrow(const Point2Di &position, IRenderContext *ctx) {
        ctx->SetStrokePaint(style.textColor);
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

    void UltraCanvasMenu::RenderIcon(const std::string &iconPath, const Point2Di &position, IRenderContext *ctx) {
        // Would implement icon rendering based on file type
        ctx->DrawImage(iconPath, Rect2Df(position.x, position.y, style.iconSize, style.iconSize), ImageFitMode::Contain);
    }


    void UltraCanvasMenu::RenderShadow(IRenderContext *ctx) {
        Rect2Di bounds = GetBounds();
        ctx->SetStrokePaint(style.shadowColor);
        ctx->DrawRectangle(Rect2Df(bounds.x + style.shadowOffset.x, bounds.y + style.shadowOffset.y, bounds.width,
                           bounds.height));
    }

    int UltraCanvasMenu::GetItemUnderPointer(const UCEvent & ev) const {
        // Check if position is within menu bounds
        Rect2Di bnds = GetBoundsInWindow();
        if (!bnds.Contains(ev.windowX, ev.windowY)) {
            return -1;
        }

        // For horizontal menus, iterate through items by X position
        if (orientation == MenuOrientation::Horizontal) {
            int currentX = bnds.x;

            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                if (!items[i].visible) continue;

                int itemWidth = CalculateItemWidth(items[i]) + style.paddingLeft + style.paddingRight;
                if (items[i].type == MenuItemType::Checkbox || items[i].type == MenuItemType::Radio ||
                    !items[i].iconPath.empty()) {
                    itemWidth += style.iconSpacing;
                }

                if (ev.windowX >= currentX && ev.windowX < currentX + itemWidth) {
                    return i;
                }

                currentX += itemWidth;
            }
        } else {
            // For vertical menus, iterate through items by Y position
            int currentY = bnds.y - scrollOffsetPixels;

            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                if (!items[i].visible) continue;

                int itemHeight = items[i].type == MenuItemType::Separator ?
                                 style.separatorHeight : style.itemHeight;

                if (ev.windowY >= currentY && ev.windowY < currentY + itemHeight) {
                    return i;
                }

                currentY += itemHeight;
            }
        }

        return -1;
    }

    void UltraCanvasMenu::OnPopupClosed(ClosePopupReason reason) {
        if (reason == ClosePopupReason::ClickOutside) {
            CloseMenutree();
        } else {
            CloseAllSubmenus();
        }
        needCalculateSize = true;
        scrollOffsetPixels = 0;
        needsScrollbar = false;
        if (onMenuClosed) onMenuClosed();

        // Clear parent menu's reference to this submenu so it can be reopened
        auto parentMnu = parentMenu.lock();
        if (parentMnu && parentMnu->activeSubmenu.get() == this) {
            auto it = std::find(parentMnu->childMenus.begin(), parentMnu->childMenus.end(), parentMnu->activeSubmenu);
            if (it != parentMnu->childMenus.end()) {
                parentMnu->childMenus.erase(it);
            }
            parentMnu->activeSubmenu.reset();
        }

        UltraCanvasUIElement::OnPopupClosed(reason);
    }


    bool UltraCanvasMenu::HandleMouseMove(const UCEvent &event) {
        // Forward to scrollbar if dragging
        if (menuScrollbar && menuScrollbar->IsDragging()) {
            return menuScrollbar->OnEvent(event);
        }

        int newHoveredIndex = GetItemUnderPointer(event);

        if (newHoveredIndex != activeIndex) {
            activeIndex = newHoveredIndex;

            if (onItemHovered && activeIndex >= 0) {
                onItemHovered(activeIndex);
            }
            RequestRedraw();

            // Auto-open submenu on hover (with delay)
            if (activeIndex >= 0 && activeIndex < static_cast<int>(items.size())) {
                const MenuItemData &item = items[activeIndex];
                if (!item.subItems.empty()) {
                    // In a complete implementation, you'd add a timer for submenu delay
                    OpenSubmenu(activeIndex);
                } else {
                    if (activeSubmenu && activeSubmenu->parentItemIndex != activeIndex) {
                        CloseActiveSubmenu();
                    }
                }
            }
        }
        if (newHoveredIndex >= 0) {
            return true;
        } else {
            auto parentMnu = parentMenu.lock();
            if (parentMnu) {
                parentMnu->HandleMouseMove(event);
            }
        }
        return false;
    }

    bool UltraCanvasMenu::HandleMouseDown(const UCEvent &event) {
        // Forward to scrollbar if clicking on it
        if (needsScrollbar && menuScrollbar && menuScrollbar->Contains(event.x, event.y)) {
            return menuScrollbar->OnEvent(event);
        }

        int clickedIndex = GetItemUnderPointer(event);
        if (clickedIndex >= 0 && clickedIndex < static_cast<int>(items.size())) {
            selectedIndex = clickedIndex;
            RequestRedraw();
        }
        return true;
    }

    bool UltraCanvasMenu::HandleMouseUp(const UCEvent &event) {
        if (menuScrollbar && menuScrollbar->IsDragging()) {
            return menuScrollbar->OnEvent(event);
        }
        if (!Contains(event.x, event.y)) return false;

        int clickedIndex = GetItemUnderPointer(event);

        if (clickedIndex >= 0 && clickedIndex < static_cast<int>(items.size()) && clickedIndex == selectedIndex) {
            ExecuteItem(clickedIndex);
        }

        selectedIndex = -1;
        RequestRedraw();
        return true;
    }

    bool UltraCanvasMenu::HandleKeyDown(const UCEvent &event) {

        switch (event.virtualKey) {
            case UCKeys::Up:
                NavigateUp();
                EnsureActiveItemVisible();
                RequestRedraw();
                return true;

            case UCKeys::Down:
                NavigateDown();
                EnsureActiveItemVisible();
                RequestRedraw();
                return true;

            case UCKeys::Left:
                if (orientation == MenuOrientation::Horizontal) {
                    NavigateLeft();
                } else {
                    CloseSubmenu();
                }
                RequestRedraw();
                return true;

            case UCKeys::Right:
                if (orientation == MenuOrientation::Horizontal) {
                    NavigateRight();
                } else {
                    OpenSubmenuFromKeyboard();
                }
                RequestRedraw();
                return true;

            case UCKeys::Return:
            case UCKeys::Space:
                if (activeIndex >= 0) {
                    ExecuteItem(activeIndex);
                }
                RequestRedraw();
                return true;

//            case UCKeys::Escape:
//                if (closeByEscapeKey) {
//                    Hide();
//                    return true;
//                }

            default:
                break;
        }

        return false;
    }

    void UltraCanvasMenu::NavigateUp() {
        if (items.empty()) return;

        do {
            activeIndex = (activeIndex <= 0) ? static_cast<int>(items.size()) - 1 : activeIndex - 1;
        } while (activeIndex >= 0 &&
                 (!items[activeIndex].visible ||
                  items[activeIndex].type == MenuItemType::Separator ||
                  items[activeIndex].type == MenuItemType::Header ||
                  !items[activeIndex].enabled));
    }

    void UltraCanvasMenu::NavigateDown() {
        if (items.empty()) return;

        do {
            activeIndex = (activeIndex >= static_cast<int>(items.size()) - 1) ? 0 : activeIndex + 1;
        } while (activeIndex < static_cast<int>(items.size()) &&
                 (!items[activeIndex].visible ||
                  items[activeIndex].type == MenuItemType::Separator ||
                  items[activeIndex].type == MenuItemType::Header ||
                  !items[activeIndex].enabled));
    }

    void UltraCanvasMenu::OpenSubmenuFromKeyboard() {
        if (activeIndex >= 0 && activeIndex < static_cast<int>(items.size())) {
            const MenuItemData &item = items[activeIndex];
            if (!item.subItems.empty()) {
                OpenSubmenu(activeIndex);
                if (activeSubmenu) {
                    activeSubmenu->activeIndex = 0;
                }
            }
        }
    }

    void UltraCanvasMenu::CloseSubmenu() {
        CloseActiveSubmenu();
    }

    void UltraCanvasMenu::ExecuteItem(int index) {
        if (index < 0 || index >= static_cast<int>(items.size())) return;

        MenuItemData &item = items[index];
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
                // Propagate checked state back to parent menu's subItems
                if (auto parent = parentMenu.lock()) {
                    if (parentItemIndex >= 0) {
                        auto &subItems = parent->items[parentItemIndex].subItems;
                        if (index < static_cast<int>(subItems.size())) {
                            subItems[index].checked = item.checked;
                        }
                    }
                }
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
                // Propagate radio states back to parent menu's subItems
                if (auto parent = parentMenu.lock()) {
                    if (parentItemIndex >= 0) {
                        auto &subItems = parent->items[parentItemIndex].subItems;
                        for (size_t i = 0; i < items.size() && i < subItems.size(); i++) {
                            if (items[i].type == MenuItemType::Radio &&
                                items[i].radioGroup == item.radioGroup) {
                                subItems[i].checked = items[i].checked;
                            }
                        }
                    }
                }
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
        if (!item.enabled || item.type == MenuItemType::Header) return Colors::Transparent;

        if (index == activeIndex) {
            return style.hoverColor;
        }

        if (index == selectedIndex) {
            return style.pressedColor;
        }

        return Colors::Transparent;
    }

    // ===== SCROLL SUPPORT =====

    void UltraCanvasMenu::CreateMenuScrollbar() {
        menuScrollbar = std::make_shared<UltraCanvasScrollbar>(
                GetIdentifier() + "_scroll", 0, 0, 0, 10, 100,
                ScrollbarOrientation::Vertical);

        menuScrollbar->SetStyle(style.scrollbarStyle);

        menuScrollbar->onScrollChange = [this](int pos) {
            int maxScroll = std::max(0, totalContentHeight - clampedMenuHeight);
            scrollOffsetPixels = std::clamp(pos, 0, maxScroll);
            RequestRedraw();
        };
    }

    void UltraCanvasMenu::ClampMenuToWindow() {
        if (orientation != MenuOrientation::Vertical) return;

        auto win = GetWindow();
        if (!win) return;

        int windowHeight = win->GetHeight();
        int menuY = GetYInWindow();
        int fullHeight = GetHeight();

        totalContentHeight = fullHeight;

        int spaceBelow = windowHeight - menuY;
        int spaceAbove = menuY;

        const int minMenuHeight = style.itemHeight * 3;

        if (fullHeight <= spaceBelow) {
            needsScrollbar = false;
            clampedMenuHeight = fullHeight;
        } else if (fullHeight <= spaceAbove) {
            // Flip above
            SetPosition(GetX(), menuY - fullHeight);
            needsScrollbar = false;
            clampedMenuHeight = fullHeight;
        } else {
            // Clamp to larger available space
            int availableHeight = std::max(spaceBelow, spaceAbove);
            clampedMenuHeight = std::max(minMenuHeight, availableHeight);
            needsScrollbar = true;

            if (spaceAbove > spaceBelow) {
                SetPosition(GetX(), menuY - clampedMenuHeight);
            }

            SetHeight(clampedMenuHeight);

            if (!menuScrollbar) CreateMenuScrollbar();
            menuScrollbar->SetContentSize(totalContentHeight);
            menuScrollbar->SetViewportSize(clampedMenuHeight);
            menuScrollbar->SetScrollPosition(scrollOffsetPixels);
            menuScrollbar->SetWindow(win);
        }
    }

    void UltraCanvasMenu::ClampMenuToWindowHorizontal() {
        auto win = GetWindow();
        if (!win) return;

        int windowWidth = win->GetWidth();
        int menuX = GetXInWindow();
        int menuWidth = GetWidth();

        if (menuX + menuWidth > windowWidth) {
            int newX = windowWidth - menuWidth;
            if (newX < 0) newX = 0;
            SetPosition(newX, GetY());
        }
    }

    void UltraCanvasMenu::EnsureActiveItemVisible() {
        if (!needsScrollbar || activeIndex < 0) return;

        // Calculate the Y position of the keyboard item relative to content start
        int itemY = 0;
        for (int i = 0; i < activeIndex && i < static_cast<int>(items.size()); ++i) {
            if (!items[i].visible) continue;
            itemY += (items[i].type == MenuItemType::Separator) ?
                     style.separatorHeight : style.itemHeight;
        }

        int itemHeight = (items[activeIndex].type == MenuItemType::Separator) ?
                         style.separatorHeight : style.itemHeight;

        // Scroll up if item is above visible area
        if (itemY < scrollOffsetPixels) {
            scrollOffsetPixels = itemY;
        }
            // Scroll down if item is below visible area
        else if (itemY + itemHeight > scrollOffsetPixels + clampedMenuHeight) {
            scrollOffsetPixels = itemY + itemHeight - clampedMenuHeight;
        }

        int maxScroll = std::max(0, totalContentHeight - clampedMenuHeight);
        scrollOffsetPixels = std::clamp(scrollOffsetPixels, 0, maxScroll);

        if (menuScrollbar) {
            menuScrollbar->SetScrollPosition(scrollOffsetPixels);
        }
    }

    bool UltraCanvasMenu::HandleMouseWheel(const UCEvent &event) {
        if (!needsScrollbar) return false;
        if (!Contains(event.x, event.y)) return false;

        int delta = event.wheelDelta > 0 ? -style.itemHeight : style.itemHeight;
        int maxScroll = std::max(0, totalContentHeight - clampedMenuHeight);
        scrollOffsetPixels = std::clamp(scrollOffsetPixels + delta, 0, maxScroll);

        if (menuScrollbar) {
            menuScrollbar->SetScrollPosition(scrollOffsetPixels);
        }

        RequestRedraw();
        return true;
    }

    void UltraCanvasMenu::StartAnimation() {
        animationStartTime = std::chrono::steady_clock::now();
        animationProgress = 0.0f;
    }

    void UltraCanvasMenu::UpdateAnimation() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - animationStartTime);
        float elapsedSeconds = elapsed.count() / 1000.0f;

        animationProgress = std::min(1.0f, elapsedSeconds / style.animationDuration);

//        if (animationProgress >= 1.0f) {
//            // Animation complete
//            if (currentState == MenuState::Opening) {
//                currentState = MenuState::Visible;
//            } else if (currentState == MenuState::Closing) {
//                currentState = MenuState::Hidden;
//                SetVisible(false);
//            }
//        }

        // Apply animation effects (scale, fade, etc.)
        // This would modify the rendering parameters based on animationProgress
    }

    void UltraCanvasMenu::AddItem(const MenuItemData &item) {
        items.push_back(item);
        needCalculateSize = true;
    }

    void UltraCanvasMenu::InsertItem(int index, const MenuItemData &item) {
        if (index >= 0 && index <= static_cast<int>(items.size())) {
            items.insert(items.begin() + index, item);
            needCalculateSize = true;
        }
    }

    void UltraCanvasMenu::RemoveItem(int index) {
        if (index >= 0 && index < static_cast<int>(items.size())) {
            items.erase(items.begin() + index);
            needCalculateSize = true;
        }
    }

    void UltraCanvasMenu::UpdateItem(int index, const MenuItemData &item) {
        if (index >= 0 && index < static_cast<int>(items.size())) {
            items[index] = item;
            needCalculateSize = true;
        }
    }

    void UltraCanvasMenu::Clear() {
        items.clear();
        CloseAllSubmenus();
        needCalculateSize = true;
    }

    MenuItemData *UltraCanvasMenu::GetItem(int index) {
        if (index >= 0 && index < static_cast<int>(items.size())) {
            return &items[index];
        }
        return nullptr;
    }

}

// UltraCanvasMenu.cpp
// Interactive menu component with styling options and submenu support
// Version: 1.8.1 - MenuClick event.targetElement set via weak_from_this()
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework

#include <vector>
#include "UltraCanvasMenu.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasTooltipManager.h"

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
            InvalidateLayout();
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
        UltraCanvasTooltipManager::HideTooltip();
        window->ClosePopup(*this, ClosePopupReason::Manual);
        debugOutput << "Menu '" << GetIdentifier() << "' hidden.  Visible: " << IsVisible() << std::endl;
    }

    void UltraCanvasMenu::Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) {
        // The CSS engine has already sized us from the measure pass (content-driven) and
        // placed us at our requested position. Post-layout, clamp the vertical popup
        // to the window (flip above / shift left, add a scrollbar on overflow). This
        // runs on EVERY layout pass and adjusts finalBounds for the current frame only
        // (no SetElementSize/SetElementAbsolutePosition), so it stays stable across
        // re-layouts and window resizes.
        UltraCanvasUIElement::Arrange(finalRect, ctx);

        if (orientation == MenuOrientation::Vertical &&
            (menuType == MenuType::PopupMenu || menuType == MenuType::SubmenuMenu)) {
            ClampMenuToWindow();
            ClampMenuToWindowHorizontal();
        }
    }

    void UltraCanvasMenu::Render(IRenderContext *ctx, const Rect2Df& dirtyRect) {
        // FIX: Simplified visibility check - if not visible at all, don't render
        if (menuType == MenuType::Menubar) {

            // Render background in element-local space
            Rect2Di bounds = GetLocalBounds();
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

            Rect2Di bounds = GetLocalBounds();

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
            ctx->ClipRect(Rect2Dd(bw,
                                  bw,
                                  finalBounds.width - bw * 2 - sbWidth,
                                  finalBounds.height - bw * 2));

            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                if (items[i].visible) {
                    RenderItem(i, items[i], ctx);
                }
            }


            ctx->PopState();  // releases the clip region

            // Render scrollbar for overflow menus (bounds stored in menu-local space;
            // translate ctx so the scrollbar can draw from (0,0) as per element-local convention)
            if (needsScrollbar && menuScrollbar) {
                int scrollbarWidth = static_cast<int>(style.scrollbarStyle.trackSize);
                menuScrollbar->SetBounds(Rect2Di(
                        finalBounds.width - scrollbarWidth - bw,
                        bw,
                        scrollbarWidth,
                        finalBounds.height - bw * 2));
                menuScrollbar->SetScrollPosition(scrollOffsetPixels);
                ctx->PushState();
                auto sbB = menuScrollbar->GetBounds();
                ctx->Translate(Point2Di(sbB.x, sbB.y));
                menuScrollbar->Render(ctx, dirtyRect);
                ctx->PopState();
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
                UltraCanvasTooltipManager::HideTooltip();
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
                MakeContentSized();
                break;
            case MenuType::SubmenuMenu:
                SetVisible(false);
                orientation = MenuOrientation::Vertical;
                MakeContentSized();
                break;
        }
    }

    void UltraCanvasMenu::MakeContentSized() {
        // Vertical popups/submenus size to their content via MeasureOwnContent. Any fixed
        // width stamped by the constructor (e.g. CreateMenu(..., 200, 0)) would
        // otherwise pin the CSS `size`, and the absolute-position solver would reset
        // the menu to that width on every re-layout (shrinking it when a submenu
        // opens). Fold that width into style.minWidth and clear the CSS size so the
        // engine uses the measured, content-driven size instead.
        if (!size.width.isAuto()) {
            float ctorWidth = dimPx(size.width);
            if (ctorWidth > style.minWidth) {
                style.minWidth = (int)ctorWidth;
            }
        }
        size.width  = CSSLayout::Dimension::Auto();
        size.height = CSSLayout::Dimension::Auto();
        InvalidateLayout();
    }

    void UltraCanvasMenu::OpenSubmenu(int itemIndex) {
        auto parentWindow = GetWindow();
        if (!parentWindow || itemIndex < 0 || itemIndex >= static_cast<int>(items.size())) return;
        if (activeSubmenu && activeSubmenu->parentItemIndex == itemIndex) return;

        const MenuItemData &item = items[itemIndex];
        if (item.subItems.empty() && !item.subItemsProvider) return;

        // Close existing submenu
        CloseActiveSubmenu();

        // Create and show new submenu. No fixed size — it sizes to its content via
        // MeasureOwnContent (see MakeContentSized in SetMenuType).
        activeSubmenu = std::make_shared<UltraCanvasMenu>(
                GetIdentifier() + "_submenu_" + std::to_string(itemIndex),
                0, 0, 0, 0
        );

        activeSubmenu->SetMenuType(MenuType::SubmenuMenu);
        activeSubmenu->SetStyle(style);
        if (item.submenuMaxWidth > 0) {
            MenuStyle subStyle = activeSubmenu->GetStyle();
            subStyle.maxWidth = item.submenuMaxWidth;
            activeSubmenu->SetStyle(subStyle);
        }
        activeSubmenu->parentMenu = std::static_pointer_cast<UltraCanvasMenu>(shared_from_this());
        activeSubmenu->parentItemIndex = itemIndex;

        // Add items to submenu — if a provider lambda is set, invoke it fresh on every open.
        std::vector<MenuItemData> providedItems;
        const std::vector<MenuItemData> &sourceItems =
                item.subItemsProvider ? (providedItems = item.subItemsProvider(), providedItems)
                                      : item.subItems;
        for (const auto &subItem: sourceItems) {
            activeSubmenu->AddItem(subItem);
        }

        // Position submenu
        Point2Di pos = GetPositionSubmenu(*activeSubmenu, itemIndex);

        // Show submenu
        activeSubmenu->OpenMenu(pos, *parentWindow, menuPopupSettings);
        childMenus.push_back(activeSubmenu);
    }

    void UltraCanvasMenu::CloseActiveSubmenu() {
        // Local copy keeps the submenu alive across CloseMenu(): OnPopupClosed
        // (line ~844) resets this->activeSubmenu reentrantly, which would otherwise
        // destroy the submenu while it is still inside its own CloseMenu() frame.
        if (auto sub = activeSubmenu) {
            sub->CloseMenu();

            // Remove from child menus
            auto it = std::find(childMenus.begin(), childMenus.end(), sub);
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

    bool UltraCanvasMenu::ContainsInWindow(const Point2Df& point) {
        if (menuType == MenuType::PopupMenu || menuType == MenuType::SubmenuMenu) {
            // Only check bounds if menu is actually visible
            if (!IsVisible()) {
                return false;
            }
        }

        if (GetBoundsInWindow().Contains(point)) {
            return true;
        }

        // look bottom on tree
        auto mnu = activeSubmenu;
        while (mnu) {
            if (mnu->GetBoundsInWindow().Contains(point)) return true;
            mnu = mnu->activeSubmenu;
        }

        // look top on tree
        mnu = parentMenu.lock();
        while (mnu) {
            if (mnu->GetBoundsInWindow().Contains(point)) return true;
            mnu = mnu->parentMenu.lock();
        }

        return false;
    }

    Size2Df UltraCanvasMenu::MeasureOwnContent(std::optional<float> /*definiteContentWidth*/,
                                               const CSSLayout::LayoutContext& /*ctx*/) {
        IRenderContext* rc = GetRenderContext();
        if (!rc) {
            // No surface to measure text against yet — report a usable default so
            // a menu with no explicit size still gets sane dimensions. (The block
            // path still honors an explicit size.width/height if one is set.)
            return Size2Df(100.f, (float)style.itemHeight);
        }
        // MeasureMenuContent returns the full content-driven size (it already
        // honors an explicit menubar width and its own style min/max). The block
        // layout adds CSS padding/border (zero here) and applies size.*/constraints.
        rc->PushState();
        Size2Df content = MeasureMenuContent(rc);
        rc->PopState();
        return content;
    }

    Size2Df UltraCanvasMenu::MeasureMenuContent(IRenderContext *ctx) const {
        ctx->SetFontStyle(style.font);

        if (items.empty()) {
            return Size2Df(100.0f, style.itemHeight);
        }

        if (orientation == MenuOrientation::Horizontal) {
            // Horizontal (menubar) layout calculation
            int totalWidth = 0;
            int maxHeight = style.itemHeight;

            for (const auto &item: items) {
                if (!item.visible) continue;
                ctx->SetFontStyle(item.font.value_or(style.font));
                totalWidth += CalculateItemWidth(item) + style.paddingLeft + style.paddingRight;
            }
            // Honor an explicit CSS width (menubar is created with a fixed width);
            // otherwise size to the summed item widths.
            float width = !size.width.isAuto() ? dimPx(size.width) : (float)totalWidth;
            return Size2Df(width, (float)maxHeight);

        } else {
            // ============================================================
            // VERTICAL LAYOUT - Column-based width calculation
            // ============================================================
            // Menu item layout (left to right):
            // | paddingLeft | [checkbox] | [icon] | label | gap | [shortcut] | [arrow] | paddingRight |
            // ============================================================

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
                if (item.HasSubmenu()) {
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

            float width = (float)totalWidth;
            if (style.minWidth > 0) {
                width = std::max(width, (float)style.minWidth);
            }
            if (style.maxWidth > 0) {
                width = std::min(width, (float)style.maxWidth);
            }

            // Publish the FULL content height. The window-overflow clamp (and the
            // scrollbar it triggers) is applied later in Arrange()/ClampMenuToWindow,
            // which knows our resolved on-screen position.
            return Size2Df(width, (float)totalHeight);
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
            if (item.iconPath != "-") {
                RenderIcon(item.iconPath, Point2Di(currentX, iconY), ctx);
            }
            currentX += style.iconSize + style.iconSpacing;
        }

        // Render text
        if (!item.label.empty()) {
            Color textColor = item.enabled ?
                              (index == activeIndex ? style.hoverTextColor : style.textColor) :
                              style.disabledTextColor;

            // Compute the horizontal space available for the label, so the
            // text layout can ellipsize correctly when the menu is width-clamped.
            int labelRightX = itemBounds.x + itemBounds.width - style.paddingRight;
            if (item.HasSubmenu() && orientation == MenuOrientation::Vertical) {
                labelRightX -= 20;  // submenu arrow column
            }
            if (!item.shortcut.empty() && orientation == MenuOrientation::Vertical) {
                std::string displayShortcut = GetDisplayShortcut(item.shortcut);
                labelRightX -= ctx->GetTextLineWidth(displayShortcut.c_str()) + style.shortcutSpacing;
            }
            int labelMaxWidth = std::max(0, labelRightX - currentX);

            auto labelLayout = ctx->CreateTextLayout(item.label, false);
            labelLayout->SetFontStyle(item.font.value_or(style.font));
            labelLayout->SetWrap(TextWrap::WrapNone);
            labelLayout->SetEllipsize(item.ellipsize);
            labelLayout->SetExplicitWidth(labelMaxWidth);
            ctx->SetTextPaint(textColor);
            ctx->DrawTextLayout(*labelLayout, Point2Di(currentX, textY));
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
        if (item.HasSubmenu() && orientation == MenuOrientation::Vertical) {
            RenderSubmenuArrow(Point2Di(itemBounds.x + itemBounds.width - style.paddingRight - 2,
                                        itemBounds.y + itemBounds.height / 2), ctx);
        }
    }

    Rect2Di UltraCanvasMenu::GetItemBounds(int index) const {
        // Returns element-local coordinates (ctx is translated to element origin at render time)
        Rect2Di rect;

        if (orientation == MenuOrientation::Horizontal) {
            int currentX = 0;

            for (int i = 0; i < index && i < static_cast<int>(items.size()); ++i) {
                if (items[i].visible) {
                    currentX += CalculateItemWidth(items[i]) + style.paddingLeft + style.paddingRight;
                }
            }

            rect.x = currentX;
            rect.y = 0;
            rect.width = CalculateItemWidth(items[index]) + style.paddingLeft + style.paddingRight;
            rect.height = style.itemHeight;
        } else {
            int currentY = -scrollOffsetPixels;

            for (int i = 0; i < index && i < static_cast<int>(items.size()); ++i) {
                if (items[i].visible) {
                    currentY += (items[i].type == MenuItemType::Separator) ?
                                style.separatorHeight : style.itemHeight;
                }
            }

            rect.x = 0;
            rect.y = currentY;
            rect.width = GetWidth();
            rect.height = (items[index].type == MenuItemType::Separator) ?
                            style.separatorHeight : style.itemHeight;
        }

        return rect;
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
            if (item.HasSubmenu()) {
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
            submenuPos.x = GetXInWindow() + GetWidth();
            submenuPos.y = GetYInWindow() + GetItemY(itemIndex) - style.paddingTop;
        } else {
            // Position below the item
            submenuPos.x = GetXInWindow() + GetItemX(itemIndex);
            submenuPos.y = GetYInWindow() + GetHeight();
        }

        // Adjust for window boundaries
        auto win = GetWindow();
        if (win) {
            int windowWidth = win->GetWidth();
            int windowHeight = win->GetHeight();

            // Measure the submenu's content up-front so overflow flipping uses its
            // real size. The submenu has no surface or layout yet (it isn't opened
            // until below), so measure it against THIS menu's render context.
            int submenuWidth = (int)submenu.finalBounds.width;
            int submenuHeight = (int)submenu.finalBounds.height;
            if (IRenderContext* rc = GetRenderContext()) {
                rc->PushState();
                Size2Df sz = submenu.MeasureMenuContent(rc);
                rc->PopState();
                submenuWidth = (int)sz.width;
                submenuHeight = (int)sz.height;
            }

            // Horizontal: flip to left side if overflows right edge
            if (submenuPos.x + submenuWidth > windowWidth) {
                if (orientation == MenuOrientation::Vertical) {
                    submenuPos.x = GetXInWindow() - submenuWidth;
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
        // bounds is element-local (see GetItemBounds); rendering happens in the
        // ctx translated to element origin, so use bounds, not finalBounds.
        int centerY = bounds.y + bounds.height / 2;
        int startX = bounds.x + style.paddingLeft;
        int endX = bounds.x + bounds.width - style.paddingRight;

        ctx->SetStrokePaint(style.separatorColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(Point2Dd(startX, centerY), Point2Dd(endX, centerY));
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
                Point2Dd p1(position.x + 3, position.y + style.iconSize / 2);
                Point2Dd p2(position.x + style.iconSize / 2, position.y + style.iconSize - 3);
                Point2Dd p3(position.x + style.iconSize - 3, position.y + 3);
                ctx->DrawLine(p1, p2);
                ctx->DrawLine(p2, p3);
            } else {
                // Draw radio dot
                Point2Dd center = {position.x + static_cast<double>(style.iconSize) / 2.0,
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
        ctx->DrawImage(iconPath, Rect2Dd(position.x, position.y, style.iconSize, style.iconSize), ImageFitMode::Contain);
    }


    void UltraCanvasMenu::RenderShadow(IRenderContext *ctx) {
        // Draw in element-local coordinates (ctx is translated to element origin)
        Rect2Di bounds = GetLocalBounds();
        ctx->SetStrokePaint(style.shadowColor);
        ctx->DrawRectangle(Rect2Dd(style.shadowOffset.x, style.shadowOffset.y, finalBounds.width,
                           finalBounds.height));
    }

    int UltraCanvasMenu::GetItemUnderPointer(const UCEvent & ev) const {
        // Check if position is within menu bounds
        Rect2Di bnds = GetBoundsInWindow();
        if (!bnds.Contains(ev.pointerWindow)) {
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

                if (ev.pointerWindow.x >= currentX && ev.pointerWindow.x < currentX + itemWidth) {
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

                if (ev.pointerWindow.y >= currentY && ev.pointerWindow.y < currentY + itemHeight) {
                    return i;
                }

                currentY += itemHeight;
            }
        }

        return -1;
    }

    void UltraCanvasMenu::OnPopupClosed(ClosePopupReason reason) {
        // CloseMenutree() below can release the last external shared_ptr to *this
        // (the menubar resets its activeSubmenu ref); pin lifetime for the rest of this method.
        auto selfGuard = shared_from_this();
        UltraCanvasTooltipManager::HideTooltip();
        if (reason == ClosePopupReason::ClickOutside) {
            CloseMenutree();
        } else {
            CloseAllSubmenus();
        }
        InvalidateLayout();
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
        // Forward to scrollbar if dragging (translate pointer to scrollbar-local)
        if (menuScrollbar && menuScrollbar->IsDragging()) {
            UCEvent localEvent = event;
            auto sbB = menuScrollbar->GetBounds();
            localEvent.pointer = event.pointer - sbB.TopLeft();
            return menuScrollbar->OnEvent(localEvent);
        }

        int newHoveredIndex = GetItemUnderPointer(event);

        if (newHoveredIndex != activeIndex) {
            activeIndex = newHoveredIndex;

            if (onItemHovered && activeIndex >= 0) {
                onItemHovered(activeIndex);
            }
            RequestRedraw();

            // Drive per-item tooltip on hover transition
            UltraCanvasTooltipManager::HideTooltip();
            if (activeIndex >= 0 && activeIndex < static_cast<int>(items.size()) &&
                !items[activeIndex].tooltip.empty()) {
                UltraCanvasTooltipManager::UpdateAndShowTooltip(
                        GetWindow(), items[activeIndex].tooltip, event.pointerWindow);
            }

            // Auto-open submenu on hover (with delay)
            if (activeIndex >= 0 && activeIndex < static_cast<int>(items.size())) {
                const MenuItemData &item = items[activeIndex];
                if (item.HasSubmenu()) {
                    // In a complete implementation, you'd add a timer for submenu delay
                    OpenSubmenu(activeIndex);
                } else {
                    if (activeSubmenu && activeSubmenu->parentItemIndex != activeIndex) {
                        CloseActiveSubmenu();
                    }
                }
            }
        } else if (activeIndex >= 0 && activeIndex < static_cast<int>(items.size()) &&
                   !items[activeIndex].tooltip.empty()) {
            // Same item, mouse moved — track position for followCursor styles (no-op otherwise)
            UltraCanvasTooltipManager::UpdateTooltipPosition(event.pointerWindow);
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
        // Forward to scrollbar if clicking on it (pointer is menu-local; translate to scrollbar-local)
        if (needsScrollbar && menuScrollbar) {
            auto sbB = menuScrollbar->GetBounds();
            if (sbB.Contains(event.pointer)) {
                UCEvent localEvent = event;
                localEvent.pointer = event.pointer - sbB.TopLeft();
                return menuScrollbar->OnEvent(localEvent);
            }
        }

        int clickedIndex = GetItemUnderPointer(event);
        if (clickedIndex >= 0 && clickedIndex < static_cast<int>(items.size())) {
            selectedIndex = clickedIndex;
            RequestRedraw();
        }
        return true;
    }

    bool UltraCanvasMenu::HandleMouseUp(const UCEvent &event) {
        // Pin lifetime: ExecuteItem -> CloseMenutree may release the last
        // external shared_ptr to *this synchronously inside this call stack.
        auto selfGuard = shared_from_this();

        if (menuScrollbar && menuScrollbar->IsDragging()) {
            UCEvent localEvent = event;
            auto sbB = menuScrollbar->GetBounds();
            localEvent.pointer = event.pointer - sbB.TopLeft();
            return menuScrollbar->OnEvent(localEvent);
        }
        if (!Contains(event.pointer)) return false;

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
                    auto selfGuard = shared_from_this();   // ExecuteItem may destroy *this
                    ExecuteItem(activeIndex);
                    RequestRedraw();
                }
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
            if (item.HasSubmenu()) {
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

        UltraCanvasTooltipManager::HideTooltip();

        // Handle different item types
        switch (item.type) {
            case MenuItemType::Action: {
                if (item.onClick) {
                    item.onClick();
                }
                UCEvent ev;
                ev.type = UCEventType::MenuClick;
                ev.targetElement = weak_from_this();
                ev.userDataPtr = &item;
                UltraCanvasApplication::GetInstance()->PushEvent(ev);
                break;
            }

            case MenuItemType::Checkbox: {
                item.checked = !item.checked;
                // Propagate checked state back to parent menu's subItems.
                // Note: for lambda-provided submenus (subItemsProvider), the submenu is
                // regenerated on every open — checked state does not persist here and
                // must be sourced from the caller's own model via the lambda.
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
                ev.targetElement = weak_from_this();
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
                ev.targetElement = weak_from_this();
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
                GetIdentifier() + "_scroll", 0, 0, 10, 100,
                ScrollbarOrientation::Vertical);

        menuScrollbar->SetStyle(style.scrollbarStyle);

        menuScrollbar->onScrollChange = [this](int pos) {
            int maxScroll = std::max(0, totalContentHeight - clampedMenuHeight);
            scrollOffsetPixels = std::clamp(pos, 0, maxScroll);
            RequestRedraw();
        };
    }

    // Post-layout vertical clamp. The engine has already placed us at our requested
    // position with our full measured height; here we flip above / shrink + add a
    // scrollbar when we'd run off the bottom of the window. We mutate finalBounds
    // directly (frame-local) rather than calling SetElementSize/SetElementAbsolutePosition
    // — those would invalidate layout and re-run this every frame. Because Arrange()
    // re-derives the full size from the measure pass each time, recomputing the clamp here
    // is stable and survives window resizes.
    void UltraCanvasMenu::ClampMenuToWindow() {
        if (orientation != MenuOrientation::Vertical) return;

        auto win = GetWindow();
        if (!win) return;

        int windowHeight = win->GetHeight();
        int menuY = (int)finalBounds.y;
        int fullHeight = (int)finalBounds.height;

        totalContentHeight = fullHeight;

        int spaceBelow = windowHeight - menuY;
        int spaceAbove = menuY;

        const int minMenuHeight = style.itemHeight * 3;

        if (fullHeight <= spaceBelow) {
            needsScrollbar = false;
            clampedMenuHeight = fullHeight;
        } else if (fullHeight <= spaceAbove) {
            // Flip above
            finalBounds.y = menuY - fullHeight;
            needsScrollbar = false;
            clampedMenuHeight = fullHeight;
        } else {
            // Clamp to larger available space
            int availableHeight = std::max(spaceBelow, spaceAbove);
            clampedMenuHeight = std::max(minMenuHeight, availableHeight);
            needsScrollbar = true;

            if (spaceAbove > spaceBelow) {
                finalBounds.y = menuY - clampedMenuHeight;
            }

            finalBounds.height = clampedMenuHeight;

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
        int menuX = (int)finalBounds.x;
        int menuWidth = (int)finalBounds.width;

        if (menuX + menuWidth > windowWidth) {
            int newX = windowWidth - menuWidth;
            if (newX < 0) newX = 0;
            finalBounds.x = newX;
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
        if (!Contains(event.pointer)) return false;

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
        InvalidateLayout();
    }

    void UltraCanvasMenu::InsertItem(int index, const MenuItemData &item) {
        if (index >= 0 && index <= static_cast<int>(items.size())) {
            items.insert(items.begin() + index, item);
            InvalidateLayout();
        }
    }

    void UltraCanvasMenu::RemoveItem(int index) {
        if (index >= 0 && index < static_cast<int>(items.size())) {
            items.erase(items.begin() + index);
            InvalidateLayout();
        }
    }

    void UltraCanvasMenu::UpdateItem(int index, const MenuItemData &item) {
        if (index >= 0 && index < static_cast<int>(items.size())) {
            items[index] = item;
            InvalidateLayout();
        }
    }

    void UltraCanvasMenu::Clear() {
        items.clear();
        CloseAllSubmenus();
        InvalidateLayout();
    }

    MenuItemData *UltraCanvasMenu::GetItem(int index) {
        if (index >= 0 && index < static_cast<int>(items.size())) {
            return &items[index];
        }
        return nullptr;
    }

}

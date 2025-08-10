// UltraCanvasMenu.h
// Interactive menu component with styling options and submenu support
// Version: 1.2.5
// Last Modified: 2025-01-08
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasKeyboardManager.h"
#include "UltraCanvasZOrderManager.h"
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <chrono>
#include <algorithm>

namespace UltraCanvas {

// ===== MENU TYPES AND ENUMS =====
    enum class MenuType {
        MainMenu,
        DropdownMenu,
        ContextMenu,
        PopupMenu,
        SubmenuMenu
    };

    enum class MenuOrientation {
        Vertical,
        Horizontal
    };

    enum class MenuState {
        Hidden,
        Opening,
        Visible,
        Closing
    };

    enum class MenuItemType {
        Action,
        Separator,
        Checkbox,
        Radio,
        Submenu,
        Input,
        Custom
    };

// ===== MENU ITEM DATA =====
    struct MenuItemData {
        MenuItemType type = MenuItemType::Action;
        std::string label;
        std::string shortcut;
        std::string iconPath;
        bool enabled = true;
        bool visible = true;
        bool checked = false;
        int radioGroup = 0;

        // Callbacks
        std::function<void()> onClick;
        std::function<void(bool)> onToggle;
        std::function<void(const std::string&)> onTextInput;

        // Submenu items
        std::vector<MenuItemData> subItems;

        // Custom data
        void* userData = nullptr;

        // Constructors
        MenuItemData() = default;
        MenuItemData(const std::string& itemLabel) : label(itemLabel) {}
        MenuItemData(const std::string& itemLabel, std::function<void()> callback)
                : label(itemLabel), onClick(callback) {}

        // Factory methods
        static MenuItemData Action(const std::string& label, std::function<void()> callback);
        static MenuItemData Separator();
        static MenuItemData Checkbox(const std::string& label, bool checked, std::function<void(bool)> callback);
        static MenuItemData Radio(const std::string& label, int group, std::function<void()> callback);
        static MenuItemData Submenu(const std::string& label, const std::vector<MenuItemData>& items);
        static MenuItemData Input(const std::string& label, const std::string& placeholder, std::function<void(const std::string&)> callback);
    };

// ===== MENU STYLING =====
    struct MenuStyle {
        // Colors
        Color backgroundColor = Color(248, 248, 248);
        Color borderColor = Color(200, 200, 200);
        Color hoverColor = Color(230, 240, 255);
        Color hoverTextColor = Color(0, 0, 0, 255);
        Color pressedColor = Color(210, 230, 255);
        Color selectedColor = Color(25, 118, 210, 50);
        Color separatorColor = Color(220, 220, 220);
        Color textColor = Colors::Black;
        Color shortcutColor = Color(100, 100, 100, 255);
        Color disabledTextColor = Color(150, 150, 150);

        // Typography
        std::string fontFamily = "Arial";
        float fontSize = 13.0f;
        FontWeight fontWeight = FontWeight::Normal;

        // Dimensions
        float itemHeight = 28.0f;
        float iconSize = 16.0f;
        float paddingLeft = 8.0f;
        float paddingRight = 8.0f;
        float paddingTop = 4.0f;
        float paddingBottom = 4.0f;
        float iconSpacing = 6.0f;
        float shortcutSpacing = 20.0f;
        float separatorHeight = 8.0f;
        float borderWidth = 1.0f;
        float borderRadius = 4.0f;

        // Submenu
        float submenuDelay = 300.0f;  // milliseconds
        float submenuOffset = 2.0f;

        // Animation
        bool enableAnimations = true;
        float animationDuration = 0.15f;

        // Shadow
        bool showShadow = true;
        Color shadowColor = Color(0, 0, 0, 100);
        Point2D shadowOffset = Point2D(2, 2);
        float shadowBlur = 4.0f;

        static MenuStyle Default();
        static MenuStyle Dark();
        static MenuStyle Flat();
    };

// ===== MAIN MENU CLASS =====
    class UltraCanvasMenu : public UltraCanvasElement, public std::enable_shared_from_this<UltraCanvasMenu> {
    private:
        // Menu properties
        MenuType menuType = MenuType::ContextMenu;
        MenuOrientation orientation = MenuOrientation::Vertical;
        MenuState currentState = MenuState::Hidden;
        MenuStyle style;

        // Menu items
        std::vector<MenuItemData> items;

        // Navigation state
        int hoveredIndex = -1;
        int selectedIndex = -1;
        int keyboardIndex = -1;
        bool keyboardNavigation = false;
        bool needCalculateSize = true;

        // Submenu management
        std::shared_ptr<UltraCanvasMenu> activeSubmenu;
        std::weak_ptr<UltraCanvasMenu> parentMenu;
        std::vector<std::shared_ptr<UltraCanvasMenu>> childMenus;

        // Animation
        std::chrono::steady_clock::time_point animationStartTime;
        float animationProgress = 0.0f;

        // Events
        std::function<void()> onMenuOpened;
        std::function<void()> onMenuClosed;
        std::function<void(int)> onItemSelected;
        std::function<void(int)> onItemHovered;

    public:
        // ===== CONSTRUCTORS =====
        UltraCanvasMenu(const std::string& identifier, long id, long x, long y, long w, long h)
                : UltraCanvasElement(identifier, id, x, y, w, h) {
            style = MenuStyle::Default();
        }

        virtual ~UltraCanvasMenu() {
            CloseAllSubmenus();
        }

        // ===== CORE RENDERING =====
        void Render() override {
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

        bool OnEvent(const UCEvent& event) override {
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
                UltraCanvasElement::OnEvent(event);
            }
        }

        // ===== EVENT HANDLING =====
        bool HandleEvent(const UCEvent& event) {
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
                    return true;

                case UCEventType::MouseDown:
                    HandleMouseDown(event);
                    return true;

                case UCEventType::MouseUp:
                    HandleMouseUp(event);
                    return true;

                case UCEventType::KeyDown:
                    return HandleKeyDown(event);

                case UCEventType::MouseLeave:
                    hoveredIndex = -1;
                    return true;

                default:
                    break;
            }

            return false;
        }

        // ===== MENU TYPE AND CONFIGURATION =====
        void SetMenuType(MenuType type) {
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

        MenuType GetMenuType() const { return menuType; }

        void SetOrientation(MenuOrientation orient) {
            orientation = orient;
            needCalculateSize = true;
        }

        MenuOrientation GetOrientation() const { return orientation; }

        void SetStyle(const MenuStyle& menuStyle) {
            style = menuStyle;
            needCalculateSize = true;
        }

        const MenuStyle& GetStyle() const { return style; }

        // ===== ITEM MANAGEMENT =====
        void AddItem(const MenuItemData& item) {
            items.push_back(item);
            needCalculateSize = true;
        }

        void InsertItem(int index, const MenuItemData& item) {
            if (index >= 0 && index <= static_cast<int>(items.size())) {
                items.insert(items.begin() + index, item);
                needCalculateSize = true;
            }
        }

        void RemoveItem(int index) {
            if (index >= 0 && index < static_cast<int>(items.size())) {
                items.erase(items.begin() + index);
                needCalculateSize = true;
            }
        }

        void UpdateItem(int index, const MenuItemData& item) {
            if (index >= 0 && index < static_cast<int>(items.size())) {
                items[index] = item;
                needCalculateSize = true;
            }
        }

        void Clear() {
            items.clear();
            CloseAllSubmenus();
            needCalculateSize = true;
        }

        const std::vector<MenuItemData>& GetItems() const { return items; }

        MenuItemData* GetItem(int index) {
            if (index >= 0 && index < static_cast<int>(items.size())) {
                return &items[index];
            }
            return nullptr;
        }

        void Show() {
            // Ensure menu is always on top when shown
            SetZIndex(UltraCanvas::ZLayers::Menus + 100);

            // Existing Show() implementation continues...
            if (currentState != MenuState::Visible && currentState != MenuState::Opening) {
                if (menuType == MenuType::DropdownMenu || menuType == MenuType::SubmenuMenu) {
                    currentState = MenuState::Visible;
                    SetVisible(true);
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

        void Hide() {
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

                // Close all submenus
                CloseAllSubmenus();

                needCalculateSize = true;
                if (onMenuClosed) onMenuClosed();
                RequestRedraw();

                std::cout << "Menu '" << GetIdentifier() << "' hidden. State: " << (int)currentState
                          << " Visible: " << IsVisible() << std::endl;
            }
        }

        void Toggle() {
            if (IsMenuVisible()) {
                Hide();
            } else {
                Show();
            }
        }

        bool IsMenuVisible() const {
            // FIX: Simplified visibility check
            return currentState == MenuState::Visible || currentState == MenuState::Opening;
        }

        MenuState GetMenuState() const { return currentState; }

        // ===== CONTEXT MENU HELPERS =====
        void ShowAt(const Point2D& position) {
            SetPosition(static_cast<long>(position.x), static_cast<long>(position.y));
            Show();
        }

        void ShowAt(int x, int y) {
            ShowAt(Point2D(static_cast<float>(x), static_cast<float>(y)));
        }

        // ===== SUBMENU MANAGEMENT =====
        void OpenSubmenu(int itemIndex) {
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

            // Add items to submenu
            for (const auto& subItem : item.subItems) {
                activeSubmenu->AddItem(subItem);
            }

            // Position submenu
            PositionSubmenu(activeSubmenu, itemIndex);

            // Show submenu
            activeSubmenu->Show();
            childMenus.push_back(activeSubmenu);
        }

        void CloseActiveSubmenu() {
            if (activeSubmenu) {
                activeSubmenu->Hide();
                activeSubmenu->CloseAllSubmenus();

                // Remove from child menus
                auto it = std::find(childMenus.begin(), childMenus.end(), activeSubmenu);
                if (it != childMenus.end()) {
                    childMenus.erase(it);
                }

                activeSubmenu.reset();
            }
        }

        void CloseAllSubmenus() {
            for (auto& child : childMenus) {
                if (child) {
                    child->Hide();
                    child->CloseAllSubmenus();
                }
            }
            childMenus.clear();
            activeSubmenu.reset();
        }

        // UPDATE: Fix for GetItemX method to properly calculate horizontal positions
        float GetItemX(int index) const {
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

        float GetItemY(int index) const {
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

        bool Contains(float x, float y) const override {
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

        bool Contains(const Point2D& point) const override {
            return Contains(point.x, point.y);
        }


        // ===== EVENT CALLBACKS =====
        void SetOnMenuOpened(std::function<void()> callback) { onMenuOpened = callback; }
        void SetOnMenuClosed(std::function<void()> callback) { onMenuClosed = callback; }
        void SetOnItemSelected(std::function<void(int)> callback) { onItemSelected = callback; }
        void SetOnItemHovered(std::function<void(int)> callback) { onItemHovered = callback; }

    private:
        // ===== CALCULATION METHODS =====
        void CalculateSize() {
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
                    if (item.type != MenuItemType::Separator) {
                        totalWidth += style.iconSpacing; // Add spacing between items
                    }
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

        void RenderItem(int index, const MenuItemData& item) {
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

            float currentX = itemBounds.x + style.paddingLeft;
            float textY = itemBounds.y + (itemBounds.height - style.fontSize) / 2;

            // Render checkbox/radio
            if (item.type == MenuItemType::Checkbox || item.type == MenuItemType::Radio) {
                RenderCheckbox(item, Point2D(currentX, textY));
                currentX += style.iconSize + style.iconSpacing;
            }

            // Render icon
            if (!item.iconPath.empty()) {
                RenderIcon(item.iconPath, Point2D(currentX, textY));
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
                RenderSubmenuArrow(Point2D(itemBounds.x + itemBounds.width - style.paddingRight - 10,
                                           itemBounds.y + itemBounds.height / 2));
            }
        }

        Rect2D GetItemBounds(int index) const {
            Rect2D bounds;

            if (orientation == MenuOrientation::Horizontal) {
                float currentX = GetX();

                for (int i = 0; i < index && i < static_cast<int>(items.size()); ++i) {
                    if (items[i].visible) {
                        currentX += CalculateItemWidth(items[i]) + style.paddingLeft + style.paddingRight + style.iconSpacing;
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

        float CalculateItemWidth(const MenuItemData& item) const {
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

        // ===== POSITIONING =====
        void PositionSubmenu(std::shared_ptr<UltraCanvasMenu> submenu, int itemIndex) {
            if (!submenu) return;

            float itemY = GetItemY(itemIndex);
            float submenuX, submenuY;

            if (orientation == MenuOrientation::Vertical) {
                // Position to the right of the item
                submenuX = GetX() + GetWidth() + style.submenuOffset;
                submenuY = GetY() + itemY;
            } else {
                // Position below the item
                submenuX = GetX() + GetItemX(itemIndex);
                submenuY = GetY() + GetHeight() + style.submenuOffset;
            }

            // Adjust for screen boundaries (simplified)
            // In a complete implementation, you'd check screen bounds

            submenu->SetPosition(static_cast<long>(submenuX), static_cast<long>(submenuY));
        }

        // ===== RENDERING HELPERS =====
        void RenderSeparator(const Rect2D& bounds) {
            float centerY = bounds.y + bounds.height / 2;
            float startX = bounds.x + style.paddingLeft;
            float endX = bounds.x + bounds.width - style.paddingRight;

            SetStrokeColor(style.separatorColor);
            SetStrokeWidth(1.0f);
            DrawLine(Point2D(startX, centerY), Point2D(endX, centerY));
        }

        void RenderCheckbox(const MenuItemData& item, const Point2D& position) {
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

        void RenderSubmenuArrow(const Point2D& position) {
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

        void RenderIcon(const std::string& iconPath, const Point2D& position) {
            // Would implement icon rendering based on file type
            DrawImage(iconPath, Rect2D(position.x, position.y, style.iconSize, style.iconSize));
        }

        void RenderKeyboardHighlight(const Rect2D& bounds) {
            SetStrokeColor(style.selectedColor);
            SetStrokeWidth(2.0f);
            DrawRect(bounds);
        }

        void RenderShadow() {
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

        // ===== UTILITY METHODS =====
        Color GetItemBackgroundColor(int index, const MenuItemData& item) const {
            if (!item.enabled) return Colors::Transparent;

            if (index == hoveredIndex || index == keyboardIndex) {
                return style.hoverColor;
            }

            if (index == selectedIndex) {
                return style.pressedColor;
            }

            return Colors::Transparent;
        }

        int GetItemAtPosition(const Point2D& position) const {
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

        // ===== EVENT HANDLERS =====
        void HandleMouseMove(const UCEvent& event) {
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

        void HandleMouseDown(const UCEvent& event) {
            if (!Contains(event.x, event.y)) {
                // Click outside menu - close if context menu
                if (menuType == MenuType::ContextMenu || menuType == MenuType::PopupMenu) {
                    Hide();
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

        void HandleMouseUp(const UCEvent& event) {
            if (!Contains(event.x, event.y)) return;

            Point2D mousePos(static_cast<float>(event.x), static_cast<float>(event.y));
            int clickedIndex = GetItemAtPosition(mousePos);

            if (clickedIndex >= 0 && clickedIndex < static_cast<int>(items.size()) && clickedIndex == selectedIndex) {
                ExecuteItem(clickedIndex);
            }

            selectedIndex = -1;
            RequestRedraw();
        }

        bool HandleKeyDown(const UCEvent& event) {
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

        // ===== KEYBOARD NAVIGATION =====
        void NavigateUp() {
            if (items.empty()) return;

            do {
                keyboardIndex = (keyboardIndex <= 0) ? static_cast<int>(items.size()) - 1 : keyboardIndex - 1;
            } while (keyboardIndex >= 0 &&
                     (!items[keyboardIndex].visible ||
                      items[keyboardIndex].type == MenuItemType::Separator ||
                      !items[keyboardIndex].enabled));
        }

        void NavigateDown() {
            if (items.empty()) return;

            do {
                keyboardIndex = (keyboardIndex >= static_cast<int>(items.size()) - 1) ? 0 : keyboardIndex + 1;
            } while (keyboardIndex < static_cast<int>(items.size()) &&
                     (!items[keyboardIndex].visible ||
                      items[keyboardIndex].type == MenuItemType::Separator ||
                      !items[keyboardIndex].enabled));
        }

        void NavigateLeft() {
            // For horizontal menus
            NavigateUp();
        }

        void NavigateRight() {
            // For horizontal menus
            NavigateDown();
        }

        void OpenSubmenuFromKeyboard() {
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

        void CloseSubmenu() {
            CloseActiveSubmenu();
            if (auto parent = parentMenu.lock()) {
                parent->keyboardNavigation = true;
            }
        }

        // ===== ITEM EXECUTION =====
        void ExecuteItem(int index) {
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

        // ===== ANIMATION =====
        void StartAnimation() {
            animationStartTime = std::chrono::steady_clock::now();
            animationProgress = 0.0f;
        }

        void UpdateAnimation() {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - animationStartTime);
            float elapsedSeconds = elapsed.count() / 1000.0f;

            animationProgress = std::min(1.0f, elapsedSeconds / style.animationDuration);

            if (animationProgress >= 1.0f) {
                // Animation complete
                if (currentState == MenuState::Opening) {
                    currentState = MenuState::Visible;
                } else if (currentState == MenuState::Closing) {
                    currentState = MenuState::Hidden;
                    SetVisible(false);
                }
            }

            // Apply animation effects (scale, fade, etc.)
            // This would modify the rendering parameters based on animationProgress
        }
    };

// Rest of the file remains the same (factory functions, builder pattern, etc.)
// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasMenu> CreateMenu(
            const std::string& identifier, long id, long x, long y, long w, long h) {
        return UltraCanvasElementFactory::CreateWithID<UltraCanvasMenu>(
                id, identifier, id, x, y, w, h);
    }

    inline std::shared_ptr<UltraCanvasMenu> CreateContextMenu(
            const std::string& identifier, long id, long x, long y) {
        auto menu = CreateMenu(identifier, id, x, y, 150, 100);
        menu->SetMenuType(MenuType::ContextMenu);
        return menu;
    }

    inline std::shared_ptr<UltraCanvasMenu> CreateMainMenu(
            const std::string& identifier, long id, long x, long y, long w) {
        auto menu = CreateMenu(identifier, id, x, y, w, 32);
        menu->SetMenuType(MenuType::MainMenu);
        return menu;
    }

// ===== BUILDER PATTERN =====
    class MenuBuilder {
    private:
        std::shared_ptr<UltraCanvasMenu> menu;

    public:
        MenuBuilder(const std::string& identifier, long id, long x, long y, long w = 150, long h = 100) {
            menu = CreateMenu(identifier, id, x, y, w, h);
        }

        MenuBuilder& SetType(MenuType type) {
            menu->SetMenuType(type);
            return *this;
        }

        MenuBuilder& SetStyle(const MenuStyle& style) {
            menu->SetStyle(style);
            return *this;
        }

        MenuBuilder& AddItem(const MenuItemData& item) {
            menu->AddItem(item);
            return *this;
        }

        MenuBuilder& AddAction(const std::string& label, std::function<void()> callback) {
            menu->AddItem(MenuItemData::Action(label, callback));
            return *this;
        }

        MenuBuilder& AddAction(const std::string& label, const std::string& shortcut, std::function<void()> callback) {
            auto item = MenuItemData::Action(label, callback);
            item.shortcut = shortcut;
            menu->AddItem(item);
            return *this;
        }

        MenuBuilder& AddSeparator() {
            menu->AddItem(MenuItemData::Separator());
            return *this;
        }

        MenuBuilder& AddCheckbox(const std::string& label, bool checked, std::function<void(bool)> callback) {
            menu->AddItem(MenuItemData::Checkbox(label, checked, callback));
            return *this;
        }

        MenuBuilder& AddSubmenu(const std::string& label, const std::vector<MenuItemData>& items) {
            menu->AddItem(MenuItemData::Submenu(label, items));
            return *this;
        }

        std::shared_ptr<UltraCanvasMenu> Build() {
            return menu;
        }
    };

// ===== STYLE FACTORY IMPLEMENTATIONS =====
    inline MenuStyle MenuStyle::Default() {
        MenuStyle style;
        style.backgroundColor = Color(248, 248, 248, 255);
        style.borderColor = Color(200, 200, 200, 255);
        style.textColor = Color(0, 0, 0, 255);
        style.hoverColor = Color(225, 240, 255, 255);
        style.hoverTextColor = Color(0, 0, 0, 255);
        style.pressedColor = Color(200, 220, 240, 255);
        style.disabledTextColor = Color(150, 150, 150, 255);
        style.shortcutColor = Color(100, 100, 100, 255);
        style.separatorColor = Color(220, 220, 220, 255);

        // FIXED: Proper default height for menu items
        style.itemHeight = 24.0f;  // Reduced from whatever was causing 44px
        style.paddingTop = 4.0f;
        style.paddingBottom = 4.0f;
        style.paddingLeft = 8.0f;
        style.paddingRight = 8.0f;

        style.iconSize = 16.0f;
        style.iconSpacing = 8.0f;
        style.shortcutSpacing = 20.0f;
        style.submenuOffset = 2.0f;
        style.separatorHeight = 1.0f;
        style.borderWidth = 1.0f;
        style.borderRadius = 0.0f;
        style.fontSize = 14.0f;

        style.showShadow = false;
        style.enableAnimations = false;
        style.animationDuration = 0.2f;

        return style;
    }

    inline MenuStyle MenuStyle::Dark() {
        MenuStyle style;
        style.backgroundColor = Color(45, 45, 45);
        style.textColor = Colors::White;
        style.hoverColor = Color(65, 65, 65);
        return style;
    }

    inline MenuStyle MenuStyle::Flat() {
        MenuStyle style;
        style.backgroundColor = Colors::White;
        style.borderWidth = 0.0f;
        style.borderRadius = 0.0f;
        style.showShadow = false;
        style.textColor = Colors::Black;
        style.hoverColor = Color(240, 240, 240);
        return style;
    }

// ===== MENU ITEM FACTORY IMPLEMENTATIONS =====
    inline MenuItemData MenuItemData::Action(const std::string& label, std::function<void()> callback) {
        MenuItemData item;
        item.type = MenuItemType::Action;
        item.label = label;
        item.onClick = callback;
        return item;
    }

    inline MenuItemData MenuItemData::Separator() {
        MenuItemData item;
        item.type = MenuItemType::Separator;
        return item;
    }

    inline MenuItemData MenuItemData::Checkbox(const std::string& label, bool checked, std::function<void(bool)> callback) {
        MenuItemData item;
        item.type = MenuItemType::Checkbox;
        item.label = label;
        item.checked = checked;
        item.onToggle = callback;
        return item;
    }

    inline MenuItemData MenuItemData::Radio(const std::string& label, int group, std::function<void()> callback) {
        MenuItemData item;
        item.type = MenuItemType::Radio;
        item.label = label;
        item.radioGroup = group;
        item.onClick = callback;
        return item;
    }

    inline MenuItemData MenuItemData::Submenu(const std::string& label, const std::vector<MenuItemData>& items) {
        MenuItemData item;
        item.type = MenuItemType::Submenu;
        item.label = label;
        item.subItems = items;
        return item;
    }

    inline MenuItemData MenuItemData::Input(const std::string& label, const std::string& placeholder, std::function<void(const std::string&)> callback) {
        MenuItemData item;
        item.type = MenuItemType::Input;
        item.label = label;
        item.onTextInput = callback;
        return item;
    }

// ===== CONVENIENCE FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasMenu> CreateFileMenu() {
        return MenuBuilder("file_menu", 2001, 0, 0)
                .SetType(MenuType::DropdownMenu)
                .AddAction("New", "Ctrl+N", []() { /* New file */ })
                .AddAction("Open", "Ctrl+O", []() { /* Open file */ })
                .AddSeparator()
                .AddAction("Save", "Ctrl+S", []() { /* Save file */ })
                .AddAction("Save As", "Ctrl+Shift+S", []() { /* Save as */ })
                .AddSeparator()
                .AddAction("Exit", "Alt+F4", []() { /* Exit application */ })
                .Build();
    }

    inline std::shared_ptr<UltraCanvasMenu> CreateEditMenu() {
        return MenuBuilder("edit_menu", 2002, 0, 0)
                .SetType(MenuType::DropdownMenu)
                .AddAction("Undo", "Ctrl+Z", []() { /* Undo */ })
                .AddAction("Redo", "Ctrl+Y", []() { /* Redo */ })
                .AddSeparator()
                .AddAction("Cut", "Ctrl+X", []() { /* Cut */ })
                .AddAction("Copy", "Ctrl+C", []() { /* Copy */ })
                .AddAction("Paste", "Ctrl+V", []() { /* Paste */ })
                .AddSeparator()
                .AddAction("Select All", "Ctrl+A", []() { /* Select all */ })
                .Build();
    }

} // namespace UltraCanvas
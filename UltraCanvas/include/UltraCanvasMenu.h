// include/UltraCanvasMenu.h
// Interactive menu component with styling options and submenu support
// Version: 1.2.5
// Last Modified: 2025-01-08
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasKeyboardManager.h"
//#include "UltraCanvasZOrderManager.h"
#include "UltraCanvasRenderContext.h"
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <chrono>
#include <algorithm>

namespace UltraCanvas {

// ===== MENU TYPES AND ENUMS =====
    enum class MenuType {
        Menubar,
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
        MenuItemData(const std::string& itemLabel, const std::string& itemShortcut, std::function<void()> callback)
                : label(itemLabel), shortcut(itemShortcut), onClick(callback) {}

        // Factory methods
        static MenuItemData Action(const std::string& label, std::function<void()> callback);
        static MenuItemData Action(const std::string& label, const std::string& iconPath, std::function<void()> callback);
        static MenuItemData ActionWithShortcut(const std::string& label, const std::string& itemShortcut, const std::string& iconPath, std::function<void()> callback);
        static MenuItemData ActionWithShortcut(const std::string& label, const std::string& itemShortcut, std::function<void()> callback);
        static MenuItemData Separator();
        static MenuItemData Checkbox(const std::string& label, bool checked, std::function<void(bool)> callback);
        static MenuItemData Radio(const std::string& label, int group, bool checked, std::function<void(bool)> callback);
        static MenuItemData Submenu(const std::string& label, const std::vector<MenuItemData>& items);
        static MenuItemData Submenu(const std::string& label, const std::string& iconPath, const std::vector<MenuItemData>& items);
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
        std::string fontFamily = "Sans";
        float fontSize = 12.0f;
        FontWeight fontWeight = FontWeight::Normal;

        // Dimensions
        int itemHeight = 28;
        int iconSize = 16;
        int paddingLeft = 4;
        int paddingRight = 4;
        int paddingTop = 4;
        int paddingBottom = 4;
        int iconSpacing = 6;
        int shortcutSpacing = 20;
        int separatorHeight = 8;
        int borderWidth = 1;
        int borderRadius = 4;

        // Submenu
        int submenuDelay = 300;  // milliseconds
        int submenuOffset = 2;

        // Animation
        bool enableAnimations = true;
        float animationDuration = 0.15f;

        // Shadow
        bool showShadow = true;
        Color shadowColor = Color(0, 0, 0, 100);
        Point2Di shadowOffset = Point2Di(2, 2);
        int shadowBlur = 4;

        static MenuStyle Default();
        static MenuStyle Dark();
        static MenuStyle Flat();
    };

// ===== MAIN MENU CLASS =====
    class UltraCanvasMenu : public UltraCanvasUIElement {
    private:
        // Menu properties
        MenuType menuType = MenuType::PopupMenu;
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
        int parentItemIndex = -1;  // Index of the parent item that opened this submenu
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
                : UltraCanvasUIElement(identifier, id, x, y, w, h) {
            style = MenuStyle::Default();
        }

        virtual ~UltraCanvasMenu() {
            CloseAllSubmenus();
        }

        // ===== CORE RENDERING =====
        void Render(IRenderContext* ctx) override;
        void RenderPopupContent(IRenderContext* ctx) override;

        bool OnEvent(const UCEvent& event) override;

        // ===== EVENT HANDLING =====
        bool HandleEvent(const UCEvent& event);

        // ===== MENU TYPE AND CONFIGURATION =====
        void SetMenuType(MenuType type);
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

        void Show();
        void Hide();

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
        void ShowAt(const Point2Di& position) {
            ShowAt(position.x, position.y);
        }

        void ShowAt(int x, int y) {
            SetPosition(x, y);
            Show();
        }

        // ===== SUBMENU MANAGEMENT =====
        void OpenSubmenu(int itemIndex);
        void CloseActiveSubmenu();
        void CloseAllSubmenus();
        void CloseMenutree();

        // UPDATE: Fix for GetItemX method to properly calculate horizontal positions
        int GetItemX(int index) const;
        int GetItemY(int index) const;

        bool Contains(int x, int y) override;

        // ===== EVENT CALLBACKS =====
        void OnMenuOpened(std::function<void()> callback) { onMenuOpened = callback; }
        void OnMenuClosed(std::function<void()> callback) { onMenuClosed = callback; }
        void OnItemSelected(std::function<void(int)> callback) { onItemSelected = callback; }
        void OnItemHovered(std::function<void(int)> callback) { onItemHovered = callback; }

    private:
        // ===== CALCULATION METHODS =====
        void CalculateAndUpdateSize(IRenderContext* ctx);

        Rect2Di GetItemBounds(int index) const;

        int CalculateItemWidth(const MenuItemData& item) const;


        // ===== POSITIONING =====
        void PositionSubmenu(std::shared_ptr<UltraCanvasMenu> submenu, int itemIndex);

        // ===== RENDERING HELPERS =====
        void RenderItem(int index, const MenuItemData& item, IRenderContext* ctx);
        void RenderSeparator(const Rect2Di& bounds, IRenderContext* ctx);
        void RenderCheckbox(const MenuItemData& item, const Point2Di& position, IRenderContext* ctx);
        void RenderSubmenuArrow(const Point2Di& position, IRenderContext* ctx);
        void RenderIcon(const std::string& iconPath, const Point2Di& position, IRenderContext* ctx);
        void RenderKeyboardHighlight(const Rect2Di& bounds, IRenderContext* ctx);
        void RenderShadow(IRenderContext* ctx);

        // ===== UTILITY METHODS =====
        Color GetItemBackgroundColor(int index, const MenuItemData& item) const;

        int GetItemAtPosition(int x, int y) const;

        // ===== EVENT HANDLERS =====
        bool HandleMouseMove(const UCEvent& event);
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);
        bool HandleKeyDown(const UCEvent& event);

        // ===== KEYBOARD NAVIGATION =====
        void NavigateUp();
        void NavigateDown();
        void NavigateLeft() {
            // For horizontal menus
            NavigateUp();
        }
        void NavigateRight() {
            // For horizontal menus
            NavigateDown();
        }

        void OpenSubmenuFromKeyboard();

        void CloseSubmenu();

        // ===== ITEM EXECUTION =====
        void ExecuteItem(int index);

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
        return UltraCanvasUIElementFactory::Create<UltraCanvasMenu>(
                identifier, id, x, y, w, h);
    }

    inline std::shared_ptr<UltraCanvasMenu> CreateMenuBar(
            const std::string& identifier, long id, long x, long y, long w) {
        auto menu = CreateMenu(identifier, id, x, y, w, 32);
        menu->SetMenuType(MenuType::Menubar);
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
        style.itemHeight = 24;  // Reduced from whatever was causing 44px
        style.paddingTop = 4;
        style.paddingBottom = 4;
        style.paddingLeft = 8;
        style.paddingRight = 8;

        style.iconSize = 16;
        style.iconSpacing = 6;
        style.shortcutSpacing = 20;
        style.submenuOffset = 2;
        style.separatorHeight = 1;
        style.borderWidth = 1;
        style.borderRadius = 0;
        style.fontSize = 12.0f;

        style.showShadow = false;
        style.enableAnimations = false;
        style.animationDuration = 0.2f;

        return style;
    }

    inline MenuStyle MenuStyle::Dark() {
        MenuStyle style;
        style.backgroundColor = Color(45, 45, 45);
        style.textColor = Colors::White;
        style.hoverTextColor = Colors::White;
        style.hoverColor = Color(85, 85, 85);
        return style;
    }

    inline MenuStyle MenuStyle::Flat() {
        MenuStyle style;
        style.backgroundColor = Colors::White;
        style.borderWidth = 0;
        style.borderRadius = 0;
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

    inline MenuItemData MenuItemData::Action(const std::string& label, const std::string& iconPath, std::function<void()> callback) {
        MenuItemData item;
        item.type = MenuItemType::Action;
        item.label = label;
        item.iconPath = iconPath;
        item.onClick = callback;
        return item;
    }

    inline MenuItemData MenuItemData::ActionWithShortcut(const std::string& label, const std::string& shortcut, std::function<void()> callback) {
        MenuItemData item;
        item.type = MenuItemType::Action;
        item.label = label;
        item.shortcut = shortcut;
        item.onClick = callback;
        return item;
    }

    inline MenuItemData MenuItemData::ActionWithShortcut(const std::string& label, const std::string& shortcut, const std::string& iconPath, std::function<void()> callback) {
        MenuItemData item;
        item.type = MenuItemType::Action;
        item.label = label;
        item.shortcut = shortcut;
        item.iconPath = iconPath;
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

    inline MenuItemData MenuItemData::Radio(const std::string& label, int group, bool checked, std::function<void(bool)> callback) {
        MenuItemData item;
        item.type = MenuItemType::Radio;
        item.label = label;
        item.checked = checked;
        item.radioGroup = group;
        item.onToggle = callback;
        return item;
    }

    inline MenuItemData MenuItemData::Submenu(const std::string& label, const std::vector<MenuItemData>& items) {
        MenuItemData item;
        item.type = MenuItemType::Submenu;
        item.label = label;
        item.subItems = items;
        return item;
    }

    inline MenuItemData MenuItemData::Submenu(const std::string& label, const std::string& iconPath, const std::vector<MenuItemData>& items) {
        MenuItemData item;
        item.type = MenuItemType::Submenu;
        item.label = label;
        item.iconPath = iconPath;
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

} // namespace UltraCanvas
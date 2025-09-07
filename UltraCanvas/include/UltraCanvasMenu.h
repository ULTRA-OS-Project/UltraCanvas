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
//#include "UltraCanvasZOrderManager.h"
#include "UltraCanvasRenderInterface.h"
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
        static MenuItemData Action(const std::string& label, const std::string& iconPath, std::function<void()> callback);
        static MenuItemData Separator();
        static MenuItemData Checkbox(const std::string& label, bool checked, std::function<void(bool)> callback);
        static MenuItemData Radio(const std::string& label, int group, std::function<void()> callback);
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
        std::string fontFamily = "Arial";
        float fontSize = 13.0f;
        FontWeight fontWeight = FontWeight::Normal;

        // Dimensions
        float itemHeight = 28.0f;
        float iconSize = 16.0f;
        float paddingLeft = 4.0f;
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
        Point2Di shadowOffset = Point2Di(2, 2);
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
        void Render() override;

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
            SetPosition(static_cast<long>(position.x), static_cast<long>(position.y));
            Show();
        }

        void ShowAt(int x, int y) {
            ShowAt(Point2Di(static_cast<float>(x), static_cast<float>(y)));
        }

        // ===== SUBMENU MANAGEMENT =====
        void OpenSubmenu(int itemIndex);

        void CloseActiveSubmenu();

        void CloseAllSubmenus();

        // UPDATE: Fix for GetItemX method to properly calculate horizontal positions
        float GetItemX(int index) const;

        float GetItemY(int index) const;

        bool Contains(float x, float y) override;

        bool Contains(const Point2Di& point)  override {
            return Contains(point.x, point.y);
        }

        // ===== EVENT CALLBACKS =====
        void SetOnMenuOpened(std::function<void()> callback) { onMenuOpened = callback; }
        void SetOnMenuClosed(std::function<void()> callback) { onMenuClosed = callback; }
        void SetOnItemSelected(std::function<void(int)> callback) { onItemSelected = callback; }
        void SetOnItemHovered(std::function<void(int)> callback) { onItemHovered = callback; }

    private:
        // ===== CALCULATION METHODS =====
        void CalculateSize();

        void RenderItem(int index, const MenuItemData& item);

        Rect2Di GetItemBounds(int index) const;

        float CalculateItemWidth(const MenuItemData& item) const;

        // ===== POSITIONING =====
        void PositionSubmenu(std::shared_ptr<UltraCanvasMenu> submenu, int itemIndex);

        // ===== RENDERING HELPERS =====
        void RenderSeparator(const Rect2Di& bounds);

        void RenderCheckbox(const MenuItemData& item, const Point2Di& position);

        void RenderSubmenuArrow(const Point2Di& position);

        void RenderIcon(const std::string& iconPath, const Point2Di& position);

        void RenderKeyboardHighlight(const Rect2Di& bounds);

        void RenderShadow();

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

        int GetItemAtPosition(const Point2Di& position) const;

        // ===== EVENT HANDLERS =====
        void HandleMouseMove(const UCEvent& event);

        void HandleMouseDown(const UCEvent& event);

        void HandleMouseUp(const UCEvent& event);

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
        style.iconSpacing = 6.0f;
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

    inline MenuItemData MenuItemData::Action(const std::string& label_, const std::string& iconPath_, std::function<void()> callback_) {
        MenuItemData item;
        item.type = MenuItemType::Action;
        item.label = label_;
        item.iconPath = iconPath_;
        item.onClick = callback_;
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
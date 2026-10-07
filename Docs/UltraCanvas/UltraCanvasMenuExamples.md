# UltraCanvasMenu Documentation

<!-- doc-check: void OpenWithDefaultApplication(); void onToggleStandardToolbar(bool); void onToggleFormattingToolbar(bool); void onToggleDrawingToolbar(bool); -->

## Overview

**UltraCanvasMenu** is a comprehensive menu component in the UltraCanvas framework that provides flexible menu functionality including menu bars, popup context menus, and hierarchical submenu support. It offers rich styling options, keyboard navigation, scrolling for long menus, and cross-platform compatibility.

**Version:** 1.2.6  
**Last Modified:** 2026-10-07  
**Author:** UltraCanvas Framework

## Features

- **Multiple Menu Types**: Support for menu bars, popup menus, and submenus
- **Flexible Orientation**: Vertical and horizontal layout options
- **Rich Item Types**: Actions, separators, section headers, checkboxes, radio buttons and submenus (static or built on demand)
- **Keyboard Navigation**: Full keyboard support with arrow keys, Enter, Escape
- **Theming**: Pre-built themes (Default, Dark, Flat) with extensive customization
- **Icons & Shortcuts**: Support for item icons, keyboard shortcuts and tooltips
- **Event System**: Callbacks for menu open/close, item selection and hover
- **Submenu Management**: Automatic positioning and cascade control
- **Long Menus**: A menu taller than the window gets a scrollbar

## Class Structure

### Main Classes

```cpp
namespace UltraCanvas {
    class UltraCanvasMenu;   // : public UltraCanvasUIElement
    struct MenuItemData;
    struct MenuStyle;
    class MenuBuilder;
}
```

## Menu Types

### MenuType Enumeration

```cpp
enum class MenuType {
    Menubar,        // Horizontal menu bar (typically at top of window)
    PopupMenu,      // Context/popup menu
    SubmenuMenu     // Cascading submenu
};
```

### MenuOrientation

```cpp
enum class MenuOrientation {
    Vertical,       // Traditional dropdown menu layout
    Horizontal      // Menu bar style layout
};
```

## Menu Items

### MenuItemType

```cpp
enum class MenuItemType {
    Action,         // Clickable menu item
    Separator,      // Visual separator line
    Checkbox,       // Toggle item with checkbox
    Radio,          // Radio button (mutually exclusive within group)
    Submenu,        // Item with cascading submenu
    Input,          // Reserved: no text-input item is implemented yet
    Custom,         // Reserved: drawn like a plain item
    Header          // Non-clickable section title
};
```

### MenuItemData Structure

```cpp
struct MenuItemData {
    // Core properties
    MenuItemType type = MenuItemType::Action;
    std::string label;              // Display text
    std::string shortcut;           // Keyboard shortcut text (e.g., "Ctrl+C")
    std::string iconPath;           // Path to icon image
    std::shared_ptr<UCImage> iconImage;  // Already-decoded icon; drawn instead of iconPath
    std::string commandId;          // Stable id (e.g. "file.open") for saved menu layouts

    // State
    bool enabled = true;            // Item can be interacted with
    bool visible = true;            // Item is visible
    bool checked = false;           // For checkbox/radio items
    int radioGroup = 0;             // Radio button group ID

    // Callbacks
    std::function<void()> onClick;          // Action and radio items (and a clickable submenu entry)
    std::function<void(bool)> onToggle;     // Checkbox items

    // Submenu: static items, or a provider called each time the submenu opens
    std::vector<MenuItemData> subItems;
    std::function<std::vector<MenuItemData>()> subItemsProvider;

    // Per-item font override (uses the menu's font if nullopt)
    std::optional<FontStyle> font;

    // Custom data
    void* userData = nullptr;         // User-defined data

    std::string tooltip;              // Hover hint (empty = no tooltip)
    EllipsizeMode ellipsize = EllipsizeMode::EllipsizeMiddle;  // How a too-long label is shortened
    int submenuMaxWidth = 0;          // Max width of the child menu (0 = MenuStyle::maxWidth)

    // Constructors
    MenuItemData() = default;
    MenuItemData(const std::string& itemLabel);
    MenuItemData(const std::string& itemLabel, std::function<void()> callback);
    MenuItemData(const std::string& itemLabel, const std::string& itemShortcut, std::function<void()> callback);

    bool HasSubmenu() const;
};
```

### Factory Methods for Menu Items

All are static members of `MenuItemData`. Each also has an overload that
takes a `const FontStyle& font` just before the callback (or the item list),
to give that item its own font.

```cpp
struct MenuItemData {
    // Create action item
    static MenuItemData Action(const std::string& label, std::function<void()> callback);
    static MenuItemData Action(const std::string& label, const std::string& iconPath, std::function<void()> callback);
    static MenuItemData ActionWithShortcut(const std::string& label, const std::string& itemShortcut, std::function<void()> callback);
    static MenuItemData ActionWithShortcut(const std::string& label, const std::string& itemShortcut, const std::string& iconPath, std::function<void()> callback);

    // Create separator
    static MenuItemData Separator();

    // Create a non-clickable section title
    static MenuItemData Header(const std::string& label);

    // Create checkbox: the callback receives the new checked state
    static MenuItemData Checkbox(const std::string& label, bool checked, std::function<void(bool)> callback);

    // Create radio button: the callback runs when the item is chosen
    static MenuItemData Radio(const std::string& label, int group, bool checked, std::function<void()> callback);

    // Create submenu
    static MenuItemData Submenu(const std::string& label, const std::vector<MenuItemData>& items);
    static MenuItemData Submenu(const std::string& label, const std::string& iconPath, const std::vector<MenuItemData>& items);

    // Create submenu whose items are built each time it opens
    static MenuItemData Submenu(const std::string& label, std::function<std::vector<MenuItemData>()> provider);
    static MenuItemData Submenu(const std::string& label, const std::string& iconPath, std::function<std::vector<MenuItemData>()> provider);
};
```

A submenu whose parent entry is itself clickable: hovering opens the child
list as always, activating the entry runs `onClick` and closes the menu.
(The Filer's "Open with" opens the default application this way.)

```cpp
MenuItemData openWith = MenuItemData::Submenu("Open with", appItems);
openWith.onClick = [] { OpenWithDefaultApplication(); };
```

`MenuItemData::Input()` is declared in the header, but its definition is
commented out, so a call does not link: there is no text-input menu item yet.

## Menu Styling

### MenuStyle Structure

```cpp
struct MenuStyle {
    // Colors
    Color backgroundColor;      // Menu background
    Color borderColor;         // Border color
    Color hoverColor;          // Hover highlight
    Color hoverTextColor;      // Text color when hovered
    Color pressedColor;        // Pressed state color
    Color selectedColor;       // Selected item background
    Color separatorColor;      // Separator line color
    Color textColor;          // Default text color
    Color shortcutColor;      // Shortcut text color
    Color disabledTextColor;  // Disabled item text
    Color headerTextColor;    // Header item text

    // Typography
    FontStyle font;           // Font family, size and weight

    // Dimensions
    int itemHeight;          // Height of menu items
    int iconSize;            // Icon dimensions
    int paddingLeft;         // Left padding
    int paddingRight;        // Right padding
    int paddingTop;          // Top padding
    int paddingBottom;       // Bottom padding
    int iconSpacing;         // Space between icon and text
    int shortcutSpacing;     // Space before shortcut text
    int separatorHeight;     // Height of a separator row; the 1px line is centred in it
    int borderWidth;         // Border thickness
    int borderRadius;        // Corner radius
    int minWidth;            // Minimum menu width (0 = no minimum)
    int maxWidth;            // Maximum menu width (0 = none; labels ellipsize beyond it)
    MenuRadioShape radioShape;  // Outline of a Radio item's indicator: Round (default) or Square

    // Submenu
    int submenuDelay;        // Hover delay before opening (ms)

    // Animation
    bool enableAnimations;   // Enable open/close animations
    float animationDuration; // Animation duration (seconds)
    
    // Shadow
    bool showShadow;         // Display drop shadow
    Color shadowColor;       // Shadow color
    Point2Di shadowOffset;   // Shadow offset
    int shadowBlur;          // Shadow blur radius

    // Scrollbar (for menus taller than the window)
    ScrollbarStyle scrollbarStyle;
};
```

### Pre-built Themes

All are static members of `MenuStyle`:

```cpp
struct MenuStyle {
    // Light theme with subtle styling
    static MenuStyle Default();

    // Dark theme for dark interfaces
    static MenuStyle Dark();

    // Minimal flat design
    static MenuStyle Flat();
};
```

`Dark()` and `Flat()` are `Default()` with colours (and, for `Flat()`, the
border, corner radius and shadow) replaced — every metric is shared, so a menu
keeps its shape when it changes theme. Build your own theme the same way:

```cpp
MenuStyle style = MenuStyle::Default();
style.backgroundColor = Color(30, 34, 40, 255);
style.textColor = Colors::White;
menu->SetStyle(style);
```

## Core Methods

### Construction and Initialization

```cpp
// Create menu
UltraCanvasMenu(const std::string& identifier, float x, float y, float w, float h);
UltraCanvasMenu(const std::string& identifier, float w, float h);   // position left to the layout
explicit UltraCanvasMenu(const std::string& identifier);            // position and size left to the layout

// Factory functions
std::shared_ptr<UltraCanvasMenu> CreateMenu(const std::string& identifier, float x, float y, float w, float h);
std::shared_ptr<UltraCanvasMenu> CreateMenuBar(const std::string& identifier, float x, float y, float w);  // 32 px high
std::shared_ptr<UltraCanvasMenu> CreateMenuBar(const std::string& identifier);  // placed by the layout

// Configuration
void SetMenuType(MenuType type);
MenuType GetMenuType() const;
void SetOrientation(MenuOrientation orient);
MenuOrientation GetOrientation() const;
void SetStyle(const MenuStyle& menuStyle);
const MenuStyle& GetStyle() const;
```

### Item Management

```cpp
// Add items
void AddItem(const MenuItemData& item);
void InsertItem(int index, const MenuItemData& item);

// Modify items
void UpdateItem(int index, const MenuItemData& item);
void RemoveItem(int index);
void Clear();

// Access items
std::vector<MenuItemData>& GetItems();
MenuItemData* GetItem(int index);
```

### Display Control

A popup menu is opened in a window at a window-relative position, and closed
again with `CloseMenu()`:

```cpp
// Open as a popup; pos is window-relative (e.g. UCEvent::pointerWindow)
void OpenMenu(const Point2Di& pos, UltraCanvasWindowBase& window, const PopupElementSettings& settings);

// Close the popup
void CloseMenu();
```

```cpp
struct PopupElementSettings {
    bool closeByEscapeKey = true;      // Escape closes the menu
    bool closeByClickOutside = true;   // a click outside closes the menu
    std::weak_ptr<UltraCanvasUIElement> popupOwner;
};
```

Opening runs `onMenuOpened`, closing runs `onMenuClosed`.

### Submenu Management

```cpp
void OpenSubmenu(int itemIndex);
void CloseActiveSubmenu();
void CloseAllSubmenus();
void CloseMenutree();
```

A submenu entry with `enabled = false` is drawn greyed out and does not open
its submenu — not on hover, not on click, not from the keyboard.

### Event Callbacks

Public callback members; assign a function to each:

```cpp
std::function<void()> onMenuOpened;
std::function<void()> onMenuClosed;
std::function<void(int)> onItemSelected;   // index of the executed item
std::function<void(int)> onItemHovered;    // index of the item under the pointer
```

```cpp
menu->onItemSelected = [](int index) {
    std::cerr << "Item " << index << " selected" << std::endl;
};
```

## Menu Builder Pattern

The MenuBuilder class provides a fluent interface for constructing menus:

```cpp
auto menu = MenuBuilder("FileMenu", 0, 0)
    .SetType(MenuType::PopupMenu)
    .SetStyle(MenuStyle::Dark())
    .AddAction("New", "Ctrl+N", []() { /* handler */ })
    .AddAction("Open", "Ctrl+O", []() { /* handler */ })
    .AddSeparator()
    .AddCheckbox("Auto Save", true, [](bool checked) { /* handler */ })
    .AddSubmenu("Recent Files", {
        MenuItemData::Action("file1.txt", []() { /* handler */ }),
        MenuItemData::Action("file2.txt", []() { /* handler */ })
    })
    .Build();
```

```cpp
MenuBuilder(const std::string& identifier, float x, float y, float w = 150, float h = 100);
```

The builder also has `AddItem()`, `AddHeader()`, and an `AddSubmenu()` that
takes an item provider.

## Event Handling

The menu system handles the following events:

- **Mouse Events**: MouseMove, MouseDown, MouseUp, MouseLeave
- **Mouse Wheel**: Scrolls a menu that is taller than the window
- **Keyboard Events**: Arrow keys for navigation, Enter/Space for selection, Escape to close

### Keyboard Navigation

- **Up/Down Arrows**: Navigate vertical menus
- **Left/Right Arrows**: Navigate horizontal menus or open/close submenus
- **Enter/Space**: Execute selected item
- **Escape**: Close menu (when `PopupElementSettings::closeByEscapeKey` is set, the default)

## Usage Examples

### Creating a Menu Bar

```cpp
// Create menu bar
auto menuBar = CreateMenuBar("MainMenuBar", 0, 0, windowWidth);

// Add File menu
menuBar->AddItem(MenuItemData::Submenu("File", {
    MenuItemData::ActionWithShortcut("New", "Ctrl+N", onNew),
    MenuItemData::ActionWithShortcut("Open", "Ctrl+O", onOpen),
    MenuItemData::ActionWithShortcut("Save", "Ctrl+S", onSave),
    MenuItemData::Separator(),
    MenuItemData::Action("Exit", onExit)
}));

// Add Edit menu
menuBar->AddItem(MenuItemData::Submenu("Edit", {
    MenuItemData::ActionWithShortcut("Undo", "Ctrl+Z", onUndo),
    MenuItemData::ActionWithShortcut("Redo", "Ctrl+Y", onRedo),
    MenuItemData::Separator(),
    MenuItemData::ActionWithShortcut("Cut", "Ctrl+X", onCut),
    MenuItemData::ActionWithShortcut("Copy", "Ctrl+C", onCopy),
    MenuItemData::ActionWithShortcut("Paste", "Ctrl+V", onPaste)
}));
```

### Creating a Context Menu

```cpp
// Create context menu
auto contextMenu = std::make_shared<UltraCanvasMenu>("ContextMenu", 0, 0, 200, 0);
contextMenu->SetMenuType(MenuType::PopupMenu);

// Add items with shortcuts
contextMenu->AddItem(MenuItemData::ActionWithShortcut("📋 Copy", "Ctrl+C", []() {
    std::cerr << "Copy action executed" << std::endl;
}));

contextMenu->AddItem(MenuItemData::ActionWithShortcut("✂️ Cut", "Ctrl+X", []() {
    std::cerr << "Cut action executed" << std::endl;
}));

contextMenu->AddItem(MenuItemData::ActionWithShortcut("📄 Paste", "Ctrl+V", []() {
    std::cerr << "Paste action executed" << std::endl;
}));

contextMenu->AddItem(MenuItemData::Separator());

// Add checkbox item
contextMenu->AddItem(MenuItemData::Checkbox("Show Grid", true, [](bool checked) {
    std::cerr << "Grid visibility: " << checked << std::endl;
}));

// Show on right-click
element->SetEventCallback([contextMenu, owner = element.get()](const UCEvent& event) {
    if (event.type == UCEventType::MouseDown && event.button == UCMouseButton::Right) {
        if (auto* win = owner->GetWindow()) {
            contextMenu->OpenMenu(event.pointerWindow, *win, PopupElementSettings());
            return true;
        }
    }
    return false;
});
```

`SetEventCallback()` is run by the base `UltraCanvasUIElement::OnEvent()`; in a
widget class that overrides `OnEvent()`, open the menu from that override.

### Creating Hierarchical Menus

```cpp
// Create view menu with nested submenus
auto viewMenu = std::make_shared<UltraCanvasMenu>("ViewMenu", 0, 0, 250, 0);

viewMenu->AddItem(MenuItemData::Submenu("Toolbars", {
    MenuItemData::Checkbox("Standard", true, onToggleStandardToolbar),
    MenuItemData::Checkbox("Formatting", false, onToggleFormattingToolbar),
    MenuItemData::Checkbox("Drawing", false, onToggleDrawingToolbar)
}));

viewMenu->AddItem(MenuItemData::Submenu("Zoom", {
    MenuItemData::Action("Zoom In", "Ctrl++", onZoomIn),
    MenuItemData::Action("Zoom Out", "Ctrl+-", onZoomOut),
    MenuItemData::Separator(),
    MenuItemData::Radio("50%", 1, false, onZoom50),
    MenuItemData::Radio("100%", 1, true, onZoom100),
    MenuItemData::Radio("150%", 1, false, onZoom150),
    MenuItemData::Radio("200%", 1, false, onZoom200)
}));
```

### Radio Indicator Shape

A `Radio` item draws a dot inside a round outline, as `UltraCanvasRadio`
does. A style can ask for the same square box a `Checkbox` item gets instead:

```cpp
MenuStyle style = MenuStyle::Default();
style.radioShape = MenuRadioShape::Square;  // the earlier square box
menu->SetStyle(style);
```

Both shapes are `iconSize` wide and are centred on the row. The label is
centred on the row by its cap height rather than by its line box (which
holds the ascender and descender space too), so the indicator and the
visible text sit on one centre line whatever the item height.

### Custom Styling

```cpp
// Create custom menu style
MenuStyle customStyle;
customStyle.backgroundColor = Color(40, 44, 52);
customStyle.textColor = Color(171, 178, 191);
customStyle.hoverColor = Color(50, 54, 62);
customStyle.hoverTextColor = Colors::White;
customStyle.borderColor = Color(30, 34, 42);
customStyle.font.fontSize = 14.0f;
customStyle.itemHeight = 28;
customStyle.paddingLeft = 12;
customStyle.paddingRight = 12;
customStyle.borderRadius = 6;
customStyle.showShadow = true;
customStyle.shadowColor = Color(0, 0, 0, 120);
customStyle.shadowOffset = Point2Di(4, 4);
customStyle.enableAnimations = true;
customStyle.animationDuration = 0.25f;

menu->SetStyle(customStyle);
```

## Animation Support

`MenuStyle` carries animation settings:

```cpp
// Enable animations
MenuStyle style = MenuStyle::Default();
style.enableAnimations = true;
style.animationDuration = 0.2f;  // 200ms
menu->SetStyle(style);
```

In this version the menu only tracks the progress of an opening animation;
drawing does not change with it yet, so a menu appears at once either way.

## Performance Considerations

1. **Lazy Rendering**: Menu items are only rendered when visible
2. **Event Delegation**: Efficient event handling through parent-child delegation
3. **Resource Caching**: Icons and fonts are cached for reuse
4. **Smart Redraw**: Only redraws when necessary (hover, selection changes)

## Platform-Specific Notes

Menus are drawn by UltraCanvas itself on every platform. The one
platform-specific part is the shortcut text: it is passed through
`GetDisplayShortcut()`, which on macOS shows it in Mac style
(`UltraCanvas/OS/MacOS/UltraCanvasMacOSShortcutFormat.h`) and elsewhere returns
it unchanged.

## Thread Safety

UltraCanvasMenu is not thread-safe. All menu operations should be performed on the UI thread.

## Dependencies

- `UltraCanvasUIElement.h` - Base UI element class
- `UltraCanvasCommonTypes.h` - Common type definitions
- `UltraCanvasEvent.h` - Event system
- `UltraCanvasRenderContext.h` - Rendering context
- `UltraCanvasScrollbar.h` / `UltraCanvasSmoothScroll.h` - Scrolling for long menus

## Known Limitations

1. Maximum submenu depth is implementation-defined (typically 10 levels)
2. `Custom` and `Input` items have no special drawing or behaviour yet
3. Touch gesture support varies by platform
4. Open/close animation is not drawn yet (see Animation Support)

## Best Practices

1. **Reuse Menu Instances**: Create menus once and show/hide as needed
2. **Use Factory Methods**: Leverage MenuItemData factory methods for consistency
3. **Keyboard Shortcuts**: Always provide keyboard shortcuts for common actions
4. **Accessibility**: Ensure keyboard navigation works
5. **Responsive Design**: Test menus at different screen resolutions
6. **Memory Management**: Use shared_ptr for menu lifetime management
7. **Event Handling**: Return true from event handlers to stop propagation

## Future Enhancements

- Touch gesture support
- Tear-off menus
- Menu search/filtering
- Voice control integration
- Custom item renderers
- Menu recording/replay for testing

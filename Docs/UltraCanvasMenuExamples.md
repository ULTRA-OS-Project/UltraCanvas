# UltraCanvasMenu Documentation

## Overview

**UltraCanvasMenu** is a comprehensive menu component in the UltraCanvas framework that provides flexible menu functionality including menu bars, popup context menus, and hierarchical submenu support. It offers rich styling options, keyboard navigation, animation support, and cross-platform compatibility.

**Version:** 1.2.5  
**Last Modified:** 2025-01-08  
**Author:** UltraCanvas Framework

## Features

- **Multiple Menu Types**: Support for menu bars, popup menus, and submenus
- **Flexible Orientation**: Vertical and horizontal layout options
- **Rich Item Types**: Actions, separators, checkboxes, radio buttons, submenus, input fields, and custom items
- **Keyboard Navigation**: Full keyboard support with arrow keys, Enter, Escape
- **Animation Support**: Smooth open/close animations with customizable duration
- **Theming**: Pre-built themes (Default, Dark, Flat) with extensive customization
- **Icons & Shortcuts**: Support for item icons and keyboard shortcuts
- **Event System**: Comprehensive callbacks for menu events
- **Submenu Management**: Automatic positioning and cascade control

## Class Structure

### Main Classes

```cpp
namespace UltraCanvas {
    class UltraCanvasMenu : public UltraCanvasUIElement
    struct MenuItemData
    struct MenuStyle
    class MenuBuilder
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

### MenuState

```cpp
enum class MenuState {
    Hidden,         // Menu is not visible
    Opening,        // Animation: menu is opening
    Visible,        // Menu is fully visible
    Closing         // Animation: menu is closing
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
    Input,          // Text input field
    Custom          // Custom rendered item
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
    
    // State
    bool enabled = true;            // Item can be interacted with
    bool visible = true;            // Item is visible
    bool checked = false;           // For checkbox/radio items
    int radioGroup = 0;             // Radio button group ID
    
    // Callbacks
    std::function<void()> onClick;                          // Action items
    std::function<void(bool)> onToggle;                    // Checkbox/radio
    std::function<void(const std::string&)> onTextInput;   // Input items
    
    // Submenu
    std::vector<MenuItemData> subItems;  // Child items for submenus
    
    // Custom data
    void* userData = nullptr;         // User-defined data
};
```

### Factory Methods for Menu Items

```cpp
// Create action item
MenuItemData::Action(label, callback)
MenuItemData::Action(label, iconPath, callback)
MenuItemData::ActionWithShortcut(label, shortcut, callback)
MenuItemData::ActionWithShortcut(label, shortcut, iconPath, callback)

// Create separator
MenuItemData::Separator()

// Create checkbox
MenuItemData::Checkbox(label, checked, toggleCallback)

// Create radio button
MenuItemData::Radio(label, group, checked, toggleCallback)

// Create submenu
MenuItemData::Submenu(label, subItems)
MenuItemData::Submenu(label, iconPath, subItems)

// Create input field
MenuItemData::Input(label, placeholder, inputCallback)
```

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
    
    // Typography
    std::string fontFamily;   // Font family name
    float fontSize;           // Font size in points
    FontWeight fontWeight;    // Font weight
    
    // Dimensions
    int itemHeight;          // Height of menu items
    int iconSize;            // Icon dimensions
    int paddingLeft;         // Left padding
    int paddingRight;        // Right padding
    int paddingTop;          // Top padding
    int paddingBottom;       // Bottom padding
    int iconSpacing;         // Space between icon and text
    int shortcutSpacing;     // Space before shortcut text
    int separatorHeight;     // Height of separator items
    int borderWidth;         // Border thickness
    int borderRadius;        // Corner radius
    
    // Submenu
    int submenuDelay;        // Hover delay before opening (ms)
    int submenuOffset;       // Offset from parent menu
    
    // Animation
    bool enableAnimations;   // Enable open/close animations
    float animationDuration; // Animation duration (seconds)
    
    // Shadow
    bool showShadow;         // Display drop shadow
    Color shadowColor;       // Shadow color
    Point2Di shadowOffset;   // Shadow offset
    int shadowBlur;          // Shadow blur radius
};
```

### Pre-built Themes

```cpp
// Light theme with subtle styling
MenuStyle::Default()

// Dark theme for dark interfaces
MenuStyle::Dark()

// Minimal flat design
MenuStyle::Flat()
```

## Core Methods

### Construction and Initialization

```cpp
// Create menu
UltraCanvasMenu(identifier, x, y, width, height)

// Factory functions
CreateMenu(identifier, x, y, width, height)
CreateMenuBar(identifier, x, y, width)

// Configuration
SetMenuType(MenuType type)
SetOrientation(MenuOrientation orient)
SetStyle(const MenuStyle& style)
```

### Item Management

```cpp
// Add items
AddItem(const MenuItemData& item)
InsertItem(int index, const MenuItemData& item)

// Modify items
UpdateItem(int index, const MenuItemData& item)
RemoveItem(int index)
Clear()

// Access items
GetItems() const
GetItem(int index)
```

### Display Control

```cpp
// Show/hide menu
Show()
Hide()
Toggle()
IsMenuVisible() const
GetMenuState() const

// Context menu helpers
ShowAt(int x, int y)
ShowAt(const Point2Di& position)
```

### Submenu Management

```cpp
OpenSubmenu(int itemIndex)
CloseActiveSubmenu()
CloseAllSubmenus()
CloseMenutree()
```

### Event Callbacks

```cpp
// Set event handlers
OnMenuOpened(std::function<void()> callback)
OnMenuClosed(std::function<void()> callback)
OnItemSelected(std::function<void(int)> callback)
OnItemHovered(std::function<void(int)> callback)
```

## Menu Builder Pattern

The MenuBuilder class provides a fluent interface for constructing menus:

```cpp
auto menu = MenuBuilder("FileMenu", 1, 0, 0)
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

## Event Handling

The menu system handles the following events:

- **Mouse Events**: MouseMove, MouseDown, MouseUp, MouseLeave
- **Keyboard Events**: Arrow keys for navigation, Enter/Space for selection, Escape to close
- **Focus Events**: Automatic focus management for keyboard navigation

### Keyboard Navigation

- **Up/Down Arrows**: Navigate vertical menus
- **Left/Right Arrows**: Navigate horizontal menus or open/close submenus
- **Enter/Space**: Execute selected item
- **Escape**: Close menu
- **Alt+Key**: Access items by mnemonic (if implemented)

## Usage Examples

### Creating a Menu Bar

```cpp
// Create menu bar
auto menuBar = CreateMenuBar("MainMenuBar", 1, 0, 0, windowWidth);

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

// Add items with icons
contextMenu->AddItem(MenuItemData::Action("📋 Copy", "Ctrl+C", []() {
    std::cout << "Copy action executed" << std::endl;
}));

contextMenu->AddItem(MenuItemData::Action("✂️ Cut", "Ctrl+X", []() {
    std::cout << "Cut action executed" << std::endl;
}));

contextMenu->AddItem(MenuItemData::Action("📄 Paste", "Ctrl+V", []() {
    std::cout << "Paste action executed" << std::endl;
}));

contextMenu->AddItem(MenuItemData::Separator());

// Add checkbox item
contextMenu->AddItem(MenuItemData::Checkbox("Show Grid", true, [](bool checked) {
    std::cout << "Grid visibility: " << checked << std::endl;
}));

// Show on right-click
element->OnMouseDown([contextMenu](const UCEvent& event) {
    if (event.button == MouseButton::Right) {
        contextMenu->ShowAt(event.x, event.y);
        return true;
    }
    return false;
});
```

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

### Custom Styling

```cpp
// Create custom menu style
MenuStyle customStyle;
customStyle.backgroundColor = Color(40, 44, 52);
customStyle.textColor = Color(171, 178, 191);
customStyle.hoverColor = Color(50, 54, 62);
customStyle.hoverTextColor = Colors::White;
customStyle.borderColor = Color(30, 34, 42);
customStyle.fontSize = 14.0f;
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

Menus support smooth open/close animations when enabled:

```cpp
// Enable animations
MenuStyle style = MenuStyle::Default();
style.enableAnimations = true;
style.animationDuration = 0.2f;  // 200ms
menu->SetStyle(style);
```

Animation states:
- **Opening**: Menu fades in and/or slides into position
- **Closing**: Menu fades out and/or slides away
- **Visible**: Menu is fully rendered
- **Hidden**: Menu is not rendered

## Performance Considerations

1. **Lazy Rendering**: Menu items are only rendered when visible
2. **Event Delegation**: Efficient event handling through parent-child delegation
3. **Resource Caching**: Icons and fonts are cached for reuse
4. **Smart Redraw**: Only redraws when necessary (hover, selection changes)

## Platform-Specific Notes

The UltraCanvasMenu component uses platform-specific implementations for:
- Native menu rendering (when available)
- System colors and themes
- Keyboard shortcuts and accelerators
- Focus management

Platform implementations are located in:
- `/OS/Windows/UltraCanvasSupport.cpp`
- `/OS/Linux/UltraCanvasSupport.cpp`
- `/OS/MacOS/UltraCanvasSupport.cpp`

## Thread Safety

UltraCanvasMenu is not thread-safe. All menu operations should be performed on the UI thread.

## Dependencies

- `UltraCanvasUIElement.h` - Base UI element class
- `UltraCanvasCommonTypes.h` - Common type definitions
- `UltraCanvasEvent.h` - Event system
- `UltraCanvasKeyboardManager.h` - Keyboard input handling
- `UltraCanvasRenderContext.h` - Rendering context

## Known Limitations

1. Maximum submenu depth is implementation-defined (typically 10 levels)
2. Custom item rendering requires overriding render methods
3. Touch gesture support varies by platform
4. IME input for Input type items is platform-dependent

## Best Practices

1. **Reuse Menu Instances**: Create menus once and show/hide as needed
2. **Use Factory Methods**: Leverage MenuItemData factory methods for consistency
3. **Keyboard Shortcuts**: Always provide keyboard shortcuts for common actions
4. **Accessibility**: Include mnemonics and ensure keyboard navigation works
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

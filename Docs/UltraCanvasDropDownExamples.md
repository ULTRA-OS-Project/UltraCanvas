# UltraCanvasDropDown Control Documentation

## Overview

The UltraCanvasDropDown is an interactive dropdown/combobox component that provides a user-friendly way to select from a list of options. It's part of the UltraCanvas Framework and offers extensive customization options, keyboard navigation, scrolling support, and event handling capabilities.

**Version:** 1.2.4  
**Last Modified:** 2025-01-17  
**Author:** UltraCanvas Framework

## File Structure

```
UltraCanvas/
├── include/
│   └── UltraCanvasDropdown.h      // Declarations
├── core/
│   └── UltraCanvasDropdown.cpp    // Implementations
```

## Key Features

- **Item Management**: Add, remove, and clear items dynamically
- **Styling**: Comprehensive styling options with predefined themes
- **Keyboard Navigation**: Full keyboard support with arrow keys, Enter, Escape, Home, and End
- **Mouse Interaction**: Click to open/close, hover states, item selection
- **Scrolling**: Automatic scrollbar when items exceed maximum visible count
- **Callbacks**: Event callbacks for selection changes, dropdown open/close events
- **Separators**: Support for visual separators between items
- **Builder Pattern**: Fluent interface for easy dropdown creation
- **Popup Rendering**: Separate popup content rendering for z-order management

## Class Structure

### Main Class: `UltraCanvasDropdown`

Inherits from: `UltraCanvasUIElement`

### Supporting Structures

#### DropdownItem
```cpp
struct DropdownItem {
    std::string text;          // Display text
    std::string value;         // Associated value
    std::string iconPath;      // Optional icon path
    bool enabled = true;       // Is item selectable
    bool separator = false;    // Is this a separator
    void* userData = nullptr;  // Custom data
}
```

#### DropdownStyle
```cpp
struct DropdownStyle {
    // Button appearance
    Color normalColor = Colors::White;
    Color hoverColor;
    Color pressedColor;
    Color disabledColor;
    Color borderColor;
    Color focusBorderColor;
    
    // Text colors
    Color normalTextColor = Colors::Black;
    Color disabledTextColor;
    
    // List appearance  
    Color listBackgroundColor;
    Color listBorderColor;
    Color itemHoverColor;
    Color itemSelectedColor;
    
    // Dimensions
    float borderWidth = 1.0f;
    float cornerRadius = 2.0f;
    float paddingLeft = 8.0f;
    float paddingRight = 20.0f;
    float itemHeight = 24.0f;
    int maxItemWidth = 400;
    int maxVisibleItems = 8;
    float arrowSize = 8.0f;
    
    // Shadow
    bool hasShadow = true;
    Color shadowColor;
    Point2Di shadowOffset;
    
    // Font
    std::string fontFamily = "Arial";
    float fontSize = 12.0f;
}
```

## Public API

### Constructor
```cpp
UltraCanvasDropdown(const std::string& identifier,  
                    long x, long y, long w, long h = 24);
```

### Item Management Methods

```cpp
// Add items
void AddItem(const std::string& text);
void AddItem(const std::string& text, const std::string& value);
void AddItem(const DropdownItem& item);
void AddSeparator();

// Remove/Clear items
void ClearItems();
void RemoveItem(int index);

// Access items
const std::vector<DropdownItem>& GetItems() const;
int GetItemCount() const;
const DropdownItem* GetItem(int index) const;
```

### Selection Management

```cpp
void SetSelectedIndex(int index);
int GetSelectedIndex() const;
const DropdownItem* GetSelectedItem() const;
```

### Dropdown State Control

```cpp
void OpenDropdown();
void CloseDropdown();
bool IsDropdownOpen() const;
```

### Styling

```cpp
void SetStyle(const DropdownStyle& newStyle);
const DropdownStyle& GetStyle() const;
```

### Event Callbacks

```cpp
// Selection changed callback
std::function<void(int, const DropdownItem&)> onSelectionChanged;

// Item hovered callback  
std::function<void(int, const DropdownItem&)> onItemHovered;

// Dropdown opened/closed callbacks
std::function<void()> onDropdownOpened;
std::function<void()> onDropdownClosed;
```

## Predefined Styles

The framework provides several predefined styles accessible through the `DropdownStyles` namespace:

### Default Style
```cpp
DropdownStyle style = DropdownStyles::Default();
```
Standard appearance with white background and gray borders.

### Flat Style
```cpp
DropdownStyle style = DropdownStyles::Flat();
```
Modern flat design without shadows or rounded corners.

### Modern Style
```cpp
DropdownStyle style = DropdownStyles::Modern();
```
Contemporary design with rounded corners and enhanced padding.

### Dark Style
```cpp
DropdownStyle style = DropdownStyles::Dark();
```
Dark theme suitable for dark mode interfaces.

## Builder Pattern Usage

The DropdownBuilder provides a fluent interface for creating dropdowns:

```cpp
auto dropdown = DropdownBuilder("myDropdown", 100, 100)
    .AddItem("Option 1")
    .AddItem("Option 2")
    .AddSeparator()
    .AddItem("Option 3", "value3")
    .SetStyle(DropdownStyles::Modern())
    .SetSelectedIndex(0)
    .OnSelectionChanged([](int index, const DropdownItem& item) {
        std::cout << "Selected: " << item.text << std::endl;
    })
    .Build();
```

## Factory Functions

### CreateDropdown
```cpp
std::shared_ptr<UltraCanvasDropdown> CreateDropdown(
    const std::string& identifier, long id, 
    long x, long y, long w, long h = 24);
```

### CreateAutoDropdown
```cpp
std::shared_ptr<UltraCanvasDropdown> CreateAutoDropdown(
    const std::string& identifier, long x, long y,
    const std::vector<std::string>& items);
```
Creates a dropdown with automatic sizing and populates it with the provided items.

## Event Handling

The dropdown handles various event types:

### Mouse Events
- **MouseDown**: Opens/closes dropdown, selects items
- **MouseUp**: Releases button press state
- **MouseMove**: Updates hover state for items
- **MouseLeave**: Clears hover state
- **MouseWheel**: Scrolls through items when dropdown is open

### Keyboard Events
- **Return/Space**: Opens dropdown or selects hovered item
- **Escape**: Closes dropdown
- **Up/Down**: Navigate through items
- **Home/End**: Jump to first/last enabled item

### Focus Events
- **FocusLost**: Automatically closes dropdown when focus is lost

## Implementation Details

### Rendering Pipeline

1. **Main Button Rendering** (`RenderButton`)
   - Draws button background with current state color
   - Renders selected item text
   - Draws dropdown arrow indicator
   - Handles focus border rendering

2. **Popup Content Rendering** (`RenderPopupContent`)
   - Calculates popup position relative to button
   - Draws list background and border
   - Renders visible items with scroll offset
   - Displays scrollbar when needed

3. **Item Rendering** (`RenderDropdownItem`)
   - Handles normal items and separators
   - Applies hover/selected state colors
   - Renders item text with proper crossAlignment

### Scrolling System

The dropdown automatically displays a scrollbar when items exceed `maxVisibleItems`:

- Scrollbar appears on the right side of the list
- Mouse wheel scrolling supported (3 items per notch)
- Keyboard navigation automatically scrolls to keep selection visible
- `EnsureItemVisible` method maintains view of current item

### Position Calculation

The dropdown intelligently positions its popup:

1. Default placement below the button
2. Adjusts horizontally if extending beyond window bounds
3. Opens above if insufficient space below

## Usage Examples

### Basic Dropdown

```cpp
// Create dropdown
auto dropdown = std::make_shared<UltraCanvasDropdown>(
    "countrySelect", 50, 100, 200, 30);

// Add items
dropdown->AddItem("United States");
dropdown->AddItem("Canada");
dropdown->AddItem("Mexico");

// Set initial selection
dropdown->SetSelectedIndex(0);

// Add to container
container->AddChild(dropdown);
```

### Dropdown with Separators

```cpp
auto dropdown = CreateDropdown("fileMenu", 2, 10, 40, 150);

dropdown->AddItem("New");
dropdown->AddItem("Open");
dropdown->AddItem("Save");
dropdown->AddSeparator();
dropdown->AddItem("Export");
dropdown->AddItem("Print");
dropdown->AddSeparator();
dropdown->AddItem("Exit");
```

### Dropdown with Custom Styling

```cpp
// Create custom style
DropdownStyle customStyle;
customStyle.normalColor = Color(250, 250, 250);
customStyle.hoverColor = Color(230, 240, 255);
customStyle.itemHeight = 32.0f;
customStyle.fontSize = 14.0f;
customStyle.maxVisibleItems = 10;

// Apply to dropdown
dropdown->SetStyle(customStyle);
```

### Dropdown with Event Handlers

```cpp
auto dropdown = CreateAutoDropdown("themeSelector", 200, 50, 
    {"Light", "Dark", "Blue", "High Contrast"});

// Selection change handler
dropdown->onSelectionChanged = [](int index, const DropdownItem& item) {
    std::cout << "Theme changed to: " << item.text << std::endl;
    ApplyTheme(item.text);
};

// Dropdown opened handler
dropdown->onDropdownOpened = []() {
    std::cout << "Theme selector opened" << std::endl;
};
```

### Dynamic Item Management

```cpp
// Clear and repopulate based on context
void UpdateDropdownOptions(UltraCanvasDropdown* dropdown, 
                          const std::string& category) {
    dropdown->ClearItems();
    
    if (category == "Fruits") {
        dropdown->AddItem("Apple");
        dropdown->AddItem("Banana");
        dropdown->AddItem("Orange");
    } else if (category == "Vegetables") {
        dropdown->AddItem("Carrot");
        dropdown->AddItem("Broccoli");
        dropdown->AddItem("Spinach");
    }
    
    dropdown->SetSelectedIndex(0);
}
```

## Navigation Methods

### NavigateItem
Navigates through items when dropdown is open, skipping disabled items and separators.

### NavigateSelection
Changes selection when dropdown is closed, cycling through enabled items.

### FindFirstEnabledItem / FindLastEnabledItem
Helper methods to find the first or last selectable item in the list.

## Performance Considerations

1. **Lazy Dimension Calculation**: Dimensions are recalculated only when items change
2. **Efficient Rendering**: Only visible items are rendered when scrolling
3. **Event Optimization**: Mouse move events are processed only when dropdown is open
4. **Clipping**: Popup content uses clipping to avoid overdraw

## Integration Notes

### Z-Order Management
The dropdown uses the popup element system for proper z-order:
- `AddThisPopupElementToWindow()` when opening
- `RemoveThisPopupElementFromWindow()` when closing

### Window Coordination
The dropdown tracks its parent window for:
- Render context access
- Position calculations
- Bounds checking

## Known Limitations

1. Shadow rendering is currently commented out (pending implementation)
2. Icon rendering for items not yet implemented
3. Multi-select mode not available in current version
4. Editable combobox functionality not implemented

## Future Enhancements

Planned features for future versions:
- Icon support for items
- Multi-select capability
- Editable text input mode
- Custom item rendering callbacks
- Filtered/searchable dropdown
- Grouped items with headers
- Async item loading support

## Debugging

Debug output is available through console logging:
- Event handling traces
- Render state information
- Item selection feedback

To enable verbose logging, the component includes strategic `std::cout` statements that can be toggled for development.

## Dependencies

- UltraCanvasUIElement (base class)
- UltraCanvasEvent (event system)
- UltraCanvasCommonTypes (geometric types)
- UltraCanvasRenderContext (rendering interface)
- Standard C++ libraries: vector, string, functional, memory, algorithm

## Version History

- **1.2.4** (2025-01-17): Current version with popup rendering support
- **1.2.3**: Added keyboard navigation improvements
- **1.2.0**: Introduced builder pattern and predefined styles
- **1.0.0**: Initial implementation with basic functionality

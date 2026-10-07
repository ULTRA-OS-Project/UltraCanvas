# UltraCanvasDropDown Control Documentation

<!-- doc-check: void ApplyTheme(const std::string& name); -->

## Overview

The UltraCanvasDropDown is an interactive dropdown/combobox component that provides a user-friendly way to select from a list of options. It's part of the UltraCanvas Framework and offers styling, item icons, single or multiple selection, keyboard navigation, scrolling support, and event handling capabilities.

**Version:** 1.2.5  
**Last Modified:** 2026-10-07  
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
- **Icons**: An optional icon per item, shown in the list and on the button
- **Multi-selection**: Optional checkbox mode for picking several items
- **Styling**: One `DropdownStyle` structure for the button, the list and the scrollbar
- **Keyboard Navigation**: Arrow keys, Page Up/Down, Home, End, Enter, Space and Escape
- **Mouse Interaction**: Click to open/close, hover states, item selection
- **Scrolling**: Automatic scrollbar when items exceed maximum visible count
- **Callbacks**: Event callbacks for selection changes, dropdown open/close events
- **Separators**: Support for visual separators between items
- **Popup List**: The list is an `UltraCanvasListView` opened as a window popup, so it draws above other elements

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
    bool selected = false;     // Checked state in multi-selection mode
    void* userData = nullptr;  // Custom data

    DropdownItem() = default;
    DropdownItem(const std::string& itemText);   // value = text
    DropdownItem(const std::string& itemText, const std::string& itemValue);
    DropdownItem(const std::string& itemText, const std::string& itemValue, const std::string& icon);
};
```

#### DropdownStyle
```cpp
struct DropdownStyle {
    // Button appearance
    Color normalColor = Colors::White;
    Color hoverColor = Color(240, 245, 255, 255);
    Color pressedColor = Color(225, 235, 255, 255);
    Color disabledColor = Color(245, 245, 245, 255);
    Color borderColor = Color(180, 180, 180, 255);
    Color focusBorderColor = Color(100, 150, 255, 255);

    // Text colors
    Color normalTextColor = Colors::Black;
    Color disabledTextColor = Color(128, 128, 128, 255);

    // List appearance
    Color listBackgroundColor = Colors::White;
    Color listBorderColor = Color(180, 180, 180, 255);
    Color itemHoverColor = Color(240, 245, 255, 255);
    Color itemSelectedColor = Color(225, 235, 255, 255);

    // Multi-selection colors
    Color checkboxBorderColor = Color(180, 180, 180, 255);
    Color checkboxCheckedColor = Color(100, 150, 255, 255);
    Color checkmarkColor = Colors::White;

    // Dimensions
    float borderWidth = 1.0f;
    float cornerRadius = 2.0f;
    float paddingLeft = 8.0f;
    float paddingRight = 20.0f;
    float itemHeight = 24.0f;
    float maxItemWidth = 400;
    int maxVisibleItems = 8;       // -1 = show every item
    float arrowSize = 8.0f;

    // Icon dimensions
    float iconSize = 16.0f;
    float iconPadding = 4.0f;

    // Checkbox dimensions (for multi-select)
    float checkboxSize = 14.0f;
    float checkboxPadding = 6.0f;

    // Font
    std::string fontFamily;        // empty = the default font
    float fontSize = 11.0f;

    // Scrollbar style
    ScrollbarStyle scrollbarStyle = GetDefaultScrollbarStyleOr(ScrollbarStyle::DropDown());
};
```

## Public API

### Constructors
```cpp
UltraCanvasDropdown(const std::string& identifier, float x, float y, float w, float h);
UltraCanvasDropdown(const std::string& identifier, float w, float h);   // positioned by a layout
explicit UltraCanvasDropdown(const std::string& identifier);            // sized by a layout too
```

### Item Management Methods

```cpp
// Add items
void AddItem(const std::string& text);
void AddItem(const std::string& text, const std::string& value);
void AddItem(const std::string& text, const std::string& value, const std::string& iconPath);
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
void SetSelectedIndex(int index, bool runNotifications = true);   // false: no onSelectionChanged
int GetSelectedIndex() const;
const DropdownItem* GetSelectedItem() const;
```

### Multi-selection

```cpp
void SetMultiSelectEnabled(bool enabled);
bool IsMultiSelectEnabled() const;

void SetItemSelected(int index, bool selected);
bool IsItemSelected(int index) const;

void SelectAll();
void DeselectAll();

std::vector<int> GetSelectedIndices() const;
std::vector<DropdownItem> GetSelectedItems() const;
int GetSelectedCount() const;
```

In multi-selection mode each item shows a checkbox, a click toggles it and the
list stays open. The button shows the single checked item, `"N items selected"`,
or `"Select items..."` when nothing is checked.

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

// Key pressed on the dropdown; return true to consume it
std::function<bool(const UCEvent&)> onKeyDown;

// Multi-selection callbacks
std::function<void(const std::vector<int>&)> onMultiSelectionChanged;
std::function<void(const std::vector<DropdownItem>&)> onSelectedItemsChanged;
```

## Styles

There are no predefined dropdown styles and no builder: start from a default
`DropdownStyle`, change the fields you need and pass it to `SetStyle()` (see
*Dropdown with Custom Styling* below). The look of the list scrollbar comes
from `scrollbarStyle`.

## Factory Functions

### CreateDropdown
```cpp
inline std::shared_ptr<UltraCanvasDropdown> CreateDropdown(
        const std::string& identifier, float x, float y, float w, float h = 24);
```

## Event Handling

The dropdown handles various event types:

### Mouse Events
- **MouseDown / MouseDoubleClick**: Opens or closes the dropdown (every click toggles)
- **MouseUp**: Releases button press state
- **MouseMove**: Sets the cursor over the button
- **Item hover, click and wheel scrolling** are handled by the popup list view

### Keyboard Events
- **Down/Space** (closed): Opens the dropdown
- **Up/Down, Page Up/Page Down, Home/End** (open): Move through the list
- **Return** (open): Selects the focused item (toggles it in multi-selection mode)
- **Space** (open, multi-selection): Toggles the focused item
- **Escape**: Closes dropdown
- `onKeyDown` sees every key first; returning true stops the default handling

### Focus Events
- **FocusLost**: Closes the dropdown (not in multi-selection mode)

A disabled dropdown (`SetDisabled(true)`) ignores all input and is drawn with
`disabledColor` and `disabledTextColor`.

## Implementation Details

### Rendering Pipeline

1. **Main Button Rendering** (`Render`)
   - Draws button background with current state color
   - Renders the selected item's icon and text (or the multi-selection summary)
   - Draws dropdown arrow indicator
   - Handles focus border rendering

2. **Popup List** (an `UltraCanvasListView`)
   - Built once per dropdown, backed by a `DropdownListModel` over the items
   - Sized to the widest item (at least the button width, at most
     `maxItemWidth`) and `maxVisibleItems` rows of `itemHeight`
   - Draws the list background, border, hover and selection colors from the style

3. **Item Rendering** (`DropdownItemDelegate`)
   - Handles normal items and separators
   - Draws the checkbox in multi-selection mode and the item icon
   - Applies hover/selected state colors

### Scrolling System

The list view shows a scrollbar when items exceed `maxVisibleItems`:

- The scrollbar uses `DropdownStyle::scrollbarStyle`
- Mouse wheel scrolling is handled by the list view
- Opening the dropdown scrolls the selected item into view

### Position Calculation

The dropdown positions its popup:

1. Below the button when it fits
2. Above the button when it fits there instead
3. Otherwise on the side with more room, shortened to fit
4. Shifted left if it would extend beyond the window's right edge

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
auto dropdown = CreateDropdown("fileMenu", 10, 40, 150);

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
auto dropdown = CreateDropdown("themeSelector", 200, 50, 160);
for (const char* theme : {"Light", "Dark", "Blue", "High Contrast"}) {
    dropdown->AddItem(theme);
}

// Selection change handler
dropdown->onSelectionChanged = [](int index, const DropdownItem& item) {
    std::cerr << "Theme changed to: " << item.text << std::endl;
    ApplyTheme(item.text);
};

// Dropdown opened handler
dropdown->onDropdownOpened = []() {
    std::cerr << "Theme selector opened" << std::endl;
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

// For example, refill the item list when a category is picked
categoryDropdown->onSelectionChanged = [itemDropdown](int, const DropdownItem& item) {
    UpdateDropdownOptions(itemDropdown.get(), item.text);
};
```

### Items with Icons and Multi-selection

```cpp
auto langs = CreateDropdown("languages", 10, 80, 200);
langs->SetMultiSelectEnabled(true);
langs->AddItem("C++", "cpp", "media/icons/cpp.png");
langs->AddItem(DropdownItem("Python", "py", "media/icons/python.png"));
langs->AddItem("Rust", "rs");

langs->onSelectedItemsChanged = [](const std::vector<DropdownItem>& picked) {
    std::cerr << picked.size() << " languages selected" << std::endl;
};
```

## Performance Considerations

1. **Popup Sizing on Open**: The list size is measured each time the dropdown opens
2. **Efficient Rendering**: The list view renders only the visible rows
3. **Shared Item Storage**: The list model reads the dropdown's own item vector, nothing is copied

## Integration Notes

### Z-Order Management
The dropdown uses the window's popup system for proper z-order:
- `OpenDropdown()` opens the list with `UltraCanvasWindowBase::OpenPopup()`
- `CloseDropdown()` closes it with `UltraCanvasWindowBase::ClosePopup()`; the
  window also closes it on Escape or a click outside, and `onDropdownClosed`
  fires in every case

`OpenDropdown()` does nothing until the dropdown is in a window and has items.

### Window Coordination
The dropdown tracks its parent window for:
- Render context access
- Position calculations
- Bounds checking

## Known Limitations

1. No drop shadow under the list
2. Editable combobox functionality not implemented

## Future Enhancements

Planned features for future versions:
- Editable text input mode
- Custom item rendering callbacks
- Filtered/searchable dropdown
- Grouped items with headers
- Async item loading support

## Dependencies

- UltraCanvasUIElement (base class)
- UltraCanvasListView, UltraCanvasListModel, UltraCanvasListDelegate, UltraCanvasListSelection (popup list)
- UltraCanvasEvent (event system)
- UltraCanvasCommonTypes (geometric types)
- UltraCanvasRenderContext (rendering interface)
- Standard C++ libraries: vector, string, functional, memory, algorithm, set

## Version History

- **1.2.5** (2026-10-07): Match the header: real `DropdownStyle` fields, `float` constructors and `CreateDropdown()`, multi-selection and icon API, list-view popup; removed the nonexistent DropdownStyles presets, DropdownBuilder, CreateAutoDropdown and shadow fields
- **1.2.4** (2025-01-17): Popup rendering support
- **1.2.3**: Added keyboard navigation improvements
- **1.2.0**: Introduced builder pattern and predefined styles
- **1.0.0**: Initial implementation with basic functionality

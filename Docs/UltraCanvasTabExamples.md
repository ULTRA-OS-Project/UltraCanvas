# UltraCanvasTabbedContainer Documentation

## Overview

The **UltraCanvasTabbedContainer** is an advanced tabbed interface component in the UltraCanvas framework that provides rich functionality for organizing content in tabs. It features automatic overflow handling with dropdown menus, integrated search capabilities, multiple tab positioning options, and extensive customization possibilities.

**Version:** 1.6.0  
**Last Modified:** 2025-09-14  
**Author:** UltraCanvas Framework

## Features

### Core Features
- **Multiple Tab Positions:** Top, Bottom, Left, Right
- **Tab Styles:** Classic, Modern, Flat, Rounded, Custom
- **Overflow Management:** Automatic dropdown when tabs exceed available space
- **Search Functionality:** Real-time filtering of tabs in dropdown
- **Tab Reordering:** Drag-and-drop tab repositioning
- **Close Buttons:** Configurable close button behavior
- **Keyboard Navigation:** Arrow keys, shortcuts, and search input
- **Event System:** Comprehensive callbacks for all user interactions
- **Content Management:** Each tab can contain any UltraCanvas UI element

### Enhanced Dropdown Features
- **Smart Overflow Detection:** Automatically shows dropdown when tabs don't fit
- **Position Control:** Left or Right side dropdown positioning
- **Search Integration:** Configurable search threshold with visual indicators
- **Visual Markers:** Active (●) and disabled ([]) tab indicators in dropdown
- **Real-time Filtering:** Instant search results as user types

## Class Definition

```cpp
namespace UltraCanvas {
    class UltraCanvasTabbedContainer : public UltraCanvasContainer {
        // ... implementation
    };
}
```

## Enumerations

### TabPosition
Defines where tabs are displayed relative to content area.

```cpp
enum class TabPosition {
    Top,      // Tabs above content (default)
    Bottom,   // Tabs below content
    Left,     // Tabs on left side
    Right     // Tabs on right side
};
```

### TabStyle
Visual appearance presets for tabs.

```cpp
enum class TabStyle {
    Classic,  // Traditional tab appearance
    Modern,   // Contemporary flat design
    Flat,     // Minimal borders
    Rounded,  // Rounded corners
    Custom    // User-defined styling
};
```

### TabCloseMode
Controls close button behavior.

```cpp
enum class TabCloseMode {
    NoClose,             // No close buttons shown
    Closable,            // All tabs have close buttons
    ClosableExceptFirst  // All except first tab can be closed
};
```

### OverflowDropdownPosition
Where to display the overflow dropdown.

```cpp
enum class OverflowDropdownPosition {
    Off,    // No dropdown (scrolling only)
    Left,   // Dropdown on left side of tabs
    Right   // Dropdown on right side of tabs
};
```

## TabData Structure

Represents individual tab properties.

```cpp
struct TabData {
    std::string title;                        // Tab label text
    std::string tooltip;                      // Hover tooltip text
    bool enabled = true;                      // Interactive state
    bool visible = true;                      // Visibility state
    bool closable = true;                     // Can be closed
    Color textColor = Colors::Black;          // Text color
    Color backgroundColor = Color(240, 240, 240); // Background color
    std::shared_ptr<UltraCanvasUIElement> content = nullptr; // Tab content
    void* userData = nullptr;                 // Custom user data
};
```

## Public Methods

### Construction and Initialization

```cpp
UltraCanvasTabbedContainer(const std::string& elementId, 
                           long uniqueId, 
                           long posX, long posY, 
                           long width, long height)
```

### Tab Management

#### Adding Tabs
```cpp
int AddTab(const std::string& title, 
          std::shared_ptr<UltraCanvasUIElement> content)
```
Adds a new tab with specified title and content. Returns the index of the new tab.

#### Removing Tabs
```cpp
void RemoveTab(int index)
```
Removes the tab at the specified index. Triggers onTabClose callback.

#### Setting Active Tab
```cpp
void SetActiveTab(int index)
```
Switches to the specified tab index.

### Tab Properties

```cpp
void SetTabTitle(int index, const std::string& title)
std::string GetTabTitle(int index) const
void SetTabEnabled(int index, bool enabled)
bool IsTabEnabled(int index) const
```

### Container Configuration

#### Tab Appearance
```cpp
void SetTabHeight(int height)           // Default: 30
void SetTabMinWidth(int width)          // Default: 50
void SetTabMaxWidth(int width)          // Default: 200
void SetTabPosition(TabPosition position)
void SetTabStyle(TabStyle style)
void SetCloseMode(TabCloseMode mode)
```

#### Overflow Dropdown
```cpp
void SetOverflowDropdownPosition(OverflowDropdownPosition position)
void SetOverflowDropdownWidth(int width)  // Default: 200
void SetDropdownSearchEnabled(bool enabled)
void SetDropdownSearchThreshold(int threshold) // Default: 15
void ClearDropdownSearch()
```

### Query Methods

```cpp
int GetActiveTab() const
int GetTabCount() const
TabPosition GetTabPosition() const
TabStyle GetTabStyle() const
TabCloseMode GetCloseMode() const
```

## Event Callbacks

The container provides extensive callback support for user interactions:

```cpp
// Tab selection events
std::function<void(int oldIndex, int newIndex)> onTabChange;
std::function<void(int index)> onTabSelect;

// Tab closure events
std::function<bool(int index)> onTabClose;      // Return false to cancel
std::function<void(int index)> onTabCloseRequest;

// Tab modification events
std::function<void(int fromIndex, int toIndex)> onTabReorder;
std::function<void(int index, const std::string& newTitle)> onTabRename;

// Overflow events
std::function<void(bool visible)> onOverflowChange;
std::function<void(const std::string& searchText)> onSearchChange;
```

## Styling Properties

### Color Properties
```cpp
Color tabBarColor = Color(245, 245, 245);
Color tabBorderColor = Color(200, 200, 200);
Color activeTabColor = Colors::White;
Color activeTabTextColor = Colors::Black;
Color inactiveTabColor = Color(235, 235, 235);
Color inactiveTabTextColor = Color(100, 100, 100);
Color hoveredTabColor = Color(240, 240, 240);
Color hoveredTabTextColor = Colors::Black;
Color disabledTabColor = Color(250, 250, 250);
Color disabledTabTextColor = Color(180, 180, 180);
```

### Layout Properties
```cpp
int tabSpacing = 2;              // Space between tabs
int tabPadding = 10;             // Internal tab padding
int closeButtonSize = 14;        // Close button dimensions
int closeButtonMargin = 5;       // Close button spacing
bool autoSizeTab = true;         // Auto-adjust tab width
bool allowTabReordering = true;  // Enable drag-and-drop
bool enableTabScrolling = true;  // Enable scroll buttons
```

## Factory Functions

### CreateTabbedContainerWithDropdown
Creates a tabbed container with dropdown configuration:

```cpp
auto container = CreateTabbedContainerWithDropdown(
    "main_tabs",                          // ID
    1001,                                 // UID
    10, 10, 980, 500,                    // Position and size
    OverflowDropdownPosition::Left,       // Dropdown position
    true,                                 // Enable search
    5                                     // Search threshold
);
```

### CreateTabbedContainer
Creates a basic tabbed container:

```cpp
auto container = CreateTabbedContainer(
    "tabs",                               // ID
    1000,                                 // UID
    0, 0, 800, 600                       // Position and size
);
```

## Usage Examples

### Basic Tab Container
```cpp
// Create container
auto tabs = CreateTabbedContainer("myTabs", 10, 10, 800, 600);

// Add tabs with content
auto panel1 = std::make_shared<UltraCanvasContainer>("panel1", 0, 0, 0, 0);
tabs->AddTab("Dashboard", panel1);

auto panel2 = std::make_shared<UltraCanvasContainer>("panel2", 0, 0, 0, 0);
tabs->AddTab("Settings", panel2);

// Configure appearance
tabs->SetTabStyle(TabStyle::Modern);
tabs->SetCloseMode(TabCloseMode::Closable);
```

### Advanced Configuration with Dropdown
```cpp
// Create with dropdown and search
auto tabs = CreateTabbedContainerWithDropdown(
    "advancedTabs", 200, 0, 0, 1024, 768,
    OverflowDropdownPosition::Right,
    true,  // Enable search
    10     // Show search when >10 tabs
);

// Customize colors
tabs->activeTabColor = Color(0, 120, 215);
tabs->activeTabTextColor = Colors::White;
tabs->tabBarColor = Color(240, 248, 255);

// Set up event handlers
tabs->onTabChange = [](int oldIndex, int newIndex) {
    std::cout << "Tab changed from " << oldIndex << " to " << newIndex << std::endl;
};

tabs->onTabClose = [](int index) {
    // Confirm before closing
    return MessageBox::Confirm("Close this tab?");
};

// Enable reordering
tabs->allowTabReordering = true;
tabs->onTabReorder = [](int from, int to) {
    std::cout << "Tab moved from " << from << " to " << to << std::endl;
};
```

### Nested Tabs Example
```cpp
// Create main container
auto mainTabs = CreateTabbedContainer("mainTabs", 0, 0, 800, 600);

// Create nested container for one tab
auto nestedTabs = CreateTabbedContainer("nestedTabs", 0, 0, 780, 540);
nestedTabs->SetTabPosition(TabPosition::Left);
nestedTabs->SetTabStyle(TabStyle::Flat);

// Add nested tabs
nestedTabs->AddTab("Option 1", option1Panel);
nestedTabs->AddTab("Option 2", option2Panel);

// Add nested container to main
mainTabs->AddTab("Advanced", nestedTabs);
```

## Keyboard Shortcuts

| Key Combination | Action |
|----------------|--------|
| Left Arrow | Navigate to previous tab |
| Right Arrow | Navigate to next tab |
| Ctrl+W | Close current tab (if closable) |
| Escape | Clear search / Close dropdown |
| Enter | Select highlighted search result |
| Backspace | Delete search character |
| Any character | Add to search filter (when dropdown open) |

## Event Flow

### Tab Selection
1. User clicks on tab or selects from dropdown
2. `onTabChange` callback fired with old and new indices
3. `onTabSelect` callback fired with selected index
4. Content visibility updated
5. Tab scrolled into view if needed

### Tab Closure
1. User clicks close button or presses Ctrl+W
2. `onTabClose` callback fired (can return false to cancel)
3. If not cancelled, tab removed
4. `onTabCloseRequest` callback fired
5. Active tab updated if necessary

### Dropdown Search
1. Search activated when tab count > threshold
2. User types in dropdown
3. `onSearchChange` callback fired with search text
4. Dropdown list filtered in real-time
5. Escape clears search, Enter selects match

## Rendering Details

The component renders in multiple layers:

1. **Tab Bar Background** - Base layer with configurable color
2. **Individual Tabs** - Rendered with state-based colors
3. **Active Tab Highlight** - Visual indicator for selected tab
4. **Tab Text** - Truncated with ellipsis if needed
5. **Close Buttons** - Positioned at right edge of tabs
6. **Overflow Dropdown** - Rendered when tabs exceed space
7. **Content Area** - Active tab's content rendered below/beside tabs

## Performance Considerations

- **Lazy Loading:** Tab content is only rendered when visible
- **Efficient Layout:** Layout recalculated only when needed
- **Smart Overflow:** Dropdown populated only when shown
- **Optimized Search:** Case-insensitive substring matching
- **Event Delegation:** Single event handler for all tabs

## Best Practices

1. **Limit Tab Count:** Keep under 20 tabs for best UX
2. **Use Search:** Enable search for >10 tabs
3. **Meaningful Titles:** Use clear, concise tab labels
4. **Icon Support:** Prefix titles with emoji/icons for recognition
5. **Consistent Style:** Match tab style to application theme
6. **Proper Cleanup:** Remove event handlers when destroying
7. **Content Caching:** Reuse content components when possible

## Migration from Previous Versions

### From v1.5.x to v1.6.0
- Added `OverflowDropdownPosition` enum
- Added search functionality with threshold
- New callbacks: `onOverflowChange`, `onSearchChange`
- Enhanced keyboard navigation support

## Thread Safety

The UltraCanvasTabbedContainer is **not** thread-safe. All operations should be performed on the main UI thread.

## Dependencies

- UltraCanvasContainer (base class)
- UltraCanvasDropdown (overflow menu)
- UltraCanvasEvent (event handling)
- UltraCanvasRenderContext (rendering)

## Platform Support

- **Windows:** Full support
- **Linux:** Full support
- **macOS:** Full support
- **UltraOS:** Full support
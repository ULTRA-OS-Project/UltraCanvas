# UltraCanvasToolbar Documentation

## Overview

**UltraCanvasToolbar** is a comprehensive cross-platform toolbar component supporting buttons, toggle buttons, dropdowns, labels, separators, and spacers. It can be rendered as a horizontal toolbar, a vertical sidebar, a ribbon, a status bar, or a dock, with built-in overflow handling and optional drag-to-reorder.

**Version:** 1.1.0
**Header:** `include/UltraCanvasToolbar.h`
**Namespace:** `UltraCanvas`
**Base Class:** `UltraCanvasContainer`

## Features

- **Item Types**: Button, ToggleButton, DropdownButton, Separator, Spacer (fixed and flex), Label, TextInput, SearchBox, Checkbox
- **Multiple Styles**: Standard, Flat, Raised, Docked (macOS-like), Ribbon, StatusBar, Sidebar
- **Orientation**: Horizontal or Vertical
- **Overflow Handling**: Wrap, overflow menu, scroll, or hide
- **Builder API**: Fluent `UltraCanvasToolbarBuilder`
- **Preset Factories**: One-call construction of common toolbar shapes
- **Item Badges and Tooltips**: Visual notification badges and hover tooltips
- **Auto-Hide and Drag Modes**: Optional auto-hide and drag-to-move / drag-to-reorder

## Header Include

```cpp
#include "UltraCanvasToolbar.h"
```

## Class Reference

### Constructor

```cpp
UltraCanvasToolbar(const std::string& identifier,
                   long x, long y, long width, long height);
```

### Configuration

```cpp
void SetOrientation(ToolbarOrientation orient);
void SetToolbarPosition(ToolbarPosition pos);
void SetAppearance(const ToolbarAppearance& app);
void SetOverflowMode(ToolbarOverflowMode mode);
void SetVisibility(ToolbarVisibility vis);
void SetDragMode(ToolbarDragMode mode);

ToolbarOrientation       GetOrientation() const;
ToolbarPosition          GetPosition() const;
const ToolbarAppearance& GetAppearance() const;
```

### Item Management

```cpp
void AddItem(const ToolbarItemDescriptor& descriptor);
void AddItem(std::shared_ptr<UltraCanvasToolbarItem> item);
void InsertItem(int index, const ToolbarItemDescriptor& descriptor);
void InsertItem(int index, std::shared_ptr<UltraCanvasToolbarItem> item);
void RemoveItem(const std::string& identifier);
void RemoveItemAt(int index);
void ClearItems();

std::shared_ptr<UltraCanvasToolbarItem> GetItem(const std::string& identifier);
std::shared_ptr<UltraCanvasToolbarItem> GetItemAt(int index);
int GetItemCount() const;
```

### Convenience Methods

```cpp
void AddButton(const std::string& id, const std::string& text,
               const std::string& icon = "", std::function<void()> onClick = nullptr);

void AddToggleButton(const std::string& id, const std::string& text,
                     const std::string& icon = "",
                     std::function<void(bool)> onToggle = nullptr);

void AddDropdownButton(const std::string& id, const std::string& text,
                       const std::vector<std::string>& items,
                       std::function<void(const std::string&)> onSelect = nullptr);

void AddSeparator(const std::string& id = "");
void AddSpacer(int size = 8);
void AddStretch(float stretch = 1.0f);
void AddLabel(const std::string& id, const std::string& text);
void AddSearchBox(const std::string& id,
                  const std::string& placeholder = "Search...",
                  std::function<void(const std::string&)> onTextChange = nullptr);
```

### Auto-Hide

```cpp
void SetAutoHideDelay(float delay);
void ShowToolbar();
void HideToolbar();
bool IsAutoHidden() const;
```

### Drag and Drop

```cpp
void EnableItemReordering(bool enable);
void BeginDrag(const Point2Di& startPos);
void UpdateDrag(const Point2Di& currentPos);
void EndDrag();
```

## Enumerations

### ToolbarOrientation

```cpp
enum class ToolbarOrientation { Horizontal, Vertical };
```

### ToolbarPosition

```cpp
enum class ToolbarPosition { Top, Bottom, Left, Right, Floating };
```

### ToolbarStyle

```cpp
enum class ToolbarStyle {
    Standard,    // Classic toolbar
    Flat,        // Borderless
    Raised,      // With shadows
    Docked,      // macOS-style dock
    Ribbon,      // Office-style ribbon
    StatusBar,   // Status bar at bottom
    Sidebar      // Vertical sidebar
};
```

### ToolbarItemType

```cpp
enum class ToolbarItemType {
    Button, ToggleButton, DropdownButton, SplitButton,
    Separator, Spacer, Label, TextInput, Dropdown,
    Checkbox, RadioButton, CustomWidget, ButtonGroup, SearchBox
};
```

### ToolbarOverflowMode

```cpp
enum class ToolbarOverflowMode {
    OverflowNone, Wrap, Menu, Scroll, Hide
};
```

### ToolbarIconSize

```cpp
enum class ToolbarIconSize {
    Small,      // 16x16
    Medium,     // 24x24
    Large,      // 32x32
    ExtraLarge, // 48x48
    Huge,       // 64x64
    Custom      // user-defined
};
```

## ToolbarAppearance Structure

```cpp
struct ToolbarAppearance {
    ToolbarStyle style = ToolbarStyle::Standard;

    Color foregroundColor          = Colors::Black;
    Color backgroundColor          = Color(245, 245, 245, 255);
    Color separatorColor           = Color(200, 200, 200, 255);
    Color hoverBackgroundColor     = Color(225, 235, 255, 255);
    Color activeBackgroundColor    = Color(204, 228, 247, 255);
    Color disabledBackgroundColor  = Color(220, 220, 220, 255);

    float itemSpacing  = 4.0f;
    float groupSpacing = 8.0f;

    // Shadow
    bool     hasShadow     = false;
    Color    shadowColor   = Color(0, 0, 0, 60);
    Point2Di shadowOffset  = Point2Di(0, 2);
    float    shadowBlur    = 4.0f;

    // Animation
    bool  enableAnimations  = true;
    float animationDuration = 0.2f;

    // Icons
    ToolbarIconSize iconSize        = ToolbarIconSize::Medium;
    int             customIconWidth  = 24;
    int             customIconHeight = 24;
    bool            showIconLabels   = true;
    bool            centerIcons      = false;

    // Dock magnification
    bool  enableMagnification  = false;
    float magnificationScale   = 1.5f;
    float magnificationRadius  = 100.0f;

    // Presets
    static ToolbarAppearance Default();
    static ToolbarAppearance Flat();
    static ToolbarAppearance MacOSDock();
    static ToolbarAppearance Ribbon();
    static ToolbarAppearance StatusBar();
    static ToolbarAppearance Sidebar();
};
```

## ToolbarItemDescriptor

A declarative description that the toolbar turns into a concrete item.

```cpp
struct ToolbarItemDescriptor {
    ToolbarItemType type = ToolbarItemType::Button;
    std::string identifier;
    std::string text;
    std::string iconPath;
    std::string tooltip;

    bool isToggle  = false;
    bool isChecked = false;
    bool isEnabled = true;
    bool isVisible = true;

    int visibilityPriority = 0;   // Higher = stays visible longer in overflow

    std::vector<std::string> dropdownItems;

    std::function<void()>                    onClick;
    std::function<void(bool)>                onToggle;
    std::function<void(const std::string&)>  onDropdownSelect;
    std::function<void(const std::string&)>  onTextChange;

    int   minWidth   = 0;
    int   maxWidth   = 0;
    int   fixedWidth = 0;
    float stretch    = 0.0f;

    // Badge
    bool        hasBadge   = false;
    std::string badgeText;
    Color       badgeColor = Color(255, 0, 0, 255);

    // Factory methods
    static ToolbarItemDescriptor CreateButton(const std::string& id, const std::string& text,
                                              const std::string& icon = "",
                                              std::function<void()> onClick = nullptr);
    static ToolbarItemDescriptor CreateToggleButton(const std::string& id, const std::string& text,
                                                    const std::string& icon = "",
                                                    std::function<void(bool)> onToggle = nullptr);
    static ToolbarItemDescriptor CreateDropdown(const std::string& id, const std::string& text,
                                                const std::vector<std::string>& items,
                                                std::function<void(const std::string&)> onSelect = nullptr);
    static ToolbarItemDescriptor CreateSeparator(const std::string& id = "");
    static ToolbarItemDescriptor CreateSpacer(int size = 8);
    static ToolbarItemDescriptor CreateFlexSpacer(float stretch = 1.0f);
    static ToolbarItemDescriptor CreateLabel(const std::string& id, const std::string& text);
};
```

## Events and Callbacks

```cpp
std::function<void()>                       onToolbarShow;
std::function<void()>                       onToolbarHide;
std::function<void(const std::string&)>     onItemAdded;
std::function<void(const std::string&)>     onItemRemoved;
std::function<void(int, int)>               onItemReordered;
std::function<void(ToolbarPosition)>        onPositionChanged;
```

Per-item callbacks live on the descriptors or the typed item objects (`UltraCanvasToolbarButton::SetOnClick`, `SetOnToggle`, etc.).

## Builder Pattern

```cpp
auto toolbar = UltraCanvasToolbarBuilder("MyToolbar")
    .SetOrientation(ToolbarOrientation::Horizontal)
    .SetAppearance(ToolbarAppearance::Default())
    .SetDimensions(20, 100, 960, 48)
    .AddButton("new",   "", "media/icons/new-icon.png",   [](){ /* ... */ })
    .AddButton("open",  "", "media/icons/open-icon.png",  [](){ /* ... */ })
    .AddSeparator()
    .AddStretch(1.0f)
    .AddLabel("status", "Ready")
    .Build();
```

## Preset Factories

```cpp
namespace ToolbarPresets {
    std::shared_ptr<UltraCanvasToolbar> CreateStandardToolbar(const std::string& id = "Toolbar");
    std::shared_ptr<UltraCanvasToolbar> CreateDockStyleToolbar(const std::string& id = "Dock");
    std::shared_ptr<UltraCanvasToolbar> CreateRibbonToolbar(const std::string& id = "Ribbon");
    std::shared_ptr<UltraCanvasToolbar> CreateSidebarToolbar(const std::string& id = "Sidebar");
    std::shared_ptr<UltraCanvasToolbar> CreateStatusBar(const std::string& id = "StatusBar");
}
```

## Usage Examples

### Standard Horizontal Toolbar (Builder)

```cpp
auto standardToolbar = UltraCanvasToolbarBuilder("StandardToolbar")
    .SetOrientation(ToolbarOrientation::Horizontal)
    .SetAppearance(ToolbarAppearance::Default())
    .SetDimensions(20, 100, 960, 48)
    .AddButton("new",   "", "media/icons/new-icon.png",
               [](){ std::cerr << "New clicked"   << std::endl; })
    .AddButton("open",  "", "media/icons/open-icon.png",
               [](){ std::cerr << "Open clicked"  << std::endl; })
    .AddButton("save",  "", "media/icons/save-icon.png",
               [](){ std::cerr << "Save clicked"  << std::endl; })
    .AddSeparator()
    .AddButton("cut",   "", "media/icons/cut-icon.png",   [](){})
    .AddButton("copy",  "", "media/icons/copy-icon.png",  [](){})
    .AddButton("paste", "", "media/icons/paste-icon.png", [](){})
    .AddSeparator()
    .AddStretch(1.0f)             // Pushes status label to the right
    .AddLabel("status", "Ready")
    .Build();
container->AddChild(standardToolbar);
```

### Toolbar with Dropdowns and Toggles

```cpp
auto dropdownToolbar = std::make_shared<UltraCanvasToolbar>(
    "DropdownToolbar", 20, 200, 960, 48);
dropdownToolbar->SetOrientation(ToolbarOrientation::Horizontal);
dropdownToolbar->SetAppearance(ToolbarAppearance::Default());

dropdownToolbar->AddButton("undo", "", "media/icons/undo-icon.png",
                           [](){ std::cerr << "Undo" << std::endl; });
dropdownToolbar->AddButton("redo", "", "media/icons/redo-icon.png",
                           [](){ std::cerr << "Redo" << std::endl; });
dropdownToolbar->AddSeparator();

std::vector<std::string> fonts = {
    "Arial", "Times New Roman", "Courier New", "Verdana", "Georgia"
};
dropdownToolbar->AddDropdownButton("font", "Arial", fonts,
    [](const std::string& selected) {
        std::cerr << "Font selected: " << selected << std::endl;
    });

dropdownToolbar->AddSpacer(10);

std::vector<std::string> sizes = {"8", "10", "12", "14", "16", "18", "20", "24"};
dropdownToolbar->AddDropdownButton("size", "12", sizes,
    [](const std::string& selected) {
        std::cerr << "Size selected: " << selected << std::endl;
    });

dropdownToolbar->AddSeparator();

dropdownToolbar->AddToggleButton("bold", "B", "", [](bool checked) {
    std::cerr << "Bold: " << (checked ? "ON" : "OFF") << std::endl;
});
dropdownToolbar->AddToggleButton("italic", "I", "", [](bool checked) {
    std::cerr << "Italic: " << (checked ? "ON" : "OFF") << std::endl;
});
dropdownToolbar->AddToggleButton("underline", "U", "", [](bool checked) {
    std::cerr << "Underline: " << (checked ? "ON" : "OFF") << std::endl;
});
```

### Flat Style Toolbar

```cpp
auto flatToolbar = UltraCanvasToolbarBuilder("FlatToolbar")
    .SetOrientation(ToolbarOrientation::Horizontal)
    .SetAppearance(ToolbarAppearance::Flat())
    .SetDimensions(20, 300, 960, 48)
    .AddButton("home",     "", "media/icons/home-icon.png",     [](){})
    .AddButton("profile",  "", "media/icons/profile-icon.png",  [](){})
    .AddButton("settings", "", "media/icons/settings.png",      [](){})
    .AddStretch(1.0f)
    .AddButton("notifications", "", "media/icons/bell-icon.png",     [](){})
    .AddButton("messages",      "", "media/icons/envelope-icon.png", [](){})
    .Build();
```

### Vertical Sidebar Toolbar

```cpp
auto sidebarContainer = std::make_shared<UltraCanvasContainer>(
    "SidebarDemo", 20, 400, 960, 240);
sidebarContainer->SetBackgroundColor(Color(245, 245, 245, 255));
sidebarContainer->SetBorders(1, Color(220, 220, 220, 255));

auto verticalToolbar = UltraCanvasToolbarBuilder("VerticalToolbar")
    .SetOrientation(ToolbarOrientation::Vertical)
    .SetAppearance(ToolbarAppearance::Sidebar())
    .SetDimensions(10, 10, 50, 220)
    .AddButton("dashboard", "📊", "", [](){ /* ... */ })
    .AddButton("files",     "📁", "", [](){ /* ... */ })
    .AddButton("users",     "👥", "", [](){ /* ... */ })
    .AddSeparator()
    .AddButton("analytics", "📈", "", [](){ /* ... */ })
    .AddButton("reports",   "📋", "", [](){ /* ... */ })
    .Build();

sidebarContainer->AddChild(verticalToolbar);
```

### Ribbon-Style Toolbar (Grouped)

`AddLabel(...)` followed by buttons and `AddSpacer(...)` creates labeled groups that read like a Microsoft Office ribbon.

```cpp
auto ribbonToolbar = UltraCanvasToolbarBuilder("RibbonToolbar")
    .SetOrientation(ToolbarOrientation::Horizontal)
    .SetAppearance(ToolbarAppearance::Ribbon())
    .SetDimensions(20, 700, 960, 58)

    .AddLabel("clipboard", "Clipboard")
    .AddSpacer(5)
    .AddButton("cut",   "", "media/icons/cut-icon.png",   [](){})
    .AddButton("copy",  "", "media/icons/copy-icon.png",  [](){})
    .AddButton("paste", "", "media/icons/paste-icon.png", [](){})
    .AddSpacer(20)

    .AddLabel("format", "Format")
    .AddSpacer(5)
    .AddButton("bold",      "B", "", [](){})
    .AddButton("italic",    "I", "", [](){})
    .AddButton("underline", "U", "", [](){})
    .AddSpacer(20)

    .AddLabel("insert", "Insert")
    .AddSpacer(5)
    .AddButton("image", "", "media/icons/image-icon.png", [](){})
    .AddButton("table", "", "media/icons/table-icon.png", [](){})
    .AddButton("chart", "", "media/icons/chart-icon.png", [](){})
    .Build();
```

### Item Descriptor with Badge

```cpp
ToolbarItemDescriptor notif = ToolbarItemDescriptor::CreateButton(
    "notifications", "", "media/icons/bell-icon.png",
    [](){ /* open notifications */ });
notif.hasBadge   = true;
notif.badgeText  = "3";
notif.badgeColor = Color(255, 0, 0, 255);

toolbar->AddItem(notif);
```

## Implementation Notes

- Use `UltraCanvasToolbarBuilder` for fluent construction; for one-off items, the convenience methods on the toolbar itself are equivalent.
- `AddSpacer(size)` inserts a fixed-width spacer; `AddStretch(weight)` (or `ToolbarItemDescriptor::CreateFlexSpacer`) inserts a flexible spacer that consumes remaining space.
- `AddSeparator()` adds a thin visual divider that adapts to the toolbar's orientation.
- Horizontal and vertical orientations are fully supported by all styles.
- Built-in styles: `Standard`, `Flat`, `Docked` (macOS dock), `Ribbon`, `Sidebar`, `StatusBar`.

## See Also

- [UltraCanvasButton](UltraCanvasButton.md) - Building block for toolbar buttons
- [UltraCanvasMenu](UltraCanvasMenu.md) - Used internally for dropdown buttons and overflow menus
- [UltraCanvasContainer](UltraCanvasContainer.md) - Base container class
- [UltraCanvasLayoutExamples](UltraCanvasLayoutExamples.md) - Layout managers used internally
- [UltraCanvasSegmentedControl](UltraCanvasSegmentedControl.md) - Alternative for grouped toggle choices

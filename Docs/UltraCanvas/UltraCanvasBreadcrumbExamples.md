# UltraCanvasBreadcrumb Documentation

## Overview

**UltraCanvasBreadcrumb** is a hierarchical navigation control that renders a path of clickable segments separated by configurable separators. It supports per-item icons, per-item dropdown menus, several built-in style presets, and four different overflow strategies (clip, collapse, ellipsize, shrink-text) for narrow containers.

**Version:** 1.0.0
**Header:** `include/UltraCanvasBreadcrumb.h`
**Namespace:** `UltraCanvas`
**Base Class:** `UltraCanvasUIElement`

## Features

- **Path-based API**: Build a breadcrumb from a string with `SetPath`
- **Item-based API**: Add individual items with icons, dropdowns, and per-item callbacks
- **Multiple Separator Styles**: Chevron, slash, arrow, bullet, pipe, dot, custom text/icon
- **Item Styles**: Plain, Pill, Underline, and Tab rendering
- **Overflow Handling**: Collapse middle items into a `...` menu, ellipsize text, or shrink
- **Style Presets**: Default, Compact, Pills, FileExplorer, WebDocs
- **Builder Pattern**: Fluent `BreadcrumbBuilder` interface
- **Navigation Helpers**: `RemoveItemsAfter` for "click to navigate up" patterns

## Header Include

```cpp
#include "UltraCanvasBreadcrumb.h"
```

## Class Reference

### Constructor

```cpp
UltraCanvasBreadcrumb(const std::string& identifier = "Breadcrumb",
                      long x = 0, long y = 0,
                      long w = 400, long h = 28);
```

### Factory Function

```cpp
std::shared_ptr<UltraCanvasBreadcrumb> CreateBreadcrumb(
    const std::string& identifier,
    long x, long y, long w = 400, long h = 28);
```

### BreadcrumbItem Structure

```cpp
struct BreadcrumbItem {
    std::string id;                                  // Optional unique identifier
    std::string text;                                // Display text
    std::shared_ptr<UCImage> icon;                   // Optional leading icon
    std::string tooltip;
    bool clickable    = true;
    bool hasDropdown  = false;
    std::vector<MenuItemData> dropdownItems;
    std::function<std::vector<MenuItemData>()> dropdownItemsProvider; // Lazy
    std::function<void()> onClick;
    void* userData    = nullptr;

    // Static helpers
    static BreadcrumbItem WithIcon(const std::string& text, const std::string& iconPath);
    static BreadcrumbItem IconOnly(const std::string& iconPath, const std::string& tooltip = "");
    static BreadcrumbItem WithDropdown(const std::string& text,
                                       const std::vector<MenuItemData>& items);
};
```

### Item Management

```cpp
void AddItem(const BreadcrumbItem& item);
void AddItem(const std::string& text);
void AddItem(const std::string& text, std::function<void()> onClick);
void InsertItem(int index, const BreadcrumbItem& item);
bool RemoveItem(int index);
bool RemoveItemById(const std::string& id);
void RemoveItemsAfter(int index);    // Useful for "navigate up"
void Clear();

size_t GetItemCount() const;
bool   IsEmpty() const;
BreadcrumbItem*       GetItem(int index);
const BreadcrumbItem* GetItem(int index) const;
BreadcrumbItem*       GetItemById(const std::string& id);
int                   GetItemIndexById(const std::string& id) const;
const std::vector<BreadcrumbItem>& GetItems() const;
```

### Path Helpers

```cpp
// Splits a path by `separator` and creates one item per non-empty segment.
void SetPath(const std::string& path, char separator = '/');

// Joins item texts using `separator`.
std::string GetPath(char separator = '/') const;

void SetItems(std::vector<BreadcrumbItem> newItems);
```

### Current Item

```cpp
void SetCurrentIndex(int index);    // Defaults to last item
int  GetCurrentIndex() const;
```

### Style Configuration

```cpp
void SetStyle(const BreadcrumbStyle& newStyle);
const BreadcrumbStyle& GetStyle() const;
BreadcrumbStyle& GetStyle();

void SetSeparatorStyle(BreadcrumbSeparatorStyle s);
void SetCustomSeparatorText(const std::string& text);
void SetCustomSeparatorIcon(const std::string& iconPath);
void SetItemStyle(BreadcrumbItemStyle s);
void SetOverflowMode(BreadcrumbOverflowMode mode);
void SetFont(const std::string& family, float size,
             FontWeight weight = FontWeight::Normal);
void SetMaxItemTextWidth(int maxWidth);
```

## Enumerations

### BreadcrumbSeparatorStyle

```cpp
enum class BreadcrumbSeparatorStyle {
    Chevron,        // Drawn ">" shape (vector path)
    ChevronDouble,  // Drawn ">>" shape
    Slash,          // "/"
    BackSlash,      // "\"
    GreaterThan,    // ">"
    Arrow,          // "->"
    Bullet,         // "*"
    Pipe,           // "|"
    Dot,            // "."
    NoSeparator,
    CustomText,     // Use customSeparatorText
    CustomIcon      // Use customSeparatorIcon
};
```

### BreadcrumbOverflowMode

```cpp
enum class BreadcrumbOverflowMode {
    Clip,        // Clip whatever doesn't fit
    Collapse,    // Hide middle items behind a "..." menu
    Ellipsize,   // Trim per-item text
    ShrinkText   // Reduce maxTextWidth until everything fits
};
```

### BreadcrumbItemStyle

```cpp
enum class BreadcrumbItemStyle {
    Plain,       // Text only, background on hover
    Pill,        // Rounded background
    Underline,   // Underline on hover
    Tab          // Tab-like with bottom border
};
```

## Style Presets

```cpp
BreadcrumbStyle::Default();
BreadcrumbStyle::Compact();
BreadcrumbStyle::Pills();
BreadcrumbStyle::FileExplorer();
BreadcrumbStyle::WebDocs();
```

## Events and Callbacks

```cpp
// Fired when a non-current item is clicked. `index` is the item's position.
std::function<void(int index, const BreadcrumbItem& item)> onItemClicked;

// Fired when the dropdown chevron of an item is clicked.
std::function<void(int index, const BreadcrumbItem& item)> onItemDropdown;

// Fired when the overflow ("...") menu is opened.
std::function<void()> onOverflowOpened;

// Fired when the path changes via SetPath / Add / Remove / Clear.
std::function<void()> onPathChanged;
```

Each `BreadcrumbItem` also carries its own `onClick` callback, which fires alongside the breadcrumb-level `onItemClicked`.

## Builder Pattern

```cpp
auto bc = BreadcrumbBuilder("MyBC", 10, 10, 600, 30)
    .SetStyle(BreadcrumbStyle::Pills())
    .AddItem("Home")
    .AddItem("Projects")
    .AddItem("UltraCanvas")
    .OnItemClicked([](int i, const BreadcrumbItem& item) {
        std::cerr << "clicked " << item.text << std::endl;
    })
    .Build();
```

## Usage Examples

### Default Breadcrumb

```cpp
auto bcDefault = CreateBreadcrumb("bc_default", 380, 90, 580, 30);
bcDefault->AddItem("Home");
bcDefault->AddItem("Documents");
bcDefault->AddItem("Projects");
bcDefault->AddItem("UltraCanvas");
bcDefault->onItemClicked = [](int idx, const BreadcrumbItem& item) {
    std::cerr << "Clicked '" << item.text << "' (index " << idx << ")" << std::endl;
};
container->AddChild(bcDefault);
```

### Style Presets

```cpp
// Compact (smaller font, tighter padding)
auto bcCompact = CreateBreadcrumb("bc_compact", 380, 150, 580, 24);
bcCompact->SetStyle(BreadcrumbStyle::Compact());
bcCompact->AddItem("Home");
bcCompact->AddItem("Documents");
bcCompact->AddItem("Projects");

// Pills (rounded backgrounds, no separators)
auto bcPills = CreateBreadcrumb("bc_pills", 380, 210, 580, 32);
bcPills->SetStyle(BreadcrumbStyle::Pills());
bcPills->AddItem("Dashboard");
bcPills->AddItem("Reports");
bcPills->AddItem("Q4 2025");
bcPills->AddItem("Summary");

// Web docs (slash separators, underline-on-hover)
auto bcWeb = CreateBreadcrumb("bc_web", 380, 330, 580, 26);
bcWeb->SetStyle(BreadcrumbStyle::WebDocs());
bcWeb->AddItem("Docs");
bcWeb->AddItem("Guides");
bcWeb->AddItem("UI Components");
bcWeb->AddItem("Breadcrumb");
```

### Building a Breadcrumb from a Path String

```cpp
auto bcExplorer = CreateBreadcrumb("bc_explorer", 380, 270, 580, 28);
bcExplorer->SetStyle(BreadcrumbStyle::FileExplorer());
bcExplorer->SetPath("/usr/local/share/applications", '/');
```

### Iterating Separator Styles

```cpp
struct SepEntry {
    BreadcrumbSeparatorStyle style;
    const char* label;
};
const SepEntry sepEntries[] = {
    {BreadcrumbSeparatorStyle::Chevron,       "Chevron"},
    {BreadcrumbSeparatorStyle::ChevronDouble, "ChevronDouble"},
    {BreadcrumbSeparatorStyle::Slash,         "Slash"},
    {BreadcrumbSeparatorStyle::GreaterThan,   "GreaterThan"},
    {BreadcrumbSeparatorStyle::Arrow,         "Arrow"},
    {BreadcrumbSeparatorStyle::Bullet,        "Bullet"},
    {BreadcrumbSeparatorStyle::Pipe,          "Pipe"},
    {BreadcrumbSeparatorStyle::Dot,           "Dot"},
};

int y = 500;
for (const auto& entry : sepEntries) {
    auto bc = CreateBreadcrumb(std::string("bc_sep_") + entry.label,
                               490, y, 470, 26);
    bc->SetSeparatorStyle(entry.style);
    bc->AddItem("Alpha");
    bc->AddItem("Beta");
    bc->AddItem("Gamma");
    bc->AddItem("Delta");
    container->AddChild(bc);
    y += 30;
}
```

### Item with Dropdown Menu

```cpp
auto bcDropdown = CreateBreadcrumb("bc_dropdown", 380, 760, 580, 30);
bcDropdown->AddItem("Workspace");

std::vector<MenuItemData> projectsMenu = {
    MenuItemData::Action("Open UltraCanvas", []() { /* ... */ }),
    MenuItemData::Action("Open DemoApp",     []() { /* ... */ }),
    MenuItemData::Action("Open Texter",      []() { /* ... */ }),
    MenuItemData::Separator(),
    MenuItemData::Action("New project...",   []() { /* ... */ }),
};

bcDropdown->AddItem(BreadcrumbItem::WithDropdown("Projects", projectsMenu));
bcDropdown->AddItem("UltraCanvas");
bcDropdown->AddItem("Source");

bcDropdown->onItemDropdown = [](int idx, const BreadcrumbItem& item) {
    std::cerr << "Dropdown chevron clicked on '" << item.text << "'" << std::endl;
};
```

### Items with Icons

```cpp
const std::string iconsRoot = "media/icons/";

auto bcIcons = CreateBreadcrumb("bc_icons", 380, 820, 580, 30);
bcIcons->AddItem(BreadcrumbItem::IconOnly(iconsRoot + "home-icon.png", "Home"));
bcIcons->AddItem(BreadcrumbItem::WithIcon("Documents", iconsRoot + "folder.png"));
bcIcons->AddItem(BreadcrumbItem::WithIcon("Projects",  iconsRoot + "folder.png"));
bcIcons->AddItem(BreadcrumbItem::WithIcon("Report.txt", iconsRoot + "document.png"));
```

### Live Navigation (Truncate Path on Click)

`RemoveItemsAfter(index)` is the idiomatic way to implement "click an ancestor to navigate up":

```cpp
const std::string initialPath = "/home/user/projects/ultracanvas/Apps/DemoApp";
auto bcNav = CreateBreadcrumb("bc_nav", 380, 880, 490, 30);
bcNav->SetPath(initialPath, '/');

// Capture by weak_ptr to avoid creating reference cycles.
std::weak_ptr<UltraCanvasBreadcrumb> bcNavWeak = bcNav;

bcNav->onItemClicked = [bcNavWeak](int idx, const BreadcrumbItem& item) {
    auto bc = bcNavWeak.lock();
    if (!bc) return;
    bc->RemoveItemsAfter(idx);                  // Truncate
    std::cerr << "New path: " << bc->GetPath('/') << std::endl;
};
```

### Overflow: Collapse Middle Items

When the breadcrumb is too narrow for all items, middle items are folded into an "..." menu.

```cpp
auto bcCollapse = CreateBreadcrumb("bc_collapse", 380, 940, 360, 30);
bcCollapse->SetOverflowMode(BreadcrumbOverflowMode::Collapse);
bcCollapse->AddItem("Root");
bcCollapse->AddItem("Engineering");
bcCollapse->AddItem("Frameworks");
bcCollapse->AddItem("Graphics");
bcCollapse->AddItem("2D");
bcCollapse->AddItem("Cairo");
bcCollapse->AddItem("Renderers");
bcCollapse->AddItem("Linux");
bcCollapse->AddItem("X11");

bcCollapse->onOverflowOpened = []() {
    std::cerr << "Overflow menu opened" << std::endl;
};
```

### Overflow: Ellipsize per-item Text

```cpp
auto bcEllipsize = CreateBreadcrumb("bc_ellipsize", 380, 1000, 580, 30);
bcEllipsize->SetOverflowMode(BreadcrumbOverflowMode::Ellipsize);
bcEllipsize->SetMaxItemTextWidth(60);
bcEllipsize->AddItem("A Very Long Workspace Name");
bcEllipsize->AddItem("Quarterly Financial Reports");
bcEllipsize->AddItem("Department of Cross-Platform Engineering");
bcEllipsize->AddItem("Final Approved Version 2025-Q4");
```

### Overflow: ShrinkText

```cpp
auto bcShrink = CreateBreadcrumb("bc_shrink", 380, 1060, 420, 30);
bcShrink->SetOverflowMode(BreadcrumbOverflowMode::ShrinkText);
bcShrink->AddItem("Customers");
bcShrink->AddItem("Acme Corporation");
bcShrink->AddItem("North America Division");
bcShrink->AddItem("Quarterly Reports");
bcShrink->AddItem("2026 Q2 Final");
```

### Custom Rounded Strip Style

```cpp
auto bcStrip = CreateBreadcrumb("bc_strip", 380, 1120, 420, 32);

BreadcrumbStyle stripStyle = BreadcrumbStyle::Default();
stripStyle.backgroundColor       = Color(243, 243, 243, 255);
stripStyle.cornerRadius          = 4.0f;
stripStyle.separatorStyle        = BreadcrumbSeparatorStyle::Chevron;
stripStyle.separatorColor        = Color(140, 140, 140, 255);
stripStyle.itemTextColor         = Color(60, 60, 60, 255);
stripStyle.currentItemTextColor  = stripStyle.itemTextColor;
stripStyle.currentItemBold       = false;
stripStyle.currentItemClickable  = true;
stripStyle.itemPaddingHorizontal = 10;
stripStyle.separatorSpacing      = 8;

bcStrip->SetStyle(stripStyle);
bcStrip->AddItem("PlantUML");
bcStrip->AddItem("Language specification");
bcStrip->AddItem("Yaml Diagram");
```

## See Also

- [UltraCanvasMenu](UltraCanvasMenu.md) - Used internally for dropdown and overflow menus
- [UltraCanvasButton](UltraCanvasButton.md) - For "back" / "up" navigation buttons
- [UltraCanvasLabel](UltraCanvasLabel.md) - Companion text element
- [UltraCanvasContainer](UltraCanvasContainer.md) - Hosting container

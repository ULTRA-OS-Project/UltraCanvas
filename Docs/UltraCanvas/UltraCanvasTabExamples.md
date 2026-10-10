# UltraCanvasTabbedContainer Documentation

## Overview

The **UltraCanvasTabbedContainer** is an advanced tabbed interface component in the UltraCanvas framework that provides rich functionality for organizing content in tabs. It features automatic overflow handling with dropdown menus, integrated search capabilities, multiple tab positioning options, and extensive customization possibilities.

**Version:** 1.7.1  
**Last Modified:** 2026-10-07  
**Author:** UltraCanvas Framework

<!-- doc-check: std::shared_ptr<UltraCanvasUIElement> MakePage(); -->

## Features

### Core Features
- **Multiple Tab Positions:** Top, Bottom, Left, Right
- **Tab Styles:** Classic, Modern, Flat, Rounded, Custom
- **Overflow Management:** Automatic dropdown when tabs exceed available space
- **Search Functionality:** Real-time filtering of tabs in dropdown
- **Tab Reordering:** Drag-and-drop tab repositioning (off by default)
- **Drag Out / Drag In:** Tabs can be dragged out of the bar and transferred between containers
- **Close Buttons:** Configurable close button behavior
- **Keyboard Navigation:** Arrow keys, shortcuts, and search input
- **Event System:** Comprehensive callbacks for all user interactions
- **Content Management:** Each tab can contain any UltraCanvas UI element
- **New Tab Button:** An optional "+" before, after or at the far right of the tab list
- **Detached Content:** The tab strip can live apart from the pages it switches

### Enhanced Dropdown Features
- **Smart Overflow Detection:** Automatically shows dropdown when tabs don't fit
- **Position Control:** Left or Right side dropdown positioning
- **Search Integration:** With search on and enough tabs, the overflow button
  opens a "Search tabs..." popup; otherwise it opens a plain menu of the tabs
- **Visual Markers:** Disabled tabs are listed in `[brackets]` in the search
  popup and greyed out in the plain menu
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
    Pill,     // Capsules floating in the bar: the open tab outlined, the others plain
    Custom    // User-defined styling
};
```

`Pill` is the browser-chip look: every tab is a capsule inset in its slot of
the tab bar, the open one filled with `activeTabColor` and outlined with
`activeTabBorderColor`, an inactive one filled with `inactiveTabColor` (make it
transparent for a text-only tab) and outlined with `inactiveTabBorderColor`, a
hovered one with `hoveredTabColor` / `hoveredTabBorderColor`. The page below
gets no frame, only a hairline in `tabContentBorderColor` on the bar's side.
See [Pill Style](#pill-style-tabstylepill) for the colourways.

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

> **Note:** The overflow dropdown is only supported for horizontal tab layouts
> (`TabPosition::Top`/`Bottom`). For vertical layouts (`TabPosition::Left`/`Right`)
> it is automatically deactivated — the overflow button is never shown, and
> enabling it emits the warning "Overflow dropdown not supported for vertical
> tabs". Use tab scrolling for vertical layouts instead.

### NewTabButtonPosition
Where the optional "+" button sits in the tab bar.

```cpp
enum class NewTabButtonPosition {
    AfterTabs,  // Right behind the last tab (default) - moves with the list
    FarRight,   // Pinned to the right end of the tab bar
    BeforeTabs  // In front of the first tab
};
```

### NewTabButtonShape
How the "+" button is drawn inside its slot.

```cpp
enum class NewTabButtonShape {
    RoundedSquare,  // Square with rounded corners (default)
    Circle          // Round button
};
```

## TabData Structure

Represents individual tab properties. The container keeps one per tab in its
public `tabs` vector; use the per-tab setters below rather than editing it.

```cpp
struct TabData {
    std::string title;                        // Tab label text
    std::string tooltip;                      // Hover tooltip text
    std::string iconPath;                     // Path to tab icon (16x16 recommended)
    std::shared_ptr<UCImage> iconImage;       // Decoded icon, drawn instead of iconPath
    std::shared_ptr<UCImageAnimationController> iconAnimation; // Animated icon (e.g. spinner)
    std::string badgeText;
    int badgeWidth = 0;
    int badgeHeight = 0;
    bool enabled = true;                      // Interactive state
    bool visible = true;                      // Visibility state
    bool closable = true;                     // Can be closed
    bool hasIcon = false;
    bool showBadge = false;
    bool showMarker = false;                  // Dot marker (e.g. "modified")
    Color markerColor = Colors::Transparent;  // Transparent = container default
    Color textColor = Colors::Black;          // Text color
    Color backgroundColor = Color(240, 240, 240); // Background color
    Color badgeBackgroundColor = Color(220, 50, 50);
    std::shared_ptr<UltraCanvasUIElement> content = nullptr; // Tab content
    void* userData = nullptr;                 // Custom user data

    TabData(const std::string& tabTitle);
};
```

## Public Methods

### Construction and Initialization

```cpp
UltraCanvasTabbedContainer(const std::string& elementId, float posX, float posY, float w, float h);
UltraCanvasTabbedContainer(const std::string& elementId, float w, float h);   // position -1, -1
explicit UltraCanvasTabbedContainer(const std::string& elementId);            // size -1, -1 (layout decides)
```

### Tab Management

#### Adding Tabs
```cpp
int AddTab(const std::string& title, std::shared_ptr<UltraCanvasUIElement> content = nullptr);
```
Adds a new tab with specified title and content. Returns the index of the new tab.

#### Removing Tabs
```cpp
void RemoveTab(int index);
```
Removes the tab at the specified index. Calls `onTabClose` first; if it returns
false the tab stays.

#### Setting Active Tab
```cpp
void SetActiveTab(int index);
```
Switches to the specified tab index (ignored for a disabled tab).

### Tab Properties

```cpp
void SetTabTitle(int index, const std::string& title);
std::string GetTabTitle(int index) const;
void SetTabTooltip(int index, const std::string& tooltip);
std::string GetTabTooltip(int index) const;
void SetTabEnabled(int index, bool enabled);
bool IsTabEnabled(int index) const;
void SetTabContent(int index, std::shared_ptr<UltraCanvasUIElement> content);
std::shared_ptr<UltraCanvasUIElement> GetTabContent(int index) const;
```

### Container Configuration

#### Tab Appearance
```cpp
void SetTabHeight(int th);                 // Default: 32
void SetTabMinWidth(int w);                // Default: 80
void SetTabMaxWidth(int w);                // Default: 200
void SetTabPosition(TabPosition position); // Default: Top
void SetTabStyle(TabStyle style);          // Default: Rounded
void SetCloseMode(TabCloseMode mode);      // Default: NoClose
void SetTabBarColor(const Color& c);
void SetActiveTabBackgroundColor(const Color& c);
void SetActiveTabTextColor(const Color& c);
void SetInactiveTabBackgroundColor(const Color& c);
void SetInactiveTabTextColor(const Color& c);
void SetHoveredTabBackgroundColor(const Color& c);
```

#### Pill Style (TabStyle::Pill only)
```cpp
void SetPillInset(int horizontal, int vertical);   // Default: 2, 4 - the capsule inside its slot
int GetPillInsetX() const;
int GetPillInsetY() const;
void SetPillBorderWidth(float width);              // Default: 1
float GetPillBorderWidth() const;
void SetPillCornerRadius(float radius);            // Default: 0 = full capsule; > 0 = rounded chip
float GetPillCornerRadius() const;
void SetActiveTabBorderColor(const Color& c);      // Default: Color(184, 156, 255)
Color GetActiveTabBorderColor() const;
void SetInactiveTabBorderColor(const Color& c);    // Default: transparent
Color GetInactiveTabBorderColor() const;
void SetHoveredTabBorderColor(const Color& c);     // Default: transparent
Color GetHoveredTabBorderColor() const;
Rect2Di GetPillBounds(int index);                  // The capsule drawn for a tab, in local coordinates
```

#### Overflow Dropdown
```cpp
void SetOverflowDropdownPosition(OverflowDropdownPosition position); // Default: Off
void SetOverflowDropdownWidth(int width);          // Overflow button width. Default: 24 (min 16)
void SetDropdownSearchEnabled(bool enabled);       // Default: true
void SetDropdownSearchThreshold(int threshold);    // Default: 5 (min 1)
void ClearDropdownSearch();
bool UsesDropdownSearch() const;                   // What the overflow button opens next
```

The overflow button opens one of two lists of the visible tabs:

- the **search popup** ("Search tabs..."), when search is enabled and at
  least `GetDropdownSearchThreshold()` tabs are visible — the list is long
  enough to be worth filtering;
- a **plain menu** otherwise: one entry per visible tab, the active one
  checked and the disabled ones greyed out. Choosing an entry activates its
  tab; Escape or a click outside closes it.

`UsesDropdownSearch()` says which one a click opens now.

```cpp
auto tabs = CreateTabbedContainerWithDropdown("docs", 0, 0, 800, 600,
                                              OverflowDropdownPosition::Right);
tabs->SetDropdownSearchThreshold(8);   // a plain menu up to 7 tabs, search from 8
```

#### New Tab Button
```cpp
void SetShowNewTabButton(bool show);                        // Default: false
void SetNewTabButtonPosition(NewTabButtonPosition position); // Default: AfterTabs
void SetNewTabButtonWidth(int w);                           // Slot width in the tab bar. Default: 32
void SetNewTabButtonGap(int gap);                           // Space between the tabs and the slot. Default: 4
void SetNewTabButtonShape(NewTabButtonShape shape);         // Default: RoundedSquare
void SetNewTabButtonSize(int size);                         // Side of the shape, centred in the slot. Default: 24
void SetNewTabButtonCornerRadius(float radius);             // RoundedSquare corners. Default: 6
void SetNewButtonColor(const Color& c);                     // Idle fill of the shape. Default: (240, 240, 240)
Color newTabButtonHoverColor;                               // Fill while the mouse is over it
Color newTabButtonIconColor;                                // The "+" strokes
std::function<void()> onNewTabRequest;                      // Clicked
```

`NewTabButtonPosition::AfterTabs` draws the "+" directly behind the last tab
(so it walks along as tabs are added and removed), `FarRight` pins it to the
right end of the tab bar, `BeforeTabs` puts it in front of the first tab. The
space it needs — slot width plus gap — is reserved before the tabs are laid
out, so the button never overlaps a tab.

The button is not a full-height block of the tab bar: it is a small rounded
square (or a circle) centred in its slot, and only that shape is painted, idle
and hovered alike. Set the idle colour to the tab bar colour (or transparent)
for a browser-style "+" that only lights up on hover. The gap keeps the shape
clear of the neighbouring tab's outline, which is stroked half a pixel outside
the tab's bounds. The callback only reports the click — creating the tab is
the application's job:

```cpp
tabs->SetNewTabButtonPosition(NewTabButtonPosition::AfterTabs);
tabs->SetShowNewTabButton(true);
tabs->SetNewTabButtonShape(NewTabButtonShape::Circle);   // or RoundedSquare (default)
tabs->SetNewButtonColor(Colors::Transparent);            // highlight on hover only
tabs->newTabButtonHoverColor = Color(225, 225, 230);
tabs->onNewTabRequest = [tabs]() {
    tabs->SetActiveTab(tabs->AddTab("Untitled", MakePage()));
};
```

#### Detached Content Area
```cpp
void SetContentHost(const std::shared_ptr<UltraCanvasContainer>& host);
const std::shared_ptr<UltraCanvasContainer>& GetContentHost() const;
bool IsContentDetached() const;
```

By default a tabbed container is tab strip *and* content area in one element.
`SetContentHost()` splits the two: the pages become children of `host` instead,
`host`'s own layout sizes them, and the container itself shrinks to nothing but
the tab bar. That is what a browser-style layout needs — the tab strip as the
window's topmost bar, with toolbars between it and the pages it switches. Pages
that were already added move over with the call, so the host may be set before
or after the tabs; passing `nullptr` takes them back.

The host shows exactly one page at a time (the others are hidden, and hidden
elements are out of flow), so give it a stretching layout and its pages a
growing layout item, and give the strip a height of `GetTabHeight()`:

```cpp
auto tabs = std::make_shared<UltraCanvasTabbedContainer>("tabs", 0, 0, 0, 30);
tabs->SetTabHeight(30);
tabs->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

auto pages = std::make_shared<UltraCanvasContainer>("tab-pages");
pages->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
pages->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                 .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
tabs->SetContentHost(pages);

window->AddChild(tabs);          // the strip: the window's top bar
window->AddChild(toolbar);       // whatever belongs between the two
window->AddChild(pages);         // the active tab's page
```

Each page needs `layoutItem.SetFlexGrow(1)` (and `AlignSelf::Stretch`) so it
fills the host while it is the visible one. UltraFiler's folder tabs are built
this way — see `Apps/UltraFiler/UltraFilerWindow.cpp`.

### Query Methods

```cpp
int GetActiveTab() const;
int GetTabCount() const;
int GetTabHeight() const;
TabPosition GetTabPosition() const;
TabStyle GetTabStyle() const;
TabCloseMode GetCloseMode() const;
OverflowDropdownPosition GetOverflowDropdownPosition() const;
NewTabButtonPosition GetNewTabButtonPosition() const;
bool GetShowNewTabButton() const;
bool IsContentDetached() const;
```

## Event Callbacks

The container provides extensive callback support for user interactions:

```cpp
// Tab selection events
std::function<void(int, int)> onTabChange;           // (oldIndex, newIndex)
std::function<void(int)> onTabSelect;                // (tabIndex)

// Tab closure events
std::function<bool(int)> onTabClose;                 // (tabIndex) - return false to cancel

// Tab modification events
std::function<void(int, int)> onTabReorder;          // (fromIndex, toIndex)
std::function<void(int, const std::string&)> onTabRename; // (tabIndex, newTitle) - from SetTabTitle()

// Mouse events
std::function<void()> onTabBarRightClick;
std::function<void(int, int, int)> onTabContextMenu; // (tabIndex, windowX, windowY) on right-click
std::function<void(int)> onTabHover;                 // (tabIndex, or -1 when no tab is hovered)

// Drag-out / drag-in between containers
std::function<bool(int tabIndex, int screenX, int screenY)> onTabDragOut; // true = handler removed the tab
std::function<int(const TabTransferData& data, int insertionIndex)> onTabDragIn; // new index, or -1 to reject
```

## Styling Properties

### Color Properties
```cpp
Color tabBarColor = Colors::Transparent;
Color tabBorderColor = Colors::Gray;
Color activeTabBorderColor = Color(184, 156, 255);     // TabStyle::Pill: the open pill's outline
Color inactiveTabBorderColor = Colors::Transparent;    // TabStyle::Pill: the other pills' outline
Color hoveredTabBorderColor = Colors::Transparent;     // TabStyle::Pill: the hovered pill's outline
Color activeTabColor = Color(255, 255, 255);
Color activeTabTextColor = Colors::Black;
Color inactiveTabColor = Color(236, 236, 236);
Color inactiveTabTextColor = Color(80, 80, 80);
Color hoveredTabColor = Color(240, 240, 255);
Color disabledTabColor = Color(200, 200, 200);
Color disabledTabTextColor = Color(150, 150, 150);
Color closeButtonColor = Color(120, 120, 120);
Color closeButtonHoverColor = Color(200, 50, 50);
Color contentAreaColor = Color(255, 255, 255);
```

### Layout Properties
```cpp
int tabSpacing = 0;              // Space between tabs
int tabPadding = 12;             // Internal tab padding
int closeButtonSize = 16;        // Close button dimensions
int closeButtonMargin = 4;       // Close button spacing
bool allowTabReordering = false; // Enable drag-and-drop
bool enableTabScrolling = true;  // Enable scroll buttons
```

## Factory Functions

### CreateTabbedContainerWithDropdown
Creates a tabbed container with dropdown configuration:

```cpp
std::shared_ptr<UltraCanvasTabbedContainer> CreateTabbedContainerWithDropdown(
        const std::string& id, float x, float y, float width, float height,
        OverflowDropdownPosition dropdownPos = OverflowDropdownPosition::Left,
        bool enableSearch = true, int searchThreshold = 5);
```

```cpp
auto container = CreateTabbedContainerWithDropdown(
    "main_tabs",                          // ID
    10, 10, 980, 500,                     // Position and size
    OverflowDropdownPosition::Left,       // Dropdown position
    true,                                 // Enable search
    5                                     // Search threshold
);
```

### CreateTabbedContainer
Creates a basic tabbed container:

```cpp
std::shared_ptr<UltraCanvasTabbedContainer> CreateTabbedContainer(
        const std::string& id, float x, float y, float width, float height);
```

```cpp
auto container = CreateTabbedContainer(
    "tabs",                               // ID
    0, 0, 800, 600                        // Position and size
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
    "advancedTabs", 0, 0, 1024, 768,
    OverflowDropdownPosition::Right,
    true,  // Enable search
    10     // Search threshold
);

// Customize colors
tabs->activeTabColor = Color(0, 120, 215);
tabs->activeTabTextColor = Colors::White;
tabs->tabBarColor = Color(240, 248, 255);

// Set up event handlers
tabs->onTabChange = [](int oldIndex, int newIndex) {
    std::cerr << "Tab changed from " << oldIndex << " to " << newIndex << std::endl;
};

tabs->onTabClose = [](int index) {
    // Confirm before closing
    return NativeDialog::Confirm("Close this tab?");
};

// Enable reordering
tabs->allowTabReordering = true;
tabs->onTabReorder = [](int from, int to) {
    std::cerr << "Tab moved from " << from << " to " << to << std::endl;
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

### Pill Style (TabStyle::Pill)

The tab bar of a mail client or a browser: the open page is a white capsule
with a thin accent outline, the other pages are plain text until the pointer
hovers them, and the whole strip sits on a tinted bar. Everything about the
look is a colour or an inset, so the same style gives several colourways; the
test `Tests/TabPillStyleScreenshotTest.cpp` renders the five below side by
side (`ULTRACANVAS_SCREENSHOT_DIR=<dir> xvfb-run -a ./build/bin/TabPillStyleScreenshotTest`).

**1. Outlined pill on a tinted bar** - the reference look. The unselected tab
is nothing but icon and text; hovering it fades in a translucent white
capsule.

```cpp
auto tabs = CreateTabbedContainer("mail", 0, 0, 800, 600);
tabs->SetTabStyle(TabStyle::Pill);
tabs->SetTabHeight(36);
tabs->SetPillInset(2, 4);                                   // a 28px capsule in a 36px bar
tabs->SetCloseMode(TabCloseMode::Closable);
tabs->fontSize = 12;

tabs->SetTabBarColor(Color(234, 228, 247));                 // lavender strip
tabs->SetActiveTabBackgroundColor(Colors::White);
tabs->SetActiveTabBorderColor(Color(184, 156, 255));        // the accent outline
tabs->SetActiveTabTextColor(Color(36, 41, 46));
tabs->SetInactiveTabBackgroundColor(Colors::Transparent);   // unselected: text only
tabs->SetInactiveTabTextColor(Color(92, 85, 109));
tabs->SetHoveredTabBackgroundColor(Color(255, 255, 255, 140));
tabs->closeButtonColor = Color(92, 85, 109);
tabs->closeButtonHoverColor = Color(36, 41, 46);
tabs->tabContentBorderColor = Color(214, 208, 230);         // hairline under the bar

tabs->SetShowNewTabButton(true);                            // a "+" in the bar's colour
tabs->SetNewTabButtonShape(NewTabButtonShape::Circle);
tabs->newTabButtonColor = Colors::Transparent;
tabs->newTabButtonHoverColor = Color(255, 255, 255, 140);
tabs->newTabButtonIconColor = Color(92, 85, 109);

tabs->AddTab("Inbox", MakePage());
tabs->SetTabIcon(0, "icons/home-icon.png");
tabs->AddTab("UltraMail: Add/Delete account", MakePage());
tabs->SetTabIcon(1, "icons/settings.png");
```

**2. Tinted pill with accent outline** - a near-white bar, the open pill
filled with a pale shade of the accent.

```cpp
tabs->SetTabBarColor(Color(250, 250, 252));
tabs->SetActiveTabBackgroundColor(Color(241, 238, 255));
tabs->SetActiveTabBorderColor(Color(124, 92, 255));
tabs->SetActiveTabTextColor(Color(40, 30, 80));
tabs->SetInactiveTabBackgroundColor(Colors::Transparent);
tabs->SetInactiveTabTextColor(Color(90, 90, 100));
tabs->SetHoveredTabBackgroundColor(Color(238, 238, 243));
```

**3. Solid accent pill** - the open tab in the accent colour with white text,
the others as grey pills.

```cpp
tabs->SetTabBarColor(Color(245, 246, 248));
tabs->SetActiveTabBackgroundColor(Color(124, 92, 255));
tabs->SetActiveTabBorderColor(Colors::Transparent);
tabs->SetActiveTabTextColor(Colors::White);
tabs->SetInactiveTabBackgroundColor(Color(226, 228, 234));
tabs->SetInactiveTabTextColor(Color(60, 60, 70));
tabs->SetHoveredTabBackgroundColor(Color(212, 214, 222));
tabs->closeButtonColor = Color(150, 150, 165);
```

**4. Neutral chips** - squarer corners and a grey outline instead of an
accent; the unselected tabs are light grey chips.

```cpp
tabs->SetPillCornerRadius(6.0f);
tabs->SetTabBarColor(Color(252, 252, 253));
tabs->SetActiveTabBackgroundColor(Colors::White);
tabs->SetActiveTabBorderColor(Color(205, 205, 214));
tabs->SetInactiveTabBackgroundColor(Color(240, 241, 244));
tabs->SetInactiveTabTextColor(Color(90, 90, 100));
tabs->SetHoveredTabBackgroundColor(Color(232, 233, 237));
```

**5. Dark bar** - the same outline on a dark strip; the hover is a faint
white wash.

```cpp
tabs->SetTabBarColor(Color(30, 30, 36));
tabs->SetActiveTabBackgroundColor(Color(46, 46, 58));
tabs->SetActiveTabBorderColor(Color(184, 156, 255));
tabs->SetActiveTabTextColor(Color(240, 240, 245));
tabs->SetInactiveTabBackgroundColor(Colors::Transparent);
tabs->SetInactiveTabTextColor(Color(170, 170, 185));
tabs->SetHoveredTabBackgroundColor(Color(255, 255, 255, 24));
tabs->closeButtonColor = Color(170, 170, 185);
tabs->contentAreaColor = Color(24, 24, 30);
tabs->tabContentBorderColor = Color(60, 60, 72);
```

Geometry: the slot a tab occupies is still `GetTabBounds()` and is the whole
hit area, so a click in the gap between two pills lands on the nearer one;
`GetPillBounds()` is the capsule actually drawn. `tabPadding`, the icon and
the close button are measured from the slot edge as in every other style, so
the icon sits `tabPadding - pillInsetX` pixels inside the capsule. For wider
gaps between pills use `tabSpacing`, for a shorter capsule a larger vertical
inset. `TabPosition::Left` / `Right` stack the same capsules vertically.

## Keyboard Shortcuts

| Key Combination | Action |
|----------------|--------|
| Left Arrow | Navigate to previous tab |
| Right Arrow | Navigate to next tab |
| Ctrl+Tab / Ctrl+Shift+Tab | Next / previous tab (wraps around) |
| Ctrl+W | Close current tab (if closable) |
| Escape | Clear search / Close dropdown |
| Enter | Select highlighted search result |
| Backspace | Delete search character |
| Any character | Add to search filter (when dropdown open) |

## Event Flow

### Tab Selection
1. User clicks on tab or selects from dropdown
2. Tab scrolled into view if needed
3. Content visibility updated
4. `onTabChange` callback fired with old and new indices
5. `onTabSelect` callback fired with selected index

### Tab Closure
1. User clicks close button or presses Ctrl+W
2. `onTabClose` callback fired (can return false to cancel)
3. If not cancelled, tab removed
4. Active tab updated if necessary

### Dropdown Search
1. User clicks the overflow button; with search enabled and at least the
   threshold's number of visible tabs, a search popup opens listing them
   (with fewer, or search off, a plain menu of the tabs opens instead)
2. User types in the popup
3. Dropdown list filtered in real-time
4. Escape clears search, Enter selects match

## Rendering Details

The component renders in multiple layers:

1. **Tab Bar Background** - Base layer with configurable color
2. **Individual Tabs** - Rendered with state-based colors
3. **Active Tab Highlight** - Visual indicator for selected tab
4. **Tab Text** - Truncated with ellipsis if needed
5. **Close Buttons** - Positioned at right edge of tabs
6. **Overflow Dropdown** - Rendered when tabs exceed space
7. **Content Area** - Active tab's content rendered below/beside tabs
   (framed, except in `Flat` - no frame - and `Pill` - a hairline on the
   bar's side only)

## Performance Considerations

- **Lazy Loading:** Tab content is only rendered when visible
- **Efficient Layout:** Layout recalculated only when needed
- **Smart Overflow:** Dropdown populated only when shown
- **Optimized Search:** Case-insensitive substring matching
- **Event Delegation:** Single event handler for all tabs

## Best Practices

1. **Limit Tab Count:** Keep under 20 tabs for best UX
2. **Use the Overflow Dropdown:** Turn it on when many tabs are expected; the search threshold decides when the list becomes searchable
3. **Meaningful Titles:** Use clear, concise tab labels
4. **Icon Support:** Prefix titles with emoji/icons for recognition
5. **Consistent Style:** Match tab style to application theme
6. **Proper Cleanup:** Remove event handlers when destroying
7. **Content Caching:** Reuse content components when possible

## Migration from Previous Versions

### From v1.5.x to v1.6.0
- Added `OverflowDropdownPosition` enum
- Added search functionality with threshold
- Enhanced keyboard navigation support

## Thread Safety

The UltraCanvasTabbedContainer is **not** thread-safe. All operations should be performed on the main UI thread.

## Dependencies

- UltraCanvasContainer (base class)
- UltraCanvasButton and UltraCanvasAutoComplete (overflow button and search popup)
- UltraCanvasMenu (plain overflow list, tab context menu)
- UltraCanvasEvent (event handling)
- UltraCanvasRenderContext (rendering)

## Platform Support

- **Windows:** Full support
- **Linux:** Full support
- **macOS:** Full support
- **UltraOS:** Full support
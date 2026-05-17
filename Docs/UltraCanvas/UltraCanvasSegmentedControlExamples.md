# UltraCanvasSegmentedControl Documentation

## Overview

**UltraCanvasSegmentedControl** is a multi-segment selection control for choosing between mutually exclusive options (single mode) or toggling several options independently (multiple/toggle mode). It is well-suited for view-switchers, filter bars, formatting toolbars, and size pickers.

**Version:** 1.0.0
**Header:** `include/UltraCanvasSegmentedControl.h`
**Namespace:** `UltraCanvas`
**Base Class:** `UltraCanvasUIElement`

## Features

- **Three Selection Modes**: Single, Multiple, Toggle
- **Three Width Modes**: Equal, FitContent, Custom (per-segment width)
- **Four Style Presets**: Default, Modern (iOS-like), Flat, Bar
- **Per-Segment Properties**: Text, icon, enabled flag, alignment
- **Keyboard Navigation**: Arrow keys, Home, End
- **Optional No-Selection**: Allow toggling the active segment off
- **Builder Pattern**: Fluent `SegmentedControlBuilder` API
- **Animation Hook**: Optional cross-fade between selections

## Header Include

```cpp
#include "UltraCanvasSegmentedControl.h"
```

## Class Reference

### Constructor

```cpp
UltraCanvasSegmentedControl(const std::string& identifier = "SegmentedControl",
                            long x = 0, long y = 0,
                            long w = 300, long h = 32);
```

### Factory Functions

```cpp
std::shared_ptr<UltraCanvasSegmentedControl> CreateSegmentedControl(
    const std::string& identifier, long x, long y, long w, long h);

std::shared_ptr<UltraCanvasSegmentedControl> CreateSegmentedControl(
    const std::string& identifier, const Rect2Di& bounds);
```

### Segment Management

```cpp
int  AddSegment(const std::string& text, TextAlignment alignment = TextAlignment::Center);
int  AddSegment(const std::string& text, const std::string& iconPath,
                TextAlignment alignment = TextAlignment::Center);
int  InsertSegment(int index, const std::string& text,
                   TextAlignment alignment = TextAlignment::Center);
void RemoveSegment(int index);
void ClearSegments();
int  GetSegmentCount() const;
```

### Segment Properties

```cpp
void        SetSegmentText(int index, const std::string& text);
std::string GetSegmentText(int index) const;

void SetSegmentIcon(int index, const std::string& iconPath);
void SetSegmentEnabled(int index, bool enabled);
bool IsSegmentEnabled(int index) const;
void SetSegmentWidth(int index, float width);    // Used with SegmentWidthMode::Custom
```

### Selection — Single Mode

```cpp
void        SetSelectedIndex(int index);
int         GetSelectedIndex() const;
std::string GetSelectedText() const;
```

### Selection — Multiple / Toggle Modes

```cpp
void SetSelectedIndices(const std::vector<int>& indices);
std::vector<int> GetSelectedIndices() const;

void SelectSegment(int index, bool select = true);
void ToggleSegmentSelection(int index);
bool IsSegmentSelected(int index) const;

void SelectAll();
void DeselectAll();

std::vector<std::string> GetSelectedTexts() const;

void SetAllowNoSelection(bool allow);   // Allow deselecting the last active segment
```

### Selection Mode

```cpp
void                  SetSelectionMode(SegmentSelectionMode mode);
SegmentSelectionMode  GetSelectionMode() const;
```

### Styling

```cpp
void                   SetStyle(SegmentedControlStyle newStyle);
SegmentedControlStyle  GetStyle() const;

void              SetWidthMode(SegmentWidthMode mode);
SegmentWidthMode  GetWidthMode() const;
```

## Enumerations

### SegmentWidthMode

```cpp
enum class SegmentWidthMode {
    Equal,        // All segments equal width
    FitContent,   // Each segment sized to fit its content
    Custom        // Use SetSegmentWidth per segment
};
```

### SegmentSelectionMode

```cpp
enum class SegmentSelectionMode {
    Single,       // One segment selected at a time (default)
    Multiple,     // Multiple segments selectable
    Toggle        // Each segment is an independent toggle
};
```

## SegmentedControlStyle Structure

```cpp
struct SegmentedControlStyle {
    // State colors
    Color normalColor    = Colors::ButtonFace;
    Color selectedColor  = Colors::Selection;
    Color hoverColor     = Colors::SelectionHover;
    Color disabledColor  = Colors::LightGray;

    // Text colors per state
    Color normalTextColor    = Colors::TextDefault;
    Color selectedTextColor  = Colors::White;
    Color hoverTextColor     = Colors::TextDefault;
    Color disabledTextColor  = Colors::TextDisabled;

    // Borders & separators
    Color  borderColor      = Colors::ButtonShadow;
    double borderWidth      = 1.0f;
    Color  separatorColor   = Color(200, 200, 200, 255);
    double separatorWidth   = 1.0f;

    // Layout
    double cornerRadius       = 5.0f;
    int    paddingHorizontal  = 10;
    int    paddingVertical    = 6;
    int    iconSpacing        = 6;
    int    segmentSpacing     = 0;  // Inter-segment gap

    // Typography
    std::string fontFamily;
    float       fontSize    = 12.0f;
    FontWeight  fontWeight  = FontWeight::Normal;

    // Animation
    bool   enableAnimation    = false;
    double animationDuration  = 0.15f;

    // Icons
    int iconSize = 16;

    // Presets
    static SegmentedControlStyle Default();
    static SegmentedControlStyle Modern();   // iOS-like blue
    static SegmentedControlStyle Flat();     // Spaced individual segments
    static SegmentedControlStyle Bar();      // No separators, solid bar
};
```

## Events and Callbacks

```cpp
// Called when the selected index changes (single mode)
std::function<void(int)> onSegmentSelected;

// Called when the selection set changes (multiple/toggle modes)
std::function<void(const std::vector<int>&)> onSelectionChanged;

// Called for every click on a segment (regardless of state change)
std::function<void(int)> onSegmentClick;

// Called when a segment becomes hovered
std::function<void(int)> onSegmentHover;
```

## Builder Pattern

```cpp
auto control = SegmentedControlBuilder("filter", 50, 100, 420, 35)
    .AddSegment("All")
    .AddSegment("Active")
    .AddSegment("Completed")
    .AddSegment("Archived")
    .SetSelectedIndex(0)
    .OnSegmentSelected([](int index) {
        std::cerr << "Selected index " << index << std::endl;
    })
    .Build();
```

## Usage Examples

### Basic Bordered Style (Single Selection)

```cpp
auto basicControl = SegmentedControlBuilder("basic", 50, 100, 420, 35)
    .AddSegment("All")
    .AddSegment("Active")
    .AddSegment("Completed")
    .AddSegment("Archived")
    .SetSelectedIndex(0)
    .OnSegmentSelected([](int index) {
        const char* labels[] = {"All", "Active", "Completed", "Archived"};
        std::cerr << "Selected '" << labels[index] << "' (index "
                  << index << ")" << std::endl;
    })
    .Build();
container->AddChild(basicControl);
```

### iOS-Style (Modern Preset)

```cpp
auto iOSControl = CreateSegmentedControl("ios", 50, 200, 350, 32);
iOSControl->AddSegment("Map");
iOSControl->AddSegment("Transit");
iOSControl->AddSegment("Satellite");
iOSControl->SetStyle(SegmentedControlStyle::Modern());
iOSControl->SetSelectedIndex(0);
iOSControl->onSegmentSelected = [](int index) {
    std::cerr << "View mode index: " << index << std::endl;
};
```

### Flat Style (Spaced Segments)

```cpp
auto flatControl = CreateSegmentedControl("flat", 50, 290, 420, 35);
flatControl->AddSegment("Day");
flatControl->AddSegment("Week");
flatControl->AddSegment("Month");
flatControl->AddSegment("Year");
flatControl->SetStyle(SegmentedControlStyle::Flat());
flatControl->SetSelectedIndex(2);  // Pre-select "Month"
```

### Bar Style with Custom Colors

```cpp
auto barControl = CreateSegmentedControl("bar", 50, 380, 360, 36);
barControl->AddSegment("Small");
barControl->AddSegment("Medium");
barControl->AddSegment("Large");
barControl->AddSegment("X-Large");

SegmentedControlStyle barStyle = SegmentedControlStyle::Bar();
barStyle.normalColor       = Color(230, 230, 230, 255);
barStyle.selectedColor     = Color(0, 120, 215, 255);
barStyle.normalTextColor   = Color(80, 80, 80, 255);
barStyle.selectedTextColor = Colors::White;
barStyle.cornerRadius      = 8.0f;
barStyle.separatorWidth    = 0;

barControl->SetStyle(barStyle);
barControl->SetSelectedIndex(1);
```

### Toggle Mode (Text Formatting Toolbar)

In `Toggle` mode, each segment can be turned on or off independently. With
`SetAllowNoSelection(true)`, clicking the active segment again deselects it.

```cpp
auto textStyleControl = CreateSegmentedControl("textStyle", 50, 470, 200, 32);
textStyleControl->AddSegment("<b>B</b>");
textStyleControl->AddSegment("<i>I</i>");
textStyleControl->AddSegment("<u>U</u>");
textStyleControl->AddSegment("<span strikethrough=\"true\">S</span>");
textStyleControl->SetAllowNoSelection(true);
textStyleControl->SetWidthMode(SegmentWidthMode::Equal);
textStyleControl->SetSelectionMode(SegmentSelectionMode::Toggle);

textStyleControl->onSegmentClick = [textStyleControl](int index) {
    const char* labels[] = {"Bold", "Italic", "Underline", "Strikethrough"};
    if (textStyleControl->IsSegmentSelected(index)) {
        std::cerr << labels[index] << " ENABLED" << std::endl;
    } else {
        std::cerr << labels[index] << " DISABLED" << std::endl;
    }
};
```

### FitContent Width Mode (Alignment Toolbar)

```cpp
auto alignmentControl = CreateSegmentedControl("alignment", 50, 560, 400, 34);
alignmentControl->AddSegment("Left",   TextAlignment::Left);
alignmentControl->AddSegment("Center", TextAlignment::Center);
alignmentControl->AddSegment("Right",  TextAlignment::Right);
alignmentControl->SetWidthMode(SegmentWidthMode::FitContent);

SegmentedControlStyle alignAppearance;
alignAppearance.selectedColor = Color(52, 152, 219, 255);
alignAppearance.hoverColor    = Color(52, 152, 219, 64);
alignAppearance.cornerRadius  = 6.0f;
alignmentControl->SetStyle(alignAppearance);

alignmentControl->SetSelectedIndex(0);
```

### Disabled Segments

```cpp
auto disabledControl = CreateSegmentedControl("disabled", 50, 650, 600, 35);
disabledControl->AddSegment("Enabled 1");
disabledControl->AddSegment("Disabled");
disabledControl->AddSegment("Enabled 2");
disabledControl->AddSegment("Also Disabled");
disabledControl->AddSegment("Enabled 3");
disabledControl->SetSegmentEnabled(1, false);
disabledControl->SetSegmentEnabled(3, false);
disabledControl->SetSelectedIndex(0);
// Keyboard navigation automatically skips disabled segments.
```

### Variable-Length Segments with a Custom Theme

```cpp
auto customWidthControl = CreateSegmentedControl("customWidth", 50, 740, 500, 36);
customWidthControl->AddSegment("Short");
customWidthControl->AddSegment("Medium Length");
customWidthControl->AddSegment("Very Long Segment Name");
customWidthControl->SetWidthMode(SegmentWidthMode::FitContent);

SegmentedControlStyle greenAppearance = SegmentedControlStyle::Flat();
greenAppearance.selectedColor = Color(46, 204, 113, 255);
greenAppearance.hoverColor    = Color(46, 204, 113, 64);
customWidthControl->SetStyle(greenAppearance);

customWidthControl->SetSelectedIndex(1);
```

## Keyboard Navigation

When the control has focus:

- **Left / Right Arrow** — Navigate between segments
- **Up / Down Arrow** — Alternative navigation
- **Home** — Jump to the first enabled segment
- **End** — Jump to the last enabled segment
- Disabled segments are skipped automatically

## See Also

- [UltraCanvasButton](UltraCanvasButton.md) - Single-purpose clickable button
- [UltraCanvasMenu](UltraCanvasMenu.md) - Menus with checkable/radio items
- [UltraCanvasToolbar](UltraCanvasToolbar.md) - Toolbar of actions
- [UltraCanvasLabel](UltraCanvasLabel.md) - Companion text element

# UltraCanvasGroupBox Documentation

## Overview

**UltraCanvasGroupBox** is a titled container (a "group box" or "fieldset") that
visually frames a set of related child elements under a caption. It extends
`UltraCanvasContainer`, so it inherits the framework's full child-management,
layout, and scrolling machinery while adding a titled frame and optional
interactive behaviour.

**Version:** 1.0.0
**Header:** `include/UltraCanvasGroupBox.h`
**Namespace:** `UltraCanvas`
**Base Class:** `UltraCanvasContainer`

## Features

- **Three Frame Styles**: `Framed` (classic fieldset, caption embedded in a gap
  in the top border), `Header` (caption strip + separator above the content) and
  `Flat` (no border, just a caption strip over an optional fill).
- **Title Alignment**: `Left`, `Center`, `Right`.
- **Full Visual Styling**: border colour/width, corner radius, background fill,
  caption font/colour, header background and separator, content padding — all via
  `GroupBoxVisualStyle`.
- **Style Presets**: `GroupBoxVisualStyle::Default()` and
  `GroupBoxVisualStyle::Card()` (iOS-like grouped card).
- **Activator (checkbox or switch)**: an optional checkbox- or switch-style
  toggle in the title bar that enables/disables (greys out) the contained
  children. It can be placed on the **left or right** side of the title bar.
- **Info icon + help text**: an optional "ⓘ" icon (left or right) that reveals
  help text as a tooltip on hover.
- **Collapsible**: a disclosure arrow collapses the box down to its title bar.
- **Standard Container**: children are added with `AddChild()` and laid out by the
  CSS layout engine inside the frame, below the caption.

## Header Include

```cpp
#include "UltraCanvasGroupBox.h"
```

## Class Reference

### Constructors

```cpp
UltraCanvasGroupBox(const std::string& identifier, float x, float y, float w, float h,
                    const std::string& title = "");
UltraCanvasGroupBox(const std::string& identifier, float w, float h,
                    const std::string& title = "");
UltraCanvasGroupBox(const std::string& identifier, const std::string& title = "");
```

### Factory Functions

```cpp
auto gb = CreateGroupBox("settings", 20, 20, 300, 160, "Settings");
```

### Key Methods

```cpp
void SetTitle(const std::string& title);
void SetTitleAlignment(GroupBoxTitleAlignment alignment);   // Left / Center / Right
void SetFrameStyle(GroupBoxFrameStyle style);               // Framed / Header / Flat

void SetVisualStyle(const GroupBoxVisualStyle& style);
void SetTitleFont(const std::string& family, float size, FontWeight weight = FontWeight::Bold);
void SetTitleColor(const Color& color);
void SetBorderColor(const Color& color);
void SetBorderWidth(float width);
void SetCornerRadius(float radius);
void SetGroupBackgroundColor(const Color& color);
void SetContentPadding(float padding);

void SetCheckable(bool checkable);
void SetChecked(bool checked);
bool IsChecked() const;
void SetActivatorStyle(GroupBoxActivatorStyle style);   // Checkbox / Switch
void SetActivatorSide(GroupBoxIndicatorSide side);      // Left / Right
void EnableActivatorSwitch(GroupBoxIndicatorSide side = GroupBoxIndicatorSide::Right);

void SetInfoIcon(bool show);
void SetInfoIconSide(GroupBoxIndicatorSide side);       // Left / Right
void SetHelpText(const std::string& text);              // shows the info icon

void SetCollapsible(bool collapsible);
void SetCollapsed(bool collapsed);
bool IsCollapsed() const;
```

### Callbacks

```cpp
std::function<void(bool)> onCheckedChanged;     // new checked state
std::function<void(bool)> onCollapsedChanged;   // new collapsed state
```

## Usage Examples

### Basic framed group

```cpp
auto gb = CreateGroupBox("food", 20, 20, 280, 150, "Best Food");
gb->AddChild(CreateRadio("r1", "Pizza"));
gb->AddChild(CreateRadio("r2", "Taco"));
gb->AddChild(CreateRadio("r3", "Burrito"));
window->AddElement(gb);
```

### Checkable group (enables/disables its contents)

```cpp
auto gb = CreateGroupBox("notify", 20, 20, 300, 120, "Notifications");
gb->SetCheckable(true);
gb->SetChecked(true);
gb->AddChild(std::make_shared<UltraCanvasCheckbox>("snd", 240, 24, "Play sound"));
gb->onCheckedChanged = [](bool on) { /* ... */ };
```

### Switch-activated section with an info icon

```cpp
auto gb = CreateGroupBox("settings", 20, 20, 300, 130, "Settings");
gb->SetFrameStyle(GroupBoxFrameStyle::Header);
gb->EnableActivatorSwitch(GroupBoxIndicatorSide::Right);  // switch on the right
gb->SetInfoIconSide(GroupBoxIndicatorSide::Left);         // ⓘ on the left
gb->SetHelpText("Toggle the switch to enable or disable this section.");
gb->AddChild(/* ... */);
gb->onCheckedChanged = [](bool on) { /* ... */ };
```

### Collapsible card

```cpp
auto gb = CreateGroupBox("info", 20, 20, 300, 160, "GROUPBOX");
gb->SetFrameStyle(GroupBoxFrameStyle::Header);
gb->SetVisualStyle(GroupBoxVisualStyle::Card());
gb->SetCollapsible(true);
```

## Demo

See `Apps/DemoApp/UltraCanvasGroupBoxExamples.cpp` for a showcase of all frame
styles, title alignments, and the checkable / collapsible behaviours.

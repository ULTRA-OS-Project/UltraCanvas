# UltraCanvasGroupBox Documentation

<!-- doc-check: UltraCanvasRadioGroup foodGroup; -->

## Overview

**UltraCanvasGroupBox** is a titled container (a "group box" or "fieldset") that
visually frames a set of related child elements under a caption. It extends
`UltraCanvasContainer`, so it inherits the framework's full child-management,
layout, and scrolling machinery while adding a titled frame and optional
interactive behaviour.

**Version:** 1.3.0
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
- **Style Presets**: `GroupBoxVisualStyle::Default()`,
  `GroupBoxVisualStyle::Card()` (iOS-like grouped card) and
  `GroupBoxVisualStyle::SectionCard()` (a window section as a white card on
  the light page of `Colors::SectionPage` - the look UltraMail's panes and
  UltraPaint's side panel share).
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
auto pizza   = std::make_shared<UltraCanvasRadio>("r1", "Pizza");
auto taco    = std::make_shared<UltraCanvasRadio>("r2", "Taco");
auto burrito = std::make_shared<UltraCanvasRadio>("r3", "Burrito");
gb->AddChild(pizza);
gb->AddChild(taco);
gb->AddChild(burrito);
window->AddChild(gb);

// The box only frames the radios. One-of-many selection comes from an
// UltraCanvasRadioGroup that lives as long as they do (e.g. a member of
// your window class).
foodGroup.AddRadioButton(pizza);
foodGroup.AddRadioButton(taco);
foodGroup.AddRadioButton(burrito);
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
gb->AddChild(std::make_shared<UltraCanvasCheckbox>("autosave", "Save automatically"));
gb->onCheckedChanged = [](bool on) { /* ... */ };
```

### Collapsible card

```cpp
auto gb = CreateGroupBox("info", 20, 20, 300, 160, "GROUPBOX");
gb->SetFrameStyle(GroupBoxFrameStyle::Header);
gb->SetVisualStyle(GroupBoxVisualStyle::Card());
gb->SetCollapsible(true);
```

### Sections of a window as cards

`SectionCard()` is one section of a window as a white card: a hairline border,
8 px corners, the caption in bold grey over a hairline separator, 8 px of
padding inside. It is drawn for the `Header` frame style and for a page of
`Colors::SectionPage` around the cards - on a white page a white card shows
nothing but its hairline. Its colours are compile-time constants beside the
other UI colours (`Colors::SectionCardBackground`, `SectionCardBorder`,
`SectionCardCaption`), so a container drawn as a matching card can use them
too; a list inside a card takes `Colors::ListRowHover`
and `Colors::ListRowSelected` for its rows (see
`UltraCanvasListViewExamples.md`). Applications that share them look alike,
and stay alike when they change; UltraMail and UltraPaint both build their
cards from them. Change what the content needs, not the look: the caption's
size to the application's body size, the padding.

```cpp
auto panel = CreateContainer("sidePanel", 0, 0, 300, 0);
panel->layout.SetFlexColumn().SetFlexGap(8);
panel->SetPadding(8);
panel->SetBackgroundColor(Colors::SectionPage);

auto layers = CreateGroupBox("layers", 0, 0, 268, 0, "Layers");
layers->SetFrameStyle(GroupBoxFrameStyle::Header);
GroupBoxVisualStyle card = GroupBoxVisualStyle::SectionCard();
card.titleFont.fontSize = 9.0f;     // the application's body size
card.contentPadding = 6.0f;
layers->SetVisualStyle(card);
layers->layout.SetFlexColumn().SetFlexGap(4);
panel->AddChild(layers);
```

## Demo

See `Apps/DemoApp/UltraCanvasGroupBoxExamples.cpp` for a showcase of all frame
styles, title alignments, and the checkable / collapsible behaviours.

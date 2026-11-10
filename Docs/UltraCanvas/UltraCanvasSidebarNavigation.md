# UltraCanvas Sidebar Navigation Documentation

## Contents

- [Overview](#overview)
- [Quick Start](#quick-start)
- [Sections, items and badges](#sections-items-and-badges)
- [Keyboard and mouse](#keyboard-and-mouse)
- [Compact mode](#compact-mode)
- [Key API](#key-api)
- [Following the theme](#following-the-theme)
- [Notes](#notes)

## Overview

`UltraCanvasSidebarNavigation` is the navigation column of an application
window: sections of items, each an icon, a label and an optional badge, one of
them selected. Up / Down, Home / End and Return on the keyboard; the wheel
scrolls when the list is taller than the element; a compact mode shows the
icons alone with the label as a tooltip. A store's Discover / Categories /
Installed / Updates, a mail client's folders, a settings window's pages.

Self-rendered (no child widgets), like `UltraCanvasToolbar` and the chips.

**File Location**: `include/UltraCanvasSidebarNavigation.h`
**Version**: 1.1.0 - `SidebarStyle::FromTheme`, `ApplyTheme`: the sidebar follows the theme
**Author**: UltraCanvas Framework

---

## Quick Start
```cpp
#include "UltraCanvasSidebarNavigation.h"
using namespace UltraCanvas;

auto nav = CreateSidebarNavigation("nav");
nav->AddItem("discover", "Discover", "media/icons/compass.svg");
nav->AddItem("categories", "Categories", "media/icons/grid.svg");
int library = nav->AddSection("Library");
nav->AddItem(library, "installed", "Installed", "media/icons/box.svg");
nav->AddItem(library, "updates", "Updates", "media/icons/arrow-up.svg");
nav->SetBadge("updates", 3);
nav->Select("discover");
nav->onSelectionChanged = [](const std::string& id) { /* show that page */ };
row->AddChild(nav);   // the parent is a flex row; the sidebar is PreferredWidth() wide
```

## Sections, items and badges

- Items added without a section go into an untitled first section, made on
  demand; `AddSection` adds a titled one and returns its index.
- An item's `id` is what every callback and setter names; adding an item
  whose id exists replaces that item in place.
- `SetBadge(id, count)` shows a count pill at the item's right (0 clears);
  `SetBadgeText` any short text. In compact mode a badge is a dot at the
  icon's corner.
- `SetItemEnabled(id, false)` greys an item out; it cannot be selected by
  click or keyboard.

## Keyboard and mouse

| Input | Effect |
|---|---|
| click | selects the item; the sidebar takes the focus |
| double-click, Return, Space | `onItemActivated` for the selected item |
| Up / Down | the previous / next enabled item, across sections |
| Home / End | the first / last enabled item |
| wheel | scrolls when the content is taller than the element |

`onSelectionChanged` fires once per change, for a click, the keyboard and
`Select`; `Select(id, false)` changes the selection silently (restoring a
saved state).

## Compact mode

`SetCompact(true)` shows the icons alone, centred, with each item's label (or
`SidebarItem::tooltip`) as a tooltip on hover, and the element becomes
`compactWidth` wide; section headers shrink to a thin separator.
`onCompactChanged` reports the switch, so a window can remember it.

## Key API
```cpp
int  AddSection(const std::string& title);
std::string AddItem(int sectionIndex, const SidebarItem& item);
std::string AddItem(int sectionIndex, const std::string& id, const std::string& label, const std::string& iconPath = "");
std::string AddItem(const std::string& id, const std::string& label, const std::string& iconPath = "");
bool RemoveItem(const std::string& id);
void Clear();
void SetBadge(const std::string& id, int count);
void SetBadgeText(const std::string& id, const std::string& text);
void SetItemEnabled(const std::string& id, bool enabled);
void SetItemLabel(const std::string& id, const std::string& label);
void SetItemIcon(const std::string& id, const std::string& iconPath);
void SetItemTooltip(const std::string& id, const std::string& tooltip);
bool Select(const std::string& id, bool fireCallback = true);
const std::string& GetSelectedId() const;
bool SelectNext();
bool SelectPrevious();
void SetCompact(bool compact);
bool IsCompact() const;
float PreferredWidth() const;
float ContentHeight() const;
void SetStyle(const SidebarStyle& newStyle);
Rect2Df ItemRect(const std::string& id) const;
```

```cpp
std::function<void(const std::string& id)> onSelectionChanged;
std::function<void(const std::string& id)> onItemActivated;
std::function<void(bool compact)> onCompactChanged;
```

`SidebarStyle` holds the colours (background, text, section text, hover,
selected, the indicator bar, the badge), the row heights and paddings, the
icon size, the corner radius, the expanded and compact widths and the item /
section / badge fonts; `SidebarStyle::Default()` is a light grey column with a
blue indicator bar at the selected item.

## Following the theme

`SidebarStyle::FromTheme` puts the framework theme's roles
(`UltraCanvasTheme.md`) over a base style's shape (heights, paddings,
widths, the fonts' weights): the panel for the column, ink for the items at
the body size and faint ink disabled, muted ink for a section at the caption
size, the hover fill, the soft accent with ink for the selected item and the
accent for its bar, the accent with text on it for a badge, the focus ring.
Under the theme policy a new sidebar follows the theme by itself
(`ApplyTheme`); a `SetStyle` by hand stops that, `SetFollowTheme(true)`
after it keeps both.

```cpp
auto nav = std::make_shared<UltraCanvasSidebarNavigation>("nav", 220, 400);
SidebarStyle narrow = nav->GetStyle();
narrow.expandedWidth = 180;
nav->SetStyle(SidebarStyle::FromTheme(UltraCanvasTheme::Current(), narrow));
nav->SetFollowTheme(true);
```

## Notes

- `UltraCanvasToolbar` in its vertical orientation is a strip of tools, each
  an action; the sidebar is a list of places, one of which is current.
- `UltraCanvasTreeView` is the element when the places nest; the sidebar has
  one level under section headers.

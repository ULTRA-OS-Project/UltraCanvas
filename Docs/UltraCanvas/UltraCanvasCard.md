# UltraCanvas Card Documentation

## Contents

- [Overview](#overview)
- [Quick Start](#quick-start)
- [Structure](#structure)
- [States](#states)
- [Key API](#key-api)
- [Notes](#notes)

## Overview

`UltraCanvasCard` is a raised surface with a **header** (icon, title, subtitle
and a trailing element such as a badge), a **content** area for the caller's
elements and a row of **actions** - real `UltraCanvasButton` children,
right-aligned - at the foot. The surface itself can be clicked (`onClick`
fires for a press no child took), hovered, highlighted and selected. A store's
app tile, a password vault's entry, a settings page's section with Save beside
it.

Built on `UltraCanvasContainer`: the children are the framework's labels and
buttons laid out as a flex column; the card adds the structure, the states and
the surface.

**File Location**: `include/UltraCanvasCard.h`
**Version**: 1.0.0
**Author**: UltraCanvas Framework

---

## Quick Start
```cpp
#include "UltraCanvasCard.h"
#include "UltraCanvasBadge.h"
#include "UltraCanvasLabel.h"
using namespace UltraCanvas;

auto card = CreateCard("tile", "UltraVideo", "The video editor");
card->SetIcon("media/icons/ultravideo.svg");
card->SetTrailing(CreateBadge("tile.price", 0, 0, "29.90 EUR"));
card->AddContent(CreateLabel("tile.summary", 0, 0, 0, 40, "Cut, trim and export in one window."));
card->AddActionButton("tile.install", "Install", [] { /* install */ });
card->AddActionButton("tile.details", "Details", [] { /* open */ });
card->onClick = [] { /* the surface was clicked: open the details page */ };
column->AddChild(card);
```

The card is as tall as its parts. In a flex column of cards give the column a
gap; in a scroll pane leave the card a margin on the side the border would
otherwise be clipped on.

## Structure

```
+--------------------------------------------------------+
| [icon]  Title                               [trailing] |   header
|         Subtitle                                       |
|  content: whatever was added with AddContent           |
|                                   [Action] [Action]    |   actions
+--------------------------------------------------------+
```

- The header shows when there is a title, a subtitle, an icon or a trailing
  element; the content area when it has children; the actions row when it
  has children.
- The icon is drawn by the card at the header's left (`CardStyle::iconSize`);
  the titles move over by that much.
- `GetContent()` is a flex column the caller may lay out as it likes;
  `GetActions()` is a right-aligned flex row.

## States

| Call | Look |
|---|---|
| `onClick` set, or `SetClickable(true)` | hand cursor; hover tints the surface; a press and release on the surface fires `onClick` |
| `SetHighlighted(true)` | the accent border (`highlightBorderColor`, `highlightBorderWidth`) |
| `SetSelected(true)` | the accent border and the selected surface tint |

A press a child took - a button, a chip in the trailing slot - never reaches
the card, so the actions work as buttons do and the surface stays a surface.

## Key API
```cpp
void SetTitle(const std::string& text);
void SetSubtitle(const std::string& text);
void SetIcon(const std::string& path);
void SetTrailing(std::shared_ptr<UltraCanvasUIElement> element);
void AddContent(std::shared_ptr<UltraCanvasUIElement> element);
void ClearContent();
void AddAction(std::shared_ptr<UltraCanvasUIElement> element);
std::shared_ptr<UltraCanvasButton> AddActionButton(const std::string& id, const std::string& text,
                                                   std::function<void()> onClick, float minWidth = 72.0f);
void ClearActions();
void SetClickable(bool clickable);
void SetHighlighted(bool on);
void SetSelected(bool on);
void SetStyle(const CardStyle& newStyle);
```

```cpp
std::function<void()> onClick;
std::function<void()> onDoubleClick;
```

`CardStyle` holds the colours (surface, hover, selected, border, accent), the
corner radius, the padding and gaps, the icon size and the title / subtitle
fonts and colours; `CardStyle::Default()` is white on a light border with a
14 pt bold title.

## Notes

- `UltraCanvasGroupBox` with `GroupBoxVisualStyle::Card()` frames a *section*
  of a window under a caption; this card is an *item* - one of many, with its
  own actions and a clickable surface.
- The card does not scroll its content; put long content in a container of
  its own inside it, or the card in a scrolling pane.

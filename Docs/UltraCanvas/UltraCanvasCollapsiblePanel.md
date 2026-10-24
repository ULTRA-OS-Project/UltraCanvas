# UltraCanvas Collapsible Panel Documentation

## Contents

- [Overview](#overview)
- [Quick Start](#quick-start)
- [Folding and height](#folding-and-height)
- [Key API](#key-api)
- [Notes](#notes)

## Overview

`UltraCanvasCollapsiblePanel` folds to its header: a strip with a disclosure
chevron, a title, a detail text ("3 downloads") and real buttons at the right,
over a **body** the caller fills. Collapsed, only the header shows; expanded,
the body below it at the expanded height, or as tall as its content. Docked at
the bottom of a flex column it is a download queue, a log or a problems list,
and its top edge can be dragged to resize it.

Built on `UltraCanvasContainer`: the header's label and buttons and the body's
children are framework elements; the panel adds the fold, the chevron, the
separator and the resize grip.

**File Location**: `include/UltraCanvasCollapsiblePanel.h`
**Version**: 1.0.0
**Author**: UltraCanvas Framework

---

## Quick Start
```cpp
#include "UltraCanvasCollapsiblePanel.h"
using namespace UltraCanvas;

auto panel = CreateCollapsiblePanel("downloads", "Downloads");
panel->SetDetail("2 running");
panel->AddHeaderButton("downloads.clear", "Clear finished", [] { /* clear */ });
panel->GetBody()->AddChild(queueList);
panel->SetExpandedHeight(220);
panel->onExpandedChanged = [](bool expanded) { /* remember it */ };
column->AddChild(panel);   // the parent is a flex column; the panel keeps its height
```

## Folding and height

| State | Height |
|---|---|
| collapsed | the header strip (`headerHeight`) and the border |
| expanded, `SetExpandedHeight(h)` | `h`, never below `minExpandedHeight` |
| expanded, expanded height 0 (the default) | as tall as the body's content |

A press on the header strip toggles the fold (a press on a header button is the
button's and does not). `SetCollapsible(false)` makes the header inert for a
fixed panel with a header strip. With `SetResizable` on (the default) a drag
that starts in the `gripSize` band along the top edge changes the expanded
height - dragging up makes a bottom-docked panel taller - and
`onExpandedHeightChanged` reports the result when the drag ends.

## Key API
```cpp
void SetTitle(const std::string& text);
void SetDetail(const std::string& text);
void AddHeaderAction(std::shared_ptr<UltraCanvasUIElement> element);
std::shared_ptr<UltraCanvasButton> AddHeaderButton(const std::string& id, const std::string& text,
                                                   std::function<void()> onClick, float minWidth = 64.0f);
void ClearHeaderActions();
std::shared_ptr<UltraCanvasContainer> GetBody() const;
void SetExpanded(bool expanded);
bool IsExpanded() const;
void Toggle();
void SetExpandedHeight(float height);
float GetExpandedHeight() const;
void SetResizable(bool resizable);
void SetCollapsible(bool collapsible);
void SetStyle(const CollapsiblePanelStyle& newStyle);
Rect2Df HeaderRect() const;
```

```cpp
std::function<void(bool expanded)> onExpandedChanged;
std::function<void(float height)> onExpandedHeightChanged;
```

`CollapsiblePanelStyle` holds the surface, header, separator, chevron and grip
colours, the header height, paddings, chevron size, grip size, the minimum
expanded height and the title / detail fonts; `CollapsiblePanelStyle::Default()`
is a light header strip over white with a 13 pt bold title.

## Notes

- `UltraCanvasGroupBox` with `SetCollapsible(true)` folds a framed *section*
  under its caption; this panel is a *dock*: a header strip with buttons, a
  detail text, a resizable height, and a body that is a plain container.
- The panel does not scroll its body; a list in the body scrolls itself, or
  the body gets a scrolling container.

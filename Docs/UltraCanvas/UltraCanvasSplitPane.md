# UltraCanvas Split Pane Documentation

## Overview

**UltraCanvasSplitPane** is a container that divides its content area into *N*
panes along one axis, separated by draggable splitters. Panes are sized by
proportional **weights**, so resizing the split pane preserves relative sizes,
and per-pane **min/max pixel sizes** clamp the drag.

Each split line can additionally carry an optional **handle** (square, rounded
square or round/capsule, at a configurable size) and any number of **action
icons** centred on the line, each with its own tooltip, enabled state and click
handler.

**File Location**: `include/UltraCanvasSplitPane.h`
**Version**: 1.2.0
**Author**: UltraCanvas Framework

## Features

- ✅ **N panes** on one axis (`Horizontal` or `Vertical`), not just two
- ✅ **Weighted** sizing plus per-pane **min / max** pixel clamps
- ✅ Drag a splitter to resize only the two panes it separates
- ✅ **Nestable** — put a split pane inside a pane for VS Code-style layouts
- ✅ Panes are real `UltraCanvasContainer`s: padding, background, scrollbars,
  CSS layout
- ✅ Optional **splitter handle**: `Square`, `RoundedSquare`, `Round`, with a
  configurable size across and along the line, corner radius, colors, border
  and grip lines
- ✅ **Action icons on the split line**: variable count per splitter, centred
  across the line, image or text glyph, tooltip, enabled/visible state,
  per-icon click callback
- ✅ Icons never start a drag; the splitter still drags everywhere else
- ✅ Callbacks: `onSplitterDragStart`, `onSplitterDragEnd`, `onWeightsChanged`,
  `onSplitterIconClicked`

## Quick Start

```cpp
#include "UltraCanvasSplitPane.h"
using namespace UltraCanvas;

auto split = CreateHorizontalSplitPane("MainSplit", 20, 20, 960, 400);

auto sidebar = split->AddPane(1.0);   // weight 1
auto editor  = split->AddPane(3.0);   // weight 3
split->SetPaneMinSize(0, 120);        // sidebar never narrower than 120 px

sidebar->SetBackgroundColor(Color(232, 240, 252, 255));
sidebar->SetPadding(8);
sidebar->AddChild(CreateButton("Files", 1, 10, 10, 120, 28, "Files"));

editor->SetBackgroundColor(Colors::White);
editor->AddChild(myEditorWidget);

split->onWeightsChanged = [](const std::vector<double>& w) {
    SaveLayout(w);
};
```

`CreateVerticalSplitPane(...)` gives the top-to-bottom variant. Splitter *i* is
always the line between pane *i* and pane *i + 1*, so a split pane with *N*
panes has *N - 1* splitters.

## Pane management

| Method | Purpose |
|---|---|
| `AddPane(weight = 1.0)` | Append a pane, returns the new `UltraCanvasContainer` |
| `InsertPane(index, weight)` | Insert a pane at `index` |
| `RemovePane(index)` / `RemovePane(pane*)` | Remove a pane and its split line |
| `PaneCount()` / `GetPane(index)` / `GetPaneIndex(pane*)` | Query |
| `SetPaneWeight(index, w)` / `GetPaneWeight(index)` | Proportional sizing |
| `SetPaneMinSize(index, px)` / `SetPaneMaxSize(index, px)` | Clamps (0 max = unlimited) |
| `SetPaneSizes({px, px, …})` | Set all weights at once from pixel sizes |
| `SplitterCount()` | Number of split lines (`PaneCount() - 1`) |

## Styling the splitter

```cpp
SplitPaneStyle style = split->GetSplitPaneStyle();
style.splitterThickness = 3;                       // the visible line
style.splitterHitMargin = 3;                       // extra grab room each side
style.splitterColor       = Color(220, 220, 220, 255);
style.splitterHoverColor  = Color(180, 200, 240, 255);
style.splitterActiveColor = Color(120, 160, 230, 255);
split->SetSplitPaneStyle(style);
```

The splitter strip is as thick as the line plus its hit margin, widened
further when a handle or icons need the room. Only the `splitterThickness`
band is painted; the rest of the strip stays transparent, so a thin line can
carry a much larger handle.

## Splitter handle

```cpp
enum class SplitterHandleShape { NoHandle, Square, RoundedSquare, Round };
```

`Round` is fully rounded: a **circle** when the handle is square, a **capsule**
when it is elongated.

```cpp
// Shorthand
split->SetSplitterHandleShape(SplitterHandleShape::RoundedSquare,
                              /*crossSize*/ 14, /*axisLength*/ 48,
                              /*cornerRadius*/ 4.0f);

// Full control
SplitterHandleStyle hs = split->GetSplitterHandleStyle();
hs.shape        = SplitterHandleShape::Round;
hs.crossSize    = 22;     // extent ACROSS the split line
hs.axisLength   = 0;      // extent ALONG the line; <= 0 = auto
hs.position     = 0.5f;   // 0 = start of the line, 0.5 = centred, 1 = end
hs.color        = Color(244, 244, 244, 255);
hs.hoverColor   = Color(205, 220, 248, 255);
hs.activeColor  = Color(150, 185, 240, 255);
hs.borderColor  = Color(170, 170, 170, 255);   // Transparent = no border
hs.borderWidth  = 1.0f;
hs.showGrip     = true;   // grip lines, suppressed when the handle holds icons
hs.gripLineCount = 3;
hs.gripSpacing   = 3;
hs.gripColor     = Color(120, 120, 120, 255);
hs.containsIcons = true;  // the handle wraps this splitter's icon group
split->SetSplitterHandleStyle(hs);
```

| Field | Meaning |
|---|---|
| `crossSize` | Handle size perpendicular to the line. The splitter strip widens to fit it. |
| `axisLength` | Handle size along the line. `<= 0` means auto: `crossSize` for a bare handle, or just enough to wrap the icon group when `containsIcons` is set. |
| `cornerRadius` | Used by `RoundedSquare` only. |
| `position` | Where the handle sits along the line, `0.0` … `1.0`. |
| `containsIcons` | `true` (default): the icons of this splitter are drawn inside the handle. `false`: handle and icons are positioned independently. |

Dragging works on the handle exactly as on the rest of the line — the handle is
a visual affordance, not a separate control.

## Action icons on the split line

Icons are centred across the split line and grouped at the middle along it (or
inside the handle). The count is per splitter and free.

```cpp
struct SplitPaneIcon {
    std::string id;         // your identifier, passed back on click
    std::string iconPath;   // image/SVG path; empty -> `label` is drawn instead
    std::string label;      // short text/glyph fallback, e.g. "\xE2\x80\xB9"
    std::string tooltip;
    bool enabled = true;
    bool visible = true;
    Color iconColor = Colors::Transparent;   // Transparent = use the icon style
    std::function<void(size_t splitterIndex, size_t iconIndex)> onClick;
};
```

```cpp
SplitPaneIcon collapse;
collapse.id       = "collapse-sidebar";
collapse.iconPath = "media/icons/angle-left.svg";
collapse.tooltip  = "Collapse the sidebar";
collapse.onClick  = [split](size_t s, size_t i) {
    split->SetPaneWeight(0, 0.02);
    split->SetSplitterIconVisible(s, i, false);       // swap to the restore icon
    split->SetSplitterIconVisible(s, i + 1, true);
};
split->AddSplitterIcon(/*splitterIndex*/ 0, collapse);

// AddSplitterIcon(icon) is shorthand for splitter 0 — the only one in a
// two-pane split.
split->AddSplitterIcon(restore);

split->onSplitterIconClicked =
    [](size_t splitterIndex, size_t iconIndex, const SplitPaneIcon& icon) {
        Log("clicked " + icon.id);
    };
```

### Icon API

| Method | Purpose |
|---|---|
| `SetSplitterIcons(splitterIndex, icons)` | Replace the whole list |
| `AddSplitterIcon(splitterIndex, icon)` | Append, returns the new index |
| `AddSplitterIcon(icon)` | Append to splitter 0 |
| `InsertSplitterIcon(splitterIndex, iconIndex, icon)` | Insert |
| `RemoveSplitterIcon(splitterIndex, iconIndex)` | Remove one |
| `ClearSplitterIcons(splitterIndex)` / `ClearAllSplitterIcons()` | Remove all |
| `SplitterIconCount(splitterIndex)` | Count |
| `GetSplitterIcons(splitterIndex)` / `GetSplitterIcon(s, i)` | Read back |
| `SetSplitterIconEnabled(s, i, bool)` | Grey out and stop responding |
| `SetSplitterIconVisible(s, i, bool)` | Hide without removing (the group re-centres) |
| `SetSplitterIconTooltip(s, i, text)` | Change the tooltip |
| `SetSplitterIconPath(s, i, path)` | Swap the image |

### Icon styling

```cpp
SplitterIconStyle is = split->GetSplitterIconStyle();
is.size    = 16;      // icon box, square
is.spacing = 6;       // gap between icons
is.padding = 5;       // free space around the group
is.position = 0.5f;   // along the line, used when not drawn inside the handle
is.useIconAsMask = true;                          // tint monochrome icons
is.iconColor         = Color(70, 70, 70, 255);
is.iconHoverColor    = Color(20, 20, 20, 255);
is.iconDisabledColor = Color(170, 170, 170, 255);
is.hoverBackgroundColor   = Color(0, 0, 0, 30);
is.pressedBackgroundColor = Color(0, 0, 0, 55);
is.hoverCornerRadius = 3.0f;
is.labelFontFamily = "Arial";                     // for `label`-only icons
is.labelFontSize   = 12.0f;
split->SetSplitterIconStyle(is);
```

### Behaviour

- Pointing at an icon shows its tooltip and switches the cursor from the resize
  cursor to a hand.
- Pressing an icon does **not** start a drag; the click fires on release, and
  only if the release lands on the same icon.
- A disabled icon is drawn dimmed, shows no hover highlight and swallows the
  press (so it does not drag either).
- Hiding icons shrinks the group and re-centres what is left; an auto-sized
  handle shrinks with it.
- `onClick` runs first, then `onSplitterIconClicked`. Both may mutate the icon
  list — the icon is copied before the handlers run.

## Callbacks

| Callback | Signature |
|---|---|
| `onSplitterDragStart` | `void(size_t splitterIndex)` |
| `onSplitterDragEnd` | `void(size_t splitterIndex)` |
| `onWeightsChanged` | `void(const std::vector<double>& weights)` |
| `onSplitterIconClicked` | `void(size_t splitterIndex, size_t iconIndex, const SplitPaneIcon&)` |

## Nesting

Put a split pane inside a pane and let a flex layout stretch it:

```cpp
auto outer = CreateHorizontalSplitPane("Outer", 20, 20, 960, 400);
auto left  = outer->AddPane(1.0);
auto right = outer->AddPane(3.0);

auto inner = CreateVerticalSplitPane("Inner", 0, 0, 100, 100);
inner->AddPane(3.0);   // editor
inner->AddPane(1.0);   // output

right->layout.SetFlexColumn();
right->layout.SetFlexGap(0);
right->AddChild(inner);
inner->layoutItem.SetFlexGrow(1)
                 .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
```

## Notes

- Icons and handles are configured on the split pane, not on individual
  splitter objects — they survive pane insertion and removal. Removing a pane
  drops the icons of the split line that disappears with it; the remaining
  lines keep theirs.
- The handle and icon styles are shared by every splitter of a split pane; the
  icon *list* is per splitter.
- `SplitterHandleShape::NoHandle` (the default) is spelled that way rather than
  `None` because `<X11/X.h>` defines `None` as a macro.

## Demo

`Apps/DemoApp/UltraCanvasSplitPaneExamples.cpp` (demo page **Basic UI > Split
Pane**) shows weighted horizontal and vertical splits, nesting, live
handle-shape switching, and action icons wired to collapse, grow and even out
panes.

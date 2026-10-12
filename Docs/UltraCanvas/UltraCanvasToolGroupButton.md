# UltraCanvasToolGroupButton

Several tools sharing one slot of a tool palette or toolbar - the selection
tools, the brushes, the shapes of a paint program.

The slot shows one tool of the group, and a click on it picks that tool.
Resting the pointer on the slot folds out a bar beside it with every tool of
the group; a click in the bar picks that tool. A small triangle in the
slot's corner says the slot holds more than one tool. The bar folds back when
the pointer leaves both the slot and the bar, on Escape and on a click
elsewhere. A group of one tool is an ordinary toggle button: no triangle and
nothing to fold out.

**Version:** 1.0.0
**Header:** `include/UltraCanvasToolGroupButton.h`
**Namespace:** `UltraCanvas`
**Base Class:** `UltraCanvasButton`

## Adaptive or static

What the slot shows after a pick is the host's choice:

| `SetSlotShowsLastPicked` | The slot shows | A click on the slot picks |
|---|---|---|
| `true` (default) - an *adaptive* toolbar | the tool last picked, from the bar or with `SetCurrentItem` | that tool |
| `false` - a *static* toolbar | always the first item, the group's main tool | the main tool |

Either way the bar marks the tool in use - the *current* item - while the slot
is pressed. UltraPaint offers the choice as *Settings > Display > Tool
palette*.

## Adding one to a toolbar

`UltraCanvasToolbar::AddToolGroup` creates the slot with the toolbar's button
appearance, as `AddToggleButton` does for a single tool. In a vertical toolbar
the bar folds out to the right of the slot as a row; in a horizontal one it
folds out below as a column. Where the window has no room on that side it
goes to the opposite one.

```cpp
#include "UltraCanvasToolbar.h"

auto palette = std::make_shared<UltraCanvasToolbar>("palette", 0, 0, 48, 0);
palette->SetOrientation(ToolbarOrientation::Vertical);

auto selectGroup = palette->AddToolGroup("select", {
        {"rect-select",    "media/icons/ultrapaint/28/select-rect.svg",    "Rectangle Select (R)"},
        {"ellipse-select", "media/icons/ultrapaint/28/select-ellipse.svg", "Ellipse Select (E)"},
        {"lasso",          "media/icons/ultrapaint/28/lasso.svg",          "Lasso (L)"},
    },
    [](const std::string& toolId) {
        // Make toolId the active tool.
        (void)toolId;
    });
selectGroup->SetIconSize(28, 28);
selectGroup->SetElementSize(Size2Df(36, 36));
```

The bar's buttons copy the slot when the bar folds out - its size, its icon
size and its `ButtonStyle` - so a flat palette gets a flat bar, and the tool
in use wears the slot's `pressedColor`.

## Keeping the palette in step

The slot is a toggle button: a pick presses it. When the active tool changes
some other way - a shortcut key, a command that switches tools - the host
presses the slot of the new tool's group, lets every other slot go, and tells
the group which of its tools it is:

```cpp
// slots: every group of the palette; toolId: the tool that just became active.
for (const auto& slot : slots) {
    const bool inGroup = slot->SetCurrentItem(toolId);   // false: not one of its tools
    slot->SetPressed(inGroup);
}
```
<!-- doc-check: std::vector<std::shared_ptr<UltraCanvasToolGroupButton>> slots; std::string toolId; -->

`SetCurrentItem` fires nothing, so this does not loop back into
`onItemSelected`.

## Class reference

```cpp
UltraCanvasToolGroupButton(const std::string& identifier, float x, float y, float w, float h);
UltraCanvasToolGroupButton(const std::string& identifier, float w, float h);

// Items - the group's tools, in the bar's order
void SetItems(const std::vector<ToolGroupItem>& groupItems);
void AddItem(const ToolGroupItem& item);
const std::vector<ToolGroupItem>& GetItems() const;
int GetItemCount() const;
bool HasItem(const std::string& itemId) const;

// The tool last picked or set, and the one the slot shows
bool SetCurrentItem(const std::string& itemId);
std::string GetCurrentItem() const;
std::string GetSlotItem() const;
void SetSlotShowsLastPicked(bool showLastPicked);
bool GetSlotShowsLastPicked() const;

// The fold-out bar
void SetFoldOutSide(ToolGroupFoldOutSide side);
ToolGroupFoldOutSide GetFoldOutSide() const;
void SetFoldOutDelay(int milliseconds);
int GetFoldOutDelay() const;
void OpenFoldOut();
void CloseFoldOut();
bool IsFoldOutOpen() const;
std::shared_ptr<UltraCanvasContainer> GetFoldOut() const;
```

```cpp
std::function<void(const std::string& itemId)> onItemSelected;
```

`onItemSelected` fires on a click on the slot (with the tool it shows) and on
a click in the bar (with that tool).

`SetFoldOutDelay` is how long the pointer rests on the slot before the bar
folds out: 150 ms by default, so a pointer crossing the palette on its way to
the canvas does not flash every group's bar; 0 folds it out at once.
`OpenFoldOut` and `CloseFoldOut` do it from code - a group of one, or a slot
in no window, has nothing to fold out.

`CreateToolGroupButton(identifier, x, y, w, h, items)` creates one outside a
toolbar.

### ToolGroupItem

```cpp
struct ToolGroupItem {
    std::string id;          // handed back by onItemSelected
    std::string iconPath;
    std::string tooltip;     // its name (and shortcut)
};
```

### ToolGroupFoldOutSide

```cpp
enum class ToolGroupFoldOutSide {
    Auto = 0,     // right of a slot in a vertical toolbar, below one in any other
    Right = 1,
    Left = 2,
    Bottom = 3,   // below the slot
    Top = 4       // above it
};
```

The bar runs along its side: a row to the right or left, a column below or
above. The values are `Bottom` and `Top` rather than *Below* and *Above*
because X11's headers `#define` those two words.

## Implementation notes

- The bar is a popup on the slot's window, with the slot as its owner. The
  window hands an open popup every pointer event that is on neither it nor its
  owner, which is how the bar sees the pointer leave and folds back. It
  touches the slot, so the pointer never crosses a gap that belongs to neither
  on its way from one to the other.
- The bar's buttons do not take the keyboard focus: the canvas keeps it while
  a tool is picked.
- A focused slot picks its tool with Space or Return.

## See Also

- [UltraCanvasToolbarExamples.md](UltraCanvasToolbarExamples.md) - `AddToolGroup` and the rest of the toolbar
- [UltraCanvasButtonExamples.md](UltraCanvasButtonExamples.md) - the button it is built on

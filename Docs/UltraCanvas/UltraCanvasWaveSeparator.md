# UltraCanvasWaveSeparator

The S-curve transition between two groups on one bar: a desktop taskbar's
system group flowing into its running-apps group, a side panel's organiser
flowing into its info panel. One group's colour fills the part of the
separator on its side of the curve, the next group's colour the rest, so the
groups read as carved into one bar rather than boxed side by side.

**Header:** `UltraCanvasWaveSeparator.h` · **Base:** `UltraCanvasUIElement`

```cpp
#include "UltraCanvasWaveSeparator.h"

auto bar = CreateContainer("Taskbar", 0, 0, 0, 0);
bar->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

bar->AddChild(systemGroup);                                    // bar colour
bar->AddChild(CreateWaveSeparator("Wave1", /*verticalBar=*/true, barColor, insetColor));
bar->AddChild(runningGroup);                                   // inset colour
bar->AddChild(CreateWaveSeparator("Wave2", true, insetColor, barColor));
bar->AddChild(pinnedGroup);                                    // bar colour
```

## What it is

A flex child like [`UltraCanvasSeparator`](UltraCanvasUIElements.md), only
shaped. On a vertical bar it spans the bar's width and takes `length` px (36
by default) of its height; on a horizontal bar the other way round. It takes
no events and no focus and publishes its own size, so a flex container needs
nothing beyond the `AddChild`.

The curve is a cubic whose control points sit halfway along the main axis on
the two edges — the classic S. It enters and leaves the neighbours at a
tangent, so the transition has no corner at either end, and it is drawn as
two fills (the far colour under everything, the near colour up to the curve)
so there is never a seam against either neighbour whatever the pixel
rounding.

## API

| Call | Meaning |
|---|---|
| `UltraCanvasWaveSeparator(id, verticalBar, length = 36)` | The bar's direction and how much of its main axis the curve takes. |
| `CreateWaveSeparator(id, verticalBar, before, after, length = 36)` | The same, with the colours set. |
| `SetColors(before, after)` | The colour of the group before the separator (above on a vertical bar, left on a horizontal one) and of the group after it. |
| `SetFlipped(bool)` | Mirror the curve: from the right edge at the top to the left edge at the bottom. Purely visual; alternating the flip on a bar's separators reads as one continuous wave. |
| `SetVerticalBar(bool)`, `SetLength(px)` | Re-shape after construction (a taskbar moved from the left edge to the top). |

## See also

- [UltraDesktop](../UltraDesktop/README.md) — where the bars it joins live.
- [UltraCanvasToolbar](UltraCanvasToolbarExamples.md) — the groups on either side.

# UltraCanvasGradientEditor

A gradient ramp editor: the stops of a gradient on a horizontal strip.
Click a stop to select it, drag it along the strip to move it, double-click
the strip to add a stop with the ramp's colour there, drag a stop away
from the strip (or press Delete) to remove it, and nudge the selected stop
with the arrow keys. The stops are the render context's `GradientStop`, so
what the editor holds feeds `CreateLinearGradientPattern` and the other
pattern factories and `VectorStorage`'s gradient data without conversion.

**Header:** `UltraCanvasGradientEditor.h` · **Source:** `core/UltraCanvasGradientEditor.cpp`
**Version**: 1.0.0 · **Last Modified**: 2026-09-15 · **Author**: UltraCanvas Framework

The tone curve editor ([UltraCanvasCurveEditor](UltraCanvasCurveEditor.md))
is the precedent for dragging control points on a strip; this is its
colour-ramp sibling. It edits the stops only — where a linear gradient
starts and ends on an object is the fill tool's business, drawn on the
canvas.

## Usage

```cpp
#include "UltraCanvasGradientEditor.h"
#include "UltraCanvasColorPicker.h"

auto ramp = CreateGradientEditor("ramp", 10, 10, 260, 44);
ramp->SetStops({{0.0, Color(255, 0, 0)}, {0.5, Color(255, 255, 0)}, {1.0, Color(0, 0, 255)}});

// The selected stop's colour comes from a colour picker.
ramp->onSelectionChanged = [&](int index) {
    if (index >= 0) picker->SetColor(ramp->GetSelectedColor());
};
picker->onColorChanged = [&](const Color& c) {
    ramp->SetStopColor(ramp->GetSelectedStop(), c);
};

// Every edit: rebuild the fill.
ramp->onStopsChanged = [&] {
    fill.Stops = ramp->GetStops();
    canvas->Refresh();
};
ramp->onStopsChanging = ramp->onStopsChanged;   // live while a stop is dragged
```

## API

| Method | What it does |
|---|---|
| `SetStops(stops)` / `GetStops()` | The ramp. Stops are kept sorted by position; an empty list becomes black → white |
| `AddStop(position, color)` / `AddStopAt(position)` | Insert a stop, the second with the ramp's colour there; returns the index, selects it |
| `RemoveStop(index)` | Removes a stop; refuses below `SetMinimumStops` (default 2) |
| `SetStopColor(index, color)` / `SetStopPosition(index, position)` | Edit one stop (positions re-sort; the selection follows the stop) |
| `ColorAt(position)` | The ramp's interpolated colour |
| `SelectStop(index)` / `GetSelectedStop()` / `GetSelectedColor()` | Selection; `-1` for none |
| `Reverse()` | Flip the ramp end for end |
| `SetShowAlphaChecker(bool)` | Checkerboard behind the ramp so alpha shows (default on) |
| `onStopsChanged` | Any edit, including the end of a drag |
| `onStopsChanging` | While a stop is dragged |
| `onSelectionChanged(int)` | The selected index changed |

## Behaviour

- The first and last stops can be moved but not removed while the count is
  at the minimum; a middle stop dragged well away from the strip is removed
  on release.
- Left / Right nudge the selected stop by 1 % (Shift: 10 %); Delete removes it.
- The strip is drawn with the context's own linear gradient pattern, so it
  shows exactly what a fill with these stops looks like.

## Tests

`Tests/VectorEditTest.cpp` exercises sorting, insertion, colour
interpolation, removal limits, reversal and notifications without a window.

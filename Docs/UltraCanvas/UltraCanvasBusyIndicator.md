# UltraCanvas BusyIndicator Documentation

## Overview

**UltraCanvasBusyIndicator** is the animation that says *working on it*
when there is no percentage to show: a mail fetch, a network call, a folder
scan. It comes in five kinds (`BusyIndicatorKind`), all driven by the same
`Start()` / `Stop()`:

| Kind | What it draws | Box |
|---|---|---|
| `Ring` (default) | a partial arc turning over a faint full ring | square, e.g. 16 × 16 |
| `DualRing` | two concentric arcs turning in opposite directions | square, e.g. 20 × 20 |
| `Dots` | a row of dots that swell and brighten one after another, left to right | wide, e.g. 36 × 10 |
| `Bar` | a segment sliding left to right along a track, entering and leaving at the edges | wide, e.g. 120 × 4 |
| `Pulse` | a filled circle that grows and brightens, then shrinks and pales, over a faint disc | square, e.g. 14 × 14 |

When it stops, the space is left blank by default. The element can therefore
sit permanently in a status line and costs nothing while idle, because no
timer runs.

**File Location**: `include/UltraCanvasBusyIndicator.h`
**Version**: 1.1.0
**Author**: UltraCanvas Framework

It is not the right element for:

- a known fraction: use `UltraCanvasGaugeDiagramElement` in
  `GaugeMode::LinearBar`, or `UltraCanvasProgressDialog`;
- a number with up/down arrows: use `UltraCanvasSpinner`, which is a spin box
  despite its name.

## Features

- ✅ `Start()` / `Stop()` / `SetRunning(bool)` / `IsRunning()`, all idempotent
- ✅ Five kinds: `Ring`, `DualRing`, `Dots`, `Bar`, `Pulse`
- ✅ The animation phase is computed from elapsed time, so a late timer tick
  never slows it down; after `Stop()` it resumes where it rested
- ✅ The timer runs only while the indicator turns; the destructor stops it
- ✅ Style: kind, colour of the moving part, track colour (alpha 0 means no
  track), thickness, arc length, dot count, bar segment length, cycles per
  second, frame interval, and whether a stopped indicator stays visible

## Quick Start

```cpp
#include "UltraCanvasBusyIndicator.h"
using namespace UltraCanvas;

auto busy = CreateBusyIndicator("statusBusy", 0, 0, 14);   // 14 px square
busy->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
statusRow->AddChild(busy);

busy->Start();   // work begins
// ...
busy->Stop();    // work done: the ring disappears

// Other kinds: pass the box and the kind.
auto dots  = CreateBusyIndicator("loadDots", 0, 0, 36, 10, BusyIndicatorKind::Dots);
auto bar   = CreateBusyIndicator("loadBar",  0, 0, 120, 4, BusyIndicatorKind::Bar);
auto pulse = CreateBusyIndicator("syncPulse", 0, 0, 14, 14, BusyIndicatorKind::Pulse);
auto dual  = CreateBusyIndicator("dualRing", 0, 0, 20, 20, BusyIndicatorKind::DualRing);
```

## Style

```cpp
BusyIndicatorStyle st = busy->GetStyle();
st.kind       = BusyIndicatorKind::Ring;
st.arcColor   = Color(0, 120, 215, 255);
st.trackColor = Color(0, 0, 0, 0);   // no track
st.thickness  = 2.0f;                // default: side / 8
st.arcDegrees = 270.0f;
st.revolutionsPerSecond = 1.0f;
st.hideWhenStopped = true;           // false: show a still ring when idle
busy->SetStyle(st);
```

| Field | Default | Meaning |
|---|---|---|
| `kind` | `Ring` | which animation, see the table above |
| `arcColor` | blue `#0078D7` | the moving part: arc, dots, bar segment, pulsing circle |
| `trackColor` | black at alpha 28 | the still part behind it (ring, dot outlines, bar track, disc); alpha 0 means none |
| `thickness` | `0` | `Ring` / `DualRing`: stroke width, `0` → side / 8 (at least 1.5 px). `Bar`: bar height, `0` → the element height. Ignored by `Dots` and `Pulse` |
| `arcDegrees` | `270` | `Ring`: arc length, clamped to 10–350. `DualRing`: each ring's arc is half of this, clamped to 10–175 |
| `dotCount` | `3` | `Dots`: how many, clamped to 2–12 |
| `barFraction` | `0.3` | `Bar`: segment length as a share of the width, clamped to 0.05–0.9 |
| `revolutionsPerSecond` | `1.0` | speed: cycles per second for every kind (one turn, one wave, one pass, one breath) |
| `frameIntervalMs` | `33` | redraw period while running |
| `hideWhenStopped` | `true` | draw nothing while stopped |

## Threading

`Start()` and `Stop()` use the application timer, so call them on the UI
thread. From a worker thread, hand the call over with
`UltraCanvasApplicationBase::GetCurrent()->PostToUIThread(...)`.

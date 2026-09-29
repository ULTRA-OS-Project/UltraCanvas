# UltraCanvas BusyIndicator Documentation

## Overview

**UltraCanvasBusyIndicator** is the turning ring that says *working on it*
when there is no percentage to show: a mail fetch, a network call, a folder
scan. A partial arc turns over a faint full track while the indicator runs.
When it stops, the space is left blank by default. The element can therefore
sit permanently in a status line and costs nothing while idle, because no
timer runs.

**File Location**: `include/UltraCanvasBusyIndicator.h`
**Version**: 1.0.0
**Author**: UltraCanvas Framework

It is not the right element for:

- a known fraction: use `UltraCanvasGaugeDiagramElement` in
  `GaugeMode::LinearBar`, or `UltraCanvasProgressDialog`;
- a number with up/down arrows: use `UltraCanvasSpinner`, which is a spin box
  despite its name.

## Features

- ✅ `Start()` / `Stop()` / `SetRunning(bool)` / `IsRunning()`, all idempotent
- ✅ The angle is computed from elapsed time, so a late timer tick never slows
  the ring down
- ✅ The timer runs only while the indicator turns; the destructor stops it
- ✅ Style: arc colour, track colour (alpha 0 means no track), thickness, arc
  length, revolutions per second, frame interval, and whether a stopped
  indicator stays visible

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
```

## Style

```cpp
BusyIndicatorStyle st = busy->GetStyle();
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
| `arcColor` | blue `#0078D7` | the turning arc |
| `trackColor` | black at alpha 28 | the full ring behind it; alpha 0 means none |
| `thickness` | `0` → side / 8 (at least 1.5 px) | stroke width |
| `arcDegrees` | `270` | arc length, clamped to 10–350 |
| `revolutionsPerSecond` | `1.0` | speed |
| `frameIntervalMs` | `33` | redraw period while running |
| `hideWhenStopped` | `true` | draw nothing while stopped |

## Threading

`Start()` and `Stop()` use the application timer, so call them on the UI
thread. From a worker thread, hand the call over with
`UltraCanvasApplicationBase::GetCurrent()->PostToUIThread(...)`.

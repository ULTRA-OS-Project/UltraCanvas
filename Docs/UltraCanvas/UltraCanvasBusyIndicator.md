# UltraCanvas BusyIndicator Documentation

## Overview

**UltraCanvasBusyIndicator** is the animation that says *working on it*
when there is no percentage to show: a mail fetch, a network call, a folder
scan. It comes in six kinds (`BusyIndicatorKind`), all driven by the same
`Start()` / `Stop()`:

| Kind | What it draws | Box |
|---|---|---|
| `Ring` (default) | a partial arc turning over a faint full ring | square, e.g. 16 × 16 |
| `DualRing` | two concentric arcs turning in opposite directions, the outer in `arcColor` and the inner in `secondArcColor` | square, e.g. 20 × 20 |
| `Dots` | a row of dots that swell and brighten one after another, left to right | wide, e.g. 36 × 10 |
| `Bar` | a segment sliding left to right along a track, entering and leaving at the edges | wide, e.g. 120 × 4 |
| `Pulse` | a filled circle that grows and brightens, then shrinks and pales, over a faint disc | square, e.g. 14 × 14 |
| `DotRing` | a ring of dots with a head running round it clockwise; the dots behind the head shrink, and fade or not as `dotRingFade` says | square, e.g. 20 × 20 |

When it stops, the space is left blank by default. The element can therefore
sit permanently in a status line and costs nothing while idle, because no
timer runs.

**File Location**: `include/UltraCanvasBusyIndicator.h`
**Version**: 1.2.0
**Author**: UltraCanvas Framework

It is not the right element for:

- a known fraction: use `UltraCanvasGaugeDiagramElement` in
  `GaugeMode::LinearBar`, or `UltraCanvasProgressDialog`;
- a number with up/down arrows: use `UltraCanvasSpinner`, which is a spin box
  despite its name.

## Features

- ✅ `Start()` / `Stop()` / `SetRunning(bool)` / `IsRunning()`, all idempotent
- ✅ Six kinds: `Ring`, `DualRing`, `Dots`, `Bar`, `Pulse`, `DotRing`
- ✅ `DualRing` in two colours; `DotRing` with three fades, one of them a new
  random colour on every fade-in
- ✅ The animation phase is computed from elapsed time, so a late timer tick
  never slows it down; after `Stop()` it resumes where it rested
- ✅ The timer runs only while the indicator turns; the destructor stops it
- ✅ Style: kind, colour of the moving part, a second colour for the inner
  ring, track colour (alpha 0 means no track), thickness, arc length, dot
  count, bar segment length (as a share or in pixels), ring dot count and
  fade, cycles per second, frame interval, and whether a stopped indicator
  stays visible

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
auto ring  = CreateBusyIndicator("dotRing",  0, 0, 20, 20, BusyIndicatorKind::DotRing);
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
| `arcColor` | blue `#0078D7` | the moving part: arc, dots, bar segment, pulsing circle; `DualRing`: the outer arc |
| `secondArcColor` | orange `#F0821E` | `DualRing`: the inner arc. Set it to `arcColor` for a one-colour dual ring |
| `trackColor` | black at alpha 28 | the still part behind it (ring, dot outlines, bar track, disc); alpha 0 means none |
| `thickness` | `0` | `Ring` / `DualRing`: stroke width, `0` → side / 8 (at least 1.5 px). `Bar`: bar height, `0` → the element height. `DotRing`: dot diameter, at most half the side; `0` → each dot three quarters of the distance to its neighbour. Ignored by `Dots` and `Pulse` |
| `arcDegrees` | `270` | `Ring`: arc length, clamped to 10–350. `DualRing`: each ring's arc is half of this, clamped to 10–175 |
| `dotCount` | `3` | `Dots`: how many, clamped to 2–12 |
| `barFraction` | `0.3` | `Bar`: segment length as a share of the width, clamped to 0.05–0.9 |
| `barLength` | `0` | `Bar`: segment length in pixels, at most the width; above `0` it overrides `barFraction` |
| `ringDotCount` | `8` | `DotRing`: how many dots on the ring, clamped to 4–16 |
| `dotRingFade` | `Fade` | `DotRing`: how the dots behind the head look, see below |
| `revolutionsPerSecond` | `1.0` | speed: cycles per second for every kind (one turn, one wave, one pass, one breath) |
| `frameIntervalMs` | `33` | redraw period while running |
| `hideWhenStopped` | `true` | draw nothing while stopped |

## DotRing fades

The head of a `DotRing` runs clockwise from 12 o'clock, one turn per cycle.
Every dot is at its fullest the moment the head reaches it, shrinks as the
head moves on, and swells back during the last step before the head comes
round again. `dotRingFade` (`BusyDotRingFade`) says what happens to its
colour meanwhile:

| Value | The dots behind the head |
|---|---|
| `Fade` (default) | fade out in `arcColor`, and back in just ahead of the head |
| `NoFade` | stay in full `arcColor`; only their size shows where the head is |
| `FadeRandomColor` | fade out like `Fade`, and every time a dot fades back in it takes a new random colour, at least 60° round the colour wheel from the one it faded out in |

```cpp
auto spinner = CreateBusyIndicator("party", 0, 0, 64, 64, BusyIndicatorKind::DotRing);
BusyIndicatorStyle st = spinner->GetStyle();
st.ringDotCount = 10;
st.dotRingFade  = BusyDotRingFade::FadeRandomColor;
spinner->SetStyle(st);
spinner->Start();
```

A dot changes colour at the moment it is invisible, so the colours never
jump. `FadeRandomColor` keeps `arcColor`'s alpha and ignores its hue; the
track (`trackColor`, a faint disc under every dot) keeps the ring visible
where the dots have faded out.

## Threading

`Start()` and `Stop()` use the application timer, so call them on the UI
thread. From a worker thread, hand the call over with
`UltraCanvasApplicationBase::GetCurrent()->PostToUIThread(...)`.

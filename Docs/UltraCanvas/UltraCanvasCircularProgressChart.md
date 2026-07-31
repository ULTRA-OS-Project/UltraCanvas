# UltraCanvasCircularProgressChart Documentation

## Overview

The `UltraCanvasCircularProgressChart` is the angle-encoded member of the UltraCanvas circular chart family. Each ring carries **one value**, drawn as an arc whose sweep is proportional to that value within the ring's own range — the "concentric progress rings" / "activity rings" chart. Sub-styles cover the single thick progress ring ("Completed 72 %") and the progress pie (a filled sector over a track disc). It complements `UltraCanvasRadialBarChart` (value → ray **length**) and `UltraCanvasPieChartElement` (slices as **shares of one total**): here, rings are independent percentages, not parts of a whole.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Charts/UltraCanvasCircularProgressChart.h`
**Base Class:** `UltraCanvasChartElementBase`
**Version:** 1.0.0
**Last Modified:** 2026-07-29
**Author:** UltraCanvas Framework

See [`UltraCanvasCircularCharts.md`](UltraCanvasCircularCharts.md) for the family-wide decision table (which circular chart element to use when).

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasCircularProgressChart
```

## Features

### Core Capabilities
- **Three sub-styles:** `ConcentricRings` (one arc per ring), `SingleRing` (first ring at full thickness) and `ProgressPie` (filled sector to the centre)
- **Independent ring ranges:** Every ring maps `value` through its own `[minValue, maxValue]` (default 0–100, so values read as percentages)
- **Tracks:** The remainder of each ring drawn as a pale auto-tint of the ring colour, an explicit colour, or hidden
- **Cap styles:** Round (default) or butt arc ends
- **Arc-tip callouts:** Percentage or value at the moving end of each arc, optionally in a rounded bubble
- **Ring name labels:** Horizontal text inside the band at the arc start, or a stacked column of `label value` rows aligned with each ring's start point (the "radial bar card" look)
- **Centre disc:** Filled/bordered disc in the hole with title + subtitle text
- **Legend:** Numbered chips (`01`, `02`, …) or plain swatches, with optional per-ring icons, on any side
- **Direction & orientation:** Start angle (default −90° = 12 o'clock) and clockwise/counter-clockwise sweep
- **Interactive:** Hover highlighting, tooltips, `onRingClick` / `onRingHover` callbacks

## Data Model

```cpp
struct ProgressRing {
    std::string label;
    double value;              // mapped onto [minValue, maxValue]
    double minValue;           // default 0
    double maxValue;           // default 100
    Color color;               // Colors::Transparent = take from palette
    std::string iconPath;      // optional icon for the legend chip
};
```

## Enumerations

```cpp
enum class CircularProgressStyle  { ConcentricRings, SingleRing, ProgressPie };
enum class RingCapStyle           { Butt, Round };
enum class RingTrackMode          { AutoTint, Explicit, Hidden };
enum class RingTipLabel           { NoLabel, Percentage, Value };
enum class RingNamePosition       { Hidden, InsideStart, StackedStart };
enum class CircularLegendStyle    { NoLegend, NumberedChips, Swatches };
enum class CircularLegendPosition { Left, Right, Top, Bottom };
```

## Public API

### Data Management
| Method | Description |
|---|---|
| `AddRing(ring)` / `AddRing(label, value, color)` | Append a ring (inner to outer); returns its index |
| `ClearRings()` / `GetRingCount()` / `GetRing(i)` | Access and reset |
| `SetRingValue(i, v)` | Update one ring's value in place |
| `SetRingColor(i, color)` / `SetRingRange(i, min, max)` | Per-ring styling and scale |

### Sub-Style & Geometry
| Method | Description |
|---|---|
| `SetSubStyle(style)` | `ConcentricRings`, `SingleRing` or `ProgressPie` |
| `SetStartAngle(deg)` | Arc base position; default −90 (12 o'clock) |
| `SetClockwise(on)` | Sweep direction; default clockwise |
| `SetInnerRadiusFraction(f)` | Centre hole, 0–0.9 |
| `SetRingSpacing(px)` | Radial gap between rings |
| `SetRingThickness(px)` | Fixed band thickness; 0 = automatic from the available radius |

### Arcs & Tracks
| Method | Description |
|---|---|
| `SetCapStyle(cap)` | `Round` or `Butt` arc ends |
| `SetTrackMode(mode)` | `AutoTint`, `Explicit` or `Hidden` |
| `SetTrackColor(color)` | Track colour for `Explicit` mode |
| `SetTrackTintAlpha(a)` | Tint strength for `AutoTint` mode |
| `SetColorPalette(colors)` | Fallback colours for rings without an explicit colour |

### Labels & Centre
| Method | Description |
|---|---|
| `SetTipLabelContent(content)` | `Percentage`, `Value` or `NoLabel` at the arc tip |
| `SetTipLabelBubble(on)` | Rounded background pill behind tip callouts |
| `SetTipLabelFont(family, size, weight)` / `SetTipLabelColor(c)` | Tip callout styling |
| `SetRingNameLabels(position)` | `Hidden`, `InsideStart` or `StackedStart` |
| `SetNameLabelFont(...)` / `SetNameLabelColor(c)` | Name label styling (use a light colour for `InsideStart` on dark bands) |
| `SetCenterDisc(fill, border)` / `SetShowCenterDisc(on)` | Centre disc |
| `SetCenterText(title, subtitle)` | Centre content; the subtitle is drawn above the title ("Completed" over "72%") |
| `SetCenterTextColor(c)` / `SetCenterFont(...)` | Centre text styling |

### Legend
| Method | Description |
|---|---|
| `SetLegendStyle(style, position)` | `NumberedChips` / `Swatches` on `Left`/`Right`/`Top`/`Bottom` |
| `SetLegendFont(family, size)` / `SetLegendTextColor(c)` | Legend styling |

### Formatting & Interaction
| Method | Description |
|---|---|
| `SetValueFormatter(fn)` | `double → string` for value labels and tooltips |
| `SetPercentFormatter(fn)` | Receives the percentage (0–100) |
| `SetHoverHighlightEnabled(on)` / `SetTooltipsEnabled(on)` | Interactivity toggles |
| `SetRingTooltipFormatter(fn)` | Custom tooltip from a `ProgressRing` |
| `onRingClick` / `onRingHover` | `std::function<void(size_t ringIndex)>` |

## Usage Examples

### Circular infographic (concentric rings, numbered legend, centre disc)

```cpp
#include "Plugins/Charts/UltraCanvasCircularProgressChart.h"

auto rings = UltraCanvas::CreateCircularProgressChart("kpis", 20, 20, 520, 480);
rings->AddRing("Research",  48.0, UltraCanvas::Color(255,  62, 133, 255));
rings->AddRing("Marketing", 54.0, UltraCanvas::Color(255, 200,  40, 255));
rings->AddRing("Sales",     64.0, UltraCanvas::Color(150, 200,  60, 255));
rings->AddRing("Support",   83.0, UltraCanvas::Color(120,  90, 200, 255));

rings->SetCapStyle(UltraCanvas::RingCapStyle::Round);
rings->SetTrackMode(UltraCanvas::RingTrackMode::AutoTint);
rings->SetTipLabelContent(UltraCanvas::RingTipLabel::Percentage);
rings->SetCenterDisc(UltraCanvas::Color(225, 30, 40, 255), UltraCanvas::Colors::White);
rings->SetCenterText("Lorem", "ipsum");
rings->SetCenterTextColor(UltraCanvas::Colors::White);
rings->SetLegendStyle(UltraCanvas::CircularLegendStyle::NumberedChips,
                      UltraCanvas::CircularLegendPosition::Left);
rings->onRingClick = [](size_t ringIndex) { /* ... */ };
container->AddChild(rings);
```

### Stacked start labels (the "radial bar card" look)

```cpp
auto card = UltraCanvas::CreateConcentricRingChart("card", 20, 20, 360, 360, {
    {"Legend", 22.0}, {"Legend", 27.0}, {"Legend", 38.0}, {"Legend", 39.0}
});
card->SetTipLabelContent(UltraCanvas::RingTipLabel::NoLabel);
card->SetRingNameLabels(UltraCanvas::RingNamePosition::StackedStart);
card->SetTrackMode(UltraCanvas::RingTrackMode::Hidden);
```

### Single completion ring

```cpp
auto ring = UltraCanvas::CreateCircularProgressChart("done", 20, 20, 240, 240);
ring->SetSubStyle(UltraCanvas::CircularProgressStyle::SingleRing);
ring->AddRing("Completed", 72.0, UltraCanvas::Color(120, 90, 220, 255));
ring->SetInnerRadiusFraction(0.68f);
ring->SetTrackMode(UltraCanvas::RingTrackMode::Explicit);
ring->SetTrackColor(UltraCanvas::Color(240, 240, 243, 255));
ring->SetTipLabelContent(UltraCanvas::RingTipLabel::NoLabel);
ring->SetCenterText("72%", "Completed");
```

### Progress pie

```cpp
auto pie = UltraCanvas::CreateProgressPieChart("used", 20, 20, 200, 200,
                                               "Storage used", 65.0,
                                               UltraCanvas::Color(40, 140, 230, 255));
```

## Factory Functions

```cpp
std::shared_ptr<UltraCanvasCircularProgressChart> CreateCircularProgressChart(
    const std::string& id, int x, int y, int w, int h);

// Concentric rings from (label, percent) pairs.
std::shared_ptr<UltraCanvasCircularProgressChart> CreateConcentricRingChart(
    const std::string& id, int x, int y, int w, int h,
    const std::vector<std::pair<std::string, double>>& labeledPercentages);

// One-value progress pie.
std::shared_ptr<UltraCanvasCircularProgressChart> CreateProgressPieChart(
    const std::string& id, int x, int y, int w, int h,
    const std::string& label, double percent,
    const Color& color = Colors::Transparent);
```

## Notes & Conventions

- Rings are ordered **inner to outer** in `AddRing` order.
- Values are clamped to each ring's range; a full sweep is 360°.
- `StackedStart` name labels assume the default top start angle; with other
  start angles the rows still anchor to each ring's start point.
- The chart owns its ring data — `SetDataSource` from the chart base class is
  not used by this element.
- Demo: `Apps/DemoApp/UltraCanvasCircularProgressChartExamples.cpp`
  (DemoApp → Charts → Circular Progress Chart).
- Roadmap for this element (animated tweens, partial sweeps, gradients, colour
  bands, targets, angular axis): see
  [`UltraCanvasCircularChartProposal.md`](UltraCanvasCircularChartProposal.md).

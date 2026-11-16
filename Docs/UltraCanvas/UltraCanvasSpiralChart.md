# UltraCanvasSpiralChart

A spiral plot: a time series wound along a spiral, one turn per period - a
day, a week, a year. Times one period apart land on the same ray, one ring
further out, so a rhythm lines up from the centre outward: every winter on
one side, every weekday morning in one place.

```cpp
#include "Plugins/Charts/Engine/Charts/UltraCanvasSpiralChart.h"
```

- Design: [`UltraCanvasSpiralChartProposal.md`](UltraCanvasSpiralChartProposal.md)
  (this is Phase 1 of three)

## Contents

- [Overview](#overview)
- [Quick start](#quick-start)
- [The spiral's geometry](#the-spirals-geometry)
- [Displays](#displays)
- [Labels and look](#labels-and-look)
- [Reading the chart](#reading-the-chart)
- [Demo](#demo)
- [Tests](#tests)

## Overview

| Part | What it is |
|---|---|
| `UltraCanvasSpiralModel` | UI-free: the samples, the period, where a time lands on the spiral, the value range |
| `UltraCanvasSpiralChart` | The chart on the chart engine: the ribbon, band or line along the spiral, spokes, labels, a colour bar, hover |

Time is a plain number in any unit - days, hours, seconds - and a formatter
turns it into text for the tooltip. The model maps a time to an angle and a
radius; the chart draws everything on the spiral itself.

## Quick start

```cpp
UltraCanvasSpiralModel model;
model.title = "Visits per hour";
model.period = 24.0;                                    // hours: a day per turn
model.sectorLabels = {"00:00", "06:00", "12:00", "18:00"};
for (int hour = 0; hour < 24 * 14; ++hour) {
    model.samples.push_back({static_cast<double>(hour), 50.0 + 40.0 * std::sin(hour * 0.26), ""});
}

auto chart = CreateSpiralChart("visits", 0, 0, 640, 640);
chart->SetModel(model);
chart->SetDisplay(SpiralDisplay::Ribbon);
chart->SetColormap(HeatmapColormap::Inferno);
```

## The spiral's geometry

A time `t` is `u = (t - start) / period` turns along: its whole part is the
ring (0 innermost), its fraction the angle. With `n` rings and an inner
radius `r0`, each ring is `(1 - r0) / (n + 1)` wide, so the first turn's
inner edge is at `r0` and the last turn's outer edge at the outer radius:

```cpp
model.start = 0.0;            // the time the first turn starts at (unset = the first sample)
model.innerRadius = 0.18;     // the empty middle, 0..0.6
model.startAngle = 90.0;      // degrees where every turn starts: 12 o'clock
model.clockwise = true;
SpiralPosition p = model.Position(400.0);        // angle (radians) and radius (0..1)
SpiralPosition edge = model.Position(400.0, -0.5);   // the ring's inner edge
int rings = model.TurnCount();
```

A sample covers the time to the next sample, so pieces neither gap nor
overlap; where the next sample is more than three typical steps away, the
sample covers one typical step and the rest stays empty. A value of NaN is
a missing sample.

The period is what the chart is for: wound to the right period, a cycle
lines up on one ray; wound to a wrong one, it drifts into a slant.
`chart->SetPeriod(p)` rewinds the spiral (the demo's slider does).

## Displays

| `SpiralDisplay` | Each sample is |
|---|---|
| `Ribbon` (default) | a piece of ribbon coloured by its value through `SetColormap`, with a colour bar under the spiral |
| `Band` | a band from its ring's inner edge, as wide as its value (in `SetColor`) |
| `Line` | a point on a line, as far out from the ring's inner edge as its value |

Values are scaled to the data's range, or to `model.valueMin` /
`model.valueMax` when set; a value outside them is clamped.

## Labels and look

```cpp
model.sectorLabels = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};   // evenly around a turn, with spokes
model.turnLabels = {"2021", "2022", "2023"};          // at each turn's start
model.valueName = "Temperature";                      // the tooltip's and the colour bar's
chart->SetColormap(HeatmapColormap::RdBu, true);      // reversed: low blue, high red
chart->SetRibbonWidth(0.9f);                          // share of the ring the ribbon fills
chart->SetShowGuide(true);                            // the rings' edge and the spokes
chart->SetShowColorBar(true);
chart->SetValueDecimals(1);
```

Sector labels sit outside the spiral, in the middle of their sector. A turn
label sits on the start ray in its ring, on a light patch, and only while
the ring is tall enough for it.

## Reading the chart

```cpp
chart->SetTimeFormatter([](double day) { return "day " + std::to_string(static_cast<int>(day)); });
chart->onSampleHover = [&chart](int index) {     // -1 when the pointer leaves the spiral
    if (index >= 0) std::string text = chart->DescribeSample(index);
};
Point2Dd where = chart->ScreenPoint(400.0, 0.0);  // a time on screen, as of the last render
```

Each sample's piece of its ring, whatever the display, is its hit region:
the tooltip gives the time through the formatter, the value and the
sample's note, and the hovered piece is outlined.

## Demo

**Charts > Spiral Chart** shows two invented samples:
`UltraCanvasSpiralModel::DailyTemperature()`, three years of daily
temperatures, a year per turn, coloured blue to red; and `HourlyVisits()`,
four weeks of hourly visits to a shop's website, a week per turn, with a
sale on the third Thursday. The period slider rewinds the spiral (try 24
hours on the visits); the display, the colours, the direction and the guide
can be changed, and the sample under the pointer is read out.

## Tests

`Tests/SpiralModelTest.cpp` checks the geometry (turns, rings, angles,
direction, start angle, the inner and outer edges), the value range, the
samples' spans and gaps, the nearest sample, validation and both samples.
`Tests/SpiralChartRenderTest.cpp` draws the chart and checks the spiral's
circle, every January blue and every July red on opposite rays, each day's
hit region and tooltip, the band's widths, the line, the colour bar's room,
a new period (a day a turn puts 10:00 of every day on one ray) and hover.

# UltraCanvasSpiralChart — Research & Feature Proposal

**Status:** **Phase 1 is implemented** (2026-11-16) as `UltraCanvasSpiralChart`
and `UltraCanvasSpiralModel` - see [`UltraCanvasSpiralChart.md`](UltraCanvasSpiralChart.md)
for the guide. It delivered D1-D3, A1-A6, I1 and I3, the
`DailyTemperature` and `HourlyVisits` samples, the demo page
(**Charts → Spiral Chart**) with its period slider,
`Tests/SpiralModelTest.cpp` and `Tests/SpiralChartRenderTest.cpp`. Phases 2
and 3 are still to come.

How Phase 1 departs from the plan below, and why:

* **The model also has `Normalize` and `TypicalStep`.** The chart scales
  every display with one and finds gaps with the other; tests check both.
* **The turn labels sit on a light patch** over the ribbon at the start of
  each turn; without it a year's number is unreadable on a dark winter.
* **The spiral draws itself in** with the engine's animation: the samples
  appear from the first to the last.
* **The demo's colours default to blue-to-red reversed** (low blue, high
  red), the order temperatures are read in.


A spiral plot draws a time series along a spiral, one turn per period: a
day, a week, a year. Times that are one period apart land on the same ray,
one turn further out, so a cycle shows as a pattern that lines up from the
centre outwards, and a change from one cycle to the next shows as the pattern
shifting or fading. Typical uses:

* daily temperatures over several years, one turn per year: every winter
  sits on the same side;
* website visits per hour over a month, one turn per week: weekday working
  hours light up together, weekends stay dark;
* new cases of an illness per day, one turn per year, the width of a band
  showing the count (the well-known spiral of a pandemic's waves);
* sunspots, tides, heart beats - anything with a rhythm.

UltraCanvas has no chart that does this. This proposal adds one on the chart
engine, with a UI-free model of the spiral's geometry.

**Author:** UltraCanvas Framework

---

## Contents

- [1. Summary — the headline question](#1-summary--the-headline-question)
- [2. What a spiral plot is](#2-what-a-spiral-plot-is)
- [3. Reference shapes](#3-reference-shapes)
- [4. Where this belongs in the repository](#4-where-this-belongs-in-the-repository)
  - [4.1 Chart-drawn geometry, not an engine projection](#41-chart-drawn-geometry-not-an-engine-projection)
  - [4.2 Registration checklist (what "done" touches)](#42-registration-checklist-what-done-touches)
- [5. Data model sketch](#5-data-model-sketch)
  - [5.1 The model](#51-the-model)
  - [5.2 The chart](#52-the-chart)
- [6. Feature list](#6-feature-list)
- [7. The hard parts](#7-the-hard-parts)
  - [7.1 The geometry](#71-the-geometry)
  - [7.2 What a sample covers](#72-what-a-sample-covers)
  - [7.3 Finding the period](#73-finding-the-period)
  - [7.4 Labels around a circle](#74-labels-around-a-circle)
  - [7.5 Hover](#75-hover)
- [8. What already exists to build on](#8-what-already-exists-to-build-on)
- [9. Open questions (with recommendations)](#9-open-questions-with-recommendations)
- [10. Suggested phasing](#10-suggested-phasing)

## 1. Summary — the headline question

**Does any chart we have draw a spiral plot?** No. The closest:

| Candidate | What it does | What a spiral plot needs that it lacks |
|---|---|---|
| `UltraCanvasPolarChart`, `UltraCanvasEnginePolarChart` | Points, lines and areas on an angle and a radius; points keep their order, so a hand-computed spiral line can be drawn | An angle axis of at most one circle; no time axis that winds turn after turn; no ticks along a spiral, no turn labels, no band or colour ribbon along it |
| `UltraCanvasEngineRadialBarChart` | Rays from a base ring, one per category | One ring, categories not time |
| `UltraCanvasHeatmapChart` | Value as colour on a grid; a calendar heat map is the "unrolled" spiral | Rows instead of turns: the end of one period and the start of the next are far apart |

**Recommendation: a new chart type, `UltraCanvasSpiralChart`, derived from
`UltraCanvasChartEngineElement`, with a UI-free `UltraCanvasSpiralModel`**
that holds the samples and the period and maps a time to its place on the
spiral. Three displays:

* **Ribbon**: each sample a short piece of a ribbon along the spiral,
  coloured by its value through a colour map (the heat-map spiral);
* **Band**: a filled band whose width is the value (the pandemic-waves
  spiral);
* **Line**: a line that moves outward from the spiral's centre line with the
  value.

Around the spiral: spokes and labels for the parts of a period (Mon to Sun,
Jan to Dec, 00:00 to 23:00), a label at the start of each turn (2021, 2022,
week 3), a colour bar for the ribbon, and hover with the time and value.

Three phases (§10). Phase 1 draws a temperature year spiral and an hourly
week spiral, with a period the demo can change.

---

## 2. What a spiral plot is

* **Time winds outward.** A time `t` is `(t - start) / period` turns from the
  start. Its whole part is the turn (the ring), its fraction the angle. Turn
  0 is the innermost ring.
* **One ring per turn.** Each turn has the same width; a sample's mark sits in
  its ring, at its angle.
* **The period is the question.** With the right period, a cycle's peaks line
  up on one ray; with a wrong one they drift into a slanting pattern. Trying
  periods is how a spiral plot finds a rhythm (Weber, Alexa and Müller,
  *Visualizing Time-Series on Spirals*, 2001).
* **What it is not.** A polar chart (one circle, no winding time), a radar
  chart (categories), a sunburst (a hierarchy).

---

## 3. Reference shapes

| # | Shape | Needs |
|---|---|---|
| 1 | Daily temperatures over three years, one turn per year, a blue-to-red ribbon, month names around the outside, the year at the start of each turn | Ribbon (D1), colour map and colour bar (A2, A5), sector labels (A3), turn labels (A4) |
| 2 | Visits per hour over four weeks, one turn per week, a ribbon, day names around | Ribbon; a period the demo can change, so the weekly pattern can be found (I3) |
| 3 | New cases per day over three years, one turn per year, a band whose width is the count | Band (D2) |
| 4 | A value as a line weaving along the spiral | Line (D3) |
| 5 | Two series on one spiral (two cities' temperatures) | Several series (Phase 2) |

The demo recreates 1, 2 and 4 with invented data in Phase 1, and 3 with the
band.

---

## 4. Where this belongs in the repository

### 4.1 Chart-drawn geometry, not an engine projection

The engine maps a normalised `(u, v)` to the screen through a projection, and
the ternary chart added one. A spiral does not fit that model well: the same
angle occurs once per turn, so `u` (time) would have to grow without bound
and `v` (the value) would be a radial offset whose screen size depends on the
turn. Lines, polygons and hit regions would still need dense sampling along
the arc, and nothing else in the engine would gain from it.

**Recommendation:** the geometry lives in the model (§7.1), UI-free and
tested: `Position(t, offset)` gives an angle and a radius in a unit circle.
The chart draws its rings, spokes, labels and marks itself, as the ternary
chart draws its edge axes, and takes from the engine the frame, title,
theme, legend, tooltips and hit regions. The engine's axes are both hidden.

### 4.2 Registration checklist (what "done" touches)

1. `UltraCanvas/include/Plugins/Charts/Engine/Charts/UltraCanvasSpiralModel.h`
   and `UltraCanvasSpiralChart.h`, with their `.cpp` files under
   `UltraCanvas/Plugins/Charts/Engine/Charts/`, added to
   `UltraCanvas/CMakeLists.txt` next to the other engine charts.
2. `Apps/DemoApp/UltraCanvasSpiralChartExamples.cpp`, declared in
   `UltraCanvasDemo.h`, registered in the **Charts** section as
   `spiralchart` with a variant per sample, and added to the demo sources in
   the root `CMakeLists.txt`.
3. `Docs/UltraCanvas/UltraCanvasSpiralChart.md`, the guide, with a
   `## Contents`, and a row in `UltraCanvasUIElements.md` ("a time series
   wound one period per turn, to show a rhythm").
4. `Tests/SpiralModelTest.cpp` (UI-free) and `Tests/SpiralChartRenderTest.cpp`,
   in `Tests/CMakeLists.txt`.
5. A `Docs/UltraCanvas/changelog.d/` entry; `generate_doc_toc.py`,
   `check_doc_examples.py` on the guide, `check_element_catalogue.py`, and
   this proposal's baseline entries updated as the API lands.

---

## 5. Data model sketch

Everything in this section is a sketch of an API that does not exist yet.

### 5.1 The model

```cpp
#include "Plugins/Charts/Engine/Charts/UltraCanvasSpiralModel.h"

struct SpiralSample {
    double t = 0.0;                  // time, in any unit; increasing
    double value = 0.0;              // NaN = missing (a gap)
    std::string note;                // an extra tooltip line
};

struct SpiralPosition {
    double angle = 0.0;              // radians, 0 = 3 o'clock, counter-clockwise
    double radius = 0.0;             // 0..1 of the spiral's outer radius
};

struct UltraCanvasSpiralModel {
    std::string title, subtitle, valueName = "Value";
    std::vector<SpiralSample> samples;
    double period = 1.0;                       // time units per turn
    std::optional<double> start;               // time at the first turn's start (unset = first sample)
    std::vector<std::string> sectorLabels;     // evenly around the turn: Jan..Dec, Mon..Sun
    std::vector<std::string> turnLabels;       // at each turn's start: 2021, 2022, ...
    std::optional<double> valueMin, valueMax;  // unset = the data's range
    double innerRadius = 0.18;                 // the empty middle, 0..0.6 of the radius
    double startAngle = 90.0;                  // degrees where each turn starts (90 = 12 o'clock)
    bool clockwise = true;

    double Start() const;
    double TurnsOf(double t) const;            // (t - start) / period
    int TurnCount() const;                     // rings the samples need
    double RingWidth() const;                  // in units of the radius
    SpiralPosition Position(double t, double offset = 0.0) const;   // offset in ring widths
    std::pair<double, double> ValueRange() const;
    double SampleSpan(size_t i) const;         // the time sample i covers
    size_t NearestSample(double t) const;
    std::vector<std::string> Validate() const;

    static UltraCanvasSpiralModel DailyTemperature();   // invented, three years
    static UltraCanvasSpiralModel HourlyVisits();       // invented, four weeks
};
```

### 5.2 The chart

```cpp
auto chart = CreateSpiralChart("temperature", 0, 0, 640, 640);
chart->SetModel(UltraCanvasSpiralModel::DailyTemperature());
chart->SetDisplay(SpiralDisplay::Ribbon);            // or Band, Line
chart->SetColormap(HeatmapColormap::RdBu, true);     // cold blue, warm red
chart->SetRibbonWidth(0.9f);                         // share of the ring
chart->SetTimeFormatter([](double t) { return std::to_string(static_cast<int>(t)); });
chart->onSampleHover = [](int index) { /* -1 when none */ };
```

---

## 6. Feature list

| ID | Feature | Phase |
|---|---|---|
| D1 | Ribbon: a piece of ribbon per sample, coloured by its value | 1 |
| D2 | Band: a filled band, its width the value | 1 |
| D3 | Line: the value as a radial offset from the ring's inner edge | 1 |
| D4 | Several series: lines or bands side by side in the ring, with a legend | 2 |
| D5 | Points: a marker per sample, sized or coloured by value | 2 |
| A1 | The spiral's centre line as a faint guide; ring edges | 1 |
| A2 | Colour map for the ribbon (any `HeatmapColormap`, reversible); a colour for band and line | 1 |
| A3 | Spokes and sector labels around the outside | 1 |
| A4 | Turn labels at each turn's start, along the start ray | 1 |
| A5 | A colour bar with the value range under the spiral (ribbon) | 1 |
| A6 | Inner radius, start angle, direction | 1 |
| A7 | Highlighted spans: an arc marking a period of interest (a holiday, a lockdown) | 2 |
| I1 | Hover: the sample under the pointer, its time (through a formatter) and value; `onSampleHover` | 1 |
| I2 | Click: `onSampleClick` | 2 |
| I3 | The period can be changed live (the demo's slider); the spiral rewinds | 1 |
| I4 | A suggested period from the autocorrelation of the values | 2 |
| X1 | JSON of the model, CSV of the samples (`t,value,note`), through `UltraCanvasFileLoader` in the demo | 2 |
| G1 | Equal spacing along the spiral (constant speed along the arc, not constant angle) | 3 |
| G2 | Zoom into a range of turns | 3 |

---

## 7. The hard parts

### 7.1 The geometry

With `n` rings (`TurnCount`, the last sample's turn rounded up) and an inner
radius `r0`, each ring is `w = (1 - r0) / (n + 1)` wide: one extra ring's
width, because the spiral climbs a whole ring per turn and the last turn
must still fit. A time `t` at `u = (t - start) / period` turns has its
centre line at

```
radius = r0 + w * (u + 0.5)
angle  = startAngle -/+ 2 pi * frac(u)      (clockwise / anticlockwise)
```

so the inner edge of the first turn starts at `r0` and the outer edge of the
last ends at 1. An offset of `o` ring widths moves a point to
`radius + o * w`; a ribbon spans offsets -0.45 to 0.45, a band and a line
start at -0.5 (the ring's inner edge). The radius grows evenly with the
angle (an Archimedean spiral), so rings are evenly spaced.

### 7.2 What a sample covers

A ribbon piece runs from a sample's time to the next sample's (the last
reuses the step before it), so irregular sampling leaves no gaps and no
overlaps; a gap longer than three typical steps is drawn as a gap. Each
piece is drawn as a polygon sampled along its arc (at least one point every
3 degrees), so a long piece still curves.

### 7.3 Finding the period

The right period is the point of the chart. Phase 1 lets the demo change it
with a slider; Phase 2 adds `SuggestPeriods()`, the lags where the values'
autocorrelation peaks, for a "try this period" list.

### 7.4 Labels around a circle

Sector labels sit just outside the spiral at the middle of their sector,
pushed out by half their width or height depending on the angle, so they
never overlap the outer ring. Turn labels sit on the start ray, inside each
ring, only when the ring is tall enough for the text.

### 7.5 Hover

Each sample's piece of ring (its full ring width, whatever the display) is a
hit region with its tooltip: the time through the formatter, the value and
the note. Pieces never overlap (§7.2), so the pointer always finds one
sample.

---

## 8. What already exists to build on

| Need | Reuse |
|---|---|
| Base class, title, theme, legend, tooltips, hit regions | `UltraCanvasChartEngineElement` |
| Colour maps and their direction | `SampleColormap`, `HeatmapColormap` (`UltraCanvasColormap.h`) |
| Drawing its own furniture beside hidden axes | `UltraCanvasTernaryChart` (edge axes, subtitle) |
| Tests | `Tests/TernaryModelTest.cpp`, `Tests/RidgelineChartRenderTest.cpp` as templates |
| Files (Phase 2) | `UltraCanvasJSON.h`, `UltraCanvasCSVImport.h`, `UltraCanvasFileLoader` |

Nothing new is needed from the engine or the render context.

---

## 9. Open questions (with recommendations)

**Q1. An engine projection or the chart's own geometry?** **Recommendation:
the chart's own** (§4.1).

**Q2. Which display by default?** **Recommendation: Ribbon**, the most
common and the one that shows a rhythm best.

**Q3. Time as a number or as dates?** **Recommendation: a number** in any unit
(days, hours), with a formatter for the tooltip. A date type would tie the
chart to a calendar library; a formatter lets the program show dates.

**Q4. Charts or Diagrams?** **Recommendation: Charts**, as `spiralchart`.

---

## 10. Suggested phasing

| Phase | Delivers | Retires |
|---|---|---|
| **1** *(done)* | The model with the geometry; Ribbon, Band and Line (D1-D3); A1-A6; hover (I1); the demo with a temperature year spiral and an hourly week spiral, a period slider (I3), the three displays and the colour maps; the guide; model and render tests | Reference shapes 1-4 |
| **2** | Several series and points (D4, D5); highlighted spans (A7); click (I2); suggested periods (I4); JSON and CSV (X1) | Reference shape 5 |
| **3** | Equal spacing along the arc (G1); zoom into turns (G2) | — |

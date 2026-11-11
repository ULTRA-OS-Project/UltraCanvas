# UltraCanvasImportancePerformanceChart

<!-- doc-check: std::shared_ptr<UltraCanvasContainer> container; -->

The importance–performance chart plots the *factors* of a product or service
(price, delivery speed, after-sales support…) by two scores: how much
customers care about each one, and how it compares with competitors. The
plane is cut into authored **zones** that name the action: in Slack's form
(1994) *Urgent action*, *Improve*, *Appropriate* and *Excess?*. The answer the
chart gives is which factors land in which zone.

- Header: `include/Plugins/Charts/Engine/Charts/UltraCanvasImportancePerformanceChart.h`
- Model: `include/Plugins/Charts/Engine/Charts/UltraCanvasImportancePerformanceModel.h` (UI-free)
- Base class: `UltraCanvasEngineScatterChart`, on the chart engine
- Demo: **Charts → Importance-Performance Chart**
- Design: [`UltraCanvasImportancePerformanceChartProposal.md`](UltraCanvasImportancePerformanceChartProposal.md)

## Contents

- [Which element do I want?](#which-element-do-i-want)
- [Quick start](#quick-start)
- [The model](#the-model)
  - [Axes and level ranges](#axes-and-level-ranges)
  - [Zones](#zones)
  - [Bands](#bands)
  - [Factors](#factors)
- [Classification](#classification)
- [The Slack preset](#the-slack-preset)
- [Display options](#display-options)
- [Interaction](#interaction)
- [What is not built yet](#what-is-not-built-yet)

## Which element do I want?

| You want | Use |
|---|---|
| Factors over zones with curved or stepped edges (Slack), or more than four regions | This chart |
| The classic four quadrants split at the means (Martilla & James), or any 2×2 matrix | `UltraCanvasQuadrantChart` with `QuadrantType::Custom` and `xCenter` / `yCenter` at the means |
| A grid of people rated on performance and potential (a 9-box) | Not this chart: see `UltraCanvasPerformanceMatrixProposal.md` |
| A scatter plot with a hand-drawn region on it | `UltraCanvasEngineScatterChart` and an `AddHighlight` of `ChartHighlightShape::Polygon` |

## Quick start

```cpp
#include "Plugins/Charts/Engine/Charts/UltraCanvasImportancePerformanceChart.h"
using namespace UltraCanvas;

ImportancePerformanceModel model = ImportancePerformanceSamples::SlackZones(true);   // smooth zones
model.title = "Parcel service";
model.factors = {
    {"Delivery reliability", "Delivery", 2.0, 8.0},   // label, group, importance, performance
    {"Price",                "Delivery", 1.4, 5.0},
    {"Tracking app",         "Service",  5.0, 3.0},
    {"Gift wrapping",        "Service",  8.4, 2.0},
};

auto chart = CreateImportancePerformanceChart("ipm", 0, 0, 840, 600);
chart->SetModel(model);
container->AddChild(chart);
```

The Slack preset uses Slack's nine-point scales, on which **1 is the best
grade** (strongly order-winning, far better than competitors). Both axes are
therefore inverted, so the most important factors are on the right and the
best performance at the top, as readers expect.

## The model

`ImportancePerformanceModel` is plain data. `SetModel` takes a copy, so
change the model and set it again to update the chart.

### Axes and level ranges

`importance` (x) and `performance` (y) are `ImportancePerformanceAxis`:

| Field | Meaning |
|---|---|
| `title` | Axis title (one line) |
| `min`, `max` | The range in score units. The Slack preset uses 0.5 .. 9.5 |
| `inverted` | Draw `min` at the far end (right / top): Slack's "1 = best" |
| `levels` | Named ranges in value order: `{"Order winning", 0.5, 3.5}` |
| `lowCaption`, `highCaption` | Captions at the screen's low and high ends ("Low" / "High", "Bad" / "Good"). They stay with the screen ends whatever `inverted` says |

The chart labels each level at the centre of its range and puts a tick mark
without a label at each boundary between levels. `LevelOf(axis, value)`
returns the level a score falls in: ranges are half-open, and the last level
keeps its upper edge.

```cpp
ImportancePerformanceAxis importance;
importance.title = "Importance for customers";
importance.min = 1.0;
importance.max = 5.0;
importance.lowCaption = "Low";
importance.highCaption = "High";
importance.levels = {{"Nice to have", 1.0, 3.0}, {"Must have", 3.0, 5.0}};
int level = ImportancePerformanceModel::LevelOf(importance, 4.2);   // 1, "Must have"
```

### Zones

An `ImportancePerformanceZone` is a closed `outline` in value space with a
`title`, an `action` (shown in tooltips), an optional `iconPath` (drawn above
the title), a `fill` and a `textColor`. **Later zones cover earlier ones**,
both in the drawing and in classification. That makes a zone list easy to
write: start with one zone covering the whole plane, then lay the
exceptions over it.

`smoothing` rounds the outline with Chaikin corner cutting (0 = as given,
up to 6 passes). An outline may run past the axis ranges, because the chart
clips to the plot. Let it: rounding cuts corners, and a corner that lies
outside the plot cannot leave a gap inside it.

```cpp
ImportancePerformanceZone focus;
focus.id = "focus";
focus.title = "Concentrate here";
focus.action = "Important and underperforming";
focus.fill = Color(214, 34, 76, 255);
focus.outline = {{3.0, 0.0}, {6.0, 0.0}, {6.0, 3.0}, {3.0, 3.0}};
focus.smoothing = 0;
```

Each zone title is drawn at the zone's *pole of inaccessibility*: the
point of the zone's visible part (what later zones leave uncovered, inside
the plot) that lies farthest from every edge. That is not the centroid,
which can fall outside a concave zone or under a later one. A title that
does not fit is left out, and so is an icon. The title still shows in the
zone's tooltip.

### Bands

An `ImportancePerformanceBand` is a labelled thick line along an open
polyline, such as Slack's *lower bound of acceptability*. It is drawn over
the zones, with `width` in y-axis units, and its `smoothing` keeps the two
end points. A band takes no part in classification.

Its label runs along the longest straight stretch of the band inside the
plot, turned with it and never upside down. On a band thinner than about
12 px the label sits beside the line instead. Factor names steer around
both the band labels and the zone titles.

### Factors

`ImportancePerformanceFactor` has `id`, `label`, `group`, the two scores
`importance` and `performance`, and a `note` added to the tooltip. Each
distinct `group` becomes one scatter group, in order of first appearance,
with its own legend entry. Clicking an entry hides the group.

Group marks default to dark colours with a white outline, so they read on
any zone colour, and to a different shape per group (circle, square,
diamond, triangle), so a group never depends on colour alone. Change them
with `SetGroupStyles`:

```cpp
chart->SetGroupStyles({Color(20, 20, 20, 255), Color(255, 255, 255, 255)},
                      {ChartMarkerShape::Circle, ChartMarkerShape::Square});
```

## Classification

```cpp
std::vector<int> zones = model.ClassifyFactors();   // one zone index per factor, -1 = none
int zone = model.ZoneOf(2.0, 8.0);                  // the zone of one point
std::vector<std::string> problems = model.Validate();
```

The chart draws exactly the polygon that classification tests:
`ZoneShape(i)` is the outline after its smoothing, in value space. The chart
maps that polygon to the screen and does not smooth again. A factor drawn
inside *Improve* is therefore always reported in *Improve*.
`GetFactorZones()` on the chart returns the same list as `ClassifyFactors()`
on the model it was given.

`Validate()` reports an empty or reversed axis range, levels that overlap
or run outside the range, zones with fewer than three points, bands with
fewer than two, and factors that are missing a score or lie outside the
axis ranges.

## The Slack preset

`ImportancePerformanceSamples::SlackZones(bool smooth)` builds Slack's
matrix: nine-point inverted scales read in thirds (*order winning /
qualifying / less important*; *better than / same as / worse than*), the four
zones, and the lower bound of acceptability as a band.

- `SlackZones(false)` is the textbook form: the boundaries are staircases
  on the 9×9 grid, and the band is a thin line.
- `SlackZones(true)` is the slide form: the boundaries are smooth curves
  through a thinned set of the same corners, and the band is wide with its
  label inside.

`ImportancePerformanceSamples::SampleFactors()` returns twelve invented
factors of a parcel carrier that land in all four zones, in both forms.

## Display options

| Call | Effect |
|---|---|
| `SetShowZoneTitles(bool)` / `SetShowZoneIcons(bool)` | Zone titles and icons (on) |
| `SetShowBandLabels(bool)` | Band labels (on) |
| `SetShowLevelRanges(bool)` | Level names, boundary ticks and end captions on the axes (on); off = plain numbers |
| `SetShowGrid(bool)` | Grid lines over the zones (off: the zones are the grid) |
| `SetZoneTitleFontSize(float)` | Zone title size (14) |
| `SetPointLabelMode(EngineScatterLabelMode)` | Factor names, from the scatter chart: `Outside` (the default here) or `Hidden` |

The title is `model.title`. Everything else the scatter chart offers (theme,
legend position, marker radius and outline, animation) still applies.

```cpp
chart->SetShowGrid(true);
chart->SetShowLevelRanges(false);
chart->SetPointLabelMode(EngineScatterLabelMode::Hidden);
chart->SetMarkerRadius(7.0f);
```

## Interaction

Hovering a factor shows its label, group, both scores with their level
names, its zone and the zone's action. Hovering a bare stretch of a zone
shows the zone's title, action and how many factors it holds. Clicking a
legend entry hides that group, and its factors stop answering the
pointer.

## What is not built yet

This is Phase 1 of the proposal. Still to come:

- **Phase 2:** a zone legend, emphasising a hovered zone's factors, a list
  of factors per zone as an API (the demo builds one from
  `GetFactorZones()`), Martilla & James and diagonal presets, and JSON and
  CSV interchange through `UltraCanvasFileLoader`.
- **Phase 3:** arrows showing how a factor moved since an earlier survey.
- Axis titles are one line. A two-line title such as "Performance / Digital
  Competition" is not supported yet.

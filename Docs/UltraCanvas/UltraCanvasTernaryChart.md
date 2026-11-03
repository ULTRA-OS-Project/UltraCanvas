# UltraCanvasTernaryChart

<!-- doc-check: std::shared_ptr<UltraCanvasContainer> container; -->

A ternary chart draws things made of three parts - a three-party vote, the
sand, silt and clay of a soil sample, an alloy - as points in an equilateral
triangle. Each corner is all of one part, the edge opposite it none of it, and
the three shares of every point add up to 100 %.

- Header: `include/Plugins/Charts/Engine/Charts/UltraCanvasTernaryChart.h`
- Model: `include/Plugins/Charts/Engine/Charts/UltraCanvasTernaryModel.h` (UI-free)
- Base class: `UltraCanvasChartEngineElement`, on the engine's `Ternary`
  projection ([`UltraCanvasChartEngine.md`](UltraCanvasChartEngine.md))
- Demo: **Charts → Ternary Chart**
- Design: [`UltraCanvasTernaryChartProposal.md`](UltraCanvasTernaryChartProposal.md)

## Contents

- [Quick start](#quick-start)
- [The model](#the-model)
  - [Points keep their amounts](#points-keep-their-amounts)
  - [Corners and axes](#corners-and-axes)
- [Reading direction](#reading-direction)
- [What the chart draws](#what-the-chart-draws)
- [Interaction](#interaction)
- [Samples](#samples)
- [What is not built yet](#what-is-not-built-yet)

## Quick start

```cpp
#include "Plugins/Charts/Engine/Charts/UltraCanvasTernaryChart.h"

TernaryModel model;
model.title = "Alloy batches";
model.axes[0] = TernaryAxis("Copper");     // A, the apex
model.axes[1] = TernaryAxis("Zinc");       // B, bottom left
model.axes[2] = TernaryAxis("Tin");        // C, bottom right

TernarySeries batches("Batch 12");
batches.points.emplace_back(62.0, 35.5, 2.5, "12-a");   // grams, any unit
batches.points.emplace_back(58.0, 39.0, 3.0, "12-b");
model.series.push_back(batches);

auto chart = CreateTernaryChart("alloys", 0, 0, 640, 520);
chart->SetModel(model);
container->AddChild(chart);
```

## The model

### Points keep their amounts

A `TernaryPoint` holds the amounts it was given - votes, grams, percentages
from a lab - and its label and note. `Shares(a, b, c)` divides each by their
sum, so the shares always add up to 1, and a lab result that sums to 99.7 is
drawn rather than refused. The raw amounts stay in the data, so the tooltip
can say "41200 votes (43.3 %)". A point with a negative or non-finite part, or
parts summing to 0, has no shares: it is not drawn, and `Validate()` names it.

Points come in `TernarySeries`, each with a name (the legend entry), a colour
(transparent = from the theme palette), a marker shape and a marker radius.

### Corners and axes

The corners are fixed: `axes[0]` is A at the apex, `axes[1]` B at the bottom
left, `axes[2]` C at the bottom right. Which part goes to which corner is the
model's choice. Each `TernaryAxis` has a `title` (drawn at its corner and
along its edge), a `unit` for the edge title ("%" by default) and a
`tickStep` as a share (0.1 = every 10 %).

```cpp
TernaryModel soil = TernarySamples::SoilSamples();
soil.axes[0].tickStep = 0.2;                 // clay every 20 %
std::vector<double> ticks = soil.Ticks(0);   // 0, 0.2, 0.4 ... 1
std::vector<std::string> problems = soil.Validate();
```

## Reading direction

A share is read by following its grid line - the line parallel to the edge
opposite its corner - from the point to the edge that carries that part's
numbers. Which edge that is depends on the reading direction, and both are
in print:

| `TernaryReading` | A's numbers | B's numbers | C's numbers |
|---|---|---|---|
| `Clockwise` (the USDA soil triangle) | up the left edge | right to left along the bottom | down the right edge |
| `Anticlockwise` | up the right edge | down the left edge | left to right along the bottom |

One flag sets the labels and the ticks together, so they can never disagree.
Each tick is drawn along the grid line it belongs to, poking out of the
triangle, so the eye follows the right line from the number. The edge title
carries an arrow pointing the way the share grows; text never runs upside
down, so a share that grows right to left reads "← Sand (%)".

```cpp
chart->SetReading(TernaryReading::Anticlockwise);
Point2Dd tick = chart->TickPosition(0, 0.2);   // where "20" of A sits
```

## What the chart draws

- The triangle, filled as the plot area, and its grid: the lines of equal
  share for each part, at its ticks (`SetShowGrid(false)` hides them).
- Three edge axes with their ticks and numbers, an axis title along each
  labelled edge with its arrow (`SetShowEdgeTitles(false)` hides them), and
  each part's name at its corner.
- The points as `DrawChartMarker` markers in their series' colours. While
  the engine's entrance animation runs, they spread out from the centre.
- A legend of the series when there is more than one.
- Point labels, when `SetShowPointLabels(true)`, through the engine's label
  plan. They steer around the tick numbers and titles. A label the solver
  could only park far from its point is left out (the name is still in the
  tooltip): across a crowded triangle a long leader reads worse than none.

## Interaction

Hovering a point enlarges it and shows its tooltip: its label, its series,
each part's share and, when the amounts are not already percentages, the raw
amounts. A click calls `onPointClick(seriesIndex, pointIndex)`. A click on a
legend entry hides that series; `SetSeriesVisible` does the same from code.

After the first render, the geometry is available for tests and overlays:
`CornerPoint`, `SharesToScreen`, `ScreenToShares` (false outside the
triangle), `TickPosition`, `PointPosition` and `GetHoveredPoint`.

```cpp
chart->onPointClick = [](size_t series, size_t point) {
    (void)series;
    (void)point;
};
double a = 0.0, b = 0.0, c = 0.0;
if (chart->ScreenToShares(Point2Dd(320.0, 300.0), a, b, c)) {
    std::string text = FormatTernaryShare(a);   // "38.1%"
}
std::string tip = chart->BuildPointTooltip(0, 1);
```

## Samples

| Sample | What it shows |
|---|---|
| `TernarySamples::ThreePartyVotes()` | Votes for three invented parties in twelve invented regions at two elections, read anticlockwise |
| `TernarySamples::SoilSamples()` | Invented soil samples on the USDA layout (clay at the apex, sand left, silt right), read clockwise; some sum to a little more or less than 100, as lab results do |

Every name and number in the samples is made up.

## What is not built yet

Phase 1 of the proposal is built. Still to come:

- **Phase 2:** named regions in share space with classification (the USDA
  soil texture classes, the De Finetti diagram), paths with arrowheads,
  bubble sizes, reference curves, a reading guide under the pointer, and
  JSON and CSV through `UltraCanvasFileLoader`.
- **Phase 3:** zoom to a sub-triangle, and density by triangular binning
  with iso-lines.

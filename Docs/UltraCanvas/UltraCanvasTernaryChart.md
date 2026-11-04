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
- [Regions](#regions)
- [Paths, bubbles and curves](#paths-bubbles-and-curves)
- [Reading direction](#reading-direction)
- [What the chart draws](#what-the-chart-draws)
- [Interaction](#interaction)
- [Files: JSON and CSV](#files-json-and-csv)
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

## Regions

A region is an authored area of the triangle - a soil texture class, a rock
field - given as its corners in order, in any unit, as points are. Regions
are filled under the grid, outlined over it and named inside themselves, at
the inside point farthest from their edges. A name that does not fit on one
line wraps at its spaces or steps the font down; one that still does not fit
is left to the region's tooltip.

```cpp
TernaryModel soil = TernarySamples::SoilTexture();      // the twelve USDA classes
soil.series = TernarySamples::SoilSamples().series;     // invented samples over them

TernaryRegion field;
field.id = "target";
field.name = "Target blend";
field.outline = {TernaryComposition(60, 30, 10), TernaryComposition(50, 40, 10),
                 TernaryComposition(50, 30, 20)};
field.fill = Color(200, 230, 255, 255);
soil.regions.push_back(field);

int region = soil.RegionOf(27.3, 22.4, 50.1);            // the class a sample falls in
```

`RegionOf` classifies by the geometry the chart draws, in share space. A point
exactly on an edge two regions share belongs to the region listed first, so
the order of `regions` is the tie-break.

The USDA preset's outlines are derived from the class rules of the USDA Soil
Survey Manual (loam: clay 7-27 %, silt 28-50 %, sand at most 52 %, and so
on), not drawn by hand. `TernaryModelTest` checks every class against those
rules on a grid of 19,900 compositions.

Hovering a region shows its name and note, a click calls
`onRegionClick(regionIndex)`, and a point's tooltip names the region it is
in. `SetShowRegionLabels(false)` hides the names.

## Paths, bubbles and curves

- **Paths.** A series with `kind = TernarySeriesKind::Path` joins its points
  in order - a composition changing over time. With `arrows` set, each step
  ends in an arrowhead.
- **Bubbles.** A point's `size` is a fourth value drawn as the marker's area,
  scaled against the largest size in the model between the radii given to
  `SetBubbleRadiusRange` (4 and 18 px by default). Points without a size keep
  their series' marker. Bubbles are translucent, so those under them show.
- **Curves.** A `TernaryCurve` is a reference line through given
  compositions, dashed by default: the Hardy-Weinberg parabola of a De Finetti
  diagram, a mixing line. Curves are listed in the legend after the series,
  and a click there hides one, as `SetCurveVisible` does.

```cpp
TernaryModel swings = TernarySamples::VoteTrajectories();   // paths with arrows
TernaryModel genetics = TernarySamples::DeFinetti();        // bubbles and a curve
chart->SetBubbleRadiusRange(3.0, 22.0);
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
amounts, then its region and size when it has them. A click calls
`onPointClick(seriesIndex, pointIndex)`; a click on a region outside any point
calls `onRegionClick`. A click on a legend entry hides that series or curve;
`SetSeriesVisible` and `SetCurveVisible` do the same from code.

`SetReadingGuide(true)` turns on the reading guide: while the pointer is in
the triangle, the three grid lines through it are drawn to the edges that
carry their numbers, and a box above the pointer gives its three shares and
region. `GetGuideShares` reports them.

After the first render, the geometry is available for tests and overlays:
`CornerPoint`, `SharesToScreen`, `ScreenToShares` (false outside the
triangle), `TickPosition`, `PointPosition`, `PointRadius`,
`RegionLabelCentre`, `GetHoveredPoint` and `GetHoveredRegion`.

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

## Files: JSON and CSV

The model functions take and return text; reading and writing the file is
`UltraCanvasFileLoader`'s job, as for every file a user opens or saves.

**JSON** carries the whole model - title, axes, reading, series with their
points, regions and curves - as `"format": "ultracanvas-ternary"`,
`"version": 1`. A point's parts are an array of three numbers. A file of
another format, a newer version, an unknown series kind or reading, an
unreadable colour or a model that does not pass `Validate()` is refused with
the reason, and the output model is left untouched.

```cpp
const std::string json = ExportTernaryJson(chart->GetModel());
TernaryModel opened;
std::string error;
if (ImportTernaryJson(json, opened, &error)) chart->SetModel(opened);
```

**CSV** carries the points alone, one per line:

```
label,series,Clay,Sand,Silt,size,note
R-01,River terrace,8.2,71.5,20.1,,
```

The three part columns are found by the axes' titles - an exact match
first, then one that differs only in letter case (the genotypes `AA`, `Aa`
and `aa` differ only in case), then `a`, `b`, `c`. The other columns may be
left out and come in any order. Numbers are dot-decimal whatever the locale,
so `"2,5"` is refused, not read as 2. `ImportTernaryCsv` replaces the model's
points: a series named in the file keeps its look when the model already has
it, and lines without a series go to "Points". A wrong field count, a part or
size that is not a number, or a missing part column is refused with the line,
and the model is left untouched.

## Samples

| Sample | What it shows |
|---|---|
| `TernarySamples::ThreePartyVotes()` | Votes for three invented parties in twelve invented regions at two elections, read anticlockwise |
| `TernarySamples::SoilSamples()` | Invented soil samples on the USDA layout (clay at the apex, sand left, silt right), read clockwise; some sum to a little more or less than 100, as lab results do |
| `TernarySamples::SoilTexture()` | The USDA soil texture triangle: the twelve classes as regions on the same layout, no points |
| `TernarySamples::DeFinetti()` | Genotype shares (AA, Aa, aa) of invented populations as bubbles sized by sample, with the Hardy-Weinberg curve; the isolated populations lack heterozygotes |
| `TernarySamples::VoteTrajectories()` | Four invented regions' vote shares over five elections, as paths with arrowheads |

Every name and number in the samples is made up.

## What is not built yet

Phases 1 and 2 of the proposal are built. Still to come, as Phase 3: zoom to
a sub-triangle, and density by triangular binning with iso-lines.

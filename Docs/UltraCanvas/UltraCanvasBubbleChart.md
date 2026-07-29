# UltraCanvasBubbleChart

Comprehensive bubble chart element for UltraCanvas
(`include/Plugins/Charts/UltraCanvasBubbleChart.h`,
`Plugins/Charts/UltraCanvasBubbleChart.cpp`).

A bubble chart displays data points as circles whose **area** carries a third
data dimension on top of the position (and optionally a fourth dimension via
colour). The element implements the three classic bubble chart families
described by Tableau's chart guide ("What is a bubble chart?") and EdrawSoft's
bubble chart documentation, plus an OpenGL-based 3D variant in the demo app:

| Mode | Enum | Description |
| --- | --- | --- |
| Scatter bubbles | `BubbleChartMode::ScatterBubbles` | Scatter plot with size (3rd) and colour (4th) encodings, axes and grid |
| Packed bubbles / bubble cloud | `BubbleChartMode::PackedBubbles` | Axis-free circle packing (Tableau "Packed Bubbles"); size and colour carry the message; optional enclosure circle |
| Bubble matrix | `BubbleChartMode::BubbleMatrix` | Categorical rows x columns grid of proportionally sized bubbles (survey/infographic style) |
| Hierarchical packed | `BubbleChartMode::HierarchicalPacked` | Two-level circle packing: children packed inside tinted, labelled parent circles; parents packed against each other (d3-style hierarchy) |
| Timeline bubbles | `BubbleChartMode::TimelineBubbles` | Named categorical rows x continuous (time) X axis, bubble area = value (Tableau "orders per month" style) |

The demo (`Apps/DemoApp/UltraCanvasBubbleChartExamples.cpp`) recreates two
classic reference charts natively:

* **"Mrs. President"** (CBS News, June 2008) — a bubble matrix of Yes/No survey
  percentages by age group and gender, with per-row colours and in-bubble
  values.
* **"Concerns when traveling"** — scatter bubbles where X = cost concern,
  Y = safety concern, bubble area = health concern and darkness = food concern,
  including the nested-circles size legend (46.1 / 20.5 / 5.1) and the
  "Darkness relates to Food concerns" footnote.

## Creating a chart

```cpp
#include "Plugins/Charts/UltraCanvasBubbleChart.h"

auto scatter = CreateBubbleChartElement("chart1", 20, 20, 640, 480);           // ScatterBubbles
auto packed  = CreatePackedBubbleChart("chart2", 20, 20, 600, 500);            // PackedBubbles
auto matrix  = CreateBubbleMatrixChart("chart3", 20, 20, 420, 560,
                                       {"Under 45", "45-64"}, {"Yes", "No"});  // BubbleMatrix
```

## Data

```cpp
// Scatter / packed: x, y, size, colorValue (4th dimension), name, category
scatter->AddBubble(9, 79, 46.1, 82, "Mexico");
packed->AddBubble(0, 0, 236, 0, "Spotify");     // x/y ignored in packed mode

// Matrix: define the grid, then fill cells
matrix->AddMatrixBubble("Under 45", "Yes", 79);
matrix->AddMatrixBubble("Under 45", "No", 16);

chart->ClearBubbles();
```

Each bubble is a `BubbleDataPoint { x, y, size, colorValue, name, category,
overrideColor }`. A non-transparent `overrideColor` always wins over the colour
mode.

## Size encoding

* `SetSizeScale(BubbleSizeScale::Area)` *(default)* — value proportional to the
  circle **area**, the perceptually honest mapping recommended by every chart
  guide. `Diameter` maps the value to the diameter instead (exaggerates
  differences).
* `SetRadiusRange(minPx, maxPx)` — pixel radius range (scatter mode; packed
  rescales to fit, matrix derives the maximum radius from the cell size).
* `SetSizeDomain(min, max)` / `SetAutoSizeDomain()` — the value range mapped
  onto the radius range. Matrix mode auto-anchors the domain at zero so cell
  areas compare directly.

## Colour encoding

`SetColorMode(BubbleColorMode::...)`:

* `Uniform` — `SetUniformColor(color)`
* `Palette` — cycles `SetPalette({...})` by bubble index (colour-blind-friendly
  default palette built in)
* `CategoryMap` — `SetCategoryColorMap({{"Europe", blue}, ...})` keyed on the
  bubble's `category`
* `ColormapByValue` / `ColormapBySize` — continuous colormap over the
  `colorValue` (4th dimension) or the size itself:
  `SetColormap(HeatmapColormap::Viridis)`, `SetCustomColormap({c0, c1, ...})`,
  `SetColorDomain(min, max)`.

`SetBubbleAlpha(0..1)` multiplies the fill alpha (useful for overlapping
scatter bubbles). Matrix mode also supports `SetRowColorMap()` — a colour per
row name that beats the colour mode (used for the purple "Men" / orange
"Women" rows in the demo).

## Style

* `SetBubbleStyle(BubbleRenderStyle::Flat | Shaded | Glossy)` — solid fill,
  soft radial-gradient 3D shading, or shading plus a specular highlight.
* `SetBubbleBorder(color, width)`
* `SetHighlightOnHover(bool)` — brighten + outline the hovered bubble.

## Labels

* `SetNameLabelMode(Hidden | Inside | Below | Auto)` — `Auto` places the name
  inside the bubble when it fits and below otherwise. Inside labels pick a
  contrasting text colour (white on dark fills) automatically.
* `SetValueLabelMode(Hidden | Inside | Auto)` — numeric value labels, formatted
  with `SetValueDecimals(n)` and `SetValueSuffix("%")`.
* `SetLabelFontSize(px)`, `SetNameLabelColor(color)`,
  `SetMinRadiusForInsideLabels(px)`.
* Outside (below) labels are placed collision-free by the shared
  `PlaceShapeLabels()` solver (`UltraCanvasLabelPlacement.h`): each label
  prefers the spot directly under its bubble but moves to the least crowded
  alternative when that would cover a neighbouring bubble, another label, or
  a group label. Smaller bubbles get placement priority, since their labels
  have the least room to move.
* In hierarchical mode the outside labels instead straddle their bubble's rim
  at the 2 o'clock position (falling back to 4, 10 and 8 o'clock, then the
  outside sides), stay within their own group circle, and are drawn with a
  light halo so the part over the bubble fill stays readable — the same
  convention node/adjacency diagrams use for labels drawn over their nodes.

## Size legend and annotations

* `SetShowSizeLegend(true)` — nested-circles legend (largest at the back,
  tangent at the bottom) drawn in a reserved band right of the plot.
* `SetSizeLegendTitle("Health")`, `SetSizeLegendValues({46.1, 20.5, 5.1})`
  (empty = automatic max/mid/min). Legend radii always use the same mapping as
  the drawn bubbles.
* `SetFootnote("Darkness relates to Food concerns")` — small caption centred
  under the plot.

## Packed layout

* `SetPackedSortOrder(SizeDescending | SizeAscending | DataOrder)` — placement
  order; biggest-first yields the classic centred Tableau cloud.
* `SetPackedPadding(px)` — gap kept between circles.
* `SetPackedEnclosure(show, strokeColor, strokeWidth, fillColor)` — draws an
  outlined enclosure circle around the cluster and scales the packing to fill
  it (the "circle packing in a circle" look).

The layout is a greedy tangent packing: each circle is placed at the candidate
position tangent to two already-placed circles that is closest to the cluster
centre, then the whole cluster is uniformly scaled to fit the plot area (or
the enclosure circle when enabled).

## Hierarchical packing

`HierarchicalPacked` groups bubbles by their `category`: the children of each
group are packed around their own centre, wrapped in a parent circle (size
derived automatically from the packed children plus `SetGroupPadding()` and
label headroom), and the parent circles are then packed against each other.

* Children take the saturated group colour (`SetCategoryColorMap()` or a
  stable palette fallback); the parent fill is the same colour lightened by
  `SetGroupTint(0..1)`.
* The group name is drawn in the headroom ring between the children and the
  parent rim; `SetGroupLabelStyle(fontSize, color, bold)` styles it
  (a transparent colour means automatic contrast against the parent fill).
  A name wider than the ring is chord-fitted: it is pulled towards the centre
  until its corners sit inside the parent circle instead of poking through
  the rim, and the bubble name labels keep clear of it.

```cpp
auto hier = CreateHierarchicalBubbleChart("launches", 20, 20, 700, 600);
hier->AddBubble(0, 0, 96, 0, "Falcon", "The United States");
hier->AddBubble(0, 0, 48, 0, "Long March", "China");
hier->SetCategoryColorMap({{"The United States", blue}, {"China", orange}});
hier->SetGroupTint(0.55f);
```

## Timeline bubbles

`TimelineBubbles` plots named rows against a continuous X value with bubble
area as the measure — the Tableau "which months had the highest number of
orders?" presentation. The size scale is zero-anchored.

* `CreateTimelineBubbleChart(id, x, y, w, h, rows)` /
  `SetTimelineRows({...})` — optional explicit row order (otherwise rows
  appear in insertion order).
* `AddTimelineBubble(rowName, xValue, size, colorValue = 0)`.
* `SetXAxisLabelFormatter(fn)` — formats the X tick labels (e.g. a month
  index into `"June 2014"`); tick count adapts to the plot width.
* Row labels reuse `SetMatrixRowLabelStyle()`; separators and vertical
  gridlines use the grid colour.

```cpp
auto tl = CreateTimelineBubbleChart("orders", 20, 20, 900, 600,
                                    {"Africa", "Canada", "EMEA"});
tl->AddTimelineBubble("EMEA", 31, 173);          // month index 31, 173 orders
tl->SetXAxisLabelFormatter(monthName);           // 31 -> "June 2014"
```

## Interactivity

* Tooltips through the framework tooltip system (`SetEnableTooltips`,
  `SetCustomTooltipGenerator`); content adapts to the mode.
* `onBubbleClick = [](size_t index, const BubbleDataPoint& b) { ... };`
* Hover highlight, grow-in animation on data load (inherits the base chart
  animation switches).

## 3D bubble chart (demo)

`Apps/DemoApp/UltraCanvasBubbleChartExamples.cpp` includes a "3D Bubbles
(OpenGL)" tab (built when `ULTRACANVAS_ENABLE_GL` is on) rendering spheres in a
3-axis data space through `UltraCanvasGLSurface`: one shared icosphere mesh
with per-bubble model matrices, Blinn-Phong + rim shading, back-to-front
sorted semi-transparent spheres, an axis cube with a floor grid, mouse-orbit
camera, wheel zoom and auto-rotation — sphere **volume** encodes the fourth
dimension (investment) as suggested by EdrawSoft's 2-D/3-D bubble chart types.

## Feature research sources

The API consolidates the bubble chart types, encodings and best practices
described on:

* Tableau — "What is a bubble chart?" (`tableau.com/chart/what-is-bubble-chart`):
  scatter-with-size bubbles, packed bubbles, size-by-area best practice,
  labelling and legend conventions.
* EdrawSoft — "Bubble Chart" (`edrawsoft.com/bubble-chart.html`) and
  "Visualize Your Data with Bubble Chart Templates"
  (`edrawsoft.wondershare.fr/visualize-data-with-bubble-chart-template.html`):
  2-D vs 3-D bubble charts, bubble clouds (circle packing), bubble maps,
  three-variable comparison and glossy bubble styling.

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
| Packed bubbles / bubble cloud | `BubbleChartMode::PackedBubbles` | Axis-free circle packing (Tableau "Packed Bubbles"); size and colour carry the message |
| Bubble matrix | `BubbleChartMode::BubbleMatrix` | Categorical rows x columns grid of proportionally sized bubbles (survey/infographic style) |

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

The layout is a greedy tangent packing: each circle is placed at the candidate
position tangent to two already-placed circles that is closest to the cluster
centre, then the whole cluster is uniformly scaled to fit the plot area.

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

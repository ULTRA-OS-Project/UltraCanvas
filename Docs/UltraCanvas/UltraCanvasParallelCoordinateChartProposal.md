# UltraCanvasParallelCoordinateChart — Research & Feature Proposal

Status: **Proposed — nothing implemented yet.** This document is the research
write-up and the roadmap for a comprehensive parallel coordinate chart element.
It follows the pattern established by
[`UltraCanvasContourChartProposal.md`](UltraCanvasContourChartProposal.md):
research first, then a phased feature list, an API sketch, and the open
questions that need a decision before coding starts.

Author: UltraCanvas Framework
Last Modified: 2026-07-31

---

## 1. What a parallel coordinate chart is

A parallel coordinate plot (PCP, also *parallel coordinates*, *||-coords*)
visualises **many quantitative dimensions at once** by giving each dimension its
own axis, drawing those axes *parallel* to one another (conventionally vertical
and evenly spaced) instead of orthogonal. A single record — one row of an
*n*-column table, i.e. one point in *n*-dimensional space — becomes a
**polyline** whose vertex on axis *k* sits at that record's value for dimension
*k*. A table of 150 rows × 4 columns becomes 150 polylines crossing 4 axes.

The technique is usually attributed to Alfred Inselberg (1959 onwards, and the
1985 *The Plane with Parallel Coordinates* paper) and, earlier, to Maurice
d'Ocagne (1885). Its mathematical foundation is the **point–line duality**: a
point in Cartesian space maps to a line in parallel coordinates and vice versa,
which is why structure in the data shows up as structure in the drawing.

Reading rules — these are the whole reason the chart exists, and every design
decision below serves them:

| Pattern between two neighbouring axes | Meaning |
|---|---|
| Lines run roughly **parallel** | Positive correlation between the two dimensions |
| Lines **cross in a tight X** (an hourglass waist) | Negative correlation; the waist location is the crossing point of the dual |
| Lines fan out with no structure | Little or no correlation |
| A **bundle** of lines sharing a corridor | A cluster / regime in the data |
| A single line leaving the envelope | An outlier |

Two structural facts follow, and both are design constraints:

1. **Only neighbouring axes can be compared directly.** Relationships are
   visible between *adjacent* axes, so **axis order is a first-class,
   user-controlled property** — with *n* axes there are *n!* orderings and only
   *n − 1* adjacencies are shown at a time. Interactive reordering is not a
   nicety; it is how the chart is used.
2. **Over-plotting is the dominant failure mode.** Beyond a few hundred records
   the plot turns into a solid block. Alpha blending, filtering (brushing) and
   density rendering are what keep it readable — brushing is described in the
   literature as effectively *mandatory* for real datasets.

Typical uses: multivariate exploratory analysis, machine-learning
hyper-parameter sweeps (the archetypal HiPlot/Optuna use case), engineering
design-of-experiments trade-offs, sensor/ensemble comparison, quality control,
country/entity comparison across indicators, and any "how do these many metrics
trade off against each other" question.

Related chart types already in the framework: the **radar chart** is a parallel
coordinate plot with the axes wrapped onto a circle (`UltraCanvasRadarChartElement`
— its axis/series data model is the closest precedent here); the **jitter /
beeswarm plot** solves the categorical-axis and hue-mapping problems this chart
also needs; **Andrews plots** and **parallel sets** (categorical-only, ribbon
widths by count) are neighbouring variants worth noting but out of scope.

Sources consulted:
[Wikipedia — Parallel coordinates](https://en.wikipedia.org/wiki/Parallel_coordinates),
[Spotfire — What is a parallel coordinate plot](https://www.spotfire.com/glossary/what-is-a-parallel-coordinate-plot),
[Spotfire Analyst — Parallel coordinate plot docs](https://docs.tibco.com/pub/sfire-analyst/14.0.8/doc/html/en-US/TIB_sfire_client/client/topics/en-US/parallel_coordinate_plot.html),
[Domo — Parallel Coordinates Plot: Definition, Uses, and Tips](https://www.domo.com/learn/charts/parallel-coordinates-plot),
[Analytics Vidhya — Guide to Visual Data Mining using Parallel Coordinates](https://www.analyticsvidhya.com/blog/2025/09/parallel-coordinates/),
[EDAV — Parallel coordinates plot cheatsheet](https://jtr13.github.io/cc21fall1/parallel-coordinates-plot-cheatsheet.html),
[Syntagmatic — Parallel Coordinates (d3 library, brush modes)](https://syntagmatic.github.io/parallel-coordinates/),
[Plotly — Parallel coordinates plot](https://plotly.com/javascript/parallel-coordinates-plot/),
[HiPlot (Facebook Research)](https://github.com/facebookresearch/hiplot),
[PC-Expo: A Metrics-Based Interactive Axes Reordering Method](https://arxiv.org/pdf/2208.03430),
[Confluent-Drawing Parallel Coordinates](https://arxiv.org/pdf/1906.10017),
[GPU accelerated scalable parallel coordinates plots](https://www.sciencedirect.com/science/article/pii/S0097849322001868),
[Heinrich, *Parallel-Coordinates Art*](https://www.joules.de/files/heinrich_parallel-coordinates_2013.pdf).

---

## 2. What the five reference images demand

Each uploaded image pins down a concrete capability set. Together they define
the scope — the element is "done" when all five can be reproduced from the
public API.

### Image 1 — Iris PCP, common scale, categorical colouring
"Parallel Coordinate Plot for the Iris Data": four axes (*Petal Length*,
*Petal Width*, *Sepal Length*, *Sepal Width*) laid out along a bottom
`variable` axis, with a **single shared vertical `value` axis** running roughly
−2…3 — i.e. the columns are **z-score standardised onto one common scale**
rather than each axis being normalised to its own min/max. Lines are coloured by
a **categorical grouping variable** (`Species`: setosa / versicolor / virginica,
viridis palette), with a **discrete legend on the right**, **point markers at
every axis crossing**, and a grey panel with light horizontal gridlines.

> Requires: common-scale + standardisation mode, one shared value axis with
> gridlines, categorical hue mapping, discrete legend, vertex markers,
> dimension-name labels along the bottom, panel/theme styling.

### Image 2 — Dense sweep, per-axis ranges, value-based gradient, ghosted context
Five axes, each drawn as a vertical rule with tick marks and its **own numeric
range printed in boxes at the top and bottom of the axis** (per-axis independent
normalisation). Thousands of thin, **alpha-blended** lines coloured on a
continuous blue→purple→red ramp keyed to one dimension, drawn over a large mass
of **faint grey "context" lines** (records excluded by the current filter). One
axis header is highlighted, marking the active/selected dimension.

> Requires: per-axis min/max normalisation with endpoint labels, continuous
> colormap driven by a chosen dimension, auto-alpha for large N, a two-layer
> context/foreground draw (filtered-out records dimmed, not deleted), axis tick
> marks, active-axis highlighting.

### Image 3 — Curved lines, value-based colouring, hyper-parameter sweep
Title "Parallel Coordinate Chart with Value-based Coloring"; four axes titled
below the plot (`batch_size`, `channels_one`, `learning_rate`, `accuracy`) with
per-axis tick labels. Lines are **smooth curves** (spline interpolation with
visible overshoot between axes), one colour per record from a wide qualitative
palette, over a white background with faint vertical bands.

> Requires: curve interpolation mode (Catmull-Rom / monotone cubic / Bezier)
> with a smoothness factor, axis titles below the plot, per-axis tick labels,
> per-record colour, alternating axis-gap background bands.

### Image 4 — Distribution histograms on the axes, highlighted entities
World-Bank / Google-Public-Data style: each axis carries its **own histogram of
the column's distribution drawn behind it** in grey, plus **min and max value
labels at the top and bottom**. Each axis has a **header chip** naming the
indicator ("Population ages 0-14 (% of total)", "Life expectancy at birth,
total", "Fertility rate, total (births per…)"). Most records are drawn in faint
pastel; three **pinned entities** (Sweden, Germany, Niger) are drawn bold in
saturated colours and listed in a **small legend box at the top left**.

> Requires: per-axis histogram/density decoration, axis endpoint value labels,
> axis header chips (the drag/reorder/remove handle), a highlight-subset model
> (pin records → bold on top, everything else de-emphasised), highlighted-entity
> legend.

### Image 5 — Curved bundles, group colouring, on-axis range brushes
Axis header labels in boxes across the top (some highlighted, i.e. active),
**translucent grey slabs spanning parts of the plot** — the brushed/filtered
range bands — small handle arrows on the axes, tick labels per axis, and two
strongly separated colour groups (orange vs green) drawn as heavy smooth
**bundles** of curves.

> Requires: interactive on-axis range brushes with visible bands and drag
> handles, multi-axis brush combination, group colouring, curve rendering,
> draggable axis header chips, per-gap background shading.

---

## 3. How this fits the existing UltraCanvas code

The framework already carries most of the supporting machinery. The element
should **reuse, not duplicate**:

| Existing piece | Reuse for |
|---|---|
| `UltraCanvasChartElementBase` (`include/Plugins/Charts/`) | Plot area, margins, background/grid/axes styling, tooltip integration (`ShowChartPointTooltip`, `HideTooltip`), zoom/pan/selection flags, animation clock, `FormatAxisLabel`, empty state |
| `UltraCanvasRadarChartElement` | **The closest precedent**: a chart with *its own* axis + series data model (`RadarChartAxis`, `RadarChartSeries`) instead of `IChartDataSource`, its own legend layout pass with a reserved strip, and a `UltraCanvasTimer`-driven animation. A parallel coordinate chart is the same model with linear axis geometry |
| `UltraCanvasJitterPlotElement` | `AxisScale` (linear/log) enum, hue variable + `std::map<std::string, Color>` hue map, jitter distributions (needed to separate coincident records on categorical axes), category label positioning |
| `UltraCanvasColormap.h` | All built-in ramps plus `SampleColormap`, `InterpolateColormap`, `QuantizeNorm`, `DivergingNorm`, `IsDivergingColormap` — the continuous value-colouring of images 2 and 3 needs no new colour code |
| `UltraCanvasHeatmapChartElement::RenderColorBar` / the contour legend modes (`ContourLegendMode`) | The continuous colour bar and the discrete swatch legend |
| `UltraCanvasLabelPlacement.h` (`PlaceShapeLabels`, `LabelPlacementOptions`) | Collision-free axis titles, endpoint labels and legend entries when axes are close together |
| `UltraCanvasTimeAxis.h` | Time-valued dimensions and their tick formatting |
| `IRenderContext` | Everything the renderer needs already exists: `DrawLinePath` / `FillLinePath` (polylines and brush bands), `DrawBezierCurve` (curved mode), `SetLineDash`, `ClipPath`, `Rotate` / `SetTransform` (rotated axis titles), alpha |
| `UCPixmap` (`UltraCanvasImage.h`) | Off-screen cache of the static context layer, so brushing only redraws the thin selected layer — the same trick the heatmap uses for its raster mode |
| `UltraCanvasContourGrid.h` / `UltraCanvasMarchingSquares.h` | The **precedent for UI-free, unit-testable algorithm headers** paired with a thin element — the model here follows it |

Nothing new needs vendoring: no third-party dependency, no GL requirement.

---

## 4. Proposed architecture

Three new units, following the Contour/Hexbin precedent of a pure model header
that is testable without a window plus a thin UI element:

```
include/Plugins/Charts/UltraCanvasParallelAxisModel.h    # dimensions, scales, normalisation, brushes, ordering metrics (no UI deps)
include/Plugins/Charts/UltraCanvasParallelCoordinateChart.h  # the element (ChartElementBase)
Plugins/Charts/UltraCanvasParallelAxisModel.cpp
Plugins/Charts/UltraCanvasParallelCoordinateChart.cpp
```

### 4.1 Data model

```cpp
enum class PCPDimensionKind { Numeric, Categorical, Ordinal, Time };
enum class PCPScale { Linear, Log, SymLog, Percentile, ZScore, RobustZScore };

struct PCPDimension {
    std::string name;                       // axis title / header chip text
    PCPDimensionKind kind  = PCPDimensionKind::Numeric;
    PCPScale scale         = PCPScale::Linear;
    double minValue = 0.0, maxValue = 0.0;  // auto-computed unless SetAxisRange
    bool autoRange = true;
    bool inverted  = false;                 // flip high/low
    bool visible   = true;
    std::vector<std::string> categories;    // Categorical/Ordinal slot labels
    std::vector<double> tickValues;         // explicit ticks (optional)
    std::function<std::string(double)> formatter;  // "$27.5K", "10.0M", "%"
};

struct PCPRecord {
    std::vector<double> values;   // one per dimension; NaN = missing
    std::string label;            // tooltip / highlight legend text
    std::string group;            // categorical hue key
    double weight = 1.0;
    Color colorOverride = Color(0,0,0,0);   // alpha 0 = "use the colour rules"
    uint64_t id = 0;              // stable id for linked selection
};
```

Records are stored row-major and normalised once into a cached
`std::vector<float>` in **[0,1] axis space**; screen geometry is then a pure
function of the plot rect and the axis order, so resizing and reordering never
re-touch the source data.

### 4.2 Rendering pipeline

```
normalise (cached) → order/lay out axes → classify records
   → layer 0: context   (filtered-out / non-highlighted, dimmed, pixmap-cached)
   → layer 1: normal    (passing records, alpha-blended)
   → layer 2: emphasis  (hovered / selected / pinned, drawn last, full opacity)
   → axis decorations (histograms, boxes) → axes, ticks, labels, headers
   → brush bands + handles → legend / colour bar → title
```

Splitting the context layer out and caching it in a `UCPixmap` is what makes
interactive brushing on 100k records feel instant: dragging a brush only
re-rasterises layers 1–2.

### 4.3 Brush model (UI-free, testable)

```cpp
struct PCPBrush { size_t dimensionIndex; double lo, hi; bool inverted; };
struct PCPCategoryBrush { size_t dimensionIndex; std::vector<int> categoryIndices; };
```

A record passes when it satisfies **every** brushed axis (AND across axes) and
**any** brush on a given axis (OR within an axis) — the standard semantics used
by Spotfire, Plotly's `constraintrange` and d3-parcoords. The predicate lives in
the model header and is unit-testable without a render context.

---

## 5. Proposed feature list

Grouped, with a suggested delivery phase. **P1** = core, ship first;
**P2** = completes the reference images; **P3** = polish / advanced.

### 5.1 Data & dimensions
| # | Feature | Phase |
|---|---|---|
| D1 | Record-oriented input: `SetRecords(std::vector<PCPRecord>)` | P1 |
| D2 | Column-oriented input: `AddDimension(name, std::vector<double>)`, one call per column | P1 |
| D3 | `IChartDataSource` / CSV adapter consistent with the other chart elements | P2 |
| D4 | Categorical & ordinal dimensions (string values → evenly spaced slots with category tick labels) | P2 |
| D5 | Time dimensions (reuse `UltraCanvasTimeAxis` for ticks and formatting) | P2 |
| D6 | Missing values (`NaN`): policy per chart — skip the vertex and bridge, break the line, or route through a "missing" slot below the axis | P1 |
| D7 | Per-record weight → line width and/or alpha (ensemble/importance weighting) | P2 |
| D8 | Grouping (hue) variable for categorical colouring; separate value variable for continuous colouring | P1 |
| D9 | Stable record ids + external selection sync, for linked views with a table or another chart | P2 |
| D10 | Streaming / incremental append with a bounded ring buffer (live sensor data) | P3 |
| D11 | Dimension visibility subset + reordering API independent of insertion order | P1 |
| D12 | Degenerate-dimension handling: a column with a single distinct value gets a padded range and a note rather than a divide-by-zero or a silently dropped axis | P1 |
| D13 | Derived dimensions: `AddComputedDimension(name, fn(record))` (ratios, residuals, rank) | P3 |

### 5.2 Scales & normalisation
| # | Feature | Phase |
|---|---|---|
| N1 | Per-axis independent min/max normalisation (images 2, 4, 5) — the default | P1 |
| N2 | Common scale: all axes share one value range and one visible value axis (image 1) | P1 |
| N3 | Standardisation: z-score, and robust median/MAD, evaluated per column before a common-scale layout (image 1) | P1 |
| N4 | Explicit per-axis range `SetAxisRange(dim, lo, hi)` with "nice number" rounding for auto ranges | P1 |
| N5 | Logarithmic and symlog axis scale (per axis) | P2 |
| N6 | Percentile / rank scale — uniformises skewed columns (HiPlot's "percentile" option) | P2 |
| N7 | Per-axis inversion (flip high/low), API + double-click on the header | P1 |
| N8 | Out-of-range policy: clamp to the axis end, or exclude the record | P2 |
| N9 | Per-axis display formatter — decimals, prefix/suffix, `K`/`M`/`%`/currency, custom callback | P1 |
| N10 | Runtime scale switch per axis (linear ↔ log ↔ percentile) with animated re-layout | P3 |

### 5.3 Line rendering
| # | Feature | Phase |
|---|---|---|
| R1 | Straight polylines through the axis crossings — the default and the honest one | P1 |
| R2 | Curved lines: Catmull-Rom, monotone cubic, and cubic Bezier, with a `SetCurveTension` smoothness factor (images 3, 5) | P1 |
| R3 | Vertex markers at each axis crossing — shape, size, per-group colour (image 1) | P1 |
| R4 | Alpha blending with an automatic default alpha derived from the record count (image 2) | P1 |
| R5 | Line width: global, per group, per record, and by weight | P1 |
| R6 | Two-layer draw: filtered-out/non-highlighted records as a dimmed "context/ghost" layer beneath the active ones (images 2, 4, 5); option to hide them entirely instead | P1 |
| R7 | Draw-order policy: as given, sorted by a dimension, selected-last, or shuffled — a fixed order silently biases which group appears "on top" | P2 |
| R8 | Density / binned rendering for very large N: aggregate segments into per-gap bins and draw shaded ribbons instead of individual lines | P3 |
| R9 | Progressive rendering with a frame budget, so a 500k-record plot stays interactive while it fills in | P3 |
| R10 | Curve bundling toward cluster centroids or axis mid-points (edge-bundling / confluent drawing) to cut clutter | P3 |
| R11 | Slope-dependent line-thickness compensation, to remove the density distortion and "ghost cluster" artefacts steep segments cause | P3 |
| R12 | Group envelope / band mode: per group draw min–max or IQR ribbons plus a median line, instead of every record (ensembles) | P2 |
| R13 | Jitter on categorical axes so coincident records separate (reuse `JitterDistribution`) | P2 |
| R14 | Animated transitions on axis reorder, inversion and scale change | P2 |

### 5.4 Colour & style
| # | Feature | Phase |
|---|---|---|
| S1 | Categorical colouring by group: built-in qualitative palettes + explicit `{group → Color}` map (image 1) | P1 |
| S2 | Continuous colouring by a chosen dimension through `UltraCanvasColormap` (images 2, 3), with diverging-midpoint support | P1 |
| S3 | Per-record explicit colour override | P1 |
| S4 | Hover / selection / pin styling: highlight colour, width multiplier, halo | P1 |
| S5 | Dim styling for filtered-out records: colour, alpha, optional desaturation to grey (images 2, 4) | P1 |
| S6 | Fully theme-neutral palette — background, plot area, axis line, tick, label, header chip, grid, brush band colours all settable (dark theme) | P1 |
| S7 | Per-group dash pattern (accessibility, monochrome print) | P2 |
| S8 | Gradient along a line when colouring by value — colour interpolated between axis crossings | P3 |
| S9 | Per-segment colouring by local slope (up/down/flat) as an alternative colour mode | P3 |

### 5.5 Axes, ticks & labels
| # | Feature | Phase |
|---|---|---|
| A1 | Vertical axes evenly spaced across the plot; **horizontal orientation** option (axes as rows) for long dimension names | P1/P2 |
| A2 | Axis titles above **or** below the plot (images 3, 4, 5), with rotation, ellipsising and wrapping | P1 |
| A3 | Axis **header chips** — a boxed, hit-testable label used as the drag/reorder/menu handle (images 4, 5), with an "active axis" highlight state | P2 |
| A4 | Tick marks and tick labels per axis, with tick-count control and "nice" tick selection | P1 |
| A5 | Min/max endpoint value labels at the top and bottom of each axis (images 2, 4) | P1 |
| A6 | Category tick labels on categorical axes | P2 |
| A7 | Shared value axis on the left with horizontal gridlines, for common-scale mode (image 1) | P1 |
| A8 | Chart title plus optional axis-group titles ("variable" / "value", image 1) | P1 |
| A9 | Axis spacing modes: equal, weighted, explicit, and interactive drag-to-space | P3 |
| A10 | Per-gap background shading: alternating stripes and explicit slab bands between chosen axes (images 3, 5) | P2 |
| A11 | Axis line styling (colour, width, caps) and collision-aware label placement via `UltraCanvasLabelPlacement` | P1 |
| A12 | Overflow handling when axes outnumber the available width: horizontal scroll/paging with sticky headers, or auto-shrink | P3 |

### 5.6 Axis decorations & distribution summaries
| # | Feature | Phase |
|---|---|---|
| H1 | Histogram of each column drawn behind / beside its axis, with bin-count control and side selection (left / right / both / centred) (image 4) | P2 |
| H2 | KDE density strip / violin along the axis as an alternative to the histogram | P3 |
| H3 | Box-and-whisker summary per axis, optionally split per group | P2 |
| H4 | Mean / median markers per axis, per group | P2 |
| H5 | Brushed histogram: the filtered subset overlaid on the full distribution, so the filter's effect on each column is visible | P2 |
| H6 | Hover a histogram bin → tooltip with count and share; click a bin → brush that range | P3 |

### 5.7 Interaction
| # | Feature | Phase |
|---|---|---|
| I1 | Hover a line: emphasise it and show a tooltip listing every dimension value of that record (with a custom generator hook) | P1 |
| I2 | Robust nearest-line hit testing — point-to-segment distance with a pixel tolerance, backed by a per-gap spatial index | P1 |
| I3 | Click to select / pin records; multi-select with Ctrl; pinned records draw bold on top and appear in the highlight legend (image 4) | P1 |
| I4 | **1-D axis range brush**: drag on an axis to create a range filter, with handles to resize and move, and drag-off to clear (images 4, 5) | P1 |
| I5 | Multiple brushes per axis (OR within an axis, AND across axes) | P2 |
| I6 | Brush inversion / exclusion, plus "invert selection" and "clear all brushes" | P2 |
| I7 | Categorical brush: click category slots on a categorical axis to include/exclude them | P2 |
| I8 | **Axis reordering** by dragging the header chip, with a drop indicator and animated settle | P1 |
| I9 | Axis hide/remove from the header chip menu, and a dimension-picker panel to bring axes back | P2 |
| I10 | Axis inversion by double-click on the axis or header | P1 |
| I11 | Per-axis zoom: wheel or endpoint drag rescales that axis to a sub-range (independent of brushing) | P2 |
| I12 | **2-D strum brush** — drag a stroke between two axes to select the lines crossing it | P3 |
| I13 | **Angular brush** — select by the slope of a segment between two axes | P3 |
| I14 | Callbacks: `OnRecordHover`, `OnRecordClick`, `OnSelectionChanged`, `OnBrushChanged`, `OnAxisOrderChanged`, `OnAxisRangeChanged` | P1 |
| I15 | Selection API: get/set selected ids, get the filtered record list, export the current filter as a predicate | P1 |
| I16 | Keyboard access: tab between axes, arrows to move/resize the focused brush, Esc to clear, Home to reset | P2 |
| I17 | Reset control: restore original axis order, ranges and brushes | P1 |
| I18 | Context menu on an axis: invert, hide, sort records by, set scale, clear brush | P2 |

### 5.8 Legend
| # | Feature | Phase |
|---|---|---|
| G1 | Discrete legend for the grouping variable — swatch + label, positionable left/right/top/bottom (image 1), reusing the radar chart's reserved-strip layout approach | P1 |
| G2 | Legend interaction: click an entry to toggle that group's visibility, hover to emphasise it | P2 |
| G3 | Continuous colour bar when colouring by value, with ticks and a title (images 2, 3) — reuse the heatmap colour bar | P1 |
| G4 | Highlight legend: a compact box listing only the pinned records with their colours (image 4) | P2 |
| G5 | Legend title, ordering, multi-column layout, entry cap with "+N more" | P2 |

### 5.9 Analytics & assistance
| # | Feature | Phase |
|---|---|---|
| C1 | Pearson / Spearman correlation between neighbouring axes, optionally printed in each axis gap | P2 |
| C2 | Automatic axis ordering heuristics: maximise the absolute correlation of neighbours, minimise crossings, or similarity/PCA ordering — with a one-call `AutoOrderAxes(metric)` | P2 |
| C3 | Record clustering (k-means on the normalised rows) with cluster colouring and per-cluster envelopes | P3 |
| C4 | Outlier flagging and emphasis (per-axis z-score or Mahalanobis distance) | P3 |
| C5 | Aggregation mode: per group draw the median line plus an IQR band instead of every record | P2 |
| C6 | Summary read-out: count of records passing the current filter, per group, live while brushing | P1 |

### 5.10 Performance & engineering
| # | Feature | Phase |
|---|---|---|
| E1 | UI-free `UltraCanvasParallelAxisModel.h` (normalisation, scales, brush predicate, ordering metrics, hit-test maths) with unit tests under `Tests/` | P1 |
| E2 | Normalised-geometry cache with a generation counter; resize and reorder never re-normalise | P1 |
| E3 | Per-gap spatial index for O(log n) hover hit-testing at 100k+ records | P2 |
| E4 | `UCPixmap` cache of the static context layer so brush drags redraw only the active layers | P2 |
| E5 | Factory helper `CreateParallelCoordinateChartElement(...)`, PascalCase API, `namespace UltraCanvas`, no new third-party dependency | P1 |
| E6 | `Docs/UltraCanvas/UltraCanvasParallelCoordinateChart.md` in the house style, plus `Apps/DemoApp/UltraCanvasParallelCoordinateChartExamples.cpp` reproducing all five reference images | P1 |
| E7 | CMake registration in `UltraCanvas/CMakeLists.txt` alongside the other chart plugins | P1 |
| E8 | Guard rails: sensible caps and clear behaviour for 0 dimensions, 1 dimension, 0 records, all-NaN columns | P1 |

---

## 6. Proposed API sketch

### 6.1 Image 1 — Iris, common scale, coloured by species

```cpp
#include "Plugins/Charts/UltraCanvasParallelCoordinateChart.h"

auto pcp = UltraCanvas::CreateParallelCoordinateChartElement("iris", 20, 20, 720, 420);

pcp->AddDimension("Petal Length", petalLength);
pcp->AddDimension("Petal Width",  petalWidth);
pcp->AddDimension("Sepal Length", sepalLength);
pcp->AddDimension("Sepal Width",  sepalWidth);
pcp->SetRecordGroups(species);                 // "setosa" / "versicolor" / "virginica"

pcp->SetNormalizationMode(UltraCanvas::PCPNormalization::CommonScale);
pcp->SetScale(UltraCanvas::PCPScale::ZScore);  // standardise every column
pcp->SetShowSharedValueAxis(true);             // the "value" axis on the left
pcp->SetShowVertexMarkers(true, 3.0f);

pcp->SetColorMode(UltraCanvas::PCPColorMode::ByGroup);
pcp->SetGroupColors({{"setosa",     UltraCanvas::Color(68, 1, 84)},
                     {"versicolor", UltraCanvas::Color(33, 145, 140)},
                     {"virginica",  UltraCanvas::Color(253, 231, 37)}});
pcp->SetLegendMode(UltraCanvas::PCPLegendMode::DiscreteGroups);
pcp->SetLegendTitle("Species");
pcp->SetChartTitle("Parallel Coordinate Plot for the Iris Data");
pcp->SetAxisGroupTitles("variable", "value");

container->AddChild(pcp);
```

### 6.2 Image 2 — Dense sweep, per-axis ranges, value gradient, ghost context

```cpp
pcp->SetRecords(runs);                                     // vector<PCPRecord>
pcp->SetNormalizationMode(UltraCanvas::PCPNormalization::PerAxis);
pcp->SetShowAxisEndpointLabels(true);
pcp->SetColorMode(UltraCanvas::PCPColorMode::ByValue);
pcp->SetColorDimension("score");
pcp->SetColormap(UltraCanvas::HeatmapColormap::Turbo);
pcp->SetLineAlpha(0.0f);                                   // 0 = auto from record count
pcp->SetContextLayer(true, UltraCanvas::Color(190, 190, 190, 60));   // ghosted, filtered-out
pcp->SetLegendMode(UltraCanvas::PCPLegendMode::ColorBar);
```

### 6.3 Image 3 — Curved, per-record colour, titles below

```cpp
pcp->SetLineMode(UltraCanvas::PCPLineMode::Curved);
pcp->SetCurveType(UltraCanvas::PCPCurveType::CatmullRom);
pcp->SetCurveTension(0.6f);
pcp->SetAxisTitlePosition(UltraCanvas::PCPAxisTitlePosition::Below);
pcp->SetShowAxisTicks(true, 6);
pcp->SetAxisGapBands(true);                                // faint alternating bands
pcp->SetChartTitle("Parallel Coordinate Chart with Value-based Coloring");
```

### 6.4 Image 4 — Histograms on the axes, pinned entities

```cpp
pcp->SetShowAxisHistograms(true);
pcp->SetHistogramBins(24);
pcp->SetHistogramSide(UltraCanvas::PCPHistogramSide::Both);
pcp->SetHistogramColor(UltraCanvas::Color(160, 160, 160, 110));
pcp->SetShowAxisHeaderChips(true);                         // draggable / closable
pcp->SetAxisFormatter("Life expectancy at birth, total",
                      [](double v){ return UltraCanvas::FormatNumber(v, 0); });

pcp->SetDimmedStyle(UltraCanvas::Color(200, 205, 215, 70), 1.0f);
pcp->PinRecords({"Sweden", "Germany", "Niger"});           // bold, drawn last
pcp->SetLegendMode(UltraCanvas::PCPLegendMode::PinnedRecords);
pcp->SetLegendPositionPreset(UltraCanvas::PCPLegendPosition::TopLeft);
```

### 6.5 Image 5 — Brushes, groups, curved bundles

```cpp
pcp->SetLineMode(UltraCanvas::PCPLineMode::Curved);
pcp->SetEnableBrushing(true);
pcp->SetBrushBandColor(UltraCanvas::Color(120, 120, 130, 45));
pcp->SetEnableAxisReordering(true);

pcp->AddBrush("learning_rate", 0.001, 0.01);               // programmatic brush
pcp->AddBrush("accuracy",      0.85,  1.00);

pcp->SetOnBrushChanged([](const std::vector<size_t>& passing) {
    UpdateStatusBar("matching runs: " + std::to_string(passing.size()));
});
pcp->SetOnAxisOrderChanged([](const std::vector<std::string>& order) { /* persist */ });
```

---

## 7. Suggested delivery order

1. **Phase 1 — core chart.** `UltraCanvasParallelAxisModel` (dimensions,
   per-axis and common-scale normalisation, z-score, inversion, NaN policy,
   brush predicate) with unit tests; the element with straight **and** curved
   lines, vertex markers, alpha blending, the context/normal/emphasis layering,
   group and value colouring, axis titles/ticks/endpoint labels, the shared value
   axis, discrete legend and colour bar, hover tooltips, click-to-pin, 1-D axis
   brushing, drag-to-reorder, double-click inversion, callbacks, docs and a demo.
   This reproduces images 1, 2, 3 and the core of 5.
2. **Phase 2 — completeness.** Axis histograms and box summaries (image 4),
   header chips with menus, categorical/time dimensions, log and percentile
   scales, multi-brush and category brushes, per-axis zoom, correlation read-out
   and auto-ordering, group envelopes, legend interaction, spatial index and the
   context-layer pixmap cache, keyboard access.
3. **Phase 3 — advanced.** Density/binned rendering and progressive draw for
   very large N, bundling and slope-compensated rendering, strum and angular
   brushes, clustering and outlier emphasis, derived dimensions, streaming input,
   axis overflow paging, horizontal orientation.

---

## 8. Open questions for review

1. **Base class and data model.** `IChartDataSource` is a 2-D `(x, y)` point
   model and does not fit *n*-dimensional records. Recommendation: derive
   `UltraCanvasParallelCoordinateChartElement` from `UltraCanvasChartElementBase`
   and carry its **own** dimension/record model, exactly as
   `UltraCanvasRadarChartElement` does — with a thin `IChartDataSource` adapter
   added later (D3) for consistency with the Cartesian charts.
2. **Shared legend component.** This is the third element (after radar and
   contour) to grow its own legend. Should a reusable chart legend widget —
   discrete swatches, colour bar, positioning, click-to-toggle — be factored out
   now and adopted by radar, heatmap, contour, hexbin, Mekko and this chart?
   Recommendation: yes, and do it as part of Phase 1 of this work.
3. **Shared selection/brush model for linked views.** Brushing is most valuable
   when a selection made here also filters a table or a scatter plot.
   Recommendation: put the selection in a small shared
   `ChartSelectionModel` (`std::shared_ptr`, change callback) rather than
   private state, so linked-view support costs nothing later.
4. **Default line mode.** Curves (images 3, 5) look better but *misrepresent
   values between axes* and can overshoot outside an axis range.
   Recommendation: **straight by default**, curves opt-in, and clamp curve
   control points so a spline never leaves the axis range.
5. **Very-large-N strategy.** Is the P3 binned/density path needed, or is the
   pixmap-cached context layer plus alpha blending sufficient for the datasets
   UltraCanvas apps actually load? This decides whether the model header needs a
   binning structure designed in from the start.
6. **Radar chart convergence.** A radar chart is this chart on radial axes. Is
   it worth sharing the axis/normalisation model between the two (one model,
   two geometries), or is the duplication cheaper than the coupling?

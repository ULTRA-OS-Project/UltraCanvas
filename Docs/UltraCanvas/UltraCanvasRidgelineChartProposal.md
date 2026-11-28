# UltraCanvasRidgelineChart — Research & Feature Proposal

**Status:** **Phase 1 is implemented** (2026-11-16) as `UltraCanvasRidgelineChart`
and `UltraCanvasRidgelineModel` - see [`UltraCanvasRidgelineChart.md`](UltraCanvasRidgelineChart.md)
for the guide. It delivered R1, R2, R4-R6, D1-D4 and the statistics in the
model, A1-A7, N2 and I1 in the chart, the `WordRatings` and
`DailyActivities` samples, the demo page (**Charts → Ridgeline Chart**),
`Tests/RidgelineModelTest.cpp` and `Tests/RidgelineChartRenderTest.cpp`.
Phase 2 (2026-11-16) delivered R3 (several series per row, with a
legend), R7 (the wrapping axis), D5 and D6 (tail cut, binned curves), N1 and
N3 (marks, quantile bands), I2 and I3 (fading on hover, `onRowClick`), X1
and X2 (CSV and JSON, with the demo's Open / Save JSON and Import / Export
CSV through `UltraCanvasFileLoader`), and the `Sleep` sample with the demo's
third variant. Phase 3 and the Follow-up are still to come.

How Phase 1 and Phase 2 depart from the plan below, and why:

* **A row holds `values` and `curve` itself, not `entries` per series, and
  the model has no `series` yet.** Several series per row are Phase 2 (R3);
  a list nothing can fill with more than one item would only be a promise
  in the API. Phase 2 adds the second series beside them.
* **No `period` and no `tailCut` yet** (R7 and D5 are Phase 2). The
  time-of-day sample covers 03:00 to 03:00 as hours 3 to 27, with a
  formatter that reads them as clock times, which needs no wrap.
* **The rows are not a visible category axis** (§4.2). The value column has
  to sit between the names and the plot, and margins a chart reserves do
  not add to an axis band, so the names, the value column and its header
  are the chart's own furniture beside a hidden 0..1 row axis, as the
  ternary chart draws its own axes. The chart places each baseline itself,
  so the tallest curve ends exactly at the top of the plot.
* **`RidgelineValue::Hidden`, not `None`:** X11's `Xlib.h` defines `None`.
* **`onRowHover` is in Phase 1** (I3 planned it for Phase 2), and it fires
  as the pointer moves along a row as well as between rows: the demo's
  read-out needs the x. `onRowClick` and the fading of other rows (I2)
  are still Phase 2.
* **The word sample uses a fixed bandwidth of 0.6.** §7.3 below is
  corrected: Scott's rule does not give whole-number ratings a bandwidth
  near 1.
* **Hover works in strips:** each row's area is cut into 64 vertical strips,
  so the tooltip can give the x under the pointer and the curve's height
  there.
* **Series (Phase 2):** a row keeps its first series in `values` / `curve`
  and the others in `more` (`SetValues(s, ...)`, `ValuesOf(s)`), so a
  one-series row is written as before; the model's `entries` sketch was
  dropped. With two or more series every curve takes its series' colour and
  the row colourings apply to one series only: rows and series cannot both
  be told apart by colour. The legend sits right of the plot, because the
  title and subtitle hold the top and the x axis the bottom.
* **The wrap (Phase 2)** applies to supplied curves as well as values, and a
  wrapped density is circular (each value also one period either side), so
  it meets itself at the ends instead of being cut. Statistics are taken on
  the wrapped values: a median bedtime is 00:40, not noon.
* **Binned curves (Phase 2)** have area 1, like a density, so the scales
  treat both alike; a bin's peak is its middle.
* **CSV (Phase 2)** lines may mix values and curve points, and numbers are
  written with 17 significant digits, so a file reads back exactly.
* **The value column (Phase 2)** writes mean, median and peak through the
  x formatter when there is one: a bedtime reads 23:15, not 23.25.
* **A click (Phase 2)** is a press and release on the same row, so a drag
  across rows does not click one.

A ridgeline chart (also called a joyplot) stacks one distribution per row.
Every row has its own baseline. Its curve is a smooth, filled outline, and
it may rise over the rows above it. Read down the rows, the eye compares
shapes: where each row peaks, how wide it is, whether it has one hump or
two. Typical uses:

* how people rate words such as *Abysmal*, *OK* and *Perfect* on a 0-10
  scale, one word per row, sorted by the average score;
* when in the day people do each sport or hobby, each row scaled to its own
  peak;
* monthly temperatures, response times per server, scores per class;
* two years in each row (2019 against 2020), drawn over each other.

UltraCanvas has no chart that does this. This proposal adds one on the
chart engine, with a UI-free model that turns raw values into curves.

**Author:** UltraCanvas Framework

---

## Contents

- [1. Summary — the headline question](#1-summary--the-headline-question)
- [2. What a ridgeline chart is](#2-what-a-ridgeline-chart-is)
- [3. Reference shapes](#3-reference-shapes)
- [4. Where this belongs in the repository](#4-where-this-belongs-in-the-repository)
  - [4.1 A chart of its own, not a line chart mode](#41-a-chart-of-its-own-not-a-line-chart-mode)
  - [4.2 Rows on a category axis](#42-rows-on-a-category-axis)
  - [4.3 Registration checklist (what "done" touches)](#43-registration-checklist-what-done-touches)
- [5. Data model sketch](#5-data-model-sketch)
  - [5.1 The model](#51-the-model)
  - [5.2 The chart](#52-the-chart)
- [6. Feature list](#6-feature-list)
  - [Rows and curves (R)](#rows-and-curves-r)
  - [Density (D)](#density-d)
  - [Appearance (A)](#appearance-a)
  - [Annotations (N)](#annotations-n)
  - [Interaction (I)](#interaction-i)
  - [Import / export (X)](#import--export-x)
  - [Oblique view (O)](#oblique-view-o)
- [7. The hard parts](#7-the-hard-parts)
  - [7.1 Which curve is under the pointer](#71-which-curve-is-under-the-pointer)
  - [7.2 Room above the top row](#72-room-above-the-top-row)
  - [7.3 Bandwidth, bounds and whole-number data](#73-bandwidth-bounds-and-whole-number-data)
  - [7.4 Long, thin tails](#74-long-thin-tails)
  - [7.5 Scaling the rows against each other](#75-scaling-the-rows-against-each-other)
  - [7.6 Speed](#76-speed)
- [8. What already exists to build on](#8-what-already-exists-to-build-on)
- [9. Open questions (with recommendations)](#9-open-questions-with-recommendations)
- [10. Suggested phasing](#10-suggested-phasing)

## 1. Summary — the headline question

**Can any chart we have draw a ridgeline chart?** No. The closest are:

| Candidate | What it already does | What a ridgeline needs that it lacks |
|---|---|---|
| `UltraCanvasEngineLineChart`, `Area` mode with the `Curved` line shape | Smooth (monotone cubic) filled curves, fill opacity and gradient, per-series colours, tooltips, a legend | Every series sits on the same zero baseline, so the curves pile up instead of each having a row |
| `UltraCanvasEngineLineChart` panes (`SetPanes`) | One band per group, sharing the domain axis | Each pane is a closed box with its own value axis and a gap; a curve cannot rise into the row above |
| `UltraCanvasJitterPlotElement`, violin and raincloud modes | A Gaussian kernel density of one group's values | The density is private to the element (`CalculateKernelDensity`), symmetric (a violin), and one group at a time on the legacy chart base |
| `UltraCanvasHistogramChart` | Counts per level, filled outlines on the engine | One distribution, not a stack |

Recommendation: **a new chart type, `UltraCanvasRidgelineChart`, derived from
`UltraCanvasChartEngineElement`, with a UI-free `UltraCanvasRidgelineModel`.**
Nothing is needed from the engine beyond what it has. Rows are the slots of
a category axis. Each row's curve is an area outline, built with
`BuildAreaOutlines` in the `Curved` shape, between the row's baseline and the
baseline raised by the curve. The model:

* turns raw values into a curve by a 1D kernel density estimate, with the
  bandwidth chosen by rule (Scott, Silverman) or set, and the edges of a
  bounded scale (0 to 10) handled by reflection;
* also takes curves directly, as (x, y) points, when the program already has
  them (participants per minute of the day);
* scales the rows to their own peak or to one shared scale, sorts them, and
  gives each row's count, mean, median and quantiles.

Three phases (§10). Phase 1 draws the word-rating chart and the
time-of-day chart from the reference pictures.

---

## 2. What a ridgeline chart is

* **Rows.** One per category, top to bottom. A row has a name (drawn on the
  left), a baseline and a curve. The rows share one horizontal axis.
* **The curve.** A density of the row's values, or any curve the program
  supplies. It is filled down to the row's baseline and outlined along its
  top.
* **Overlap.** A curve's peak may stand taller than the gap between two
  baselines, so it rises in front of the rows above. Rows are drawn from the
  top down, so each row's fill covers the outline of the row behind it. The
  overlap is the chart's main dial: at 1 the tallest peak just reaches the
  next baseline up; the reference pictures use about 1.5 to 3.
* **Scale.** Either every row is scaled to its own peak, so only the shape
  is compared ("compared to peak popularity"), or all rows share one scale,
  so a row with more mass stands taller.
* **Extras.** A column of numbers beside the names ("Average score"), a
  colour per row (often from a colour scale across the rows, red to green),
  vertical grid lines, and marks for each row's mean, median or quantiles.

What it is **not**: a stacked area chart (the rows do not add up), nor a
small-multiples grid (the rows share one axis and overlap on purpose).

---

## 3. Reference shapes

The four reference pictures the request came with, and what each needs:

| # | Picture | Needs |
|---|---|---|
| 1 | *Peak time of day for sports and leisure*: 27 activities over a 24-hour day from 03:00 to 03:00, each scaled to its own peak, two alternating blues with a white outline, rows sorted by when they peak, side notes ("evening peak", "morning peak") | Curves given directly (R2); per-row-peak scale (R5); a time axis that wraps at a set start (R7); alternating colours (A2); outline colour (A3); sort by peak (R6); side notes (N4) |
| 2 | *Word ratings*, 0-10, about 40 words from *Abysmal* to *Perfect*, density of the answers, red-to-green by score, an "Average score" column | Density from raw values (D1-D3); bounded scale 0-10 (D4); colour from a colour scale by row value (A2); value column (A5); sort by mean (R6) |
| 3 | The same survey in a second year, same layout, lighter fills | As 2; fill opacity (A1) |
| 4 | *A dashboard of three panels*, each an oblique, perspective-like stack with two series per row (2019 and 2020) on a floor grid | Two series per row with a legend (R3); oblique view and floor grid (O1, O2); three charts side by side, which is page layout, not the chart |

The demo recreates 1 and 2 with invented data (§9, Q4) and, in Phase 3, the
look of 4.

---

## 4. Where this belongs in the repository

### 4.1 A chart of its own, not a line chart mode

A `Ridgeline` mode on `UltraCanvasEngineLineChart` was considered. That chart
is about series over one shared value axis; its modes (line, area, stacked
area, 100 %) and its panes all share that idea. A ridgeline row has no value
axis at all: the curve's height is only a shape, and the rows are labelled,
sorted and coloured as categories. A mode would add a branch to almost every
function of the line chart and still need a density model, sorting and a
value column that no other mode uses. A separate chart, like the histogram
and ternary charts, keeps both readable. It shares the drawing helpers
(`BuildAreaOutlines`, `ChartLineShape::Curved`) rather than the class.

### 4.2 Rows on a category axis

The chart describes two axes:

* **axis 0, rows:** a `Category` axis on the left with the row names as
  categories. The engine already lays out category labels, shortens long
  ones and keeps them clear of each other, as the heat map's rows do.
* **axis 1, x:** a numeric axis (or a time axis) along the bottom, with nice
  ticks and vertical grid lines.

Each row's baseline is the bottom of its slot, and its curve is drawn in
normalised coordinates: `v = baseline + overlap × slotHeight × height(x)`,
where `height` is the row's curve scaled to 0..1 (§7.5). Highlights, limiters
on the x axis, hit regions and tooltips all work through the engine's
projection without a ridgeline case.

### 4.3 Registration checklist (what "done" touches)

1. `UltraCanvas/include/Plugins/Charts/Engine/Charts/UltraCanvasRidgelineModel.h`
   (UI-free: rows, kernel density, scaling, sorting, statistics, CSV and
   JSON) and `UltraCanvasRidgelineChart.h`, with their `.cpp` files under
   `UltraCanvas/Plugins/Charts/Engine/Charts/`, added to
   `UltraCanvas/CMakeLists.txt` next to the other engine charts.
2. `Apps/DemoApp/UltraCanvasRidgelineChartExamples.cpp`, declared in
   `UltraCanvasDemo.h`, registered in the **Charts** section as
   `ridgelinechart` with one variant per sample, and added to the demo
   sources in the root `CMakeLists.txt`.
3. `Docs/UltraCanvas/UltraCanvasRidgelineChart.md`, the guide, with a
   generated `## Contents`, and a row in `UltraCanvasUIElements.md` ("how a
   value is spread in each of many groups, as overlapping curves").
4. `Tests/RidgelineModelTest.cpp` (UI-free, as `HistogramModelTest`) and
   `Tests/RidgelineChartRenderTest.cpp`, both in `Tests/CMakeLists.txt`.
5. A `Docs/UltraCanvas/changelog.d/` entry. Run
   `python3 scripts/generate_doc_toc.py`, `check_doc_examples.py` on the
   guide, `check_element_catalogue.py`, `check_ui_reuse.py` and
   `check_callback_cycles.py`. Remove this proposal's entries from
   `scripts/doc_examples_baseline.txt` with
   `check_doc_examples.py --update-baseline` as each API lands.

A chart is a self-rendered view (AGENTS.md), so drawing its rows, curves and
value column by hand is within that exception.

---

## 5. Data model sketch

Everything in this section is a sketch of an API that does not exist yet.

### 5.1 The model

```cpp
#include "Plugins/Charts/Engine/Charts/UltraCanvasRidgelineModel.h"

enum class RidgelineBandwidth { Scott, Silverman, Fixed };
enum class RidgelineScale { RowPeak, Shared, RowArea };
enum class RidgelineSort { AsGiven, ByMean, ByMedian, ByPeak, ByName };

struct RidgelineDensityOptions {
    RidgelineBandwidth rule = RidgelineBandwidth::Scott;
    double bandwidth = 0.0;          // Fixed: in x units
    double adjust = 1.0;             // multiplies the chosen bandwidth
    std::optional<double> lowerBound;   // a bounded scale: reflect at the edge
    std::optional<double> upperBound;
    int resolution = 256;            // points each curve is evaluated at
    double tailCut = 0.0;            // drop tails below this share of the peak
};

struct RidgelineCurvePoint { double x = 0.0; double y = 0.0; };

struct RidgelineEntry {              // one series' data in one row
    std::vector<double> values;      // raw values: a density is computed
    std::vector<RidgelineCurvePoint> curve;   // or the curve itself
};

struct RidgelineRow {
    std::string name;
    std::vector<RidgelineEntry> entries;      // one per series
    std::optional<Color> color;              // unset = the chart's colouring
    std::string note;                        // tooltip text
};

struct RidgelineSeries { std::string name; Color color; };

struct RidgelineRowStatistics {
    size_t count = 0;
    double mean = 0.0, median = 0.0, standardDeviation = 0.0;
    double peakAt = 0.0;             // x of the curve's highest point
    double quartile1 = 0.0, quartile3 = 0.0;
};

struct UltraCanvasRidgelineModel {
    std::string title, subtitle, xTitle;
    std::vector<RidgelineSeries> series;     // empty = one unnamed series
    std::vector<RidgelineRow> rows;
    RidgelineDensityOptions density;
    RidgelineScale scale = RidgelineScale::Shared;
    std::optional<double> xMin, xMax;        // unset = the data's range
    std::optional<double> period;            // a wrapping axis: 24 for hours

    // The curve drawn for a row and series, scaled to 0..1 by `scale`.
    std::vector<RidgelineCurvePoint> Curve(size_t row, size_t series = 0) const;
    RidgelineRowStatistics Statistics(size_t row, size_t series = 0) const;
    double Bandwidth(size_t row, size_t series = 0) const;
    void Sort(RidgelineSort by, bool descending = false);
    std::vector<std::string> Validate() const;   // empty = fine

    static UltraCanvasRidgelineModel WordRatings();     // invented data, 0-10
    static UltraCanvasRidgelineModel DailyActivities(); // invented, 03:00-03:00
};

// The Gaussian kernel density of `values`, evaluated at `xs`.
std::vector<double> RidgelineKernelDensity(const std::vector<double>& values,
                                           const std::vector<double>& xs,
                                           double bandwidth,
                                           std::optional<double> lowerBound = std::nullopt,
                                           std::optional<double> upperBound = std::nullopt);
double RidgelineScottBandwidth(const std::vector<double>& values);
double RidgelineSilvermanBandwidth(const std::vector<double>& values);
```

Notes:

* A row keeps its raw values, not the curve, so the tooltip and the value
  column can say "312 answers, mean 5.43".
* `entries` has one item per series. A one-series chart (pictures 1 to 3)
  leaves `series` empty and fills one entry per row.

### 5.2 The chart

```cpp
auto chart = CreateRidgelineChart("ratings", 0, 0, 900, 1100);
chart->SetModel(UltraCanvasRidgelineModel::WordRatings());
chart->SetOverlap(2.2);                                  // peaks rise 2.2 rows
chart->SetColoring(RidgelineColoring::ByRowValue);       // red to green by mean
chart->SetColormap(HeatmapColormap::RdYlGn);
chart->SetValueColumn("Average score", RidgelineValue::Mean, 2);   // 2 decimals
chart->SetOutline(Color(30, 30, 30, 255), 1.0f);
chart->SetFillOpacity(0.85f);
chart->onRowClick = [](size_t row) { /* ... */ };
```

* `SetOverlap(double)`: the tallest scaled curve is this many row heights
  tall (0.5 to 5).
* `SetColoring`: `Single`, `Alternating` (two colours, picture 1), `ByRow`
  (a colour map down the rows), `ByRowValue` (a colour map by the value
  column, picture 2) or `BySeries` (picture 4).
* `SetValueColumn(title, Mean | Median | Count | Peak | Custom, decimals)`:
  the numbers beside the names.
* `SetMarks(RidgelineMarks::Mean | Median | Quartiles)`: short vertical
  ticks inside each curve.
* `SetShowRowLines(bool)`: a thin baseline under each row.
* `SetHighlightOnHover(bool)`: other rows fade while one is hovered.
* `SetObliqueView(...)`: Phase 3, §6 O1.

---

## 6. Feature list

### Rows and curves (R)

| ID | Feature | Phase |
|---|---|---|
| R1 | Rows from raw values: a density per row (D1) | 1 |
| R2 | Rows from curves given as (x, y) points | 1 |
| R3 | Several series per row, drawn over each other in series order, with a legend that switches a series off | 2 |
| R4 | Overlap: how many row heights the tallest curve may span | 1 |
| R5 | Scale: each row to its own peak, one shared scale, or equal area | 1 |
| R6 | Sort the rows by mean, median, peak position or name, either way | 1 |
| R7 | A wrapping x axis with a period and a start (03:00 to 03:00): values before the start are moved one period on | 2 |
| R8 | Groups of rows with a heading between them | 3 |

### Density (D)

| ID | Feature | Phase |
|---|---|---|
| D1 | Gaussian kernel density, evaluated at `resolution` points over the x range | 1 |
| D2 | Bandwidth by Scott's or Silverman's rule, a fixed width, and an `adjust` factor | 1 |
| D3 | One bandwidth for all rows (comparable smoothness) or one per row | 1 |
| D4 | Bounded scales: reflect the density at a lower and an upper bound, so a 0-10 rating does not leak mass below 0 | 1 |
| D5 | Tail cut: end each curve where it falls below a share of its peak | 2 |
| D6 | Binned instead of smoothed: a stepped outline of counts per bin (the histogram look) | 2 |

### Appearance (A)

| ID | Feature | Phase |
|---|---|---|
| A1 | Fill colour and opacity, and an optional gradient towards the baseline | 1 |
| A2 | Colouring: single, alternating, a colour map down the rows, a colour map by the value column, by series | 1 |
| A3 | Outline colour and width; a contrasting outline (white on blue, picture 1) | 1 |
| A4 | Row names on the left, from the category axis, with the engine's shortening | 1 |
| A5 | A value column beside the names with its own header ("Average score") | 1 |
| A6 | Vertical grid lines at the x ticks; optional thin baselines | 1 |
| A7 | Theme colours from the engine's chart theme, in light and dark | 1 |

### Annotations (N)

| ID | Feature | Phase |
|---|---|---|
| N1 | Marks inside each curve at the mean, the median or the quartiles | 2 |
| N2 | Vertical reference lines and bands across all rows (the engine's limiters on the x axis) | 1 |
| N3 | Shaded quantile bands inside each curve (the middle 50 % darker) | 2 |
| N4 | Side notes at a row with an arrow ("evening peak ↑") | 3 |

### Interaction (I)

| ID | Feature | Phase |
|---|---|---|
| I1 | Hover: the row in front under the pointer (§7.1); the tooltip gives the row, x, the curve's height there, the count, mean and median | 1 |
| I2 | Highlight on hover: other rows fade | 2 |
| I3 | `onRowClick(row)` and `onRowHover(row)` (-1 when none) | 2 |
| I4 | A thin vertical rule that follows the pointer across all rows, with each row's value at that x | 3 |

### Import / export (X)

| ID | Feature | Phase |
|---|---|---|
| X1 | CSV, long form: one line per value (`row,value` or `row,series,value`); or curves (`row,x,y`) | 2 |
| X2 | JSON of the whole model, read and written through `UltraCanvasFileLoader` in the demo | 2 |

### Oblique view (O)

| ID | Feature | Phase |
|---|---|---|
| O1 | An oblique view: each row shifted right and up by a set amount, so the stack looks like a slanted 3D landscape (picture 4). It is a shear of the normalised plane, so lines stay straight and hit regions keep working | 3 |
| O2 | A floor grid under the oblique view, and row names along the slanted side | 3 |

---

## 7. The hard parts

### 7.1 Which curve is under the pointer

Curves overlap by design, so a point on screen can lie inside three rows'
fills. The one the person sees is the one drawn last (the lowest row). Hit
regions are therefore registered in drawing order and tested front to back:
`HitRegionAt` returns the first row, from the bottom, whose filled outline
holds the point. Between a curve and the next baseline down the pointer is
over nothing, and the tooltip hides.

### 7.2 Room above the top row

The top row's curve rises above the top of the plot by up to
`overlap − 1` row heights. The chart reports that height in `MeasureContent`
so the engine leaves it free above the plot, under the title. Without it the
top curve would be cut off or cover the title.

### 7.3 Bandwidth, bounds and whole-number data

* **The rule.** Scott: `h = 1.06 σ n^(−1/5)`. Silverman uses
  `min(σ, IQR / 1.34)` in place of σ, which suits skewed data better.
  A row with one value, or with every value equal, has σ = 0; it gets the
  shared bandwidth (D3) or, alone, a bandwidth of 1 % of the x range, so it
  draws as a narrow peak and not as nothing.
* **Bounds.** A rating of 0 to 10 smoothed without care shows mass below 0
  and above 10, and too little at the ends. Reflection adds, for each value
  `v`, mirror kernels at `2·lower − v` and `2·upper − v`, and the curve is
  drawn only inside the bounds. The reference word chart draws tails past
  10; with bounds set they end at 10, which is correct for the data.
* **Whole numbers.** Ratings are integers. With a small bandwidth the curve
  becomes a row of spikes at each integer. Scott's rule does not avoid
  this: 300 ratings with a spread of about one point give it a bandwidth
  near 0.35, which spikes. (An earlier draft of this section said Scott
  gives about 1; Phase 1 showed it does not.) Such data wants a fixed
  bandwidth of 0.5 to 0.8, as the word sample uses (0.6); a fixed bandwidth
  below 0.5 over whole-number values earns a `Validate` warning.

### 7.4 Long, thin tails

A density never quite reaches zero. Drawn across the whole x range, every row
gets a long flat line that crowds the baselines. D5 cuts the curve where it
falls below a share of its peak (ggridges' `rel_min_height`). The default is
0, as in pictures 2 and 3, where the tails run to the edges.

### 7.5 Scaling the rows against each other

* `Shared`: one scale for all curves; the tallest point of any row is 1.
  Rows can be compared in height, but a narrow row dwarfs a wide one.
* `RowPeak`: each row's own peak is 1 (picture 1). Only the shapes can be
  compared.
* `RowArea`: each row's area is equal. For densities this is the same as
  `Shared`; for supplied curves (counts per hour) it makes busy and quiet
  activities comparable in shape and height.

The tooltip always gives the unscaled value, so scaling never hides a number.

### 7.6 Speed

The density costs rows × resolution × values. 40 rows × 256 points × 1,000
answers is 10 million kernel evaluations, about 10 ms. For larger data the
model first bins the values into `resolution × 4` bins and runs the kernel
over the bins (linear binning), which is fast for any size and accurate to
well under a pixel. Curves are computed when the model changes, not when the
chart is drawn.

---

## 8. What already exists to build on

| Need | Reuse |
|---|---|
| Base class, theme, legend, tooltips, animation | `UltraCanvasChartEngineElement` |
| Rows as categories, x ticks, grid | `ChartAxis` with `ChartScale::Category` and a numeric or time scale (`UltraCanvasChartAxis.h`) |
| Smooth filled curves | `BuildAreaOutlines` and `ChartLineShape::Curved` (`UltraCanvasChartSeries.h`) |
| Colours down the rows or by value | `SampleColormap`, `HeatmapColormap` (`UltraCanvasColormap.h`), `LegendColorBar` |
| Reference lines and bands (N2) | `ChartLimiter` on the x axis |
| Hover and click | `AddHitRegion` with a polygon, `OnHitRegionHoverChanged` |
| A density to compare against | `UltraCanvasJitterPlotElement::CalculateKernelDensity` (private; the model's will replace a copy of it, not call it) |
| Statistics | `ImageHistogram`'s statistics show the shape of the API (`UltraCanvasHistogramModel.h`) |
| CSV and JSON | `UltraCanvasCSVImport.h`, `UltraCanvasJSON.h`, as the ternary model does |
| Numbers in files | `TryParseFloat` / `FormatFloatClassic` (AGENTS.md rule 2) |
| Loading and saving files | `UltraCanvasFileLoader`; the model functions take and return strings |
| Model tests | `Tests/HistogramModelTest.cpp`, `Tests/TernaryModelTest.cpp` as templates |

Nothing new is needed from the engine or the render context: every element
is a filled polygon, a line or text.

---

## 9. Open questions (with recommendations)

**Q1. A new chart, or a mode of the line chart?** **Recommendation: a new
chart** (§4.1). The line chart's modes share a value axis; a ridgeline has
none. The helpers that draw the curves are shared, not the class.

**Q2. Where does the kernel density live?** **Recommendation: in the
ridgeline model, as free functions** (`RidgelineKernelDensity` and the two
bandwidth rules). They are UI-free and tested on their own. The jitter
plot's private copy moves onto them later, in a change of its own (§10,
Follow-up).

**Q3. Default overlap and scale?** **Recommendation: overlap 1.8 and the
`Shared` scale for densities; `RowPeak` when curves are given.** That
matches what each reference picture does with its kind of data.

**Q4. Can the demo use the survey data from the pictures?** The word-rating
pictures show a published survey's results. **Recommendation: invent the
data.** The demo's samples are generated: answers drawn from a distribution
per word whose centre follows the word's meaning, with the averages in a
plausible order. The guide says the data is invented. The time-of-day
sample is generated the same way.

**Q5. Is the oblique view worth it?** It is the most eye-catching picture
and the least readable: depth makes heights hard to compare.
**Recommendation: Phase 3, and only as the shear (O1)**, not a true 3D
projection. A shear needs no new engine code and keeps every hit region
correct.

**Q6. Charts section or Diagrams?** A ridgeline plots data on an axis.
**Recommendation: Charts**, as `ridgelinechart`, on the engine with the
other charts.

---

## 10. Suggested phasing

| Phase | Delivers | Retires |
|---|---|---|
| **1** *(done)* | The model with R1, R2, R4-R6, D1-D4 and the statistics; the chart with A1-A7, N2 and I1; the `WordRatings` and `DailyActivities` samples; the demo page (**Charts → Ridgeline Chart**) with an overlap slider and choices of scale, sort and colouring; the guide; model and render tests | Reference shapes 2 and 3; shape 1 without the wrap and side notes |
| **2** *(done)* | Several series per row with a legend (R3); the wrapping axis (R7); tail cut and binned curves (D5, D6); marks and quantile bands (N1, N3); highlight and row callbacks (I2, I3); CSV and JSON (X1, X2) with demo Open / Save through `UltraCanvasFileLoader` | Reference shape 1 in full |
| **3** | The oblique view and floor grid (O1, O2); row groups (R8); side notes (N4); the pointer rule across rows (I4) | Reference shape 4 |
| **Follow-up** | A change of its own, after Phase 1: `UltraCanvasJitterPlotElement`'s private `CalculateKernelDensity` (its violin and raincloud modes) moves onto `RidgelineKernelDensity` and the bandwidth rules, so the framework has one 1D density. The violins should look the same; the jitter plot's tests and demo page confirm it | The second copy of the density |

Every phase is filled polygons, lines and text through the engine's
projection.

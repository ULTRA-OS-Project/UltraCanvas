# Error Bars — Research & Feature Proposal

**Status:** Proposal. Nothing in this document is implemented yet.

An error bar shows how sure a value is: a thin line through a point or the
top of a bar, from a low to a high value, usually with short caps at both
ends. It can stand for a standard deviation, a standard error, a 95 %
confidence interval, a measured minimum and maximum, or a tolerance. Typical
uses:

* survey averages per month for two groups, each with its interval;
* measured cost against price, with an uncertainty in both directions;
* mean height per age group for men and women, with the spread of each.

UltraCanvas draws no error bars on any chart. This proposal adds them once,
in the chart engine, as an option that the line, scatter and bar charts
share. It is not a new kind of chart: an error bar always belongs to a point
of a chart that exists already. It is listed in the element catalogue on its
own so that it can be found by name, and the demo gets a page of its own.

**Author:** UltraCanvas Framework

---

## Contents

- [1. Summary — the headline question](#1-summary--the-headline-question)
- [2. What an error bar is](#2-what-an-error-bar-is)
- [3. Reference shapes](#3-reference-shapes)
- [4. Where this belongs in the repository](#4-where-this-belongs-in-the-repository)
  - [4.1 An engine option, not a chart type](#41-an-engine-option-not-a-chart-type)
  - [4.2 The bar chart the third picture needs](#42-the-bar-chart-the-third-picture-needs)
  - [4.3 Registration checklist (what "done" touches)](#43-registration-checklist-what-done-touches)
- [5. Data model sketch](#5-data-model-sketch)
  - [5.1 The engine part](#51-the-engine-part)
  - [5.2 On each chart](#52-on-each-chart)
- [6. Feature list](#6-feature-list)
  - [Data (E)](#data-e)
  - [Drawing (D)](#drawing-d)
  - [Charts (C)](#charts-c)
  - [Interaction (I)](#interaction-i)
- [7. The hard parts](#7-the-hard-parts)
  - [7.1 The axis has to reach the bars](#71-the-axis-has-to-reach-the-bars)
  - [7.2 Grouped bars and series that share an x](#72-grouped-bars-and-series-that-share-an-x)
  - [7.3 Stacked bars](#73-stacked-bars)
  - [7.4 Logarithmic axes](#74-logarithmic-axes)
  - [7.5 Computed intervals](#75-computed-intervals)
  - [7.6 Labels and markers](#76-labels-and-markers)
- [8. What already exists to build on](#8-what-already-exists-to-build-on)
- [9. Open questions (with recommendations)](#9-open-questions-with-recommendations)
- [10. Suggested phasing](#10-suggested-phasing)

## 1. Summary — the headline question

**Does any chart we have draw error bars?** No. What comes closest:

| Candidate | What it does | Why it is not an error bar |
|---|---|---|
| `UltraCanvasEngineLineChart::AddSeriesBand` | A filled band between two series, point by point | A continuous band, not a bar per point; one per pair of series; no caps, no x direction |
| `UltraCanvasEngineFinancialChart` | High-low lines and candles | Built into the price chart, with open and close; not available to other charts |
| `UltraCanvasEngineDumbbellChart` | A range between two values per category | The range is the data itself, not the uncertainty of a value |
| `UltraCanvasJitterPlotElement` box mode | Whiskers at 1.5 IQR | A box plot of raw points, on the legacy chart base |
| `UltraCanvasTimelineChart` | Uncertain start and end dates as lighter ends | Dates in a timeline only |

**Should error bars be a chart of their own, or an option of several
charts?** **An option**, written once in the engine (§4.1):

* the engine gets one geometry function and one drawing function for error
  bars, beside `BuildBarSpans` and `DrawChartMarker`, working through the
  projection so that vertical, horizontal and polar charts all get them;
* the line chart, the scatter chart (both directions) and the bar chart take
  error bars per series;
* the grouped bar chart that picture 3 needs exists today only inside the
  demo; this proposal moves it into the library as
  `UltraCanvasEngineBarChart` (§4.2);
* the element catalogue gets a row of its own ("how sure each value is"),
  and the demo a page of its own, **Charts → Error Bars**.

Two phases (§10). Phase 1 draws pictures 1 and 2; Phase 2 picture 3.

---

## 2. What an error bar is

* **A range per point.** Each point has a low and a high value around its
  value. The range is often symmetric (± 0.12) but need not be (−0.1,
  +0.25), and a point can have none.
* **A direction.** On line and bar charts the range runs along the value
  axis. On a scatter chart it can run along y, along x, or both, which
  gives a cross (picture 2).
* **Caps.** Short lines across each end. Their width is fixed in pixels;
  some styles leave them off.
* **Where the range comes from.** Given by the program as low and high
  values, or as ± amounts; a fixed amount or a percentage for a whole
  series; or computed from the raw values behind each point: standard
  deviation, standard error of the mean, or a confidence interval.
* **What it is not.** A band (the space between two curves is shaded,
  `AddSeriesBand`), a box plot (quartiles of raw data) or a range chart
  (the range is the data).

---

## 3. Reference shapes

The three reference pictures the request came with:

| # | Picture | Needs |
|---|---|---|
| 1 | *Perceived threat of COVID-19 by political affiliation (March–August 2020)*: two line series over six months, circle and square markers, symmetric y error bars with caps in each series' colour | Line chart, per-series error bars (C1); ± values (E1); caps (D2); the value axis reaching the bars (§7.1) |
| 2 | *Prime costs and prices*: a scatter of about 20 items, each with an x and a y error bar | Scatter chart with both directions (C2); ± values per point in x and in y (E1) |
| 3 | *Mean height*: grouped bars, men and women for each age group, an error bar on top of each bar in a darker shade of the bar | A library bar chart (C3, §4.2); error bars centred on each bar of the group (§7.2); a colour derived from the series colour (D3) |

The demo recreates all three with invented data.

---

## 4. Where this belongs in the repository

### 4.1 An engine option, not a chart type

A separate `UltraCanvasErrorBarChart` was considered. It would have to draw
lines, markers, scatter points and grouped bars, so it would repeat three
charts and fall behind them as they grow. Each reference picture is one of
those charts with a range added to its points.

The engine already shares series geometry between charts:
`UltraCanvasChartSeries.h` has `BuildBarSpans` for every bar-family chart,
`BuildLineRuns` and `BuildAreaOutlines` for the line family, and
`DrawChartMarker` for markers. Error bars fit there as one more piece:
geometry in normalised coordinates, drawn through `IChartProjection`. Polar
and horizontal charts then get them with no extra code: on a polar chart an
error bar runs along a radius.

### 4.2 The bar chart the third picture needs

The library's bar charts today are:

* `UltraCanvasBarChartElement`, on the legacy chart base: one series, no
  groups;
* `EngineBarChart`, a class defined inside
  `Apps/DemoApp/UltraCanvasBarChartExamples.cpp`. It draws grouped, stacked
  and 100 % bars on the engine with `BuildBarSpans`. `UltraCanvasEngineCharts.md`
  lists it as the engine port of the bar chart, but a program cannot use it:
  it is not in the library.

Picture 3 needs grouped bars. **Recommendation: move `EngineBarChart` into
the library as `UltraCanvasEngineBarChart`**, with its current features and
the demo switched over to it, and give it error bars there. Adding grouped
series to the legacy bar chart instead would extend a chart the engine is
replacing.

### 4.3 Registration checklist (what "done" touches)

1. `UltraCanvas/include/Plugins/Charts/Engine/UltraCanvasChartSeries.h` and
   its `.cpp`: `ChartErrorRange`, `ChartErrorBarOptions`,
   `BuildErrorBars` and `DrawChartErrorBars` (§5.1).
   `UltraCanvas/include/Plugins/Charts/Engine/UltraCanvasChartAnalysis.h`:
   the interval statistics (E4).
2. `UltraCanvasEngineLineChart`, `UltraCanvasEngineScatterChart` and (Phase
   2) the new `UltraCanvasEngineBarChart`: the per-series fields, the axis
   ranges (§7.1), the tooltip lines.
3. Phase 2: `UltraCanvas/include/Plugins/Charts/Engine/Charts/UltraCanvasEngineBarChart.h`
   and `.cpp`, moved from the demo and added to `UltraCanvas/CMakeLists.txt`;
   `UltraCanvasBarChartExamples.cpp` uses it.
4. `Apps/DemoApp/UltraCanvasErrorBarExamples.cpp`, declared in
   `UltraCanvasDemo.h`, registered in the **Charts** section as `errorbars`
   with one variant per picture, and added to the demo sources in the root
   `CMakeLists.txt`.
5. `Docs/UltraCanvas/UltraCanvasErrorBars.md`, the guide, with a generated
   `## Contents`; a section in `UltraCanvasChartEngine.md`; and a row in
   `UltraCanvasUIElements.md` ("how sure each value is: error bars on line,
   scatter and bar charts").
6. `Tests/ChartEngineTest.cpp` cases for the geometry and the statistics
   (UI-free), and `Tests/ErrorBarRenderTest.cpp` drawing each chart with
   error bars, in `Tests/CMakeLists.txt`.
7. A `Docs/UltraCanvas/changelog.d/` entry. Run
   `python3 scripts/generate_doc_toc.py`, `check_doc_examples.py` on the
   guide, `check_element_catalogue.py`, `check_ui_reuse.py` and
   `check_callback_cycles.py`. Remove this proposal's entries from
   `scripts/doc_examples_baseline.txt` with
   `check_doc_examples.py --update-baseline` as each API lands.

---

## 5. Data model sketch

Everything in this section is a sketch of an API that does not exist yet.

### 5.1 The engine part

```cpp
#include "Plugins/Charts/Engine/UltraCanvasChartSeries.h"

// One point's range, in data units, measured from the point's value:
// low and high are distances (both >= 0), so ± 0.12 is {0.12, 0.12}.
// NaN in either = no bar on that side.
struct ChartErrorRange {
    double low = std::nan("");
    double high = std::nan("");
};

enum class ChartErrorKind {
    None,
    Given,            // the ranges in the series (per point)
    Fixed,            // one amount for every point
    Percent,          // a share of each value
    StandardDeviation,
    StandardError,
    Confidence        // a confidence interval at `confidence` (0.95)
};

struct ChartErrorBarOptions {
    ChartErrorKind kind = ChartErrorKind::None;
    double amount = 0.0;              // Fixed: data units; Percent: 0..100
    double confidence = 0.95;         // Confidence
    double multiple = 1.0;            // StandardDeviation / StandardError: 2 = ±2σ
    float lineWidth = 1.0f;
    float capWidth = 8.0f;            // pixels; 0 = no caps
    Color color = Color(0, 0, 0, 0);  // alpha 0 = the series colour, darkened
    bool dashed = false;
};

// One drawn error bar in normalised space: its centre, its two ends, and
// the direction it runs (along the value axis, or along the domain).
struct ChartErrorBar {
    ChartNormalizedPoint centre, low, high;
    bool alongValue = true;
    size_t series = 0, index = 0;
};

std::vector<ChartErrorBar> BuildErrorBars(const ChartAxis& valueAxis,
                                          const std::vector<ChartNormalizedPoint>& centres,
                                          const std::vector<double>& values,
                                          const std::vector<ChartErrorRange>& ranges,
                                          bool alongValue = true);
void DrawChartErrorBars(IRenderContext* ctx, const IChartProjection& projection,
                        const std::vector<ChartErrorBar>& bars,
                        const ChartErrorBarOptions& options, const Color& seriesColor);
```

The interval statistics go in `UltraCanvasChartAnalysis.h`:

```cpp
// The ± range for the mean of `samples` by `kind` (StandardDeviation,
// StandardError or Confidence); Student's t below 30 samples.
ChartErrorRange ComputeErrorRange(const std::vector<double>& samples,
                                  ChartErrorKind kind, double confidence = 0.95,
                                  double multiple = 1.0);
double StudentTQuantile(double probability, int degreesOfFreedom);
```

### 5.2 On each chart

```cpp
// Line chart: per series, one range per value (picture 1).
EngineLineSeries democrats;
democrats.values = {4.26, 4.46, 4.36, 4.29, 4.49, 4.47};
democrats.errors = {{0.10, 0.10}, {0.11, 0.11}, {0.13, 0.13},
                    {0.13, 0.13}, {0.12, 0.12}, {0.12, 0.12}};
democrats.errorBars.kind = ChartErrorKind::Given;

// Or one amount for the whole series, or a share of each value:
democrats.errorBars.kind = ChartErrorKind::Percent;
democrats.errorBars.amount = 5.0;

// Or computed from the raw answers behind each point:
democrats.samples = monthlyAnswers;         // std::vector<std::vector<double>>
democrats.errorBars.kind = ChartErrorKind::Confidence;

// Scatter chart: per point, in y and in x (picture 2).
EngineScatterPoint item(278.0, 790.0);
item.yError = {28.0, 28.0};
item.xError = {20.0, 20.0};
group.errorBars.kind = ChartErrorKind::Given;

// Bar chart: per series, centred on each bar of the group (picture 3).
EngineBarSeries male{"Male", {104, 130, 165, 178, 185}};
male.errors = {{3, 7}, {10, 4}, {16, 15}, {25, 15}, {18, 9}};
male.errorBars.kind = ChartErrorKind::Given;
```

* `errorBars` is a `ChartErrorBarOptions` on each series (scatter: each
  group), so one series can show 95 % intervals and another none.
* `errors` holds given ranges, one per value; shorter than the values = the
  rest have none.
* A scatter point carries `xError` and `yError` itself, because scatter
  points are not indexed by category.

---

## 6. Feature list

### Data (E)

| ID | Feature | Phase |
|---|---|---|
| E1 | Ranges given per point: ± or separate low and high | 1 |
| E2 | One fixed amount for a whole series | 1 |
| E3 | A percentage of each value | 1 |
| E4 | Computed from raw samples per point: standard deviation (× a multiple), standard error, confidence interval with Student's t | 1 |
| E5 | One side only (only the high side, as some bar charts show) | 1 |
| E6 | Ranges in CSV: extra columns `low` / `high` or `error` read by the demo's import | 2 |

### Drawing (D)

| ID | Feature | Phase |
|---|---|---|
| D1 | A line from low to high through the point, along the value axis | 1 |
| D2 | Caps of a set pixel width; none | 1 |
| D3 | Colour: the series colour darkened (picture 3), the series colour (picture 1) or set; line width; dashed | 1 |
| D4 | Both directions on scatter points (a cross, picture 2) | 1 |
| D5 | Drawn in front of the line and behind the marker, so the marker stays readable; on bars, in front of the bar | 1 |
| D6 | Through the projection: horizontal charts draw them across, polar charts along the radius | 1 |
| D7 | A thin translucent box instead of a line (the "error box" style) | 2 |

### Charts (C)

| ID | Feature | Phase |
|---|---|---|
| C1 | `UltraCanvasEngineLineChart`, per series | 1 |
| C2 | `UltraCanvasEngineScatterChart`, per group, x and y per point | 1 |
| C3 | `UltraCanvasEngineBarChart` (moved from the demo, §4.2), per series, grouped and stacked | 2 |
| C4 | `UltraCanvasEnginePolarChart`, per series | 2 |
| C5 | `UltraCanvasEngineDumbbellChart` and the radar chart | Later |

### Interaction (I)

| ID | Feature | Phase |
|---|---|---|
| I1 | The tooltip adds the range: "4.26 (4.16 – 4.36)", and what it is ("95 % CI", "± 1 SD") | 1 |
| I2 | The legend switch hides a series' error bars with it | 1 |
| I3 | `SetShowErrorBars(false)` on a chart hides all of them, for a quick look without | 1 |

---

## 7. The hard parts

### 7.1 The axis has to reach the bars

A value axis that fits only the values cuts off the bars of the highest and
lowest points (in picture 1 the top bar reaches 4.61 while the highest value
is 4.49). Each chart's `DescribeAxes` must widen the value range to
`value + high` and `value − low` of every shown bar, and the scatter chart
its x range too. Hidden series (I2) do not widen it.

### 7.2 Grouped bars and series that share an x

On a grouped bar chart each bar of a group sits at its own offset; the error
bar is centred on the bar, not on the category. `BuildBarSpans` already gives
each bar's centre, so the error bars take their centres from the spans. On a
line chart two series at the same x draw their bars on top of each other;
an optional small offset per series (`SetErrorBarDodge`) keeps them apart, as
scientific plots often do. Off by default, because it moves the bars off
their points.

### 7.3 Stacked bars

On a stacked bar the error bar belongs at the top of each segment, measured
from the segment's top, not from zero. Usually only the top segment's bar is
wanted (the total's uncertainty). **Recommendation:** in Phase 2, stacked
charts draw error bars only for the total, given per category; per-segment
bars are left out, because overlapping segments make them unreadable.

### 7.4 Logarithmic axes

A range below zero on a log axis has no place. The low end is clamped to the
axis floor and drawn without a cap, so the reader sees that it runs on.
Symmetric ranges in data become unequal on screen, which is correct.

### 7.5 Computed intervals

* **Standard deviation** describes the spread of the data; **standard
  error** (σ / √n) and the **confidence interval** describe how sure the
  mean is. The tooltip says which one is drawn, because they differ by a
  factor of √n and readers confuse them.
* Below 30 samples the confidence interval uses Student's t, not 1.96.
  `StudentTQuantile` is computed by inverting the t distribution's CDF
  (incomplete beta function) and is tested against published table values.
* A point with fewer than two samples has no computed bar.

### 7.6 Labels and markers

Value labels placed above a point would collide with its bar. The label plan
receives each error bar as a keep-out rectangle, as it already does for
other furniture, so labels move beside the bar. Markers are drawn after the
bars (D5).

---

## 8. What already exists to build on

| Need | Reuse |
|---|---|
| Normalised space and projections (vertical, horizontal, polar) | `UltraCanvasChartProjection.h` |
| Bar centres in groups and stacks | `BuildBarSpans` (`UltraCanvasChartSeries.h`) |
| Markers drawn after the bars | `DrawChartMarker`, `ChartMarkerShape` |
| A grouped and stacked engine bar chart to move into the library | `EngineBarChart` in `Apps/DemoApp/UltraCanvasBarChartExamples.cpp` |
| Statistics helpers | `UltraCanvasChartAnalysis.h`; `UltraCanvasControlLimits.h` for the shape of a tested statistics API |
| Tooltips and legend switching | `AddHitRegion`, `OnLegendEntryToggled` |
| Keep-out rectangles for labels | `ChartLabelBroker` (`UltraCanvasChartLabels.h`) |
| Darker shade of a series colour | the engine theme's colour helpers |
| Tests | `Tests/ChartEngineTest.cpp`, `Tests/EngineLineChartTest.cpp` |

Nothing new is needed from the render context: an error bar is three lines.

---

## 9. Open questions (with recommendations)

**Q1. A chart type, or an option?** **Recommendation: an option of the
line, scatter and bar charts**, built once in the engine (§4.1), with its
own catalogue row and demo page so it is found by name.

**Q2. Where do grouped bars come from?** **Recommendation: move the demo's
`EngineBarChart` into the library as `UltraCanvasEngineBarChart`** (§4.2)
in Phase 2. It is already written and tested by the demo; the move gives
programs a grouped engine bar chart whether or not they want error bars.

**Q3. Ranges as distances or as end values?** **Recommendation: distances
from the value** (`{0.12, 0.12}`). Most sources give "mean ± error", and
distances survive a change of the value without being recomputed. A helper
`ChartErrorRange::FromEnds(value, low, high)` covers data given as ends.

**Q4. Default colour?** **Recommendation: the series colour, darkened by
about 30 %.** On bars (picture 3) the plain series colour would vanish into
the bar; on lines (picture 1) the darker shade still reads as the series.

**Q5. Should the legacy charts get error bars?** **Recommendation: no.**
The engine charts replace them; the legacy bar, line and scatter elements
stay as they are.

---

## 10. Suggested phasing

| Phase | Delivers | Retires |
|---|---|---|
| **1** | The engine geometry and drawing (D1-D6); given, fixed, percentage and computed ranges (E1-E5) with `ComputeErrorRange` and `StudentTQuantile`; the line chart (C1) and scatter chart (C2); the tooltip, legend and show switches (I1-I3); the axis widening (§7.1); the demo page **Charts → Error Bars** with pictures 1 and 2; the guide and catalogue row; engine and render tests | Reference shapes 1 and 2 |
| **2** | `UltraCanvasEngineBarChart` moved from the demo into the library (§4.2) with error bars, grouped and for stacked totals (C3); the polar chart (C4); the error-box style (D7); ranges in the demo's CSV import (E6); picture 3 on the demo page | Reference shape 3 |
| **Later** | The dumbbell and radar charts (C5) | — |

Every part is lines through the engine's projection.

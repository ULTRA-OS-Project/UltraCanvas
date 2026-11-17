# UltraCanvasRidgelineChart

A ridgeline chart (also called a joyplot): one row per category, each with a
filled curve on its own baseline that may rise over the rows above it. Read
down the rows to compare where each row's values lie, how widely they
spread, and whether they have one hump or two.

```cpp
#include "Plugins/Charts/Engine/Charts/UltraCanvasRidgelineChart.h"
```

- Design: [`UltraCanvasRidgelineChartProposal.md`](UltraCanvasRidgelineChartProposal.md)
  (this is Phase 1 of three)

## Contents

- [Overview](#overview)
- [Quick start](#quick-start)
- [Rows from values: the density](#rows-from-values-the-density)
- [Rows from curves](#rows-from-curves)
- [Scale, order and statistics](#scale-order-and-statistics)
- [Appearance](#appearance)
- [Reading the chart](#reading-the-chart)
- [Demo](#demo)
- [Tests](#tests)

## Overview

| Part | What it is |
|---|---|
| `UltraCanvasRidgelineModel` | UI-free: the rows, the kernel density, the scaling, sorting and each row's statistics |
| `UltraCanvasRidgelineChart` | The chart on the chart engine: the rows back to front, their names, a value column, the x axis, hover |

A row gets its curve one of two ways: from **raw values**, whose spread the
model turns into a smooth curve (a Gaussian kernel density), or from a
**curve the program already has**, as (x, y) points. Rows are drawn from the
top down, so each row's fill covers the outline of the row behind it, and
the pointer finds the row in front.

## Quick start

```cpp
UltraCanvasRidgelineModel model;
model.title = "Response times";
model.xTitle = "Milliseconds";
model.rows.push_back({"Server A", {112, 98, 130, 141, 120, 105}, {}, Color(0, 0, 0, 0), ""});
model.rows.push_back({"Server B", {210, 190, 260, 180, 230, 205}, {}, Color(0, 0, 0, 0), ""});
model.Sort(RidgelineSort::ByMedian);

auto chart = CreateRidgelineChart("times", 0, 0, 700, 500);
chart->SetModel(model);
chart->SetOverlap(2.0);                                   // peaks rise two rows
chart->SetValueColumn("Median", RidgelineValue::Median, 0);
```

## Rows from values: the density

The model smooths each row's values with a Gaussian kernel. The kernel's
width, the **bandwidth**, decides how smooth the curve is:

```cpp
model.density.rule = RidgelineBandwidth::Scott;      // 1.06 sigma n^(-1/5), the default
model.density.rule = RidgelineBandwidth::Silverman;  // 0.9 min(sigma, IQR / 1.34) n^(-1/5)
model.density.rule = RidgelineBandwidth::Fixed;      // in x units:
model.density.bandwidth = 0.6;
model.density.adjust = 1.5;                          // smoother than the rule says
model.density.sharedBandwidth = true;                // every row equally smooth
```

A **bounded scale**, such as a rating from 0 to 10, gets its bounds. The
density is then reflected at each bound, so it neither leaks past the ends
nor sags at them:

```cpp
model.density.lowerBound = 0.0;
model.density.upperBound = 10.0;
```

**Whole numbers** need care. Ratings from 0 to 10 with a spread of about one
point give Scott's rule a bandwidth near 0.35 for 300 answers, and the curve
then shows a spike at every whole number. A fixed bandwidth of 0.5 to 0.8
draws the shape instead; `Validate()` warns when a fixed bandwidth below
0.5 meets whole-number values.

The curve is evaluated at `density.resolution` points (256) over the x
range: the data, three bandwidths wider, clamped to the bounds, or
`model.xMin` / `model.xMax` when set. A row of more than about a thousand
values is binned first, which keeps it fast at any size. The density
function is free for other uses:

```cpp
std::vector<double> xs = {0.0, 0.5, 1.0};
std::vector<double> heights = RidgelineKernelDensity({0.2, 0.4, 1.1}, xs, 0.3, 0.0);   // reflected at 0
double h = RidgelineScottBandwidth({0.2, 0.4, 1.1});
```

## Rows from curves

When the program already has the curve, such as participants per quarter
hour, it gives the points instead of values (x increasing, y not negative):

```cpp
RidgelineRow running;
running.name = "Running";
for (int q = 0; q <= 96; ++q) {
    const double hour = 3.0 + q * 0.25;
    running.curve.push_back({hour, std::exp(-0.5 * (hour - 7.0) * (hour - 7.0))});
}
model.rows.push_back(running);
model.scale = RidgelineScale::RowPeak;
model.curveName = "Participants";
```

A supplied curve is joined smoothly (a monotone cubic, which never rises
above its points), so a coarse curve still draws round.

## Scale, order and statistics

| `model.scale` | Every row's curve is |
|---|---|
| `Shared` (default) | on one scale: the tallest point of any row is 1, so heights can be compared |
| `RowPeak` | scaled to its own peak: only the shapes are compared |
| `RowArea` | given the same area, then one scale |

`SetOverlap(rows)` sets how many row heights a curve of height 1 spans (0.5
to 5; at 1 it just reaches the next baseline up).

```cpp
model.Sort(RidgelineSort::ByMean);           // ByMedian, ByPeak, ByName; `true` = falling
RidgelineRowStatistics s = model.Statistics(0);
// s.count, s.mean, s.median, s.standardDeviation, s.quartile1, s.quartile3,
// s.minimum, s.maximum, s.peakAt (the x of the curve's highest point)
```

A supplied curve's statistics are those of the distribution it draws: its
weighted mean, its median and quartiles by area. `count` is 0. Rows without
data sort last, and `Validate()` names them.

## Appearance

```cpp
chart->SetColoring(RidgelineColoring::ByRowValue);   // by the value column (the mean when it is off)
chart->SetColormap(HeatmapColormap::RdYlGn);         // red to green
chart->SetColoring(RidgelineColoring::ByRow);        // a colour map down the rows
chart->SetColoring(RidgelineColoring::Alternating);  // two colours (SetAlternateColors)
chart->SetColoring(RidgelineColoring::Single);       // SetFillColor
chart->SetFillOpacity(0.85f);
chart->SetGradientFill(true);                        // fades towards the baseline
chart->SetOutline(Color(255, 255, 255, 255), 1.5f);  // alpha 0 = the fill, darker; width 0 = none
chart->SetShowRowLines(true);                        // a thin line along each baseline
chart->SetXTicks({3, 6, 9, 12, 15, 18, 21, 24, 27});
chart->SetXFormatter([](double h) { return std::to_string(static_cast<int>(h) % 24) + ":00"; });
```

A row's own `color` (alpha above 0) wins over the colouring. The value
column shows `Mean`, `Median`, `Count` or `Peak` (an x, through the
formatter), with its header; the row names and values are drawn left of the
plot, and when rows are closer than a line of text every n-th name is shown.
The chart's title is the model's, and its subtitle is drawn under it. Lines
across the plot (an average, a target) are the engine's limiters on axis 1
(`AddLimiter`).

## Reading the chart

```cpp
chart->onRowHover = [&chart](int row) {      // -1 when the pointer leaves the rows
    if (row >= 0) std::string text = chart->DescribeRow(row, chart->GetHoveredX());
};
double baseline = chart->RowBaseline(2);     // normalised plot height
double height = chart->RowHeightAt(2, 5.0);  // the curve above it at x = 5
```

The tooltip, and `DescribeRow`, give the row, the x under the pointer, the
curve's unscaled height there, and the row's count, mean and median (for a
curve: where it peaks and its middle half). A hovered row's name turns bold
and a thin rule marks the x on its curve.

## Demo

**Charts > Ridgeline Chart** shows two invented samples:
`UltraCanvasRidgelineModel::WordRatings()`, how 300 people might rate 33
words from *Abysmal* to *Perfect* on a scale of 0 to 10, coloured red to
green by the average score; and `DailyActivities()`, when in the day people
might do 16 activities, from 03:00 to 03:00, each to its own peak. The
overlap, scale, order and colouring can be changed, and the row under the
pointer is read out.

## Tests

`Tests/RidgelineModelTest.cpp` checks the quantiles, both bandwidth rules,
the density (its area, a single value's peak, reflection at bounds, binning
against the exact sum), the x range, the three scales, statistics from
values and from curves, sorting, validation and the samples.
`Tests/RidgelineChartRenderTest.cpp` draws the chart and checks the rows'
colours on their baselines, the lower row in front where rows overlap, the
tallest curve reaching the top, the hit strips and tooltips, the value
column, the colourings and hover.

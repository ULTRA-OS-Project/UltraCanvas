# UltraCanvasRidgelineChart

A ridgeline chart (also called a joyplot): one row per category, each with a
filled curve on its own baseline that may rise over the rows above it. Read
down the rows to compare where each row's values lie, how widely they
spread, and whether they have one hump or two.

```cpp
#include "Plugins/Charts/Engine/Charts/UltraCanvasRidgelineChart.h"
```

- Design: [`UltraCanvasRidgelineChartProposal.md`](UltraCanvasRidgelineChartProposal.md)
  (Phases 1 and 2 of three)

## Contents

- [Overview](#overview)
- [Quick start](#quick-start)
- [Rows from values: the density](#rows-from-values-the-density)
- [Rows from curves](#rows-from-curves)
- [Scale, order and statistics](#scale-order-and-statistics)
- [Several series per row](#several-series-per-row)
- [A wrapping axis](#a-wrapping-axis)
- [Tails and histograms](#tails-and-histograms)
- [Appearance](#appearance)
- [Marks and bands](#marks-and-bands)
- [Reading the chart](#reading-the-chart)
- [Files](#files)
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

## Several series per row

Two or more series share each row, drawn over each other in series order -
2019 against 2020, bedtimes against wake-up times. `series` names them and
gives them their colours (alpha 0 = the theme's palette); a row holds the
first series' data in `values` / `curve` and the others in `more`:

```cpp
model.series = {{"2019", Color(0, 0, 0, 0)}, {"2020", Color(230, 120, 30, 255)}};
RidgelineRow row;
row.name = "Berlin";
row.SetValues(0, {31, 28, 35, 40});        // 2019
row.SetValues(1, {22, 25, 19, 27});        // 2020
model.rows.push_back(row);
RidgelineRowStatistics later = model.Statistics(0, 1);
```

With two or more series every curve takes its series' colour (the
colourings below are for one series), and a legend right of the plot names
them; a click on an entry hides that series, as `SetSeriesVisible` does.
The tooltip reads every shown series. Sorting and the value column use each
row's first series with data. `RidgelineScale::RowPeak` scales a row's
series together, so they keep their ratio to one another.

## A wrapping axis

Times of day, angles and months run in a circle. With a `period`, the axis
runs one period from `xMin`, values outside it are moved by whole periods
into it, and a density wraps across the ends instead of being cut there:

```cpp
model.xMin = 18.0;                 // 18:00 ...
model.period = 24.0;               // ... to 18:00 the next day
// A bedtime recorded as 0.5 (00:30) is drawn at 24.5, next to 23:30.
double onAxis = model.Wrap(0.5);   // 24.5
```

Statistics are taken on the wrapped values, so the median bedtime of a
group that goes to bed around midnight is 24.67 (00:40) and not a mean of
23:30 and 00:30 that lands at noon. Bounds are not used with a period.

## Tails and histograms

A density never quite reaches zero, so every curve runs along its baseline
to the edges. `density.tailCut` ends each curve where it falls below that
share of its peak (0.02 is usually enough); a supplied curve's statistics
are taken before the cut.

`density.binned` counts the values in `density.bins` equal bins over the x
range instead of smoothing them: a stepped outline whose area is 1, as the
density's is, so the scales work unchanged. Whole-number data looks best
with one bin per value, centred on it:

```cpp
model.density.binned = true;
model.density.bins = 11;
model.xMin = -0.5;                 // 0 .. 10, each answer its own bin
model.xMax = 10.5;
```

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

## Marks and bands

```cpp
chart->SetMarks(RidgelineMarkMedian | RidgelineMarkQuartiles);   // or RidgelineMarkMean
chart->SetQuantileBands(true);       // the middle half (Q1 to Q3) shaded darker
```

A mark is a rule from the baseline up to the curve at the statistic;
quartile marks are thinner. Both are drawn for every shown series.

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
curve: where it peaks and its middle half), for every shown series. A
hovered row's name turns bold and a thin rule marks the x on its curve.

```cpp
chart->SetHighlightOnHover(true);    // the other rows fade while one is hovered
chart->onRowClick = [](int row) { /* pressed and released on the same row */ };
```

## Files

The whole model goes to and from JSON, the data alone to and from CSV. The
functions take and return text; the files themselves go through
`UltraCanvasFileLoader`, as every user file does:

```cpp
std::string json = ExportRidgelineJson(model);
UltraCanvasRidgelineModel loaded;
std::string error;
if (!ImportRidgelineJson(json, loaded, &error)) { /* error says why */ }

std::string csv = ExportRidgelineCsv(model);      // row,series,value  or  row,series,x,y
ImportRidgelineCsv("group,value\nA,1\nA,2.5\nB,3\n", model, &error);   // replaces the rows
```

A CSV line holds one value or one curve point; the row column may be called
row, name, group or category, the series column is optional, and numbers
are dot-decimal in every locale. Importing keeps every setting of the model
and the colour of a series it already has. Both readers refuse a file they
cannot read in full, leave the model untouched, and say which line or field
is wrong.

## Demo

**Charts > Ridgeline Chart** shows three invented samples:
`UltraCanvasRidgelineModel::WordRatings()`, how 300 people might rate 33
words from *Abysmal* to *Perfect* on a scale of 0 to 10, coloured red to
green by the average score; `DailyActivities()`, when in the day people
might do 16 activities, from 03:00 to 03:00, each to its own peak; and
`Sleep()`, when people of seven age groups go to bed and wake up, two
series on a 24-hour axis that wraps at 18:00. The overlap, scale, order,
colouring, marks, middle-half shading, tail trim, histogram look and
fading can be changed; the row under the pointer is read out and a click
keeps a row's numbers. **Open JSON**, **Save JSON**, **Import CSV** and
**Export CSV** go through `UltraCanvasFileLoader`.

## Tests

`Tests/RidgelineModelTest.cpp` checks the quantiles, both bandwidth rules,
the density (its area, a single value's peak, reflection at bounds, binning
against the exact sum), the x range, the three scales, statistics from
values and from curves, sorting, validation and the samples; several series,
the wrapping axis (a circular density with area 1 that meets itself at the
ends), the tail cut, binned curves, and the JSON and CSV round trips with
their refusals.
`Tests/RidgelineChartRenderTest.cpp` draws the chart and checks the rows'
colours on their baselines, the lower row in front where rows overlap, the
tallest curve reaching the top, the hit strips and tooltips, the value
column, the colourings and hover; two series in their colours with the
legend switching one off, marks, the middle-half shading, the fading on
hover, row clicks, the histogram steps and a clock-time axis.

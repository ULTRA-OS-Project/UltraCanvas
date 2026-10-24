# UltraCanvas Engine Charts Documentation

## Contents

- [Overview](#overview)
- [Which charts were ported](#which-charts-were-ported)
- [Line, area and cumulative flow](#line-area-and-cumulative-flow)
- [Scatter and bubble](#scatter-and-bubble)
- [Waterfall](#waterfall)
- [Diverging and population pyramid](#diverging-and-population-pyramid)
- [Dumbbell](#dumbbell)
- [Financial](#financial)
- [Heatmap](#heatmap)
- [Mekko](#mekko)
- [Radar](#radar)
- [Radial bar](#radial-bar)
- [Pie and donut](#pie-and-donut)
- [Polar](#polar)
- [Engine features added for the ports](#engine-features-added-for-the-ports)
- [Switching a chart](#switching-a-chart)

## Overview

The engine charts are ports of the legacy chart elements onto
`UltraCanvasChartEngineElement`
([`UltraCanvasChartEngine.md`](UltraCanvasChartEngine.md)). Each one implements
only the engine's content contract - its axes description, its own drawing,
its labels - and receives the rest: edge and polar axis furniture with
decluttered tick labels, grids derived from the ticks, a measured layout, the
shared legend (discrete, colour bar or size key), the solved label plan,
themes and palettes, hover hit regions with tooltips, the entrance animation,
and the Vertical / Horizontal / Polar projections.

They are written **next to** the legacy charts, not in place of them: the
legacy classes are unchanged, so an application switches one chart at a time.
**Charts → Chart Engine Comparison** in the demo application
(`Apps/DemoApp/UltraCanvasEngineComparisonExamples.cpp` and the family files
`UltraCanvasEngineComparison{Lines,Bars,Finance,Polar}.cpp`) shows every port
beside the legacy chart it replaces, on the same data, so the two can be
compared before the switch.

- Headers: `include/Plugins/Charts/Engine/Charts/UltraCanvasEngine<Name>Chart.h`
- Sources: `Plugins/Charts/Engine/Charts/UltraCanvasEngine<Name>Chart.cpp`
- Each class has a `CreateEngine<Name>Chart(id, x, y, w, h)` factory and the
  usual `(id, x, y, w, h)` constructor (position and size default to 0, for a
  chart sized by its layout).

**Version:** 1.1.0 (`SetValueAtLeast` on the line family)
**Last Modified:** 2026-10-24
**Author:** UltraCanvas Framework
**Namespace:** `UltraCanvas`

## Which charts were ported

The charts that draw on axes or around a centre - the pattern the engine
models - were ported. Charts whose layout is not an axis system at all (the
sunburst, chord, Gantt, timeline, Kanban, funnel, pyramid, nested-area,
quadrant, circular progress and infographic charts, the 3D scatter and
surfaces) stay on their own renderers for now; the 3D ones wait for the
engine's `Space3D` projection.

| Legacy chart | Engine port | Notes |
|---|---|---|
| `UltraCanvasLineChartElement` | `UltraCanvasEngineLineChart` (`Line`) | Straight, monotone curved or stepped; markers; polar line chart under Polar |
| `UltraCanvasAreaChartElement` | `UltraCanvasEngineLineChart` (`Area`) | Gradient fill orthogonal only (flat under Polar) |
| `UltraCanvasCumulativeFlowChartElement` | `UltraCanvasEngineLineChart` (`StackedArea`, `SetReverseStackOrder`) | Lead / cycle time and WIP as engine span annotations; no Kanban binding |
| `UltraCanvasScatterPlotElement` | `UltraCanvasEngineScatterChart` | Trend line with r / r², confidence ellipses, both grids |
| `UltraCanvasBubbleChartElement` | `UltraCanvasEngineScatterChart` (size channel) | XY bubbles only - packed and hierarchical layouts have no axes |
| `UltraCanvasBarChartElement` | the demo's `EngineBarChart` (`UltraCanvasBarChartExamples.cpp`) | Converted earlier, on its own page |
| `UltraCanvasWaterfallChartElement` | `UltraCanvasEngineWaterfallChart` | Change / subtotal / total steps, connectors |
| `UltraCanvasDivergingBarChart` | `UltraCanvasEngineDivergingBarChart` | Likert (neutral split across zero), tornado, opposing bars - one shared scale |
| `UltraCanvasPopulationChart` | `UltraCanvasEngineDivergingBarChart` (`PopulationPyramid`) | Surplus shaded inside the true bar; average ages as limiters |
| `UltraCanvasDumbbellChart` | `UltraCanvasEngineDumbbellChart` | Any number of dots per row, nine sort orders |
| `UltraCanvasFinancialChartElement` | `UltraCanvasEngineFinancialChart` | Candles, OHLC, real Heikin-Ashi; volume pane as an axis band; SMA / EMA |
| `UltraCanvasHeatmapChartElement` | `UltraCanvasEngineHeatmapChart` | Grid heatmap; the colour-bar legend; masks; no image mode for huge grids |
| `UltraCanvasMekkoChartElement` | `UltraCanvasEngineMekkoChart` | Marimekko and bar-Mekko; headers pinned over their columns |
| `UltraCanvasRadarChartElement` | `UltraCanvasEngineRadarChart` | Cyclic spokes, polygon web, per-spoke ranges |
| `UltraCanvasRadialBarChart` | `UltraCanvasEngineRadialBarChart` | Rays from a base ring, sectors per series, caps |
| `UltraCanvasPieChartElement` | `UltraCanvasEnginePieChart` | Pie and donut, solved outside labels with leaders; no 3D mode |
| `UltraCanvasPolarChart` | `UltraCanvasEnginePolarChart` | Line, spline, area, scatter and column series; radial and angular bands |
| `UltraCanvasParallelCoordinateChartElement` | (native from the start) | The engine's first client |

Every port draws its series from the theme palette unless the data gives a
colour (`Color(0, 0, 0, 0)`, alpha 0, means "from the palette"), restarts its
entrance animation on new data, and toggles a series when its legend entry is
clicked. None of them has the legacy zoom / pan flags, which the engine does
not model yet.

## Line, area and cumulative flow

```cpp
auto chart = CreateEngineLineChart("Sales", 0, 0, 640, 400);
chart->SetChartTitle("Monthly sales");
chart->SetCategories({"Jan", "Feb", "Mar", "Apr", "May", "Jun"});

EngineLineSeries sales;
sales.name = "2025";
sales.values = {45000, 52000, 48000, 61000, 55000, 67000};
chart->AddSeries(sales);

chart->SetLineShape(ChartLineShape::Curved);     // Straight, StepAfter, ...
chart->SetShowValueLabels(true);
chart->SetValueFormat("$", "", true);            // $52.0K
chart->SetMode(EngineLineChartMode::Area);       // Line, Area, StackedArea, PercentStackedArea
```

`SetXValues` replaces the categories with a numeric domain. A NaN value is a
gap (`SetBridgeGaps(true)` joins across it). `SetValueAtLeast(5)` keeps the
value axis reaching at least 5 whatever the data - a live chart of a quiet
server's rates then reads 0 to 5 rather than an axis fitted to a flat zero; a
live chart also turns off the rise from the base on every update
(`SetAnimateOnDataChange(false)`). Under `ChartProjectionKind::Polar`
the categories go round the circle and the series closes into a loop.

A cumulative flow diagram is the `StackedArea` mode with the first series on
top (`SetReverseStackOrder(true)`); `AddCumulativeFlowAnnotations()` adds the
lead time, cycle time and WIP spans as engine span annotations, which the
value labels steer around.

## Scatter and bubble

```cpp
auto chart = CreateEngineScatterChart("Spend", 0, 0, 640, 400);
EngineScatterGroup campaigns;
campaigns.name = "Campaigns";
campaigns.points = {{1200, 21000}, {2600, 26000}, {4100, 28500}, {7800, 41000}};
chart->AddGroup(campaigns);
chart->SetAxisTitles("Marketing spend", "Sales");
chart->SetShowTrendLine(true);                   // least squares, with r and r^2
chart->SetShowConfidenceEllipses(true, 0.95);    // engine highlight per group
```

A point with a finite `size` turns the chart into a bubble chart: radii are
area-proportional (`SetSizeScale(EngineBubbleScale::Diameter)` for the other
convention) and `SetShowSizeLegend(true, "Health")` keys them with the engine
legend's area-scaled size key. `colorValue` drives a colour channel keyed by a
colour bar. Names are placed by the label plan (`SetPointLabelMode`), inside
the bubble when they fit; a name pushed away from its mark gets a leader.

## Waterfall

```cpp
auto chart = CreateEngineWaterfallChart("Flow", 0, 0, 640, 400);
chart->AddStep("Start", 500);
chart->AddStep("Q1", 150);
chart->AddStep("Returns", -25);
chart->AddStep("Total", 0, WaterfallStepKind::Subtotal);
chart->SetValueFormat("$", "K", false);
```

`Change` steps float from the running total, `Subtotal` and `Total` stand on
zero. Connectors (`SetConnectorStyle`), cumulative labels ("+150 = 650") and
the Horizontal projection are options.

## Diverging and population pyramid

```cpp
auto chart = CreateEngineDivergingBarChart("Survey", 0, 0, 640, 400);
chart->SetCategories({{"Disagree", Color(0, 0, 0, 0), DivergingSide::Negative},
                      {"Neutral",  Color(0, 0, 0, 0), DivergingSide::Neutral},
                      {"Agree",    Color(0, 0, 0, 0), DivergingSide::Positive}});
chart->AddRow("Question 1", {20, 15, 65});
chart->AddRow("Question 2", {45, 20, 35});
chart->SetStyle(DivergingBarStyle::Likert);      // Tornado, OpposingBars, PopulationPyramid
```

Both sides share one symmetric value axis (`ChartAxis::symmetric`) that prints
magnitudes (`ChartAxis::absoluteLabels`). `SetPopulationData(groups, "Males",
color, "Females", color)` builds a population pyramid in one call; the average
age of each side is a limiter on the age axis.

## Dumbbell

```cpp
auto chart = CreateEngineDumbbellChart("Satisfaction", 0, 0, 640, 400);
chart->SetGroups({{"Before", Color(0, 0, 0, 0)}, {"After", Color(0, 0, 0, 0)}});
chart->AddRow("North America", 62, 78);
chart->AddRow("Latin America", 55, 71);
chart->SetSortOrder(DumbbellRowSort::InsertionOrder);
chart->SetShowDifferenceLabels(true);
```

## Financial

```cpp
auto chart = CreateEngineFinancialChart("Stock", 0, 0, 800, 480);
chart->SetData({{"2024-01-02", 100.0, 102.5, 99.1, 101.8, 2.1e6},
                {"2024-01-03", 101.8, 103.0, 100.4, 100.9, 1.7e6}});
chart->SetCandleStyle(ChartCandleStyle::HeikinAshi);   // Candlestick, OHLCBars
chart->SetShowVolumePane(true);                        // a band below the price
chart->SetShowMovingAverage(true, 20, ChartMovingAverageKind::Exponential);
```

Price and volume are two value axes in separate sub-bands of one plot
(`ChartAxis::bandStart` / `bandEnd`) over one date axis, so the panes share the
grid, the crosshair and the hover slot. Heikin-Ashi candles are computed with
`HeikinAshiCandles` (`Engine/UltraCanvasChartAnalysis.h`).

## Heatmap

```cpp
auto chart = CreateEngineHeatmapChart("Correlation", 0, 0, 520, 480);
chart->SetData({{1.0, 0.4, -0.2}, {0.4, 1.0, 0.1}, {-0.2, 0.1, 1.0}});
chart->SetColumnLabels({"Revenue", "Margin", "Churn"});
chart->SetRowLabels({"Revenue", "Margin", "Churn"});
chart->SetColormap(HeatmapColormap::RdBu);
chart->SetDiverging(true, 0.0);
chart->SetShowCellValues(true);
```

Rows and columns are Category axes (crowded labels are thinned, not
overprinted); the key is the engine legend in `ColorBar` mode, its ticks read
back through the cells' own scale, so a log or diverging key labels exactly
what the cells show.

## Mekko

```cpp
auto chart = CreateEngineMekkoChart("Share", 0, 0, 640, 420);
chart->SetData({{"USA", Color(0, 0, 0, 0)}, {"China", Color(0, 0, 0, 0)}},
               {{"Technology", {88, 44}}, {"Energy", {35, 42}}});
chart->SetSegmentLabelMode(ChartMekkoSegmentLabel::Value);
chart->SetColumnHeaderMode(ChartMekkoHeader::WidthPercent);
```

The domain axis runs 0-100% of the grand total and each column spans its
share. Column headers are pinned labels: a crowded one is dropped, never slid
over its neighbour.

## Radar

```cpp
auto chart = CreateEngineRadarChart("Products", 0, 0, 480, 480);
for (const char* axis : {"Performance", "Features", "Usability", "Price", "Support"}) {
    chart->AddAxis(axis);                        // 0..100 unless given
}
chart->AddSeries("Product A", {90, 80, 70, 60, 85});
chart->AddSeries("Product B", {75, 85, 90, 70, 65});
chart->SetGridLevels(5);
```

The spokes are a cyclic Category domain axis (`ChartAxis::cyclic`), the web is
the polygon value grid (`SetPolarGridShape(ChartPolarGridShape::Polygon)`),
and the value axis is the radial rule - all engine furniture.

## Radial bar

```cpp
auto chart = CreateEngineRadialBarChart("Sensors", 0, 0, 520, 520);
chart->AddSeries("Sensor 01", {{"00:00", 420.0}, {"01:00", 515.0}, {"02:00", 658.0}});
chart->AddSeries("Sensor 02", {{"00:00", 310.0}, {"01:00", 470.0}, {"02:00", 402.0}});
chart->SetInnerRadiusFraction(0.25f);
chart->SetCapStyle(EngineRadialBarCap::Round);
chart->SetCenterText("Sensors");
```

Rays radiate from a base ring (`SetPolarInnerRadius`), each series filling an
angular sector; the series names are the rim labels.

## Pie and donut

```cpp
auto chart = CreateEnginePieChart("Market", 0, 0, 480, 400);
chart->AddSlice("Product A", 31.2);
chart->AddSlice("Product B", 25.0);
chart->AddSlice("Others", 10.7);
chart->SetDonutMode(true);
chart->SetCenterText("Share", "2025");
chart->SetLabelPosition(EnginePieLabelPosition::Auto);
```

A pie is the Polar projection of one 100% stacked bar: slices are ring sectors
from `BuildBarOutline`, outside labels ride the label plan with leader lines
(the centre text is an obstacle), and clicking a legend entry drops the slice
from the shares.

## Polar

```cpp
auto chart = CreateEnginePolarChart("Wind", 0, 0, 520, 520);
chart->AddSeries("Gusts", EnginePolarSeriesType::Spline,
                 {{0, 12.0}, {90, 18.0}, {180, 9.0}, {270, 15.0}});
chart->AddCategorySeries("Monthly", EnginePolarSeriesType::Column,
                         {3, 5, 8, 6, 4, 2, 1, 2, 4, 6, 7, 5});
chart->SetRadialScale(EnginePolarRadialScale::SquareRoot);
```

Numeric (degrees) or categorical angles, mixed series types, radial and
angular bands (engine value-band highlights: rings and wedges under Polar),
linear, logarithmic or square-root radii (`ChartScale::Sqrt`).

## Engine features added for the ports

What a legacy chart did privately and other charts would share went into the
engine, not into the port:

| Feature | Where |
|---|---|
| Polar furniture: rim labels for a domain axis, a radial rule for a value axis, ring / polygon grids, round plot fill, rim labels as label obstacles | `UltraCanvasChartEngineElement` |
| `SetPolarAngles`, `SetPolarInnerRadius`, `SetPolarGridShape`, `SetDomainGridAxis` | `UltraCanvasChartEngineElement` |
| Axis roles, sub-bands, symmetric ranges, cyclic categories, absolute labels, range padding, the `Sqrt` scale, single nice-number rounding, `-$25` | `ChartAxis` |
| Rotated left/right axis titles; edge labels kept inside the element; edge axes on one side stacking outward | `UltraCanvasChartEngineElement` |
| Line runs (straight / curved / stepped, gaps, closed loops), area outlines, line-family stacking, markers (`DrawChartMarker`) | `Engine/UltraCanvasChartSeries.h` |
| Trend fit, moving averages, Heikin-Ashi | `Engine/UltraCanvasChartAnalysis.h` |
| Pinned labels (exact position, dropped rather than slid) | `ChartLabelRequest::pinned` |
| Span annotations (dimension lines with captions), limiter extents and domain-axis limiters, role-aware value bands | `UltraCanvasChartEngineElement` |
| Area-scaled size legend with explicit values | `ChartLegend` (`LegendSizeScale`) |

The model-layer pieces are unit-tested in `Tests/ChartEngineTest.cpp`.

## Switching a chart

1. Compare the two on the demo's comparison page with the application's own
   data shape; the notes column above lists what a port leaves out.
2. Replace the legacy element with the port. The data structs differ (the
   ports take plain value vectors and structs rather than
   `IChartDataSource`), so the feeding code changes; the styling setters keep
   their legacy names where the concept is the same.
3. Theme the chart with `SetTheme("Dark")` or `SetPalette(...)` instead of
   per-chart colour setters, and drop any hand-placed legend or labels - the
   engine places them.

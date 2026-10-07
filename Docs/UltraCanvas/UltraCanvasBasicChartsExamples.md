# Basic Chart Elements: Examples

**Version:** 2.0.0
**Last Modified:** 2026-10-07

The basic chart elements are four ready-made XY charts — line, bar, scatter
and area — declared in
`UltraCanvas/include/Plugins/Charts/UltraCanvasSpecificChartElements.h`. They
share a base class (`UltraCanvasChartElementBase`), one data model
(`ChartDataPoint` in a `ChartDataVector`) and one set of common options; each
adds a few options of its own. Each element draws a single series with a
title, grid, axes and hover tooltips.

The demo app's **Line Chart**, **Scatter Plot Chart** and **Area Chart** pages
are built in `Apps/DemoApp/UltraCanvasBasicChartsExamples.cpp` — read it for
the full pages with their data-switching buttons and toggles. The demo's **Bar
Chart** page shows the chart engine instead
(`Apps/DemoApp/UltraCanvasBarChartExamples.cpp`); `UltraCanvasBarChartElement`
itself is unchanged and is still used, for example by the audio analysis page.

## Which element

| Element | Factory | Draws | Its own options | Reference |
|---|---|---|---|---|
| `UltraCanvasLineChartElement` | `CreateLineChartElement` | a line through the points, straight or smoothed, with optional dots | line colour and width, dots, dot colour, smoothing | [UltraCanvasLineChartElement.md](UltraCanvasLineChartElement.md) |
| `UltraCanvasBarChartElement` | `CreateBarChartElement` | one bar per point | bar colour, border colour and width, spacing | [UltraCanvasBarChartElement.md](UltraCanvasBarChartElement.md) |
| `UltraCanvasScatterPlotElement` | `CreateScatterPlotElement` | one marker per point, optional least-squares trend line and r / r² readout | marker colour, size and shape, trend line, correlation info | [UltraCanvasScatterPlotElement.md](UltraCanvasScatterPlotElement.md) |
| `UltraCanvasAreaChartElement` | `CreateAreaChartElement` | a line with the area under it filled, flat or with a gradient | fill colour or gradient, line, dots, smoothing | [UltraCanvasAreaChartElement.md](UltraCanvasAreaChartElement.md) |

For several series, axis scales, legends or limit lines, use the chart engine
([UltraCanvasChartEngine.md](UltraCanvasChartEngine.md)). For an (x, y, z) point
cloud, `UltraCanvasScatterPlot3DElement`
([UltraCanvasScatterPlot3D.md](UltraCanvasScatterPlot3D.md)) sits next to the 2D
scatter plot on the demo page.

```cpp
#include "Plugins/Charts/UltraCanvasSpecificChartElements.h"
```

## Feeding data

A point is a `ChartDataPoint(x, y, z = 0, label = "", value = 0, color = Colors::Transparent)`.
A `ChartDataVector` holds the points; hand it to the chart with
`SetDataSource`.

```cpp
auto monthlySales = std::make_shared<ChartDataVector>();
monthlySales->LoadFromArray({
    ChartDataPoint(1, 45000, 0, "Jan"),
    ChartDataPoint(2, 52000, 0, "Feb"),
    ChartDataPoint(3, 48000, 0, "Mar"),
    ChartDataPoint(4, 61000, 0, "Apr"),
    ChartDataPoint(5, 55000, 0, "May"),
    ChartDataPoint(6, 67000, 0, "Jun"),
});
monthlySales->AddPoint(ChartDataPoint(7, 71000, 0, "Jul"));

auto fromFile = std::make_shared<ChartDataVector>();
fromFile->LoadFromCSV("/data/sales.csv");     // lines "x,y[,z[,label]]"; throws if the file cannot be opened
```

- **x** places the point horizontally. With the default
  `XAxisLabelMode::NumericValue` the axis is numbered and points sit at their x
  value. With `XAxisLabelMode::DataLabel` the points are spaced evenly in data
  order and the axis shows each point's **label** — the right choice for
  months, quarters, regions.
- **y** is the plotted value. The y axis runs from the smallest to the largest y
  plus 5 % on each side; it does not start at 0 unless the data does.
- **color** is honoured by the scatter plot: a point with a non-transparent
  colour is drawn in it instead of the marker colour.
- **z**, **value** and **category** are carried along for other chart types;
  these four charts do not draw them.
- The chart keeps the pointer and reads the points each time it draws, but it
  works out the axis ranges only when the data source is set. After changing
  the vector (`AddPoint`, `Clear`, `LoadFromArray`), call
  `SetDataSource(data)` again.
- A CSV file is read with dot decimals. Its first line is skipped as a header
  if it contains an `x` or a `y` anywhere, so do not start a header-less file
  with a labelled row such as `1,45000,0,May`. For files too large to hold in
  memory, `ChartDataStream` reads the points in chunks.

## Size and placement

The constructors and factories take `(id, x, y, width, height)` as integers,
and the charts have no content size of their own, so always give a width and
a height. A non-zero x or y pins the chart at that point; inside a flex or grid
container pass `0, 0` and let the container place it
([UltraCanvasLayoutExamples.md](UltraCanvasLayoutExamples.md)).

```cpp
auto chartRow = std::make_shared<UltraCanvasContainer>("chart-row");
chartRow->SetPadding(20);
chartRow->layout.SetFlexRow().SetFlexGap(20);

auto trendChart = CreateLineChartElement("trend", 0, 0, 480, 320);
auto volumeChart = CreateAreaChartElement("volume", 0, 0, 480, 320);
chartRow->AddChild(trendChart);
chartRow->AddChild(volumeChart);
window->AddChild(chartRow);
```

## Options every chart has

| Call | Effect |
|---|---|
| `SetDataSource(data)` | the points to draw (any `IChartDataSource`) |
| `SetChartTitle(text)` | the title above the plot (`SetTitle` does the same) |
| `SetXAxisLabelMode(XAxisLabelMode::DataLabel)` | evenly spaced points labelled with their `label` |
| `SetRotateXAxisLabels(true, 45.0f)` | slanted x labels, for long or many labels |
| `SetShowGrid(bool)`, `SetGridColor(color)` | the grid lines (on by default) |
| `SetShowAxes(bool)` | the axes and their labels (on by default) |
| `SetBackgroundColor(color)`, `SetPlotAreaColor(color)` | the whole element, and the plot rectangle |
| `SetShowValueLabels(bool)` | the y value printed at each point — on by default; drawn by the line and area charts |
| `SetPointRadius(radius)` | the dot radius of the area chart and the offset of value labels |
| `SetEnableTooltips(bool)` | hover tooltip with X and Y (on by default) |
| `SetSeriesName(text)` | a first line for the tooltip |
| `SetCustomTooltipGenerator(fn)` | your own tooltip text, from the point and its index |
| `SetEnableSelection(bool)` | a ring around the hovered point (on by default for the scatter plot) |

```cpp
trendChart->SetSeriesName("Sales 2024");
trendChart->SetCustomTooltipGenerator([](const ChartDataPoint& point, size_t index) {
    return point.label + ": " + std::to_string(static_cast<int>(point.y)) + " EUR";
});
```

## Line chart

```cpp
auto salesChart = CreateLineChartElement("sales-line", 0, 0, 600, 400);
salesChart->SetDataSource(monthlySales);
salesChart->SetChartTitle("Monthly Sales Trend");
salesChart->SetXAxisLabelMode(XAxisLabelMode::DataLabel);
salesChart->SetLineColor(Color(0, 102, 204, 255));
salesChart->SetLineWidth(3.0f);
salesChart->SetShowDataPoints(true);                 // dots on the points (off by default)
salesChart->SetPointColor(Color(255, 99, 71, 255));
salesChart->SetSmoothingEnabled(true);               // a Catmull-Rom curve instead of straight segments
salesChart->SetShowValueLabels(false);
```

## Bar chart

```cpp
auto unitsByRegion = std::make_shared<ChartDataVector>();
unitsByRegion->LoadFromArray({
    ChartDataPoint(1, 420, 0, "North"),
    ChartDataPoint(2, 380, 0, "South"),
    ChartDataPoint(3, 510, 0, "East"),
    ChartDataPoint(4, 290, 0, "West"),
});

auto regionChart = CreateBarChartElement("units-by-region", 0, 0, 600, 360);
regionChart->SetDataSource(unitsByRegion);
regionChart->SetChartTitle("Units by Region");
regionChart->SetXAxisLabelMode(XAxisLabelMode::DataLabel);   // one labelled bar per point
regionChart->SetBarColor(Color(76, 175, 80, 255));
regionChart->SetBarBorderColor(Color(46, 125, 50, 255));
regionChart->SetBarBorderWidth(1.0);                         // 0 for no border
regionChart->SetBarSpacing(0.25);                            // 25 % of each slot left empty (0 to 0.9)
```

Every bar has the same colour, and bars rise from the bottom of the plotted y
range (see *Feeding data*), not from 0.

## Scatter plot

```cpp
auto spendVsSales = std::make_shared<ChartDataVector>();
std::mt19937 generator(42);
std::uniform_real_distribution<double> spend(1000.0, 10000.0);
std::uniform_real_distribution<double> noise(-5000.0, 5000.0);
for (int i = 0; i < 50; ++i) {
    double x = spend(generator);
    spendVsSales->AddPoint(ChartDataPoint(x, x * 3.2 + 15000.0 + noise(generator)));
}
ChartDataPoint outlier(9500, 12000, 0, "Outlier");
outlier.color = Color(220, 60, 60, 255);             // this point's own colour
spendVsSales->AddPoint(outlier);

auto scatter = CreateScatterPlotElement("spend-vs-sales", 0, 0, 540, 420);
scatter->SetDataSource(spendVsSales);
scatter->SetChartTitle("Marketing Spend vs Sales");
scatter->SetPointColor(Color(255, 140, 0, 255));
scatter->SetPointSize(6.0);                          // marker radius in pixels
scatter->SetPointShape(UltraCanvasScatterPlotElement::PointShape::Diamond);   // Circle, Square, Triangle, Diamond
scatter->SetShowTrendLine(true);                     // least-squares line
scatter->SetTrendLineColor(Color(220, 60, 60, 255));
scatter->SetTrendLineStyle(UltraCanvasScatterPlotElement::TrendLineStyle::Dashed);
scatter->SetShowCorrelationInfo(true);               // "y = ax + b" and r / r² beside the plot
```

The fit is also available as numbers:

```cpp
double slope = 0.0;
double intercept = 0.0;
if (scatter->ComputeLinearRegression(slope, intercept)) {      // false for < 2 points or constant x
    double r = scatter->GetCorrelationCoefficient();
    std::printf("y = %.2fx + %.0f, r = %.3f\n", slope, intercept, r);
}
```

## Area chart

```cpp
auto quarterlyRevenue = std::make_shared<ChartDataVector>();
quarterlyRevenue->LoadFromArray({
    ChartDataPoint(1, 85000, 0, "Q1 2024"),
    ChartDataPoint(2, 92000, 0, "Q2 2024"),
    ChartDataPoint(3, 78000, 0, "Q3 2024"),
    ChartDataPoint(4, 105000, 0, "Q4 2024"),
});

auto revenueChart = CreateAreaChartElement("revenue-area", 0, 0, 600, 400);
revenueChart->SetDataSource(quarterlyRevenue);
revenueChart->SetChartTitle("Quarterly Revenue");
revenueChart->SetXAxisLabelMode(XAxisLabelMode::DataLabel);
revenueChart->SetLineColor(Color(0, 150, 136, 255));
revenueChart->SetLineWidth(3.0);
revenueChart->SetFillColor(Color(0, 150, 136, 120));                // used while the gradient is off
revenueChart->SetFillGradientEnabled(true);
revenueChart->SetGradientColors(Color(0, 150, 136, 180),            // at the highest point
                                Color(0, 150, 136, 40));            // at the lowest
revenueChart->SetShowDataPoints(true);
revenueChart->SetPointColor(Color(255, 87, 34, 255));
revenueChart->SetPointRadius(4.0f);
revenueChart->SetSmoothingEnabled(true);
```

## Switching data at run time

A chart can show any data source at any time; this is what the demo's
"Load Revenue" / "Load Sales" buttons do.

```cpp
auto showRevenue = std::make_shared<UltraCanvasButton>("show-revenue", "Revenue");
showRevenue->SetOnClick([salesChart, quarterlyRevenue]() {
    salesChart->SetDataSource(quarterlyRevenue);
    salesChart->SetChartTitle("Quarterly Revenue");
});

auto addMonth = std::make_shared<UltraCanvasButton>("add-month", "Add August");
addMonth->SetOnClick([salesChart, monthlySales]() {
    monthlySales->AddPoint(ChartDataPoint(8, 69000, 0, "Aug"));
    salesChart->SetDataSource(monthlySales);          // re-set so the axes take the new point in
});
```

## Limitations

- **One series per element.** Overlaying two series means two elements or the
  chart engine.
- **Zoom and pan do nothing visible.** `SetEnableZoom` and `SetEnablePan` are
  accepted, and the line, area and scatter charts switch both on in their
  constructors, but nothing rescales or moves. With zoom on, the chart takes
  the mouse wheel for itself, so inside a scrolling container call
  `SetEnableZoom(false)` to let the wheel scroll the container.
- **The plot area is worked out when the data is set.** A chart whose size the
  layout changes later keeps its old plot rectangle until `SetDataSource` is
  called again, so give charts a fixed width and height.
- **The line chart's dots are always 4 px.** `SetPointRadius` reaches the area
  chart's dots, but the line chart draws its dots with a radius of its own.

## See also

- [UltraCanvasLineChartElement.md](UltraCanvasLineChartElement.md),
  [UltraCanvasBarChartElement.md](UltraCanvasBarChartElement.md),
  [UltraCanvasScatterPlotElement.md](UltraCanvasScatterPlotElement.md),
  [UltraCanvasAreaChartElement.md](UltraCanvasAreaChartElement.md) — one page per element
- [UltraCanvasScatterPlot3D.md](UltraCanvasScatterPlot3D.md) — the 3D scatter plot
- [UltraCanvasChartEngine.md](UltraCanvasChartEngine.md) — multi-series charts, scales and legends
- `Apps/DemoApp/UltraCanvasBasicChartsExamples.cpp` — the demo's line, scatter and area pages

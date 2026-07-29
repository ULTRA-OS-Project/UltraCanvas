# UltraCanvasPolarChart Documentation

## Overview

The `UltraCanvasPolarChart` is the general-purpose polar coordinate chart of the UltraCanvas framework. Every observation is an (angle, radius) pair: the angular axis positions the point around the circle, the radial axis positions it away from the centre. One element covers the whole family of round plots that share that coordinate system — polar scatter, polar line and spline, polar area, and polar columns (the Nightingale rose / stacked polar column chart).

It complements the other radial elements rather than replacing them:

| Element | Encodes |
|---------|---------|
| `UltraCanvasPolarChart` | Free (angle, radius) pairs — any series type on a shared polar coordinate system |
| `UltraCanvasRadarChartElement` | One axis per category, each with its own independent min/max range |
| `UltraCanvasRadialBarChart` | Rays whose length is the value, grouped into per-series sectors |
| `UltraCanvasSunburstChart` | Hierarchy depth as rings, value as angular span |
| `UltraCanvasPieChart` | Value as angular span of a single ring |

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Charts/UltraCanvasPolarChart.h`
**Base Class:** `UltraCanvasChartElementBase`
**Version:** 1.0.0
**Last Modified:** 2026-07-29
**Author:** UltraCanvas Framework

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasPolarChart
```

## Features

### Core Capabilities
- **Six series types on one coordinate system:** line, spline, area, spline area, scatter and column — mix them freely in a single chart
- **Numeric or categorical angle axis:** points carry their own angular value, or are distributed evenly one slot per category
- **Full geometry control:** zero-angle direction, clockwise / counter-clockwise winding, sweep angle (fans and wedges from 10° to 360°) and a donut hole via the inner radius fraction
- **Radial scales:** linear, logarithmic and square-root (area-true) with automatic "nice" ticks, manual ranges, negative minima, reversed direction and a configurable tick interval
- **Dual angle axis:** an optional secondary ring of angular labels with its own interval, side, colour and font
- **Grid styles:** circular rings or polygonal spider-web, minor rings between major ticks, and alternating band shading
- **Range bands:** shaded radial rings (tolerance zones) and angular wedges (sectors) drawn behind the data
- **Stacking:** stacked and percent-stacked column and area series; unstacked column series are grouped side by side inside their slot
- **Markers:** circle, square, diamond, triangle, cross and star, sized per series or per point
- **Legend:** four positions with click-to-toggle series visibility
- **Interactive:** hover highlighting, tooltips, click and hover callbacks, and optional drag-to-rotate

## Data Model

```cpp
struct PolarDataPoint {
    double angle;        // Angular value (Numeric mode); unused in Categorical mode
    double radius;       // Value on the radial axis
    std::string label;
    Color color;         // Colors::Transparent = inherit the series color
    float markerSize;    // 0 = inherit the series marker size
};

struct PolarSeries {
    std::string name;
    PolarSeriesType type;
    std::vector<PolarDataPoint> points;

    Color color;             // Colors::Transparent = take from the palette
    Color fillColor;         // Colors::Transparent = color at fillOpacity
    float lineWidth;
    float fillOpacity;
    bool dashed;

    PolarMarkerShape marker;
    float markerSize;

    bool closed;             // Connect the last point back to the first
    bool visible;
    bool showValueLabels;
};
```

In **Categorical** mode a point's angular position comes from its index in the series, so the *n*-th point of every series lands on the *n*-th category. In **Numeric** mode each point's `angle` is mapped through the angular domain (`SetAngleRange`, 0–360 by default) onto the sweep.

## Enumerations

```cpp
enum class PolarSeriesType       { Line, Spline, Area, SplineArea, Scatter, Column };
enum class PolarMarkerShape      { NoMarker, Circle, Square, Diamond, Triangle, Cross, Star };
enum class PolarAngleMode        { Numeric, Categorical };
enum class PolarDirection        { Clockwise, CounterClockwise };
enum class PolarGridShape        { Circular, Polygonal };
enum class PolarRadialScale      { Linear, Logarithmic, SquareRoot };
enum class PolarLabelPlacement   { Outside, Inside, Hidden };
enum class PolarLabelOrientation { Horizontal, Tangential, Radial };
enum class PolarStackMode        { NoStacking, Stacked, PercentStacked };
enum class PolarLegendPosition   { NoLegend, Top, Bottom, Left, Right };
enum class PolarCategoryPlacement{ OnSpokes, BetweenSpokes };
```

> The `NoMarker` / `NoStacking` / `NoLegend` spellings avoid a collision with the `None` macro that `X11/X.h` defines on Linux builds.

## Public API

### Series management
```cpp
void AddSeries(const PolarSeries& series);
void AddSeries(const std::string& name, PolarSeriesType type,
               const std::vector<std::pair<double, double>>& angleRadiusPairs,
               const Color& color = Colors::Transparent);
void AddCategorySeries(const std::string& name, PolarSeriesType type,
                       const std::vector<double>& values,
                       const Color& color = Colors::Transparent);
void ClearSeries();
size_t GetSeriesCount() const;
PolarSeries* GetSeries(size_t index);            // mutable; triggers a relayout
void SetSeriesColor(size_t index, const Color& color);
void SetSeriesType(size_t index, PolarSeriesType type);
void SetSeriesVisible(size_t index, bool visible);
bool IsSeriesVisible(size_t index) const;
```

### Angular axis
```cpp
void SetAngleMode(PolarAngleMode mode);
void SetCategories(const std::vector<std::string>& categories);  // implies Categorical
void SetCategoryPlacement(PolarCategoryPlacement placement);
void SetAngleRange(double minAngle, double maxAngle);            // default 0..360
void SetZeroAngle(float degrees);                                // -90 = 12 o'clock
void SetDirection(PolarDirection direction);
void SetSweepAngle(float degrees);                               // 10..360
void SetAngleAxisStyle(const PolarAngleAxisStyle& style);
void SetShowAngleLabels(bool show);
void SetAngleTickInterval(float degrees);                        // 0 = automatic
void SetAngleLabelOrientation(PolarLabelOrientation orientation);
void SetAngleLabelPlacement(PolarLabelPlacement placement);
void SetAngleLabelFormatter(std::function<std::string(double)> formatter);
void SetSecondaryAngleAxisEnabled(bool enabled);
void SetSecondaryAngleAxisStyle(const PolarAngleAxisStyle& style);
```

`PolarCategoryPlacement::OnSpokes` puts each category on a grid spoke — the radar-style layout used when a line or area outline should have a vertex on every axis. `BetweenSpokes` shifts categories to the middle of their slot so the grid spokes fall on slot boundaries, which is what column charts want.

### Radial axis
```cpp
void SetRadialScale(PolarRadialScale scale);
void SetRadialRange(double minValue, double maxValue);   // manual override
void ClearRadialRange();                                  // back to automatic
double GetRadialMin() const;
double GetRadialMax() const;
void SetRadialTickCount(int count);                       // target count, default 5
void SetRadialTickInterval(double interval);              // 0 = automatic
void SetRadialAxisIncludesZero(bool include);
void SetRadialAxisReversed(bool reversed);                // values grow inward
void SetInnerRadiusFraction(float fraction);              // 0..0.9 donut hole
void SetShowRadialLabels(bool show);
void SetRadialLabelAngle(double angle);                   // spoke carrying the labels
void SetRadialLabelFormatter(std::function<std::string(double)> formatter);
void SetRadialLabelUnit(const std::string& unit);         // suffix, e.g. " ppm"
void SetRadialAxisLineVisible(bool visible);
void SetRadialLabelColor(const Color& color);
```

The automatic range snaps outward to round tick values. A manual range is respected exactly — ticks are then placed at the interval inside it.

### Grid, background and bands
```cpp
void SetGridShape(PolarGridShape shape);
void SetShowRadialGrid(bool show);        // concentric rings
void SetShowAngularGrid(bool show);       // spokes
void SetGridLineWidth(float width);
void SetMinorRadialGridCount(int count);  // extra rings between major ticks
void SetAlternatingBands(bool enabled, const Color& colorA, const Color& colorB);
void SetPlotBackgroundColor(const Color& color);

void AddRadialBand(double from, double to, const Color& color, const std::string& label = "");
void ClearRadialBands();
void AddAngularBand(double fromAngle, double toAngle, const Color& color, const std::string& label = "");
void ClearAngularBands();
```

Radial bands are clipped to the axis range, so a band declared beyond the data never bleeds outside the plot.

### Series layout and styling
```cpp
void SetStackMode(PolarStackMode mode);
void SetColumnWidthFraction(float fraction);   // 0.05..1.0 of the angular slot
void SetColumnBorder(const Color& color, float width);
void SetSortPointsByAngle(bool sort);          // numeric mode only
void SetSplineTension(float tension);          // 0..1, default 0.5
void SetColorPalette(const std::vector<Color>& palette);
void SetSubtitle(const std::string& subtitle);
void SetTitleColor(const Color& color);
void SetLabelFont(const std::string& family, float size);
```

Point order is preserved by default, which is what spirals and parametric curves need. `SetSortPointsByAngle(true)` sorts a numeric series by angle before it is drawn.

### Legend and interaction
```cpp
void SetLegendPosition(PolarLegendPosition position);
void SetLegendFont(const std::string& family, float size);
void SetLegendTextColor(const Color& color);
void SetLegendToggleEnabled(bool enabled);

void SetHoverHighlightEnabled(bool enabled);
void SetTooltipsEnabled(bool enabled);
void SetPolarTooltipFormatter(
    std::function<std::string(const PolarSeries&, const PolarDataPoint&)> formatter);
void SetRotateOnDrag(bool enabled);

std::function<void(size_t, size_t)> onPointClick;   // (seriesIndex, pointIndex)
std::function<void(size_t, size_t)> onPointHover;
std::function<void(size_t, bool)>   onSeriesVisibilityChanged;
```

### Coordinate helpers
```cpp
Point2Dd PolarToScreen(double dataAngle, double radiusValue) const;
bool ScreenToPolar(const Point2Dd& pos, double& dataAngle, double& radiusValue) const;
```

Both work in element-local pixels and are valid once the chart has been laid out (that is, from the first render onward). `ScreenToPolar` returns `false` for positions outside the plotted radius band.

## Factory Functions

```cpp
std::shared_ptr<UltraCanvasPolarChart> CreatePolarChart(
    const std::string& id, int x, int y, int w, int h);

std::shared_ptr<UltraCanvasPolarChart> CreatePolarScatterChart(
    const std::string& id, int x, int y, int w, int h,
    const std::vector<std::pair<std::string,
                                std::vector<std::pair<double, double>>>>& seriesData);

std::shared_ptr<UltraCanvasPolarChart> CreatePolarRoseChart(
    const std::string& id, int x, int y, int w, int h,
    const std::vector<std::string>& categories,
    const std::vector<std::pair<std::string, std::vector<double>>>& seriesData,
    PolarStackMode stackMode = PolarStackMode::Stacked);
```

## Usage Examples

### Polar scatter with a numeric angle axis

```cpp
auto chart = CreatePolarChart("signals", 20, 20, 460, 380);
chart->SetAngleMode(PolarAngleMode::Numeric);
chart->SetAngleTickInterval(30.0f);
chart->SetRadialRange(0.0, 16.0);
chart->SetRadialTickInterval(2.0);
chart->SetLegendPosition(PolarLegendPosition::Top);

PolarSeries series("Signal A", PolarSeriesType::Scatter, Color(102, 178, 235, 255));
series.marker = PolarMarkerShape::Diamond;
series.markerSize = 7.0f;
series.lineWidth = 0.0f;
series.closed = false;
series.points.emplace_back(35.0, 12.4);      // 35 degrees, radius 12.4
series.points.emplace_back(148.0, 6.1);
chart->AddSeries(series);
```

### Stacked polar columns (Nightingale rose)

```cpp
auto chart = CreatePolarRoseChart("profit", 20, 20, 460, 380,
    {"Powder", "Nail polish", "Eyebrow pencil", "Rouge", "Lipstick"},
    {
        {"2025", {33, 18, 14, 10, 21}},
        {"2026", {41, 22, 17, 12, 26}}
    },
    PolarStackMode::Stacked);

chart->SetChartTitle("Company Profit Dynamic in Regions by Year");
chart->SetAngleLabelOrientation(PolarLabelOrientation::Radial);
```

`CreatePolarRoseChart` selects `BetweenSpokes` placement and a full-width column so the sectors meet edge to edge. Use `SetColumnWidthFraction` for gaps between them, or `SetRadialScale(PolarRadialScale::SquareRoot)` to make sector *area* — rather than radius — proportional to the value.

### Tolerance bands behind a categorical line

```cpp
auto chart = CreatePolarChart("concentration", 20, 20, 460, 380);
chart->SetCategories({"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"});
chart->SetRadialRange(0.0, 100.0);
chart->SetRadialTickInterval(20.0);
chart->SetRadialLabelUnit(" ppm");

chart->AddRadialBand(0.0,  20.0,  Color(150, 160, 235, 150), "Under-Absorption");
chart->AddRadialBand(20.0, 60.0,  Color(150, 230, 160, 150), "Normal");
chart->AddRadialBand(60.0, 100.0, Color(240, 150, 150, 150), "Over-Absorption");

chart->AddCategorySeries("Concentration", PolarSeriesType::Line,
                         {58, 21, 30, 44, 28, 64, 72, 34, 26, 88, 40, 33},
                         Color(20, 40, 160, 255));
```

### A 270° fan with a donut hole

```cpp
auto chart = CreatePolarChart("fan", 20, 20, 460, 380);
chart->SetSweepAngle(270.0f);
chart->SetZeroAngle(-225.0f);
chart->SetInnerRadiusFraction(0.25f);
chart->SetAngleRange(0.0, 24.0);           // 24 hours across the fan
chart->SetAngleLabelFormatter([](double h) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02.0f:00", h);
    return std::string(buf);
});
```

### Dual angle axis

```cpp
chart->SetSecondaryAngleAxisEnabled(true);

PolarAngleAxisStyle inner;
inner.placement = PolarLabelPlacement::Inside;
inner.tickIntervalDeg = 15.0f;
inner.labelColor = Color(200, 60, 60, 255);
inner.fontSize = 9.0f;
chart->SetSecondaryAngleAxisStyle(inner);
```

## Rendering Order

Understanding the paint order helps when layering bands and translucent fills:

1. Element background
2. Plot disc background, then alternating ring shading
3. Radial bands, then angular bands
4. Grid rings, then grid spokes
5. Column sectors
6. Area fills (in series order), then line and spline outlines (in series order)
7. Markers, then value labels
8. Angular axis (primary, then secondary), then radial tick labels
9. Legend, then title and subtitle

Fills are drawn before outlines across all series, so a translucent area never hides another series' line.

## Interaction Details

- **Hit testing** checks column sectors first (they cover the larger area), then falls back to the nearest marker within 12 px.
- **Tooltips** show the series name, the category or angle, and the value; `SetPolarTooltipFormatter` replaces that text entirely.
- **Legend clicks** toggle series visibility when `SetLegendToggleEnabled(true)` (the default), which invalidates the layout so an automatic radial range rescales to the remaining series.
- **Drag-to-rotate** is off by default. When enabled, horizontal dragging changes the zero angle at 0.5° per pixel; a click on a data point or legend entry still takes precedence.

## Notes and Limitations

- Stacking applies in **Categorical** mode only — stacked semantics are undefined for arbitrary numeric angles. Numeric column series are always grouped side by side.
- Logarithmic radial scales force a strictly positive lower bound, derived from the smallest positive value in the data when no manual range is set.
- Splines are interpolated in screen space with a Catmull-Rom basis; with very few widely spaced points the curve may bulge past its control points near the centre. Use `SetSplineTension(0.0f)` for a tighter fit, or the `Line` type.
- `closed = true` on a series with a partial sweep draws a chord across the gap. Set `closed = false` for fans and wedges.
- The chart owns its data. `SetDataSource()` from the base class is not used, and `Render()` is overridden so the chart draws without an `IChartDataSource` attached.

## Demo

The demo application registers the element under **Charts > Polar Chart** with six examples — polar scatter, dual angle axis curves, overlapping polar areas, categorical spline area, radial tolerance bands and stacked polar columns — plus a live control panel for grid shape, label orientation, radial scale, zero angle, inner radius, sweep, banding, drag-to-rotate and hover highlighting.

**Source:** `Apps/DemoApp/UltraCanvasPolarChartExamples.cpp`

# UltraCanvasRadarChartElement Documentation

## Overview

The `UltraCanvasRadarChartElement` is a multi-axis radar (also called *spider* or *star*) chart visualization component in the UltraCanvas framework. It extends `UltraCanvasChartElementBase` to plot several quantitative axes on a shared circular grid, drawing each data series as a filled polygon.

Radar charts are ideal for comparing multiple entities across the same set of metrics — performance scorecards, skills assessments, product/feature comparisons, survey results, and any "many dimensions at a glance" visualization.

Unlike the Cartesian charts (line, bar, scatter, area), the radar chart manages its **own** data model — a list of axes and a list of series — rather than the shared `IChartDataSource`. It therefore overrides `Render()` to draw without a data source.

## Class Definition

```cpp
namespace UltraCanvas {
    class UltraCanvasRadarChartElement : public UltraCanvasChartElementBase
}
```

### Header Information
- **File**: `UltraCanvas/include/Plugins/Charts/UltraCanvasRadarChartElement.h`
- **Implementation**: `UltraCanvas/Plugins/Charts/UltraCanvasRadarChartElement.cpp`
- **Version**: 2.0.0
- **Last Modified**: 2026-06-19
- **Author**: UltraCanvas Framework

## Constructor

```cpp
UltraCanvasRadarChartElement(const std::string& id,
                             int x, int y, int width, int height)
```

### Parameters
- `id`: Unique string identifier for the element
- `x`: X coordinate position
- `y`: Y coordinate position
- `width`: Width of the chart area
- `height`: Height of the chart area

### Default Settings
- Grid visible, light grey (`200, 200, 200`), 1px lines, 5 concentric levels
- Axis labels visible, Arial 12px, dark grey (`64, 64, 64`)
- Value labels hidden
- Legend visible (auto-positioned to the right of the plot)
- Start angle `-90°` (first axis points straight up), clockwise rotation
- Grow-out animation enabled, 1000 ms, smoothstep easing
- Zoom and pan disabled (not meaningful for radar charts); selection enabled

## Data Model

A radar chart is described by two collections.

### RadarChartAxis

```cpp
struct RadarChartAxis {
    std::string name;        // Axis label
    double minValue;         // Value mapped to the centre
    double maxValue;         // Value mapped to the outer ring
    Color  axisColor;        // Spoke / grid tint for this axis
    bool   showGrid;         // Include this axis in grid-level calculation
    int    gridLevels;       // Number of concentric rings contributed
};
```

### RadarChartSeries

```cpp
struct RadarChartSeries {
    std::string         name;          // Series name (shown in the legend)
    std::vector<double> values;        // One value per axis
    Color  fillColor;                  // Polygon fill colour
    Color  strokeColor;                // Polygon outline / point colour
    float  strokeWidth;                // Outline width (default 2.0)
    float  fillOpacity;                // Extra fill multiplier 0..1 (default 0.3)
    bool   showPoints;                 // Draw vertex markers (default true)
    float  pointRadius;                // Marker radius (default 4.0)
};
```

Each series must provide one value per axis. When a series is added, its value
vector is automatically padded with zeros or truncated to match the current
axis count.

## Axis Management

### AddAxis
```cpp
void AddAxis(const std::string& name, double minValue = 0.0, double maxValue = 100.0)
```
Appends a new axis. Add all axes **before** adding series so the series values
line up correctly.

**Example:**
```cpp
radar->AddAxis("Speed", 0, 100);
radar->AddAxis("Quality", 0, 100);
```

### SetAxisProperties
```cpp
void SetAxisProperties(size_t axisIndex, const Color& color,
                       bool showGridLines = true, int gridLevels = 5)
```
Customizes the per-axis tint and how many grid rings it contributes.

### SetAxisRange
```cpp
void SetAxisRange(size_t axisIndex, double minValue, double maxValue)
```
Changes the value range mapped from the centre to the outer ring of one axis.

### GetAxisCount
```cpp
size_t GetAxisCount() const
```
Returns the number of axes currently defined.

## Series Management

### AddSeries
```cpp
void AddSeries(const std::string& name, const std::vector<double>& values,
               const Color& fillColor   = Color(0, 102, 204, 80),
               const Color& strokeColor = Color(0, 102, 204, 255))
```
Adds a data series (a filled polygon). Restarts the grow-out animation.

**Example:**
```cpp
radar->AddSeries("Current", {85, 92, 78, 65, 88, 72},
                 Color(70, 130, 180, 100), Color(70, 130, 180, 255));
```

### SetSeriesProperties
```cpp
void SetSeriesProperties(size_t seriesIndex, const Color& fillColor,
                         const Color& strokeColor,
                         float strokeWidth = 2.0f, float fillOpacity = 0.3f)
```
Customizes the fill, outline colour, outline width and fill opacity of a series.

### SetSeriesPointDisplay
```cpp
void SetSeriesPointDisplay(size_t seriesIndex, bool showPoints, float pointRadius = 4.0f)
```
Toggles the vertex markers for a series and sets their radius.

### ClearSeries
```cpp
void ClearSeries()
```
Removes all series (axes are kept). Useful for live data replacement.

### GetSeriesCount
```cpp
size_t GetSeriesCount() const
```
Returns the number of series currently defined.

## Visual Configuration

### SetGridDisplay
```cpp
void SetGridDisplay(bool show, const Color& color = Color(200, 200, 200, 255),
                    float lineWidth = 1.0f)
```
Shows/hides the concentric rings and radial spokes, and sets their colour/width.

### SetAxisLabels
```cpp
void SetAxisLabels(bool show, const Color& color = Color(64, 64, 64, 255),
                   const std::string& font = "Arial", float fontSize = 12.0f)
```
Controls the text labels drawn just outside each axis.

### SetValueLabels
```cpp
void SetValueLabels(bool show, const Color& color = Color(64, 64, 64, 255),
                    float fontSize = 10.0f)
```
Shows the numeric value at each vertex of the **first** series (kept to one
series to avoid clutter).

### SetRadarOrientation
```cpp
void SetRadarOrientation(float startAngleInDegrees, bool clockwise = true)
```
Sets the angle of the first axis (in degrees; `-90` is straight up) and the
rotation direction.

### SetLegendDisplay
```cpp
void SetLegendDisplay(bool show, const Point2Df& position = Point2Df(0, 0),
                      const Color& backgroundColor = Color(255, 255, 255, 200),
                      const Color& textColor       = Color(64, 64, 64, 255))
```
Shows/hides the legend. With the default `(0, 0)` position the legend is
auto-placed to the right of the plot; pass an explicit element-local position
to override.

### Accessors
```cpp
bool GetShowGrid() const;
bool GetShowAxisLabels() const;
bool GetShowLegend() const;
bool GetAnimationEnabled() const;
```

## Animation

### SetAnimationEnabled
```cpp
void SetAnimationEnabled(bool enabled, int durationMs = 1000)
```
Enables/disables the grow-out animation and sets its duration. When enabled the
polygons animate from the centre outward using a smoothstep easing curve.

### StartAnimation
```cpp
void StartAnimation()
```
Restarts the animation from the centre. Called automatically by `AddSeries`.

## Core Methods

### Render
```cpp
void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override
```
Overridden so the radar chart paints from its own axes/series model rather than
the base-class `IChartDataSource`. Draws the background, then delegates to
`RenderChart`, then selection indicators.

### RenderChart
```cpp
void RenderChart(IRenderContext* ctx) override
```
Draws, in order: animation update → layout → grid → axis labels → series
polygons (fill, outline, points) → value labels → legend.

### HandleChartMouseMove
```cpp
bool HandleChartMouseMove(const Point2Di& mousePos) override
```
Hit-tests the nearest series vertex (within 15px) and shows a tooltip with the
series name, axis name and value. Returns `true` when handled.

## Factory Function

```cpp
inline std::shared_ptr<UltraCanvasRadarChartElement> CreateRadarChartElement(
    const std::string& id, int x, int y, int width, int height)
```
Convenience factory to create a radar chart element instance.

## Usage Example

```cpp
// Create the radar chart
auto radar = CreateRadarChartElement("performanceRadar", 240, 160, 460, 290);

// Visual configuration
radar->SetGridDisplay(true, Color(220, 220, 220, 255), 1.0f);
radar->SetAxisLabels(true, Color(60, 60, 60, 255), "Arial", 11.0f);
radar->SetLegendDisplay(true);
radar->SetAnimationEnabled(true, 1200);

// 1) Define the axes (do this before adding series)
radar->AddAxis("Speed", 0, 100);
radar->AddAxis("Quality", 0, 100);
radar->AddAxis("Efficiency", 0, 100);
radar->AddAxis("Innovation", 0, 100);
radar->AddAxis("Collaboration", 0, 100);
radar->AddAxis("Leadership", 0, 100);

// 2) Add one or more series (one value per axis)
radar->AddSeries("Current", {85, 92, 78, 65, 88, 72},
                 Color(70, 130, 180, 100), Color(70, 130, 180, 255));
radar->AddSeries("Target",  {95, 95, 90, 85, 90, 85},
                 Color(255, 140, 0, 80),  Color(255, 140, 0, 255));

// 3) Fine-tune series appearance
radar->SetSeriesProperties(0, Color(70, 130, 180, 100), Color(70, 130, 180, 255), 2.5f, 0.3f);
radar->SetSeriesProperties(1, Color(255, 140, 0, 80),  Color(255, 140, 0, 255), 2.0f, 0.2f);

// Add to a container
container->AddChild(radar);
```

### Live Data Replacement

```cpp
radar->ClearSeries();
radar->AddSeries("New Snapshot", newValues,
                 Color(255, 69, 0, 100), Color(255, 69, 0, 255));
```

## Coordinate System

The chart renders in **element-local** coordinates (origin at the element's
top-left, obtained via `GetLocalBounds()`), consistent with the other chart
elements. Internally:

1. `NormalizeValue` maps a value to `[0..1]` within its axis range.
2. `ValueToPolarCoordinate` converts an axis index + value into `(angle, radius)`,
   applying the current animation progress.
3. `PolarToScreen` converts `(angle, radius)` into element-local screen
   coordinates around `centerPoint`.

## Inherited Features from UltraCanvasChartElementBase

- **Tooltips** (`SetEnableTooltips`) — per-vertex value tooltips on hover
- **Background / styling** — `SetBackgroundColor`, `showBackground`
- **Selection** — enabled by default
- **Title** — `SetChartTitle` / `SetTitle`

> Note: zoom and pan are intentionally disabled for radar charts, and the
> Cartesian grid/axes of the base class are not used (the radar draws its own
> circular grid).

## Rendering Pipeline

1. **Background** — fills the element background colour
2. **Animation update** — advances the smoothstep grow-out progress
3. **Layout** — computes `centerPoint` and `maxRadius` from the local bounds,
   reserving margin for labels and the legend
4. **Grid** — concentric rings + radial spokes (one per axis)
5. **Axis labels** — positioned just outside each spoke with angle-aware alignment
6. **Series** — filled polygon, closed outline, optional vertex markers
7. **Value labels** (optional) — numeric values on the first series
8. **Legend** (optional) — colour swatch + series name per row

## Best Practices

1. **Add axes first**, then series — series values are matched against the axis count.
2. **Use at least 3 axes** so the series form a meaningful polygon.
3. **Use semi-transparent fills** so overlapping series remain readable.
4. **Keep value labels to small charts** — they are drawn for the first series only.
5. **Share a common range** across axes where possible for fair visual comparison;
   use `SetAxisRange` when individual axes need different scales.

## Limitations

- A series must have one value per axis (auto padded/truncated otherwise).
- Value labels are rendered for the first series only, to avoid clutter.
- Zoom and pan are not supported.
- All axes are spaced at equal angles around the circle.

## See Also

- [`UltraCanvasChartElementBase`](UltraCanvasChartElementBase.md) - Base class documentation
- [`UltraCanvasLineChartElement`](UltraCanvasLineChartElement.md) - Line chart component
- [`UltraCanvasBarChartElement`](UltraCanvasBarChartElement.md) - Bar chart component
- [`UltraCanvasPieChartExamples`](UltraCanvasPieChartExamples.md) - Pie chart component
- [`IRenderContext`](IRenderContext.md) - Rendering interface

# UltraCanvasScatterPlotElement Documentation

## Overview

The `UltraCanvasScatterPlotElement` is a specialized chart control in the UltraCanvas Framework designed for visualizing relationships between two continuous variables. It excels at displaying correlations, identifying clusters, and detecting outliers in datasets through customizable point-based data representation.

## Class Declaration

```cpp
namespace UltraCanvas {
    class UltraCanvasScatterPlotElement : public UltraCanvasChartElementBase
}
```

**Header File:** `UltraCanvas/include/Plugins/Charts/UltraCanvasSpecificChartElements.h`  
**Implementation:** `UltraCanvas/Plugins/Charts/UltraCanvasSpecificChartElements.cpp`  
**Version:** 1.1.2  
**Last Modified:** 2026-10-07  

> For three-dimensional (x, y, z) point clouds see the companion element
> [`UltraCanvasScatterPlot3DElement`](UltraCanvasScatterPlot3D.md).

## Features

### Core Capabilities
- **Multiple Point Shapes:** Circle, Square, Triangle, Diamond
- **Correlation / Trend Line:** Least-squares fit with r and r² readout
- **Per-Point Colors:** Points may override the element color (outliers, series)
- **Interactive Tooltips:** The x and y values of the point under the pointer
  (or text of your own)
- **Zoom & Pan:** Wheel zoom and drag pan of the x axis, off until enabled
- **Hover Highlight:** A ring marks the point whose tooltip is showing
- **Customizable Appearance:** Configurable colors, sizes, and styles

## Constructor

```cpp
UltraCanvasScatterPlotElement(const std::string& id, int x, int y, int width, int height)
```

### Parameters
- `id` - Unique identifier string for the chart element
- `x` - X position of the chart
- `y` - Y position of the chart
- `width` - Width of the chart
- `height` - Height of the chart

There is no numeric id argument.

### Default Settings
- `enableZoom` = false
- `enablePan` = false
- `enableTooltips` = true
- `enableSelection` = true (the hover ring; the base class default is false)
- `pointShape` = PointShape::Circle
- `pointColor` = Color(0, 102, 204, 255) // Blue
- `pointSize` = 6.0 // a radius: circles are 12 px across
- `showTrendLine` = false

## Enumerations

### PointShape
Defines the visual representation of data points.

```cpp
enum class PointShape {
    Circle,    // Circular points (default)
    Square,    // Square points
    Triangle,  // Triangular points pointing up
    Diamond    // Diamond-shaped points
};
```

### TrendLineStyle
Stroke style of the correlation/trend line.

```cpp
enum class TrendLineStyle {
    Solid,
    Dashed,    // default
    Dotted
};
```

Both are nested in the class: `UltraCanvasScatterPlotElement::PointShape`,
`UltraCanvasScatterPlotElement::TrendLineStyle`.

## Configuration Methods

### Point Appearance

#### SetPointColor
```cpp
void SetPointColor(const Color& color)
```
Sets the fill color for all data points. A point whose own `color` is not
transparent keeps that color (see *Per-Point Colors*). Points are filled
only; they have no outline.

**Parameters:**
- `color` - Color object defining the point color

**Example:**
```cpp
scatterPlot->SetPointColor(Color(255, 140, 0, 255)); // Dark orange
```

#### SetPointSize
```cpp
void SetPointSize(double size)
```
Sets the size of the points in pixels, measured from the point's centre: the
circle's radius, half the side of the square, and the distance from the
centre to the tips of the triangle and the diamond. 6 by default, so a
default circle is 12 px across. The tooltip picks a point within this size
plus 5 px.

**Parameters:**
- `size` - Point radius / half-size in pixels

**Example:**
```cpp
scatterPlot->SetPointSize(8.0);
```

#### SetPointShape
```cpp
void SetPointShape(PointShape shape)
```
Changes the shape used to render data points.

**Parameters:**
- `shape` - One of the PointShape enum values

**Example:**
```cpp
scatterPlot->SetPointShape(UltraCanvasScatterPlotElement::PointShape::Diamond);
```

The base class's `SetPointRadius` and `SetShowValueLabels` do not apply to
the scatter plot: it sizes its points with `SetPointSize` and draws no value
labels.

### Correlation / Trend Line

The scatter plot can fit and draw a least-squares regression line over the
current data source, together with an optional readout of the fitted equation
and the Pearson correlation coefficient. The fit is worked out on every paint
over all the points - also while the x axis is zoomed - and the line spans
the visible x range, under the points and clipped to the plot area. With
categorical x positioning (`XAxisLabelMode::DataLabel`) the x axis carries no
metric, so the line is not drawn in that mode.

#### SetShowTrendLine
```cpp
void SetShowTrendLine(bool show);
bool GetShowTrendLine() const;
```
Enables/disables the correlation line (off by default).

**Example:**
```cpp
scatterPlot->SetShowTrendLine(true);
```

#### SetTrendLineColor / SetTrendLineWidth / SetTrendLineStyle
```cpp
void SetTrendLineColor(const Color& color);    // default: Color(220, 60, 60, 255)
void SetTrendLineWidth(float width);           // default: 2.0f, at least 0.1f
void SetTrendLineStyle(TrendLineStyle style);  // default: Dashed
```
Styling of the fitted line.

#### SetShowCorrelationInfo
```cpp
void SetShowCorrelationInfo(bool show);                // default: false
void SetCorrelationInfoColor(const Color& color);      // default: Color(80, 80, 80, 255)
void SetCorrelationInfoFontSize(float size);           // default: 11.0f, at least 1.0f
```
When enabled (and the trend line is drawn), draws `y = ax + b` and
`r = …   r² = …` in the top-right corner of the plot area.

#### ComputeLinearRegression
```cpp
bool ComputeLinearRegression(double& slope, double& intercept) const
```
Computes the least-squares fit over the current data source. Returns `false`
(and sets both to 0) when there is no data source, fewer than 2 points, or the
x values have no variance.

#### GetCorrelationCoefficient
```cpp
double GetCorrelationCoefficient() const
```
Returns the Pearson correlation coefficient *r* of the current data
(`0.0` when undefined).

**Example:**
```cpp
scatterPlot->SetShowTrendLine(true);
scatterPlot->SetTrendLineStyle(UltraCanvasScatterPlotElement::TrendLineStyle::Dashed);
scatterPlot->SetShowCorrelationInfo(true);

double r = scatterPlot->GetCorrelationCoefficient();
if (std::fabs(r) > 0.8) {
    // strong linear relationship
}
```

### Per-Point Colors

A `ChartDataPoint` whose `color` member is not transparent (alpha above 0)
overrides the element point color for that point — useful for marking
outliers or encoding a series/category:

```cpp
ChartDataPoint p(x, y, 0, "Outlier 12");
p.color = Color(220, 60, 60, 255);   // rendered red regardless of SetPointColor
data->AddPoint(p);
```

### Data Management

#### SetDataSource
```cpp
void SetDataSource(std::shared_ptr<IChartDataSource> data)
```
Inherited from `UltraCanvasChartElementBase`. Sets the data source for the
scatter plot. A new source is shown whole: any zoom is reset. The axes run
from the smallest to the largest x and y of the data, with 5 % added on each
side.

**Example:**
```cpp
auto correlationData = std::make_shared<ChartDataVector>();
// Populate data...
scatterPlot->SetDataSource(correlationData);
```

`ChartDataVector::LoadFromCSV` reads lines of `x,y[,z[,label]]`; for large
files `ChartDataStream` reads the CSV in chunks instead of holding it all.

### Chart Properties

#### SetChartTitle
```cpp
void SetChartTitle(const std::string& title)
```
Sets the title displayed above the chart.

**Example:**
```cpp
scatterPlot->SetChartTitle("Marketing Spend vs Sales");
```

### Interactive Features

#### SetEnableTooltips
```cpp
void SetEnableTooltips(bool enable)
```
On by default. Hovering within the point size plus 5 px of a point shows its
tooltip: the series name (`SetSeriesName`) when set, then `X:` and `Y:` - in
`XAxisLabelMode::DataLabel` the `X:` line shows the point's label.
`SetCustomTooltipGenerator` replaces the whole text. Turning tooltips off
hides one that is showing, and also the hover ring.

**Example:**
```cpp
scatterPlot->SetCustomTooltipGenerator([](const ChartDataPoint& point, size_t index) {
    return point.label + "\nSpend: " + std::to_string(static_cast<int>(point.x)) +
           "\nSales: " + std::to_string(static_cast<int>(point.y));
});
```

#### SetEnableZoom
```cpp
void SetEnableZoom(bool enable)
```
Wheel zoom of the x axis around the pointer: wheel up zooms in, wheel down
zooms out, by 1.25x a notch, up to 50x. Off by default. The y axis keeps the
range of the whole data. A wheel turn that changes nothing (over the margins,
or zooming out of the whole range) goes to the parent, so a scrolling
container around the chart still scrolls. Turning it off shows the whole range
again; `ResetZoom()` does that too, and `IsZoomed()` says whether the view is
narrowed. Works in both x-axis label modes: with `XAxisLabelMode::DataLabel`
the evenly spaced points spread apart.

#### SetEnablePan
```cpp
void SetEnablePan(bool enable)
```
Dragging sideways with the left button moves a zoomed-in x axis. Off by
default; it does nothing while the whole range is shown.

#### ResetZoom / IsZoomed
```cpp
void ResetZoom();
bool IsZoomed() const;
```
`ResetZoom` shows the whole x range again; `IsZoomed` is true while the view
is narrowed.

#### SetEnableSelection
```cpp
void SetEnableSelection(bool enable)
```
Turns the hover ring on or off: a red ring (8 px radius) around the point
whose tooltip is showing. On by default for the scatter plot. It follows the
pointer, so it needs tooltips on; there is no click selection and no
selected-point API.

`GetEnableTooltips()`, `GetEnableZoom()`, `GetEnablePan()` and
`GetEnableSelection()` read the four settings back.

## Factory Function

```cpp
std::shared_ptr<UltraCanvasScatterPlotElement> CreateScatterPlotElement(
    const std::string& id, int x, int y, int width, int height)
```

Convenience factory function for creating scatter plot instances.

**Example:**
```cpp
auto scatterPlot = CreateScatterPlotElement("correlationScatter", 50, 100, 600, 400);
```

The generic `CreateChartElementWithData` from the chart base also sets the
data source and an optional title in one call:

```cpp
auto scatterPlot = CreateChartElementWithData<UltraCanvasScatterPlotElement>(
    "correlationScatter", 50, 100, 600, 400, correlationData, "Marketing Spend vs Sales");
```

## Rendering Implementation

### RenderChart Method
The core rendering method draws points based on their shape setting:

```cpp
virtual void RenderChart(IRenderContext* ctx) override
```

**Rendering Process:**
1. The base class draws the background, plot area, grid, axes and title
2. While zoomed, drawing is clipped to the plot's columns
3. The trend line, when on, is drawn first, so the points sit on top of it
4. Each point is placed at its numeric x (or, in `XAxisLabelMode::DataLabel`,
   evenly spaced by index) and filled with its own color or the point color
5. The base class draws the hover ring when selection is on

### Point Shape Rendering

With `s` the point size (`SetPointSize`), each shape is centred on the point:

| Shape | Drawn as |
|---|---|
| Circle | a circle of radius `s` |
| Square | a square `2s` wide |
| Triangle | apex `s` above the point, base `s` below it, `2s` wide |
| Diamond | corners `s` above, right of, below and left of the point |

## Mouse Interaction

### HandleChartMouseMove
```cpp
virtual bool HandleChartMouseMove(const Point2Di& mousePos) override
```

Handles mouse movement for tooltip display and the hover ring.

**Functionality:**
- Does nothing (returns false) when there is no data source or tooltips are off
- Finds the nearest point in view within `pointSize + 5` pixels
- Shows its tooltip and returns true
- Hides a tooltip that is showing when no point is that close

## Usage Example

### Basic Setup
```cpp
// Create scatter plot
auto scatterPlot = CreateScatterPlotElement("salesCorrelation", 50, 50, 800, 600);

// Generate sample correlation data
auto data = std::make_shared<ChartDataVector>();
std::vector<ChartDataPoint> points;

// Create data points with correlation
for (int i = 0; i < 50; ++i) {
    double x = 1000 + (i * 200);  // Marketing spend
    double y = x * 3.2 + 15000 + (rand() % 10000 - 5000); // Sales with noise
    points.emplace_back(x, y, 0, 
                       "Point " + std::to_string(i + 1), y);
}
data->LoadFromArray(points);

// Configure the scatter plot
scatterPlot->SetDataSource(data);
scatterPlot->SetChartTitle("Marketing Spend vs Sales Revenue");
scatterPlot->SetPointColor(Color(255, 140, 0, 255));  // Orange
scatterPlot->SetPointSize(10.0);
scatterPlot->SetPointShape(UltraCanvasScatterPlotElement::PointShape::Circle);

// Interactive features (tooltips and the hover ring are on already)
scatterPlot->SetEnableTooltips(true);
scatterPlot->SetEnableZoom(true);
scatterPlot->SetEnablePan(true);
scatterPlot->SetEnableSelection(true);

// Add to container
container->AddChild(scatterPlot);
```

### Dynamic Shape Cycling
```cpp
// Create button to cycle through shapes
auto btnCycleShapes = std::make_shared<UltraCanvasButton>(
    "btnCycleShapes", 50, 520, 180, 35);
btnCycleShapes->SetText("Cycle Scatter Shapes");

// Define shape array
std::vector<UltraCanvasScatterPlotElement::PointShape> shapes = {
    UltraCanvasScatterPlotElement::PointShape::Circle,
    UltraCanvasScatterPlotElement::PointShape::Square,
    UltraCanvasScatterPlotElement::PointShape::Triangle,
    UltraCanvasScatterPlotElement::PointShape::Diamond
};

// Set click handler
static int currentShape = 0;
btnCycleShapes->SetOnClick([scatterPlot, shapes]() {
    currentShape = (currentShape + 1) % shapes.size();
    scatterPlot->SetPointShape(shapes[currentShape]);
});
```

### Advanced Customization
```cpp
// Helper function for custom scatter plot creation
std::shared_ptr<UltraCanvasScatterPlotElement> CreateCustomScatterPlot(
    const std::string& id, int x, int y, int width, int height,
    const Color& pointColor, double pointSize,
    UltraCanvasScatterPlotElement::PointShape shape) {
    
    auto chart = CreateScatterPlotElement(id, x, y, width, height);
    chart->SetPointColor(pointColor);
    chart->SetPointSize(pointSize);
    chart->SetPointShape(shape);
    chart->SetEnableZoom(true);
    chart->SetEnablePan(true);
    chart->SetEnableSelection(true);
    
    return chart;
}
```

## Best Practices

### Performance Optimization
1. **Large Datasets**: For datasets with >1000 points, consider:
   - Reducing point size for better visibility
   - Implementing data aggregation or sampling
   - Keeping in mind that every mouse move over the chart measures the
     distance to each point to find the tooltip's

2. **Memory Management**: Use shared_ptr for data sources to ensure proper cleanup

3. **Visual Clarity**: 
   - Choose contrasting colors for points vs background
   - Adjust point size based on data density
   - Use per-point colors to distinguish groups (one shape applies to all points)

### Data Preparation
Each point is a `ChartDataPoint`:

```cpp
struct ChartDataPoint {
    double x, y, z;          // x and y are plotted; z is not used by the 2D plot
    std::string label;       // x-axis label and tooltip "X:" text in XAxisLabelMode::DataLabel
    std::string category;    // not used by the scatter plot
    double value;            // not used by the scatter plot
    Color color;             // not transparent: overrides SetPointColor for this point

    ChartDataPoint(double x_val, double y_val, double z_val = 0.0,
                   const std::string& lbl = "", double val = 0.0, const Color& c = Colors::Transparent);
};
```

### Empty Data and Undefined Fits
With no data source, or one without points, the chart draws "No data to
display" in place of the plot. A trend line needs at least two points with
different x values:

```cpp
double slope = 0.0, intercept = 0.0;
if (scatterPlot->ComputeLinearRegression(slope, intercept)) {
    // y = slope * x + intercept
} else {
    // fewer than 2 points, or all at one x: no trend line is drawn
}
```

## Integration with Chart Framework

### Inherited Features
From `UltraCanvasChartElementBase`:
- Grid display and customization (`SetShowGrid`, `SetGridColor`)
- Axis rendering and labels (`SetShowAxes`, `SetXAxisLabelMode`, `SetRotateXAxisLabels`)
- Background and plot area colors (`SetBackgroundColor`, `SetPlotAreaColor`)
- Coordinate transformation
- Tooltip management (`SetSeriesName`, `SetCustomTooltipGenerator`)
- X-axis zoom and pan (`SetEnableZoom`, `SetEnablePan`, `ResetZoom`, `IsZoomed`)

### Coordinate System
The scatter plot uses the `ChartCoordinateTransform` class to convert between:
- **Data Space**: Raw data values (x, y)
- **Screen Space**: Pixel coordinates for rendering

### Event Handling
Integrates with the UltraCanvas event system:
- `HandleMouseMove`: Tooltip and hover ring; moves the view during a pan drag
- `HandleMouseDown`: Starts a pan when pan is on, the chart is zoomed in and
  the press is in the plot area
- `HandleMouseUp`: Ends the pan
- `HandleMouseWheel`: Zooms the x axis in/out when zoom is on

## Typical Use Cases

1. **Correlation Analysis**
   - Marketing spend vs sales revenue
   - Temperature vs ice cream sales
   - Study hours vs test scores

2. **Outlier Detection**
   - Quality control measurements
   - Financial anomaly detection
   - Sensor data analysis

3. **Clustering Visualization**
   - Customer segmentation
   - Geographic data points
   - Scientific measurements

4. **Trend Identification**
   - Time-series relationships
   - Performance metrics
   - Economic indicators

## Limitations and Considerations

1. **Current Limitations**:
   - Point size is uniform (not data-driven; use
     [`UltraCanvasBubbleChartElement`](UltraCanvasBubbleChart.md) for
     value-scaled points)
   - One shape for all points
   - Axis ranges always follow the data (plus 5 % padding); they cannot be
     set by hand, and only the x axis zooms
   - This element renders 2D only; use
     [`UltraCanvasScatterPlot3DElement`](UltraCanvasScatterPlot3D.md) for
     (x, y, z) data

2. **Future Enhancements** (Planned):
   - Multiple named series with a legend
   - Non-linear (polynomial, exponential) trend fits
   - Custom point renderer callbacks

## Related Components

- `UltraCanvasScatterPlot3DElement`: 3D scatter plot with correlation line
- `UltraCanvasLineChartElement`: For continuous data visualization
- `UltraCanvasBarChartElement`: For categorical comparisons
- `UltraCanvasAreaChartElement`: For cumulative data display
- `UltraCanvasBubbleChartElement`: Scatter with value-scaled point sizes
- `ChartDataVector`: Standard data source implementation
- `ChartCoordinateTransform`: Coordinate transformation utility

## Version History

- **1.1.2** (2026-10-07): the page matches the code
  - The constructor and `CreateScatterPlotElement` take no numeric id
  - `SetPointSize` takes a `double` and is the point's radius (6 by default);
    the shape geometry and the tooltip's pick distance (size + 5 px) are as
    drawn
  - Selection is the hover ring, on by default; there is no click selection
  - The trend line and correlation-info listings compile; `ResetZoom` /
    `IsZoomed` and the tooltip generator are listed
  - An invented `ShowErrorMessage` and `ScatterDataPoint` gave way to the
    empty state and the real `ChartDataPoint`
- **1.1.1** (2026-10-07): zoom and pan are off by default, and work when
  turned on (the wheel zooms the x axis, a drag pans it)
- **1.1.0** (2026-07-29): Correlation line and per-point colors
  - Least-squares trend line with solid/dashed/dotted styles
  - `y = ax + b`, r and r² readout (`SetShowCorrelationInfo`)
  - `ComputeLinearRegression` / `GetCorrelationCoefficient` accessors
  - Per-point color override via `ChartDataPoint::color`
- **1.0.0** (2025-09-10): Initial implementation with basic scatter plot functionality
  - Four point shapes supported
  - Interactive tooltips and selection
  - Zoom and pan capabilities
  - Integration with chart framework

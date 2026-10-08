# UltraCanvasLineChartElement Documentation

## Overview

**UltraCanvasLineChartElement** is a sophisticated line chart component within the UltraCanvas framework, designed for rendering interactive line charts with advanced features including smoothing, animations, tooltips, and zoom/pan capabilities.

**Namespace:** `UltraCanvas`  
**Header:** `include/Plugins/Charts/UltraCanvasSpecificChartElements.h`  
**Implementation:** `Plugins/Charts/UltraCanvasSpecificChartElements.cpp`  
**Base Class:** `UltraCanvasChartElementBase`  
**Version:** 1.1.0  
**Last Modified:** 2026-10-07  
**Author:** UltraCanvas Framework

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasLineChartElement
```

## Features

### Core Capabilities
- **Line Rendering:** High-quality line drawing with customizable colors and widths
- **Data Point Display:** Optional visualization of individual data points with customizable appearance
- **Smooth Curves:** Bezier curve smoothing for more aesthetic line representation
- **Interactive Features:** Data point tooltips, and wheel zoom and drag pan of the x axis (off until enabled)
- **Animation:** Smooth animated transitions when data changes
- **Grid & Axes:** Automatic grid lines and axis labels with smart formatting

### Visual Customization
- Adjustable line color and thickness
- Configurable data point markers (color, radius)
- Optional curve smoothing with Bezier interpolation
- Background and plot area color customization
- Grid line visibility and styling

### Interactivity
- Hover tooltips showing data point values
- Wheel zoom and drag pan of the x axis, once `SetEnableZoom(true)` / `SetEnablePan(true)`
- Selection indicators for data points
- Custom tooltip content generation

## Constructor

```cpp
UltraCanvasLineChartElement(const std::string &id, int x, int y, int width, int height)
```

### Parameters
- **id:** Unique string identifier for the chart element
- **x:** X-coordinate position
- **y:** Y-coordinate position
- **width:** Width of the chart
- **height:** Height of the chart

### Default Settings
- Zoom enabled: `false`
- Pan enabled: `false`
- Line color: Blue (RGB: 0, 102, 204)
- Line width: 2.0f
- Show data points: `false`
- Point radius: 4.0f
- Smoothing: `false`

## Public Methods

### Line Configuration

#### SetLineColor
```cpp
void SetLineColor(const Color &color)
```
Sets the color of the chart line.

#### SetLineWidth
```cpp
void SetLineWidth(float width)
```
Sets the thickness of the chart line in pixels.

#### SetSmoothingEnabled
```cpp
void SetSmoothingEnabled(bool enabled)
```
Enables or disables Bezier curve smoothing for the line.

### Data Point Configuration

#### SetShowDataPoints
```cpp
void SetShowDataPoints(bool show)
```
Toggles the display of individual data point markers.

#### SetPointColor
```cpp
void SetPointColor(const Color &color)
```
Sets the color of data point markers.

#### SetPointRadius
```cpp
void SetPointRadius(float radius)
```
Sets the radius of data point markers in pixels (4 by default for the line
chart). It also sets how far the value labels sit from the points.

### Inherited Methods from Base Class

#### SetDataSource
```cpp
void SetDataSource(std::shared_ptr<IChartDataSource> data)
```
Associates a data source with the chart.

#### SetChartTitle
```cpp
void SetChartTitle(const std::string &title)
```
Sets the title displayed above the chart.

#### SetEnableTooltips
```cpp
void SetEnableTooltips(bool enable)
```
Enables or disables interactive tooltips.

#### SetEnableZoom
```cpp
void SetEnableZoom(bool enable)
```
Turns wheel zoom of the x axis on or off (off by default). Wheel up over the
plot zooms in around the pointer, wheel down zooms back out, up to 50x. A
wheel turn that changes nothing - over the axis margins, or zooming out of
the whole range - is left to the parent, so a scrolling container around the
chart still scrolls. Works in both x-axis label modes: with
`XAxisLabelMode::DataLabel` the evenly spaced points spread apart. Turning
zoom off, or setting a new data source, shows the whole range again; so does
`ResetZoom()`, and `IsZoomed()` tells whether the view is narrowed.

#### SetEnablePan
```cpp
void SetEnablePan(bool enable)
```
Turns dragging a zoomed x axis sideways with the left button on or off (off by
default). A drag over a chart that is not zoomed in is left to the parent.

## Usage Examples

### Basic Line Chart Setup

```cpp
// Create a line chart element
auto lineChart = CreateLineChartElement("salesChart", 50, 50, 600, 400);

// Configure appearance
lineChart->SetLineColor(Color(0, 102, 204, 255));  // Blue
lineChart->SetLineWidth(3.0f);
lineChart->SetShowDataPoints(true);
lineChart->SetPointRadius(5.0f);

// Set data source
auto dataSource = std::make_shared<ChartDataVector>();
dataSource->LoadFromCSV("sales_data.csv");
lineChart->SetDataSource(dataSource);

// Add title
lineChart->SetChartTitle("Monthly Sales Trend");

// Add to container
container->AddChild(lineChart);
```

### Advanced Configuration with Smoothing

```cpp
// Create smoothed line chart
auto smoothChart = CreateLineChartElement("revenueChart", 100, 100, 800, 500);

// Enable advanced features
smoothChart->SetSmoothingEnabled(true);
smoothChart->SetEnableTooltips(true);
smoothChart->SetEnableZoom(true);
smoothChart->SetEnablePan(true);

// Customize appearance
smoothChart->SetLineColor(Color(255, 99, 71, 255));     // Tomato
smoothChart->SetLineWidth(2.5f);
smoothChart->SetShowDataPoints(true);
smoothChart->SetPointColor(Color(255, 255, 255, 255));  // White
smoothChart->SetPointRadius(6.0f);

// Configure grid and background
smoothChart->SetShowGrid(true);
smoothChart->SetGridColor(Color(220, 220, 220, 255));
```

### Dynamic Data Updates

```cpp
// Create chart with initial data
auto dynamicChart = CreateLineChartElement("liveData", 50, 50, 700, 450);

// Initial setup
auto initialData = std::make_shared<ChartDataVector>();
initialData->LoadFromArray(generateInitialData());
dynamicChart->SetDataSource(initialData);

// Update data dynamically
void updateChartData() {
    auto newData = std::make_shared<ChartDataVector>();
    newData->LoadFromArray(fetchLatestData());
    dynamicChart->SetDataSource(newData);  // Triggers animation
}
```

### Interactive Features Setup

```cpp
// Create highly interactive chart
auto interactiveChart = CreateLineChartElement("analytics", 0, 0, 1024, 600);

// Enable all interactive features
interactiveChart->SetEnableTooltips(true);
interactiveChart->SetEnableZoom(true);
interactiveChart->SetEnablePan(true);
interactiveChart->SetEnableSelection(true);

// Custom tooltip content
interactiveChart->SetSeriesName("Revenue");
interactiveChart->SetCustomTooltipGenerator(
    [](const ChartDataPoint& point, size_t index) {
        return "Q" + std::to_string(index + 1) + ": $" + 
               std::to_string(static_cast<int>(point.y)) + "k";
    }
);
```

## Data Source Interface

Line charts work with any implementation of `IChartDataSource`:

```cpp
class IChartDataSource {
public:
    virtual size_t GetPointCount() const = 0;
    virtual ChartDataPoint GetPoint(size_t index) = 0;
    virtual void LoadFromCSV(const std::string& filePath) = 0;
    virtual void LoadFromArray(const std::vector<ChartDataPoint>& data) = 0;
};
```

### ChartDataPoint Structure

```cpp
struct ChartDataPoint {
    double x, y, z;
    std::string label;
    std::string category;
    double value;
    Color color;
};
```

## Rendering Pipeline

The line chart follows this rendering sequence:

1. **Background Rendering:** Fills the chart area with background color
2. **Grid Drawing:** Renders grid lines if enabled
3. **Axes Rendering:** Draws X and Y axes with tick marks
4. **Axis Labels:** Positions and renders axis labels
5. **Line Drawing:** Renders the main data line (with smoothing if enabled)
6. **Data Points:** Draws individual point markers if enabled
7. **Title Rendering:** Displays chart title at top
8. **Selection Indicators:** Shows selection markers on hover
9. **Tooltips:** Displays interactive tooltips on mouse hover

## Mouse Interaction

### Supported Events

- **MouseMove:** Updates tooltips and hover states; pans a zoomed chart during a drag
- **MouseDown:** Starts a pan when pan is on and the chart is zoomed in
- **MouseUp:** Ends the pan
- **MouseWheel:** Zooms the x axis in/out around the pointer when zoom is on

### Tooltip Behavior

Tooltips appear when:
- Mouse hovers within 20 px of a data point that is in view
- Tooltips are enabled via `SetEnableTooltips(true)`
- A valid data source is connected

## Performance Considerations

### Optimization Features

- **Render Caching:** Plot area and data bounds are cached
- **Animation Throttling:** Smooth animations at 60 FPS
- **Clipping:** Rendering is clipped to element bounds
- **Lazy Evaluation:** The plot area and ranges are worked out again when the
  data source is set, the size changes (SetBounds or the layout), the label
  mode changes or the view is zoomed or panned

### Best Practices

1. **Large Datasets:** For datasets > 10,000 points, consider data sampling
2. **Smooth Lines:** Smoothing increases render time; use selectively
3. **Data Points:** Showing all points on large datasets impacts performance
4. **Animations:** Disable animations for real-time data updates

## Factory Function

The recommended way to create line chart instances:

```cpp
std::shared_ptr<UltraCanvasLineChartElement> CreateLineChartElement(
    const std::string &id, int x, int y, int width, int height)
```

## Integration with UltraCanvas

### Container Hierarchy

Line charts can be added to any UltraCanvas container:

```cpp
auto chartArea = std::make_shared<UltraCanvasContainer>("chart-area");
auto lineChart = CreateLineChartElement("chart1", 0, 0, 800, 600);

chartArea->AddChild(lineChart);
window->AddChild(chartArea);
```

### Event Propagation

Events flow through the standard UltraCanvas event system:
- Window → Container → LineChartElement
- Chart handles events via `OnEvent()` method
- Unhandled events bubble up to parent

## Common Issues and Solutions

### Issue: Chart not displaying data
**Solution:** Ensure data source has at least 2 points and is properly initialized

### Issue: Tooltips not appearing
**Solution:** Call `SetEnableTooltips(true)` and verify mouse events are reaching the chart

### Issue: Smooth lines look incorrect
**Solution:** Smoothing works best with evenly-spaced data points; consider preprocessing data

### Issue: Performance degradation with large datasets
**Solution:** Implement data sampling or use a streaming data source

## Related Classes

- **UltraCanvasChartElementBase:** Base class providing common chart functionality
- **ChartDataVector:** Standard vector-based data container
- **ChartCoordinateTransform:** Handles data-to-screen coordinate transformations
- **UltraCanvasBarChartElement:** Bar chart implementation
- **UltraCanvasScatterPlotElement:** Scatter plot implementation
- **UltraCanvasAreaChartElement:** Area chart implementation

## Version History

- **1.1.0** (2026-10-07):
  - `SetPointRadius` sizes the line's dots (it used to reach only the area chart)
  - Zoom and pan of the x axis work; both are off by default, and a wheel turn
    or drag that does nothing is left to the parent
  - The plot area follows a resize by the layout

- **1.0.0** (2025-09-10): Initial release with full feature set
  - Line rendering with customizable style
  - Data point markers
  - Bezier curve smoothing
  - Interactive tooltips
  - Zoom and pan support
  - Animation system

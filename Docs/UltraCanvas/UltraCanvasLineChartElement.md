# UltraCanvasLineChartElement Documentation

## Overview

**UltraCanvasLineChartElement** is a sophisticated line chart component within the UltraCanvas framework, designed for rendering interactive line charts with advanced features including smoothing, animations, tooltips, and zoom/pan capabilities.

**Namespace:** `UltraCanvas`  
**Header:** `include/Plugins/Charts/UltraCanvasSpecificChartElements.h`  
**Implementation:** `Plugins/Charts/UltraCanvasSpecificChartElements.cpp`  
**Base Class:** `UltraCanvasChartElementBase`  
**Version:** 1.0.0  
**Last Modified:** 2025-09-10  
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
- **Interactive Features:** Built-in support for zoom, pan, and data point tooltips
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
- Mouse-based panning and zooming
- Selection indicators for data points
- Custom tooltip content generation

## Constructor

```cpp
UltraCanvasLineChartElement(const std::string &id, long uid, 
                            int x, int y, int width, int height)
```

### Parameters
- **id:** Unique string identifier for the chart element
- **uid:** Unique numeric identifier
- **x:** X-coordinate position
- **y:** Y-coordinate position
- **width:** Width of the chart
- **height:** Height of the chart

### Default Settings
- Zoom enabled: `true`
- Pan enabled: `true`
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
Sets the radius of data point markers in pixels.

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
Enables or disables mouse wheel zoom functionality.

#### SetEnablePan
```cpp
void SetEnablePan(bool enable)
```
Enables or disables mouse drag panning.

## Usage Examples

### Basic Line Chart Setup

```cpp
// Create a line chart element
auto lineChart = CreateLineChartElement("salesChart", 1001, 
                                       50, 50, 600, 400);

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
auto smoothChart = CreateLineChartElement("revenueChart", 2001,
                                         100, 100, 800, 500);

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
auto dynamicChart = CreateLineChartElement("liveData", 3001,
                                          50, 50, 700, 450);

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
auto interactiveChart = CreateLineChartElement("analytics", 4001,
                                              0, 0, 1024, 600);

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

- **MouseMove:** Updates tooltips and hover states
- **MouseDown:** Initiates pan operation
- **MouseUp:** Completes pan operation
- **MouseWheel:** Zooms in/out around cursor position

### Tooltip Behavior

Tooltips appear when:
- Mouse hovers within 2× point radius of a data point
- Tooltips are enabled via `SetEnableTooltips(true)`
- A valid data source is connected

## Performance Considerations

### Optimization Features

- **Render Caching:** Plot area and data bounds are cached
- **Animation Throttling:** Smooth animations at 60 FPS
- **Clipping:** Rendering is clipped to element bounds
- **Lazy Evaluation:** Recalculations only on data/size changes

### Best Practices

1. **Large Datasets:** For datasets > 10,000 points, consider data sampling
2. **Smooth Lines:** Smoothing increases render time; use selectively
3. **Data Points:** Showing all points on large datasets impacts performance
4. **Animations:** Disable animations for real-time data updates

## Factory Function

The recommended way to create line chart instances:

```cpp
std::shared_ptr<UltraCanvasLineChartElement> CreateLineChartElement(
    const std::string &id, long uid, 
    int x, int y, int width, int height
)
```

## Integration with UltraCanvas

### Container Hierarchy

Line charts can be added to any UltraCanvas container:

```cpp
auto mainWindow = std::make_shared<UltraCanvasWindow>();
auto container = std::make_shared<UltraCanvasContainer>();
auto lineChart = CreateLineChartElement("chart1", 0, 0, 800, 600);

container->AddChild(lineChart);
mainWindow->SetContent(container);
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

- **1.0.0** (2025-09-10): Initial release with full feature set
  - Line rendering with customizable style
  - Data point markers
  - Bezier curve smoothing
  - Interactive tooltips
  - Zoom and pan support
  - Animation system

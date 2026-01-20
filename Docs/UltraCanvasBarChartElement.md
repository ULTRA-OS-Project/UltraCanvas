# UltraCanvasBarChartElement Documentation

## Overview

The `UltraCanvasBarChartElement` is a specialized chart component within the UltraCanvas framework that renders vertical bar charts for data visualization. It inherits from `UltraCanvasChartElementBase` and provides comprehensive functionality for displaying categorical data with customizable bar styling, interactive features, and animation support.

**Version:** 1.0.0  
**Header:** `include/Plugins/Charts/UltraCanvasSpecificChartElements.h`  
**Implementation:** `Plugins/Charts/UltraCanvasSpecificChartElements.cpp`  
**Namespace:** `UltraCanvas`

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasBarChartElement
```

## Constructor

```cpp
UltraCanvasBarChartElement(const std::string& id, long uid, 
                           int x, int y, int width, int height)
```

### Parameters

- **id** - Unique string identifier for the chart element
- **uid** - Unique numeric identifier
- **x** - X coordinate position  
- **y** - Y coordinate position
- **width** - Width of the chart area
- **height** - Height of the chart area

### Example

```cpp
auto barChart = std::make_shared<UltraCanvasBarChartElement>(
    "salesChart", 1001, 50, 100, 600, 400
);
```

## Properties

### Bar Appearance Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| barColor | Color | Color(0, 102, 204, 255) | Fill color for bars (blue) |
| barBorderColor | Color | Color(0, 51, 102, 255) | Border color for bars (dark blue) |
| barBorderWidth | float | 1.0f | Width of bar borders in pixels |
| barSpacing | float | 0.1f | Spacing between bars as percentage (0.0-0.9) |

### Inherited Properties from Base Class

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| chartTitle | std::string | "" | Chart title displayed above the plot area |
| backgroundColor | Color | Color(255, 255, 255, 255) | Background color of entire chart |
| plotAreaColor | Color | Color(250, 250, 250, 255) | Background color of plot area |
| showGrid | bool | true | Whether to display grid lines |
| gridColor | Color | Color(220, 220, 220, 255) | Color of grid lines |
| enableTooltips | bool | true | Enable interactive tooltips |
| enableZoom | bool | false | Enable zoom functionality |
| enablePan | bool | false | Enable pan functionality |
| enableSelection | bool | false | Enable data point selection |
| animationEnabled | bool | true | Enable chart animations |
| animationDuration | float | 1.0f | Animation duration in seconds |

## Methods

### Configuration Methods

#### SetBarColor
```cpp
void SetBarColor(const Color& color)
```
Sets the fill color for all bars in the chart.

**Parameters:**
- **color** - RGB color with alpha channel

**Example:**
```cpp
barChart->SetBarColor(Color(60, 179, 113, 255)); // Medium sea green
```

#### SetBarBorderColor
```cpp
void SetBarBorderColor(const Color& color)
```
Sets the border color for all bars.

**Parameters:**
- **color** - RGB color with alpha channel

**Example:**
```cpp
barChart->SetBarBorderColor(Color(34, 139, 34, 255)); // Forest green
```

#### SetBarBorderWidth
```cpp
void SetBarBorderWidth(float width)
```
Sets the width of bar borders in pixels.

**Parameters:**
- **width** - Border width in pixels (0 to disable borders)

**Example:**
```cpp
barChart->SetBarBorderWidth(2.0f);
```

#### SetBarSpacing
```cpp
void SetBarSpacing(float spacing)
```
Sets the spacing between bars as a percentage of bar width.

**Parameters:**
- **spacing** - Spacing value from 0.0 to 0.9 (0% to 90% of bar width)

**Example:**
```cpp
barChart->SetBarSpacing(0.2f); // 20% spacing
```

### Data Management Methods (Inherited)

#### SetDataSource
```cpp
void SetDataSource(std::shared_ptr<IChartDataSource> data)
```
Sets the data source for the chart.

**Parameters:**
- **data** - Shared pointer to data source implementing IChartDataSource

**Example:**
```cpp
auto dataVector = std::make_shared<ChartDataVector>();
dataVector->LoadFromArray({
    ChartDataPoint(1, 100, 0, "Q1"),
    ChartDataPoint(2, 150, 0, "Q2"),
    ChartDataPoint(3, 120, 0, "Q3"),
    ChartDataPoint(4, 180, 0, "Q4")
});
barChart->SetDataSource(dataVector);
```

#### SetChartTitle
```cpp
void SetChartTitle(const std::string& title)
```
Sets the title displayed above the chart.

**Parameters:**
- **title** - Title text

**Example:**
```cpp
barChart->SetChartTitle("Quarterly Sales Performance");
```

### Interactive Features (Inherited)

#### SetEnableTooltips
```cpp
void SetEnableTooltips(bool enabled)
```
Enables or disables interactive tooltips on hover.

#### SetEnableZoom
```cpp
void SetEnableZoom(bool enabled)
```
Enables or disables zoom functionality.

#### SetEnablePan
```cpp
void SetEnablePan(bool enabled)
```
Enables or disables pan functionality.

## Factory Functions

### CreateBarChartElement
```cpp
std::shared_ptr<UltraCanvasBarChartElement> CreateBarChartElement(
    const std::string& id, int x, int y, 
    int width, int height)
```
Factory function to create a new bar chart element.

**Returns:** Shared pointer to new UltraCanvasBarChartElement instance

**Example:**
```cpp
auto barChart = CreateBarChartElement("myChart", 50, 50, 600, 400);
```

### CreateThemedBarChart (Helper Function)
```cpp
std::shared_ptr<UltraCanvasBarChartElement> CreateThemedBarChart(
    const std::string& id, int x, int y, 
    int width, int height, const Color& barColor, 
    const Color& borderColor)
```
Helper function to create a pre-styled bar chart.

**Example:**
```cpp
auto themedChart = CreateThemedBarChart(
    "themedChart", 1002, 50, 50, 600, 400,
    Color(60, 179, 113, 255),  // Bar color
    Color(34, 139, 34, 255)     // Border color
);
```

## Rendering Process

The bar chart rendering process follows these steps:

1. **Data Validation** - Checks if data source exists and contains points
2. **Coordinate Transform** - Creates transformation from data to screen coordinates
3. **Bar Calculation** - Calculates bar width based on number of data points
4. **Bar Rendering** - For each data point:
   - Calculates bar position and height
   - Fills rectangle with bar color
   - Draws border if border width > 0
5. **Grid and Axes** - Renders grid lines and axis labels (inherited)
6. **Title Rendering** - Draws chart title if set (inherited)

## Event Handling

### Mouse Events

#### HandleChartMouseMove
```cpp
bool HandleChartMouseMove(const Point2Di& mousePos) override
```
Handles mouse movement for tooltip display and bar highlighting.

**Returns:** true if event was handled

### Inherited Event Handlers

- `HandleMouseDown` - Initiates dragging for pan
- `HandleMouseUp` - Ends dragging
- `HandleMouseWheel` - Handles zoom with mouse wheel

## Usage Examples

### Basic Bar Chart

```cpp
// Create bar chart
auto barChart = CreateBarChartElement("salesChart", 50, 100, 600, 400);

// Create and populate data
auto salesData = std::make_shared<ChartDataVector>();
salesData->LoadFromArray({
    ChartDataPoint(0, 45000, 0, "Jan"),
    ChartDataPoint(1, 52000, 0, "Feb"),
    ChartDataPoint(2, 48000, 0, "Mar"),
    ChartDataPoint(3, 61000, 0, "Apr")
});

// Configure chart
barChart->SetDataSource(salesData);
barChart->SetChartTitle("Monthly Sales");
barChart->SetBarColor(Color(52, 152, 219, 255));
barChart->SetBarBorderWidth(1.5f);
barChart->SetEnableTooltips(true);

// Add to container
container->AddChild(barChart);
```

### Styled Performance Chart

```cpp
// Create performance chart with custom styling
auto perfChart = CreateBarChartElement("performanceChart", 50, 100, 600, 400);

// Configure appearance
perfChart->SetBarColor(Color(46, 204, 113, 255));      // Green bars
perfChart->SetBarBorderColor(Color(39, 174, 96, 255)); // Darker green border
perfChart->SetBarBorderWidth(2.0f);
perfChart->SetBarSpacing(0.25f);                       // 25% spacing

// Set data and labels
perfChart->SetDataSource(performanceData);
perfChart->SetChartTitle("Team Performance Metrics");
perfChart->SetEnableTooltips(true);

// Enable interaction
perfChart->SetEnableZoom(true);
perfChart->SetEnablePan(true);
```

### Dynamic Data Update

```cpp
// Create chart with initial data
auto dynamicChart = CreateBarChartElement("dynamicChart", 50, 100, 600, 400);
dynamicChart->SetDataSource(initialData);

// Button to switch data
auto switchButton = std::make_shared<UltraCanvasButton>("btnSwitch", 50, 520, 120, 35);
switchButton->SetText("Load Q2 Data");
switchButton->onClick = [dynamicChart, q2Data]() {
    dynamicChart->SetDataSource(q2Data);
    dynamicChart->SetChartTitle("Q2 Results");
    dynamicChart->RequestRedraw();
};
```

## Data Source Interface

Bar charts work with any data source implementing `IChartDataSource`:

```cpp
class IChartDataSource {
public:
    virtual size_t GetPointCount() const = 0;
    virtual ChartDataPoint GetPoint(size_t index) = 0;
    virtual bool SupportsStreaming() const = 0;
    virtual void LoadFromCSV(const std::string& filePath) = 0;
    virtual void LoadFromArray(const std::vector<ChartDataPoint>& data) = 0;
};
```

### ChartDataPoint Structure

```cpp
struct ChartDataPoint {
    double x;        // X position (category index for bar charts)
    double y;        // Y value (bar height)
    double z;        // Z value (unused in bar charts)
    std::string label;    // Category label
    std::string category; // Category grouping
    double value;    // Additional value storage
    Color color;     // Optional per-point color override
};
```

## Integration with UltraCanvas Framework

### Container Hierarchy

```cpp
// Create main container
auto mainContainer = CreateContainer("mainContainer", 0, 0, 800, 600);

// Add bar chart to container
auto barChart = CreateBarChartElement("chart1", 50, 50, 700, 400);
mainContainer->AddChild(barChart);

// Add controls below chart
auto refreshBtn = CreateButton("btnRefresh", 50, 470, 100, 30);
mainContainer->AddChild(refreshBtn);
```

### Tooltip Integration

The bar chart automatically integrates with UltraCanvas tooltip system:

```cpp
// Enable tooltips
barChart->SetEnableTooltips(true);

// Custom tooltip generator
barChart->SetCustomTooltipGenerator([](const ChartDataPoint& point, size_t index) {
    return "Month: " + point.label + "\nSales: $" + std::to_string(static_cast<int>(point.y));
});
```

## Performance Considerations

- **Data Points**: Optimized for up to 100 bars
- **Rendering**: Uses cached plot area calculations
- **Redraw**: Only triggered when properties change via `RequestRedraw()`
- **Memory**: Uses shared_ptr for data management

## Dependencies

- `UltraCanvasChartElementBase.h` - Base chart functionality
- `UltraCanvasChartDataStructures.h` - Data structures
- `UltraCanvasRenderContext.h` - Rendering interface
- `UltraCanvasCommonTypes.h` - Common type definitions
- `UltraCanvasUIElement.h` - UI element base class

## See Also

- [UltraCanvasLineChartElement](UltraCanvasLineChartElement.md) - Line chart component
- [UltraCanvasScatterPlotElement](UltraCanvasScatterPlotElement.md) - Scatter plot component
- [UltraCanvasAreaChartElement](UltraCanvasAreaChartElement.md) - Area chart component
- [UltraCanvasChartElementBase](UltraCanvasChartElementBase.md) - Base chart class
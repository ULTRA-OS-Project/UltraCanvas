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
**Version:** 1.0.0  
**Last Modified:** 2025-09-10  

## Features

### Core Capabilities
- **Multiple Point Shapes:** Circle, Square, Triangle, Diamond
- **Interactive Tooltips:** Display detailed information on hover
- **Zoom & Pan:** Navigate through large datasets with mouse controls
- **Point Selection:** Interactive selection of individual data points
- **Customizable Appearance:** Configurable colors, sizes, and styles
- **High Performance:** Efficient rendering for large datasets

## Constructor

```cpp
UltraCanvasScatterPlotElement(const std::string& id, long uid, 
                              int x, int y, int width, int height)
```

### Parameters
- `id` - Unique identifier string for the chart element
- `uid` - Unique numeric identifier
- `x` - X position of the chart
- `y` - Y position of the chart
- `width` - Width of the chart
- `height` - Height of the chart

### Default Settings
- `enableZoom` = true
- `enablePan` = true
- `enableSelection` = true
- `pointShape` = PointShape::Circle
- `pointColor` = Color(0, 102, 204, 255) // Blue
- `pointSize` = 6.0f

## Enumerations

### PointShape
Defines the visual representation of data points.

```cpp
enum class PointShape {
    Circle,    // Circular points (default)
    Square,    // Square points
    Triangle,  // Triangular points pointing up
    Diamond    // Diamond-shaped points
}
```

## Configuration Methods

### Point Appearance

#### SetPointColor
```cpp
void SetPointColor(const Color& color)
```
Sets the fill color for all data points.

**Parameters:**
- `color` - Color object defining the point color

**Example:**
```cpp
scatterPlot->SetPointColor(Color(255, 140, 0, 255)); // Dark orange
```

#### SetPointSize
```cpp
void SetPointSize(float size)
```
Sets the base size for data points in pixels.

**Parameters:**
- `size` - Point diameter/size in pixels

**Example:**
```cpp
scatterPlot->SetPointSize(8.0f);
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

### Data Management

#### SetDataSource
```cpp
void SetDataSource(std::shared_ptr<IChartDataSource> data)
```
Inherited from `UltraCanvasChartElementBase`. Sets the data source for the scatter plot.

**Example:**
```cpp
auto correlationData = std::make_shared<ChartDataVector>();
// Populate data...
scatterPlot->SetDataSource(correlationData);
```

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
Enables/disables tooltips when hovering over data points.

#### SetEnableZoom
```cpp
void SetEnableZoom(bool enable)
```
Enables/disables zoom functionality using mouse wheel.

#### SetEnablePan
```cpp
void SetEnablePan(bool enable)
```
Enables/disables panning by dragging the mouse.

#### SetEnableSelection
```cpp
void SetEnableSelection(bool enable)
```
Enables/disables the ability to select individual data points.

## Factory Function

```cpp
std::shared_ptr<UltraCanvasScatterPlotElement> CreateScatterPlotElement(
    const std::string& id, long uid, 
    int x, int y, int width, int height)
```

Convenience factory function for creating scatter plot instances.

**Example:**
```cpp
auto scatterPlot = CreateScatterPlotElement("correlationScatter", 1003, 
                                            50, 100, 600, 400);
```

## Rendering Implementation

### RenderChart Method
The core rendering method draws points based on their shape setting:

```cpp
virtual void RenderChart(IRenderContext* ctx) override
```

**Rendering Process:**
1. Transforms data coordinates to screen coordinates
2. Iterates through all data points
3. Renders each point according to the selected shape
4. Applies the configured color and size settings

### Point Shape Rendering

#### Circle
```cpp
ctx->FillCircle(screenPos.x, screenPos.y, currentPointSize);
```

#### Square
```cpp
float halfSize = currentPointSize / 2;
ctx->FillRectangle(screenPos.x - halfSize, screenPos.y - halfSize,
                   currentPointSize, currentPointSize);
```

#### Triangle
```cpp
std::vector<Point2Df> trianglePoints = {
    Point2Df(screenPos.x, screenPos.y - currentPointSize),
    Point2Df(screenPos.x - currentPointSize * 0.866f, 
             screenPos.y + currentPointSize * 0.5f),
    Point2Df(screenPos.x + currentPointSize * 0.866f, 
             screenPos.y + currentPointSize * 0.5f)
};
ctx->FillLinePath(trianglePoints);
```

#### Diamond
```cpp
std::vector<Point2Df> diamondPoints = {
    Point2Df(screenPos.x, screenPos.y - currentPointSize),
    Point2Df(screenPos.x + currentPointSize, screenPos.y),
    Point2Df(screenPos.x, screenPos.y + currentPointSize),
    Point2Df(screenPos.x - currentPointSize, screenPos.y)
};
ctx->FillLinePath(diamondPoints);
```

## Mouse Interaction

### HandleChartMouseMove
```cpp
virtual bool HandleChartMouseMove(const Point2Di& mousePos) override
```

Handles mouse movement for tooltip display and hover effects.

**Functionality:**
- Calculates distance to nearest point
- Shows tooltip when mouse is within `pointSize * 2` pixels
- Returns true if tooltip is shown
- Hides tooltip when mouse moves away from points

## Usage Example

### Basic Setup
```cpp
// Create scatter plot
auto scatterPlot = CreateScatterPlotElement("salesCorrelation", 1001, 
                                            50, 50, 800, 600);

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
scatterPlot->SetPointSize(10.0f);
scatterPlot->SetPointShape(UltraCanvasScatterPlotElement::PointShape::Circle);

// Enable interactive features
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
    const Color& pointColor, float pointSize,
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
   - Using simpler shapes (Circle) for faster rendering

2. **Memory Management**: Use shared_ptr for data sources to ensure proper cleanup

3. **Visual Clarity**: 
   - Choose contrasting colors for points vs background
   - Adjust point size based on data density
   - Use different shapes to distinguish multiple series

### Data Preparation
```cpp
// Optimal data structure for scatter plots
struct ScatterDataPoint {
    double x;        // X-axis value
    double y;        // Y-axis value
    double z;        // Optional: for bubble charts (future)
    std::string label;  // Tooltip label
    double value;    // Additional value for tooltips
};
```

### Error Handling
```cpp
// Always check data validity before setting
if (data && data->GetPointCount() > 0) {
    scatterPlot->SetDataSource(data);
} else {
    // Handle empty or invalid data
    ShowErrorMessage("No data available for scatter plot");
}
```

## Integration with Chart Framework

### Inherited Features
From `UltraCanvasChartElementBase`:
- Grid display and customization
- Axis rendering and labels
- Background and plot area colors
- Coordinate transformation
- Tooltip management
- Animation support
- Zoom/Pan infrastructure

### Coordinate System
The scatter plot uses the `ChartCoordinateTransform` class to convert between:
- **Data Space**: Raw data values (x, y)
- **Screen Space**: Pixel coordinates for rendering

### Event Handling
Integrates with the UltraCanvas event system:
- `HandleMouseMove`: Tooltip and hover effects
- `HandleMouseDown`: Selection and drag start
- `HandleMouseUp`: Selection completion
- `HandleMouseWheel`: Zoom in/out

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
   - Point size is uniform (not data-driven)
   - No built-in trend line calculation
   - No automatic axis scaling options
   - Limited to 2D visualization

2. **Future Enhancements** (Planned):
   - Bubble chart support (variable point sizes)
   - Regression line overlay
   - Multiple series support
   - Custom point renderer callbacks
   - 3D scatter plot variant

## Related Components

- `UltraCanvasLineChartElement`: For continuous data visualization
- `UltraCanvasBarChartElement`: For categorical comparisons
- `UltraCanvasAreaChartElement`: For cumulative data display
- `ChartDataVector`: Standard data source implementation
- `ChartCoordinateTransform`: Coordinate transformation utility

## Version History

- **1.0.0** (2025-09-10): Initial implementation with basic scatter plot functionality
  - Four point shapes supported
  - Interactive tooltips and selection
  - Zoom and pan capabilities
  - Integration with chart framework

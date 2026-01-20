# UltraCanvasAreaChartElement Documentation

## Overview

The `UltraCanvasAreaChartElement` is a sophisticated area chart visualization component in the UltraCanvas framework. It extends `UltraCanvasChartElementBase` to provide filled area charts with support for gradients, transparency, data point markers, smoothing, and interactive features like tooltips, zooming, and panning.

Area charts are ideal for displaying cumulative values, trends over time, and volume data such as revenue, resource usage, or any metric where the magnitude of change is important.

## Class Definition

```cpp
namespace UltraCanvas {
    class UltraCanvasAreaChartElement : public UltraCanvasChartElementBase
}
```

### Header Information
- **File**: `UltraCanvas/include/Plugins/Charts/UltraCanvasSpecificChartElements.h`
- **Implementation**: `UltraCanvas/Plugins/Charts/UltraCanvasSpecificChartElements.cpp`
- **Version**: 1.0.0
- **Last Modified**: 2025-09-10
- **Author**: UltraCanvas Framework

## Constructor

```cpp
UltraCanvasAreaChartElement(const std::string &id, long uid, 
                            int x, int y, int width, int height)
```

### Parameters
- `id`: Unique string identifier for the element
- `uid`: Unique numeric identifier
- `x`: X coordinate position
- `y`: Y coordinate position  
- `width`: Width of the chart area
- `height`: Height of the chart area

### Default Settings
- Zoom enabled by default
- Pan enabled by default
- Semi-transparent blue fill color (0, 102, 204, 128)
- Solid blue line color (0, 102, 204, 255)
- Line width: 2.0f
- Data points hidden by default

## Properties and Configuration Methods

### Fill Style Configuration

#### SetFillColor
```cpp
void SetFillColor(const Color &color)
```
Sets the solid fill color for the area. Supports transparency via alpha channel.

**Parameters:**
- `color`: RGBA color for area fill

**Example:**
```cpp
areaChart->SetFillColor(Color(0, 150, 136, 120)); // Teal with transparency
```

#### SetFillGradientEnabled
```cpp
void SetFillGradientEnabled(bool enabled)
```
Enables or disables gradient fill instead of solid color.

#### SetGradientColors
```cpp
void SetGradientColors(const Color &startColor, const Color &endColor)
```
Sets the gradient colors when gradient fill is enabled. Creates a vertical gradient from top to bottom.

**Parameters:**
- `startColor`: Color at the top of the area
- `endColor`: Color at the bottom of the area

**Example:**
```cpp
areaChart->SetFillGradientEnabled(true);
areaChart->SetGradientColors(
    Color(0, 150, 136, 180),  // Teal top
    Color(0, 150, 136, 40)    // Faded teal bottom
);
```

### Line Style Configuration

#### SetLineColor
```cpp
void SetLineColor(const Color &color)
```
Sets the color of the line drawn along the top edge of the area.

#### SetLineWidth
```cpp
void SetLineWidth(float width)
```
Sets the width of the top edge line in pixels.

### Data Point Markers

#### SetShowDataPoints
```cpp
void SetShowDataPoints(bool show)
```
Toggles visibility of circular markers at each data point.

#### SetPointColor
```cpp
void SetPointColor(const Color &color)
```
Sets the fill color for data point markers.

#### SetPointRadius
```cpp
void SetPointRadius(float radius)
```
Sets the radius of data point markers in pixels. Values are clamped to minimum 0.0f.

### Smoothing

#### SetSmoothingEnabled
```cpp
void SetSmoothingEnabled(bool enabled)
```
Enables curve smoothing for a more fluid appearance. When enabled, the chart interpolates between points for smoother transitions.

## Core Methods

### RenderChart
```cpp
void RenderChart(IRenderContext *ctx) override
```
Main rendering method that draws the area chart. This method:
1. Updates the rendering cache if needed
2. Transforms data coordinates to screen coordinates
3. Creates the area path by connecting data points and baseline
4. Applies smoothing if enabled
5. Fills the area with gradient or solid color
6. Draws the top edge line
7. Renders data point markers if enabled

### HandleChartMouseMove
```cpp
bool HandleChartMouseMove(const Point2Di &mousePos) override
```
Handles mouse movement for interactive features like tooltips. Finds the closest data point on the X-axis and displays tooltip information.

**Returns:** `true` if the event was handled, `false` otherwise

## Helper Methods

### SmoothAreaPoints (Private)
```cpp
std::vector<Point2Df> SmoothAreaPoints(const std::vector<Point2Df> &points) const
```
Applies interpolation to create smoother curves between data points.

### RenderDataPoints (Private)
```cpp
void RenderDataPoints(IRenderContext *ctx, const std::vector<Point2Df> &areaPoints) const
```
Renders circular markers at each data point position with optional white border.

### RenderGradientFill (Private)
```cpp
void RenderGradientFill(IRenderContext *ctx, const std::vector<Point2Df> &areaPoints) const
```
Creates and applies a vertical gradient fill from the top to bottom of the area.

## Factory Function

```cpp
inline std::shared_ptr<UltraCanvasAreaChartElement> CreateAreaChartElement(
    const std::string &id, int x, int y, int width, int height)
```
Convenience factory function to create an area chart element instance.

## Usage Example

```cpp
// Create area chart
auto areaChart = CreateAreaChartElement("revenueAreaChart", 50, 100, 600, 400);

// Create and set data source
auto revenueData = std::make_shared<ChartDataVector>();
std::vector<ChartDataPoint> revenue = {
    ChartDataPoint(1, 85000, 0, "Q1 2023", 85000),
    ChartDataPoint(2, 92000, 0, "Q2 2023", 92000),
    ChartDataPoint(3, 78000, 0, "Q3 2023", 78000),
    ChartDataPoint(4, 105000, 0, "Q4 2023", 105000),
    // ... more data points
};
revenueData->LoadFromArray(revenue);

// Configure the chart
areaChart->SetDataSource(revenueData);
areaChart->SetChartTitle("Quarterly Revenue Growth");
areaChart->SetFillColor(Color(0, 150, 136, 120));        // Teal with transparency
areaChart->SetLineColor(Color(0, 150, 136, 255));        // Solid teal line
areaChart->SetLineWidth(3.0f);
areaChart->SetShowDataPoints(true);
areaChart->SetPointColor(Color(255, 87, 34, 255));       // Deep orange points
areaChart->SetPointRadius(4.0f);
areaChart->SetFillGradientEnabled(true);
areaChart->SetGradientColors(
    Color(0, 150, 136, 180),    // Teal top
    Color(0, 150, 136, 40)      // Faded teal bottom
);
areaChart->SetSmoothingEnabled(false);
areaChart->SetEnableTooltips(true);
areaChart->SetEnableZoom(true);
areaChart->SetEnablePan(true);

// Add to container
container->AddChild(areaChart);
```

## Inherited Features from UltraCanvasChartElementBase

The area chart inherits numerous features from its base class:

### Interactive Features
- **Tooltips**: Display data values on hover
- **Zoom**: Mouse wheel zoom in/out
- **Pan**: Click and drag to pan the view
- **Keyboard shortcuts**: R (reset), +/- (zoom)

### Visual Configuration
- Chart title display
- Grid lines (toggleable)
- Axis labels and formatting
- Background colors for chart and plot area
- Custom tooltip content generators

### Data Management
- Support for various data sources via `IChartDataSource` interface
- Automatic data bounds calculation
- Coordinate transformation between data and screen space
- Data caching for performance

## Rendering Pipeline

1. **Cache Validation**: Checks if rendering cache needs updating
2. **Data Transformation**: Converts data points to screen coordinates
3. **Path Construction**: 
   - Adds baseline start point (bottom-left)
   - Adds all data points (top edge)
   - Adds baseline end point (bottom-right)
   - Creates closed shape for filling
4. **Smoothing** (optional): Interpolates between points
5. **Fill Rendering**: Applies gradient or solid fill
6. **Line Drawing**: Renders top edge line
7. **Point Rendering** (optional): Draws circular markers

## Performance Considerations

- Uses caching to avoid redundant calculations
- Only redraws when `RequestRedraw()` is called
- Efficient coordinate transformation via `ChartCoordinateTransform`
- Minimal memory allocations with reserved vector capacity

## Best Practices

1. **Data Requirements**: Ensure at least 2 data points for meaningful visualization
2. **Color Selection**: Use semi-transparent fills to allow overlapping areas in multi-series charts
3. **Gradient Usage**: Enable gradients for better visual depth and data emphasis
4. **Point Markers**: Enable for smaller datasets, disable for large datasets to improve performance
5. **Smoothing**: Use smoothing for trend visualization, disable for precise data representation

## Limitations

- Requires minimum of 2 data points to render
- Gradient direction is fixed (vertical only)
- Point markers are always circular
- Smoothing algorithm uses simple interpolation

## See Also

- [`UltraCanvasChartElementBase`](UltraCanvasChartElementBase.md) - Base class documentation
- [`UltraCanvasLineChartElement`](UltraCanvasLineChartElement.md) - Line chart component
- [`UltraCanvasBarChartElement`](UltraCanvasBarChartElement.md) - Bar chart component
- [`ChartDataVector`](ChartDataVector.md) - Data source implementation
- [`IRenderContext`](IRenderContext.md) - Rendering interface
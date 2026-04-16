# UltraCanvasWaterfallChart Documentation

## Overview

The **UltraCanvasWaterfallChart** is a sophisticated data visualization component within the UltraCanvas framework that displays cumulative data flow through a series of positive and negative changes. It's particularly effective for showing how an initial value is affected by a series of intermediate values, leading to a final result.

**Version:** 1.0.1  
**Last Modified:** 2025-09-29  
**Author:** UltraCanvas Framework  
**Location:** `Plugins/Charts/`

## Key Features

- **Cumulative Flow Visualization** - Automatically calculates and displays running totals
- **Multi-Type Bar Support** - Regular, subtotal, and total bars with distinct styling
- **Connection Lines** - Visual connectors between bars showing flow continuity
- **Customizable Styling** - Full control over colors, borders, and visual effects
- **Interactive Tooltips** - Rich hover information for each data point
- **Value Labels** - Display change values and cumulative totals
- **Responsive Layout** - Automatic scaling and positioning
- **Animation Support** - Smooth transitions (when enabled)

## Architecture

### File Structure

```
UltraCanvas/
├── include/Plugins/Charts/
│   └── UltraCanvasWaterfallChart.h    // Header with declarations
└── Plugins/Charts/
    └── UltraCanvasWaterfallChart.cpp  // Implementation
```

### Class Hierarchy

```cpp
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
        └── UltraCanvasWaterfallChartElement
```

## Data Structures

### WaterfallChartDataPoint

Represents a single data point in the waterfall chart:

```cpp
struct WaterfallChartDataPoint {
    std::string label;          // Category/step name
    double value;               // Change amount (+/-)
    double cumulativeValue;     // Running total (auto-calculated)
    bool isSubtotal;           // Intermediate subtotal bar
    bool isTotal;              // Final total bar
    Color customColor;         // Optional custom color
    std::string category;      // Optional grouping
};
```

### WaterfallChartDataVector

Manages the collection of waterfall data points:

```cpp
class WaterfallChartDataVector : public IChartDataSource {
    // Core methods
    void AddWaterfallPoint(const std::string& label, double value, 
                           bool isSubtotal = false, bool isTotal = false);
    void ClearData();
    const WaterfallChartDataPoint& GetWaterfallPoint(size_t index) const;
    double GetFinalValue() const;
    
    // Data loading
    void LoadFromArray(const std::vector<std::pair<std::string, double>>& data);
};
```

## Enumerations

### ConnectionStyle

Defines the style of connecting lines between bars:

```cpp
enum class ConnectionStyle {
    None,       // No connecting lines
    Dotted,     // Dotted connecting lines
    Solid,      // Solid connecting lines  
    Dashed      // Dashed connecting lines
};
```

### BarStyle

Defines the visual style of the bars:

```cpp
enum class BarStyle {
    Standard,   // Standard rectangular bars
    Rounded,    // Rounded corner bars
    Gradient    // Gradient fill bars
};
```

## Configuration Methods

### Color Customization

```cpp
// Bar colors
void SetPositiveBarColor(const Color& color);    // Green default: (46, 125, 50)
void SetNegativeBarColor(const Color& color);    // Red default: (211, 47, 47)
void SetSubtotalBarColor(const Color& color);    // Gray default: (158, 158, 158)
void SetTotalBarColor(const Color& color);       // Blue default: (25, 118, 210)
void SetStartingBarColor(const Color& color);    // Blue-gray default: (96, 125, 139)

// Connection lines
void SetConnectionLineStyle(ConnectionStyle style, 
                           const Color& color = Color(117, 117, 117), 
                           float width = 1.5f);

// Bar borders
void SetBarBorder(const Color& color, float width);
```

### Visual Configuration

```cpp
// Bar styling
void SetBarStyle(BarStyle style);
void SetBarSpacing(float spacing);  // 0.0-0.8 ratio

// Labels
void SetShowValueLabels(bool show);
void SetShowCumulativeLabels(bool show);
void SetLabelStyle(const Color& textColor, float fontSize);

// Features
void SetShowStartingBar(bool show);
void SetEnableConnectorLines(bool enable);
```

### Data Management

```cpp
// Set data source
void SetWaterfallDataSource(std::shared_ptr<WaterfallChartDataVector> ds);
std::shared_ptr<WaterfallChartDataVector> GetWaterfallDataSource() const;

// Chart properties (inherited)
void SetChartTitle(const std::string& title);
```

## Usage Examples

### Basic Waterfall Chart

```cpp
// Create chart element
auto chart = CreateWaterfallChartElement("revenue_chart", 10, 10, 600, 400);

// Create and populate data
auto data = std::make_shared<WaterfallChartDataVector>();
data->AddWaterfallPoint("Q1 Revenue", 150.0);
data->AddWaterfallPoint("Q2 Revenue", 120.0);
data->AddWaterfallPoint("Q3 Revenue", -30.0);
data->AddWaterfallPoint("Q4 Revenue", 180.0);
data->AddWaterfallPoint("Total", 0.0, false, true);  // Total bar

// Configure chart
chart->SetWaterfallDataSource(data);
chart->SetChartTitle("Annual Revenue Flow");
chart->SetPositiveBarColor(Color(76, 175, 80));  // Green
chart->SetNegativeBarColor(Color(244, 67, 54));  // Red
```

### Cash Flow with Subtotals

```cpp
auto cashFlow = std::make_shared<WaterfallChartDataVector>();

// Income section
cashFlow->AddWaterfallPoint("Starting Balance", 1000.0);
cashFlow->AddWaterfallPoint("Sales", 500.0);
cashFlow->AddWaterfallPoint("Services", 300.0);
cashFlow->AddWaterfallPoint("Income Subtotal", 0.0, true, false);  // Subtotal

// Expenses section
cashFlow->AddWaterfallPoint("Salaries", -400.0);
cashFlow->AddWaterfallPoint("Rent", -200.0);
cashFlow->AddWaterfallPoint("Utilities", -100.0);
cashFlow->AddWaterfallPoint("Expense Subtotal", 0.0, true, false);  // Subtotal

// Final
cashFlow->AddWaterfallPoint("Net Cash", 0.0, false, true);  // Total

auto chart = CreateWaterfallChartWithData("cash_flow", 2001, 10, 10, 700, 450, 
                                          cashFlow, "Monthly Cash Flow Analysis");

// Configure appearance
chart->SetConnectionLineStyle(ConnectionStyle::Dotted);
chart->SetBarStyle(BarStyle::Rounded);
chart->SetShowCumulativeLabels(true);
```

### Factory Functions

```cpp
// Create empty chart
std::shared_ptr<UltraCanvasWaterfallChartElement> CreateWaterfallChartElement(
    const std::string& id, int x, int y, int width, int height);

// Create chart with data
std::shared_ptr<UltraCanvasWaterfallChartElement> CreateWaterfallChartWithData(
    const std::string& id, int x, int y, int width, int height,
    std::shared_ptr<WaterfallChartDataVector> data, 
    const std::string& title = "");
```

## Rendering Pipeline

### Main Render Method

```cpp
void RenderChart(IRenderContext* ctx) override {
    // 1. Draw background and grid
    DrawCommonBackground(ctx);
    if (showGrid) DrawGrid(ctx);
    DrawAxes(ctx);
    
    // 2. Draw waterfall bars
    DrawWaterfallBars(ctx);
    
    // 3. Draw connecting lines
    if (enableConnectorLines) {
        DrawConnectionLines(ctx);
    }
    
    // 4. Draw value labels
    if (showValueLabels || showCumulativeLabels) {
        DrawValueLabels(ctx);
    }
}
```

### Rendering Cache

The chart maintains an internal cache for optimized rendering:

```cpp
struct WaterfallRenderData {
    float barWidth;
    float barSpacing;
    std::vector<float> barX;      // X positions
    std::vector<float> barY;      // Y positions
    std::vector<float> barHeight; // Heights
    std::vector<Color> barColors; // Colors
    bool isValid;
};
```

## Interactive Features

### Tooltips

Automatic tooltip generation on hover:

```cpp
// Tooltip content includes:
- Label/category name
- Change value (with +/- sign)
- Cumulative total
- Category (if specified)
- Special labels for totals/subtotals
```

### Mouse Interaction

```cpp
bool HandleChartMouseMove(const Point2Di& mousePos) override;
size_t GetBarIndexAtPosition(const Point2Di& mousePos) const;
std::string GenerateWaterfallTooltip(size_t index) const;
```

## Value Formatting

The chart includes intelligent value formatting:

```cpp
std::string FormatValue(double value) const {
    // Values >= 1M: "1.5M"
    // Values >= 1K: "1.5K"  
    // Integers: "100"
    // Decimals: "10.5"
}
```

## Default Styling

### Color Palette

| Element | Default Color | RGB Values |
|---------|--------------|------------|
| Positive Bars | Green | (46, 125, 50) |
| Negative Bars | Red | (211, 47, 47) |
| Subtotal Bars | Gray | (158, 158, 158) |
| Total Bars | Blue | (25, 118, 210) |
| Starting Bar | Blue-Gray | (96, 125, 139) |
| Connection Lines | Gray | (117, 117, 117) |
| Bar Borders | Black | (0, 0, 0) |
| Label Text | Dark Gray | (33, 33, 33) |

### Default Settings

- **Bar Spacing:** 0.2 (20% of bar width)
- **Connection Style:** Dotted
- **Bar Style:** Standard
- **Border Width:** 1.0px
- **Label Font Size:** 10.0pt
- **Show Value Labels:** true
- **Show Cumulative Labels:** false
- **Enable Tooltips:** true
- **Enable Zoom:** false (typically disabled for waterfall charts)
- **Enable Pan:** false

## Performance Considerations

- **Render Caching:** Bar positions and dimensions are cached until data changes
- **Selective Redraw:** Only affected areas are redrawn on updates
- **Efficient Calculations:** Cumulative values calculated once and cached
- **Optimized Hit Testing:** Fast bar selection using cached positions

## Integration with UltraCanvas Framework

The WaterfallChartElement integrates seamlessly with:

- **UltraCanvasContainer** - Can be added as child element
- **UltraCanvasTooltipManager** - Automatic tooltip handling
- **IRenderContext** - Platform-agnostic rendering
- **Event System** - Mouse and interaction events
- **Layout System** - Responsive sizing and positioning

## Best Practices

1. **Data Organization**
   - Add data points in logical sequence
   - Use subtotals to group related items
   - Always end with a total bar for clarity

2. **Visual Design**
   - Use consistent color scheme (green/red for positive/negative)
   - Enable connection lines for better flow visualization
   - Show value labels for important data points

3. **Performance**
   - Limit number of bars for readability (typically < 20)
   - Disable unused features (cumulative labels if not needed)
   - Use render caching for static charts

4. **User Experience**
   - Enable tooltips for detailed information
   - Use clear, descriptive labels
   - Format values appropriately (K, M suffixes)

## Limitations

- No built-in animation (planned for future)
- Zoom/pan typically disabled (not suitable for this chart type)
- Maximum reasonable bar count: ~20-30 for clarity
- No stacked bar support (use regular bar chart for that)

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | 2025-09-20 | Initial implementation |
| 1.0.1 | 2025-09-29 | Added tooltip support, improved rendering cache |

## Dependencies

- `UltraCanvasChartElementBase` - Base chart functionality
- `UltraCanvasChartDataStructures` - Data interfaces
- `UltraCanvasTooltipManager` - Tooltip display
- `IRenderContext` - Rendering interface
- Standard C++ libraries: `<vector>`, `<string>`, `<memory>`

## See Also

- [UltraCanvasChartElementBase](./UltraCanvasChartElementBase.md)
- [UltraCanvasBarChart](./UltraCanvasBarChart.md)
- [UltraCanvasLineChart](./UltraCanvasLineChart.md)
- [UltraCanvas Charts Overview](./ChartsOverview.md)
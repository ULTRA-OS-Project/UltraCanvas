# UltraCanvasDivergingBarChart Documentation

## Overview

The `UltraCanvasDivergingBarChart` is a specialized chart component in the UltraCanvas framework designed for displaying multi-valued categorical data that diverges from a central axis. This chart type is ideal for visualizing data with opposing or contrasting values, such as population pyramids, Likert scale survey responses, tornado charts, and other comparative visualizations.

**Version:** 1.1.0  
**Last Modified:** 2025-09-23  
**Author:** UltraCanvas Framework  
**Namespace:** `UltraCanvas`

## Features

- **Multiple Chart Styles**: Supports various diverging chart visualizations including Population Pyramid, Likert Scale, Tornado Chart, and Opposing Bars
- **Flexible Data Categories**: Define custom categories with colors and positive/negative positioning
- **Interactive Elements**: Hover effects, tooltips, and click callbacks for user interaction
- **Animation Support**: Smooth animations when rendering or updating data
- **Customizable Appearance**: Configurable bar heights, center gaps, grid lines, and labels
- **Row-Based Data Structure**: Organize data by rows with multiple category values per row

## Class Hierarchy

```
UltraCanvasChartElementBase
    └── UltraCanvasDivergingBarChart
```

## File Structure

```
UltraCanvas/
├── include/
│   └── Plugins/
│       └── Charts/
│           └── UltraCanvasDivergingBarChart.h
└── Plugins/
    └── Charts/
        └── UltraCanvasDivergingBarChart.cpp
```

## Data Structures

### DivergingChartStyle Enum

```cpp
enum class DivergingChartStyle {
    PopulationPyramid,   // Symmetric bars extending from center
    TornadoChart,        // Single bars with positive/negative values
    LikertScale,         // Stacked segments diverging from center
    OpposingBars         // Two separate bars meeting at center
};
```

### DivergingCategory Structure

```cpp
struct DivergingCategory {
    std::string name;        // Category name (e.g., "Strongly Agree", "Male")
    Color categoryColor;     // Color for this category
    bool isPositive;        // True if on right/positive side, false if on left/negative
    
    DivergingCategory(const std::string& name, const Color& color, bool positive);
};
```

### DivergingChartPoint Structure

```cpp
struct DivergingChartPoint : public ChartDataPoint {
    std::map<std::string, float> categoryValues;  // Values for each category
    std::string rowLabel;                         // Y-axis label
    
    void AddCategoryValue(const std::string& category, float value);
};
```

### DivergingDataSource Class

A specialized data source for diverging bar charts that manages categories and data points:

```cpp
class DivergingDataSource : public IChartDataSource {
    // Manages diverging data points with category values
    // Tracks which categories are on positive vs negative sides
    // Provides methods for adding rows and calculating bounds
};
```

## Constructor

```cpp
UltraCanvasDivergingBarChart(const std::string& id, long uid, 
                              int x, int y, int width, int height)
```

**Parameters:**
- `id`: Unique identifier string for the chart
- `uid`: Unique numeric identifier
- `x`: X position of the chart
- `y`: Y position of the chart
- `width`: Width of the chart
- `height`: Height of the chart

## Public Methods

### Data Management

#### SetCategories
```cpp
void SetCategories(const std::vector<DivergingCategory>& cats)
```
Sets the categories for the diverging chart. Categories define the segments that appear on either side of the center axis.

#### AddDataRow
```cpp
void AddDataRow(const std::string& rowLabel, 
                const std::map<std::string, float>& values)
```
Adds a single data row with values for each category.

**Parameters:**
- `rowLabel`: Label for the row (appears on Y-axis)
- `values`: Map of category names to their corresponding values

#### LoadDataMatrix
```cpp
void LoadDataMatrix(const std::vector<std::string>& rowLabels,
                    const std::vector<std::vector<float>>& matrix)
```
Loads a complete data matrix where rows correspond to labels and columns to categories.

#### ClearData
```cpp
void ClearData()
```
Clears all data from the chart.

### Configuration

#### SetChartStyle
```cpp
void SetChartStyle(DivergingChartStyle style)
```
Sets the visualization style for the diverging chart.

#### SetBarHeight
```cpp
void SetBarHeight(float height)
```
Sets the height of bars as a percentage of row height (0.1 to 1.0).

#### SetCenterGap
```cpp
void SetCenterGap(float gap)
```
Sets the gap between opposing bars at the center axis in pixels.

#### SetShowCenterLine
```cpp
void SetShowCenterLine(bool show)
```
Toggles the visibility of the vertical center line.

#### SetShowRowLabels
```cpp
void SetShowRowLabels(bool show)
```
Toggles the visibility of row labels.

#### SetShowValueLabels
```cpp
void SetShowValueLabels(bool show)
```
Toggles the visibility of value labels on the bars.

### Rendering

#### RenderChart
```cpp
void RenderChart(IRenderContext* ctx) override
```
Main rendering method that draws the complete chart based on the selected style.

### Interaction

#### HandleChartMouseMove
```cpp
bool HandleChartMouseMove(const Point2Di& mousePos)
```
Handles mouse movement for hover effects and tooltips.

#### HandleMouseClick
```cpp
bool HandleMouseClick(const Point2Di& mousePos)
```
Handles mouse click events on chart elements.

### Callbacks

```cpp
std::function<void(int rowIndex, const std::string& category)> onBarClick;
std::function<void(int rowIndex, const std::string& category)> onBarHover;
```
Callback functions for bar interaction events.

## Factory Functions

### CreateDivergingBarChart
```cpp
std::shared_ptr<UltraCanvasDivergingBarChart> CreateDivergingBarChart(
    const std::string& id, int x, int y, int width, int height)
```
Creates a standard diverging bar chart instance.

### CreatePopulationPyramid
```cpp
std::shared_ptr<UltraCanvasDivergingBarChart> CreatePopulationPyramid(
    const std::string& id, int x, int y, int width, int height,
    const std::vector<std::string>& rowLabels,
    const std::vector<DivergingCategory>& categories)
```
Creates a pre-configured population pyramid chart.

### CreateLikertChart
```cpp
std::shared_ptr<UltraCanvasDivergingBarChart> CreateLikertChart(
    const std::string& id, int x, int y, int width, int height)
```
Creates a pre-configured Likert scale chart.

## Private Properties

### Visual Properties
- `barHeight`: Height of bars as percentage of row height (default: 0.8f)
- `barSpacing`: Vertical spacing between bars (default: 0.2f)
- `centerGap`: Gap at the center axis in pixels (default: 10.0f)
- `showCenterLine`: Show vertical line at center (default: true)
- `centerLineColor`: Color of center line (default: gray)
- `centerLineWidth`: Width of center line (default: 1.0f)

### Labels and Text
- `showRowLabels`: Display row labels (default: true)
- `showValueLabels`: Display value labels (default: false)
- `labelFontSize`: Font size for labels (default: 10.0f)
- `labelColor`: Color for labels (default: dark gray)

### Interaction State
- `hoveredRowIndex`: Currently hovered row index (-1 if none)
- `hoveredCategory`: Currently hovered category name

### Animation
- `enableAnimation`: Enable/disable animations (default: true)
- `animationProgress`: Current animation progress (0.0 to 1.0)
- `animationStartTime`: Start time of current animation

### Cached Calculations
- `maxNegativeValue`: Maximum value on negative side
- `maxPositiveValue`: Maximum value on positive side
- `needsRecalculation`: Flag indicating data bounds need recalculation

## Usage Examples

### Creating a Likert Scale Chart

```cpp
// Create Likert scale categories
std::vector<DivergingCategory> categories;
categories.emplace_back("Strongly Disagree", Color(255, 80, 51, 255), false);
categories.emplace_back("Disagree", Color(244, 165, 130, 255), false);
categories.emplace_back("Neutral", Color(220, 220, 220, 255), false);
categories.emplace_back("Agree", Color(4, 209, 214, 255), true);
categories.emplace_back("Strongly Agree", Color(33, 102, 172, 255), true);

// Create the chart
auto likertChart = CreateLikertChart("likert", 50, 50, 700, 450);
likertChart->SetCategories(categories);
likertChart->SetChartTitle("Survey Responses");
likertChart->SetBarHeight(0.85f);
likertChart->SetCenterGap(5.0f);

// Add data rows
std::map<std::string, float> responses;
responses["Strongly Disagree"] = 5;
responses["Disagree"] = 12;
responses["Neutral"] = 8;
responses["Agree"] = 25;
responses["Strongly Agree"] = 15;
likertChart->AddDataRow("Question 1", responses);

// Add click handler
likertChart->onBarClick = [](int row, const std::string& category) {
    std::cout << "Clicked row " << row << ", category: " << category << std::endl;
};
```

### Creating a Population Pyramid

```cpp
// Define age group categories
std::vector<DivergingCategory> categories;
categories.emplace_back("Male", Color(100, 150, 200, 255), false);
categories.emplace_back("Female", Color(200, 100, 150, 255), true);

// Create the pyramid
auto pyramid = CreatePopulationPyramid("pyramid", 1002, 50, 50, 600, 500,
                                        {"0-9", "10-19", "20-29", "30-39", "40-49"},
                                        categories);

// Add population data
pyramid->AddDataRow("0-9", {{"Male", 1200}, {"Female", 1150}});
pyramid->AddDataRow("10-19", {{"Male", 1350}, {"Female", 1300}});
pyramid->AddDataRow("20-29", {{"Male", 1500}, {"Female", 1450}});
pyramid->AddDataRow("30-39", {{"Male", 1400}, {"Female", 1420}});
pyramid->AddDataRow("40-49", {{"Male", 1250}, {"Female", 1280}});

// Enable grid and labels
pyramid->SetGridEnabled(true);
pyramid->SetShowRowLabels(true);
pyramid->SetShowValueLabels(true);
```

### Creating a Tornado Chart

```cpp
auto tornadoChart = CreateDivergingBarChart("tornado", 50, 50, 700, 400);
tornadoChart->SetChartStyle(DivergingChartStyle::TornadoChart);

// Define impact categories
std::vector<DivergingCategory> categories;
categories.emplace_back("Negative Impact", Color(255, 100, 100, 255), false);
categories.emplace_back("Positive Impact", Color(100, 200, 100, 255), true);
tornadoChart->SetCategories(categories);

// Add sensitivity analysis data
tornadoChart->AddDataRow("Variable A", {{"Negative Impact", -25}, {"Positive Impact", 35}});
tornadoChart->AddDataRow("Variable B", {{"Negative Impact", -15}, {"Positive Impact", 20}});
tornadoChart->AddDataRow("Variable C", {{"Negative Impact", -30}, {"Positive Impact", 10}});
```

## Rendering Styles

### Population Pyramid
- Symmetric bars extending horizontally from a central axis
- Categories are stacked within each bar
- Ideal for demographic data comparison

### Likert Scale
- Stacked segments diverging from center
- Each category is represented as a segment
- Perfect for survey response visualization

### Tornado Chart
- Single bars extending left or right from center based on net value
- Shows the impact or sensitivity of different variables
- Useful for risk analysis and sensitivity studies

### Opposing Bars
- Two separate bars meeting at the center
- Clear distinction between positive and negative values
- Good for simple binary comparisons

## Best Practices

1. **Data Organization**: Ensure all row labels are unique and descriptive
2. **Category Colors**: Choose contrasting colors for opposing categories
3. **Bar Height**: Use 0.7-0.9 for optimal visual balance
4. **Center Gap**: Keep minimal (5-15 pixels) for better visual connection
5. **Animation**: Enable for initial render, disable for real-time updates
6. **Tooltips**: Always enable for better data exploration
7. **Grid Lines**: Enable when precise value comparison is needed

## Performance Considerations

- The chart caches calculated bounds and only recalculates when data changes
- Animation uses ease-out cubic interpolation for smooth transitions
- Hover detection is optimized to minimize redraw calls
- Large datasets (>100 rows) may benefit from disabling animations

## Dependencies

- `UltraCanvasChartElementBase`: Base class for chart elements
- `UltraCanvasChartDataStructures`: Chart data structures and interfaces
- `UltraCanvasTooltipManager`: Tooltip display functionality
- `IRenderContext`: Rendering context interface
- Standard C++ libraries: `<vector>`, `<map>`, `<algorithm>`, `<numeric>`, `<cmath>`, `<cstdio>`

## Notes

- The chart automatically adjusts the plot area to accommodate labels and legends
- Data bounds are calculated based on the maximum values in each direction
- The rendering order ensures grid lines appear behind chart elements
- Mouse interaction is handled through the base class event system

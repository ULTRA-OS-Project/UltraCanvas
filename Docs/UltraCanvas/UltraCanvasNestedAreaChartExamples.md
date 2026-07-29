# UltraCanvasNestedAreaChart Documentation

## Overview

The `UltraCanvasNestedAreaChart` is a chart component in the UltraCanvas framework for comparing the magnitude of a small set of values at a glance. Each value is drawn as a shape (square, circle, or rounded square) whose **area** is proportional to the value, and all shapes are layered on top of each other sharing a common anchor point. This makes relative size differences immediately visible — the classic "how big is X compared to Y" infographic.

**Version:** 1.2.0
**Last Modified:** 2026-07-29
**Author:** UltraCanvas Framework
**Namespace:** `UltraCanvas`

## Features

- **Area-true scaling**: Shape side/radius is computed as `sqrt(value / maxValue)`, so the *area* (not the side length) is proportional to the value — the mathematically correct way to compare magnitudes visually
- **Three shape modes**: Rectangle, Circle, and RoundedRect
- **Seven alignment modes**: shapes can share a corner (BottomLeft, BottomRight, TopLeft, TopRight), an edge midpoint (BottomCenter, TopCenter), or be concentric (Center)
- **14 predefined color themes** plus fully custom palettes
- **Interactive**: hover highlighting, framework tooltips, click selection, and hover/click/select callbacks
- **Labels & legend**: inside/outside labels with automatic contrast color, value formatting with unit suffixes (K/M/B), and a wrapping legend. Label positions are solved by the shared `PlaceShapeLabels()` solver (`UltraCanvasLabelPlacement.h`), which keeps the labels of closely sized nested shapes from overlapping each other; the smallest (most cramped) shapes get placement priority
- **Reference grid**: optional area-scale reference outlines
- **Fluent builder**: `NestedAreaChartBuilder` for compact, declarative chart construction

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasNestedAreaChart
```

## File Structure

```
UltraCanvas/
├── include/
│   └── Plugins/
│       └── Charts/
│           └── UltraCanvasNestedAreaChart.h
└── Plugins/
    └── Charts/
        └── UltraCanvasNestedAreaChart.cpp
```

## Data Structures

### NestedAreaDataPoint Structure

```cpp
struct NestedAreaDataPoint {
    std::string label;      // Display label
    double value;           // Value (area will be proportional to this)
    Color color;            // Explicit color override (Transparent = use theme)
    std::string tooltip;    // Custom tooltip text (optional)
    std::string category;   // Category grouping (shown in tooltip)
    std::string unit;       // Unit of measurement (e.g. "km²", "T$", "B")

    NestedAreaDataPoint(const std::string& lbl, double val);
    NestedAreaDataPoint(const std::string& lbl, double val, const Color& col);
    NestedAreaDataPoint(const std::string& lbl, double val, const Color& col,
                        const std::string& tip);
};
```

### NestedAreaDataSource Class

A specialized `IChartDataSource` that stores the nested area points. It is created automatically by the chart and registered with the base-class chart pipeline, so the common empty-state handling, background, and title rendering all work as with every other UltraCanvas chart.

### Enums

```cpp
enum class NestedAreaShapeMode {
    Rectangle,      // Nested squares
    Circle,         // Nested circles (bubble style)
    RoundedRect     // Rounded corner squares
};

enum class NestedAreaAlignmentMode {
    BottomLeft,     // Shapes align to bottom-left corner (default)
    BottomRight,    // Shapes align to bottom-right corner
    TopLeft,        // Shapes align to top-left corner
    TopRight,       // Shapes align to top-right corner
    Center,         // Shapes align to center (concentric)
    BottomCenter,   // Shapes align to bottom-center
    TopCenter       // Shapes align to top-center
};

enum class NestedAreaLabelPosition {
    Inside,         // Label inside the shape (default)
    Outside,        // Label outside the shape
    Tooltip,        // Label shown on hover only
    None            // No labels
};

enum class NestedAreaColorTheme {
    Default,        // Blue sequential gradient
    Categorical,    // Distinct colors for comparison
    Sequential,     // Light-to-dark single hue
    Warm, Cool, Earth, Pastel, Vibrant, Monochrome,
    Ocean, Sunset, Forest, Corporate,
    Colorblind,     // Accessible palette (deuteranopia/protanopia safe)
    Custom          // User-defined colors (see SetCustomColors)
};
```

## API Reference

### Construction

```cpp
// Direct construction
auto chart = std::make_shared<UltraCanvasNestedAreaChart>("myChart", x, y, width, height);

// Factory function
auto chart = CreateNestedAreaChartElement("myChart", x, y, width, height);
```

### Data Management

| Method | Description |
|--------|-------------|
| `void AddDataPoint(const NestedAreaDataPoint& point)` | Add a fully specified data point |
| `void AddDataPoint(const std::string& label, double value)` | Add a labeled value |
| `void AddDataPoint(const std::string& label, double value, const Color& color)` | Add a labeled value with an explicit color |
| `void SetDataPoints(const std::vector<NestedAreaDataPoint>& points)` | Replace all data points |
| `void ClearDataPoints()` | Remove all data points |
| `size_t GetDataPointCount() const` | Number of data points |
| `const NestedAreaDataPoint& GetDataPoint(size_t index) const` | Read a data point |
| `NestedAreaDataPoint& GetDataPointMutable(size_t index)` | Modify a data point (invalidates layout) |

### Configuration

| Method | Description |
|--------|-------------|
| `void SetShapeMode(NestedAreaShapeMode mode)` | Rectangle / Circle / RoundedRect |
| `void SetAlignmentMode(NestedAreaAlignmentMode mode)` | Anchor corner / center |
| `void SetColorTheme(NestedAreaColorTheme theme)` | One of the 14 predefined themes |
| `void SetCustomColors(const std::vector<Color>& colors)` | Custom palette (sets theme to `Custom`) |
| `void SetLabelPosition(NestedAreaLabelPosition position)` | Inside / Outside / Tooltip / None |
| `void SetShowLabels(bool show)` / `SetShowValues(bool show)` | Toggle name / value text |
| `void SetShowLegend(bool show)` | Toggle the legend strip below the shapes |
| `void SetShowBorders(bool show)` / `SetBorderWidth(float)` / `SetBorderColor(const Color&)` | Shape outlines |
| `void SetCornerRadius(float radius)` | Corner radius for `RoundedRect` mode |
| `void SetChartPadding(float pad)` | Inset between the element border and the shapes |
| `void SetShowGridLines(bool show)` / `SetGridLineCount(int count)` | Area-scale reference outlines |
| `void SetChartTitle(const std::string& title)` | Title drawn above the chart (inherited from base) |
| `void SetEnableTooltips(bool enable)` | Framework tooltips on hover (inherited from base) |

Note: `SetChartPadding` controls the drawing inset and is intentionally distinct from `UltraCanvasUIElement::SetPadding`, which sets the CSS box-model padding.

### Selection & Interaction

| Member | Description |
|--------|-------------|
| `int GetHoveredIndex() const` | Currently hovered data index, -1 if none |
| `int GetSelectedIndex() const` | Currently selected data index, -1 if none |
| `void SetSelectedIndex(int index)` | Select programmatically (fires `onShapeSelect`) |
| `void ClearSelection()` | Clear selection (fires `onSelectionClear`) |
| `std::function<void(size_t)> onShapeHover` | Called when the pointer enters a shape |
| `std::function<void(size_t)> onShapeClick` | Called when a shape is left-clicked |
| `std::function<void(size_t)> onShapeSelect` | Called when a shape becomes selected |
| `std::function<void()> onSelectionClear` | Called when the selection is cleared |

Clicking a shape toggles its selection. Clicking the currently selected shape deselects it.

## Usage Examples

### Quick Start

```cpp
#include "Plugins/Charts/UltraCanvasNestedAreaChart.h"

using namespace UltraCanvas;

auto chart = CreateNestedAreaChartElement("countryChart", 10, 10, 480, 360);
chart->SetChartTitle("Country Land Area (M km²)");
chart->SetShapeMode(NestedAreaShapeMode::Rectangle);
chart->SetAlignmentMode(NestedAreaAlignmentMode::BottomLeft);
chart->SetColorTheme(NestedAreaColorTheme::Default);

chart->AddDataPoint("Russia", 17.1);
chart->AddDataPoint("Canada", 10.0);
chart->AddDataPoint("USA",     9.8);
chart->AddDataPoint("China",   9.6);

window->AddChild(chart);
```

### Rich Data Points

```cpp
std::vector<NestedAreaDataPoint> data = {
    {"Apple",     3.0},
    {"Microsoft", 2.8},
    {"Alphabet",  1.7},
};
for (auto& p : data) {
    p.unit = "T$";                 // Shown after formatted values
    p.category = "Market Cap";     // Shown in the tooltip
}
chart->SetDataPoints(data);
```

### Builder Pattern

```cpp
auto chart = NestedAreaChartBuilder()
    .SetIdentifier("populationChart")
    .SetPosition(10, 400)
    .SetSize(480, 330)
    .SetTitle("World Population (B)")
    .SetShapeMode(NestedAreaShapeMode::RoundedRect)
    .SetAlignmentMode(NestedAreaAlignmentMode::BottomLeft)
    .SetColorTheme(NestedAreaColorTheme::Sunset)
    .AddData("World", 8.0)
    .AddData("Asia",  4.7)
    .AddData("China", 1.4)
    .AddData("EU",    0.45)
    .ShowLegend(true)
    .Build();
```

### Interaction Callbacks

```cpp
chart->onShapeClick = [chart, statusLabel](size_t index) {
    const auto& point = chart->GetDataPoint(index);
    statusLabel->SetText("Clicked: " + point.label);
};

chart->onShapeHover = [](size_t index) {
    // e.g. highlight a related table row
};
```

### Custom Colors

```cpp
chart->SetCustomColors({
    Color(0, 120, 215, 255),
    Color(0, 90, 160, 255),
    Color(0, 60, 110, 255),
});
```

## How the Scaling Works

To compare magnitudes honestly, the *area* of each shape must be proportional to its value. Because the area of a square is `side²` and of a circle `π·r²`, the chart computes:

```
side   = sqrt(value / maxValue) * maxDimension        // rectangles
radius = sqrt(value / maxValue) * (maxDimension / 2)  // circles
```

A value that is half of the maximum therefore gets a shape with half the *area* (not half the width), which is what the human eye reads as "half as much".

## Demo Application

The Demo App page **Charts → Nested Chart** (`Apps/DemoApp/UltraCanvasNestedAreaChartExamples.cpp`) shows:

1. **Rectangle Mode** – country land areas anchored bottom-left (the classic infographic style)
2. **Circle Mode** – company market capitalizations as concentric circles
3. **Theme Showcase** – world population with rounded rectangles, the Sunset theme, and reference grid lines

Interactive dropdowns switch the color theme and shape mode of the showcase chart and the alignment of all three charts. Clicking a shape reports it in the status line; hovering shows tooltips.

## Best Practices

- **Keep the data set small** — nested area charts read best with 3–8 values; beyond that the inner shapes become indistinguishable
- **Sort is automatic** — shapes are always rendered largest-first, so insertion order does not matter
- **Use `Categorical` or `Colorblind` themes** when the values are unrelated items; use sequential themes (`Default`, `Sequential`, `Ocean`, …) when they form a natural ordering
- **Add units** (`point.unit`) so labels, legend and tooltips read "9.8 M km²" instead of a bare number
- **Prefer `Center` alignment with `Circle` mode** — concentric circles are the most familiar form of this chart
- **Turn labels off for tiny charts** and rely on the legend or tooltips (`SetShowLabels(false)`, `SetLabelPosition(NestedAreaLabelPosition::Tooltip)`)

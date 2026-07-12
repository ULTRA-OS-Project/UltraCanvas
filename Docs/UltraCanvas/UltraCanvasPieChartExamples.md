# UltraCanvasPieChartElement Documentation

## Overview

The `UltraCanvasPieChartElement` is a comprehensive pie / donut / 3D chart component within the UltraCanvas framework. It renders proportional categorical data with support for donut (ring) mode, slice explosion, radial gradients, full 3D extrusion with lighting, and an advanced labeling system (inside / outside / edge / leader-lined). Per-slice height extrusion allows individual slices to be visually raised in 3D mode to highlight key categories.

**Namespace:** `UltraCanvas`  
**Header:** `include/Plugins/Charts/UltraCanvasPieChart.h`  
**Base Class:** `UltraCanvasChartElementBase`  
**Version:** 1.0.0  
**Last Modified:** 2026-05-14  
**Author:** UltraCanvas Framework

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasPieChartElement
```

## Features

### Core Capabilities
- **Pie & Donut Modes:** Toggle between filled pie and ring/donut visualization with adjustable inner radius
- **Slice Explosion:** Both global and per-slice radial offset to emphasize individual slices
- **3D Mode:** Depth extrusion with configurable perspective angle and directional lighting
- **Per-Slice Height:** Raise individual slices above the base depth to highlight them (3D mode only)
- **Radial Gradients:** Auto-generated or custom per-slice gradient fills
- **Slice Patterns:** Optional `IPaintPattern` fills per slice
- **Advanced Labels:** Inside, outside, edge, auto, or none — with leader lines, custom fonts, and formatters
- **Interactive Tooltips:** Hover-driven slice highlighting and tooltips
- **Export:** `QuickExport` and `ExportToFile` for saving the rendered chart

## Enumerations

### LabelPosition
Controls where slice labels are drawn.

```cpp
enum class LabelPosition {
    None,
    Inside,
    Outside,
    Edge,
    Auto
};
```

### LabelContent
Controls what each slice label displays.

```cpp
enum class LabelContent {
    Name,
    Value,
    Percentage,
    NameValue,
    NamePercentage,
    ValuePercentage,
    All
};
```

## Constructor

```cpp
UltraCanvasPieChartElement(const std::string& id, int x, int y, int w, int h)
```

### Parameters
- **id** — Unique string identifier for the chart element
- **x** — X-coordinate position
- **y** — Y-coordinate position
- **w** — Width of the chart
- **h** — Height of the chart

### Default Settings
- `borderColor` = `Colors::White`
- `borderWidth` = `1.5f`
- `donutMode` = `false`
- `innerRadiusFraction` = `0.45f`
- `labelPosition` = `LabelPosition::Auto`
- `labelContent` = `LabelContent::NamePercentage`
- `leaderLinesEnabled` = `true`
- `enable3D` = `false`
- `depthHeight` = `25.0f`
- `perspectiveAngleDeg` = `20.0f`
- `ambientLight` = `0.4f`, `diffuseLight` = `0.6f`

## Public Methods

### Palette & Borders

```cpp
void SetColorPalette(const std::vector<Color>& palette);
const std::vector<Color>& GetColorPalette() const;
void SetBorderColor(const Color& c);
void SetBorderWidth(float w);
```

### Donut / Ring

```cpp
void SetDonutMode(bool on);
bool GetDonutMode() const;
void SetInnerRadius(float fraction);    // fraction of outer radius (0..1)
float GetInnerRadius() const;
```

### Explosion

```cpp
void SetGlobalExplosion(float fraction);
float GetGlobalExplosion() const;
void SetSliceExplosion(size_t index, float fraction);
void ClearSliceExplosion(size_t index);
void ClearAllSliceExplosions();
```

### Per-Slice Height (3D Extrusion)

`heightFactor` of 1.0 = base depth (no change). Values > 1.0 raise the slice above its neighbours. Only takes effect when 3D mode is enabled.

```cpp
void SetSliceHeight(size_t index, float heightFactor);
float GetSliceHeight(size_t index) const;
void ClearSliceHeight(size_t index);
void ClearAllSliceHeights();
```

### Labels

```cpp
void SetLabelPosition(LabelPosition p);
LabelPosition GetLabelPosition() const;
void SetLabelContent(LabelContent c);
LabelContent GetLabelContent() const;
void SetLeaderLinesEnabled(bool on);
bool GetLeaderLinesEnabled() const;
void SetLeaderLineStyle(const Color& c, float w);
void SetLabelFont(const std::string& family, float size, FontWeight weight);
void SetLabelColor(const Color& c);
void SetLabelBackgroundEnabled(bool on);
void SetLabelBackgroundColor(const Color& c);
void SetPercentageFormatter(std::function<std::string(double pct)> f);
void SetValueFormatter(std::function<std::string(double v)> f);
```

### Fills

```cpp
void SetSliceGradient(size_t index, const std::vector<GradientStop>& stops);
void SetAutoRadialGradients();
void ClearSliceGradients();
void SetSlicePattern(size_t index, std::shared_ptr<IPaintPattern> pattern);
void ClearSlicePatterns();
```

### 3D Mode

```cpp
void Enable3DMode(float depthHeight, float perspectiveAngleDeg);
void Disable3DMode();
bool Is3DEnabled() const;

void SetPerspectiveAngle(float angleDeg);
float GetPerspectiveAngle() const;
void SetDepthHeight(float h);
float GetDepthHeight() const;
void SetLightDirection(Point2Df dir);
void SetAmbientLight(float a);
void SetDiffuseLight(float d);
```

`SetPerspectiveAngle` and `SetDepthHeight` are independent tuning knobs — you can adjust either without having to re-issue `Enable3DMode`.

### Tooltips

```cpp
void SetTooltipsEnabled(bool on);   // forwards to SetEnableTooltips()
```

### Export

```cpp
bool QuickExport(const std::string& filename);
bool ExportToFile(const std::string& filename, int w, int h);
```

## Factory Function

```cpp
std::shared_ptr<UltraCanvasPieChartElement> CreatePieChartElement(
    const std::string& id, int x, int y, int w, int h);
```

## Usage Examples

All examples below are drawn from `Apps/DemoApp/UltraCanvasPieChartExamples.cpp`.

### Sample Data Setup

```cpp
std::shared_ptr<ChartDataVector> GenerateMarketShareData() {
    auto data = std::make_shared<ChartDataVector>();
    std::vector<ChartDataPoint> marketShare = {
            ChartDataPoint(1, 350, 0, "Product A", 350),
            ChartDataPoint(2, 280, 0, "Product B", 280),
            ChartDataPoint(3, 210, 0, "Product C", 210),
            ChartDataPoint(4, 160, 0, "Product D", 160),
            ChartDataPoint(5, 120, 0, "Others", 120)
    };
    data->LoadFromArray(marketShare);
    return data;
}

std::shared_ptr<ChartDataVector> GenerateBudgetData() {
    auto data = std::make_shared<ChartDataVector>();
    std::vector<ChartDataPoint> budget = {
            ChartDataPoint(1, 4500, 0, "Salaries", 4500),
            ChartDataPoint(2, 2300, 0, "Marketing", 2300),
            ChartDataPoint(3, 1800, 0, "R&D", 1800),
            ChartDataPoint(4, 1200, 0, "Operations", 1200),
            ChartDataPoint(5, 800, 0, "Infrastructure", 800),
            ChartDataPoint(6, 400, 0, "Other", 400)
    };
    data->LoadFromArray(budget);
    return data;
}
```

### Example 1: Market Share Pie Chart (Auto-Labeled)

```cpp
auto marketChart = CreatePieChartElement("MarketPieChart", 240, 160, 460, 290);
marketChart->SetDataSource(GenerateMarketShareData());
marketChart->SetColorPalette({
        Color(54, 162, 235, 255),   // Blue
        Color(255, 99, 132, 255),   // Red
        Color(255, 205, 86, 255),   // Yellow
        Color(75, 192, 192, 255),   // Teal
        Color(153, 102, 255, 255)   // Purple
});
marketChart->SetBorderColor(Colors::White);
marketChart->SetBorderWidth(2.0f);
marketChart->SetLabelContent(LabelContent::NamePercentage);
marketChart->SetLabelPosition(LabelPosition::Auto);
marketChart->SetTooltipsEnabled(true);
container->AddChild(marketChart);
```

### Example 2: Budget Allocation with Outside Labels and Leader Lines

```cpp
auto budgetChart = CreatePieChartElement("BudgetPieChart", 720, 160, 460, 290);
budgetChart->SetDataSource(GenerateBudgetData());
budgetChart->SetColorPalette({
        Color(255, 159, 64, 255),   // Orange
        Color(54, 162, 235, 255),   // Blue
        Color(255, 99, 132, 255),   // Red
        Color(75, 192, 192, 255),   // Teal
        Color(153, 102, 255, 255),  // Purple
        Color(199, 199, 199, 255)   // Grey
});
budgetChart->SetBorderColor(Colors::White);
budgetChart->SetBorderWidth(2.0f);
budgetChart->SetLabelContent(LabelContent::NameValue);
budgetChart->SetLabelPosition(LabelPosition::Outside);
budgetChart->SetLeaderLinesEnabled(true);
budgetChart->SetTooltipsEnabled(true);
container->AddChild(budgetChart);
```

### Example 3: 3D Donut with Highlighted Slice and Radial Gradients

```cpp
auto donutChart = CreatePieChartElement("DonutChart", 240, 500, 460, 290);
donutChart->SetDataSource(GenerateMarketShareData());
donutChart->SetColorPalette({
        Color(255, 99, 132, 255),
        Color(54, 162, 235, 255),
        Color(255, 205, 86, 255),
        Color(75, 192, 192, 255),
        Color(153, 102, 255, 255)
});

// Enable 3D mode with custom depth + perspective and configure lighting
donutChart->Enable3DMode(30.0f, 22.0f);
donutChart->SetLightDirection(Point2Df(-0.6f, -0.8f));
donutChart->SetAmbientLight(0.35f);
donutChart->SetDiffuseLight(0.65f);

// Enable donut (ring) mode
donutChart->SetDonutMode(true);
donutChart->SetInnerRadius(0.45f);

// Auto-generate radial gradients for every slice
donutChart->SetAutoRadialGradients();

// Explode the first slice outward
donutChart->SetSliceExplosion(0, 0.18f);

// Highlight the largest slice by raising it 60% above the others
// (per-slice height extrusion only takes effect in 3D mode)
donutChart->SetSliceHeight(0, 1.6f);

// Labels with leader lines
donutChart->SetLabelContent(LabelContent::NamePercentage);
donutChart->SetLabelPosition(LabelPosition::Outside);
donutChart->SetLeaderLinesEnabled(true);
donutChart->SetLeaderLineStyle(Color(100, 100, 100, 255), 1.5f);
donutChart->SetBorderColor(Colors::White);
donutChart->SetBorderWidth(2.5f);
donutChart->SetTooltipsEnabled(true);

container->AddChild(donutChart);
```

### Example 4: Stylized Pie Chart with Bold Labels

```cpp
auto styledChart = CreatePieChartElement("StyledChart", 720, 500, 460, 290);
styledChart->SetDataSource(GenerateBudgetData());
styledChart->SetColorPalette({
        Color(255, 159, 64, 255),
        Color(54, 162, 235, 255),
        Color(255, 99, 132, 255),
        Color(75, 192, 192, 255),
        Color(153, 102, 255, 255),
        Color(199, 199, 199, 255)
});

// Showcase styling: radial gradients, bold labels, thick white borders
styledChart->SetAutoRadialGradients();
styledChart->SetLabelContent(LabelContent::NameValue);
styledChart->SetLabelPosition(LabelPosition::Auto);
styledChart->SetLabelFont("Arial", 12.0f, FontWeight::Bold);
styledChart->SetBorderColor(Color(255, 255, 255, 255));
styledChart->SetBorderWidth(3.0f);
styledChart->SetTooltipsEnabled(true);

container->AddChild(styledChart);
```

### Wiring Live Controls (Donut / 3D / Gradients / Explosion)

The demo wires Sliders, Dropdowns, and Checkboxes to drive the charts at runtime — the same setters can be invoked from any UI:

```cpp
// Toggle donut mode
donutCheckbox->onStateChanged = [pieChart1, pieChart2](CheckedState oldState, CheckedState newState) {
    pieChart1->SetDonutMode(newState == CheckedState::Checked);
    pieChart2->SetDonutMode(newState == CheckedState::Checked);
};

// Toggle 3D effects with cached depth + perspective
enable3DCheckbox->onStateChanged = [pieChart1, pieChart2](CheckedState oldState, CheckedState newState) {
    if (newState == CheckedState::Checked) {
        pieChart1->Enable3DMode(pieChartControls.depthHeight, pieChartControls.perspectiveAngle);
        pieChart2->Enable3DMode(pieChartControls.depthHeight, pieChartControls.perspectiveAngle);
    } else {
        pieChart1->Disable3DMode();
        pieChart2->Disable3DMode();
    }
};

// Toggle auto radial gradients
gradientCheckbox->onStateChanged = [pieChart1, pieChart2](CheckedState oldState, CheckedState newState) {
    if (newState == CheckedState::Checked) {
        pieChart1->SetAutoRadialGradients();
        pieChart2->SetAutoRadialGradients();
    } else {
        pieChart1->ClearSliceGradients();
        pieChart2->ClearSliceGradients();
    }
};

// Global explosion slider (0.0 .. 0.5)
explosionSlider->onValueChanged = [pieChart1, pieChart2](double value) {
    pieChart1->SetGlobalExplosion(static_cast<float>(value));
    pieChart2->SetGlobalExplosion(static_cast<float>(value));
};

// Perspective angle slider (5..70 degrees) — independent of Enable3DMode
perspectiveSlider->onValueChanged = [pieChart1, pieChart2](double value) {
    pieChart1->SetPerspectiveAngle(static_cast<float>(value));
    pieChart2->SetPerspectiveAngle(static_cast<float>(value));
};

// Label position dropdown (Auto / Inside / Outside / Edge / None)
labelPosDropdown->onSelectionChanged = [pieChart1, pieChart2](int index, const DropdownItem& item) {
    LabelPosition pos;
    switch (index) {
        case 0: pos = LabelPosition::Auto; break;
        case 1: pos = LabelPosition::Inside; break;
        case 2: pos = LabelPosition::Outside; break;
        case 3: pos = LabelPosition::Edge; break;
        case 4: pos = LabelPosition::None; break;
        default: pos = LabelPosition::Auto;
    }
    pieChart1->SetLabelPosition(pos);
    pieChart2->SetLabelPosition(pos);
};
```

## Data Source Interface

Pie charts work with any `IChartDataSource`. Each `ChartDataPoint.y` (or `.value`) supplies the magnitude for one slice, and `.label` supplies the slice name displayed in labels and tooltips.

```cpp
struct ChartDataPoint {
    double x, y, z;
    std::string label;
    std::string category;
    double value;
    Color color;
};
```

## Slice Cache and Hit Testing

Internally the chart caches a `Slice` per data point — start/end/mid angles, base color, per-slice explosion and height factor — and uses `HitTestSlice()` to drive hover highlighting and tooltips.

```cpp
struct Slice {
    size_t index;
    std::string name;
    double value;
    double percentage;
    double startAngle;
    double endAngle;
    double midAngle;
    Color baseColor;
    float explosion;
    float heightFactor;
};
```

## Rendering Pipeline

`RenderChart()` dispatches to either the 2D or 3D path based on `Is3DEnabled()`:

1. Compute plot area and slice geometry (`RebuildSlices`, `ComputeCenterAndRadius`)
2. **2D path:** `DrawSlice2D` per slice with optional gradient/pattern fills
3. **3D path:** `DrawSlice3DSides` then `DrawSlice3DTop` with directional lighting
4. `RenderLabels` — places inside / outside / edge labels and draws leader lines when enabled
5. Tooltips and hover indicators on top

## See Also

- [UltraCanvasLineChartElement](UltraCanvasLineChartElement.md) — Line chart component
- [UltraCanvasBarChartElement](UltraCanvasBarChartElement.md) — Bar chart component
- [UltraCanvasScatterPlotElement](UltraCanvasScatterPlotElement.md) — Scatter plot component
- [UltraCanvasAreaChartElement](UltraCanvasAreaChartElement.md) — Area chart component
- [UltraCanvasPopulationChartExamples](UltraCanvasPopulationChartExamples.md) — Population pyramid charts
- [UltraCanvasJitterPlotExamples](UltraCanvasJitterPlotExamples.md) — Jitter / strip plot charts

# UltraCanvasPopulationChart Documentation

## Overview

The `UltraCanvasPopulationChart` is a specialized demographic visualization component within the UltraCanvas framework. It renders **population pyramid** charts — back-to-back horizontal bar charts showing the age and gender distribution of a population. Each age group is drawn as two opposing bars (males to one side of a centre line, females to the other), with optional **surplus highlighting** that shades the imbalance between male and female counts.

**Namespace:** `UltraCanvas`  
**Header:** `include/Plugins/Charts/UltraCanvasPopulationChart.h`  
**Base Class:** `UltraCanvasUIElement`  
**Version:** 1.0.0  
**Last Modified:** 2025-01-19  
**Author:** UltraCanvas Framework

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasPopulationChart
```

Note: Unlike the chart elements in `UltraCanvasSpecificChartElements.h`, the population chart inherits directly from `UltraCanvasUIElement` and manages its own layout, axes, legend and tooltip rendering.

## Features

### Core Capabilities
- **Age-group bars:** Add any number of age groups with male / female counts
- **Automatic surplus calculation:** For each age group the chart computes which side has the surplus and draws an emphasized overlay using the configured surplus color
- **Title, subtitle, axis labels:** Three independent text slots above the plot
- **Configurable axis scale:** Fixed `SetMaxAxisValue()` or auto-scale via `EnableAutoScale()`
- **Grid, axis labels, and centre line:** Each independently toggleable
- **Legend system:** Add arbitrary legend items and choose position by string (`"top-right"`, etc.)
- **Hover interaction:** `HitTest()` and internal hover index drive the tooltip overlay
- **Demographic helpers:** `PopulationChartUtils` namespace ships `FormatPopulation`, `CalculateGenderRatio`, `GenerateAgeLabels`, and `CalculateStatistics`

## Configuration Structs

### PopulationAgeGroup

```cpp
struct PopulationAgeGroup {
    std::string AgeLabel;
    double MalePopulation;
    double FemalePopulation;
    double MaleSurplus;
    double FemaleSurplus;
    int CenterY;

    PopulationAgeGroup();
    PopulationAgeGroup(const std::string& label, double males, double females);
    void CalculateSurplus();   // populates MaleSurplus / FemaleSurplus
};
```

`CalculateSurplus()` runs automatically in the two-argument constructor and writes whichever side is larger into the corresponding surplus field; the other surplus is zeroed.

### PopulationLegendItem

```cpp
struct PopulationLegendItem {
    std::string Label;
    Color ItemColor;

    PopulationLegendItem(const std::string& label, const Color& color);
};
```

### PopulationChartUtils::DemographicStats

Returned by `PopulationChartUtils::CalculateStatistics()`.

```cpp
struct DemographicStats {
    double TotalPopulation;
    double MalePopulation;
    double FemalePopulation;
    double MalePercentage;
    double FemalePercentage;
    double GenderRatio;
    double MedianAgeGroup;
    int YoungestGroupIndex;
    int OldestGroupIndex;
};
```

## Constructor

```cpp
UltraCanvasPopulationChart(const std::string& identifier = "PopulationChart",
                           int x = 0, int y = 0,
                           int w = 600, int h = 700);
```

### Parameters
- **identifier** — Unique string identifier
- **x, y** — Top-left position of the chart
- **w, h** — Width and height

## Public Methods

### Data Management

```cpp
void AddAgeGroup(const std::string& ageLabel,
                 double malePopulation,
                 double femalePopulation);
void ClearAgeGroups();
size_t GetAgeGroupCount() const;
const PopulationAgeGroup& GetAgeGroup(size_t index) const;
```

### Chart Configuration

```cpp
void SetTitle(const std::string& title);
void SetSubtitle(const std::string& subtitle);
std::string GetTitle() const;
std::string GetSubtitle() const;
```

### Axis Configuration

```cpp
void SetAxisLabel(const std::string& label);
void SetMaxAxisValue(double maxValue);
void EnableAutoScale(bool enable);
double GetMaxAxisValue() const;
```

### Color Configuration

```cpp
void SetMaleColor(const Color& color);
void SetFemaleColor(const Color& color);
void SetMaleSurplusColor(const Color& color);
void SetFemaleSurplusColor(const Color& color);
Color GetMaleColor() const;
Color GetFemaleColor() const;

void SetBackgroundColor(const Color& color);
void SetGridColor(const Color& color);
void SetAxisColor(const Color& color);
void SetTextColor(const Color& color);
```

### Display Options

```cpp
void EnableGrid(bool enable);
void EnableAxisLabels(bool enable);
void EnableCenterLine(bool enable);
void SetBarHeight(int height);
void SetBarSpacing(int spacing);
void SetFontSize(int size);
```

### Legend

```cpp
void SetLegendPosition(const std::string& position);   // e.g. "top-right"
void EnableLegend(bool enable);
void AddLegendItem(const std::string& label, const Color& color);
void ClearLegend();
```

### Layout

```cpp
void SetPadding(int left, int right, int top, int bottom);
void SetPlotWidth(int width);
```

### Interaction

```cpp
void EnableInteraction(bool enable);
int HitTest(int x, int y);   // returns age-group index or -1
```

### Aggregate Data Access

```cpp
double GetTotalMalePopulation() const;
double GetTotalFemalePopulation() const;
double GetTotalPopulation() const;
```

### Rendering & Events (Overrides)

```cpp
void Render(IRenderContext* ctx, const Rect2Di& dirtyRect) override;
bool OnEvent(const UCEvent& event) override;
```

## Utility Functions

The `PopulationChartUtils` namespace provides supporting helpers:

```cpp
namespace PopulationChartUtils {
    std::string FormatPopulation(double value);                       // 1.2K / 3.4M / 5.6B
    double CalculateGenderRatio(double males, double females);
    std::vector<std::string> GenerateAgeLabels(int minAge, int maxAge,
                                               int groupSize);
    DemographicStats CalculateStatistics(
            const std::vector<PopulationAgeGroup>& ageGroups);
    Color InterpolateGenderColor(const Color& baseColor,
                                 double intensity);
}
```

## Usage Examples

All examples below are drawn from `Apps/DemoApp/UltraCanvasPopulationChartExamples.cpp`, which builds a side-by-side Germany / Russia comparison.

### Example 1: Germany Population Pyramid (2024)

```cpp
auto germanyChart = std::make_shared<UltraCanvasPopulationChart>(
    "GermanyChart", 20, 75, 480, 640);
germanyChart->SetTitle("Germany");
germanyChart->SetSubtitle("2024");
germanyChart->SetAxisLabel("Population (millions)");
germanyChart->SetMaxAxisValue(4.0);

// Configure colors (base male/female + surplus tones)
germanyChart->SetMaleColor(Color(100, 150, 200));
germanyChart->SetFemaleColor(Color(220, 120, 140));
germanyChart->SetMaleSurplusColor(Color(70, 110, 160));
germanyChart->SetFemaleSurplusColor(Color(190, 90, 110));

// Add age groups (real 2024-2025 data, populations in millions)
germanyChart->AddAgeGroup("0-4",   2.0, 1.9);
germanyChart->AddAgeGroup("5-9",   2.1, 2.0);
germanyChart->AddAgeGroup("10-14", 2.1, 2.0);
germanyChart->AddAgeGroup("15-19", 2.0, 1.9);
germanyChart->AddAgeGroup("20-24", 2.3, 2.2);
germanyChart->AddAgeGroup("25-29", 2.8, 2.7);
germanyChart->AddAgeGroup("30-34", 3.0, 2.9);
germanyChart->AddAgeGroup("35-39", 3.2, 3.1);
germanyChart->AddAgeGroup("40-44", 2.9, 2.8);
germanyChart->AddAgeGroup("45-49", 2.8, 2.7);
germanyChart->AddAgeGroup("50-54", 3.4, 3.3);
germanyChart->AddAgeGroup("55-59", 3.6, 3.5);
germanyChart->AddAgeGroup("60-64", 3.0, 3.0);
germanyChart->AddAgeGroup("65-69", 2.7, 2.8);
germanyChart->AddAgeGroup("70-74", 2.2, 2.4);
germanyChart->AddAgeGroup("75-79", 1.8, 2.1);
germanyChart->AddAgeGroup("80-84", 1.2, 1.7);
germanyChart->AddAgeGroup("85-89", 0.7, 1.2);
germanyChart->AddAgeGroup("90-94", 0.2, 0.5);
germanyChart->AddAgeGroup("95-99", 0.05, 0.15);
germanyChart->AddAgeGroup("100+",  0.01, 0.04);

// Display options
germanyChart->EnableGrid(true);
germanyChart->EnableAxisLabels(true);
germanyChart->EnableCenterLine(true);
germanyChart->SetBarSpacing(2);

// Legend
germanyChart->AddLegendItem("Males",          Color(100, 150, 200));
germanyChart->AddLegendItem("Male surplus",   Color(70, 110, 160));
germanyChart->AddLegendItem("Females",        Color(220, 120, 140));
germanyChart->AddLegendItem("Female surplus", Color(190, 90, 110));
germanyChart->EnableLegend(true);
germanyChart->SetLegendPosition("top-right");

container->AddChild(germanyChart);
```

### Example 2: Russia Population Pyramid (2024)

Same colour scheme, wider axis range (7.0 million) to fit the larger absolute population, and a noticeably different age structure:

```cpp
auto russiaChart = std::make_shared<UltraCanvasPopulationChart>(
    "RussiaChart", 510, 75, 480, 640);
russiaChart->SetTitle("Russia");
russiaChart->SetSubtitle("2024");
russiaChart->SetAxisLabel("Population (millions)");
russiaChart->SetMaxAxisValue(7.0);

russiaChart->SetMaleColor(Color(100, 150, 200));
russiaChart->SetFemaleColor(Color(220, 120, 140));
russiaChart->SetMaleSurplusColor(Color(70, 110, 160));
russiaChart->SetFemaleSurplusColor(Color(190, 90, 110));

// Real 2024-2025 data
russiaChart->AddAgeGroup("0-4",   3.8, 3.6);
russiaChart->AddAgeGroup("5-9",   4.2, 4.0);
russiaChart->AddAgeGroup("10-14", 4.0, 3.8);
russiaChart->AddAgeGroup("15-19", 3.5, 3.3);
russiaChart->AddAgeGroup("20-24", 3.8, 3.6);
russiaChart->AddAgeGroup("25-29", 5.5, 5.3);
russiaChart->AddAgeGroup("30-34", 6.2, 6.1);
russiaChart->AddAgeGroup("35-39", 6.3, 6.5);   // female surplus emerges
russiaChart->AddAgeGroup("40-44", 5.2, 5.8);
russiaChart->AddAgeGroup("45-49", 4.5, 5.3);
russiaChart->AddAgeGroup("50-54", 4.8, 6.0);
russiaChart->AddAgeGroup("55-59", 5.0, 6.5);
russiaChart->AddAgeGroup("60-64", 4.2, 5.8);
russiaChart->AddAgeGroup("65-69", 3.5, 5.2);
russiaChart->AddAgeGroup("70-74", 2.8, 4.8);
russiaChart->AddAgeGroup("75-79", 1.8, 3.9);
russiaChart->AddAgeGroup("80-84", 0.9, 2.5);
russiaChart->AddAgeGroup("85-89", 0.4, 1.4);
russiaChart->AddAgeGroup("90-94", 0.1, 0.5);
russiaChart->AddAgeGroup("95-99", 0.03, 0.15);
russiaChart->AddAgeGroup("100+",  0.01, 0.05);

russiaChart->EnableGrid(true);
russiaChart->EnableAxisLabels(true);
russiaChart->EnableCenterLine(true);
russiaChart->SetBarSpacing(2);

russiaChart->AddLegendItem("Males",          Color(100, 150, 200));
russiaChart->AddLegendItem("Male surplus",   Color(70, 110, 160));
russiaChart->AddLegendItem("Females",        Color(220, 120, 140));
russiaChart->AddLegendItem("Female surplus", Color(190, 90, 110));
russiaChart->EnableLegend(true);
russiaChart->SetLegendPosition("top-right");

container->AddChild(russiaChart);
```

### Putting it Together: Side-by-Side Comparison

```cpp
auto container = std::make_shared<UltraCanvasContainer>(
    "PopulationChartContainer", 0, 0, 1000, 780);

auto titleLabel = std::make_shared<UltraCanvasLabel>("TitleLabel", 20, 10, 960, 30);
titleLabel->SetText("Population Pyramid Charts - Germany vs Russia Comparison");
titleLabel->SetFontSize(18);
titleLabel->SetFontWeight(FontWeight::Bold);
titleLabel->SetAlignment(TextAlignment::Center);
container->AddChild(titleLabel);

// ... add germanyChart on the left, russiaChart on the right (see above) ...

auto statsLabel = std::make_shared<UltraCanvasLabel>("StatsLabel", 20, 720, 960, 20);
statsLabel->SetText(
    "Key Statistics: "
    "Germany (83.6M, 49.1% M / 50.9% F, Median Age 45.5) | "
    "Russia (144M, 46.4% M / 53.6% F, Median Age 40.3)");
statsLabel->SetFontSize(11);
statsLabel->SetTextColor(Color(60, 60, 60));
statsLabel->SetAlignment(TextAlignment::Center);
container->AddChild(statsLabel);
```

## Internal Rendering Pipeline

`Render()` walks the standard pyramid layout:

1. **Background** (`RenderBackground`)
2. **Title / subtitle / axis label** (`RenderTitle`)
3. **Grid** (`RenderGrid`) — toggled by `EnableGrid()`
4. **Axes** (`RenderAxes`) — toggled by `EnableAxisLabels()`
5. **Age-group bars** (`RenderAgeGroups` → `RenderAgeGroup` → `DrawHorizontalBar`)
   - Base male/female bar drawn first
   - Surplus overlay drawn on top of the longer side using the surplus color
6. **Center line** (`RenderCenterLine`) — toggled by `EnableCenterLine()`
7. **Legend** (`RenderLegend`)
8. **Tooltip** (`RenderTooltip`) — when `hoveredGroupIndex >= 0`

Layout values (centerX, plot width, padding) are recomputed by `CalculateLayout()` when the `layoutDirty` flag is set.

## See Also

- [UltraCanvasBarChartElement](UltraCanvasBarChartElement.md) — Standard vertical bar chart
- [UltraCanvasDivergingChartExamples](UltraCanvasDivergingChartExamples.md) — Related diverging bar visualization
- [UltraCanvasPieChartExamples](UltraCanvasPieChartExamples.md) — Pie / donut / 3D chart
- [UltraCanvasJitterPlotExamples](UltraCanvasJitterPlotExamples.md) — Categorical distribution plots
- [UltraCanvasLineChartElement](UltraCanvasLineChartElement.md) — Line chart component

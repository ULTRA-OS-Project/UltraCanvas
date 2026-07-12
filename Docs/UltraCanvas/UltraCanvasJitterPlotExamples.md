# UltraCanvasJitterPlotElement Documentation

## Overview

The `UltraCanvasJitterPlotElement` is a categorical distribution chart for the UltraCanvas framework. It renders **jitter (strip) plots** — every individual observation is drawn as a point, with horizontal offset (jitter) used to reveal the local density of each category. The element supports both random-jitter algorithms (uniform / Gaussian) and **beeswarm packing** (swarm / center / hex / square) for non-overlapping layouts, plus hybrid overlays that combine jitter with box plots, bar charts, violin plots, or raincloud plots.

**Namespace:** `UltraCanvas`  
**Header:** `include/Plugins/Charts/UltraCanvasJitterPlotElement.h`  
**Base Class:** `UltraCanvasChartElementBase`  
**Version:** 1.1.3  
**Last Modified:** 2026-05-09  
**Author:** UltraCanvas Framework

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasJitterPlotElement
```

## Features

### Core Capabilities
- **Two jitter algorithms:** `JitterDistribution::Uniform` (rectangular spread) and `JitterDistribution::Gaussian` (Box-Muller normal)
- **Beeswarm packing:** Four placement methods (`Swarm`, `Center`, `Hex`, `Square`) with priority, corral, and side controls
- **Hybrid modes:** Pure jitter, jitter + box plot, jitter + bar chart, jitter + violin plot, raincloud
- **Statistical overlays:** Median line, mean marker (Diamond / Cross / Circle / Square / Star), quartiles
- **Axis scaling:** Linear, Logarithmic (`SetLogScaleBase`), Symmetric Log
- **Secondary grouping:** Hue variable with per-hue colour map and optional dodge
- **Filtering hooks:** `SetRegionFilter` and `SetMinScoreFilter` for live data subsetting
- **Reproducibility:** `SetJitterSeed()` for deterministic random jitter
- **Tooltips:** Hover values via the inherited `IChartDataSource` tooltip pipeline

## Enumerations

```cpp
enum class JitterDistribution { Uniform, Gaussian, Beeswarm };
enum class JitterRepresentationMode { IndividualPoints, CountMode };
enum class JitterHybridMode {
    JitterOnly, JitterBoxPlot, JitterBarChart,
    JitterViolinPlot, RaincloudPlot
};
enum class StatisticalMarkerStyle {
    MedianLine, MeanPoint, MeanLine, QuartileLines, BoxPlotOverlay
};
enum class AxisScale { Linear, Logarithmic, SymmetricLog };
enum class MeanMarkerShape { Diamond, Cross, Circle, Square, Star };
enum class CategoryLabelPosition { Bottom, Top, Both };

enum class BeeswarmMethod   { Swarm, Center, Hex, Square };
enum class BeeswarmPriority { Ascending, Descending, Random, Compact, Dense };
enum class BeeswarmCorral   { NoneCorral, Open, Wrap, Clamp };
enum class BeeswarmSide     { Both, Left, Right };

// Public nested point shape (used by examples)
enum class UltraCanvasJitterPlotElement::PointShape {
    Circle, Square, Triangle, Diamond
};
```

`BeeswarmCorral::NoneCorral` is named to avoid colliding with the X11 `None` macro (the header `#undef`s `None` for safety).

## Configuration Structs

### JitterCategoryData

```cpp
struct JitterCategoryData {
    std::string categoryName;
    std::vector<double> values;
    std::string hueGroup;     // Secondary grouping variable
    Color customColor;        // Optional per-data colour override

    // Cached statistics (median / mean / Q1 / Q3 / min / max)
    double cachedMedian, cachedMean;
    double cachedQ1, cachedQ3;
    double cachedMin, cachedMax;
    bool statisticsCached;

    JitterCategoryData(const std::string& name, const std::string& hue = "");
};
```

### BeeswarmPoint (Public Inner)

```cpp
struct BeeswarmPoint {
    Point2Df position;     // Final rendered position
    float radius;          // Circle radius
    int originalIndex;     // Index in original data array
    double yValue;         // Original Y value
};
```

## Constructor

```cpp
UltraCanvasJitterPlotElement(const std::string& id,
                              int x, int y, int width, int height);
```

## Public Methods

### Jitter & Representation

```cpp
void SetJitterDistribution(JitterDistribution dist);
void SetJitterAmount(float amount);   // 0.0 .. 1.0
void SetJitterSeed(int seed);         // 0 = random, >0 = reproducible
void SetRepresentationMode(JitterRepresentationMode mode);
void SetHybridMode(JitterHybridMode mode);
```

### Point Appearance

```cpp
void SetPointColor(const Color& color);
void SetPointSize(float size);
void SetPointAlpha(float alpha);      // 0.0 .. 1.0
void SetPointEdgeColor(const Color& color);
void SetPointEdgeWidth(float width);
void SetPointShape(PointShape shape);
```

### Statistical Overlays

```cpp
void SetShowMedianMarker(bool show);
void SetMedianMarkerStyle(const Color& color, float width);
void SetShowMeanMarker(bool show);
void SetMeanMarkerStyle(const Color& color, float size);
void SetMeanMarkerShape(MeanMarkerShape shape);
void SetShowQuartiles(bool show);
void SetQuartileMarkerStyle(const Color& color, float width);
void SetStatisticalMarkerStyle(StatisticalMarkerStyle style);
```

### Axis & Layout

```cpp
void SetYAxisScale(AxisScale scale);
void SetLogScaleBase(double base);
void SetCategoryLabelPosition(CategoryLabelPosition position);
void SetCategories(const std::vector<std::string>& cats);
void SetCategorySpacing(float spacing);
void SetOrientation(bool horizontal);   // false = vertical
```

### Grouping (Hue)

```cpp
void SetHueVariable(const std::string& varName);
void SetHueColorMap(const std::map<std::string, Color>& colorMap);
void SetDodgeEnabled(bool enabled, float width = 0.8f);
```

### Hybrid Overlay Settings

```cpp
void SetBoxPlotSettings(bool showWhiskers, bool showOutliers, float width);
void SetBoxPlotColors(const Color& fill, const Color& border, float borderWidth);

void SetBarChartSettings(const Color& color, float width, bool showValues);

void SetViolinSettings(const Color& fill, const Color& border,
                       float borderWidth, int resolution);
```

### Beeswarm Configuration

```cpp
void SetBeeswarmMethod(BeeswarmMethod method);
void SetBeeswarmPriority(BeeswarmPriority priority);
void SetBeeswarmCorral(BeeswarmCorral corral);
void SetBeeswarmSide(BeeswarmSide side);
void SetBeeswarmSpacing(float spacing);
void SetBeeswarmMaxWidth(float maxWidth);
void SetBeeswarmCompact(bool compact);
```

### Data Loading & Filtering

```cpp
void AddCategoryData(const std::string& category,
                     const std::vector<double>& values,
                     const std::string& hueValue = "");
void ClearData();
void SetRegionFilter(const std::string& region);   // empty = no filter
void SetMinScoreFilter(double minScore);
```

### Background / Grid / Axes

```cpp
void SetBackgroundColor(const Color& color);
void SetGridVisible(bool visible);
void SetGridColor(const Color& color);
void SetAxesVisible(bool visible);
```

### Overrides

```cpp
void Render(IRenderContext* ctx, const Rect2Di& dirtyRect) override;
void RenderChart(IRenderContext* ctx) override;
bool HandleChartMouseMove(const Point2Di& mousePos) override;
std::string GenerateTooltipContent(const ChartDataPoint& point, size_t index) override;
```

## Factory Functions

```cpp
std::shared_ptr<UltraCanvasJitterPlotElement> CreateJitterPlotElement(
    const std::string& id, int x, int y, int width, int height);

std::shared_ptr<UltraCanvasJitterPlotElement> CreateJitterPlotWithCategories(
    const std::string& id, int x, int y, int width, int height,
    const std::vector<std::string>& categories,
    const std::string& title = "");
```

## Usage Examples

All examples below are drawn from `Apps/DemoApp/UltraCanvasJitterPlotExamples.cpp`.

### Example 1: Brazil School Scores — Classic Gaussian Jitter

Demonstrates per-region colouring via a hue variable, Gaussian jitter, median markers and thin white point edges for separating overlapping dots.

```cpp
auto jitter = CreateJitterPlotElement("brazilScoresClassic",
                                      kChartLeftMargin, kChartTopY,
                                      kChartWidth, kChartHeight);

// Categories = Brazilian states, hue variable = region, with colour map
jitter->SetCategories(ds.states);
jitter->SetHueVariable("Region");
jitter->SetHueColorMap(ds.regionalColors);   // map<string,Color>

// Add each state's scores tagged with its region
for (const auto& state : ds.states) {
    jitter->AddCategoryData(state,
                            ds.scoresByState[state],
                            ds.regionByState[state]);
}

// Core jitter settings
jitter->SetJitterDistribution(JitterDistribution::Gaussian);
jitter->SetJitterAmount(0.25f);
jitter->SetPointSize(3.5f);
jitter->SetPointAlpha(0.55f);
jitter->SetPointShape(UltraCanvasJitterPlotElement::PointShape::Circle);

// Thin white edges to separate overlapping dots
jitter->SetPointEdgeColor(Color(255, 255, 255, 180));
jitter->SetPointEdgeWidth(0.5f);

// Light grid + black median line
jitter->SetGridVisible(true);
jitter->SetGridColor(Color(220, 220, 220, 120));
jitter->SetShowMedianMarker(true);
jitter->SetMedianMarkerStyle(Color(0, 0, 0, 255), 2.0f);

jitter->SetChartTitle("Score");
jitter->SetEnableTooltips(true);
```

#### Live Filter Wiring (Region + Min-Score)

```cpp
regionDropdown->onSelectionChanged = [jitter](int index, const DropdownItem& item) {
    std::string region;
    if (index == 1) region = "NorthEast";
    else if (index == 2) region = "North";
    else if (index == 3) region = "CentralWest";
    else if (index == 4) region = "SouthEast";
    else if (index == 5) region = "South";
    jitter->SetRegionFilter(region);
};

applyBtn->SetOnClick([jitter, scoreInput]() {
    try { jitter->SetMinScoreFilter(std::stod(scoreInput->GetText())); }
    catch (...) { jitter->SetMinScoreFilter(0.0); }
});

btnClear->SetOnClick([jitter, scoreInput, regionDropdown]() {
    jitter->SetMinScoreFilter(0.0);
    jitter->SetRegionFilter("");
    scoreInput->SetText("0.0");
    regionDropdown->SetSelectedIndex(0);
});
```

### Example 2: Brazil School Scores — Beeswarm Packing

Same dataset rendered with non-overlapping beeswarm layout, with live dropdowns to switch method / priority / corral / side.

```cpp
auto jitter = CreateJitterPlotElement("brazilScoresBee",
                                      kChartLeftMargin, kChartTopY,
                                      kChartWidth, kChartHeight);
// (same SetCategories / SetHueVariable / SetHueColorMap as Example 1)

// Beeswarm settings
jitter->SetJitterDistribution(JitterDistribution::Beeswarm);
jitter->SetBeeswarmMethod(BeeswarmMethod::Swarm);
jitter->SetBeeswarmPriority(BeeswarmPriority::Ascending);
jitter->SetBeeswarmCorral(BeeswarmCorral::Clamp);
jitter->SetBeeswarmSide(BeeswarmSide::Both);
jitter->SetBeeswarmSpacing(0.5f);
jitter->SetBeeswarmMaxWidth(0.85f);

// Point appearance — slightly larger + edges; alpha can be higher since no overlap
jitter->SetPointSize(4.0f);
jitter->SetPointAlpha(0.85f);
jitter->SetPointShape(UltraCanvasJitterPlotElement::PointShape::Circle);
jitter->SetPointEdgeColor(Color(255, 255, 255, 220));
jitter->SetPointEdgeWidth(0.6f);

jitter->SetGridVisible(true);
jitter->SetGridColor(Color(220, 220, 220, 120));
jitter->SetShowMedianMarker(true);
jitter->SetMedianMarkerStyle(Color(0, 0, 0, 255), 2.0f);
jitter->SetChartTitle("Score");
jitter->SetEnableTooltips(true);
```

#### Live Beeswarm Controls

```cpp
methodDropdown->onSelectionChanged = [jitter](int index, const DropdownItem& item) {
    BeeswarmMethod m = BeeswarmMethod::Swarm;
    if (index == 1) m = BeeswarmMethod::Center;
    else if (index == 2) m = BeeswarmMethod::Hex;
    else if (index == 3) m = BeeswarmMethod::Square;
    jitter->SetBeeswarmMethod(m);
};

prioDropdown->onSelectionChanged = [jitter](int index, const DropdownItem& item) {
    BeeswarmPriority p = BeeswarmPriority::Ascending;
    if (index == 1) p = BeeswarmPriority::Descending;
    else if (index == 2) p = BeeswarmPriority::Random;
    else if (index == 3) p = BeeswarmPriority::Compact;
    else if (index == 4) p = BeeswarmPriority::Dense;
    jitter->SetBeeswarmPriority(p);
};

corralDropdown->onSelectionChanged = [jitter](int index, const DropdownItem& item) {
    BeeswarmCorral c = BeeswarmCorral::NoneCorral;
    if (index == 1) c = BeeswarmCorral::Open;
    else if (index == 2) c = BeeswarmCorral::Wrap;
    else if (index == 3) c = BeeswarmCorral::Clamp;
    jitter->SetBeeswarmCorral(c);
};

sideDropdown->onSelectionChanged = [jitter](int index, const DropdownItem& item) {
    BeeswarmSide s = BeeswarmSide::Both;
    if (index == 1) s = BeeswarmSide::Left;
    else if (index == 2) s = BeeswarmSide::Right;
    jitter->SetBeeswarmSide(s);
};
```

### Example 3: Continental Population (Simple Jitter)

```cpp
auto jitter = CreateJitterPlotElement("continents", 50, 70, 700, 500);

std::vector<std::string> continents = {"Africa", "Americas", "Asia", "Europe", "Oceania"};
jitter->SetCategories(continents);

std::mt19937 gen(123);
struct ContinentData { double mean, stddev; int count; Color color; };
std::map<std::string, ContinentData> contData = {
    {"Africa",   {45.0, 15.0, 80, Color(100, 150, 250, 180)}},
    {"Americas", {55.0, 20.0, 70, Color(255, 180, 100, 180)}},
    {"Asia",     {65.0, 25.0, 90, Color(255, 100, 100, 180)}},
    {"Europe",   {50.0, 12.0, 75, Color(100, 200, 150, 180)}},
    {"Oceania",  {30.0,  8.0, 50, Color(200, 150, 255, 180)}}
};

for (const auto& cont : continents) {
    const auto& data = contData[cont];
    std::normal_distribution<> dist(data.mean, data.stddev);
    std::vector<double> values;
    for (int i = 0; i < data.count; ++i) {
        values.push_back(std::max(10.0, dist(gen)));
    }
    jitter->AddCategoryData(cont, values);
}

jitter->SetJitterAmount(0.3f);
jitter->SetPointSize(4.0f);
jitter->SetPointAlpha(0.6f);
jitter->SetShowMedianMarker(true);
jitter->SetChartTitle("Population (millions)");
jitter->SetEnableTooltips(true);
```

### Example 4: Market Capitalization — Logarithmic Y-Axis

```cpp
auto jitter = CreateJitterPlotElement("marketCap", 50, 70, 900, 550);

std::vector<std::string> marketTiers = {
    "1-Nano", "2-Micro", "3-Small", "4-Mid", "5-Large", "6-Mega"
};
jitter->SetCategories(marketTiers);
jitter->SetYAxisScale(AxisScale::Logarithmic);
jitter->SetLogScaleBase(10.0);

// ... generate log-uniform samples spanning 10 to 10,000,000 ...
for (size_t tier = 0; tier < marketTiers.size(); ++tier) {
    std::vector<double> marketCaps = /* exp(log-uniform samples) */;
    jitter->AddCategoryData(marketTiers[tier], marketCaps);
}

jitter->SetChartTitle("Market Cap ($");
jitter->SetEnableTooltips(true);
```

### Example 5: Hybrid Jitter + Box Plot Overlay (Interactive)

```cpp
auto jitter = CreateJitterPlotElement("boxPlotJitter", 50, 70, 800, 500);

std::vector<std::string> groups = {"Group A", "Group B", "Group C", "Group D"};
jitter->SetCategories(groups);
jitter->SetHybridMode(JitterHybridMode::JitterBoxPlot);

jitter->AddCategoryData("Group A", dataA);   // normal + outliers
jitter->AddCategoryData("Group B", dataB);
jitter->AddCategoryData("Group C", dataC);
jitter->AddCategoryData("Group D", dataD);   // bimodal

// Box plot: show whiskers, show outliers, box width 0.6
jitter->SetBoxPlotSettings(true, true, 0.6f);
jitter->SetBoxPlotColors(
    Color(150, 180, 220, 120),   // fill
    Color( 80, 120, 180, 255),   // border
    2.0f                         // border width
);

jitter->SetJitterAmount(0.15f);
jitter->SetPointSize(4.0f);
jitter->SetPointAlpha(0.6f);
jitter->SetChartTitle("Data Value");
jitter->SetEnableTooltips(true);

// Live toggle buttons
btnToggleWhiskers->SetOnClick([jitter]() {
    static bool showWhiskers = true;
    showWhiskers = !showWhiskers;
    jitter->SetBoxPlotSettings(showWhiskers, true, 0.6f);
});

btnToggleOutliers->SetOnClick([jitter]() {
    static bool showOutliers = true;
    showOutliers = !showOutliers;
    jitter->SetBoxPlotSettings(true, showOutliers, 0.6f);
});
```

### Example 6: Scientific Cross-Mean Markers (Publication Style)

```cpp
auto jitter = CreateJitterPlotElement("muscleData", 50, 70, 700, 500);

std::vector<std::string> species = {"Cheetah", "Impala", "Lion", "Zebra"};
jitter->SetCategories(species);
jitter->SetHybridMode(JitterHybridMode::JitterBoxPlot);

// (populate jitter->AddCategoryData(species[i], measurements) per species)

// Light grey IQR box, dark borders, whiskers
jitter->SetBoxPlotSettings(true, true, 0.5f);
jitter->SetBoxPlotColors(
    Color(235, 235, 235, 180),
    Color(120, 120, 120, 255),
    1.5f
);

// Black cross (+) at the mean of each species
jitter->SetShowMeanMarker(true);
jitter->SetMeanMarkerShape(MeanMarkerShape::Cross);
jitter->SetMeanMarkerStyle(Color(0, 0, 0, 255), 8.0f);

jitter->SetJitterAmount(0.08f);
jitter->SetPointSize(3.5f);
jitter->SetPointAlpha(0.7f);
jitter->SetChartTitle("Peak Power (W/kg)");
jitter->SetEnableTooltips(true);
```

### Example 7: Raincloud Plot (Half-Violin + Jitter)

```cpp
auto jitter = CreateJitterPlotElement("raincloud", 50, 70, 800, 500);

std::vector<std::string> groups = {"Control", "Treatment A", "Treatment B"};
jitter->SetCategories(groups);
jitter->SetHybridMode(JitterHybridMode::RaincloudPlot);

// (populate jitter->AddCategoryData(group, values) per group)

// Violin density: light blue fill, darker outline, 40 KDE samples
jitter->SetViolinSettings(
    Color(180, 200, 220, 100),
    Color( 80, 100, 140, 200),
    1.5f,
    40
);

jitter->SetJitterAmount(0.2f);
jitter->SetPointSize(3.0f);
jitter->SetPointAlpha(0.6f);
jitter->SetChartTitle("Response Value");
jitter->SetEnableTooltips(true);
```

## Internal Pipeline

The element delegates to specialized rendering helpers based on `hybridMode`:

- `RenderJitterPoints` — points (random jitter or beeswarm layout)
- `RenderMedianMarkers`, `RenderMeanMarkers`, `RenderQuartileMarkers` — statistical overlays
- `RenderBoxPlotOverlay`, `RenderBarChartBase`, `RenderViolinOverlay`, `RenderRaincloudPlot` — hybrid overlays
- `RenderJitterAxes`, `RenderRegionBands`, `RenderNationalAverageLine` — axis chrome

Point positions are cached in `pointPositionsCache` (invalidated on data / mode / size changes — see the v1.2.1 cache fix referenced in the demo file) so that hover lookups and tooltips run in `O(1)` per query.

Beeswarm layout is computed by `CalculateBeeswarmLayout()` which dispatches to one of `CalculateBeeswarmSwarm` (greedy), `CalculateBeeswarmCenter`, `CalculateBeeswarmHex`, or `CalculateBeeswarmSquare`. The `priority`, `corral`, and `side` enums shape the placement order and clipping behaviour.

## See Also

- [UltraCanvasScatterPlotElement](UltraCanvasScatterPlotElement.md) — Continuous-vs-continuous scatter plot
- [UltraCanvasBarChartElement](UltraCanvasBarChartElement.md) — Categorical bar chart
- [UltraCanvasLineChartElement](UltraCanvasLineChartElement.md) — Line chart component
- [UltraCanvasPieChartExamples](UltraCanvasPieChartExamples.md) — Pie / donut / 3D chart
- [UltraCanvasPopulationChartExamples](UltraCanvasPopulationChartExamples.md) — Population pyramid charts
- [UltraCanvasBasicChartsExamples](UltraCanvasBasicChartsExamples.md) — Overview of basic chart elements

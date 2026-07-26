# UltraCanvasQuadrantChart Documentation

**Version:** 1.0.0
**Last Modified:** 2026-07-25
**Author:** UltraCanvas Framework

## Overview

`UltraCanvasQuadrantChart` renders interactive 2x2 strategic-analysis matrices. Data points are placed on a two-axis plane that is divided into four quadrants by a configurable center point. The element ships with presets for the most common business frameworks — **SWOT**, **BCG matrix**, **Ansoff growth matrix**, **Eisenhower decision matrix**, **Gartner-style magic quadrant**, **risk matrix** (probability vs impact) and **priority matrix** (impact vs effort) — plus a fully custom mode where every quadrant label, color and axis caption is user-defined.

Points support per-point color, radius (e.g. BCG revenue bubbles), shape (circle, square, triangle, diamond) and outline. The element handles hover highlighting with tooltips, click selection (single or multi), double-click callbacks, and exposes quadrant utilities (which quadrant a point falls into, per-quadrant counts) for building statistics panels.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Charts/UltraCanvasQuadrantChart.h`
**Implementation:** `Plugins/Charts/UltraCanvasQuadrantChart.cpp`
**Base Class:** `UltraCanvasChartElementBase`

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasQuadrantChart
```

## Features

- **Preset chart types:** `QuadrantType::SWOT`, `BCG`, `Ansoff`, `Eisenhower`, `MagicQuadrant`, `RiskMatrix`, `Priority`, `Custom`. Selecting a preset applies matching quadrant labels, quadrant colors and axis captions.
- **Custom quadrants:** `QuadrantDefinition` carries the four labels (multi-line via `\n`) and four background colors; `QuadrantAxisConfiguration` carries the axis captions, the data range (`xMin`..`xMax`, `yMin`..`yMax`, default `0..1`) and the quadrant division point (`xCenter`, `yCenter`).
- **Visual styles:** `QuadrantStyle::Clean`, `Business` (default), `Scientific` (adds a grid), `Modern`.
- **Data points:** label, x/y value, color, radius, shape (`QuadrantPointShape::Circle`, `Square`, `Triangle`, `Diamond`), outline color/width.
- **Interaction:** hover highlighting with tooltips (label, values, quadrant name), click to select / click empty space to deselect, multi-selection, double-click callback.
- **Quadrant utilities:** map a point to its `QuadrantPosition`, list/count points per quadrant, read the quadrant's label.
- **Sample data:** `LoadSampleData()` fills the chart with a small built-in data set matching the current chart type (SWOT, BCG, Eisenhower, RiskMatrix, Priority).

## Header Include

```cpp
#include "Plugins/Charts/UltraCanvasQuadrantChart.h"
```

## Class Reference

### Constructor

```cpp
UltraCanvasQuadrantChart(const std::string& id, int x, int y, int w, int h);
```

The constructor enables selection and tooltips by default and applies the `Business` visual style. The default chart type is `Custom` with generic quadrant labels.

### Chart Type & Configuration

```cpp
void SetQuadrantType(QuadrantType type);   // Applies the preset's labels/colors/axes
QuadrantType GetQuadrantType() const;

void SetQuadrantDefinition(const QuadrantDefinition& def);
const QuadrantDefinition& GetQuadrantDefinition() const;

void SetAxisConfiguration(const QuadrantAxisConfiguration& config);
const QuadrantAxisConfiguration& GetAxisConfiguration() const;

void SetQuadrantStyle(QuadrantStyle style);
QuadrantStyle GetQuadrantStyle() const;
```

`SetQuadrantType` re-applies the preset's `QuadrantDefinition` and axis captions, so call `SetQuadrantDefinition` / `SetAxisConfiguration` **after** it when you want to override parts of a preset. `QuadrantDefinition` also provides the presets directly (`QuadrantDefinition::SWOT()`, `::BCG()`, `::Ansoff()`, `::Eisenhower()`, `::MagicQuadrant()`, `::RiskMatrix()`, `::Priority()`).

The chart title comes from the base class: `SetTitle("...")` / `GetChartTitle()`.

### Data Management

```cpp
void AddDataPoint(const QuadrantDataPoint& point);
void AddDataPoint(const std::string& label, float x, float y,
                  const Color& color = Color(100, 150, 255, 255));
void SetDataPoints(const std::vector<QuadrantDataPoint>& points);
const std::vector<QuadrantDataPoint>& GetDataPoints() const;
void ClearDataPoints();
void RemoveDataPoint(size_t index);   // Keeps the selection consistent

void LoadSampleData();                // Built-in demo data for the current type
```

`QuadrantDataPoint` fields:

```cpp
struct QuadrantDataPoint {
    std::string label;
    float xValue, yValue;         // In axis units (default range 0.0 .. 1.0)
    Color pointColor;             // Fill color
    float radius = 6.0f;          // Use larger radii for BCG-style bubbles
    QuadrantPointShape shape = QuadrantPointShape::Circle;
    Color strokeColor;            // Outline
    float strokeWidth = 1.0f;
};
```

### Visual Toggles

```cpp
void SetShowQuadrantLabels(bool show);   // Labels inside each quadrant (default: on)
void SetShowQuadrantColors(bool show);   // Quadrant background fills (default: on)
void SetShowDataPointLabels(bool show);  // Text above each point (default: off)
```

### Interaction Settings

```cpp
// Inherited from UltraCanvasChartElementBase:
void SetEnableSelection(bool enable);    // Default: on
void SetEnableTooltips(bool enable);     // Default: on

// Quadrant chart specific:
void SetEnableMultiSelection(bool enable);  // Default: on
```

### Selection Management

```cpp
void SelectDataPoint(size_t index);
void DeselectDataPoint(size_t index);
void ClearSelection();
const std::vector<size_t>& GetSelectedPoints() const;
bool IsPointSelected(size_t index) const;
```

Clicking a point toggles its selection; clicking empty plot area clears the selection.

### Quadrant Utilities

```cpp
QuadrantPosition GetPointQuadrant(const QuadrantDataPoint& point) const;
std::vector<size_t> GetPointsInQuadrant(QuadrantPosition quadrant) const;
size_t CountPointsInQuadrant(QuadrantPosition quadrant) const;
const std::string& GetQuadrantLabel(QuadrantPosition quadrant) const;
```

`QuadrantPosition` is `TopLeft`, `TopRight`, `BottomLeft` or `BottomRight`. A point on the division line counts toward the right/top side.

### Event Callbacks

```cpp
std::function<void(size_t, const QuadrantDataPoint&)> onPointSelect;
std::function<void(size_t, const QuadrantDataPoint&)> onPointDeselect;
std::function<void(size_t, const QuadrantDataPoint&)> onPointHover;
std::function<void(size_t, const QuadrantDataPoint&)> onPointDoubleClick;
std::function<void()> onSelectionChange;   // Fires after any click-driven change
```

### Factory Functions

```cpp
std::shared_ptr<UltraCanvasQuadrantChart> CreateQuadrantChartElement(
        const std::string& id, int x, int y, int width, int height);

std::shared_ptr<UltraCanvasQuadrantChart> CreateSWOTAnalysisChart(...);
std::shared_ptr<UltraCanvasQuadrantChart> CreateBCGMatrixChart(...);
std::shared_ptr<UltraCanvasQuadrantChart> CreateEisenhowerMatrixChart(...);
std::shared_ptr<UltraCanvasQuadrantChart> CreateRiskMatrixChart(...);
std::shared_ptr<UltraCanvasQuadrantChart> CreatePriorityMatrixChart(...);

std::shared_ptr<UltraCanvasQuadrantChart> CreateCustomQuadrantChart(
        const std::string& id, int x, int y, int width, int height,
        const std::string& title, const QuadrantDefinition& definition);
```

All specialized factories take the same `(id, x, y, width, height)` signature, set the matching `QuadrantType` and a default title. They do **not** load data — call `LoadSampleData()` or provide your own points.

### Sample Data Generators

```cpp
namespace QuadrantChartSamples {
    std::vector<QuadrantDataPoint> SWOTData();
    std::vector<QuadrantDataPoint> BCGData();       // Varying bubble radii
    std::vector<QuadrantDataPoint> EisenhowerData();
    std::vector<QuadrantDataPoint> RiskData();
    std::vector<QuadrantDataPoint> PriorityData();
}
```

## Usage Examples

### Basic SWOT Analysis

```cpp
#include "Plugins/Charts/UltraCanvasQuadrantChart.h"

auto swot = CreateSWOTAnalysisChart("swot", 20, 20, 600, 480);
swot->LoadSampleData();               // Or add your own points:
swot->AddDataPoint("Strong Brand", 0.2f, 0.8f, Color(60, 179, 113, 255));
swot->SetShowDataPointLabels(true);
parentContainer->AddChild(swot);
```

### BCG Matrix with Bubble Sizes

```cpp
auto bcg = CreateBCGMatrixChart("bcg", 20, 20, 600, 480);

QuadrantDataPoint alpha("Product Alpha", 0.8f, 0.9f, Color(46, 160, 67, 220));
alpha.radius = 12.0f;                 // Bubble size encodes revenue
bcg->AddDataPoint(alpha);

QuadrantDataPoint beta("Product Beta", 0.9f, 0.2f, Color(9, 105, 218, 220));
beta.radius = 10.0f;
beta.shape = QuadrantPointShape::Diamond;
bcg->AddDataPoint(beta);
```

### Fully Custom Quadrants

```cpp
QuadrantDefinition def;
def.topLeftLabel = "Innovation\nZone";        // Multi-line labels supported
def.topRightLabel = "Success\nZone";
def.bottomLeftLabel = "Learning\nZone";
def.bottomRightLabel = "Comfort\nZone";
def.topLeftColor = Color(255, 215, 0, 100);   // Quadrant background fills
def.topRightColor = Color(144, 238, 144, 100);
def.bottomLeftColor = Color(173, 216, 230, 100);
def.bottomRightColor = Color(255, 182, 193, 100);

auto chart = CreateCustomQuadrantChart("innovation", 20, 20, 600, 480,
                                       "Innovation vs Execution Matrix", def);

QuadrantAxisConfiguration axis;
axis.xAxisLabel = "Execution Capability";
axis.yAxisLabel = "Innovation Level";
axis.xAxisLeftLabel = "Low Execution";
axis.xAxisRightLabel = "High Execution";
axis.yAxisBottomLabel = "Low Innovation";
axis.yAxisTopLabel = "High Innovation";
chart->SetAxisConfiguration(axis);

chart->AddDataPoint("R&D Projects", 0.3f, 0.8f, Color(212, 167, 44, 255));
chart->AddDataPoint("Core Products", 0.8f, 0.8f, Color(46, 160, 67, 255));
```

### Custom Data Range and Division Point

The axis range does not have to be `0..1`, and the quadrant division does not have to be centered:

```cpp
QuadrantAxisConfiguration axis = chart->GetAxisConfiguration();
axis.xMin = 0.0f;  axis.xMax = 100.0f;  axis.xCenter = 60.0f;
axis.yMin = -50.0f; axis.yMax = 50.0f;  axis.yCenter = 0.0f;
chart->SetAxisConfiguration(axis);

chart->AddDataPoint("Sample", 75.0f, -12.5f);   // Bottom-right quadrant
```

### Selection, Callbacks and Statistics

```cpp
chart->onPointSelect = [](size_t index, const QuadrantDataPoint& point) {
    std::cout << "Selected " << point.label << std::endl;
};

chart->onSelectionChange = [chartPtr = chart.get()]() {
    std::cout << chartPtr->GetSelectedPoints().size() << " points selected\n";
};

// Per-quadrant statistics (e.g. for a side panel):
size_t critical = chart->CountPointsInQuadrant(QuadrantPosition::TopRight);
for (size_t i : chart->GetPointsInQuadrant(QuadrantPosition::TopRight)) {
    std::cout << chart->GetDataPoints()[i].label << " is in "
              << chart->GetQuadrantLabel(QuadrantPosition::TopRight) << "\n";
}
```

### Switching Styles at Runtime

```cpp
chart->SetQuadrantStyle(QuadrantStyle::Scientific);  // Adds a grid, black axes
chart->SetShowQuadrantColors(false);                 // Plain white plot
chart->SetShowQuadrantLabels(true);
```

## Demo Application

The demo page lives in `Apps/DemoApp/UltraCanvasQuadrantChartExamples.cpp` (`Charts > Quadrant Chart`) and shows six tabs:

1. **SWOT** — internal/external factors vs positive/negative impact.
2. **BCG Matrix** — market share vs growth with revenue-sized bubbles.
3. **Eisenhower** — urgency vs importance for task management.
4. **Risk Matrix** — probability vs impact with severity-colored quadrants.
5. **Priority** — impact vs effort (Quick Wins / Major Projects / Fill-ins / Thankless Tasks).
6. **Custom** — a user-defined innovation-vs-execution matrix.

The sidebar demonstrates runtime reconfiguration (visual style, quadrant colors, point labels), dynamic data (add random point, clear, reload sample data) and the statistics panel is driven entirely by the selection callbacks and the per-quadrant counting utilities.

## Notes & Best Practices

- **Preset then override:** `SetQuadrantType` resets labels/axes to the preset, so apply custom `QuadrantDefinition` / `QuadrantAxisConfiguration` afterwards.
- **Multi-line labels:** use `\n` inside quadrant labels; the element centers each line separately.
- **Hit testing:** points are picked within `radius + 3px`; overlapping points resolve to the one drawn last (topmost).
- **Tooltips** show the point label, both values and the quadrant name; disable them with `SetEnableTooltips(false)` if the host page provides its own hover UI.
- **Redraws** are requested automatically by every setter; no manual invalidation is needed.

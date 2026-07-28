# UltraCanvasSunburstChart Documentation

## Overview

The `UltraCanvasSunburstChart` is a hierarchical radial partition ("sunburst") chart component within the UltraCanvas framework. It renders a tree of values as concentric rings: each ring corresponds to one level of the hierarchy and each segment's angular span is proportional to its aggregated value. It supports drill-down zooming, depth-based shading, partial (fan) sweeps, configurable geometry, auto-fitting radial/horizontal labels, and interactive tooltips with breadcrumb paths.

**Namespace:** `UltraCanvas`  
**Header:** `include/Plugins/Charts/UltraCanvasSunburstChart.h`  
**Base Class:** `UltraCanvasChartElementBase`  
**Version:** 1.0.0  
**Last Modified:** 2026-07-28  
**Author:** UltraCanvas Framework

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasSunburstChart
```

## Features

### Core Capabilities
- **Hierarchical Data Model:** Arbitrary-depth `SunburstNode` tree; branch weights are the sum of their children (standard partition convention)
- **Proportional Layout:** Angular spans are computed recursively from aggregated values; rings map to hierarchy depth
- **Drill-Down Zoom:** Click a branch segment to re-root the chart on it; click the center hole to navigate back up
- **Depth Shading:** Children automatically lighten or darken relative to their branch color, or inherit it unchanged
- **Partial Sweeps:** Render fan/gauge-style sunbursts by limiting the sweep angle (10°–360°) and choosing a start angle
- **Depth Limiting:** `SetMaxVisibleDepth` collapses deep hierarchies into an overview
- **Auto-Fitting Labels:** Name/value/percentage labels placed horizontally or radially, hidden automatically when they do not fit
- **Center Area:** Custom center text, automatic total, and drill-up hint inside the donut hole
- **Interactive:** Hover highlighting, click/hover/drill callbacks, tooltips with breadcrumb path, share of total, and share of parent

## Data Model

### SunburstNode

```cpp
struct SunburstNode {
    std::string label;
    double value;                 // used for leaves; branches aggregate children
    Color color;                  // Colors::Transparent = derive automatically
    std::vector<std::shared_ptr<SunburstNode>> children;

    std::shared_ptr<SunburstNode> AddChild(const std::string& label,
                                           double value = 0.0,
                                           const Color& color = Colors::Transparent);
    std::shared_ptr<SunburstNode> FindChild(const std::string& label) const;
    bool IsLeaf() const;
    double AggregatedValue() const;   // sum of children for branches
    int SubtreeDepth() const;
};
```

The root node itself is not drawn — its children form the innermost ring.

## Enumerations

### SunburstLabelContent
```cpp
enum class SunburstLabelContent {
    None, Name, Value, Percentage, NameValue, NamePercentage
};
```

### SunburstLabelOrientation
```cpp
enum class SunburstLabelOrientation {
    Auto,        // horizontal when it fits, radial otherwise, hidden if neither fits
    Horizontal,
    Radial       // runs along the radius, flipped on the left half
};
```

### SunburstShadeMode
```cpp
enum class SunburstShadeMode {
    None, LightenByDepth, DarkenByDepth
};
```

## Public API

### Data Management
| Method | Description |
|---|---|
| `SetRootNode(node)` | Replace the whole hierarchy |
| `GetRootNode()` | Access the hierarchy root |
| `AddNode(path, value, color)` | Build/extend the hierarchy along a label path |
| `ClearData()` | Reset to an empty hierarchy |

### Geometry
| Method | Description |
|---|---|
| `SetStartAngle(deg)` | Where the sweep starts (default −90°, 12 o'clock) |
| `SetSweepAngle(deg)` | Total angular sweep, 10–360 (default 360) |
| `SetInnerRadiusFraction(f)` | Center hole size, 0–0.9 (default 0.25) |
| `SetRingSpacing(px)` | Radial gap between rings |
| `SetSegmentSpacingAngle(deg)` | Angular gap between segments |
| `SetMaxVisibleDepth(n)` | Limit visible rings (0 = all) |

### Colors & Borders
| Method | Description |
|---|---|
| `SetColorPalette(colors)` | One color per top-level branch (cycled) |
| `SetShadeMode(mode)` / `SetShadeStep(f)` | Depth-based lighten/darken of children |
| `SetBorderColor(c)` / `SetBorderWidth(w)` | Segment outlines |

### Center
| Method | Description |
|---|---|
| `SetCenterText(text)` | Custom center caption (empty = automatic) |
| `SetShowCenterTotal(on)` | Append the formatted total of the current root |
| `SetCenterTextColor(c)` / `SetCenterFont(family, size, weight)` | Center styling |

### Labels
| Method | Description |
|---|---|
| `SetLabelContent(content)` / `SetLabelOrientation(orientation)` | What and how to draw |
| `SetLabelFont(...)` / `SetLabelColor(c)` | Label styling |
| `SetMinLabelAngle(deg)` | Hide labels on segments narrower than this |
| `SetValueFormatter(f)` / `SetPercentageFormatter(f)` | Custom formatting |

### Interaction
| Method | Description |
|---|---|
| `SetHoverHighlightEnabled(on)` | Lighten + expand the hovered segment |
| `SetTooltipsEnabled(on)` | Framework tooltips with breadcrumb path |
| `SetSunburstTooltipFormatter(f)` | Custom tooltip text per node |
| `SetDrillDownEnabled(on)` | Click-to-zoom navigation |
| `DrillTo(node)` / `DrillUp()` / `ResetDrill()` / `GetCurrentRoot()` | Programmatic navigation |
| `onSegmentClick` / `onSegmentHover` | `std::function<void(node, depth)>` callbacks |
| `onDrillChange` | Fired whenever the visible root changes |

## Usage Examples

### Basic Three-Level Sunburst
```cpp
auto chart = CreateSunburstChart("Sales", 20, 20, 460, 320);

auto root = std::make_shared<SunburstNode>("Total Sales");
auto west = root->AddChild("West");
auto tech = west->AddChild("Technology");
tech->AddChild("Phones", 126);
tech->AddChild("Copiers", 83);
chart->SetRootNode(root);

chart->SetCenterText("Total Sales");
chart->SetDrillDownEnabled(true);
chart->SetTooltipsEnabled(true);
```

### Path-Based Construction
```cpp
chart->AddNode({"Europe", "Germany"}, 47.8);
chart->AddNode({"Europe", "France"}, 41.7);
chart->AddNode({"Asia", "Japan"}, 48.6);
```

### Two-Level Grouped Factory
```cpp
auto chart = CreateSunburstChartFromGroups("Geo", 0, 0, 460, 320, {
    {"Europe", {{"Germany", 47.8}, {"France", 41.7}}},
    {"Asia",   {{"Japan", 48.6}, {"China", 38.4}}}
});
```

### Partial-Sweep Fan With Explicit Branch Colors
```cpp
auto root = std::make_shared<SunburstNode>("Requests");
auto ok = root->AddChild("Approved", 0, Color(76, 175, 80, 255));
ok->AddChild("Automatic", 380);
chart->SetRootNode(root);
chart->SetSweepAngle(240.0f);
chart->SetStartAngle(-210.0f);
```

## Demo

See `Apps/DemoApp/UltraCanvasSunburstChartExamples.cpp` for a complete interactive demonstration: a three-level sales hierarchy with drill-down, a demographic continent → country sunburst, a 240° fan with explicit branch colors, a depth-limited overview, and a live control panel (label content/orientation, shade mode, hole size, segment gap, border width, hover highlight and drill-down toggles).

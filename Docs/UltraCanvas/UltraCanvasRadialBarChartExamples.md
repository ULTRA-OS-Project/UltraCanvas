# UltraCanvasRadialBarChart Documentation

## Overview

The `UltraCanvasRadialBarChart` is a radial "ray" chart component within the UltraCanvas framework. Values are drawn as rays radiating outward from a base ring, with ray length proportional to the value. Series (e.g. sensors, continents, teams) occupy contiguous angular sectors separated by configurable gaps — the classic multi-sensor / time-series radial dashboard. It complements `UltraCanvasSunburstChart`: a sunburst encodes values as **angular spans across hierarchy rings**, while the radial bar chart encodes them as **ray lengths**.

**Namespace:** `UltraCanvas`  
**Header:** `include/Plugins/Charts/UltraCanvasRadialBarChart.h`  
**Base Class:** `UltraCanvasChartElementBase`  
**Version:** 1.0.0  
**Last Modified:** 2026-07-28  
**Author:** UltraCanvas Framework

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasRadialBarChart
```

## Features

### Core Capabilities
- **Series-Sector Layout:** Each series is a contiguous angular sector of consecutive bars; slot widths are computed automatically from the total value count
- **Bar & Line Styles:** Thick auto-sized bars or thin uniform spokes (`RadialBarStyle::Bars` / `Lines`)
- **Cap Shapes:** Butt, round, square, or arrow ray tips
- **Gradient Fade:** Bars can fade toward the tip for the classic sensor-dashboard look
- **Flexible Scaling:** Global min/max, per-series scaling, or an explicit manual range; minimum-length floor keeps small values visible
- **Partial Sweeps:** Fan/gauge layouts via start angle + sweep angle (10°–360°)
- **Guides & Center:** Optional concentric ring guides, styled inner circle, and center caption
- **Labels:** Series captions around the perimeter (with background chips) and per-series "Max" peak annotations
- **Interactive:** Hover highlighting, tooltips (series / label / value), click and hover callbacks

## Data Model

```cpp
struct RadialBarValue {
    std::string label;
    double value;
    Color color;      // Colors::Transparent = inherit the series color
};

struct RadialBarSeries {
    std::string name;
    Color color;      // Colors::Transparent = take from palette
    std::vector<RadialBarValue> values;
};
```

## Enumerations

```cpp
enum class RadialBarStyle        { Bars, Lines };
enum class RadialBarCapStyle     { Butt, Round, Square, Arrow };
enum class RadialBarNormalization{ Global, PerSeries };
```

## Public API

### Data Management
| Method | Description |
|---|---|
| `AddSeries(series)` | Append a prepared `RadialBarSeries` |
| `AddSeries(name, {{label, value}, ...}, color)` | Convenience overload |
| `ClearSeries()` / `GetSeriesCount()` / `GetSeries(i)` | Access and reset |
| `SetSeriesColor(i, color)` | Recolor one series |

### Geometry
| Method | Description |
|---|---|
| `SetStartAngle(deg)` / `SetSweepAngle(deg)` | Angular placement (fan charts) |
| `SetInnerRadiusFraction(f)` | Base ring where rays start (0.05–0.9) |
| `SetSeriesGapAngle(deg)` | Angular gap between series sectors |
| `SetBarWidth(px)` | Fixed ray thickness; 0 = automatic from slot |

### Value Scaling
| Method | Description |
|---|---|
| `SetNormalization(mode)` | `Global` (comparable) or `PerSeries` |
| `SetValueRange(min, max)` / `ClearValueRange()` | Manual range override |
| `SetMinBarLengthFraction(f)` | Floor so tiny values stay visible |

### Style
| Method | Description |
|---|---|
| `SetBarStyle(style)` / `SetCapStyle(cap)` | Ray look |
| `SetGradientFade(on)` | Fade bars toward the tip |
| `SetColorPalette(colors)` | Per-series fallback colors (cycled) |

### Guides, Center & Labels
| Method | Description |
|---|---|
| `SetShowRingGuides(on)` / `SetRingGuideCount(n)` / `SetRingGuideColor(c)` | Concentric guides |
| `SetInnerCircleColor(fill, border)` | Base circle styling |
| `SetCenterText(text)` / `SetCenterTextColor(c)` / `SetCenterFont(...)` | Center caption |
| `SetShowSeriesLabels(on)` / `SetSeriesLabelFont(...)` | Perimeter captions |
| `SetShowPeakLabels(on)` | "Max <value>" annotation at each series peak |
| `SetLabelColor(c)` / `SetValueFormatter(f)` | Text styling and formatting |

### Interaction
| Method | Description |
|---|---|
| `SetHoverHighlightEnabled(on)` | Lighten + thicken the hovered ray |
| `SetTooltipsEnabled(on)` / `SetRadialTooltipFormatter(f)` | Tooltips |
| `onBarClick` / `onBarHover` | `std::function<void(seriesIdx, valueIdx)>` |

## Usage Examples

### Multi-Sensor Dashboard
```cpp
std::vector<std::pair<std::string, std::vector<double>>> sensorData = {
    {"Sensor 01", {45, 52, 48, 51, 47, 49}},
    {"Sensor 02", {120, 118, 122, 125, 119, 121}},
    {"Sensor 03", {65, 68, 62, 70, 67, 66}}
};
auto chart = CreateRadialBarChartFromSeries("Sensors", 20, 20, 460, 320, sensorData);
chart->SetCenterText("Sensors");
chart->SetGradientFade(true);
chart->SetShowPeakLabels(true);
chart->SetTooltipsEnabled(true);
```

### Radial Line (Spoke) Style
```cpp
auto chart = CreateRadialBarChart("Ages", 0, 0, 460, 320);
chart->AddSeries("Europe", {{"Germany", 47.8}, {"France", 41.7}}, Color(3, 169, 244, 255));
chart->AddSeries("Africa", {{"Nigeria", 18.1}, {"Egypt", 24.6}}, Color(76, 175, 80, 255));
chart->SetBarStyle(RadialBarStyle::Lines);
chart->SetGradientFade(false);
```

### Fan With Manual Range and Arrow Tips
```cpp
chart->SetSweepAngle(270.0f);
chart->SetStartAngle(-225.0f);
chart->SetValueRange(0.0, 150.0);
chart->SetCapStyle(RadialBarCapStyle::Arrow);
chart->SetShowRingGuides(true);
```

## Demo

See `Apps/DemoApp/UltraCanvasRadialBarChartExamples.cpp` for a complete interactive demonstration: a 6×24 multi-sensor dashboard, continent-grouped radial lines, a 270° rainfall fan with ring guides, a KPI chart with arrow caps and a manual range, plus a live control panel (style, caps, gradient, scaling mode, hole size, series gap, guides, labels and hover toggles).

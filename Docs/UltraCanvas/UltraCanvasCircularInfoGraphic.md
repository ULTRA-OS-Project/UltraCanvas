# UltraCanvasCircularInfoGraphic Documentation

## Overview

The `UltraCanvasCircularInfoGraphic` is a multi-ring circular **infographic** component: concentric rings of individually styled cells, each carrying text, a background colour or image, and an optional value visualisation. Rings can add decorative bands, and cells can be linked with cross-ring connection lines. It targets presentation-grade graphics (org charts, ecosystems, category wheels) rather than plain value plotting — for "N percentages as N rings" use `UltraCanvasCircularProgressChart`, and see [`UltraCanvasCircularCharts.md`](UltraCanvasCircularCharts.md) for the full family map.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Charts/UltraCanvasCircularInfoGraphic.h`
**Base Class:** `UltraCanvasChartElementBase`
**Version:** 1.0.0
**Last Modified:** 2026-07-29
**Author:** UltraCanvas Framework

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasCircularInfoGraphic
```

## Features

### Core Capabilities
- **Cell grid on rings:** Every ring holds an ordered list of `CircularCell`s; cells size themselves evenly across the ring's sweep, with per-cell spacing
- **Text styles per ring:** Horizontal, circular (glyph-by-glyph along the ring), radial (along the spoke, flipped on the left half), or star-style (parked outside with a leader line)
- **Value visualisations per ring:** Radial bars filling the cell from its inner edge, an angular progress arc across the cell, or a polyline linking every cell's value around the ring
- **Colour scale:** An optional `ColorScale` maps cell values to colours (banded or interpolated); cells opt in with `useColorScale`
- **Decorative rings:** Colour highlight, repeated graphics, tick patterns, or a radial gradient across the band
- **Cross-ring connections:** Value-weighted lines routed through the centre, around the outside, or as direct curves; per-connection styles and per-ring connection indicators
- **Cell grouping:** Cells sharing a `groupName` are outlined together
- **Geometry:** Auto-fit or explicit radii, inner-hole fraction, whole-chart rotation, partial sweeps (10°–360°), ring spacing and cell spacing
- **Centre & background:** Centre disc with colours/graphic/caption, optional background graphic
- **CSV import:** One `ImportDataFromCSV` call appends one ring (`"RingLabel;Cell,TextColor,BgColor,Value,Image;..."`); colour tokens accept `#RGB`/`#RRGGBB`/`#RRGGBBAA`, `rgb()/rgba()` and common names
- **Interactive:** Hover highlighting, tooltips (per-cell override supported), `onCellClick` / `onCellHover` callbacks

## Data Model

```cpp
struct CircularCell {
    std::string text;
    Color textColor, backgroundColor;
    std::string backgroundImage;
    double value;
    bool isEmpty, isTransparent, useColorScale;
    float rotation;            // extra label rotation (degrees)
    bool autoRotate;           // align label with the cell's mid-angle
    std::string groupName;     // cells sharing a name are outlined together
    std::string tooltip;       // overrides the generated tooltip
};

struct CircularRing {
    std::vector<CircularCell> cells;
    CircularTextStyle textStyle;
    ValueVisualizationType valueType;
    DecorativeRingType decorativeType;
    Color decorativeColor, borderColor, valueColor;
    float borderWidth;
    bool showConnectionIndicators;
    std::string label;
};

struct CircularConnection {
    size_t fromRing, fromCell, toRing, toCell;
    double value;              // drives proportional thickness
    ConnectionStyle style;
    std::string label;
};
```

## Enumerations

```cpp
enum class CircularTextStyle      { Horizontal, Circular, Radial, StarStyle };
enum class ValueVisualizationType { None, Bars, CircularLine, CircularBar };
enum class DecorativeRingType     { None, ColorHighlight, Graphics, Pattern, Gradient };
enum class ConnectionLineType     { None, InsideCenter, OutsideRing, DirectCurve };
```

## Public API

### Rings & Cells
| Method | Description |
|---|---|
| `AddRing(ring)` | Append a ring (inner to outer); returns its index |
| `SetRing(i, ring)` / `GetRing(i)` / `GetRingCount()` / `ClearRings()` | Ring access |
| `SetNumberOfLevels(n)` | Resize to exactly `n` rings |
| `SetRingTextStyle(i, style)` / `SetRingValueType(i, type)` | Per-ring configuration |
| `AddCell(i, cell)` / `GetCell(i, j)` / `GetCellCount(i)` | Cell access |
| `UpdateCellValue(i, j, v)` / `UpdateCellText(i, j, text)` | In-place cell updates |

### Geometry
| Method | Description |
|---|---|
| `SetRadiusRange(inner, outer)` | Explicit radii in pixels (disables auto-fit) |
| `SetAutoFitRadius(on)` / `SetInnerRadiusFraction(f)` | Auto-fit sizing |
| `SetAngleOffset(deg)` | Rotate the whole chart |
| `SetTotalAngle(deg)` | Partial sweeps, 10°–360° |
| `SetRingSpacing(px)` / `SetCellSpacingAngle(deg)` | Gaps |

### Appearance
| Method | Description |
|---|---|
| `SetCenterGraphic(path)` / `SetBackgroundGraphic(path)` | Images |
| `SetShowCenterCircle(on)` / `SetCenterColor(fill, border)` / `SetCenterText(text)` | Centre disc |
| `SetColorScale(scale)` | Value → colour mapping for `useColorScale` cells |
| `SetLabelFont(family, size, weight)` / `SetLabelColor(c)` | Label styling |
| `SetShowGroupOutlines(on)` / `SetGroupOutlineColor(c)` | Group outlines |

### Connections
| Method | Description |
|---|---|
| `SetConnectionLineType(type)` | Routing: `InsideCenter`, `OutsideRing`, `DirectCurve`, `None` |
| `AddConnection(fromRing, fromCell, toRing, toCell, value)` | Add a link |
| `SetConnectionStyle(i, style)` / `SetDefaultConnectionStyle(style)` | Styling |
| `ClearConnections()` / `GetConnectionCount()` | Management |

### Data Import & Interaction
| Method | Description |
|---|---|
| `ImportDataFromCSV(csv)` | One call appends one ring (format above) |
| `ParseColorToken(token, fallback)` | Static colour-token parser (CSV rules) |
| `SetHoverHighlightEnabled(on)` / `SetTooltipsEnabled(on)` | Interactivity |
| `onCellClick` / `onCellHover` | `std::function<void(size_t ring, size_t cell)>` |
| `customTooltipGenerator` | `std::function<std::string(const CircularCell&)>` |

## Usage Example

```cpp
#include "Plugins/Charts/UltraCanvasCircularInfoGraphic.h"

auto chart = UltraCanvas::CreateCircularInfoGraphic("org", 20, 20, 560, 520);

UltraCanvas::CircularRing exec;
exec.label = "Executive";
exec.textStyle = UltraCanvas::CircularTextStyle::Horizontal;
exec.valueType = UltraCanvas::ValueVisualizationType::Bars;
UltraCanvas::CircularCell ceo;
ceo.text = "CEO"; ceo.value = 96.0;
ceo.backgroundColor = UltraCanvas::Color(238, 241, 248, 255);
ceo.textColor = UltraCanvas::Color(35, 40, 60, 255);
exec.cells = { ceo /* , ... */ };
chart->AddRing(exec);

UltraCanvas::CircularRing depts;
depts.label = "Departments";
depts.textStyle = UltraCanvas::CircularTextStyle::Circular;
depts.valueType = UltraCanvas::ValueVisualizationType::CircularBar;
/* depts.cells = ... */
chart->AddRing(depts);

chart->SetConnectionLineType(UltraCanvas::ConnectionLineType::InsideCenter);
chart->AddConnection(0, 0, 1, 0, 90.0);          // CEO -> first department
chart->SetCenterText("Org 2026");
chart->onCellClick = [](size_t ring, size_t cell) { /* ... */ };
container->AddChild(chart);
```

Convenience factory: `CreateCircularInfoGraphicFromGroups(id, x, y, w, h, groupedData)` builds one ring per group from `(groupLabel, [(cellLabel, value), ...])` pairs.

## Notes & Conventions

- Rings are ordered **inner to outer**; the computed `innerRadius`/`outerRadius` on `CircularRing` are outputs of layout, not inputs.
- The element owns its ring model — `SetDataSource` from the chart base class is not used.
- Demo: `Apps/DemoApp/UltraCanvasCircularInfoGraphicExamples.cpp` (DemoApp → Charts → Circular InfoGraphic).

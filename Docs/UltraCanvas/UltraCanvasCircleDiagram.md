# UltraCanvasCircleDiagram Documentation

## Overview

The `UltraCanvasCircleDiagram` is the **circle diagram** infographic: a central
hub, a backbone ring, and equally sized labelled node discs *threaded onto* that
ring, each carrying a fan of satellite discs on leader lines. It is the
node-on-ring member of the circular family — for rings *subdivided* into sectors
or cells use [`UltraCanvasCircularInfoGraphic`](UltraCanvasCircularInfoGraphic.md),
and see [`UltraCanvasCircularCharts.md`](UltraCanvasCircularCharts.md) for the
family map.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasCircleDiagram.h`
**Base Class:** `UltraCanvasChartElementBase`
**Version:** 1.0.0
**Last Modified:** 2026-08-08
**Author:** UltraCanvas Framework

The design survey and the phased plan this element implements are in
[`CircleDiagramInfographicVariants.md`](CircleDiagramInfographicVariants.md).
This release is **P1**: the satellite families (references 1 and 2 of the
survey). Cards, external callouts, spokes and badges are P2/P3.

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasCircleDiagram
```

## Presentation-only, by design

The diagram is authored from code and rendered. There is **no** node dragging,
inline label editing, pan/zoom or undo, and therefore no
`UltraCanvasDiagramViewport` dependency and no interchange-format layer.
Interaction is limited to hover highlighting, tooltips and click callbacks —
the same surface `UltraCanvasCircularInfoGraphic` exposes.

## Features

- **Nodes on a ring:** N equally spaced discs on the backbone circle, each with
  its own fill, border, icon and wrapped multi-line label
- **Satellite fans:** K satellites per node, spread over a fan centred on the
  node's outward radius and joined by solid, dashed or dotted leader lines
- **Hub:** free text, an icon, or a filled disc with a caption in the middle
- **Backbone:** hairline outline, thick coloured band, or none at all
- **Designs:** `SatelliteWheel` and `BandedWheel` structure presets
- **Palettes:** seven categorical themes plus `Custom`, all generating colours
  for whatever node count is set
- **Equal-sized nodes:** one radius per diagram; `value` never scales a disc
- **Interactive:** hover highlighting, tooltips (per-node/per-satellite
  override), `onNodeClick` / `onSatelliteClick` / hover callbacks

## Data Model

```cpp
struct CircleSatellite {
    std::string text;
    Color fillColor, borderColor, textColor;   // transparent = derive
    std::string iconImage;
    std::string tooltip;
    double value;              // tooltip/callback payload only
};

struct CircleNode {
    std::string text;
    Color fillColor, borderColor, textColor;   // transparent = palette / auto
    std::string iconImage;     // drawn above the label, inside the disc
    std::string tooltip;
    double value;              // tooltip/callback payload only
    std::vector<CircleSatellite> satellites;
};
```

A fully transparent colour means "derive it": a node takes the palette colour
for its index, and a label takes whichever of dark/white reads better on the
resolved fill. A satellite falls back to the design's satellite colours.

## Enumerations

```cpp
enum class CircleDiagramDesign      { SatelliteWheel, BandedWheel, Custom };
enum class CircleDiagramPaletteKind { Vibrant, Ocean, Mint, Pastel,
                                      Midnight, Dark, Monochrome, Custom };
enum class CircleBackboneStyle      { NoRing, Hairline, Band };
enum class CircleLeaderStyle        { NoLeader, Solid, Dashed, Dotted };
enum class CircleNodeLabelPlacement { Inside, Outside };
```

## Public API

### Design & palette
| Method | Description |
|---|---|
| `SetDesign(d)` / `GetDesign()` | Structure preset; fills in defaults that stay individually settable |
| `SetPalette(kind)` / `GetPalette()` | Colour theme |
| `SetCustomPalette(colors)` | Explicit colours, cycled if shorter than the node count; selects `Custom` |
| `PaletteColor(index, total)` | The colour the palette resolves for one node |

### Nodes & satellites
| Method | Description |
|---|---|
| `AddNode(node)` / `AddNode(text)` | Append a node; returns its index |
| `SetNode(i, node)` / `GetNode(i)` / `GetNodeCount()` / `ClearNodes()` | Node access |
| `SetNodeCount(n)` | Resize to exactly `n` nodes, clamped to 3–12 |
| `UpdateNodeText(i, text)` | In-place update |
| `AddSatellite(i, satellite)` / `AddSatellite(i, text)` | Attach a satellite |
| `GetSatelliteCount(i)` / `GetSatellite(i, j)` / `ClearSatellites(i)` | Satellite access |

### Hub
| Method | Description |
|---|---|
| `SetHubText(text)` / `SetHubIcon(path)` | Centre caption and icon |
| `SetShowHubDisc(on)` / `SetHubColor(fill, border)` / `SetHubTextColor(c)` | Centre disc |
| `SetHubRadiusFraction(f)` | Hub size as a fraction of the backbone radius |

### Geometry
| Method | Description |
|---|---|
| `SetNodeRadius(px)` | Explicit node radius; 0 auto-fits from the per-node arc |
| `SetSatelliteRadius(px)` | Explicit satellite radius; 0 uses 0.55 × node radius |
| `SetSatelliteOffset(px)` / `SetSatelliteFanAngle(deg)` | Fan geometry (fan clamped 0–300) |
| `SetAngleOffset(deg)` | Rotate the whole diagram (default −90°, 12 o'clock) |
| `SetBackboneStyle(style)` / `SetBackboneThickness(px)` / `SetBackboneColor(c)` | The ring |

### Appearance & interaction
| Method | Description |
|---|---|
| `SetLeaderStyle(style)` / `SetLeaderColor(c)` / `SetLeaderWidth(px)` | Leader lines |
| `SetNodeLabelPlacement(placement)` | Label inside the disc or parked outside |
| `SetNodeFont` / `SetSatelliteFont` / `SetHubFont(family, size, weight)` | Fonts |
| `SetNodeBorderWidth(px)` / `SetShowNodeIcons(on)` | Node decoration |
| `SetHoverHighlightEnabled(on)` / `SetTooltipsEnabled(on)` | Interactivity |
| `onNodeClick` / `onNodeHover` | `std::function<void(size_t node)>` |
| `onSatelliteClick` / `onSatelliteHover` | `std::function<void(size_t node, size_t satellite)>` |
| `customNodeTooltipGenerator` | `std::function<std::string(const CircleNode&)>` |

## Usage Example

```cpp
#include "Plugins/Diagrams/UltraCanvasCircleDiagram.h"

auto diagram = UltraCanvas::CreateCircleDiagram("saas", 20, 20, 580, 520);
diagram->SetDesign(UltraCanvas::CircleDiagramDesign::SatelliteWheel);
diagram->SetPalette(UltraCanvas::CircleDiagramPaletteKind::Vibrant);
diagram->SetHubText("Components of a\nSaaS Business");

size_t marketing = diagram->AddNode("Marketing\nAutomation");
diagram->AddSatellite(marketing, "Leads targeting");
diagram->AddSatellite(marketing, "Social Media Ads");
diagram->AddSatellite(marketing, "Auto Emails");

size_t billing = diagram->AddNode("Billing");
diagram->AddSatellite(billing, "Invoicing");
diagram->AddSatellite(billing, "Receipts");

diagram->onNodeClick = [](size_t node) { /* ... */ };
container->AddChild(diagram);
```

Convenience factory: `CreateCircleDiagramFromGroups(id, x, y, w, h, groupedData)`
builds one node per `(nodeLabel, [satelliteLabel, ...])` pair.

## Node count and layout

Everything about the layout derives from the **per-node arc**, `360/N`, so a
design preset is never tuned to one reference's node count:

- The auto-fitted node radius is a share of the chord between neighbours, so
  discs keep a readable gap at every N.
- The satellite fan is narrowed to what one node's own wedge can hold, so fans
  never reach into a neighbour's at high N. Auto-sized satellite discs also
  shrink if K of them cannot fit that wedge side by side; an explicit
  `SetSatelliteRadius()` is left alone.
- Disc labels shrink to fit their disc, testing the longest single word as well
  as the wrapped block, because a word too wide to break would otherwise
  ellipsize instead of wrapping.
- Whatever sits outside the backbone — satellite fans, or labels placed with
  `CircleNodeLabelPlacement::Outside` — is reserved for by shrinking the
  backbone radius, so nothing is clipped at the element edge.

`SetNodeCount()` clamps to **3–12** (`kMinNodeCount` / `kMaxNodeCount`) rather
than degrading silently, the same way `SetTotalAngle()` clamps elsewhere in the
family. Below 3 the ring reads as a line; above 12 labels collide at any radius.
`AddNode()` is not clamped — it is the explicit, one-at-a-time path.

## Notes & Conventions

- The element owns its node model — `SetDataSource` from the chart base class
  is not used.
- Node discs are all one radius and satellites another; `value` is carried for
  tooltips and callbacks only and never scales a disc.
- Start angle defaults to **−90° (12 o'clock)**, matching the rest of the
  circular family.
- Demo: `Apps/DemoApp/UltraCanvasCircleDiagramExamples.cpp`
  (DemoApp → Info Graphics → Circle diagram).

# UltraCanvasHexbinChart

A hexagonal heatmap / hexbin chart: a grid of pointy-top hexagons whose colour
encodes a value. It reuses [`UltraCanvasHeatmapChart`](UltraCanvasHeatmapChart.md)
for data storage, colour mapping, value range and the colour bar — only the cell
geometry differs (hexagons instead of rectangles).

- Element: `include/Plugins/Charts/UltraCanvasHexbinChart.h` / `Plugins/Charts/UltraCanvasHexbinChart.cpp`
- Geometry (header-only): `include/Plugins/Charts/UltraCanvasHexLayout.h`

## Two ways to use it

### 1. A grid of valued hexagons

Feed a value matrix exactly like the rectangular heatmap (`SetData`); it's drawn
as hexagons:

```cpp
#include "Plugins/Charts/UltraCanvasHexbinChart.h"

auto hex = UltraCanvas::CreateHexbinChartElement("hex1", 20, 20, 800, 500);
hex->SetData(values, cols, rows);          // row-major
hex->SetColormap(UltraCanvas::HeatmapColormap::Turbo);
hex->SetChartTitle("Clustering of Supermarkets");
container->AddChild(hex);
```

### 2. Binning 2D points (hexbin density)

Bin a scatter of points into hexagon counts over a data range:

```cpp
std::vector<UltraCanvas::Point2Dd> points = loadPoints();
hex->SetBinnedData(points, /*cols*/ 40, /*rows*/ 25,
                   /*xMin*/ 0, /*xMax*/ 100, /*yMin*/ 0, /*yMax*/ 100);
```

Each point is assigned to the hexagon whose centre is nearest (which, for a hex
grid, is exactly the containing hexagon); the per-hex counts become the values.

## Configuration

```cpp
hex->SetHexBorders(true, Color(255,255,255,200), 1.0f); // outline between hexes
hex->SetShowColorBar(true);
```

All heatmap colour options apply: `SetColormap`, `SetCustomColormap`,
`SetReverseColormap`, `SetColorLevels` (discrete buckets), `SetScale`
(linear/log), `SetDiverging`, `SetNaNColor`. Row/column labels are off by
default. Hovering a hexagon shows its cell index and value.

## Notes

- Hexagons are pointy-top with odd rows offset by half a hex width; the grid is
  scaled to fit and centred within the plot area.
- The geometry math (`MakeHexLayout`, `HexCenter`, `HexNearestCell`) is
  dependency-free and unit-tested (grid fit, centre round-trip, point binning).
- Cells whose value is `NaN` (transparent NaN colour) are skipped.

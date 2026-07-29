# UltraCanvasContourChart

Contour charts draw curves of equal value through a scalar field: isolines, or
the filled bands between them. Feed the element a regular grid, an analytic
function, or a raw point cloud it converts into a density field for you.

- 2D element: `include/Plugins/Charts/UltraCanvasContourChart.h` / `Plugins/Charts/UltraCanvasContourChart.cpp`
- 3D surface: `include/Plugins/Charts/UltraCanvasContourSurface3D.h` / `Plugins/Charts/UltraCanvasContourSurface3D.cpp`
- Field model & gridding (UI-free): `include/Plugins/Charts/UltraCanvasContourGrid.h`
- Isoline / isoband extraction (UI-free): `include/Plugins/Charts/UltraCanvasMarchingSquares.h`

`UltraCanvasContourChartElement` derives from
[`UltraCanvasHeatmapChart`](UltraCanvasHeatmapChart.md), so every colour map,
value-range, log-scale, diverging and colour-bar option documented there applies
here too — and "heatmap cells with isolines on top" is just another render mode.

## Quick start

```cpp
#include "Plugins/Charts/UltraCanvasContourChart.h"

auto contour = UltraCanvas::CreateContourChartElement("contour1", 20, 20, 640, 480);
contour->SetData(values, cols, rows);          // row-major, like the heatmap
contour->SetDataRange(-4.0, 4.0, -4.0, 4.0);   // x/y extents of the lattice
contour->SetRenderMode(UltraCanvas::ContourRenderMode::FilledWithLines);
contour->SetColormap(UltraCanvas::HeatmapColormap::Jet);
contour->SetLevelCount(10);
contour->SetShowLineLabels(true);
container->AddChild(contour);
```

Row 0 of the data is placed at the **bottom** (the element switches the
inherited row order to `BottomUp` in its constructor), so `y` increases upward
the way a graph reads rather than the way a matrix prints.

## Render modes

| Mode | What it draws |
|---|---|
| `Lines` | isolines only, no fill |
| `Filled` | filled contour bands only |
| `FilledWithLines` | bands with isolines stroked on top |
| `Smooth` | continuous (unbanded) colour fill plus isolines |
| `HeatmapWithContours` | discrete heatmap cells with isolines on top |

Fills are rasterised by bilinear sampling of the field rather than emitted as
one polygon per cell. That keeps adjacent bands free of hairline seams, makes
`Smooth` a one-line variation, and costs the same regardless of grid size.
Isolines are always real vector polylines.

## Levels

Four ways to choose them, and they all feed the same extractor:

```cpp
contour->SetLevelCount(12);           // N evenly spaced levels
contour->SetLevelStep(0.02, 0.0);     // every 0.02, anchored on 0
contour->SetLevels({-1.2, -0.6, 0.0, 0.6, 1.2});   // explicit
contour->SetQuantileLevels(8);        // equal data share per band
contour->SetNiceLevels(true);         // snap auto levels to 1/2/2.5/5 x 10^n
```

## Contours from a point cloud

When there is no grid — only scattered points — the element builds one. The
default is a Gaussian kernel density estimate, which is the "contour chart"
sense of a dense scatter plot: the points are not drawn, their *density* is.

```cpp
contour->SetGriddingMethod(UltraCanvas::ContourGridding::KDE);
contour->SetGridResolution(128, 128);
contour->SetKDEBandwidth(0.0);         // 0 = Scott's rule
contour->SetPoints(points);            // vector<Point2Dd> or vector<ContourPoint>
contour->SetShowSourcePoints(true);    // optionally overlay the raw points
```

`ContourGridding::Binning` gives a raw histogram with an optional blur, while
`IDW` and `Nearest` treat each point's weight as a measured `z` value and
interpolate it instead of estimating density.

## Isoline styling and labels

```cpp
contour->SetLineColorMode(UltraCanvas::ContourLineColorMode::FromLevel);
contour->SetLineWidth(1.4f);
contour->SetNegativeLevelsDashed(true);       // the usual peak-vs-basin convention
contour->SetMajorLineEvery(5, 2.0f);          // thicker every 5th contour
contour->SetHighlightZeroLevel(true);

contour->SetShowLineLabels(true);
contour->SetLabelDecimals(2);
contour->SetLabelBreaksLine(true);            // gap the line under the text
contour->SetLabelSpacing(220.0);              // repeat along long contours
contour->SetLabelHalo(true);                  // plate behind the text
contour->SetLabelFormatter([](double v){ return std::to_string((int)v) + " m"; });
```

Labels are rotated to the local tangent, kept upright, and skipped on contours
too short to hold them.

## Legend and axes

```cpp
contour->SetLegendMode(UltraCanvas::ContourLegendMode::DiscreteBands);
contour->SetLegendTitle("level");
contour->SetIntervalFormat(UltraCanvas::ContourIntervalFormat::Brackets);  // [0.00, 0.02]

contour->SetAxisTitles("X1", "X2");
contour->SetTickCounts(7, 5);
contour->SetXTickFormatter([](double v){ char b[32]; snprintf(b,32,"%.2fM", v/1e6); return std::string(b); });
contour->SetCategoryLabelsX({"A","B","C","D","E"});   // categorical instead of numeric
```

`ContourLegendMode::ColorBar` reuses the heatmap's continuous ramp;
`DiscreteBands` / `DiscreteBandsHorizontal` list one swatch per band with its
interval.

## Dark themes

Every foreground colour is settable, so the chart drops onto a dark background
without any special mode:

```cpp
contour->SetBackgroundColor(Color(8, 8, 10, 255));
contour->SetTitleColor(Color(235, 235, 240, 255));
contour->SetLabelColor(Color(190, 195, 205, 255));
contour->SetFrameColor(Color(70, 72, 80, 255));
```

(`SetTitleColor` / `SetLabelColor` / `SetFrameColor` live on the heatmap base
class, so the heatmap, hexbin and calendar variants gain them too.)

## Field preparation

```cpp
contour->SetGridSmoothing(1.6);    // Gaussian sigma in cells, applied to the field
contour->SetLineSmoothing(2);      // Chaikin iterations on the extracted polylines
contour->SetUpsampleFactor(2);     // bilinear refinement before contouring
contour->SetMinContourLength(3.0); // drop speckle contours shorter than this
```

Smoothing is applied to the field the contours *and* the fill are computed from,
so the two never disagree.

## 3D contour surface

```cpp
#include "Plugins/Charts/UltraCanvasContourSurface3D.h"

auto surf = UltraCanvas::CreateContourSurface3DElement("surf1", 20, 20, 700, 460);
surf->SetData(values, cols, rows);
surf->SetLevelStep(20.0);
surf->SetSurfaceColorMode(UltraCanvas::SurfaceColorMode::Banded);
surf->SetShowWireframe(true);
surf->SetCategoryLabelsX({"A","B","C","D","E"});
surf->SetCategoryLabelsY({"East","North","South","West"});
surf->SetAxisTitles("Product", "Region", "Units");
surf->SetLegendMode(UltraCanvas::SurfaceLegendMode::Horizontal);
```

Isolines can be traced onto the surface itself:

```cpp
surf->SetSurfaceColorMode(UltraCanvas::SurfaceColorMode::Smooth);
surf->SetShowSurfaceIsolines(true, Color(150, 220, 255, 170), 0.9f);
surf->SetLighting(true, 0.5);
surf->SetZExaggeration(1.15);
```

Drag with the left mouse button to orbit and use the wheel to zoom, or set the
view directly with `SetCamera(yaw, pitch, distance)` / `ViewIsometric()` /
`ViewTop()` / `ViewFront()`.

### Why it is software-rendered

The surface is drawn with a painter's algorithm straight onto `IRenderContext`,
not through OpenGL. A height field has no cyclic overlaps, so sorting quads by
centroid depth resolves occlusion exactly, and contour surfaces are low-poly by
nature. The practical payoff is that the element works in every build — it does
not require `ULTRACANVAS_ENABLE_GL` — and text, axes and the legend are drawn by
the same renderer as the rest of the UI.

Isolines get correct occlusion for free: each cell's segments are emitted
immediately after that cell's quad, so they inherit the depth ordering of the
surface. The segments are bucketed by cell up front, which keeps that lookup
constant-time instead of quadratic.

The camera defaults to a distance of 4.6, which is what it takes to fit the
`[-1, 1]` ground box (corners at radius ~1.41) plus its axis labels at the
default field of view.

## Notes

- The algorithm layer (`ContourGrid`, `GenerateContourLevels`,
  `ExtractContourPolylines`, `ExtractContourSegments`, `ClipCellToBand`,
  `BuildContourGridFromPoints`) has no UI dependency and is unit tested against
  analytic fields.
- Marching squares disambiguates saddle cells (cases 5 and 10) using the cell
  centre average, so contours never cross each other at a saddle.
- Crossings are keyed by lattice edge index rather than by position, so
  stitching polylines is exact — no floating-point tolerance, no dangling
  fragments.
- Cells touching a `NaN` sample are skipped, leaving clean holes in the field;
  `SetNaNColor` controls how those holes are painted.
- Contour geometry is cached and rebuilt only when the data, levels or smoothing
  change. Any heatmap mutator that invalidates the raster invalidates the
  contours too, via the overridable `InvalidateRaster` hook.

## Demo

`Apps/DemoApp/UltraCanvasContourChartExamples.cpp` — seven tabs: an interactive
tab wired to every option, filled contours with inline labels, a kernel-density
contour with a band legend, a line-only contour coloured by level, a dark-theme
density plot with formatted axis ticks, and the two 3D surface variants.

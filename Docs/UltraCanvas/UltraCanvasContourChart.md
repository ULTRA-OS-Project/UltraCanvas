# UltraCanvasContourChart

Contour charts draw curves of equal value through a scalar field: isolines, or
the filled bands between them. Feed the element a regular grid, an analytic
function, or a raw point cloud it converts into a density field for you.

- 2D element: `include/Plugins/Charts/UltraCanvasContourChart.h` / `Plugins/Charts/UltraCanvasContourChart.cpp`
- 3D surface (software): `include/Plugins/Charts/UltraCanvasContourSurface3D.h` / `Plugins/Charts/UltraCanvasContourSurface3D.cpp`
- 3D surface (OpenGL): `include/Plugins/Charts/UltraCanvasContourSurfaceGL.h` / `Plugins/Charts/UltraCanvasContourSurfaceGL.cpp`
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

## Zoom, pan and the view window

The element shows a *view window* — a data-space sub-rectangle of the field.
By default that window is the whole field; the mouse (or the API) can narrow
it:

```cpp
contour->SetEnableZoom(true);    // wheel zooms about the cursor
contour->SetEnablePan(true);     // left-drag pans the zoomed window
contour->SetViewRange(-1.0, 1.0, -0.5, 0.5);   // or set the window directly
contour->ResetView();            // back to the full field (double-click does too)

double x0, x1, y0, y1;
bool zoomed = contour->GetViewRange(x0, x1, y0, y1);
```

The window is clamped to the data extents and axis ticks follow it. Because
the fill raster is re-sampled per screen pixel and isolines are vector
polylines, a zoomed view renders at full resolution rather than scaling up
pixels. Zoom and pan are ignored in `HeatmapWithContours` mode, which keeps
the base heatmap's whole-matrix cell layout.

## Crosshair and click callback

```cpp
contour->SetShowCrosshair(true);                       // follows the cursor
contour->SetCrosshairColor(Color(120, 170, 235, 200));

contour->SetOnContourClick([](const UltraCanvas::ContourClickInfo& info) {
    // info.x / info.y   data-space position
    // info.value        bilinear field value there
    // info.bandIndex    0 = below the first level .. levels = above the last
});
```

The crosshair draws dashed guide lines through the cursor and pins the
formatted x/y read-out to the axes (the tick formatters apply). The click
callback fires on a press-and-release without dragging, so it coexists with
panning.

## Exporting the contour geometry

```cpp
std::vector<UltraCanvas::ContourPolyline> paths = contour->ExportContourPolylines();
```

Returns the extracted isolines converted to *data-space* coordinates, with
each polyline's `level` and `closed` flag preserved — the hand-off format for
GIS/CAD or custom post-processing. (`GetContours()` returns the same geometry
in fractional grid coordinates.)

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

### The OpenGL variant

For finer grids or heavy orbiting there is a GL-backed sibling with the same
API surface:

```cpp
#include "Plugins/Charts/UltraCanvasContourSurfaceGL.h"

auto surf = UltraCanvas::CreateContourSurfaceGLElement("surf1", 20, 20, 700, 460);
surf->SetData(values, cols, rows);          // same setters as the software element
surf->SetSurfaceColorMode(UltraCanvas::SurfaceColorMode::Smooth);
surf->SetShowSurfaceIsolines(true);
surf->SetAutoRotate(true);                  // GL-only: continuous orbit
```

`UltraCanvasContourSurfaceGLElement` follows the `UltraCanvasSTLElement`
pattern: when the build defines `ULTRACANVAS_ENABLE_GL` it derives from
`UltraCanvasGLSurface` (EGL / OpenGL 3.3 core) and renders the mesh, wireframe
and on-surface isolines with a real depth buffer — flat per-quad colours in
`Banded` mode, per-node normals and colours in `Smooth` mode, and a polygon
offset on the fill so the lines win the depth test on the surface. The chart
title, 3D axes and band legend are drawn on top by the normal 2D renderer
through a CPU projection that matches the GL camera exactly, so text stays
crisp and theme-aware. Without `ULTRACANVAS_ENABLE_GL` the class simply
derives from the software `UltraCanvasContourSurface3DElement`, so calling
code compiles and runs unchanged in every build.

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

`Apps/DemoApp/UltraCanvasContourChartExamples.cpp` — eight tabs: an interactive
tab wired to every option (including zoom/pan, the crosshair and the click
callback), filled contours with inline labels, a kernel-density contour with a
band legend, a line-only contour coloured by level, a dark-theme density plot
with formatted axis ticks, the two software 3D surface variants, and the
GL-backed surface on the UC OpenGL element. Each tab's second description line
states what the mouse does there (zoom / pan / rotate) and which renderer
draws the tab.

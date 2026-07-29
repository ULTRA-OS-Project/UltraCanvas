# UltraCanvasContourChart — Research & Feature Proposal

Status: **proposal / design document** (no implementation yet)
Author: UltraCanvas Framework
Last Modified: 2026-07-29

---

## 1. What a contour chart is

A contour chart (contour plot, isoline plot, level plot) visualises a **scalar
field over two dimensions** — `z = f(x, y)` — on a flat 2D surface, by drawing
curves that connect points of equal `z`. Each curve is an *isoline* (or
*contour line* / *level curve*); the area between two neighbouring isolines is
an *isoband* (or *contour band*), which is normally filled with a colour taken
from a colour ramp.

The standard analogy is a topographic map: the isolines are height contours,
close spacing means a steep gradient, and concentric closed loops mark peaks
and basins.

There are two distinct data situations, and a complete contour element must
handle both:

| Source data | Meaning of `z` | Path to a contour |
|---|---|---|
| **Gridded field** — a value for every `(row, col)` of a regular lattice | The measured/computed quantity itself (temperature, elevation, correlation, signal power) | Contour the grid directly |
| **Scattered points** — an unordered list of `(x, y)` (optionally with a weight) | Usually the *density* of the points | Interpolate/bin the points onto a grid first, then contour |

The second case is what InetSoft calls a *contour chart* — "displays contours
that correspond to the density of the plotted points, while the points
themselves are not displayed"; it is effectively a smoothed, contoured 2D
histogram (a KDE) of a scatter plot, and it is how you show a dense scatter
without over-plotting.

The first case is the scientific/engineering one described by LightningChart —
isolines over a heatmap grid, where "a contour plot adds iso-lines connecting
equal-value points to emphasize gradients, ridges, and peaks, while a heatmap
simply colors a grid of cells."

Both reduce to the same core primitive: **given a scalar grid and a list of
levels, produce polylines (isolines) and polygons (isobands).**

Sources consulted:
[InetSoft — What Are Contour Charts, How to Make Them](https://www.inetsoft.com/info/how-to-make-a-contour-chart-definition-examples/),
[LightningChart — Contour Plot Essentials](https://lightningchart.com/blog/contour-plot-essentials/),
[LightningChart JS — Heatmap docs](https://lightningchart.com/js-charts/docs/features/xy/heatmap/),
[Statistics By Jim — Contour Plots](https://statisticsbyjim.com/graphs/contour-plots/),
[NVIDIA HeavyAI Immerse — Contour chart type](https://docs.nvidia.com/heavyai/immerse/immerse-chart-types/contour),
[Boyd, *Contour (Isoline) Plots*, Univ. of Michigan](https://public.websites.umich.edu/~jpboyd/eng403_chap4_contourplts.pdf).

---

## 2. What the five reference images demand

Each uploaded image maps to a concrete capability. Together they define the
scope.

### Image 1 — Filled contour with labelled isolines ("contourf")
Rainbow/Jet ramp, plot range x∈[-4,4], y∈[-4,4], filled bands **plus** black
isolines drawn on top, **inline numeric labels rotated along each line**
(`0.4`, `0.6`, `0.8`, `-0.4` …) with the line broken underneath the text, and a
**continuous vertical colour bar** ticked from -1.0 to 1.0.

> Requires: isoband fill + isoline stroke in one pass, inline rotated labels
> with line-gap, continuous colour bar, diverging value range.

### Image 2 — Density contour from a scatter (KDE), discrete legend
Single-hue `Blues` ramp, uniformly stepped levels (0.02), no isolines, and a
**discrete legend listing level intervals** — `[0.00, 0.02]`, `[0.02, 0.04]`, …
labelled "level". Smooth, nested, closed contours typical of a 2D kernel
density estimate over scattered `(x, y)` points.

> Requires: point→density gridding (KDE/binning), fixed level step, discrete
> band legend (swatch + interval text) as an alternative to the colour bar.

### Image 3 — Line-only contour, level-coloured lines
No fill. Every isoline is stroked in the colour of its own level from a
diverging ramp, labelled inline with 3-decimal values (`1.500`, `-1.200`), plot
title "My Plot", axis titles "X1"/"X2", continuous colour bar with ticks. The
negative branch reads as a separate basin from the positive one.

> Requires: lines-only mode, per-level stroke colour from the colormap,
> per-level stroke style (the classic convention: negative levels dashed, zero
> level emphasised), value label format control, axis titles.

### Image 4 — Dark-theme density contour over a scatter
A "glow"/soft-edged density contour on a black background over a `Population`
× `Median Income` scatter, with formatted axis ticks (`$27.50K`, `10.00M`).
Bands are smoothed heavily; a few faint outlier blobs are visible.

> Requires: theme-neutral styling (background/line colours all settable),
> smoothing/bandwidth control, per-band alpha, optional overlay of the source
> points, axis tick formatting hooks.

### Image 5 — Excel-style 3D contour (surface)
A 3D surface over two *categorical* axes (A…E and East/North/South/West), with
the surface coloured in **z-bands** (`0-20`, `20-40`, `40-60`, `60-80`,
`80-100`) listed in a bottom legend. Wireframe edges visible, drawn in
perspective with a floor.

> Requires: a 3D surface renderer with band colouring, category tick labels on
> the two ground axes, a band legend, camera/orbit. This is the branch that
> uses the UltraCanvas 3D element.

---

## 3. How this fits the existing UltraCanvas code

The framework already has most of the supporting machinery — the contour
element should **reuse, not duplicate**:

| Existing piece | Reuse for |
|---|---|
| `include/Plugins/Charts/UltraCanvasColormap.h` | All 19 built-in ramps (Viridis, Turbo, Jet, Blues, RdBu, Spectral …), `SampleColormap`, `QuantizeNorm`, `DivergingNorm`, `IsDivergingColormap` — no new colour code needed |
| `UltraCanvasHeatmapChartElement` | The grid data model (`SetData(flat, cols, rows)`, auto range, log scale, diverging midpoint, NaN colour), `RenderColorBar`, the `Cells`/`Image` render strategy, and its `UCPixmap` rasteriser for the smooth-fill mode |
| `UltraCanvasChartElementBase` | Plot area, data bounds, `ChartCoordinateTransform`, grid/axes/axis-labels, tooltips, zoom/pan flags, animation, empty state |
| `UltraCanvasHexbinChart` | The precedent for "reuse the heatmap for data+colour, override the geometry" — and for its dependency-free, unit-tested geometry header (`UltraCanvasHexLayout.h`) |
| `IRenderContext` | Everything the 2D renderer needs already exists: `FillLinePath`, `DrawLinePath`, path building + `ClipPath`, `SetLineDash`, `Rotate`/`SetTransform` (for inline rotated labels), gradients, alpha |
| `UltraCanvasGLSurface` + `Plugins/Models/STL/UltraCanvas3DTypes.h` (`Vec3`, `Mesh3D`, `BoundingBox3D`, `RecomputeNormals`) | The 3D surface variant — `UltraCanvasSTLElement` is the working reference for a GL-backed element with mouse-orbit and a non-GL fallback |
| `UltraCanvasLabelPlacement.h` | Collision-aware placement of the contour labels / legend entries |

**Important build constraint:** `UltraCanvasGLSurface.h` hard-`#error`s unless
`ULTRACANVAS_ENABLE_GL` is defined, and `UltraCanvasSTLElement` compiles a 2D
fallback when it is not. The 3D contour element must follow the same pattern,
so a build without GL still gets a working chart.

---

## 4. Proposed architecture

Four new units, mirroring the Hexbin precedent (pure algorithm headers that are
unit-testable without a window, plus thin UI elements):

```
include/Plugins/Charts/UltraCanvasContourGrid.h      # scalar field + level model  (no UI deps)
include/Plugins/Charts/UltraCanvasMarchingSquares.h  # isoline + isoband extraction (no UI deps)
include/Plugins/Charts/UltraCanvasFieldInterpolation.h # scattered points -> grid   (no UI deps)
include/Plugins/Charts/UltraCanvasContourChart.h     # 2D element  (ChartElementBase)
include/Plugins/Charts/UltraCanvasContourSurface3D.h # 3D element  (GLSurface / 2D fallback)
Plugins/Charts/UltraCanvasContourChart.cpp
Plugins/Charts/UltraCanvasContourSurface3D.cpp
Plugins/Charts/UltraCanvasMarchingSquares.cpp
```

### 4.1 Core algorithm — marching squares

* **Isolines** (image 3): the classic 16-case marching-squares table over each
  2×2 cell, with linear interpolation of the crossing point along each edge, and
  **saddle disambiguation** using the cell-centre average (the case-5/case-10
  ambiguity — getting this wrong is the single most visible contour bug).
  Segments are then stitched into ordered polylines, with closed loops detected
  and marked so fills and labels behave.
* **Isobands** (images 1, 2, 4): the 3-state (below / inside / above) variant —
  81 cases — emitting closed polygons per band directly. This is what
  matplotlib's `contourf` and `d3-contour` do; it is more robust than the
  "stroke thick lines" hack and gives correct polygons for hole-in-band cases.
* **Smoothing**: optional Chaikin / Catmull-Rom subdivision of the extracted
  polylines, and optional Gaussian pre-smoothing of the grid itself (image 4's
  soft look).
* Output is a plain geometry struct — no render context involved — so it is
  fully unit-testable (a known analytic field → expected level count, closed
  loop count, area monotonicity).

### 4.2 Level model

Levels can be specified four ways, all of which appear in the reference images:

1. `SetLevelCount(n)` — *n* evenly spaced levels across the value range (image 1).
2. `SetLevelStep(step, origin)` — fixed increment, e.g. every 0.02 (image 2) or
   every 20 units (image 5).
3. `SetLevels({...})` — explicit, possibly non-uniform list (e.g. contours at
   1, 2, 5, 10, 20, 50 for log-ish data).
4. `SetLevelMode(Quantile)` — levels at data quantiles, so each band holds an
   equal share of the data (useful for skewed density fields).

Plus "nice" level rounding (snap auto levels to 1/2/2.5/5×10ⁿ) so labels read
`0.20` rather than `0.1978`.

### 4.3 Scattered-data path

`SetPoints(points)` accepts raw `(x, y[, weight])` and gridding is chosen by
`SetGriddingMethod`:

* **KDE / Gaussian density** — for the density contour of images 2 and 4, with
  a settable bandwidth (and a Silverman/Scott automatic default).
* **Binning + smoothing** — cheap histogram, then a box/Gaussian blur.
* **IDW (inverse distance weighting)** — when the points carry a `z` value
  rather than representing density.
* **Nearest neighbour** and **bilinear/Delaunay-linear** — for measured samples.

### 4.4 3D surface path

`UltraCanvasContourSurface3D` builds a `Mesh3D` from the same
`UltraCanvasContourGrid`, with per-vertex colour from the band ramp:

* **Primary (GL) renderer** — subclass `UltraCanvasGLSurface` exactly as
  `UltraCanvasSTLElement` does: `OnGLInit`/`OnGLRender`/`OnGLCleanup`, mouse
  orbit, `ResetCameraToFit` from `BoundingBox3D`. Handles large grids and
  smooth shading.
* **Fallback (software) renderer** — a painter's-algorithm quad renderer using
  only `IRenderContext` (`FillLinePath` per quad, back-to-front sort, flat
  shading, wireframe edges). Excel-style charts (image 5) are low-poly by
  nature, so this path is entirely adequate and keeps the element usable in
  non-GL builds. **Recommendation: implement the software path first** — it is
  portable, needs no shaders, and matches the reference image; add the GL path
  for large/interactive grids.
* Shared extras: isolines projected onto the surface, a flat contour "shadow"
  projected onto the floor plane, and category tick labels on the two ground
  axes (image 5 uses categories, not numbers, on both).

---

## 5. Proposed feature list

Grouped, with a suggested delivery phase. **P1** = core, ship first; **P2** =
completes the reference images; **P3** = polish / advanced.

### 5.1 Data input
| # | Feature | Phase |
|---|---|---|
| D1 | Gridded input, row-major flat buffer + `cols`/`rows` (same signature as the heatmap) | P1 |
| D2 | Gridded input as `vector<vector<double>>` | P1 |
| D3 | Explicit x/y coordinate vectors for non-uniform grid spacing | P2 |
| D4 | Scattered `(x, y)` points → density grid (KDE, settable bandwidth + auto bandwidth) | P1 |
| D5 | Scattered `(x, y, z)` points → interpolated grid (IDW / nearest / linear) | P2 |
| D6 | Weighted points | P2 |
| D7 | `NaN` / missing-value support (holes in the field, excluded from contours) | P1 |
| D8 | Analytic source: `SetFunction(std::function<double(double,double)>, resolution)` | P2 |
| D9 | CSV / `IChartDataSource` loading consistent with the other chart elements | P2 |
| D10 | Categorical axes (labels instead of numbers on x and y, as in image 5) | P2 |
| D11 | Live/streaming update — replace the grid in place and re-contour incrementally | P3 |

### 5.2 Contour computation
| # | Feature | Phase |
|---|---|---|
| C1 | Marching squares isoline extraction with saddle-point disambiguation | P1 |
| C2 | Isoband (filled region) extraction as closed polygons | P1 |
| C3 | Polyline stitching, closed-loop detection, orientation/winding normalisation | P1 |
| C4 | Level modes: count, fixed step, explicit list, quantile | P1 |
| C5 | "Nice number" level rounding for auto levels | P1 |
| C6 | Linear / logarithmic value scaling (reuse `HeatmapScale`) | P1 |
| C7 | Diverging normalisation around a midpoint (reuse `DivergingNorm`) | P1 |
| C8 | Grid pre-smoothing (Gaussian, settable radius) for soft contours | P2 |
| C9 | Polyline smoothing (Chaikin / Catmull-Rom subdivision) | P2 |
| C10 | Grid upsampling (bilinear/bicubic) before contouring, for coarse inputs | P2 |
| C11 | Minimum-area / minimum-length culling of speckle contours | P3 |
| C12 | Cached geometry, invalidated only when data/levels/smoothing change | P1 |

### 5.3 2D rendering modes
| # | Feature | Phase |
|---|---|---|
| R1 | `Lines` — isolines only (image 3) | P1 |
| R2 | `Filled` — isobands only (image 2) | P1 |
| R3 | `FilledWithLines` — bands plus stroked isolines (image 1) | P1 |
| R4 | `Smooth` — continuous per-pixel colour fill (raster, no banding) with isolines on top; reuses the heatmap's `UCPixmap` path | P2 |
| R5 | `HeatmapWithContours` — cell heatmap underneath, isolines on top (LightningChart's headline combination) | P2 |
| R6 | Optional overlay of the source scatter points on top of the density contour (image 4) | P2 |
| R7 | Optional background raster/image beneath the contours (map underlay) | P3 |

### 5.4 Colour & style
| # | Feature | Phase |
|---|---|---|
| S1 | All existing colormaps via `SetColormap` / `SetCustomColormap` / `SetReverseColormap` | P1 |
| S2 | Per-band explicit colour override list | P2 |
| S3 | Per-band fill alpha (uniform and per-band) | P2 |
| S4 | Isoline colour: single fixed colour (image 1) **or** per-level from the ramp (image 3) | P1 |
| S5 | Per-level stroke width | P1 |
| S6 | Per-level dash pattern, with the convention *negative levels dashed* built in as a preset | P1 |
| S7 | Emphasised "major" contours every *n*-th level (thicker/darker), minor between — the topographic-map convention | P2 |
| S8 | Highlighted zero-level (or any nominated level) styling | P2 |
| S9 | `NaN` colour, background colour, plot-area colour, grid colour — all settable (dark theme, image 4) | P1 |
| S10 | Band boundary anti-aliasing / hairline seam suppression between adjacent bands | P2 |
| S11 | Hatch/pattern fill for a nominated band (accessibility, print) | P3 |

### 5.5 Labels
| # | Feature | Phase |
|---|---|---|
| L1 | Inline isoline labels rotated along the local tangent (images 1 & 3) | P1 |
| L2 | Line-break under the label (gap in the stroke) | P1 |
| L3 | Label value format control — decimals, prefix/suffix, custom formatter callback | P1 |
| L4 | Label placement policy: on the longest/straightest run, avoiding collisions, min-spacing along the line, repeat every *n* px on long contours | P2 |
| L5 | Per-level "label this level or not" control (label majors only) | P2 |
| L6 | Halo / contrasting outline behind label text for legibility over fills | P2 |
| L7 | Callout/leader-line labels outside the plot as an alternative to inline | P3 |

### 5.6 Legend / colour bar
| # | Feature | Phase |
|---|---|---|
| G1 | Continuous vertical colour bar with ticks (images 1 & 3) — reuse `RenderColorBar` | P1 |
| G2 | Discrete band legend: swatch + interval text `[0.00, 0.02]` (image 2) | P1 |
| G3 | Horizontal band legend below the plot (image 5) | P2 |
| G4 | Legend title (e.g. "level"), position (left/right/top/bottom), and orientation | P2 |
| G5 | Tick label formatting + custom tick positions on the colour bar | P2 |
| G6 | Interval text format: `[a, b]`, `a–b`, `a to b`, `> a` for the open top band | P2 |

### 5.7 Axes, titles, framing
| # | Feature | Phase |
|---|---|---|
| A1 | Chart title, x/y axis titles (image 3) | P1 |
| A2 | Numeric ticks + gridlines from `ChartElementBase` | P1 |
| A3 | Custom tick formatter (`$27.50K`, `10.00M` — image 4) | P2 |
| A4 | Categorical tick labels (image 5) | P2 |
| A5 | Explicit x/y display range (independent of data range) | P2 |
| A6 | Aspect-ratio lock (square cells / equal x-y scaling) | P2 |

### 5.8 Interaction
| # | Feature | Phase |
|---|---|---|
| I1 | Hover tooltip: interpolated `z` at the cursor plus the containing band interval | P1 |
| I2 | Hover highlight of the band or isoline under the cursor | P2 |
| I3 | Zoom + pan (reuse the base-class flags), re-contouring at the zoomed resolution | P2 |
| I4 | Click callback with `(x, y, z, levelIndex)` | P2 |
| I5 | Crosshair with live value read-out on both axes | P3 |
| I6 | Interactive level threshold drag (move a contour level and see it update) | P3 |
| I7 | Export the extracted contours as polylines (for GIS/CAD hand-off) | P3 |

### 5.9 3D contour surface
| # | Feature | Phase |
|---|---|---|
| T1 | Software (no-GL) painter's-algorithm surface renderer from the same grid | P2 |
| T2 | Band colouring of the surface by z-range (image 5) | P2 |
| T3 | Wireframe / mesh edges, toggleable | P2 |
| T4 | Category tick labels on both ground axes + z axis ticks | P2 |
| T5 | Horizontal band legend | P2 |
| T6 | GL-backed renderer on `UltraCanvasGLSurface` with mouse orbit, following the `UltraCanvasSTLElement` pattern and its non-GL fallback | P3 |
| T7 | Smooth (Gouraud) vs banded (flat) shading toggle | P3 |
| T8 | Isolines drawn on the 3D surface | P3 |
| T9 | Contour "shadow" projected onto the floor plane | P3 |
| T10 | Z exaggeration factor, camera presets (iso / top / front) | P3 |
| T11 | Lighting controls (direction, ambient) | P3 |

### 5.10 Engineering
| # | Feature | Phase |
|---|---|---|
| E1 | Algorithm headers free of UI dependencies, unit-tested against analytic fields | P1 |
| E2 | Geometry cache keyed on data/level/smoothing generation counter | P1 |
| E3 | Raster fast path for very large grids (reuse the heatmap `Image` mode threshold logic) | P2 |
| E4 | `Docs/UltraCanvas/UltraCanvasContourChart.md` in the house style | P1 |
| E5 | `Apps/DemoApp` example reproducing all five reference images | P1 |
| E6 | Factory helpers `CreateContourChartElement` / `CreateContourSurface3DElement` | P1 |

---

## 6. Proposed API sketch

```cpp
#include "Plugins/Charts/UltraCanvasContourChart.h"

auto contour = UltraCanvas::CreateContourChartElement("contour1", 20, 20, 640, 480);

// --- data: a regular grid, row-major (same as the heatmap) ---
contour->SetData(values, cols, rows);
contour->SetDataRange(-4.0, 4.0, -4.0, 4.0);      // x/y extents of the grid

// --- levels ---
contour->SetLevelCount(12);                        // or SetLevelStep(0.02)
                                                   // or SetLevels({-1.2,-0.9,...})

// --- render mode & colour ---
contour->SetRenderMode(UltraCanvas::ContourRenderMode::FilledWithLines);
contour->SetColormap(UltraCanvas::HeatmapColormap::Jet);
contour->SetDiverging(true, 0.0);

// --- isolines ---
contour->SetLineColor(UltraCanvas::Colors::Black);         // or SetLineColorFromLevel(true)
contour->SetLineWidth(1.0f);
contour->SetNegativeLevelsDashed(true);
contour->SetMajorLineEvery(5, 2.0f);                       // every 5th level, 2px

// --- labels ---
contour->SetShowLineLabels(true);
contour->SetLabelDecimals(1);
contour->SetLabelBreaksLine(true);

// --- legend ---
contour->SetLegendMode(UltraCanvas::ContourLegendMode::ColorBar);  // or DiscreteBands
contour->SetLegendTitle("level");

// --- titles ---
contour->SetChartTitle("My Plot");
contour->SetAxisTitles("X1", "X2");

container->AddChild(contour);
```

Density contour from a scatter (images 2 & 4):

```cpp
contour->SetPoints(points);                                    // vector<Point2Dd>
contour->SetGriddingMethod(UltraCanvas::ContourGridding::KDE);
contour->SetKDEBandwidth(0.0);                                 // 0 = auto (Scott's rule)
contour->SetGridResolution(128, 128);
contour->SetLevelStep(0.02);
contour->SetColormap(UltraCanvas::HeatmapColormap::Blues);
contour->SetRenderMode(UltraCanvas::ContourRenderMode::Filled);
contour->SetLegendMode(UltraCanvas::ContourLegendMode::DiscreteBands);
contour->SetShowSourcePoints(false);
```

3D contour surface (image 5):

```cpp
#include "Plugins/Charts/UltraCanvasContourSurface3D.h"

auto surf = UltraCanvas::CreateContourSurface3DElement("surf1", 20, 20, 700, 460);
surf->SetData(values, cols, rows);
surf->SetLevelStep(20.0);                       // 0-20, 20-40, ... bands
surf->SetCategoryLabelsX({"A","B","C","D","E"});
surf->SetCategoryLabelsY({"East","North","South","West"});
surf->SetShowWireframe(true);
surf->SetLegendMode(UltraCanvas::ContourLegendMode::DiscreteBandsHorizontal);
surf->SetCamera(/*yaw*/ 0.6f, /*pitch*/ 0.35f, /*zExaggeration*/ 1.0f);
```

---

## 7. Suggested delivery order

1. **Phase 1 — core 2D contour.** `UltraCanvasContourGrid` +
   `UltraCanvasMarchingSquares` (isolines and isobands, saddle handling,
   stitching) with unit tests against analytic fields; the
   `UltraCanvasContourChart` element with Lines / Filled / FilledWithLines,
   colormap reuse, inline rotated labels with line-break, colour bar **and**
   discrete band legend, KDE gridding for scattered points, tooltips, docs and a
   demo. This alone reproduces images 1, 2 and 3.
2. **Phase 2 — completeness.** Smoothing, heatmap+contour and smooth-raster
   modes, per-band styling, major/minor contours, tick formatters, categorical
   axes, zoom/pan, source-point overlay, and the **software 3D surface**
   (images 4 and 5).
3. **Phase 3 — advanced.** GL-backed 3D surface with orbit, floor-projected
   contours, interactive level dragging, contour export, hatch fills.

## 8. Open questions for review

1. **Element inheritance** — derive `UltraCanvasContourChartElement` from
   `UltraCanvasChartElementBase` (independent, cleanest) or from
   `UltraCanvasHeatmapChartElement` (inherits the grid model, colour bar and
   raster path for free, the way Hexbin does)? Recommendation: **derive from the
   heatmap**, matching the Hexbin precedent and giving `HeatmapWithContours`
   almost for free.
2. **3D renderer priority** — confirm the recommendation to build the software
   painter's-algorithm surface first (portable, matches image 5) with the GL
   path as a later enhancement, rather than requiring `ULTRACANVAS_ENABLE_GL`
   from day one.
3. **Legend component** — should the discrete band legend be a shared,
   reusable chart legend widget (useful to Hexbin, Heatmap and the Mekko chart
   too) rather than private to the contour element?

# Chart Legend Styles — Investigation for the Charts Engine

Status: investigation / proposal (no code changes yet)

This document investigates the legend styles available to UltraCanvas charts —
placement inside or outside the graph area; dot, square and line swatches;
single-row and multi-row flow; top, bottom, left and right positions — what
the framework already implements, where the gaps are, and how the Charts
engine (`UltraCanvasChartEngineElement`) should acquire the full style range.

## 1. Current state: three coexisting legend systems

### 1.1 The shared `ChartLegend` component

`include/Plugins/Charts/UltraCanvasChartLegend.h` +
`Plugins/Charts/UltraCanvasChartLegend.cpp` (v1.0.0, 2026-07-31).

A renderable helper (not a UIElement): the host owns one by value, calls
`Measure(ctx, area)` → `RemainingArea(area)` → `Render(ctx, area)` inside its
own paint pass. It already covers most of the requested style range:

| Requested style | Support |
|---|---|
| Outside the graph area | ✅ 12 placements: `Top/Bottom/Left/Right` × `Start/Center/End` — the legend box consumes space and `RemainingArea()` shrinks the plot |
| Inside the graph area | ✅ 4 inset placements: `InsetTopLeft/TopRight/BottomLeft/BottomRight` — float over the plot, consume nothing, optional translucent background + border box |
| Coloured dots | ✅ `LegendSwatch::Circle` (filled disc) and `Ring` (hollow disc, survives greyscale) |
| Squares | ✅ `LegendSwatch::Square` (filled rect, optional border) |
| Lines | ✅ `LegendSwatch::Line` (solid stroke) and `DashedLine` |
| Other swatches | ✅ `Marker` (diamond), `Glyph` (text codes like "Y"/"N"), `Gradient` (declared; see §3.2) |
| One row | ✅ horizontal flow keeps entries on one row while they fit |
| Multiple rows | ✅ horizontal flow wraps into as many rows as the width requires, rows centred |
| Vertical column | ✅ side placements stack one entry per row (`LegendOrientation::Auto`), or force `Horizontal`/`Vertical` |

Plus: a title, right-aligned value text per entry, interval ("band") entries
with three formats, `SetMaxEntries` with an "…and N more" overflow row, a
label formatter hook, `HitTest`/`ToggleEntryEnabled`/highlight interaction
state, and `Light()`/`Dark()`/`Monochrome()` style presets.

**Adoption**: only three diagram plugins use it (`UltraCanvasPacketDiagram`,
`UltraCanvasAdjacencyDiagram`, `UltraCanvasMatrixDiagram`). **No chart uses
it**, even though its header states it replaces the per-chart legend
implementations.

### 1.2 The Charts engine's built-in legend

`UltraCanvasChartEngineElement` renders its own legend in phase 3 (slot 800),
independent of `ChartLegend`:

- `SetShowLegend(bool)` + `SetLegendEntries(std::vector<ChartLegendEntry>)`.
- Placement is **hard-coded**: outside the plot, right edge, top-aligned,
  one vertical column (`legendRect.x = plot.Right() + 10`). No top/bottom/
  left, no inset, no horizontal flow, no wrapping, no title, no overflow
  handling, no interaction (no hit test, no click-to-toggle).
- Swatch styles are **fill-fidelity** oriented, which the shared component
  lacks: `ChartLegendSwatch::Solid | Gradient | Outline | Hatched | Image`
  — a series drawn hatched or with a gradient gets a swatch that matches its
  actual paint (the native bar chart and the demo's engine examples rebuild
  their legend from the series' `SeriesPaint`).
- Engine integration is done right and must be preserved: the legend's
  measured size is **reserved during the measure/solve layout negotiation**
  (`request.Reserve(ChartAxisEdge::Right, …)`), and the legend box is
  registered as a `ChartLabelClass::LegendEntry` **obstacle so the solved
  label plan steers around it**.

Users: `UltraCanvasParallelCoordinateChartElement` (the only Tier-2 chart so
far) and the DemoApp engine/bar-chart examples.

### 1.3 Legacy per-chart legends

Twelve chart/diagram sources still carry a private `RenderLegend()` /
`DrawLegend()` with eight mutually incompatible position enums
(`CircularLegendPosition`, `CFDLegendPosition`, `DumbbellLegendPosition`,
`FunnelLegendPosition`, `MekkoChart::LegendPosition`, `PolarLegendPosition`,
`PyramidLegendPosition`, plus stringly-typed positions in
`UltraCanvasPopulationChart`). These are Tier-0 charts the engine migration
plan deliberately leaves untouched; they are listed here only so the target
picture is complete.

## 2. Gap matrix — requested styles vs. the engine

| Requested style | Shared `ChartLegend` | Engine built-in legend |
|---|---|---|
| Outside: top / bottom / left / right | ✅ ×3 alignments each | ❌ right only |
| Inside the graph area (inset corners) | ✅ 4 corners | ❌ |
| Coloured dots | ✅ Circle, Ring | ❌ |
| Squares | ✅ | ✅ (only style) |
| Lines (solid / dashed) | ✅ | ❌ |
| Swatch mirrors series paint (gradient/outline/hatched/image) | ❌ | ✅ |
| One row (horizontal) | ✅ | ❌ |
| Multiple rows (wrap) | ✅ | ❌ |
| Vertical column | ✅ | ✅ |
| Vertical multi-column | ❌ (see §3.1) | ❌ |
| Title / overflow row / value text | ✅ | ❌ |
| Click-to-toggle / hover highlight | ✅ (hit test + state) | ❌ |
| Layout-negotiated space reservation | ❌ (host does it) | ✅ |
| Label-solver obstacle registration | ❌ | ✅ |
| Colour-bar / size-legend modes (proposal §"SetLegendMode") | ❌ | ❌ |

Neither system alone covers the requested range; between them, almost
everything exists once.

## 3. Defects found during the investigation

### 3.1 Vertical flow never adds a column

`ChartLegend::Measure` comments *"Vertical: one entry per row unless it does
not fit the height, in which case we add a column"* — but the code
unconditionally emits one entry per row and then clamps the box height to the
available area, so entries that do not fit are silently clipped. Tall legends
on Left/Right placements need the promised column overflow (or an automatic
fallback to `SetMaxEntries`-style "…and N more").

### 3.2 `LegendSwatch::Gradient` renders as a plain square

The header promises "the entry's own mini colour ramp (band legends)", but
`DrawSwatch` lets `Gradient` fall through to the `Square` case. Band legends
currently get a flat colour patch.

### 3.3 ODR hazard: two `UltraCanvas::ChartLegendEntry` types

Both `UltraCanvasChartLegend.h` and
`Engine/UltraCanvasChartEngineElement.h` define a struct named
`UltraCanvas::ChartLegendEntry` — with different fields. No translation unit
currently includes both headers, but any file that ever does (the DemoApp is
one include away) breaks with a redefinition error. This must be resolved
before the two systems can meet.

### 3.4 The engine proposal's legend API was never built

`UltraCanvasChartEngineProposal.md` specifies
`SetLegendPosition(Left|Right|Top|Bottom|Inside|Custom)` and
`SetLegendMode(Discrete|ColorBar|SizeLegend|PinnedItems)` under
`Engine/UltraCanvasChartLegend.h`. Neither the file nor either API exists;
the shared component landed outside `Engine/` and the engine grew the minimal
built-in instead.

## 4. Recommendation: one legend, engine-hosted

Make the shared `ChartLegend` the engine's phase-3 legend renderer, keeping
the engine's layout negotiation and obstacle registration. Concretely:

1. **Resolve the name clash.** Migrate the engine to the shared
   `ChartLegendEntry` and retire its private struct. The engine's
   fill-fidelity swatches move into the shared vocabulary: extend
   `LegendSwatch` with `Hatched` and `Image` (add `imagePath` to the entry),
   implement the promised `Gradient` ramp (§3.2), and map the engine's
   `Solid → Square`, `Outline → Ring`-style hollow square. `ChartLegendSwatch`
   remains as a deprecated alias until the demo apps migrate.
2. **Engine API.** `UltraCanvasChartEngineElement` gains
   `SetLegendPosition(ChartLegendPosition)`,
   `SetLegendOrientation(LegendOrientation)`,
   `SetLegendTitle(const std::string&)` and
   `ChartLegend& Legend()` for full styling access;
   `SetShowLegend`/`SetLegendEntries` keep their signatures. Default stays
   `RightStart` + vertical, so existing engine charts render unchanged.
3. **Layout integration.** In the measure pass the engine calls
   `legend.Measure(ctx, contentArea)` and reserves the measured extent on the
   edge the position implies (`Top*` → `ChartAxisEdge::Top`, etc.). Inset
   positions reserve nothing and render over the plot after phase 2. In both
   cases the measured `layout.box` is registered as the
   `ChartLabelClass::LegendEntry` obstacle, exactly as today.
4. **Interaction.** Wire the engine's existing mouse path to
   `legend.HitTest(point)`: hover sets the highlighted entry
   (bold, repaint via `ChartDirty::Hover`), click calls
   `ToggleEntryEnabled` and notifies the chart through a new
   `OnLegendEntryToggled(size_t index, bool enabled)` virtual so series can
   be hidden/shown — the standard legend affordance every charting library
   ships.
5. **Fix §3.1** (vertical column wrap) in the shared component — it benefits
   the three diagrams already using it as well.
6. **Later, per the original proposal:** `ColorBar` and `SizeLegend` modes as
   siblings of the discrete legend (the contour/surface charts' colour bars
   are the consumers), and Tier-2 chart migrations then pick the shared
   legend up for free.

Steps 1–3 are mechanical and low-risk (the engine legend's current feature
set is a strict subset of the shared component's); step 4 is the only new
behaviour. Together they give every engine chart the full requested style
range — inside/outside placement on all four sides with three alignments,
dot/ring/square/line/dashed/marker/glyph/gradient/hatched/image swatches, and
single-row, wrapped multi-row or column flow — from one implementation.

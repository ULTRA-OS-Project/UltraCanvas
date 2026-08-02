# UltraCanvasChartEngine Documentation

## Overview

`UltraCanvasChartEngineElement` is the chart engine's render driver: a chart
type derives from it, implements **phase 2** — its own drawing — plus small
descriptor methods, and receives everything else. Phase 1 (background, grid
derived from the axis ticks, limiter lines, axes with decluttered tick labels)
and phase 3 (the solved label plan, the legend, the interaction overlay) are
engine-supplied, and phase 2 is clipped to the plot area so a chart cannot
paint over the axes or legend by accident.

Live demo: **Charts → Chart Engine** in the demo application
(`Apps/DemoApp/UltraCanvasChartEngineExamples.cpp`) — one ~90-line bar-chart
class rendered under the Vertical, Horizontal and Polar projections. The first
production client is the parallel coordinate chart
([`UltraCanvasParallelCoordinateChart.md`](UltraCanvasParallelCoordinateChart.md)).

- Driver: `include/Plugins/Charts/Engine/UltraCanvasChartEngineElement.h`
- Axis model: `include/Plugins/Charts/Engine/UltraCanvasChartAxis.h`
- Projections: `include/Plugins/Charts/Engine/UltraCanvasChartProjection.h`
- Label policy/plan: `include/Plugins/Charts/Engine/UltraCanvasChartLabels.h`
- Model-layer tests: `Tests/ChartEngineTest.cpp` (`ctest -R ChartEngineTest`)
- Design record: [`UltraCanvasChartEngineProposal.md`](UltraCanvasChartEngineProposal.md)

**Version:** 1.0.0
**Last Modified:** 2026-08-02
**Author:** UltraCanvas Framework
**Namespace:** `UltraCanvas`

## The content contract

```cpp
class MyChart : public UltraCanvasChartEngineElement {
    // Called only when Data or Style is dirty - never per frame.
    void DescribeAxes(ChartAxisSet& axes) override;

    // Phase 2. Already clipped to frame.plotArea.
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;

    // Optional: extra margin (measure pass), labels for the solved plan,
    // hover/brush overlay, named-property reactions, events.
    void MeasureContent(IRenderContext*, ChartLayoutRequest&) override;
    void CollectChartLabels(IRenderContext*, ChartLabelBroker&) override;
    void RenderInteractionOverlay(IRenderContext*, const ChartEngineFrame&) override;
    void OnEnginePropertyChanged(const std::string&, const UCPropertyValue&) override;
};
```

`ChartEngineFrame` is the frozen frame every phase renders against: plot area,
axes, projection, solved label plan, animation progress. It is read-only while
drawing; content maps its data through `frame.projection->ToScreen()` on
normalised `(u, v)` coordinates, which is what makes one implementation work
under every projection.

## Axes

`ChartAxis` covers the range/scale/tick/format behaviour ten legacy charts each
reimplemented:

| Facility | API |
|---|---|
| Range | `Observe(values)` for auto (nice-number rounded, NaN-safe, degenerate columns padded) or `SetRange(lo, hi)` |
| Scales | `Linear`, `Log`, `SymLog`, `Percentile`, `ZScore`, `RobustZScore`, `Category` |
| Placement | `side` (Left/Right/Top/Bottom edge) or `inPlot` + `plotPosition` (parallel coordinates) |
| Inversion | `inverted` |
| Ticks | `GenerateTicks(n)` - nice steps, decades on Log, one per category; **explicit `tickValues` override everything** (category slots under bars, hand-picked thresholds) |
| Formatting | `decimals`, `unitPrefix`/`unitSuffix`, `compactNumbers` (`$1.3M`), or a custom `formatter` |
| Endpoint labels | `showEndpointLabels` (in-plot axes) |

`Normalize(value)` maps into `[0,1]`; out-of-range values map outside rather
than clamping. Tick labels are decluttered by the 1-D pass
(`PriorityGreedy`: range ends and zero survive longest) — never by the 2-D
solver.

## Projections

`SetProjectionKind(...)`: `Vertical` (domain right, value up), `Horizontal`
(the same content transposed), `Polar` (domain around the circle, value
outward; `Space3D` reserved). Content that draws its shapes as subdivided
projected edges renders correctly under all of them — the demo's bars become
ring sectors under Polar with no chart-side change.

## Phase-1 services

- **Grid follows ticks**: `SetGridAxis(index)` derives gridlines from that
  axis's ticks, so grid and labels cannot disagree.
- **Limiters**: `AddLimiter({kind, axisIndex, value, color, width, dashed,
  caption})` draws reference lines (average, target, threshold...); captions
  are solved into the label plan so they never overprint value labels.
- **Layout is measured, not guessed**: axis tick labels, titles and the legend
  reserve their measured space in the measure pass; `MeasureContent` adds
  chart-specific needs. `SolvePlotArea` never returns an empty plot.

## Phase-3 services

- **Label plan**: labels submitted in `CollectChartLabels` go through the
  collision solver **once per invalidation, never per frame**. `allowSuppress`
  drops labels that would overprint (set `priority` on the ones that must
  survive); leaders and rotation are drawn by the engine.
- **Legend**: `SetShowLegend(true)` + `SetLegendEntries({{label, color}, ...})`
  — measured, reserved in layout, and an obstacle the solved labels avoid.
- **Interaction overlay**: `RenderInteractionOverlay` draws hover emphasis,
  crosshairs, brush bands — last, over everything.

## The dirty model

`MarkEngineDirty(flags)` with `ChartDirty::Data | Geometry | Style | Selection
| Hover | Animation`. Only Data/Geometry/Style rebuild the layout and the label
plan; hover, selection and animation repaint from the frozen frame. A resize is
detected automatically.

## Named properties

The engine implements `IConfigurableElement`: subclasses `Define()` keys in
their constructor (the engine provides `"title"`), react in
`OnEnginePropertyChanged`, and by-name creators (registry, templates, module
hosts) configure the chart with `SetProperty("key", value)` across any module
boundary.

## Best Practices

1. Draw with normalised coordinates through `frame.projection` — never compute
   pixels from the plot rect yourself, or Horizontal/Polar will be wrong.
2. Mark the right dirtiness: data changes `Data`, restyling `Style`,
   hover/selection only what they are. The label solver's cost then never
   lands on the frame path.
3. Submit measured label sizes (`GetTextLineDimensions`) and let the plan
   declutter with `allowSuppress` instead of drawing every label.
4. Keep `DescribeAxes` a pure description of the data — it runs only on
   Data/Style invalidation, and the engine finalizes the axes afterwards.

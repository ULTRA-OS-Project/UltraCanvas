# UltraCanvasChartEngine Documentation

## Overview

`UltraCanvasChartEngineElement` is the chart engine's render driver: a chart
type derives from it, implements **phase 2** — its own drawing — plus small
descriptor methods, and receives everything else. Phase 1 (background, grid
derived from the axis ticks, limiter lines, edge axes with decluttered tick
labels) and phase 3 (in-plot axes, the solved label plan, the legend, the
interaction overlay) are engine-supplied, and phase 2 is clipped to the plot
area so a chart cannot paint over the edge axes or legend by accident. In-plot
axes render **above** the content, so a dense chart cannot bury its own axis
rules and value labels under its data marks.

Live demo: **Charts → Chart Engine** in the demo application
(`Apps/DemoApp/UltraCanvasChartEngineExamples.cpp`) — one bar-chart class
implementing nothing but the content contract, with an option panel that
switches every engine service live: the three projections, the axis scales
(including `Log` and `SymLog`), number formatting, the grid source, tick
density, bar arrangement and slot geometry, limiters, the label plan's
declutter, legend swatches, hit-region tooltips, the animation driver, the
dirty model and the named-property surface. The first production client is the
parallel coordinate chart
([`UltraCanvasParallelCoordinateChart.md`](UltraCanvasParallelCoordinateChart.md)).

- Driver: `include/Plugins/Charts/Engine/UltraCanvasChartEngineElement.h`
- Axis model: `include/Plugins/Charts/Engine/UltraCanvasChartAxis.h`
- Bar series geometry: `include/Plugins/Charts/Engine/UltraCanvasChartSeries.h`
- Projections: `include/Plugins/Charts/Engine/UltraCanvasChartProjection.h`
- Label policy/plan: `include/Plugins/Charts/Engine/UltraCanvasChartLabels.h`
- Themes and palettes: `include/Plugins/Charts/Engine/UltraCanvasChartTheme.h`
- Model-layer tests: `Tests/ChartEngineTest.cpp` (`ctest -R ChartEngineTest`)
- Design record: [`UltraCanvasChartEngineProposal.md`](UltraCanvasChartEngineProposal.md)

**Version:** 1.3.0
**Last Modified:** 2026-08-20
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
| Slot padding | `categoryPadding` (Category scale, slot units): `0.5` extends the range half a slot past the outer categories, so bars and box plots centred on them keep their full footprint - no hand-rolled padded linear axis |
| Formatting | `decimals`, `unitPrefix`/`unitSuffix`, `compactNumbers` (`$1.3M`), or a custom `formatter` |
| In-plot value labels | `showTickLabels` (integrated tick labels along the rule) **or** `showEndpointLabels` (min/max at the ends) — never both: endpoint labels are suppressed while the integrated axis is active, so the extremes are not printed twice |

`Normalize(value)` maps into `[0,1]`; out-of-range values map outside rather
than clamping. Tick labels are decluttered by the 1-D pass
(`PriorityGreedy`: range ends and zero survive longest) — never by the 2-D
solver.

**Logarithmic axes** are first-class: `scale = ChartScale::Log` (base from
`logBase`, default 10) positions by decade and `GenerateTicks` emits one tick
per decade, thinned toward the requested count, so labels read 1, 10, 100, 1K…
Non-positive samples are clamped to a floor (`1e-12`) rather than producing
NaN, which has two consequences worth planning for:

- **Set the range explicitly.** A log axis has no meaningful zero, so an
  auto range fed a zero (which `ObserveBarSeries` supplies, because bars grow
  from it) stretches down to the floor. Pin it to whole decades instead —
  `axis.SetRange(pow(10, floor(log10(minPositive))), pow(10, ceil(log10(max))))`.
- **Grow marks from the axis floor.** `Normalize(0)` lands far below the plot,
  so clamp the base edge of a bar or area to `[0,1]` under a log scale.

`ChartScale::SymLog` is the variant for signed data: linear within
`symLogThreshold` of zero, logarithmic beyond, so negatives keep their sign
instead of being clamped away. The demo's **Value axis** row switches
Linear / Log / SymLog / Percentile over datasets chosen to suit each.

## Projections

`SetProjectionKind(...)`: `Vertical` (domain right, value up), `Horizontal`
(the same content transposed — note the domain runs **downward**, so the first
category is the top row), `Polar` (domain around the circle, value outward;
`Space3D` reserved). Content that draws its shapes as subdivided projected
edges renders correctly under all of them — the demo's bars become ring
sectors under Polar with no chart-side change. Edge-axis furniture follows the
projection too: the engine derives which end of an edge carries the low value
from the projection rather than assuming.

Polar has no edges to hang furniture on, so an **edge** axis would be drawn as
a straight rule along the rectangular plot bounds beside a round chart. Give
the value axis `inPlot = true` there instead — in-plot axes are mapped through
the projection, so it comes out as a radial rule with its ticks along it — and
stand the category axis down, naming its slots with annotation labels around
the rim (what the demo's Polar mode does).

## Phase-1 services

- **Grid follows ticks**: `SetGridAxis(index)` derives gridlines from that
  axis's ticks, so grid and labels cannot disagree.
- **Limiters**: `AddLimiter({kind, axisIndex, value, color, width, dashed,
  caption})` draws reference lines (average, target, threshold...); captions
  are solved into the label plan so they never overprint value labels.
- **Layout is measured, not guessed**: axis tick labels, titles and the legend
  reserve their measured space in the measure pass; `MeasureContent` adds
  chart-specific needs. `SolvePlotArea` never returns an empty plot. The chart
  title owns an exclusive band above every other top reservation, so axis
  endpoint labels can never overprint it.

## Bar series geometry

`UltraCanvasChartSeries.h` owns the arithmetic every bar-family chart used to
reimplement. `ChartBarLayoutOptions` picks the arrangement — `Grouped`
(clustered), `Stacked` or `PercentStacked` — plus `slotFill` and `groupGap`:

```cpp
ChartBarLayoutOptions options;
options.arrangement = ChartBarArrangement::Stacked;

// DescribeAxes: the value range follows the arrangement (all values for
// grouped, the signed stack totals for stacked, percent extremes for 100%).
ObserveBarSeries(valueAxis, seriesValues, options);

// RenderChartContent: every bar as a normalised (u, v) span.
for (const ChartBarSpan& span : BuildBarSpans(valueAxis, categoryAxis,
                                              categoryCount, seriesValues, options)) {
    // span.u0/u1 = domain extent, span.v0/v1 = base edge -> value edge,
    // span.value = the datum, span.plotted = what is shown (percent share)
}
```

Negative values are first-class: grouped bars grow downward from zero, stacked
bars accumulate positives upward and negatives downward (a diverging stack),
and percent stacking shares out the absolute total. Spans are normalised, so
the same spans render under Vertical, Horizontal and Polar.

`BuildBarOutline(projection, u0, v0, u1, v1, subdivisions, cornerRadiusPx)`
turns a span into its screen outline under any projection — a rectangle when
orthogonal, a ring sector under Polar. A non-zero corner radius rounds the
four corners in screen space by trimming along the (possibly curved) edges and
bridging with a sampled fillet, so rounded bars are rounded ring sectors under
Polar rather than a fallback. The radius self-clamps against short edges, so a
bar collapsing mid-animation degrades gracefully.

## Phase-3 services

- **Label plan**: labels submitted in `CollectChartLabels` go through the
  collision solver **once per invalidation, never per frame**. `allowSuppress`
  drops labels that would overprint (set `priority` on the ones that must
  survive); leaders and rotation are drawn by the engine. Solved labels are
  kept within the plot area **plus whatever margins the chart reserved for
  itself in `MeasureContent`** (`SolveLabelBounds`): reserve a text-height
  band on the value-axis end and a bar that reaches the axis maximum keeps
  its value label just above the plot edge, instead of having it pushed down
  onto the bar. Axis bands, the title band and the legend margin stay out of
  bounds (the legend also rides the plan as an obstacle).
- **Legend**: `SetShowLegend(true)` + `SetLegendEntries({{label, color}, ...})`
  — measured, reserved in layout, and an obstacle the solved labels avoid.
  `ChartLegendEntry.swatch` picks how the swatch is painted —
  `Solid | Gradient | Outline | Hatched | Image` (`imagePath` for `Image`) —
  so a non-solid series is represented faithfully.
- **Interaction overlay**: `RenderInteractionOverlay` draws hover emphasis,
  crosshairs, brush bands — last, over everything.

## Themes and palettes

`Engine/UltraCanvasChartTheme.h` is the one home for chart theming — the
place the engine proposal (§5.2) reserved. A `ChartTheme` bundles the
furniture colours every engine layer paints with (background, plot area,
grid, axis lines/ticks/labels, title, legend) with a `ChartPalette`, the
ordered series colours the content draws from. The engine's protected colour
fields are working copies filled from the active theme, so a chart can still
override a single colour after setting one.

```cpp
chart->SetTheme(ChartThemes::Dark());       // by value
chart->SetTheme("Ocean");                    // by name (case-insensitive);
                                             // returns false when unknown
chart->SetProperty("theme", "Vibrant");     // the named-property surface
chart->SetPalette(ChartThemes::Get("Colorblind").palette);  // palette only,
                                             // furniture kept
ChartThemes::Names();                        // the 14 built-in names
```

A theme is colours only, so a theme change is **repaint-only**: no layout, no
label re-solve (proposal §5.7). The engine calls the `OnThemeChanged()` hook
so content can refresh anything it cached from the theme — legend entry
colours above all (they are stored in `ChartLegendEntry`, not looked up at
draw time). The frozen frame carries `frame.theme` for phase-2 drawing.

The built-in themes are the palettes the pre-engine charts each carried
privately, lifted verbatim: **Light** (default furniture, Paul Tol bright),
**Dark** (dark furniture with the git graph's dark lane colours),
**Corporate**, **Vibrant**, **Pastel**, **Colorblind** (Okabe–Ito),
**Material** (10), **Classic** (10, Chart.js), **Tableau** (10), and the
light-to-dark ramps **Ocean**, **Sunset**, **Forest**, **Slate**,
**Monochrome** (6).

Series colours come from the palette's two lookups:

- `Palette().ColorAt(i)` — the classic cycle, except wrapped cycles are
  re-tinted (lightened, then darkened, progressively), so element 9 of an
  8-colour palette is not a repeat of element 1.
- `Palette().ColorAt(i, count)` — tell the palette how many elements the
  chart draws and it chooses better: a hand-designed categorical list keeps
  its first `count` colours (the order is deliberate), while a ramp palette
  (`isRamp`) spreads the picks across the whole run, ends included — three
  elements get dark / mid / light instead of the three nearly-equal darkest
  entries. `ColorsFor(count)` returns the whole list at once.

Eight predefined colours per palette is the deliberate default — it is both
the dominant width among the palettes the charts already carried and about
the ceiling of what stays distinguishable side by side; the wide 10-colour
lists (Material, Classic, Tableau) exist for pie/sunburst-style charts with
many slices. Past any predefined list, sample a continuous colormap instead:
`ChartPalette::FromColormap(HeatmapColormap::Viridis, 24)` builds a
categorical palette of any requested size from `UltraCanvasColormap`'s maps
(continuous value→colour mapping itself — heatmaps, colour bars — stays with
`SetColormap`/`SampleColormap`).

## Hit regions and tooltips

Content registers its interactive geometry while rendering phase 2 (the engine
clears the list first): `AddHitRegion(rect, id, tooltip)` for rectangular
marks, `AddHitRegion(polygon, id, tooltip)` for projected shapes (ring
sectors, wedges). The engine then owns hover: MouseMove hit-tests the regions
(last added wins — draw order), a change repaints via `ChartDirty::Hover`
without re-running layout or the label solver, `HoveredRegionId()` tells the
content what to emphasise, `OnHitRegionHoverChanged` notifies subclasses, and
a region's tooltip is shown through the standard tooltip manager whenever
tooltips are enabled (`SetEnableTooltips`).

## Animation

`StartEngineAnimation(seconds)` drives `frame.animationProgress` from 0 to 1
(ease-out cubic) and keeps repainting until it lands — no chart-side timers.
`SetAnimateOnDataChange(true, seconds)` restarts it on every
`ChartDirty::Data`. Content scales its geometry by the progress (bars grow
from the zero line); charts that ignore it simply render finished frames.

The frames come from a ~60fps periodic application timer the engine owns and
stops when the animation lands. This matters if you write a driver of your
own: `RequestRedraw()` from inside a paint cannot produce the next frame,
because the event loop blocks until a native event or a timer wakes it — an
animation left to redraw itself freezes at whatever progress its last paint
happened to see, and only unfreezes when something unrelated repaints.

## Image paint patterns

`IRenderContext::CreateImagePattern(path, anchorRect, fitMode, repeat)`
returns a paint pattern that floods **any path** with an image — fill a
projected quad and the texture survives Polar's ring sectors, where a
rect-clip + `DrawImage` cannot follow. Backends without image-pattern support
return `nullptr`; fall back to a plain fill.

## The dirty model

`MarkEngineDirty(flags)` with `ChartDirty::Data | Geometry | Style | Selection
| Hover | Animation`. Only Data/Geometry/Style rebuild the layout and the label
plan; hover, selection and animation repaint from the frozen frame. A resize is
detected automatically.

## Named properties

The engine implements `IConfigurableElement`: subclasses `Define()` keys in
their constructor (the engine provides `"title"` and `"theme"` — the latter
resolves a built-in theme name and rejects unknown ones), react in
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

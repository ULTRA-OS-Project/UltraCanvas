# UltraCanvasChartEngine Documentation

## Contents

- [Overview](#overview)
- [The content contract](#the-content-contract)
- [Axes](#axes)
- [Projections](#projections)
- [Highlights (slot 200 wash / slot 700 overlay)](#highlights-slot-200-wash--slot-700-overlay)
- [Phase-1 services](#phase-1-services)
- [Bar series geometry](#bar-series-geometry)
  - [Bar connections](#bar-connections)
- [Line and area series](#line-and-area-series)
- [Phase-3 services](#phase-3-services)
- [Themes and palettes](#themes-and-palettes)
- [Hit regions and tooltips](#hit-regions-and-tooltips)
- [Animation](#animation)
- [Image paint patterns](#image-paint-patterns)
- [The dirty model](#the-dirty-model)
- [Named properties](#named-properties)
- [Best Practices](#best-practices)

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
([`UltraCanvasParallelCoordinateChart.md`](UltraCanvasParallelCoordinateChart.md));
the engine ports of the line, area, scatter, bubble, waterfall, diverging,
dumbbell, financial, heatmap, Mekko, radar, radial bar, pie and polar charts are
described in [`UltraCanvasEngineCharts.md`](UltraCanvasEngineCharts.md) and
shown next to the legacy charts on **Charts → Chart Engine Comparison**.

- Driver: `include/Plugins/Charts/Engine/UltraCanvasChartEngineElement.h`
- Axis model: `include/Plugins/Charts/Engine/UltraCanvasChartAxis.h`
- Bar, line and area series geometry, markers: `include/Plugins/Charts/Engine/UltraCanvasChartSeries.h`
- Trend lines, moving averages, Heikin-Ashi: `include/Plugins/Charts/Engine/UltraCanvasChartAnalysis.h`
- Projections: `include/Plugins/Charts/Engine/UltraCanvasChartProjection.h`
- Label policy/plan: `include/Plugins/Charts/Engine/UltraCanvasChartLabels.h`
- Themes and palettes: `include/Plugins/Charts/Engine/UltraCanvasChartTheme.h`
- Model-layer tests: `Tests/ChartEngineTest.cpp` (`ctest -R ChartEngineTest`)
- Design record: [`UltraCanvasChartEngineProposal.md`](UltraCanvasChartEngineProposal.md)

**Version:** 1.4.0
**Last Modified:** 2026-10-20
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
than clamping.

Fields that cover what the ported charts each did by hand:

| Field | Effect |
|---|---|
| `role` | `ChartAxisRole::Domain` / `Value` / `Auto` (default): which normalised coordinate the axis maps. `Auto` reads it from the side - Bottom/Top are domain, Left/Right value, transposed under `Horizontal`. Only Polar needs it (see [Projections](#projections)) |
| `bandStart`, `bandEnd` | The sub-band of the value direction the axis occupies - stacked panes in one plot. A price axis on `0.3..1` above a volume axis on `0..0.22`: content, ticks, grid and the edge rule all land in the band |
| `symmetric` | Auto ranges only: widen to `[-m, m]` around zero, so diverging bars and pyramids keep zero in the middle |
| `cyclic` | Category scale only: the range runs `0..n`, one empty slot past the last, so a full-circle Polar projection spreads `n` spokes evenly with the first at the start angle - the radar layout |

```cpp
ChartAxis price("price");
price.side = ChartAxisSide::Left;
price.bandStart = 0.3;                 // upper 70% of the plot
ChartAxis volume("volume");
volume.side = ChartAxisSide::Right;
volume.bandEnd = 0.22;                 // the strip underneath
volume.compactNumbers = true;
```

The remaining fields of the same kind: `absoluteLabels` prints magnitudes
(both sides of a diverging axis read positive), `rangePadding` widens an auto
range by a fraction of its span so marks at the extremes are not cut by the
plot edge (never across a zero baseline), and `ChartScale::Sqrt` is a signed
square root for area-true radii. An auto range is rounded once, by its step
(0..1072 becomes 0..1200), and a prefixed negative reads `-$25`, not `$-25`.

Tick labels are decluttered by the 1-D pass
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

Polar has no edges to hang furniture on, so under Polar an **edge** axis is
drawn by its role instead of its side (`IsDomainAxis(axis)` answers which):

- a **domain** axis labels the rim - the outer ring is its rule, each tick a
  short outward mark with its label pushed outward along the radius, and the
  measure pass reserves room for those labels on all four sides. Labels that
  would overprint one already placed are dropped, and `0°`/`360°` on a full
  circle are one spoke;
- a **value** axis becomes the radial rule at the start angle (`u = 0`), with
  its ticks along it - exactly where an in-plot axis at position 0 stands.

So a chart written for the vertical projection (category axis on the bottom,
value axis on the left) gets round furniture under Polar with no chart-side
change. The older idiom - value axis `inPlot = true`, category axis hidden,
slot names as rim annotations (the engine demo's Polar mode) - still works.

`SetPolarAngles(startDegrees, sweepDegrees, clockwise)` and
`SetPolarInnerRadius(fraction)` reach the polar projection (`-90` = 12
o'clock; a radial bar chart sweeps `270`; a donut leaves `0.55` empty) and
survive projection switches. A radial bar chart runs the value *around* the
circle: give its value axis `role = ChartAxisRole::Domain` and its category
axis `ChartAxisRole::Value`.

## Highlights (slot 200 wash / slot 700 overlay)

`AddHighlight(ChartHighlight)` / `ClearHighlights()` — the proposal's §8.2
layer (`Engine/UltraCanvasChartHighlights.h`), clipped to the plot. Eight
shapes: explicit `Rectangle`/`Ellipse` in value space, an explicit `Polygon`
(its outline in `members`, in order), a `ValueBand` on one
axis, and the computed group shapes — `ConfidenceEllipse` (covariance eigen
decomposition at a `confidence` level, the 50%/95%-around-grouped-dots
convention, with a mean marker), `Hull` (padded convex hull), `Blob`
(Chaikin-smoothed hull hugging a cluster) and `PointHalo`. Computed shapes
take value-space `members`, x through the axis at `xAxisIndex` (the
projection's domain/u coordinate) and y through `yAxisIndex`. `zSlot` picks
the layer: 200 draws as a wash under the grid, 700 as an overlay above the
content. A highlight's `label` rides the label plan as
`ChartLabelClass::HighlightLabel`. The geometry helpers
(`ComputeConfidenceEllipse`, `ComputeConvexHull`, `ExpandPolygon`,
`SmoothPolygonChaikin`) are UI-free and unit-tested. Pair a confidence-
ellipse highlight with the legend's ellipse custom key (`SetCustomArea`) —
a key describes marks the chart actually draws.

A `Polygon` is rounded by `smoothing` the way a `Blob` is (`smoothing * 4`
Chaikin passes), but **in value space, before the mapping**. A caller that
smooths the same outline the same way therefore tests exactly the shape on
screen. That is how the importance–performance chart classifies factors into
its zones ([`UltraCanvasImportancePerformanceChart.md`](UltraCanvasImportancePerformanceChart.md)).
Set `smoothing = 0` for the outline as given, since the field's default
rounds it. Under the Polar projection each edge is sampled along the rings.

```cpp
ChartHighlight target;
target.shape = ChartHighlightShape::Polygon;
target.members = {{2.0, 10.0}, {6.0, 10.0}, {6.0, 30.0}, {2.0, 30.0}};   // value space
target.smoothing = 0.0;
target.xAxisIndex = 1;
target.yAxisIndex = 0;
target.label = "Target window";
```

## Phase-1 services

- **Grid follows ticks**: `SetGridAxis(index)` derives gridlines from that
  axis's ticks, so grid and labels cannot disagree. A value axis rules across
  the domain (rings under Polar - circles, or polygons through the domain
  ticks with `SetPolarGridShape(ChartPolarGridShape::Polygon)`, the radar
  web); a domain axis rules across the value range (spokes under Polar).
  `SetDomainGridAxis(index)` adds the second direction, so a scatter plot
  rules both x and y and a radar draws rings and spokes.
- **Limiters**: `AddLimiter({kind, axisIndex, value, color, width, dashed,
  caption})` draws reference lines (average, target, threshold...); captions
  are solved into the label plan so they never overprint value labels. A
  limiter on a domain axis runs across the value range (a pyramid's average
  age), and `extentStart` / `extentEnd` trim it to part of the plot (one side
  of a diverging chart).
- **Span annotations**: `AddSpanAnnotation({xAxisIndex, yAxisIndex, from, to,
  label, ...})` draws a dimension line between two value-space points, with
  arrowheads and a boxed caption the label plan steers around - the lead time,
  cycle time and WIP spans of a cumulative flow diagram.
- **Layout is measured, not guessed**: axis tick labels, titles and the legend
  reserve their measured space in the measure pass; `MeasureContent` adds
  chart-specific needs. `SolvePlotArea` never returns an empty plot. The chart
  title owns an exclusive band above every other top reservation, so axis
  endpoint labels can never overprint it. Edge axes on the same side stack
  outward when their bands overlap (a Mekko chart's column names under its
  cumulative-percent ticks); a left/right axis title is drawn rotated along
  the axis, outside its labels; the labels at the ends of a bottom/top axis
  reserve their overhang instead of running off the element.

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

### Bar connections

A bar connection is a line from the value end of one bar to the same series'
bar in the next category. In a clustered chart it traces each series across
the groups; in a stacked chart, anchored at the bar edges, it is the classic
series line joining the segment tops. `BuildBarConnections` turns the spans
into one screen polyline per unbroken run of each series, and the engine
element's `RenderBarConnections` strokes them in the series colours — call it
from `RenderChartContent` after the bars, so the lines ride over the bars and
under the solved labels:

```cpp
// RenderChartContent: animate the spans once, draw the bars from them, then
// join them - the lines grow with the bars.
std::vector<ChartBarSpan> spans = BuildBarSpans(valueAxis, categoryAxis,
                                                categoryCount, seriesValues, options);
for (ChartBarSpan& span : spans) {
    span.v0 = zero + (span.v0 - zero) * frame.animationProgress;
    span.v1 = zero + (span.v1 - zero) * frame.animationProgress;
}
// ... draw the bars from `spans` ...

ChartBarConnectionOptions connection;
connection.shape = ChartBarConnectionShape::Curved;       // or Straight
connection.anchor = ChartBarConnectionAnchor::BarCenter;  // or BarEdges
ChartBarConnectionStyle style;                            // width, dash, markers, halo
style.markerRadius = 3.0f;
RenderBarConnections(ctx, BuildBarConnections(*frame.projection, spans, connection),
                     seriesColors, style, hoveredSeries /* SIZE_MAX = none */);
```

| Option | Meaning |
|---|---|
| `shape` | `Straight` joins bar to bar with straight segments; `Curved` is a monotone cubic (Fritsch–Carlson) — smooth, flat where neighbouring bars are equal, and never overshooting either bar, so the curve cannot suggest a value no bar has |
| `anchor` | `BarCenter` meets the middle of each bar's value edge; `BarEdges` leaves each bar from the corner facing the next one and runs along the value edge of the bars in between |
| `bridgeGaps` | A missing value (short series, NaN) breaks the line by default; `true` joins the bars either side |
| `samplesPerSegment` | Screen samples per join (default 16). Straight lines are sampled too, so under Polar they follow the rings instead of cutting across them |

Each `ChartBarConnection` carries its `seriesIndex`, the `categories` it joins,
the stroke `points`, and `barPoints` — each joined bar's value-edge centre,
where `RenderBarConnections` puts the optional markers. `ChartBarConnectionStyle`
sets `width`, `emphasisWidth` (the series passed as emphasised, typically the
hovered bar's), `dashed`, `markerRadius` and a `haloWidth` underlay in the
plot-area colour that keeps a line readable where it crosses a bar of its own
colour. A run of fewer than two bars draws nothing. The demo's Bar Charts page
shows it on the Clustered and Stacked tabs.

## Line and area series

The line family shares the bar family's discipline: normalised `(u, v)`
points in, screen geometry through the projection out, so a line chart under
Polar is a polar line chart.

```cpp
// RenderChartContent: one series, its knots in normalised space.
std::vector<ChartNormalizedPoint> points;
for (size_t i = 0; i < values.size(); ++i) {
    points.emplace_back(category.Normalize(static_cast<double>(i)),
                        value.Normalize(values[i]));   // NaN = a gap
}
ChartLineOptions options;
options.shape = ChartLineShape::Curved;   // Straight, StepAfter, StepBefore, StepMiddle
for (const ChartLineRun& run : BuildLineRuns(*frame.projection, points, options)) {
    if (run.points.size() >= 2) ctx->DrawLinePath(run.points, false);
    for (const Point2Dd& knot : run.knots) {
        DrawChartMarker(ctx, ChartMarkerShape::Circle, knot, 3.5, color);
    }
}
```

| Helper | Meaning |
|---|---|
| `BuildLineRuns(projection, points, options)` | One `ChartLineRun` per unbroken run (a non-finite point breaks the line unless `bridgeGaps`): `points` to stroke, `knots` for markers, `path` in normalised space, `indices` joined. `Curved` is the bar connections' monotone cubic - it never overshoots a knot |
| `BuildAreaOutlines(projection, top, base, options)` | Closed outlines of the band between a top and a base line, both following the shape - an area down to its baseline, or one stacked segment |
| `StackSeriesValues(series, arrangement)` | Per series, per index, the `base` and `top` of its band: `Grouped` (all on zero), `Stacked` (positives up, negatives down, a missing value a zero-thickness band) or `PercentStacked`. Feed the value axis with `ObserveBarSeries` - the range arithmetic is the bars' |
| `ChartMarkerShape` + `DrawChartMarker(ctx, shape, centre, radius, fill, stroke, width)` | `NoMarker`, `Circle`, `Square`, `Diamond`, `TriangleUp`, `TriangleDown`, `Cross`, `Plus` - the one marker painter of the scatter / line family; `ChartMarkerPolygon` gives the outline for hit regions |

The analysis layer's arithmetic (slot 700) is
`Engine/UltraCanvasChartAnalysis.h`: `FitLinearTrend(xs, ys)` (slope,
intercept, r², Pearson r), `MovingAverage` / `ExponentialMovingAverage` (NaN
until a full window, and across a gap) and `HeikinAshiCandles`. All UI-free
and unit-tested; the content maps the results through its axes.

## Phase-3 services

- **Label plan**: labels submitted in `CollectChartLabels` go through the
  collision solver **once per invalidation, never per frame**. `allowSuppress`
  drops labels that would overprint (set `priority` on the ones that must
  survive); `pinned` keeps a label exactly on its side of the anchor and drops
  it rather than sliding it along (column headers, a bar's total - anything
  whose meaning is its position); leaders and rotation are drawn by the
  engine. Solved labels are
  kept within the plot area **plus whatever margins the chart reserved for
  itself in `MeasureContent`** (`SolveLabelBounds`): reserve a text-height
  band on the value-axis end and a bar that reaches the axis maximum keeps
  its value label just above the plot edge, instead of having it pushed down
  onto the bar. Axis bands, the title band and the legend margin stay out of
  bounds (the legend also rides the plan as an obstacle).
- **Legend**: the shared `ChartLegend` component
  ([`UltraCanvasChartLegend.h`](../../UltraCanvas/include/Plugins/Charts/UltraCanvasChartLegend.h)),
  laid out by the engine. `SetShowLegend(true)` +
  `SetLegendEntries({{label, color}, ...})` as before, plus:
  - `SetLegendPosition(...)` — 12 outside placements
    (`Top/Bottom/Left/Right` × `Start/Center/End`) that reserve their edge in
    the layout negotiation, and 4 insets (`InsetTopLeft` … `InsetBottomRight`)
    that float over the plot and reserve nothing. Either way the legend box
    is an obstacle the solved labels steer around.
  - `SetLegendOrientation(...)` — `Auto` (vertical on the sides, horizontal
    rows wrapped to the width on top/bottom), or forced
    `Horizontal`/`Vertical`.
  - `SetLegendTitle(text)`, and `Legend()` for the component's long tail:
    per-entry `valueText`, interval/band entries, a max-entries overflow row,
    a label formatter, hit-testing.
  - `SetLegendMode(...)` — `Discrete` (the entry list), `ColorBar` (a
    continuous colormap ramp over a value range with tick labels — the key a
    heatmap or contour surface uses; configure with
    `Legend().SetColorBar({colormap, min, max, ticks, formatter, ...})`,
    `quantizeLevels >= 2` draws discrete bands), or `SizeLegend` (sample
    circles keying a bubble-size scale, largest first —
    `Legend().SetSizeScale(...)`; `areaScaled` sizes the circles like
    area-proportional bubbles, `values` names the sample values).
  - `Legend().SetCustomArea(size, draw)` — a host-drawn panel below the
    entries, inside the legend box, for a key richer than any swatch: an
    annotated confidence-ellipse diagram, a bubble-size scale, a mini axis.
    The legend reserves the size in its layout and calls the drawing
    callback with the reserved (clipped) rectangle. **A key must describe
    marks the chart actually draws**: a limiter-line key belongs with a
    chart that draws limiters, and a 50%/95% confidence-ellipse key belongs
    with a scatter/jitter chart that draws group-highlight ellipses — that
    highlighting style is the engine's reserved `Highlight` layer (slot
    200, group rectangles / ellipses / hull blobs / value bands), not yet
    implemented; when it lands, its key pairs with this custom area.
  - `ChartLegendEntry.swatch` picks the swatch: `Square | Circle | Ring |
    Line | DashedLine | Marker | Glyph | Gradient | Outline | Hatched |
    Image` (`imagePath` for `Image`) — line series get a line, scatter gets
    a disc, and a non-solid painted series is represented faithfully.
  The legend styles itself from the active theme (`SetTheme` keeps its
  `ChartLegendStyle` in sync), wraps tall vertical legends into further
  columns instead of clipping, and is interactive: hovering an entry
  highlights it (repaint-only), clicking one toggles its enabled state (the
  entry renders dimmed) and calls the chart's
  `OnLegendEntryToggled(index, enabled)` — the chart decides what
  "disabled" means, typically hiding the series and marking
  `ChartDirty::Data` (the engine demo does exactly that).
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
label re-solve (proposal §5.7). The engine calls the `OnChartThemeChanged()` hook
so content can refresh anything it cached from the theme — legend entry
colours above all (they are stored in `ChartLegendEntry`, not looked up at
draw time). The frozen frame carries `frame.theme` for phase-2 drawing. (The hook was called `OnThemeChanged()` until 1.5.0; it was
renamed because that name is the element's own hook for the application's
`UltraCanvasTheme`, and the parameterless overload hid it.)

The built-in themes are the palettes the pre-engine charts each carried
privately, lifted verbatim: **Light** (default furniture, Paul Tol bright),
**Dark** (dark furniture with the git graph's dark lane colours),
**Corporate**, **Vibrant**, **Pastel** (soft palette *and* soft warm-grey
furniture of its own — the demo's default), **Colorblind** (Okabe–Ito),
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

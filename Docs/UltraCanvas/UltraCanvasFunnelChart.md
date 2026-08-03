# UltraCanvasFunnelChart

A funnel chart: a staged process whose population shrinks from one step to the
next. Each stage is a subset of the one before it, so the taper *is* the data —
where the shape narrows sharply, the process is leaking.

- Element: `include/Plugins/Charts/UltraCanvasFunnelChart.h` / `Plugins/Charts/UltraCanvasFunnelChart.cpp`
- Base class: `UltraCanvasChartElementBase` (`include/Plugins/Charts/UltraCanvasChartElementBase.h`)
- Demo: `Apps/DemoApp/UltraCanvasFunnelChartExamples.cpp` (Charts → Funnel Chart)

A funnel is not a pyramid chart. A pyramid shows a static hierarchy whose slices
are independent parts of a whole; a funnel shows a *process*, where every stage
is drawn from the one above it. If your stages do not nest, reach for
[`UltraCanvasPyramidChart`](UltraCanvasPyramidChart.md) — including for an
inverted pyramid, which looks like a funnel but still means "parts of a whole" —
or for `UltraCanvasPopulationChart` or a plain bar chart.

## Quick start

```cpp
#include "Plugins/Charts/UltraCanvasFunnelChart.h"

auto chart = UltraCanvas::CreateFunnelChart("campaign", 20, 20, 820, 560);
chart->SetChartTitle("E-commerce campaign funnel");
chart->SetAbbreviateValues(true);                 // 5680 -> "5.68K"
chart->SetShowConversionOnConnector(true);        // 68.22% drawn on the neck

chart->AddStage("Sent", 5680);
chart->AddStage("Viewed", 3875);
chart->AddStage("Clicked", 1669);
chart->AddStage("Add to Cart", 610);
chart->AddStage("Purchased", 565);

container->AddChild(chart);
```

## Data

`FunnelStage` derives from `ChartDataPoint`, so the chart also satisfies the
generic `IChartDataSource` contract used by the rest of the chart family.

```cpp
struct FunnelStage : ChartDataPoint {
    std::string stageLabel;      // "Viewed", "Add to Cart", ...
    double      stageValue;      // Population reaching this stage
    std::string description;     // Long-form annotation drawn beside the stage
    std::string badgeText;       // Short marker outside the stage, e.g. "01"
    Color       stageColor;      // Transparent = take the colour from the colour mode
    double      targetValue;     // <= 0 disables the target overlay for this stage
    std::string tooltipText;     // Optional replacement tooltip
    std::vector<FunnelSubSegment> segments;   // Empty = solid stage
};
```

Stages can be added one at a time, or through a shared `FunnelDataSource`:

```cpp
auto data = UltraCanvas::CreateFunnelDataSource();
data->AddStage("Impressions", 1163);
data->AddStage("Clicks", 742);

auto chart = UltraCanvas::CreateFunnelChartWithData("marketing", 20, 20, 800, 500,
                                                    data, "Marketing funnel");
```

`FunnelDataSource::LoadFromCSV()` reads `stageLabel,value[,description]`. Rows
whose value column does not parse are skipped, which swallows a header line
without any special casing.

### Derived metrics

Nothing derived is stored, so editing a value can never leave a stale figure
behind. `GetStageMetrics(dataIndex)` returns everything the chart knows about a
stage's relationship to its neighbours:

```cpp
struct FunnelStageMetrics {
    double value;
    double percentOfFirst;      // Share of the stage at the top of the funnel
    double percentOfPrevious;   // Conversion from the stage above
    double percentOfTotal;      // Share of every stage added together
    double dropOff;             // Absolute loss since the stage above
    double dropOffPercent;      // 100 - percentOfPrevious
};
```

The metrics always describe the stage's *drawn* neighbours, so a sorted funnel
still reports the conversion the reader can actually see. `GetOverallConversion()`
gives the first-to-last figure and `GetBottleneckStage()` the index of the worst
step.

## Shape modes

`SetShapeMode()` picks the silhouette. All five work in either orientation and
with every scale mode.

| Mode | What it draws |
| --- | --- |
| `SegmentedBars` | Detached bars joined by tapering necks that carry the conversion. The default. |
| `ContinuousFunnel` | One solid funnel, slabs touching edge to edge. |
| `CardRows` | A rounded row card per stage with the wedge running through it. |
| `SkewedInfographic` | Sheared presentation slabs, alternating direction, with badges and callouts. |
| `BarStyleFunnel` | Plain diminishing bars, no taper — the variant that survives colour-blind and low-vision readers best. |

`SetOrientation()` takes `VerticalFunnel` (top to bottom, the default) or
`HorizontalFunnel` (left to right). It swaps the flow axis and the cross axis;
every other setting — label columns, connector necks, hit testing — behaves
identically, so a funnel can be rotated without touching anything else.

## Scale modes

`SetScaleMode()` decides what the geometry actually encodes. This is the setting
that determines whether the chart tells the truth.

- **`WidthProportional`** (default) — the breadth of each stage tracks its value,
  every band the same thickness. What most people mean by "funnel chart".
- **`HeightProportional`** — the outline tapers evenly from full width to the tip
  and each band's *thickness* carries the value. The solid-cone look.
- **`AreaProportional`** — the band thickness is chosen so the drawn **area** of
  each slab tracks its value. The honest funnel: a classic funnel's tapering
  sides make the area grow faster than the width, which systematically overstates
  the upper stages.
- **`EqualStages`** — every stage the same size, a pure pipeline diagram.

`SetTipMode()` controls what happens past the last stage: `PointTip` closes it
completely, `FlatNeck` closes it to `SetNeckRatio()` of the full width, and
`MinWidthTip` (default) leaves the last stage at its own breadth.
`SetMinStageExtent()` keeps a near-zero stage visible and clickable, and
`SetAlignment()` chooses between a centred funnel and one anchored to either edge.

## Labels

There are three independent label channels, each with its own placement enum.

```cpp
chart->SetStageLabelPlacement(FunnelStageLabelPlacement::OutsideStartLabels);
chart->SetFunnelValueLabelPlacement(FunnelValueLabelPlacement::InsideValueLabels);
chart->SetPercentPlacement(FunnelPercentPlacement::OutsideEndPercent);
```

- **Stage names** — `OutsideStartLabels`, `OutsideEndLabels`, `InsideStageLabels`,
  `LeaderLineLabels` (outside, joined to the slab by a leader line) or
  `HideStageLabels`.
- **Values** — `InsideValueLabels`, `OutsideStartValues`, `OutsideEndValues` or
  `HideValueLabels`. `SetAbbreviateValues(true)` renders 5680 as `5.68K`;
  otherwise `SetValueFormat()` takes a printf format.
- **Percentages** — `OutsideEndPercent`, `OutsideStartPercent`,
  `InsideStagePercent` or `HidePercentColumn`, with
  `SetShowPercentOfFirst()`, `SetShowPercentOfPrevious()`,
  `SetShowPercentOfTotal()` and `SetShowDropOffLabels()` choosing which figures
  appear there.

A name and a value placed on the same side share one column and merge into a
single string — `"Clicks (742)"` — rather than colliding.

Inside labels are measured against the slab before they are drawn: a line that
will not fit is simply omitted, so a narrow tail stage silently drops to fewer
lines instead of spilling over the taper. `SetAutoContrastInsideLabels(true)`
(the default) picks white or near-black text per stage from the fill's
brightness.

Separately from the percentage column, `SetShowConversionOnConnector(true)` draws
the stage-to-stage conversion **in the neck between two stages** — the reading
most funnel charts are built for. The end column then stays free for the share of
the first stage, which is a different figure and always falls monotonically.

`SetDescriptionColumnWidth()` opens an annotation column fed by each stage's
`description`, joined to the slab by a leader line unless
`SetShowDescriptionLeaderLines(false)`. `SetShowBadges(true)` numbers the stages
down the outside; a stage with no `badgeText` falls back to its position.
`SetShowTerminalCallout(true)` circles the final value past the tip.

## Colour

```cpp
chart->SetColorMode(FunnelColorMode::SequentialRamp);
chart->SetColorRamp(Color(158, 202, 232), Color(21, 74, 120));
```

| Mode | Behaviour |
| --- | --- |
| `SequentialRamp` | Interpolate between two colours down the funnel. The default. |
| `CategoricalPalette` | Cycle `SetPalette()`, one colour per stage. |
| `SingleHue` | `SetBaseColor()` throughout. |
| `PerStageOverride` | Only what each stage sets; base colour otherwise. |
| `ThresholdColor` | Colour by conversion against `SetConversionThresholds()`. |

A stage that sets its own `stageColor` always wins, whatever the mode.
`SetUseGradientFill()` adds a cross-funnel gradient to every slab,
`SetShowStageBorder()` separates touching slabs, and `SetDimUnhoveredStages()`
fades everything except the stage under the pointer.

## Stacked funnels

Give a stage sub-segments and it is drawn as a stack while the funnel still
narrows, so the chart shows both the attrition and its composition:

```cpp
FunnelStage stage("Applied", 2000);
stage.segments.push_back(FunnelSubSegment("Referral",  420, kReferral));
stage.segments.push_back(FunnelSubSegment("Job board", 980, kJobBoard));
stage.segments.push_back(FunnelSubSegment("Outbound",  600, kOutbound));
chart->AddStage(stage);
```

Each slice keeps the slab's taper, and the segment breakdown is appended to the
tooltip. Segments need not add up to `stageValue`; they are normalised.

## Analysis overlays

- `SetShowTargetOverlay(true)` outlines each stage's `targetValue` as a ghost, so
  plan and actual can be read against one another.
- `SetHighlightBottleneck(true)` outlines the stage with the worst conversion
  from its predecessor.
- `SetValuePolicy()` decides what to do when a stage is larger than the one above
  it: `AllowIncrease` (default, draw it as given), `ClampToPrevious` (never draw
  wider than the stage above, while the tooltips keep reporting the real figures)
  or `AutoSortDescending`.
- `SetSortOrder()` reorders the drawn stages without touching the data source, so
  the indices handed to the callbacks always refer to the original stages.

## Interaction

```cpp
chart->onStageClick = [](size_t stageIndex) { /* drill down */ };
chart->onStageHover = [](size_t stageIndex) { /* sync a side panel */ };
chart->onConnectorClick = [](size_t upperStageIndex) { /* inspect a conversion */ };
```

Hit testing is a point-in-polygon test against the cached slab outlines, with a
band-based fallback so a near-empty stage a few pixels wide can still be hovered.
Indices are into the data source, unaffected by sorting.

Tooltips are generated per stage and carry the value, both percentages, the
absolute loss at that step, any segment breakdown and the description. Supply
`FunnelStage::tooltipText` to replace the generated text, or
`SetCustomTooltipGenerator()` from the base class. Generated text is XML-escaped
for the Pango markup the tooltip manager uses; text supplied through
`tooltipText` is passed through verbatim so it can carry markup of its own.

## Animation

Animation is opt-in because it drives repaints from inside `Render()`:

```cpp
chart->SetAnimationDuration(0.7f);
chart->SetAnimationEnabled(true);
chart->RestartAnimation();
```

Stages grow outwards from the funnel's anchor edge in a staggered sequence and
the chart stops requesting frames as soon as the last one lands.

## Choosing the settings

- Aim for four to six stages. Fewer oversimplifies; more than eight or ten turns
  the tail into an unreadable sliver.
- Order the stages by the real sequence of the process, not by size — that is
  what separates a funnel from a sorted bar chart.
- Show both an absolute count and a percentage where there is room. Counts carry
  volume, percentages carry efficiency.
- Prefer `AreaProportional`, or `BarStyleFunnel` outright, when the exact
  magnitudes matter more than the funnel metaphor.
- Keep the palette to one hue ramp unless the stages are genuinely different
  kinds of thing, and check the contrast of inside labels — the default
  auto-contrast handles this, but a hand-picked `insideLabelColor` does not.

## Demo tabs

`Apps/DemoApp/UltraCanvasFunnelChartExamples.cpp` builds six tabs:

1. **Segmented Funnel** — the classic analytics funnel with conversion necks.
2. **Continuous Cone** — `HeightProportional` scaling with leader-line callouts.
3. **Card Rows** — the dashboard layout, with the legend switched on.
4. **Presentation Infographic** — dark ground, sheared slabs, badges, annotations.
5. **Horizontal Funnel** — left-to-right flow with a terminal callout.
6. **Playground** — every shape, scale, colour and tip mode wired to live
   controls, over a stacked recruitment pipeline.

# UltraCanvasPyramidChart

A pyramid diagram: a static hierarchy whose levels are independent parts of a
whole. The tiers stack from a broad base to a narrow apex, and what the shape
carries is *rank* — each level sits on the one beneath it, and the taper says the
higher tiers are smaller, rarer or more selective.

- Element: `include/Plugins/Charts/UltraCanvasPyramidChart.h` / `Plugins/Charts/UltraCanvasPyramidChart.cpp`
- Base class: `UltraCanvasChartElementBase` (`include/Plugins/Charts/UltraCanvasChartElementBase.h`)
- Demo: `Apps/DemoApp/UltraCanvasPyramidChartExamples.cpp` (Charts → Pyramid Diagram)

### Which chart do you actually want?

Three elements in this repository get called "pyramid" and they are not
interchangeable:

| You have | Use |
| --- | --- |
| Ranked tiers of one whole — Maslow, a testing pyramid, an org structure, a market by segment | **`UltraCanvasPyramidChart`** (this one) |
| A process where each step is drawn from the step before it — impressions → clicks → purchases | [`UltraCanvasFunnelChart`](UltraCanvasFunnelChart.md) |
| Age and sex bands drawn back to back from a centre line | [`UltraCanvasPopulationChart`](UltraCanvasPopulationChartExamples.md) or `DivergingChartStyle::PopulationPyramid` in [`UltraCanvasDivergingBarChart`](UltraCanvasDivergingChartExamples.md) |

An inverted pyramid is *not* a funnel. `ApexDown` still says "these tiers are
parts of one whole"; a funnel says "this many survived the step above". The
metrics differ accordingly — a pyramid reports shares and ratios, never
conversions.

## Quick start

```cpp
#include "Plugins/Charts/UltraCanvasPyramidChart.h"

auto chart = UltraCanvas::CreatePyramidChart("needs", 20, 20, 900, 600);
chart->SetChartTitle("Hierarchy of needs");

// Levels are supplied apex first by default and carry no numbers at all
chart->AddLevel("Self-actualisation");
chart->AddLevel("Esteem");
chart->AddLevel("Love and belonging");
chart->AddLevel("Safety");
chart->AddLevel("Physiological");

container->AddChild(chart);
```

That is the default configuration: equal-height tiers, separated by a small gap,
labelled inside the band, coloured from the spectrum palette. It is the shape
most pyramid diagrams actually are.

## Data

`PyramidLevel` derives from `ChartDataPoint`, so the chart also satisfies the
generic `IChartDataSource` contract used by the rest of the chart family.

```cpp
struct PyramidLevel : ChartDataPoint {
    std::string levelLabel;     // "Esteem", "Unit tests", ...
    double      levelValue;     // Optional - a hierarchy needs no numbers
    std::string calloutTitle;   // Heading of the side callout; empty falls back to levelLabel
    std::string description;    // Body of the side callout
    std::string badgeText;      // Short marker, e.g. "01"; empty falls back to the position
    std::string iconPath;       // Drawn in the icon callout style
    Color       levelColor;     // Transparent = take the colour from the colour mode
    double      targetValue;    // <= 0 disables the target overlay for this level
    std::string tooltipText;    // Optional replacement tooltip
    std::vector<PyramidSubSegment> segments;   // Empty = solid level
};
```

Levels can be added one at a time, or through a shared `PyramidDataSource`:

```cpp
auto data = UltraCanvas::CreatePyramidDataSource();
data->AddLevel("Strategic", 130);
data->AddLevel("Enterprise", 700);

auto chart = UltraCanvas::CreatePyramidChartWithData("accounts", 20, 20, 900, 600,
                                                     data, "Customer base");
```

`PyramidDataSource::LoadFromCSV()` reads `levelLabel[,value][,description]`. A row
that is nothing but a label is valid, because a pyramid is so often a hierarchy
with no numbers; a second column that does not parse as a number is read as the
description instead, which swallows a header line without any special casing.

### Which end is the apex?

```cpp
chart->SetInputOrder(PyramidInputOrder::BaseFirst);   // levels[0] is the widest tier
```

The chart always *draws* apex first. `SetInputOrder()` only says how to read what
you supplied, so data that naturally arrives base first does not have to be
reversed by hand. Callback and metric indices are always into the data source, so
neither this setting nor sorting shifts them.

### Derived metrics

Nothing derived is stored, so editing a value can never leave a stale figure
behind. A pyramid is a hierarchy, so none of these are conversions:

```cpp
struct PyramidLevelMetrics {
    double value;
    double percentOfTotal;      // Share of every level added together
    double percentOfBase;       // Share of the widest tier at the bottom
    double ratioToLevelBelow;   // Size against the tier directly beneath
    double cumulativeFromBase;  // Share held by this level and everything below it
    double cumulativeFromApex;  // ... and by this level and everything above it
    size_t levelFromBase;       // 0 = the base
    size_t levelFromApex;       // 0 = the apex
};
```

`GetLevelMetrics(dataIndex)` returns them for one level, `GetBaseToApexRatio()`
gives the spread across the whole shape, and `GetTopHeavyLevel()` returns the
first tier that outweighs the one holding it up — the ice-cream-cone
anti-pattern — or `SIZE_MAX` when the pyramid is well formed.

## Shape modes

`SetShapeMode()` picks the silhouette. All eight work with every scale mode.

| Mode | What it draws |
| --- | --- |
| `SegmentedPyramid` | Trapezoid tiers separated by a gap. The default. |
| `SolidPyramid` | One triangle cut by level lines, tiers touching. |
| `SteppedPyramid` | Rectangular slabs stacked into a ziggurat. |
| `Pyramid3D` | Extruded solid with a lit top face and a shaded flank. |
| `CardRows` | A rounded row card per tier with the wedge running through it. |
| `SkewedInfographic` | Sheared slabs, alternating direction. |
| `ExplodedPyramid` | Tiers stepped sideways out of the stack. |
| `BarStylePyramid` | Plain centred bars — the variant that survives colour-blind and low-vision readers best. |

`SetDirection()` takes `ApexUp` (the default), `ApexDown` (the inverted pyramid),
`ApexLeft` or `ApexRight`. The flow axis always runs apex to base whichever way
the pyramid points, so every other setting behaves identically after a rotation.

`SetHalfPyramid(true)` keeps one face vertical, and `SetAlignment()` chooses which
edge it is anchored to. `SetPyramidWidthRatio()` narrows the base within the plot
area, which is what makes room for a wide callout column.

### The apex

```cpp
chart->SetApexMode(PyramidApexMode::PointApex);       // PointApex | FlatApex | MinWidthApex
chart->SetApexRatio(0.12);                            // Plateau width, FlatApex only
chart->SetApexCap(PyramidApexCap::ApexSeparateCap, 40.0);
```

`SetApexMode()` decides how the silhouette closes; `SetApexCap()` decides whether
the top of the shape is a tier at all. `ApexSeparateCap` floats a small triangle
above the first tier — the capstone of the classic deck pyramid — and
`ApexHidden` suppresses the closing taper entirely, so the top tier draws flat.

### 3D

```cpp
chart->SetShapeMode(PyramidShapeMode::Pyramid3D);
chart->SetDepth3D(26.0);
chart->SetAngle3D(32.0);              // Degrees above the horizontal
chart->SetFaceShading(0.26, 0.24);    // Lighten the top face, darken the flank
```

Each tier is extruded along one vector: the face the reader looks down on is
lightened from the tier's own colour, the flank is darkened, and the tiers are
drawn base first so the nearer tier always occludes the one behind it. Faces are
hit-tested along with the front, so the whole solid is clickable.

## Scale modes

`SetScaleMode()` decides what the geometry actually encodes. This is the setting
that determines whether the chart tells the truth.

- **`EqualLevels`** (default) — every tier the same height. The correct choice for
  a qualitative hierarchy, where the tiers are ranks rather than quantities.
- **`HeightProportional`** — tier height tracks the value.
- **`WidthProportional`** — tier width tracks the value, heights stay equal. The
  ziggurat.
- **`AreaProportional`** — the cut positions are solved so each trapezoid's drawn
  **area** tracks its value. The honest pyramid: a pyramid's sides taper, so a
  band's area grows faster than its height, and sizing tiers by height alone
  systematically overstates everything near the base.
- **`VolumeProportional`** — the cuts are solved against the volume of the
  extruded solid instead, which is what a reader integrates when the pyramid is
  drawn in 3D.

The area and volume solvers both account for a truncated apex, so `FlatApex` and
`AreaProportional` compose correctly rather than drifting.

## Colour

```cpp
chart->SetColorMode(PyramidColorMode::CategoricalPalette);
chart->SetPalettePreset(PyramidPalettePreset::SpectrumPalette);
```

| Mode | Behaviour |
| --- | --- |
| `CategoricalPalette` | Cycle `SetPalette()`, one colour per tier. The default. |
| `SequentialRamp` | Interpolate `SetColorRamp(apex, base)` down the pyramid. |
| `SingleHue` | `SetBaseColor()` throughout. |
| `PerLevelOverride` | Only what each level sets; base colour otherwise. |
| `ThresholdColor` | Colour by share of the total against `SetShareThresholds()`. |

Presets are `SpectrumPalette`, `WarmPalette`, `CoolPalette`, `CorporatePalette`
and `GrayscalePalette`; all of them run apex first, which is the direction a
reader scans a pyramid. A level that sets its own `levelColor` always wins.

`SetFillStyle()` is independent of the colour mode:

| Style | Behaviour |
| --- | --- |
| `SolidFill` | Flat fill. The default. |
| `GradientFill` | Lightened across the tier, tuned with `SetGradientLightening()`. |
| `OutlineOnly` | Stroked tiers with no fill, tuned with `SetOutlineStyle(width, dashed)`. |
| `OutlineWithTint` | The same outline over a faint wash, tuned with `SetTintStrength()`. |

`SetShowLevelBorder()` separates touching tiers and `SetDimUnhoveredLevels()`
fades everything except the tier under the pointer.

## Labels

There are four independent text channels.

```cpp
chart->SetLevelLabelPlacement(PyramidLevelLabelPlacement::InsideLevelLabels);
chart->SetPyramidValueLabelPlacement(PyramidValueLabelPlacement::InsideValueLabels);
chart->SetPercentPlacement(PyramidPercentPlacement::OutsideEndPercent);
```

- **Level names** — `InsideLevelLabels` (the default), `OutsideStartLabels`,
  `OutsideEndLabels`, `LeaderLineLabels` or `HideLevelLabels`.
- **Values** — `InsideValueLabels`, `OutsideStartValues`, `OutsideEndValues` or
  `HideValueLabels` (the default, because most pyramids carry no numbers).
  `SetAbbreviateValues(true)` renders 5680 as `5.68K`; otherwise
  `SetValueFormat()` takes a printf format.
- **Percentages** — placed with the enum above, with `SetShowPercentOfTotal()`,
  `SetShowPercentOfBase()`, `SetShowRatioToLevelBelow()` and
  `SetShowCumulativeFromBase()` choosing which figures appear there. Every
  percentage channel hides itself when the data carries no values.
- **Callouts** — the side blocks, described in their own section below.

A name and a value placed in the same column merge into a single string —
`"Enterprise (700)"` — rather than colliding. The label column can also carry a
colour swatch (`SetShowLevelSwatch()`), a hairline rule per row
(`SetShowLevelListRules()`) and a numbered badge, which together give the keyed
list that sits beside most presentation pyramids.

### Text inside a tier

A tier is a trapezoid, so inside text is measured against its **narrowest edge**
rather than its middle — otherwise the last line runs out over the slope. With
`SetWrapInsideText(true)` (the default) the label and, when
`SetShowDescriptionInside(true)` is set, the description are wrapped to that
width. If the block is still too tall, `SetShrinkInsideText(true, 7.5)` steps the
font down before any content is dropped; only then are the least important lines
removed, so a narrow apex tier degrades to just its name instead of going blank.

`SetInsideTextAlign()` picks between centred text and the left-aligned paragraph
look, and `SetAutoContrastInsideLabels(true)` (the default) picks white or
near-black per tier from the fill's brightness.

## Callouts

The callout column is what most pyramid infographics really are: the pyramid
itself is a quarter of the canvas and the text beside it is the rest.

```cpp
chart->SetCalloutStyle(PyramidCalloutStyle::ColorRibbonCallouts);
chart->SetCalloutSide(PyramidCalloutSide::CalloutsRight);
chart->SetCalloutLayout(PyramidCalloutLayout::AlignedToLevel);
chart->SetConnectorStyle(PyramidConnectorStyle::NotchPointer);
chart->SetCalloutColumnWidth(300.0);
```

| Style | What it draws |
| --- | --- |
| `PlainTextCallouts` | A bold title and a wrapped body, no decoration. |
| `ColorRibbonCallouts` | A filled banner in the tier's own colour, with contrasting text. |
| `RoundedCardCallouts` | An outlined card; the border defaults to the tier's colour. |
| `IconCardCallouts` | The same card with a circular icon from `iconPath` on the edge facing the pyramid. |
| `NumberedBlockCallouts` | A large ghost numeral behind the title and body. |

`SetCalloutSide()` takes `CalloutsRight`, `CalloutsLeft`, `CalloutsAlternating`
(odd tiers one way, even tiers the other) or `CalloutsBothSides`, which splits the
tiers between two columns. `SetCalloutLayout()` chooses between `AlignedToLevel`,
where each block is centred on its tier, and `EvenlyStacked`, where the blocks
share the height equally whatever the tiers do.

Blocks are measured, then packed: an aligned column sweeps down pushing blocks
apart where they would overlap, and sweeps back up if that pushed the last one off
the bottom. A tier with neither a `calloutTitle` nor a `description` reserves no
block at all.

Connectors are `StraightConnector`, `ElbowConnector`, `DottedConnector`,
`NotchPointer` (a wedge on the block's inner edge instead of a rule across the
gap) or `NoConnector`.

Callout columns exist only on a vertical pyramid — laid out sideways they would
fight the tiers for the same axis. A horizontal pyramid falls back to the level
label column, which reads the same way.

## Badges

```cpp
chart->SetBadgeShape(PyramidBadgeShape::CircleBadge);
chart->SetBadgePlacement(PyramidBadgePlacement::BadgeOnEdge);
chart->SetBadgeStyle(14.0, 11.0);
chart->SetBadgeColors(Color(255, 255, 255, 235), Color(60, 62, 70, 255));
```

Shapes are `CircleBadge`, `RoundedBadge`, `SquareBadge`, `PlainNumber` (bare text)
or `NoBadge`. Placements are `BadgeOnEdge` (straddling the tier's leading edge),
`BadgeInsideLevel`, `BadgeOutsideStart` (its own narrow column) and
`BadgeOnLabelRow`, which numbers the label column instead of the pyramid. A level
with no `badgeText` falls back to its position counted from the apex, and a
transparent badge fill takes the tier's own colour.

## Stacked pyramids

Give a level sub-segments and it is drawn as a stack while the pyramid still
tapers, so the chart shows both the ranking and its composition:

```cpp
PyramidLevel level("Mid-market", 2640);
level.segments.push_back(PyramidSubSegment("EMEA", 880, kEmea));
level.segments.push_back(PyramidSubSegment("Americas", 1120, kAmer));
level.segments.push_back(PyramidSubSegment("APAC", 640, kApac));
chart->AddLevel(level);
```

Each slice keeps the tier's taper, the segment breakdown is appended to the
tooltip, and the legend switches from listing tiers to listing segments. Segments
need not add up to `levelValue`; they are normalised.

## Analysis overlays

- `SetShowTargetOverlay(true)` outlines each level's `targetValue` as a ghost, so
  plan and actual can be read against one another.
- `SetReferenceShape({0.10, 0.20, 0.70})` draws the ideal silhouette over the data
  with a rule at every proportion it asks for — the testing pyramid's 70/20/10, a
  food pyramid's bands, a target org shape. Shares run apex first and are
  normalised, and the ideal cuts go through the same solver as the drawn ones, so
  the gap between a rule and the tier boundary beside it is the distance from
  target. The overlay needs a scale mode whose cuts encode the values
  (`HeightProportional`, `AreaProportional` or `VolumeProportional`) and is
  skipped under `EqualLevels` and `WidthProportional`, where there would be
  nothing to compare it against.
- `SetHighlightTopHeavy(true)` outlines the first tier that outweighs the tier
  beneath it.
- `SetBenchmarkValue(value, label)` draws a rule where the running total from the
  base reaches that value — "80% of the spend is below here".
- `SetValuePolicy()` decides what to do when a tier is larger than the one below
  it: `AllowIncrease` (default, draw it as given), `ClampToBelow` (never draw wider
  than the tier beneath, while the tooltips keep reporting the real figures) or
  `AutoSortToWiden`.
- `SetSortOrder()` reorders the drawn tiers without touching the data source. The
  orders are read along the drawn sequence, apex to base, so `ValueAscending`
  gives the ordinary pyramid and `ValueDescending` a deliberately top-heavy one.

## Interaction

```cpp
chart->onLevelClick = [](size_t levelIndex) { /* drill down */ };
chart->onLevelHover = [](size_t levelIndex) { /* sync a side panel */ };
chart->onCalloutClick = [](size_t levelIndex) { /* open the detail */ };
```

Hit testing is a point-in-polygon test against the cached tier outlines — plus the
extruded faces in `Pyramid3D` — with a band-based fallback so the sliver at the
apex can still be hovered. Callout blocks are hit-tested too, and hovering one
highlights its tier. `SetSelectedLevel(dataIndex)` outlines a tier from code and
`ClearSelection()` releases it.

Tooltips are generated per level and carry the value, the share of the total and
of the base, the ratio to the tier below, the cumulative share, any segment
breakdown and the description; a valueless hierarchy reports its position instead.
Supply `PyramidLevel::tooltipText` to replace the generated text, or
`SetCustomTooltipGenerator()` from the base class. Generated text is XML-escaped
for the Pango markup the tooltip manager uses; text supplied through `tooltipText`
is passed through verbatim so it can carry markup of its own.

## Animation

Animation is opt-in because it drives repaints from inside `Render()`:

```cpp
chart->SetAnimationDuration(0.7f);
chart->SetAnimationEnabled(true);
chart->RestartAnimation();
```

Tiers land base first — a pyramid is built from the ground up — and the chart
stops requesting frames as soon as the apex is in.

## Choosing the settings

- Aim for three to six tiers. A pyramid is a ranking device; past six or seven the
  apex is too thin to hold a label and the ranking stops being legible.
- Leave `EqualLevels` alone unless the tiers really are quantities. Sizing a
  qualitative hierarchy by value invents precision the data does not have.
- When the tiers *are* quantities, prefer `AreaProportional` — or
  `BarStylePyramid` outright when the exact magnitudes matter more than the
  metaphor.
- Put long copy in the callout column rather than inside the tiers. Text inside a
  trapezoid is bounded by its narrowest edge, so the apex will always take less
  than you expect.
- Reach for `Pyramid3D` for a deck and against a dashboard: the extrusion is
  decoration, and it costs the reader accuracy that `VolumeProportional` can only
  partly buy back.

## Demo tabs

`Apps/DemoApp/UltraCanvasPyramidChartExamples.cpp` builds seven tabs:

1. **3D Ribbon Pyramid** — extruded tiers, detached capstone, keyed list one side
   and colour ribbons the other.
2. **Spectrum 3D** — Maslow's hierarchy with wrapped copy inside every tier and
   numbered badges straddling the edge.
3. **Outline Hierarchy** — stroked tiers with no fill, described by icon cards on
   straight connectors.
4. **Flat & Minimal** — two presentation layouts side by side: a plain label
   column, and a gradient pyramid beside a numbered list.
5. **Alternating Infographic** — callouts thrown either side of the pyramid behind
   ghost numerals.
6. **Data Pyramid** — a real test suite, area-proportional, against the 70/20/10
   reference shape.
7. **Playground** — every shape, scale, direction, fill, apex and callout mode
   wired to live controls, over a stacked customer pyramid.

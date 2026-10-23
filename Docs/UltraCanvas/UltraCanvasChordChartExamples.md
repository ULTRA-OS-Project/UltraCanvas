# UltraCanvasChordChart

A chord diagram: categories sit on a ring as arcs, and ribbons join them with a
width proportional to the flow between them. It shows **who exchanges how much
with whom** — trade between regions, migration corridors, handovers between
teams or project phases — in one picture.

Flows are directed. Each direction of a relationship gets its own sub-arc on
each endpoint, so an asymmetric relationship reads as a ribbon that is wide at
one end and narrow at the other.

- Element: `include/Plugins/Charts/UltraCanvasChordChart.h` / `Plugins/Charts/UltraCanvasChordChart.cpp`
- Base class: `UltraCanvasChartElementBase` (`include/Plugins/Charts/UltraCanvasChartElementBase.h`)
- Demo: `Apps/DemoApp/UltraCanvasChordChartExamples.cpp` (Charts → Chord Chart)

## Contents

- [Quick start](#quick-start)
- [Data model](#data-model)
- [Loading data](#loading-data)
- [Geometry](#geometry)
- [Ribbons](#ribbons)
  - [Ribbon colour modes](#ribbon-colour-modes)
- [Labels](#labels)
- [Interaction](#interaction)
  - [Hover highlight](#hover-highlight)
  - [Tooltips](#tooltips)
- [Callbacks](#callbacks)
- [Configuration reference](#configuration-reference)
- [Demo tabs](#demo-tabs)
  - [Global Merchandise Trade](#global-merchandise-trade)
  - [Migration Corridors](#migration-corridors)
  - [Project Phase Handovers](#project-phase-handovers)
  - [Cross-Team Handovers](#cross-team-handovers)
- [Notes](#notes)
- [See also](#see-also)

## Quick start

```cpp
#include "Plugins/Charts/UltraCanvasChordChart.h"

using namespace UltraCanvas;

auto chart = CreateChordChart("trade", 20, 20, 700, 700);
chart->SetChartTitle("Trade between regions");
chart->SetTooltipsEnabled(true);

// Rows are the source, columns the destination: flows[i][j] moves from i to j.
std::vector<std::vector<double>> flows = {
    {  0, 420, 650},
    {380,   0, 580},
    {720, 520,   0}
};
chart->ImportFromMatrix(flows, {"North America", "Europe", "East Asia"});

container->AddChild(chart);
```

With no colours given, the categories take the chart's built-in ten-colour
palette in order. The same chart in one call:

```cpp
auto chart = CreateChordChartFromMatrix("trade", 20, 20, 700, 700, flows,
                                        {"North America", "Europe", "East Asia"});
```

## Data model

The chart holds its own data in a `ChordMatrix`; it does not need an
`IChartDataSource`. The matrix is square, one row and one column per category:

```cpp
struct ChordMatrix {
    std::vector<std::vector<double>> flows;   // flows[i][j]: amount from i to j
    std::vector<ChordCategory> categories;

    void AddCategory(const ChordCategory& category);
    bool SetFlow(size_t from, size_t to, double value);
    double GetFlow(size_t from, size_t to) const;
    size_t Size() const;
    void Clear();
    void ResizeMatrix();          // Grow/shrink flows to match categories
    void RecalculateTotals();
    double GrandTotal() const;
};

struct ChordCategory {
    std::string name;
    Color color;

    // Computed by the chart; writing to these has no effect.
    double totalValue;   // Throughput: outgoing + incoming
    double startAngle;   // Radians
    double endAngle;
};
```

Rules worth knowing:

- **Arc size is throughput.** A category's arc is sized by its row sum plus its
  column sum, so a category that only receives still gets an arc.
- **The diagonal counts twice.** A self-flow (`flows[i][i]`) occupies both an
  outgoing and an incoming sub-arc of the same category.
- **Negative values are clamped to zero.** `SetFlow` and `ImportFromMatrix`
  store `max(0, value)`.
- **Zero-throughput categories vanish.** A category with no drawn flow gets no
  arc, no padding slot and no label.
- **Sub-arcs are grouped by partner.** Inside each arc the outgoing slice to a
  partner sits next to the incoming slice from the same partner, so both
  directions of a relationship meet at the same place on the ring.

The angles and `totalValue` of `GetChordMatrix().categories` are filled in when
the chart lays itself out (the first render after a change). They reflect the
flows actually drawn, i.e. after [`SetMinFlowValue`](#ribbons) filtering.

## Loading data

Three ways to fill the chart.

**From a matrix** — replaces everything. Returns `false` (and changes nothing)
when the matrix is not square or its size differs from the number of names.
Colours are optional; a missing colour, or one with alpha 0, takes the palette
colour for that position.

```cpp
std::vector<std::vector<double>> migration = {
    {  0,  95, 640},
    {310,   0, 720},
    { 55,  70,   0}
};
std::vector<std::string> regions = {"Africa", "Asia", "Europe"};
std::vector<Color> colors = {
    Color(255,  87,  34, 255),
    Color(255, 193,   7, 255),
    Color( 63,  81, 181, 255)
};
if (!chart->ImportFromMatrix(migration, regions, colors)) {
    // Matrix and names do not match.
}
```

**Category by category** — `AddCategory` appends a category (a colour with
alpha 0 picks the next palette colour), and `SetFlow` sets one directed cell.
Out-of-range indices are ignored.

```cpp
chart->ClearData();
chart->AddCategory("Planning",    Color(233,  30,  99, 255));
chart->AddCategory("Design",      Color(  3, 169, 244, 255));
chart->AddCategory("Development", Color(0, 0, 0, 0));   // Palette colour
chart->SetFlow(0, 1, 100);   // Planning -> Design
chart->SetFlow(1, 2, 150);   // Design -> Development
chart->SetFlow(2, 1, 25);    // Feedback edge back to Design
```

**From a prepared `ChordMatrix`** — useful when the data is built elsewhere.
`SetChordMatrix` resizes the flow table to the category count and recomputes
the totals.

```cpp
ChordMatrix m;
m.AddCategory(ChordCategory("Frontend", Color(66, 133, 244, 255)));
m.AddCategory(ChordCategory("Backend",  Color(219, 68, 55, 255)));
m.SetFlow(0, 1, 85);
m.SetFlow(1, 0, 95);
chart->SetChordMatrix(m);
```

Reading back:

```cpp
const ChordMatrix& data = chart->GetChordMatrix();
double frontendToBackend = chart->GetFlow(0, 1);
size_t categories = chart->GetCategoryCount();
size_t ribbons    = chart->GetFlowCount();   // Drawn ribbons, after filtering
```

`GetFlowCount()` counts the ribbons of the last layout, so it is 0 until the
chart has rendered once after a data change.

## Geometry

```cpp
void SetRadius(float r);
void SetAutoFitRadius(bool on);
void SetRingThickness(float pixels);
void SetStartAngle(float degrees);
void SetCategoryPadding(float degrees);
```

| Setter | Default | Notes |
|--------|---------|-------|
| `SetAutoFitRadius` | `true` | Outer radius = half the smaller side of the plot area. The plot area already reserves room for the title and labels. |
| `SetRadius` | auto | Explicit outer radius; turns auto-fit off. `<= 0` turns auto-fit back on. Still clamped so the ring never spills out of the element. |
| `SetRingThickness` | 20 px | Width of the category band; minimum 2 px, and never more than half the radius. |
| `SetStartAngle` | -90° | Where the first category begins; -90° is 12 o'clock. Categories run clockwise in matrix order. |
| `SetCategoryPadding` | 2° | Gap between neighbouring arcs, clamped to 0..30°. If the gaps would eat the whole circle, padding is dropped. |

`GetRadius()` returns the outer radius of the last layout (0 before the first
render); `GetRingThickness()`, `GetStartAngle()` and `GetCategoryPadding()`
return the configured values.

The chart is a normal layout item: give it flex grow and it re-fits its ring on
every resize, with no resize handler of your own (this is how every demo tab is
built):

```cpp
chart->layoutItem.SetFlex(1, 1, CSSLayout::Dimension::Px(0))
                 .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
```

## Ribbons

```cpp
void SetRibbonOpacity(float opacity);
void SetRibbonCurvature(float curvature);
void SetRibbonColorMode(ChordRibbonColorMode mode);
void SetRibbonBorder(const Color& color, float width);
void SetMinFlowValue(double minValue);
```

| Setter | Default | Notes |
|--------|---------|-------|
| `SetRibbonOpacity` | 0.7 | 0..1, applied to every ribbon fill and border. |
| `SetRibbonCurvature` | 1.0 | 0..1. 1 pulls both bezier controls onto the centre (the classic chord look); lower values let ribbons bulge outward; 0 is straight. |
| `SetRibbonColorMode` | `Larger` | See below. |
| `SetRibbonBorder` | white, alpha 90, 1 px | Outline drawn around each ribbon. Width 0 or a transparent colour turns it off. |
| `SetMinFlowValue` | 0 | Flows **at or below** this value are not drawn. The arcs are rebuilt from the surviving flows, so the ring always accounts for exactly what is on screen. |

Ribbons are painted thickest first, so slender ribbons stay visible on top.

### Ribbon colour modes

```cpp
enum class ChordRibbonColorMode {
    Source,     // Ribbon takes the source category's colour
    Target,     // Ribbon takes the target category's colour
    Larger,     // Colour of whichever endpoint has the larger throughput
    Blend       // 50/50 blend of both endpoints
};
```

- `Source` makes direction obvious — use it when flows are strongly one-way
  (the *Migration Corridors* tab).
- `Target` answers "where does each category's inflow come from" in reverse.
- `Larger` (default) keeps the dominant categories visually dominant.
- `Blend` is neutral: no endpoint wins, good for collaboration networks.

## Labels

```cpp
void SetLabelPlacement(ChordLabelPlacement placement);
void SetShowLabels(bool show);
void SetLabelDistance(float pixels);
void SetLabelFont(const std::string& family, float size, FontWeight weight);
void SetLabelColor(const Color& c);
void SetValueFormatter(std::function<std::string(double)> f);
```

```cpp
enum class ChordLabelPlacement {
    NoLabels,        // No labels
    Outside,     // Horizontal text just beyond the ring
    Radial       // Text runs along the radius, flipped on the left half
};
```

- `Outside` (default) — horizontal names, right-aligned on the left half of the
  ring so they always grow away from it.
- `Radial` — names run along the radius and are flipped on the left half so
  they read outward. Suits many categories or long names; the chart reserves
  the full label room above and below the ring for them.
- `SetShowLabels(true/false)` is shorthand for `Outside` / `NoLabels`.

`SetLabelDistance` is the gap between the ring and the text (default 12 px).
`SetLabelFont` defaults to Arial 11 normal; `SetLabelColor` to dark grey
`Color(45, 45, 45, 255)`. The chart title (`SetChartTitle`, from the base
class) is drawn in the label font, 4 pt larger and bold.

The plot area reserves room for the longest category name, estimated from its
character count, so the ring shrinks as names grow longer.

`SetValueFormatter` formats every number in the default tooltips. Without one,
whole numbers print without decimals and others with `%g`:

```cpp
chart->SetValueFormatter([](double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "$%.0fB", v);
    return std::string(buf);
});
```

## Interaction

### Hover highlight

```cpp
void SetHoverHighlightEnabled(bool on);
void SetDimmedOpacity(float factor);
```

With hover highlight on (the default):

- Hovering a **category arc** isolates it: the arc swells by 3 px and lightens,
  its ribbons stay at full opacity, and every other arc, ribbon and label fades.
- Hovering a **ribbon** isolates that one flow and its two endpoint categories;
  the ribbon lightens and its border thickens by 1 px.

`SetDimmedOpacity` is the alpha multiplier for the faded elements (default
0.25, clamped to 0..1); 1.0 effectively disables dimming. Leaving the chart
clears the hover state.

### Tooltips

```cpp
void SetTooltipsEnabled(bool on);
```

`SetTooltipsEnabled` forwards to the base class's `SetEnableTooltips`. The
default texts are:

| Hovered | Tooltip |
|---------|---------|
| Category | Name, `Total`, `Outgoing`, `Incoming`, and `Share` of the grand total as a percentage |
| Ribbon | `Source to Target`, `Flow`, and — when the reverse flow is non-zero — `Return` and `Net` (flow minus return) |

Every number goes through the value formatter. Replace either text entirely:

```cpp
chart->customCategoryTooltip = [](const ChordCategory& cat) {
    return cat.name + "\nThroughput: " + std::to_string(static_cast<int>(cat.totalValue));
};
chart->customFlowTooltip = [chart](const ChordFlow& flow) {
    const auto& cats = chart->GetChordMatrix().categories;
    return cats[flow.fromIndex].name + " sends " +
           std::to_string(static_cast<int>(flow.value)) + " to " +
           cats[flow.toIndex].name;
};
```

The `ChordFlow` passed to `customFlowTooltip` carries the endpoints, the value,
the resolved colour and the computed sub-arc angles:

```cpp
struct ChordFlow {
    size_t fromIndex;
    size_t toIndex;
    double value;
    Color  flowColor;
    bool   isVisible;

    double sourceStartAngle;
    double sourceEndAngle;
    double targetStartAngle;
    double targetEndAngle;
};
```

## Callbacks

```cpp
std::function<void(size_t categoryIndex)> onCategoryClick;
std::function<void(size_t from, size_t to, double value)> onFlowClick;
std::function<void(size_t categoryIndex)> onCategoryHover;
std::function<std::string(const ChordCategory&)> customCategoryTooltip;
std::function<std::string(const ChordFlow&)> customFlowTooltip;
```

- `onCategoryClick` — left click on a category arc.
- `onFlowClick` — left click on a ribbon; gives the directed endpoints and the
  value of the ribbon on top at that point.
- `onCategoryHover` — fired once each time the pointer enters a different
  category arc (not repeatedly while it moves inside one).

Category indices are positions in `GetChordMatrix().categories`.

```cpp
chart->onCategoryClick = [chart](size_t index) {
    const auto& cat = chart->GetChordMatrix().categories[index];
    std::printf("%s: throughput %.0f\n", cat.name.c_str(), cat.totalValue);
};
chart->onFlowClick = [chart](size_t from, size_t to, double value) {
    double back = chart->GetFlow(to, from);
    std::printf("%zu -> %zu: %.0f (return %.0f)\n", from, to, value, back);
};
```

## Configuration reference

| Area | Members |
|------|---------|
| Factories | `CreateChordChart`, `CreateChordChartFromMatrix` |
| Data | `SetChordMatrix`, `GetChordMatrix`, `AddCategory`, `SetFlow`, `GetFlow`, `ClearData`, `ImportFromMatrix`, `GetCategoryCount`, `GetFlowCount` |
| Geometry | `SetRadius`, `GetRadius`, `SetAutoFitRadius`, `SetRingThickness`, `GetRingThickness`, `SetStartAngle`, `GetStartAngle`, `SetCategoryPadding`, `GetCategoryPadding` |
| Ribbons | `SetRibbonOpacity`, `GetRibbonOpacity`, `SetRibbonCurvature`, `SetRibbonColorMode`, `SetRibbonBorder`, `SetMinFlowValue`, `GetMinFlowValue` |
| Labels | `SetLabelPlacement`, `SetShowLabels`, `SetLabelDistance`, `SetLabelFont`, `SetLabelColor`, `SetValueFormatter` |
| Interaction | `SetHoverHighlightEnabled`, `SetTooltipsEnabled`, `SetDimmedOpacity` |
| Callbacks | `onCategoryClick`, `onFlowClick`, `onCategoryHover`, `customCategoryTooltip`, `customFlowTooltip` |
| From the base | `SetChartTitle`, `SetEnableTooltips` |

The chord chart turns off the base class's grid, axes and plot background and
does not draw them: they mean nothing on a radial chart.

## Demo tabs

The demo page is a flex column (title, description, tabs). Each tab is a flex
row: the chart takes all the width a fixed 214 px control panel leaves. The
panel drives only its own tab's chart and starts from the settings that chart
was built with: ribbon opacity, curvature, category gap, ring thickness, colour
mode, label placement, the minimum-flow filter, the dim factor and the hover
highlight switch.

### Global Merchandise Trade

Eight world regions, billions USD, an asymmetric 8×8 matrix with explicit
colours. Default `Larger` colouring and outside labels; the formatter adds the
currency:

```cpp
auto chart = CreateChordChart("TradeChord", 0, 0, 0, 0);
std::vector<std::string> regions = {"North America", "Europe", "East Asia"};
std::vector<std::vector<double>> trade = {
    {  0, 420, 650},
    {380,   0, 580},
    {720, 520,   0}
};
chart->ImportFromMatrix(trade, regions);
chart->SetTooltipsEnabled(true);
chart->SetValueFormatter([](double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "$%.0fB", v);
    return std::string(buf);
});
```

### Migration Corridors

Six regions in thousands of people. Several corridors run almost entirely one
way, so the ribbons are coloured by their **source** to make direction
readable at a glance:

```cpp
auto chart = CreateChordChart("MigrationChord", 0, 0, 0, 0);
chart->ImportFromMatrix(migration, regions, colors);
chart->SetTooltipsEnabled(true);
chart->SetRibbonColorMode(ChordRibbonColorMode::Source);
chart->SetValueFormatter([](double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0fk", v);
    return std::string(buf);
});
```

### Project Phase Handovers

A sparse matrix of six phases with cyclical feedback edges (Testing back to
Development, Maintenance back to Development). Wider gaps and stronger ribbons
separate the few relationships:

```cpp
auto chart = CreateChordChart("PhaseChord", 0, 0, 0, 0);
chart->ImportFromMatrix(phases, phaseNames, phaseColors);
chart->SetTooltipsEnabled(true);
chart->SetCategoryPadding(4.0f);
chart->SetRibbonColorMode(ChordRibbonColorMode::Source);
chart->SetRibbonOpacity(0.8f);
```

### Cross-Team Handovers

Ticket handovers between six teams. No colours are passed, so the built-in
palette is used; blended ribbons, radial labels, a thin ring and a stronger
ribbon border:

```cpp
auto chart = CreateChordChart("TeamChord", 0, 0, 0, 0);
chart->ImportFromMatrix(teamFlows, {"Frontend", "Backend", "Platform",
                                    "QA", "Design", "Support"});
chart->SetTooltipsEnabled(true);
chart->SetRibbonColorMode(ChordRibbonColorMode::Blend);
chart->SetLabelPlacement(ChordLabelPlacement::Radial);
chart->SetRingThickness(14.0f);
chart->SetRibbonBorder(Color(255, 255, 255, 140), 1.0f);
```

## Notes

- Data setters drop the cached layout and request a redraw; you never need to
  invalidate by hand. A resize by the layout engine is picked up on the next
  render.
- An empty chart (no categories) draws the base class's empty state.
- `ChordFlow::isVisible` is honoured when drawing and hit testing, but the
  chart builds its flow list itself, so there is no public way to hide one
  ribbon; filter by value with `SetMinFlowValue` or set the cell to 0.
- No enumerator is named `None`: the X11 headers define a `None` macro, so
  the placement that shows no label is `ChordLabelPlacement::NoLabels`.

## See also

- [UltraCanvasSunburstChartExamples.md](UltraCanvasSunburstChartExamples.md) — another radial chart, for hierarchies rather than flows
- [UltraCanvasFlowChartExamples.md](UltraCanvasFlowChartExamples.md) — flows as a node-and-edge diagram
- [UltraCanvasCircularCharts.md](UltraCanvasCircularCharts.md) — the circular chart family
- [UltraCanvasChartEngine.md](UltraCanvasChartEngine.md) — the shared chart base

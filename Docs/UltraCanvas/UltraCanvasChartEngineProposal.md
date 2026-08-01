# UltraCanvasChartEngine — Investigation & Architecture Proposal

Status: **Investigation complete, all design decisions resolved (§11, §12.10).
P0 (the label engine upgrade, §7) is implemented and tested; the engine itself
is not started.** This document reports what the existing chart code
actually contains, what is duplicated or broken, and specifies one layered chart
engine to replace the duplication — with the parallel coordinate chart as its
first native client.

Companion document:
[`UltraCanvasParallelCoordinateChartProposal.md`](UltraCanvasParallelCoordinateChartProposal.md)
(the chart-specific feature list; this document is the engine underneath it).

Author: UltraCanvas Framework
Last Modified: 2026-08-01

---

## 1. Scope and method

Everything below comes from reading the current tree, not from assumption:

* 43 chart headers (14,043 lines) in `include/Plugins/Charts/`
* 39 chart sources (42,874 lines) in `Plugins/Charts/`
* `UltraCanvasChartElementBase.{h,cpp}` (421 + 468 lines) — the only shared base
* `UltraCanvasLabelPlacement.{h,cpp}` (156 + 415 lines) — the shared label solver
* every `Set*` entry point across all chart headers, counted and grouped
* the render entry points, legend/axis/colour-bar/3D-projection implementations,
  and every ad-hoc label-overlap workaround

---

## 2. What exists today

### 2.1 The base class is thin, and most charts bypass it

`UltraCanvasChartElementBase` provides: plot area + data bounds cache, a
`ChartCoordinateTransform`, background/grid/axes/title painting, tooltips,
zoom/pan/selection **flags**, an animation clock, and `RenderValueLabels`.

Measured reality:

| Finding | Count | Consequence |
|---|---|---|
| Chart elements that override `Render()` completely | **21 of 43** | They lose the base background, grid, axes, title and tooltip flow and re-implement it |
| Files with their own legend renderer (`RenderLegend`, `RenderDiscreteLegend`, `RenderSizeLegend`, `RenderTitleAndLegend`) | **12** | Four different legend layouts, four styling vocabularies |
| Files with their own colour bar | **3** (heatmap, hexbin, contour) | Three tick/format implementations |
| Files with their own axis/tick rendering | **10** | Ten tick-selection and formatting behaviours |
| Separate 3D camera + `Project()` implementations | **3** (ScatterPlot3D, ContourSurface3D, ContourSurfaceGL) | Three camera conventions, three orbit handlers |
| Distinct ad-hoc label-overlap workarounds | **5** | See §4 |
| Charts using the shared label solver | **6 of 43** | Everything else overlaps or thins blindly |

The root cause is structural: `Render()` starts with

```cpp
if (!dataSource || dataSource->GetPointCount() == 0) { DrawEmptyState(ctx); return; }
```

`IChartDataSource` is a flat list of `(x, y)` points. **Any chart whose data is
not a list of 2-D points cannot use the base class at all** — radar, contour,
heatmap, Gantt, chord, sunburst, Kanban, parallel coordinates. They override
`Render()` and start from an empty canvas. That is where all the duplication
comes from, and it is why bolting more features onto the current base class
would not help.

Other concrete defects in the base:

* `RenderGrid()` hardcodes 10 vertical and 8 horizontal lines, ignoring the
  actual tick positions — grid and ticks do not line up.
* the title is positioned with `GetWidth()/2 - chartTitle.length() * 5`, a
  character-count guess instead of `GetTextLineDimensions`.
* `RenderAxisLabels()` thins x labels with `labelStep = count / 12` — blind
  uniform thinning with no measurement of the actual text width.
* there is no layer discipline: `RenderChart()` is one virtual that draws
  everything, so a chart that wants a highlight wash *behind* the grid but a
  group outline *above* the series has nowhere to put them.

### 2.2 The public API has drifted

Counted across all chart headers:

| Concept | Competing spellings found |
|---|---|
| Title | `SetChartTitle` (10), `SetTitle` (7) |
| Tooltips | `SetEnableTooltips` (9), `SetTooltipsEnabled` (7) |
| Colours | `SetPalette` (6), `SetColorPalette` (5), `SetColormap` (5), `SetCustomColormap` (4) |
| Formatting | `SetValueFormatter` (6), `SetValueFormat` (3), `SetXTickFormatter`/`SetYTickFormatter`/`SetZTickFormatter` (4/4/3) |
| Legend | `SetShowLegend` (5), `SetLegendPosition` (6), `SetLegendMode` (3), `SetLegendTitle` (3) |

`using ValueFormatter = std::function<std::string(double)>` is declared
**three times** in three headers (ScatterPlot3D, ContourSurface3D,
ContourSurfaceGL) rather than once in a shared header.

### 2.3 What the charts collectively already know how to do

Scanning every chart produced this capability inventory — it is the shopping
list the engine API must cover, and each item already exists somewhere:

| Capability | Where it exists today |
|---|---|
| Cartesian axes, ticks, formatters, rotated labels | base class, financial, jitter, quadrant, bubble, scatter3D |
| Multiple/secondary axes | financial (price + volume), Mekko (dual proportional) |
| Categorical axes, category jitter | jitter plot, population, dumbbell |
| Time axes | `UltraCanvasTimeAxis.h`, Gantt, timeline, cumulative flow |
| Log / percentile scaling | jitter (`AxisScale`), heatmap (`HeatmapScale`) |
| Colormaps + colour bar | `UltraCanvasColormap.h`, heatmap, hexbin, contour |
| Categorical palettes + discrete legend | pie, radar, nested area, contour, Mekko |
| Trend / regression line | `SpecificChartElements` (`ComputeLinearRegression`, `TrendLineStyle`) |
| Moving average line | financial chart |
| Reference/average line | jitter (`RenderNationalAverageLine`) |
| Correlation statistic annotation | scatter3D (correlation line), jitter |
| Background region shading | quadrant chart (`SetShowQuadrantColors`, `QuadrantDefinition`) |
| Density/marginal summaries | hexbin, contour (KDE), jitter (quartiles, mean/median markers) |
| 3D camera, orbit, painter sort, GL path | scatter3D, contourSurface3D, contourSurfaceGL, `UltraCanvasGLSurface` |
| Label collision solving | `UltraCanvasLabelPlacement` (6 charts) |
| Animation with a timer | radar (`UltraCanvasTimer`), funnel, pyramid |
| Tooltips | base class + `UltraCanvasTooltipManager` |
| Static image surfaces (video / GL backgrounds are out of scope) | `UltraCanvasImageElement` |
| Gradients, clipping, dashes, transforms, pixmap blit | `IRenderContext` (`CreateLinearGradientPattern`, `ClipPath`, `SetLineDash`, `DrawPixmap`) |

Nothing in the nine layers the brief asks for needs a new dependency. It needs
**one place to put these**.

---

## 3. Label placement — audit answer

The brief asks specifically: *"Check if the data is filled each time a label is
calculated. If that is the case it must be improved as the list can grow and be
reused each time a label is positioned."*

### 3.1 Inside one call: correct

`PlaceShapeLabels()` builds the working list **once** before the loop and grows
it as labels are placed:

```cpp
std::vector<Rect2Dd> placed(options.obstacles.begin(), options.obstacles.end());
placed.reserve(placed.size() + labels.size());
for (size_t i = 0; i < labels.size(); ++i) { ... placed.push_back(rect); }
```

So within a single call the list is *not* rebuilt per label. That part is
already what the brief asks for.

### 3.2 Across calls: exactly the problem described

There is **no session object and no incremental entry point**. Every call
rebuilds `placed` from `options.obstacles` and forgets everything placed by the
previous call. A chart that places labels in several passes — axis tick labels,
then series value labels, then limiter captions, then the legend, then
annotations — must either

* call the solver once with every label of every kind merged into one array
  (which no chart does, because the passes happen in different render methods
  at different z levels), or
* manually copy the previous pass's rects into `obstacles` and re-pass the whole
  `shapes` array (which only the quadrant, radar and bubble charts partially do).

The practical result is that **axis labels, value labels and legends currently
do not know about each other**, in every chart. That is the single biggest
source of visible label clutter today.

### 3.3 Cost: quadratic, and it runs inside the render path

`Score()` is called for every candidate of every label and loops over **all
shapes** (`avoidOtherShapes`) and **all already-placed rects**:

```
cost ≈ labels × candidates × (shapes + placed)
```

with ~28 candidates per label (4 sides × 7 slide positions), plus `SlideClear()`
re-scanning `placed` up to 16 times per direction. For a chart with 500 value
labels that is on the order of **7 million rectangle tests per frame**, executed
at render time (the nested-area chart solves labels inside its render path).
This is why no dense chart uses the solver: it does not scale to data labels,
only to a handful of diagram labels.

### 3.4 Capability gaps for chart use

| Gap | Why charts need it |
|---|---|
| No session / incremental API | §3.2 — the whole frame must share one collision world |
| No spatial index | §3.3 — needed before value labels can use the solver at all |
| No rotated labels | Axis labels at 45°, contour inline labels along a tangent, image-4-style curved group titles |
| Rect-only obstacles | A diagonal series line's bounding box is nearly the whole plot; charts need **polyline/segment obstacles** |
| No priority | Charts must guarantee min/max/last/pinned labels survive; input order is the only lever today |
| No suppression policy | `fitted=false` still returns a position and the caller draws it anyway — overprinting. Charts need "drop this label" |
| No minimum-separation rule | The brief's explicit requirement: a configurable minimum distance between labels, independent of overlap |
| No leader lines | A label pushed far from its point needs a connector |
| No point anchors | Every chart fabricates a zero-radius circle `LabelShape` per data point |
| No 1-D fast path | Axis tick decluttering is a sorted-interval problem; running the 2-D solver on 200 ticks is wasteful |

---

## 4. The five ad-hoc label workarounds (to be deleted)

| Location | Technique |
|---|---|
| `UltraCanvasChartElementBase.cpp:225` | `labelStep = count / maxLabels` blind uniform thinning |
| `UltraCanvasPieChart.cpp:1166` | Per-hemisphere sort + vertical push-apart, then leader-line re-anchor |
| `UltraCanvasHeatmapChart.cpp:587,606` | "Draw the next label only if it does not overlap the last drawn one" |
| `UltraCanvasBubbleChart.cpp:594` | Private `overlapsAny()` candidate search |
| `UltraCanvasPyramidChart.cpp:2481` | Tier-aligned labels pushed apart just enough |

All five are the same problem solved five times, none of them aware of the axis
labels or the legend. All five become calls into one label service.

---

## 5. Proposed engine

### 5.1 Design position

**The engine is a three-phase render frame plus shared services — not a
universal data model.** A single data model that covers Gantt, chord, Kanban,
sunburst *and* parallel coordinates would be a fantasy; charts keep their own
domain models, and each chart file keeps its own drawing code and inserts it
into the middle phase. What charts stop owning is: layout, axes, scales,
projection, theme/palette, legend, colour bar, limiters, highlights,
distributions, label placement, interaction, background and the render/dirty
pipeline.

### 5.2 Files

```
include/Plugins/Charts/Engine/UltraCanvasChartEngine.h        # frame, layer stack, dirty model, render driver
include/Plugins/Charts/Engine/UltraCanvasChartProjection.h    # Vertical / Horizontal / Polar / Space3D + one camera
include/Plugins/Charts/Engine/UltraCanvasChartAxis.h          # axis, scale, ticks, formatter, multi-axis set
include/Plugins/Charts/Engine/UltraCanvasChartTheme.h         # theme, palette, colormap binding, ValueFormatter (one home)
include/Plugins/Charts/Engine/UltraCanvasChartLegend.h        # discrete legend + colour bar (replaces 12 + 3 copies)
include/Plugins/Charts/Engine/UltraCanvasChartLimiters.h      # min/max/average/target/band lines
include/Plugins/Charts/Engine/UltraCanvasChartHighlights.h    # rect / ellipse / confidence / hull-blob / value band
include/Plugins/Charts/Engine/UltraCanvasChartDistribution.h  # axis histograms, density, box summaries
include/Plugins/Charts/Engine/UltraCanvasChartLabels.h        # chart-facing label broker over the label solver
include/Plugins/Charts/Engine/UltraCanvasChartBackground.h    # colour / gradient / image (no video / GL / 3D scene)
include/Plugins/Charts/Engine/UltraCanvasChartInteraction.h   # hover, select, brush, zoom/pan, crosshair, callbacks
Plugins/Charts/Engine/*.cpp
```

Upgrades in place (shared with the diagram family):
`include/UltraCanvasLabelPlacement.h` + `core/UltraCanvasLabelPlacement.cpp`.

### 5.3 Chart types = projections

The brief's *vertical / horizontal / 3D* becomes one interface with four
implementations, so a series renderer is written once and works in all of them:

```cpp
enum class ChartProjectionKind { Vertical, Horizontal, Polar, Space3D };

struct ChartValuePoint { double primary, secondary, depth; };  // domain, value, z

class IChartProjection {
public:
    virtual ChartProjectionKind Kind() const = 0;
    virtual Point2Dd  ValueToScreen(const ChartValuePoint&) const = 0;
    virtual bool      ScreenToValue(const Point2Dd&, ChartValuePoint&) const = 0;
    virtual double    DepthOf(const ChartValuePoint&) const { return 0.0; }  // painter sort key
    virtual bool      IsVisible(const ChartValuePoint&) const { return true; }
};
```

* `Vertical` — domain along x, value up y (line, bar, PCP with vertical axes)
* `Horizontal` — the same renderers transposed (horizontal bars, dumbbell,
  population pyramid, PCP with axes as rows). **No separate chart classes.**
* `Polar` — **confirmed as the fourth kind**; the framework has 8 radial charts
  (radar, polar, sunburst, radial bar, chord, pie, circular progress, circular
  infographic) each with its own angle maths
* `Space3D` — yaw/pitch/distance camera, perspective divide, painter depth sort,
  optional `UltraCanvasGLSurface` backend. **One** camera replaces the three
  `Project()` implementations found.

### 5.4 Rendering is three phases, not ten layers

The engine renders in **three phases**. The specific chart file implements
**only the middle one** and inserts itself there; everything above and below is
engine-supplied:

```
Phase 1  UNDER the chart   — background, highlights, grid, limiters, axes,
                             grid/tick labels, axis titles, distributions
Phase 2  THE CHART         — the specific chart's own drawing   ← chart file
Phase 3  OVER the chart    — analysis lines, value labels, legend, callouts,
                             annotations, interaction overlay
```

The brief's nine features remain as **ordering slots inside a phase**, not as an
API the chart author has to think about:

| Phase | Slot | Layer | Brief # | Contents |
|---|---|---|---|---|
| **1** | 100 | `Background` | 8 | element fill, chart-area fill, plot-area fill, gradient, image, per-level bands |
| **1** | 200 | `Highlight` (wash) | 7 | group rectangles, circular/elliptical shading, confidence ellipses, hull blobs, value/area bands |
| **1** | 300 | `Grid` | 6 | major/minor gridlines derived **from the axis ticks**, alternating stripes, polar rings, 3D floor grid |
| **1** | 400 | `Limiter` | 5 | min, max, average, median, target, threshold, up/down limits, tolerance bands, captions |
| **1** | 500 | `Axes` | 4 | axis lines, ticks, tick labels, axis titles, endpoint labels, category labels, multi-axis |
| **1** | 550 | `Distribution` | (new) | marginal histograms / density / box summaries on any axis |
| **2** | 600 | `Content` | 3 | **the specific chart** — its geometry and its graph values |
| **3** | 700 | `Analysis` | 2 | correlation line, regression/trend, moving average, progress line, statistic annotation (`τ = -0.34; p = 1.8e-59`) |
| **3** | 800 | `Annotation` | 1 | solved labels, legend, colour bar, callouts, leader lines, arrows/indicators |
| **3** | 900 | `Interaction` | 9 | hover emphasis, crosshair, tooltip anchor, brush bands + handles, rubber band, drag ghosts |

The driver in the base class is fixed and short:

```cpp
void UltraCanvasChartElementBase::Render(IRenderContext* ctx, const Rect2Df& dirty) {
    EnsureLayout();                    // §5.6 — only when geometry/data/style changed
    EnsureLabelPlan();                 // §5.7 — solver runs here, NOT per frame
    RenderPhaseUnder(ctx, frame);      // engine
    RenderChart(ctx, frame);           // the specific chart file — the one override
    RenderPhaseOver(ctx, frame);       // engine
}
```

Two rules give the brief's "prevent wrong rendering":

1. **Every phase and every slot inside it renders within its own
   `PushState()` / `ClipPath()` / `PopState()`.** No fill colour, dash pattern,
   transform or clip leaks into the next — the class of bug that makes charts
   render wrongly today.
2. **Phase 2 is clipped to the plot area by default.** A chart cannot paint over
   the axes, tick labels or legend by accident. A chart that genuinely needs to
   draw into the margin (a spill-out marker) opts in explicitly.

### 5.5 What the specific chart file provides

A chart type implements one small interface. Everything it does *not* implement
is supplied by the engine:

```cpp
class IChartContent {
public:
    // What axes exist, their ranges, scales and titles.
    virtual void DescribeAxes(ChartAxisSet&) = 0;

    // Optional extra margin this chart needs (axis header chips, spill-out
    // markers, per-axis histograms...). Runs in the measure pass, not per frame.
    virtual void Measure(ChartLayoutRequest&) {}

    // Labels this chart wants placed. Called ONLY when the label plan is
    // rebuilt (§5.7), never on an ordinary redraw.
    virtual void CollectLabels(ChartLabelRequest&) {}

    // Phase 2. The only method that must be implemented.
    virtual void RenderChart(IRenderContext*, const ChartFrame&) = 0;

    // Optional: chart-specific picking for the interaction layer.
    virtual bool HitTest(const Point2Dd&, ChartHit&) const { return false; }
};
```

So a new chart type is: a data model, a `RenderChart()`, and — if it has labels
or unusual margins — two short descriptor methods. Background, grid, axes,
limiters, highlights, legend, label placement and interaction come for free and
are never re-implemented.

### 5.6 Layout is a two-pass negotiation, run on change only

Today every chart guesses its margins (`marginLeft = 60` in the base class).
Instead:

```
pass 1  Measure: axes, legend, colour bar, marginal histograms, titles and the
                 chart itself report the space they need on each side
pass 2  Solve:   engine subtracts the reservations → plotArea, freezes ChartFrame
```

`EnsureLayout()` runs this only when the layout inputs changed (§5.9); an
ordinary repaint reuses the frozen frame:

```cpp
struct ChartFrame {
    Rect2Dd                 elementBounds, chartArea, plotArea;
    const IChartProjection* projection;
    const ChartAxisSet*     axes;
    const ChartTheme*       theme;
    const ChartPalette*     palette;
    const ChartLabelPlan*   labelPlan;      // solved earlier, read-only while drawing
    const ChartInteractionState* interaction;
    double                  animationProgress;
    uint64_t                generation;
};
```

The frame is **read-only during rendering**. All three phases receive the same
frozen frame, which is what lets phase 1 and phase 3 be cached independently of
the chart's own drawing.

### 5.7 Label placement runs at build time, not at draw time

**The solver runs when the chart is created or invalidated — never on a
redraw.** Its output is a cached plan that the draw path simply iterates:

```cpp
struct PlacedChartLabel {
    std::string text;
    Rect2Dd     bounds;          // final position, already solved
    double      rotationDegrees;
    Color       color; float fontSize; std::string font;
    ChartLabelClass  klass;
    bool        suppressed;      // decluttered away — skip it
    bool        hasLeader; Point2Dd leaderFrom;
};

struct ChartLabelPlan {
    std::vector<PlacedChartLabel> labels;
    std::vector<bool>             tickVisible;   // per axis, from DeclutterAxisTicks
    uint64_t                      generation;    // matches the frame it was solved for
};
```

`EnsureLabelPlan()` rebuilds the plan **only** when one of the inputs that can
change a label's position changed:

| Rebuilds the label plan | Does **not** rebuild it |
|---|---|
| Data set / appended / cleared | Ordinary repaint, expose, dirty-rect |
| Element resized, layout re-solved | Hover, tooltip, crosshair |
| Axis range / scale / inversion / order changed | Selection, pinning, brushing |
| Font, font size or label style changed | Animation frames (see below) |
| Label text, formatter or visibility changed | Background video / shader frames |
| Legend content or position changed | Scroll / repaint of a parent |
| Explicit `InvalidateLabels()` | Theme colour change (colours only) |

Consequences worth stating:

* **Animation.** Grow-out and transition animations move the geometry every
  frame; re-solving per frame is exactly what this design forbids. Policy:
  labels are solved once against the **final** geometry, hidden while the
  animation runs, and revealed on completion. No per-frame solver work.
* **Zoom / pan / axis reorder.** These do change geometry, so they do invalidate
  the plan — but the rebuild is **throttled**: the previous plan keeps being
  drawn during the drag and the solver runs once **on interaction end** — the chosen policy, so a drag
  costs nothing and the labels catch up on release.
* The persistent plan is also what makes the solver's cost irrelevant in
  practice: the O(L·C·k) work of §7 happens on creation and on genuine change,
  not at 60 Hz.

### 5.8 Which labels take part — decided per chart type

**Axis tick labels, grid labels and axis titles are excluded from the collision
solver by default.** They sit at fixed, constructed positions in the margin
bands, they cannot collide with in-plot labels, and feeding them to a 2-D solver
would be wasted work. They are decluttered by the cheap 1-D pass instead
(`DeclutterAxisTicks`, §7) — excluded from the solver does **not** mean
"allowed to overprint each other".

Each label class gets one of three roles, and the default set is a **per chart
type policy** that a chart may override:

```cpp
enum class ChartLabelClass : uint32_t {
    AxisTick   = 1u<<0, AxisTitle = 1u<<1, GridLabel      = 1u<<2,
    ValueLabel = 1u<<3, SeriesName = 1u<<4, LimiterCaption = 1u<<5,
    HighlightLabel = 1u<<6, LegendEntry = 1u<<7, Annotation = 1u<<8,
};

struct ChartLabelPolicy {
    uint32_t solved    = ValueLabel|SeriesName|LimiterCaption|HighlightLabel|Annotation;
    uint32_t obstacles = LegendEntry;              // fixed, but solved labels avoid them
    uint32_t excluded  = AxisTick|AxisTitle|GridLabel;   // neither move nor block
};

void SetLabelPolicy(const ChartLabelPolicy&);      // per chart type / per instance
```

* **solved** — routed through the placement solver, may move, may be suppressed.
* **obstacle** — fixed position, but claimed in the collision world so solved
  labels steer around it (the legend box; the pie chart's centre text).
* **excluded** — outside the collision world entirely (axis furniture in the
  margins).

Concrete per-type examples:

| Chart type | Override | Why |
|---|---|---|
| Line / bar / scatter | default | Axis furniture is in the margin; only value labels compete |
| Parallel coordinates | `AxisTitle` → **obstacle** | Axis header chips sit *inside* the plot band at the top of each axis, so in-plot labels must avoid them; the chips themselves declutter in 1-D |
| Heatmap | `AxisTick` → **obstacle** | Row/column labels sit against the cell grid, and cell value labels must not collide with them |
| Radar / polar | `AxisTitle` → **solved** | Spoke labels ring the plot at arbitrary angles and genuinely do collide with each other |
| Pie | `AxisTick` unused; centre text → **obstacle** | Outside slice labels must avoid the centre text and each other |

### 5.9 Dirty model — why the phase split pays for itself

```cpp
enum class ChartDirty : uint32_t {
    Data=1, Geometry=2, Style=4, Labels=8, Selection=16, Hover=32, Animation=64
};
```

The engine maps each flag to the phases and slots it invalidates; cacheable
slots keep a `UCPixmap` of their last output.

| Change | Layout | Label plan | Phase 1 | Phase 2 | Phase 3 |
|---|---|---|---|---|---|
| Hover | — | — | blit | blit | repaint |
| Selection / brush | — | — | blit | repaint | repaint |
| Data | rebuild | rebuild | repaint | repaint | repaint |
| Resize | rebuild | rebuild | repaint | repaint | repaint |
| Theme colour | — | — | repaint | repaint | repaint |
| Animation frame | — | — | blit | repaint | hidden |

Hovering a parallel-coordinate line repaints one overlay; brushing 100k lines
repaints phases 2–3 over a blitted background; neither ever touches the solver.

---

## 6. Unified API surface

Derived from the scan in §2.3. Naming canon resolves the drift in §2.2;
old spellings stay as deprecated inline aliases so nothing breaks.

```cpp
// ---- identity / framing ----
SetChartTitle(text); SetSubtitle(text); SetTitleStyle(font, size, color, align);
SetProjection(ChartProjectionKind); SetTheme(const ChartTheme&);
SetPalette(const ChartPalette&);            // categorical
SetColormap(HeatmapColormap, bool reverse); // continuous
SetValueFormatter(ValueFormatter);          // one ValueFormatter typedef, one home

// ---- axes (one or many) ----
size_t AddAxis(const ChartAxisSpec&);       // side, scale, range, ticks, title, formatter, inverted, categories, time
SetAxisRange(axis, lo, hi); SetAxisScale(axis, ChartScale); SetAxisInverted(axis, bool);
SetAxisTitle(axis, text); SetAxisTickCount(axis, n); SetAxisTickFormatter(axis, fn);
SetAxisCategories(axis, {...}); SetAxisTickRotation(axis, degrees);
SetAxisTickDeclutter(axis, TickDeclutterPolicy);   // replaces blind count/12 thinning

// ---- grid ----
SetShowGrid(bool, ChartGridPart::Major|Minor|Both); SetGridStyle(color, width, dash);
SetGridFollowsTicks(bool);                  // default true — fixes the 10x8 mismatch
SetAlternatingBands(bool, color);

// ---- limiters (phase 1, slot 400) ----
size_t AddLimiter(const ChartLimiter&);     // Min|Max|Average|Median|Target|Threshold|Band|Custom
SetLimiterStyle(id, color, width, dash); SetLimiterCaption(id, text, side);

// ---- highlights (phase 1, slot 200 / phase 3, slot 700) ----
size_t AddHighlight(const ChartHighlight&); // Rectangle|Ellipse|ConfidenceEllipse|Hull|Blob|ValueBand|PointHalo
SetHighlightLabel(id, text, color, bool followContour);
ClearHighlights();

// ---- distributions on axes (images 1 & 2) ----
size_t AddAxisDistribution(const AxisDistribution&);  // Histogram|Density|Box|Strip|Violin

// ---- analysis (phase 3, slot 700) ----
SetShowTrendLine(bool); SetTrendLineStyle(...); SetShowMovingAverage(bool, window);
SetShowCorrelationLine(bool); SetStatisticAnnotation(ChartStatistic, position);

// ---- labels & legend (phase 3) ----
SetShowValueLabels(bool); SetValueLabelPolicy(LabelDeclutterPolicy);
SetMinLabelSeparation(px); SetLabelPriorityRule(LabelPriorityRule);
SetLabelPolicy(const ChartLabelPolicy&);    // which classes are solved / obstacle / excluded
InvalidateLabels();                          // force one re-solve; never called per frame
SetLabelResolveThrottleMs(ms);               // zoom/pan/reorder re-solve cadence
SetShowLegend(bool); SetLegendMode(Discrete|ColorBar|SizeLegend|PinnedItems);
SetLegendPosition(Left|Right|Top|Bottom|Inside|Custom); SetLegendTitle(text);

// ---- background (phase 1, slot 100) ----
SetBackground(ChartBackgroundScope, const ChartBackground&);  // Solid|Gradient|Image

// ---- interaction (phase 3, slot 900) ----
SetEnableTooltips(bool); SetTooltipGenerator(fn);
SetEnableHover(bool); SetEnableSelection(SelectionMode); SetEnableBrushing(bool);
SetEnableZoom(bool); SetEnablePan(bool); SetShowCrosshair(bool);
SetOnHover(fn); SetOnClick(fn); SetOnSelectionChanged(fn); SetOnBrushChanged(fn);

// ---- animation / export ----
SetAnimation(bool, durationMs, ChartEasing); RenderToPixmap(UCPixmap&, Size2Di);
```

---

## 7. Label engine upgrade (prerequisite work) — **implemented**

Shipped in `UltraCanvasLabelPlacement` 2.0 (`include/UltraCanvasLabelPlacement.h`,
`core/UltraCanvasLabelPlacement.cpp`), with `Tests/LabelPlacementTest.cpp`
covering session reuse, index equivalence, rotation, polyline obstacles,
priority, suppression, minimum separation, leader lines, point anchors,
backward compatibility and the tick declutter policies. 800 labels solve in
about 7 ms. Every addition is opt-in and the defaults reproduce the 1.3
behaviour, so the six existing callers are untouched.

One semantic decision came out of the implementation rather than the design:
the solver prefers pushing a label **outside the bounds** over overlapping a
neighbour, so suppression keyed only on overlap would almost never fire. A
label is therefore suppressed when it would overlap something claimed **or**
would have to leave the bounds to avoid it — both are defects a chart wants
gone.

`UltraCanvasLabelPlacement` stays the solver; it gains a session, an index, and
the chart-facing capabilities from §3.4.

The session is **owned by the chart's label plan and outlives the frame**: it is
built when the chart is created, added to incrementally as each phase
contributes its labels, and thrown away only when the plan is invalidated
(§5.7). Redraws never touch it.

```cpp
// ---- session: one collision world, reused across passes ----
class LabelPlacementSession {
public:
    explicit LabelPlacementSession(const LabelPlacementOptions&);
    void AddObstacleRects(const std::vector<Rect2Dd>&);
    void AddObstaclePolyline(const std::vector<Point2Dd>&, double halfWidth);  // NEW: real lines
    void AddShapes(const std::vector<LabelShape>&);
    std::vector<PlacedShapeLabel> Place(const std::vector<ShapeLabel>&);       // incremental
    void Reset();
};

// ---- label inputs, extended ----
struct ShapeLabel {
    ...
    double rotationDegrees = 0.0;   // NEW: rotated tick / inline / contour labels
    int    priority = 0;            // NEW: higher wins; ties fall back to input order
    bool   allowSuppress = false;   // NEW: may be dropped instead of overprinted
    bool   wantLeaderLine = false;  // NEW: emit a connector when pushed away
    Point2Dd anchorPoint;           // NEW: point-anchored labels without a fake shape
};

struct PlacedShapeLabel {
    ...
    bool     suppressed = false;    // NEW: do not draw this one
    bool     hasLeader = false;     // NEW
    Point2Dd leaderFrom;            // NEW
    double   rotationDegrees = 0.0; // NEW: echo back for the drawing code
};

struct LabelPlacementOptions {
    ...
    double minLabelSeparation = 0.0;   // NEW: the brief's explicit minimum distance
    LabelDeclutterPolicy declutter = LabelDeclutterPolicy::PlaceAll;
    //  PlaceAll | SuppressColliding | ThinUniform | PriorityFirst
};

// ---- 1-D fast path for axis ticks ----
std::vector<bool> DeclutterAxisTicks(const std::vector<double>& positions,
                                     const std::vector<double>& extents,
                                     double minGap, TickDeclutterPolicy policy);
//  KeepEveryNth | Greedy | PriorityGreedy | RotateThenThin
```

Implementation changes:

* **Uniform-grid spatial index** over placed rects and shapes, cell size from the
  median label extent. `Score()` and `SlideClear()` query only the overlapping
  cells. Target: 500 labels × 28 candidates goes from ~7M rect tests to ~150k.
* **Oriented bounding boxes** for rotated labels (separating-axis overlap test),
  falling back to the axis-aligned box when `rotationDegrees == 0`.
* **Priority sort** before the greedy pass, keeping input order as the tie-break
  so existing callers are unaffected.
* **Minimum-separation** as a first-class term: candidates closer than
  `minLabelSeparation` to a placed rect are rejected outright rather than merely
  penalised, which is what makes dense value labels behave.
* **Suppression**: when no candidate clears the threshold and `allowSuppress` is
  set, mark `suppressed` instead of returning an overlapping position.
* Unit tests under `Tests/` for: index vs brute-force equivalence, rotation
  overlap, priority survival, suppression counts, tick declutter policies, and a
  performance regression guard.

This phase stands alone: it improves the six charts already using the solver and
lets the other five ad-hoc implementations (§4) be deleted.

---

## 8. Two new shared features from the reference images

### 8.1 Axis distribution bars (images 1 and 2)

Both images are joint plots: a hexbin / dot-density field with a **histogram of
each axis's marginal distribution** drawn outside the plot on the top and right,
sharing the axis scale exactly.

```cpp
struct AxisDistribution {
    size_t axisIndex;
    AxisDistributionKind kind = AxisDistributionKind::Histogram; // Density|Box|Strip|Violin
    AxisDistributionSide side = AxisDistributionSide::Outside;   // Near|Far|Both|Outside
    int    binCount = 0;          // 0 = auto (Freedman–Diaconis)
    double thickness = 48.0;      // reserved band, negotiated in Measure()
    Color  fill, stroke;
    bool   showFilteredOverlay = true;   // brushed subset over the total
    bool   logCount = false;
};
```

Reuses the jitter plot's quartile/mean/median maths for `Box`, and the contour
element's KDE for `Density`. For the parallel coordinate chart this is the same
layer, just repeated on every axis (image 4 of the PCP set).

### 8.2 Background highlighting (images 3, 4, 5)

Three distinct shapes, one layer:

| Image | Shape | Engine support |
|---|---|---|
| 3 — Scrabble divisions | **Confidence ellipses** at 50% and 95% per group, plus a mean marker | covariance → eigen decomposition → ellipse; `confidence` parameter; mean marker; legend showing the ellipse convention |
| 4 — Nicolas Cage clusters | **Smoothed blobs** hugging each cluster, with a curved coloured group title and no border | convex hull → padding → Chaikin/Catmull-Rom smoothing; label placed along the contour (`labelFollowsContour`) |
| 5 — Internet-search map | **Translucent rectangles** spanning regions, overlapping, each with a coloured title | value-space rects with alpha, per-region title, `zSlot` choice of wash-behind-grid or overlay |

```cpp
struct ChartHighlight {
    HighlightShape shape;                 // Rectangle|Ellipse|ConfidenceEllipse|Hull|Blob|ValueBand|PointHalo
    std::vector<size_t> memberIndices;    // computed shapes derive geometry from these records
    Rect2Dd rectValueSpace;               // explicit shapes
    double  confidence = 0.95, padding = 18.0, smoothing = 0.6;
    Color   fill, stroke; float strokeWidth = 0.0f; bool dashed = false;
    std::string label; Color labelColor; bool labelFollowsContour = false;
    int     zSlot = 200;                  // 200 = phase-1 wash behind the grid, 700 = phase-3 overlay
};
```

Also demanded by these images and folded into the Annotation layer: point
callouts with leader lines ("Richards", "The Rock"), and directional arrow
annotations with text ("WORSE ↑", "→ BETTER").

---

## 9. Migration strategy

Three tiers, so nothing breaks and the 42k lines are not rewritten at once:

* **Tier 0 — untouched.** Every existing chart keeps compiling and rendering.
  The current base-class virtuals stay, delegating to the engine internally.
* **Tier 1 — adapter (one line per chart).** A chart's existing `RenderChart()`
  is registered as a Series-layer contributor. It immediately gains the shared
  background, grid-follows-ticks, limiters, highlights, legend, label session and
  interaction without touching its drawing code.
* **Tier 2 — native.** The chart is re-expressed as layer contributors on the
  unified axis/projection model, and its private legend/axis/colour-bar/camera
  code is deleted.

Recommended Tier-2 order after the parallel coordinate chart, by duplication
removed: heatmap + hexbin + contour (one colour bar), radar + polar + pie +
sunburst + radial bar (one legend, one polar projection), line/bar/area/scatter/
financial (one axis renderer), scatter3D + contourSurface3D + GL (one camera).

---

## 10. Delivery phases

| Phase | Content |
|---|---|
| **P0** | Label engine upgrade (§7) + unit tests. Independently useful; deletes the §4 workarounds. |
| **P1** | Engine core: the three-phase render driver, `IChartContent` contract, frozen frame, slot ordering, dirty model, two-pass layout, cached label plan with the §5.7 invalidation rules and §5.8 per-type policies, `Vertical`/`Horizontal` projections, axis + scale + tick model, theme/palette, shared legend + colour bar, solid/gradient/image backgrounds (no video/GL/3D-scene — out of scope), interaction state. **Plus the parallel coordinate chart as the first native client**, implementing only phase 2 and exercising every phase-1/phase-3 slot including axis histograms (§8.1) and highlight regions (§8.2). |
| **P2** | Tier-1 adapter for all existing charts; Tier-2 migration of the colour-bar and legend families; `Polar` projection; `Space3D` projection with one camera; export to pixmap. |
| **P3** | Remaining Tier-2 migrations; density/binned series rendering; animated transitions between layouts; multi-chart linked selection. |

---

## 11. Decisions — all resolved

| # | Question | Decision |
|---|---|---|
| 1 | Polar as a fourth projection kind | **Yes** — `Polar` is in `IChartProjection` from the start, alongside `Vertical`, `Horizontal` and `Space3D` |
| 2 | Is the Tier-1 adapter an acceptable steady state? | **Yes** — most charts keep their own drawing code permanently; Tier 2 is reserved for the families in §9 |
| 3 | Naming canon breakage | **Keep deprecated aliases through the migration**, then remove them in one sweep afterwards |
| 4 | Background media | **Out of scope** — no video, GL/shader or 3D-scene backgrounds. The `Background` layer is solid colour, gradient and static image only |
| 5 | Label session ownership | **Persistent** — the session and its solved plan are built at chart creation and rebuilt only on the §5.7 invalidation list. The solver never runs on a redraw |
| 6 | Per-type label policies | **Accepted as proposed** in §5.8, with a review pass per chart type as each one migrates |
| 7 | Re-solve during zoom / pan / axis reorder | **On interaction end** — nothing during the drag; labels catch up on release |

---

## 12. Packaging: the engine and the charts as plugins

Requirement: **the chart engine loads only when charts are used, and then only
the chart types the application actually uses.**

### 12.1 Current state — charts are not plugins

They live under `Plugins/Charts/` but are not plugins in any sense:

* **44 chart `.cpp` files are listed in `ULTRACANVAS_CORE_SOURCES`** in
  `UltraCanvas/CMakeLists.txt` (lines ~348–456) — they are compiled
  unconditionally into the core library. There is no `ULTRACANVAS_PLUGIN_CHARTS`
  option, no separate target, no per-chart selection.
* A shared build (`ULTRACANVAS_BUILD_SHARED=ON`) therefore ships all 42,874
  lines of chart code to every application, including ones that never draw a
  chart. Every application also pays the compile time.
* In a static build the linker does already drop chart objects nothing
  references — each chart is an independent TU. **That accidental laziness is
  the thing a naive registry would destroy** (§12.5).

One piece of good news from the scan: **nothing in `core/`, the top-level
`include/*.h` or `OS/` references `Plugins/Charts`.** The coupling is
build-system only, so the split is a CMake change plus a registry — no core code
has to move.

### 12.2 Existing plugin precedents to follow, not reinvent

| Precedent | What it gives us |
|---|---|
| **UltraNet plug-ins** (`Plugins/UltraNet/*`, `core/UltraNet/UltraNetPlugins.cpp`) | The mature model: `add_library(... MODULE ...)` with `PREFIX ""`, output into `${CMAKE_BINARY_DIR}/Plugins/UltraNet`, an ABI-versioned `extern "C" UltraNet_PluginInit(host)` entry with a host vtable (`abiVersion`, `RegisterPlugin`), a POSIX v1 fallback, an idempotent directory scan keyed on canonical path, lookup by scheme, and configure-time self-disable when a backend is missing |
| **`UltraCanvasGraphicsPluginSystem.h`** | The in-process registry shape: `IGraphicsPlugin`, `RegisterPlugin` / `UnregisterPlugin` / `FindPluginForFile`, static registry with `Initialize()` / `Shutdown()` |
| **`UltraCanvasTemplate.h::RegisterElementFactory(elementType, …)`** | String-keyed element factories already exist for templates — chart type names should plug into the same idea |

The chart system should mirror the UltraNet ABI rather than invent a second one.

### 12.3 Three tiers, all supported by one registry

| Tier | Selection | Where it fits |
|---|---|---|
| **T1 — compile-time** | `ULTRACANVAS_CHARTS="Line;Bar;ParallelCoordinate"` at configure time; unselected charts are never compiled. **Default: empty** | WASM, ULTRA OS, embedded, and any app that wants the typed chart API linked in. Smallest binary, no loader, no ABI concerns |
| **T2 — link-time (default)** | Everything built, but each chart is an independent TU reached only through its own factory, so a static link pulls in exactly the charts referenced | Ordinary desktop static builds; preserves today's accidental laziness by design instead of by luck |
| **T3 — runtime modules** | One `uc_chart_<name>` MODULE per chart type, loaded on first use. **On by default** | The default path: the core ships chart-free and each chart arrives only when something asks for it by name |

T1 and T3 compose: an app can compile in the two charts it always shows and
load the rest on demand.

### 12.4 Lazy engine initialisation

The engine core is itself a module (`ultracanvas_chartengine`) and must not be
touched until a chart exists:

```cpp
// Nothing in UltraCanvas core calls this. The first chart construction does.
ChartEngine& UltraCanvasChartEngine::Instance();   // builds theme, palette,
                                                   // label service, registry
```

Because no core code references `Plugins/Charts` today (§12.1), "the engine is
not loaded unless a chart is used" is automatic in T1/T2 — the symbol is never
referenced, so the object is never pulled in. In T3 it means the engine module
is dlopen'd by the first `CreateChart(...)` call, not at application start.

### 12.5 The registry, and the static-initialiser trap

The obvious design — every chart TU registers itself with a file-scope static —
**must be avoided**. A self-registering static forces the linker to keep every
chart object in every binary (it has to run the initialiser), which is worse
than the status quo. Instead:

```cpp
// UltraCanvasChartRegistry.h  (engine core)
using ChartFactory = std::function<std::shared_ptr<UltraCanvasChartElementBase>(
        const std::string& id, int x, int y, int w, int h)>;

class UltraCanvasChartRegistry {
public:
    static void Register(const std::string& typeName, ChartFactory);
    static bool IsRegistered(const std::string& typeName);
    static std::vector<std::string> RegisteredTypes();

    // Registry miss → try to load uc_chart_<typeName> (T3) → retry once.
    static std::shared_ptr<UltraCanvasChartElementBase>
        Create(const std::string& typeName, const std::string& id,
               int x, int y, int w, int h);
};
```

Registration reaches it three ways, none of which defeats laziness:

1. **Direct construction (T2, the common case).** The app includes
   `UltraCanvasParallelCoordinateChart.h` and calls
   `CreateParallelCoordinateChartElement(...)`. No registry involved, no other
   chart linked. This stays the primary API — the registry is for *name-driven*
   creation, not a replacement for the factory helpers.
2. **A generated registration TU (T1).** CMake knows the selected chart list and
   generates `UltraCanvasChartRegistry.generated.cpp` containing exactly the
   selected `RegisterXxxChart()` calls. No `--whole-archive`, no self-registering
   statics, and name-based creation still works for the charts the app chose.
3. **Module init (T3).** The loader calls the module's entry point, which
   registers its chart types into the host registry.

### 12.6 Module ABI (mirrors UltraNet v2)

```cpp
// UltraCanvasChartPluginABI.h
struct UltraCanvasChartHost {
    uint32_t abiVersion;                       // ULTRACANVAS_CHART_ABI = 1
    void (*RegisterChart)(const char* typeName, ChartFactory);
    ChartEngine* engine;                       // shared services, one instance
};

extern "C" ULTRACANVAS_CHART_PLUGIN_EXPORT
void UltraCanvasChart_PluginInit(UltraCanvasChartHost* host);
```

Loader behaviour, copied from `UltraNet_RefreshPlugins()`: scan
`${binary}/Plugins/Charts`, skip non-module files, track canonical paths so
repeated calls are idempotent, reject mismatched `abiVersion`, and resolve by
**chart type name** the way UltraNet resolves by scheme. Loading is
**on-demand** (registry miss) rather than an eager scan, with an explicit
`RefreshChartPlugins()` for hosts that want a gallery of what is available.

The C++-types-across-the-boundary constraint (`std::shared_ptr`,
`std::function`, `std::string` in the ABI) is the same one UltraNet already
accepts: plug-ins must be built with the same compiler and standard library as
the host. That is an existing, documented constraint in this codebase, not a new
one — but it is the reason T1/T2 stay first-class rather than being deprecated
in favour of T3.

### 12.7 Granularity and shared support code

Some charts share support TUs — `UltraCanvasColormap`, `UltraCanvasContourGrid`
+ `UltraCanvasMarchingSquares`, `UltraCanvasHexLayout`, `UltraCanvasTimeAxis`,
`UltraCanvasCalendarDate`, `UltraCanvasConnectionRenderer`, `UltraCanvasSTFT`
(+ vendored KissFFT). Rule: **shared support code lives in the engine core
module; only chart-specific code lives in a chart module.**

That is not the whole story, and an earlier draft of this section was wrong to
claim a chart module never depends on another. **Five chart classes inherit from
another chart class**, not from the base:

| Derived chart | Base chart |
|---|---|
| `UltraCanvasHexbinChartElement` | `UltraCanvasHeatmapChartElement` |
| `UltraCanvasContourChartElement` | `UltraCanvasHeatmapChartElement` |
| `UltraCanvasCalendarHeatmapElement` | `UltraCanvasHeatmapChartElement` |
| `UltraCanvasSpectrogramElement` | `UltraCanvasHeatmapChartElement` |
| `UltraCanvasContourSurfaceGLElement` | `UltraCanvasContourSurface3DElement` |

Per-chart modules therefore need a **dependency closure**: `uc_chart_hexbin`
links against `uc_chart_heatmap`, and the dynamic linker resolves it
automatically (`DT_NEEDED` on ELF/Mach-O, an import library on Windows) — the
loader needs no dependency logic of its own. Loading hexbin loads heatmap,
which is correct, because a hexbin chart *is* a heatmap. The alternative —
promoting `UltraCanvasHeatmapChartElement` into the engine core — is rejected:
the heatmap is a chart in its own right and would then be paid for by every
application, including ones that never draw one.

Packaging granularity:

* **Compile-time selection is per chart** — the user requirement, met exactly.
* **Runtime module packaging is also per chart** (decided): one
  `uc_chart_<name>` per chart type, so an application loads exactly the chart it
  asked for and nothing else. `ULTRACANVAS_CHART_MODULE_GRANULARITY=family`
  remains available for deployments that would rather ship a handful of grouped
  modules than ~43 files.

### 12.8 Build layout

```
UltraCanvas/Plugins/Charts/CMakeLists.txt          # new: owns all chart targets
  → UltraCanvasChartEngine        (STATIC or SHARED)   engine core + support code
  → uc_chart_<family|name>        (MODULE, T3)         one per selected module
  → UltraCanvasCharts             (STATIC, T1/T2)      selected charts, no self-registration
ULTRACANVAS_CORE_SOURCES                            # chart entries removed
```

New options, following the existing `ULTRACANVAS_PLUGIN_*` naming:

```cmake
option(ULTRACANVAS_ENABLE_CHARTS "Build the chart engine and chart types" ON)
# Charts linked into the application (typed API available at compile time).
# Empty by default: nothing is compiled in, everything arrives as a module.
set(ULTRACANVAS_CHARTS "" CACHE STRING "Charts to link in: empty, all, or a ;-list")
set(ULTRACANVAS_CHART_MODULE_GRANULARITY "chart" CACHE STRING "chart|family")
option(ULTRACANVAS_CHART_RUNTIME_MODULES "Build charts as loadable modules (T3)" ON)
```

With `ULTRACANVAS_ENABLE_CHARTS=OFF` the core library contains no chart code at
all — which is not possible today.

### 12.9 Effect on the delivery phases

* **P1** additionally: move the chart sources out of `ULTRACANVAS_CORE_SOURCES`
  into `Plugins/Charts/CMakeLists.txt`, add the engine target, the registry, the
  generated registration TU and the `ULTRACANVAS_CHARTS` selection. The parallel
  coordinate chart ships as the first chart built through this path.
* **P2** additionally: the T3 module ABI, the on-demand loader, family packaging
  and the install rules.
* Migration of existing charts (§9) is unaffected — a Tier-1 adapter chart is
  just as selectable as a native one.

### 12.10 Packaging decisions — all resolved

| # | Question | Decision |
|---|---|---|
| 1 | Default for `ULTRACANVAS_CHARTS` | **Empty — no charts compiled in.** The core ships chart-free; charts arrive as modules |
| 2 | Module granularity default | **Per chart** — one `uc_chart_<name>` per chart type |
| 3 | Runtime modules on by default | **Yes** — T3 is the default delivery path |
| 4 | `CreateChart("ParallelCoordinate", …)` public for all apps | **Yes** — name-based creation is public API, not just for templates/markup |

### 12.11 What those decisions imply

The default build is now: **core with zero chart code, an engine module, and
~43 individual chart modules, none of which is loaded until something asks for
it by name.** Four consequences follow, and each needs handling in P1/P2.

**1. The engine must be a shared library when modules are on.** Every chart
module and the host have to see *one* registry, one label service, one theme —
a static engine would give each module its own copy of those statics. So
`ULTRACANVAS_CHART_RUNTIME_MODULES=ON` forces `UltraCanvasChartEngine` to build
`SHARED`, and each `uc_chart_<name>` links against it.

**2. Typed chart APIs need a rule.** `CreateChart(...)` returns
`std::shared_ptr<UltraCanvasChartElementBase>`, which cannot reach
`SetKDEBandwidth()` or `SetLevelStep()`. Three ways to configure a chart, and
the rule for choosing:

| Path | How the app configures it | Requires |
|---|---|---|
| Typed factory (`CreateParallelCoordinateChartElement`) | Directly, full typed API | Chart listed in `ULTRACANVAS_CHARTS` |
| `CreateChart("…")` + `std::dynamic_pointer_cast<T>` | Full typed API after the cast | Chart header included; module and host built with the same compiler and standard library (POSIX `RTLD_GLOBAL`; Windows needs exported RTTI) |
| `CreateChart("…")` + named properties | `SetProperty("levelStep", 0.02)` on the base class | Nothing — works across any module boundary |

Recommendation: implement all three, with the base class carrying a small
`SetProperty` / `GetProperty` surface that each chart registers its own keys
into. It is what makes name-based creation genuinely useful for
document-driven and gallery-style applications, and it is the only path that
does not inherit the ABI constraint.

**3. This repository's own applications must declare their chart sets.**
DemoApp includes chart headers directly and calls typed factories for roughly
25 chart types; with an empty default it would no longer link. Its CMake must
set `ULTRACANVAS_CHARTS` to the list it uses (or migrate those screens to
`CreateChart` + properties). Same check for Texter, the UltraAI dashboard and
the file manager. This is a P1 task, not a follow-up.

**4. Platforms without `dlopen` fall back to T1.** WASM and any ULTRA OS
configuration without dynamic loading must force
`ULTRACANVAS_CHART_RUNTIME_MODULES=OFF` at configure time and require a
non-empty `ULTRACANVAS_CHARTS` — otherwise the build silently produces an
application that can draw no charts at all. The CMake must emit a hard error in
that combination rather than a working-but-empty build.

Deployment: chart modules install next to the application as
`Plugins/Charts/uc_chart_<name>.<so|dll|dylib>`. A missing module makes
`CreateChart` return `nullptr`; `AvailableChartTypes()` and
`RefreshChartPlugins()` let a host enumerate what is actually installed for a
chart gallery.

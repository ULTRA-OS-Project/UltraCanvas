# UltraCanvasImportancePerformanceChart — Research & Feature Proposal

**Status:** Proposal — nothing is built yet. This chart takes over reference
image 4 of
[`UltraCanvasPerformanceMatrixProposal.md`](UltraCanvasPerformanceMatrixProposal.md),
which ceded it in that document's revision 2: the importance–performance
matrix comes from operations strategy, not talent management, and it is a
scatter plot over zones, not a grid of categories.

**Author:** UltraCanvas Framework
**Last Modified:** 2026-10-24
**Related:** `UltraCanvasEngineScatterChart` (implemented: points, groups,
point labels, tooltips and highlights on the chart engine, and the base this
chart builds on), `UltraCanvasChartEngineElement` and
`Engine/UltraCanvasChartHighlights.h` (implemented: the highlight layer that
gains one shape here), `UltraCanvasQuadrantChart` (implemented: a 2×2 plane
split by two straight lines, which already covers the classic four-quadrant
form, §4.1), `UltraCanvasContourChart` (implemented: regions from a scalar
field, §4.3).

---

## 1. Summary — the headline question

**Does any chart we have draw reference image 4?** No. The closest are:

| Candidate | What it already does | What image 4 needs that it lacks |
|---|---|---|
| `UltraCanvasQuadrantChart` | Points over four regions split by a vertical and a horizontal line through a movable centre | Regions bounded by curves or diagonal steps, more than four of them, a labelled boundary band, named level ranges on the axes |
| `UltraCanvasEngineScatterChart` | Points in groups, point labels through the solved label plan, tooltips, legends, and highlights: rectangles, ellipses, value bands, hulls and blobs | An *explicit* polygon region (a hull or blob is computed from points; it cannot be drawn to a given outline), icons in regions, classifying points by region, named level ranges on the axes |
| `UltraCanvasContourChart` | Filled regions where a scalar field crosses thresholds | The zones are authored categories, not iso-lines of a measured field |

Recommendation: **a new chart type, `UltraCanvasImportancePerformanceChart`,
derived from `UltraCanvasEngineScatterChart`**, plus **one general addition
to the chart engine**, an explicit `ChartHighlightShape::Polygon`. The
scatter chart supplies everything about the points. The new class adds:

* zones (outline, fill, title, icon, action) drawn as polygon highlights
  under the grid;
* a labelled boundary band (Slack's "lower bound of acceptability");
* named level ranges on both axes ("Less important / Qualifying / Order
  winning") with end captions ("Low", "High", "Bad", "Good");
* classification of every factor into its zone, using the geometry the
  drawing uses;
* presets for the published forms: Slack's four zones, stepped or smooth,
  Martilla & James's four quadrants, and a diagonal iso-priority line.

Three phases (§10). Phase 1 draws reference image 4.

---

## 2. What an importance–performance chart is

An importance–performance chart ranks the *factors* of a product or service
(price, delivery speed, quality, after-sales support…) to decide which to
improve first. Each factor gets two scores: how much customers care about it,
and how well the business performs on it. The factor is plotted as a point,
and the region it falls in names the action.

Two published forms matter:

* **Martilla & James (1977)**, the original importance–performance analysis
  from marketing. Four quadrants are split at the mean (or the scale
  midpoint) of each axis: *Concentrate here* (important, performing poorly),
  *Keep up the good work*, *Low priority* and *Possible overkill*
  (unimportant, performing well).
* **Slack (1994)**, from operations strategy. Slack replaced the quadrants with
  zones cut by diagonal boundaries, because a factor just across a quadrant
  line is barely different from one just inside it. In the textbook form
  both axes are nine-point scales. Importance is grouped as *order-winning*,
  *qualifying* and *less important*; performance against competitors is
  grouped as *better than*, *same as* and *worse than*. The zones are *Urgent
  action*, *Improve*, *Appropriate* and *Excess?*, and the boundary between
  "improve" and "appropriate" is called the *lower bound of acceptability*.
  The textbook draws that boundary as a staircase on the 9×9 grid. Slides
  such as reference image 4 draw it as a smooth curve.

What sets this apart from a plain scatter plot:

1. **The regions are the message.** The point positions are inputs, and the
   answer is the list of factors in *Urgent action*.
2. **The regions are authored, not computed.** They come from a method
   (Slack, Martilla & James), not from the data.
3. **The axes are ordinal scales read in named ranges.** "Order winning" is a
   range of the importance scale, not a tick.

---

## 3. What reference image 4 demands

The plane has no grid lines. Four filled zones, each with a title and an icon:
*Excess* (top left), *Appropriate* (most of the upper area), *Improve*
(bottom left) and *Urgent Action* (bottom right). A blue band runs diagonally
between them with the text *Lower Bound of Acceptability* along it. The Y axis
is titled *Performance / Digital Competition*, has the level ranges *Worse
Than / Same As / Better Than* marked with arrows, and the end captions *Bad*
and *Good*. The X axis is titled *Importance for Customer*, with *Less
Important / Qualifying / Order Winning* and *Low / High*. The slide shows no
factors, but in use the factors are plotted as points.

| Demand | Feature |
|---|---|
| Filled zones with curved outlines | **Z1**, **E1** (engine polygon) |
| Zone title and icon | **Z2** |
| The diagonal band with text along it | **Z3** |
| Level ranges with arrows on both axes | **A1** |
| End captions and multi-line axis titles | **A2** |
| Factors as points, in zones | **F1**, **F2** |

---

## 4. Where this belongs in the repository

### 4.1 The quadrant chart: right for Martilla & James, wrong for Slack

The classic four-quadrant analysis is already possible:
`QuadrantType::Custom`, a `QuadrantDefinition` with the four action names, and
`QuadrantAxisConfiguration::xCenter` / `yCenter` set to the means. The guide
(§4.4) should say so, so that nobody builds this chart only for quadrants.

Slack's form, and image 4, cannot be added to it without rewriting its
layout. `QuadrantLayout`, the centre lines and `QuadrantPosition` all assume
exactly two straight dividers. The quadrant chart is also a legacy chart,
not on the engine, so it has no label solver, no highlight layer and no
engine legend, and growing it would duplicate them. The new chart still
offers a Martilla & James preset (**P2**), so that one element covers every
form of the method and a user can switch between forms on the same data.

### 4.2 The engine scatter chart: the base, not the answer

The scatter chart has every point feature the method uses: groups with
colours and markers, labels placed by the solved label plan, tooltips,
legends, and even trend lines if someone wants them. It lacks every *region*
feature. Deriving from it keeps all of the point features and adds only the
regions. Its engine-contract overrides (`DescribeAxes`, `RenderChartContent`,
`CollectChartLabels`) are public and virtual, which is all a subclass needs:

* `DescribeAxes`: call the base, then set `tickValues` and a `formatter` for
  the named level ranges.
* `RenderChartContent`: draw the zone icons, then call the base, so the
  points stay on top.
* `CollectChartLabels`: call the base, then add the band label's box as an
  obstacle (§7.3).
* Zones go through the public `AddHighlight`, at slot 200 (under the grid
  and the points).

### 4.3 Why not the contour chart

The zones could be faked as iso-bands of an invented "priority" field over the
plane. That would make the outlines a side effect of a formula nobody
publishes, and classification would depend on contour tracing. The zones are
data; they are given as data.

### 4.4 Registration checklist (what "done" touches)

1. `UltraCanvas/include/Plugins/Charts/Engine/UltraCanvasChartHighlights.h`:
   `ChartHighlightShape::Polygon` (§5.1), and its drawing in
   `UltraCanvasChartEngineElement`. Update `Docs/UltraCanvas/UltraCanvasChartEngine.md`
   (*Highlights*: "eight shapes") and add a `Tests/ChartEngineTest` case.
2. `UltraCanvas/include/Plugins/Charts/Engine/Charts/UltraCanvasImportancePerformanceChart.h`
   and `UltraCanvas/Plugins/Charts/Engine/Charts/UltraCanvasImportancePerformanceChart.cpp`.
   Add the source to `UltraCanvas/CMakeLists.txt` next to the other engine
   charts.
3. `Apps/DemoApp/UltraCanvasImportancePerformanceChartExamples.cpp`, declared
   in `Apps/DemoApp/UltraCanvasDemo.h` and registered in the Charts section
   with one `AddVariant` per preset (Slack smooth, Slack stepped, Martilla &
   James, diagonal). Add the source to the demo list in the root
   `CMakeLists.txt`.
4. `Docs/UltraCanvas/UltraCanvasImportancePerformanceChart.md`, the guide, with
   a generated `## Contents`. Cross-reference it from
   `UltraCanvasQuadrantChartExamples.md` ("for zones that are not quadrants")
   and from `UltraCanvasPerformanceMatrix.md` once that exists ("not a
   talent matrix"), so the boundary of §4.1 and the performance matrix
   proposal's revision 2 is stated in all three docs.
5. `Tests/ImportancePerformanceModelTest.cpp` and its entry in
   `Tests/CMakeLists.txt`, covering classification (including points on a
   boundary), the presets' validity and the CSV and JSON round trips.
6. A `Docs/UltraCanvas/changelog.d/` entry. Run
   `python3 scripts/generate_doc_toc.py`, `check_doc_examples.py` on the guide,
   `check_ui_reuse.py` and `check_callback_cycles.py`. Then remove this
   proposal's entries from `scripts/doc_examples_baseline.txt`
   (`--all --update-baseline`) as each API lands.

A chart is a self-rendered view (AGENTS.md), so drawing zones, icons and
captions is within that exception. `UltraCanvasUIElements.md` needs no row.

---

## 5. Data model sketch

Everything in this section is a sketch of an API that does not exist yet.
`check_doc_examples.py` records its findings in the baseline until it does.

### 5.1 The engine addition

One new enumerator, with no new fields. An explicit polygon reuses `members`
as its outline (in value space, like every other shape) and `smoothing` as
the Chaikin amount that `Blob` already uses:

```cpp
enum class ChartHighlightShape {
    Rectangle, Ellipse, ConfidenceEllipse, Hull, Blob, ValueBand, PointHalo,
    Polygon     // explicit: `members` is the closed outline, in order;
                // smoothing > 0 rounds it with SmoothPolygonChaikin
};
```

Any engine chart gains it: a target region on a scatter plot, an acceptance
window on a line chart, a hand-drawn cluster outline. Its label rides the
label plan as `HighlightLabel`, like the other shapes'.

### 5.2 The chart

```cpp
namespace UltraCanvas {

    // A named range of one axis: "Order winning" covers 6.5 .. 9.5.
    struct ImportancePerformanceLevel {
        std::string label;
        double from = 0.0;
        double to   = 0.0;
    };

    struct ImportancePerformanceAxis {
        std::string title;                     // may hold "\n" for two lines
        std::string lowCaption;                // "Low", "Bad"
        std::string highCaption;               // "High", "Good"
        std::vector<ImportancePerformanceLevel> levels;
        double min = 0.5;
        double max = 9.5;
        bool   inverted = false;               // Slack's scales: 1 = best, drawn at the far end
    };

    struct ImportancePerformanceZone {
        std::string id;                        // "urgent"
        std::string title;                     // "Urgent Action"
        std::string action;                    // tooltip and legend: "Improve before anything else"
        std::string iconPath;
        Color fill = Color(0, 0, 0, 0);        // transparent = from the theme palette
        std::vector<Point2Dd> outline;         // closed, value space (importance, performance)
        int smoothing = 0;                     // Chaikin iterations: 0 = stepped, 3-4 = smooth
    };

    // A labelled boundary drawn over the zones.
    struct ImportancePerformanceBand {
        std::string label;                     // "Lower Bound of Acceptability"
        std::vector<Point2Dd> line;            // open polyline, value space
        int smoothing = 0;
        double width = 0.35;                   // value units, measured on the Y axis
        Color fill = Color(60, 110, 200, 220);
        Color textColor = Color(255, 255, 255, 255);
    };

    struct ImportancePerformanceFactor {
        std::string id;
        std::string label;                     // "Delivery speed"
        std::string group;                     // "Logistics": becomes a scatter group
        double importance  = 0.0;
        double performance = 0.0;
        // Optional earlier position: drawn as an arrow to the current one (F4).
        double previousImportance  = std::numeric_limits<double>::quiet_NaN();
        double previousPerformance = std::numeric_limits<double>::quiet_NaN();
        std::string note;
    };

    struct ImportancePerformanceModel {
        std::string title;
        ImportancePerformanceAxis importance;      // X
        ImportancePerformanceAxis performance;     // Y
        std::vector<ImportancePerformanceZone> zones;   // later zones cover earlier ones
        std::vector<ImportancePerformanceBand> bands;
        std::vector<ImportancePerformanceFactor> factors;

        // Index of the zone the point is in, -1 = none. Uses the smoothed
        // outline, exactly as drawn (§7.1). The last zone in order wins.
        int ZoneOf(double importance, double performance) const;
        std::vector<int> ClassifyFactors() const;    // one zone index per factor
        std::vector<std::string> Validate() const;   // problems; empty = valid
    };

    namespace ImportancePerformanceSamples {
        ImportancePerformanceModel SlackZones(bool smooth);   // image 4 when smooth
        ImportancePerformanceModel MartillaJames(double importanceSplit, double performanceSplit);
        ImportancePerformanceModel DiagonalIsoPriority();
        ImportancePerformanceModel SampleFactors();           // invented data for the demo
    }

    // Strings only; files go through UltraCanvasFileLoader.
    std::string ExportImportancePerformanceJson(const ImportancePerformanceModel& model, bool pretty = true);
    bool        ImportImportancePerformanceJson(const std::string& text, ImportancePerformanceModel& out,
                                                std::string* error = nullptr);
    // "id,label,group,importance,performance[,previousImportance,previousPerformance]".
    // Replaces model.factors only; on error returns false and leaves `model` untouched.
    bool        ImportImportancePerformanceFactorsCsv(const std::string& csv,
                                                      ImportancePerformanceModel& model,
                                                      std::string* error = nullptr);

    class UltraCanvasImportancePerformanceChart : public UltraCanvasEngineScatterChart {
    public:
        UltraCanvasImportancePerformanceChart(const std::string& id, int x, int y, int w, int h);

        void SetModel(const ImportancePerformanceModel& model);
        const ImportancePerformanceModel& GetModel() const;

        void SetShowZoneTitles(bool show);
        void SetShowZoneIcons(bool show);
        void SetShowLevelRanges(bool show);       // A1
        void SetShowMovement(bool show);          // F4
        void SetZoneLegend(bool show);            // legend of zones and their actions
        void SetHighlightZone(int zoneIndex);     // dims the other zones' factors; -1 = none

        std::function<void(int zoneIndex)> onZoneClick;
        std::function<void(const ImportancePerformanceFactor&, int zoneIndex)> onFactorClick;

        // Engine contract: each calls the scatter chart's, then adds its own.
        void DescribeAxes(ChartAxisSet& axes) override;
        void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
        void CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) override;
    };

} // namespace UltraCanvas
```

Notes on the shape of the model:

* **The zones live in value space**, on the same axes as the factors. One
  outline therefore serves a 1–9 scale and a 0–100 % scale; only the preset
  differs.
* **The model is plain data.** Zones are outlines, not formulas, so any
  published variant (and a consultancy's own) is a JSON file away.
* **Groups come from factors.** `SetModel` turns the distinct `group` values
  into `EngineScatterGroup`s, so legend, colours and markers are the
  scatter chart's own.

---

## 6. Feature list

### Zones (Z)

| Id | Feature |
|---|---|
| Z1 | Filled zones from outlines in value space, stepped or smoothed, clipped to the plot |
| Z2 | Zone title and icon, placed at the zone's pole of inaccessibility (the interior point farthest from its edges), not its centroid, which can fall outside a concave zone |
| Z3 | Labelled bands along an open polyline, text rotated to the band (§7.3) |
| Z4 | Zone legend: swatch, title and action |
| Z5 | Hover a zone: its factors are emphasised, the rest dimmed; `onZoneClick` |

### Axes (A)

| Id | Feature |
|---|---|
| A1 | Named level ranges under each axis, separated by arrows (image 4), from `levels` |
| A2 | End captions at the axis ends and multi-line axis titles |
| A3 | Inverted scales (Slack's "1 = best"), with the zones following the inversion automatically |

### Factors (F)

| Id | Feature |
|---|---|
| F1 | Factors as scatter points, grouped and coloured by `group` |
| F2 | Classification into zones; the zone name in each factor's tooltip |
| F3 | A list of factors per zone (`ClassifyFactors`), e.g. for a side table of "urgent action" items |
| F4 | Movement arrows from an earlier position to the current one, so last year's survey and this year's can be compared |
| F5 | Point labels through the scatter chart's label modes |

### Presets (P)

| Id | Feature |
|---|---|
| P1 | `SlackZones(smooth)`: four zones and the lower bound of acceptability, on nine-point scales |
| P2 | `MartillaJames(split, split)`: four quadrants split at given values (usually the means) |
| P3 | `DiagonalIsoPriority()`: two regions cut by the line where importance equals performance |

### Import / export (X)

| Id | Feature |
|---|---|
| X1 | JSON round trip of the whole model |
| X2 | CSV import of the factors |
| X3 | Image and vector export through the engine and `UltraCanvasFileLoader`, like every other chart |

---

## 7. The hard parts

### 7.1 Classification must match the drawing

If `ZoneOf` used the raw outline while the renderer drew the smoothed one, a
factor drawn inside *Improve* could be classified as *Urgent action*. That is
the one error this chart must never make, because the classification is its
answer. The plan:

* The model smooths each outline with the engine's `SmoothPolygonChaikin`,
  in value space, and classifies against the result with an even-odd test.
* The renderer maps that *same* smoothed polygon to the screen and draws it.
  It does not smooth again in screen space.

Chaikin corner cutting is affine-invariant, so for linear and inverted axes
smoothing before or after the mapping gives the same shape anyway. A log
axis would break that, but the method's scales are never logarithmic, so
`Validate()` rejects zones on a log axis instead of trying to support them.

A factor exactly on a shared edge is classified by zone order (the last zone
wins, as in the drawing). The test (§4.4 item 5) pins this case.

### 7.2 Stepped and smooth are one outline

The textbook staircase and the slide's curve (image 4) are the same zone at
different `smoothing` values. Presets therefore store the staircase vertices
only, and `SlackZones(true)` just sets the smoothing. Classification
(§7.1) stays correct in both cases because it follows the smoothing.

### 7.3 Text along a band

The band label in image 4 follows the band's direction. The engine's label
plan places horizontal labels. The chart draws the band label itself,
rotated to the direction of the band's longest near-straight run, centred on
it, and skipped when the run is shorter than the text. In `CollectChartLabels` it registers the
label's rotated box as an obstacle so that point labels avoid it, the way
`ChartSpanAnnotation` registers its caption box.

### 7.4 Many factors

A real survey has 15–40 factors, and they crowd the middle of the scale. Point
labels go through the scatter chart's label modes (`EngineScatterLabelMode`),
which already declutter. The per-zone list (F3) is the readable form, and the
demo shows it beside the chart.

### 7.5 Colour

Zone colours follow the theme palette by default. Every zone has a title on
the chart and in the legend, so colour is never the only way to tell zones
apart.

---

## 8. What already exists to build on

| Need | Reuse |
|---|---|
| Points, groups, markers, labels, tooltips, legend | `UltraCanvasEngineScatterChart` |
| Region fill under the grid, clipped to the plot | The engine's highlight layer (slot 200), plus §5.1 |
| Smoothing | `SmoothPolygonChaikin` (`UltraCanvasChartHighlights.h`) |
| Named ticks on an axis | `ChartAxis::tickValues` and `ChartAxis::formatter` |
| Inverted axes | `ChartAxis::inverted` |
| Hit-testing zones | `AddHitRegion(polygon, …)` on the engine element |
| Arrows for F4 | The span-annotation arrowheads (`ChartSpanAnnotation`). F4 needs a one-ended arrow, which is either a flag there or a few lines in this chart (§9 Q3) |
| Icons | `IRenderContext::DrawImage(path, rect, ImageFitMode::Contain)` and the `iconPath` field pattern |
| JSON and CSV | `UltraCanvasJSON.h`; numbers through `TryParseFloat` / `FormatFloatClassic` (AGENTS.md rule 2), with `ImportMatrixTable`'s error contract |
| Files | `UltraCanvasFileLoader`: the model functions take and return strings |

---

## 9. Open questions (with recommendations)

**Q1. Should the class be called `UltraCanvasEngineImportancePerformanceChart`?**
Every class in `Engine/Charts/` has the `Engine` prefix, but that prefix marks
a *port* that sits next to the legacy chart it replaces. This chart has no
legacy twin. **Recommendation: no prefix.** Put the header in `Engine/Charts/`,
so the directory still says what it is built on.

**Q2. Should the Martilla & James preset exist, given the quadrant chart?**
**Recommendation: yes.** One chart that covers every form of the method lets
an analyst switch forms on the same factors, which is a real comparison
people make. The quadrant chart's doc still points out that it handles the
four-quadrant form on its own.

**Q3. Should the engine get one-ended arrows, or should the chart draw its
own?** **Recommendation: a `ChartSpanAnnotation` flag** (`arrowAtFrom`,
default true), because a movement arrow is as general as the double-ended
span. Decide this in Phase 3, when F4 is built.

**Q4. Should factors be draggable (a what-if: "if we improved delivery to
*same as*")?** **Recommendation: not now.** The scatter chart has no point
dragging, and adding it belongs to the scatter chart first.

---

## 10. Suggested phasing

| Phase | Delivers | Retires |
|---|---|---|
| **1** | E1 `ChartHighlightShape::Polygon` with test and engine doc; the model, `ZoneOf`, `Validate`; Z1, Z2, Z3, A1, A2, A3; preset `SlackZones` (both forms); `SampleFactors`; F1, F2, F5; demo page and guide | Reference image 4 |
| **2** | Z4, Z5, F3; presets `MartillaJames` and `DiagonalIsoPriority`; X1, X2 with demo Open and Save through `UltraCanvasFileLoader` | The analyst's workflow: load a survey, read the urgent list |
| **3** | F4 movement arrows (and Q3's engine flag) | Year-on-year comparison |

Phase 1 needs only the polygon highlight from the engine. Everything else is
in the new class, so the engine change is small and can be reviewed on its
own.

# UltraCanvasTernaryChart — Research & Feature Proposal

**Status:** **Phase 1 is implemented** as `UltraCanvasTernaryChart` and
`UltraCanvasTernaryModel` - see [`UltraCanvasTernaryChart.md`](UltraCanvasTernaryChart.md)
for the guide. Phase 1 delivered `ChartProjectionKind::Ternary` and
`ChartTernaryProjection` (A1) with their `ChartEngineTest` case, the model
with normalisation, `Ticks` and `Validate`, A2-A5, S1-S3, I1, the
`ThreePartyVotes` and `SoilSamples` samples, the demo page
(**Charts → Ternary Chart**) in both reading directions,
`Tests/TernaryModelTest.cpp` and `Tests/TernaryChartRenderTest.cpp`.

How Phase 1 departs from the plan below, and why:

* **A point has no `group` and no `size`.** The series are the groups: each
  has a name, a colour, a marker and a legend entry, so a group field inside
  it would be a second way to say the same thing. Bubble size is S5, Phase 2.
* **No `TernarySeriesKind` and no axis `minimum` yet.** Paths and bubbles
  are Phase 2 and the sub-triangle zoom Phase 3; a field nothing draws would
  only be a promise in the API.
* **Tick numbers stay level.** They sit at the end of each tick, along the
  grid line the tick belongs to, so the direction is already shown;
  tilted text would only be harder to read. The edge titles are tilted.
* **Both corner titles and edge titles.** The corner says what the corner is
  all of; the edge title, with its arrow, says which way that share grows.
  `SetShowEdgeTitles(false)` keeps just the corners.
* **The engine draws no ternary furniture.** A triangle's three edge axes
  are neither sides nor roles, so the chart draws its own. The engine does
  fill the triangle as the plot area. The chart describes two hidden axes
  over the shares of B and C, so highlights map through share space.
* **Point labels are kept clear of the furniture and near their points.**
  The tick numbers and titles enter the label plan as keep-out rectangles.
  A label the solver could only park far from its point (40 px or more) is
  left to the tooltip.
* **The model has a `subtitle`,** drawn under the title, as the other
  charts' models do.

A ternary plot draws things made of three parts, such as soil (sand, silt,
clay), an alloy, a three-party vote or a day split into work, sleep and the
rest. Each point is a mix whose three shares add up to 100 %. The plot puts
it inside an equilateral triangle: each corner is 100 % of one part, and
the opposite edge is 0 % of it. UltraCanvas has no chart that does this. This
proposal adds one on the chart engine. Its only addition to the engine is a
third flat projection, next to vertical, horizontal and polar.

**Author:** UltraCanvas Framework

---

## 1. Summary — the headline question

**Can any chart we have draw a ternary plot?** No. The closest are:

| Candidate | What it already does | What a ternary plot needs that it lacks |
|---|---|---|
| `UltraCanvasEngineScatterChart` | Points in groups, labels through the label plan, tooltips, a legend, highlights (rectangles, ellipses, hulls, blobs, polygons) | Three coordinates tied by a sum; a triangular plot area; three axes along the edges, with grid lines parallel to the edges |
| `UltraCanvasEnginePolarChart` | A projection that is not a rectangle, with its own grid (rings and spokes) and rim labels | Two coordinates only; its grid is circles, not lines parallel to three edges |
| `UltraCanvasHexbinChartElement`, `UltraCanvasContourChart` | Density by binning, and iso-lines from a scalar field | Both work on a rectangle; they need a triangular mesh |

Recommendation: **a new chart type, `UltraCanvasTernaryChart`, derived from
`UltraCanvasChartEngineElement`**, plus **one addition to the engine:
`ChartProjectionKind::Ternary`**. The projection maps the engine's normalised
`(u, v)` to a point in the triangle. The map is *affine*, so every straight
line stays straight. Markers, lines, polygon highlights, hit regions and the
label plan therefore work through it unchanged. The new chart adds:

* three edge axes with nice ticks, in a clockwise or anticlockwise reading
  direction, and grid lines parallel to the edges;
* points normalised from raw parts (counts, grams, votes), with the raw
  values kept for the tooltip;
* authored regions in composition space, such as the twelve soil texture
  classes, and the classification of every point into its region;
* paths through the triangle (a composition changing over time), bubble
  size, and a reading guide that shows the pointer's three shares;
* zoom to a sub-triangle, and density by triangular binning.

Three phases (§10). Phase 1 draws a labelled scatter on three correct axes.

---

## 2. What a ternary plot is

A composition `(a, b, c)` with `a, b, c ≥ 0` has only two degrees of freedom
once it is scaled to `a + b + c = 1`. The triangle uses exactly those two.
With corners `A`, `B` and `C` on screen, the point is

```
P = a·A + b·B + c·C          (a + b + c = 1)
```

the *barycentric* form. Its consequences are what a reader relies on:

1. **A corner is a pure part**, and the edge opposite it has none of that part.
2. **The lines of equal `a` run parallel to the edge `BC`.** Reading a
   share means following the grid line parallel to the opposite edge to the
   axis, not dropping a perpendicular.
3. **Every axis runs along an edge,** from 0 at one corner to 100 at the
   next. Which way round they run is a convention (§7.1). It is the classic
   source of misreadings.
4. **A straight line in the triangle is a straight line in composition.**
   Mixing two samples in any ratio lands on the segment between them. That
   is why phase diagrams and mixing models are drawn this way.

Where it is used, and what each use asks of the chart:

| Field | Typical diagram | Needs |
|---|---|---|
| Soil science | USDA soil texture triangle: sand / silt / clay, 12 named classes | Authored regions, classification, region labels |
| Geology | QFL / QAPF provenance and rock classification | Regions; QAPF is two triangles sharing an edge (§9 Q5) |
| Materials, chemistry | Gibbs triangle for three-component phase diagrams | Regions, tie lines, iso-lines |
| Population genetics | De Finetti diagram: genotype shares AA / Aa / aa | A reference curve: Hardy–Weinberg, `Aa² = 4·AA·aa` |
| Elections, sport | Three-party vote shares; win / draw / loss | Many points, groups, bubble size, paths over time |
| Time use, budgets | Three-way splits of a whole | Paths, labels |

---

## 3. Reference shapes

There are no reference images with this request. These are the four
pictures the chart must be able to draw, in order of priority:

1. **A labelled scatter.** About 30–300 points in a few groups, corner
   titles, ticks every 10 % on all three edges, and a grid.
2. **A classification diagram.** The soil texture triangle: twelve filled,
   labelled regions with sample points over them. Every point is reported
   with its class.
3. **Trajectories.** One country's vote shares across elections, as a line
   with arrowheads and year labels.
4. **Density.** Thousands of compositions, binned into small triangles and
   coloured by count, with the grid on top.

---

## 4. Where this belongs in the repository

### 4.1 An engine projection, not chart-local maths

Polar charts went through the same choice, and `UltraCanvasChartProjection.h`
records the outcome: series code is written once against normalised `(u, v)`
and a projection maps it to the screen. A ternary projection fits that model
exactly. Let `u` be the share of `B` and `v` the share of `C`; then `a = 1 −
u − v`, and

```
ToScreen(u, v)  = A + u·(B − A) + v·(C − A)
ToNormalized(P) = solve the 2×2 system above for (u, v)
```

Everything the engine already does through the projection then works without
a ternary case:

* `ChartHighlightShape::Polygon`, `Hull`, `Blob` and `PointHalo`
  (an ellipse comes out as the affine image of an ellipse, which is correct);
* the solved label plan, which works in screen space;
* hit regions (`AddHitRegion` with a polygon), tooltips and legend toggling.

A chart that kept its own barycentric maths would have to re-implement
each of those, as the pre-engine polar chart once did.

### 4.2 Not a scatter subclass

`UltraCanvasImportancePerformanceChart` derives from the engine scatter chart
because it keeps the scatter's two value axes. A ternary chart keeps none of
them. It describes no domain or value axis (`DescribeAxes` leaves both off)
and draws three edge axes of its own. Deriving from
`UltraCanvasChartEngineElement` directly avoids inheriting x/y settings that
would mean nothing here. Point drawing reuses the engine's `DrawChartMarker`
and `ChartMarkerShape`, so markers look the same as on every other chart.

### 4.3 Registration checklist (what "done" touches)

1. `UltraCanvas/include/Plugins/Charts/Engine/UltraCanvasChartProjection.h`:
   `ChartProjectionKind::Ternary` and `ChartTernaryProjection`, created by
   `CreateChartProjection`. Add a `Tests/ChartEngineTest` case for the round
   trip `ToNormalized(ToScreen(p)) == p` and for the corners.
2. `UltraCanvas/include/Plugins/Charts/Engine/Charts/UltraCanvasTernaryModel.h`
   (UI-free: compositions, normalisation, regions, classification, CSV and
   JSON) and `UltraCanvasTernaryChart.h`, with their `.cpp` files under
   `UltraCanvas/Plugins/Charts/Engine/Charts/`, added to
   `UltraCanvas/CMakeLists.txt` next to the other engine charts.
3. `Apps/DemoApp/UltraCanvasTernaryChartExamples.cpp`, declared in
   `UltraCanvasDemo.h` and registered in the **Charts** section as
   `ternarychart`, with one `AddVariant` per preset. Add it to the demo
   sources in the root `CMakeLists.txt`.
4. `Docs/UltraCanvas/UltraCanvasTernaryChart.md`, the guide, with a
   generated `## Contents`, and a row in `UltraCanvasChartEngine.md`'s list
   of projections.
5. `Tests/TernaryModelTest.cpp` (UI-free, as `MatrixModelTest`) and
   `Tests/TernaryChartRenderTest.cpp`, both in `Tests/CMakeLists.txt`.
6. A `Docs/UltraCanvas/changelog.d/` entry. Run
   `python3 scripts/generate_doc_toc.py`, `check_doc_examples.py` on the
   guide, `check_ui_reuse.py` and `check_callback_cycles.py`, and remove this
   proposal's entries from `scripts/doc_examples_baseline.txt` with
   `check_doc_examples.py --update-baseline` as each API lands.

A chart is a self-rendered view (AGENTS.md), so drawing its axes, regions and
labels by hand is within that exception. `UltraCanvasUIElements.md` needs no
row.

---

## 5. Data model sketch

Everything in this section is a sketch of an API that does not exist yet.
`check_doc_examples.py` records its findings in the baseline until it does.

### 5.1 The engine addition

```cpp
enum class ChartProjectionKind { Vertical, Horizontal, Polar, Space3D, Ternary };

// (u, v) = the shares of the second and third component; the first is
// 1 - u - v. Points with u + v > 1 or a negative share land outside the
// triangle, as values outside 0..1 land outside the plot in the other
// projections.
class ChartTernaryProjection : public IChartProjection {
public:
    ChartProjectionKind Kind() const override { return ChartProjectionKind::Ternary; }
    Point2Dd ToScreen(const ChartNormalizedPoint& p) const override;
    ChartNormalizedPoint ToNormalized(const Point2Dd& screen) const override;

    // The largest equilateral triangle that fits the plot area, apex up
    // (or down, for a QAPF lower half; §9 Q5).
    void SetApexDown(bool down);
    Point2Dd Corner(int index) const;    // 0 = A (apex), 1 = B (left), 2 = C (right)
};
```

### 5.2 The model

```cpp
namespace UltraCanvas {

    // One composition, as given: any non-negative amounts. Shares are
    // computed, never stored, so the raw values stay for the tooltip.
    struct TernaryPoint {
        double a = 0.0, b = 0.0, c = 0.0;
        std::string label;
        std::string group;            // the legend and the colour
        double size = 0.0;            // bubble size; 0 = the series marker size
        std::string note;

        // a / (a + b + c) etc.; false for a sum of 0, a negative or a NaN part.
        bool Shares(double& sa, double& sb, double& sc) const;
    };

    enum class TernarySeriesKind { Scatter, Path, Bubble };

    struct TernarySeries {
        std::string name;
        TernarySeriesKind kind = TernarySeriesKind::Scatter;
        std::vector<TernaryPoint> points;
        Color color = Color(0, 0, 0, 0);       // transparent = from the palette
        ChartMarkerShape marker = ChartMarkerShape::Circle;
        bool arrows = false;                   // Path: an arrowhead per step
    };

    // One axis: the corner it is 100 % of, and how it is read.
    struct TernaryAxis {
        std::string title;            // "Clay"
        std::string unit = "%";
        double minimum = 0.0;         // > 0 zooms to a sub-triangle (Phase 3)
        double tickStep = 0.1;        // as a share; 0 = nice ticks
    };

    // An authored region in composition space: a soil class, a rock field.
    struct TernaryRegion {
        std::string id;
        std::string name;             // "Silty clay loam"
        std::vector<std::array<double, 3>> outline;   // shares, in order, closed
        Color fill = Color(0, 0, 0, 0);
        std::string note;
    };

    enum class TernaryReading { Clockwise, Anticlockwise };

    struct TernaryModel {
        std::string title;
        TernaryAxis axes[3];          // A (apex), B (left), C (right)
        TernaryReading reading = TernaryReading::Clockwise;
        std::vector<TernarySeries> series;
        std::vector<TernaryRegion> regions;

        // The region a composition falls in, by the same geometry the drawing
        // uses; -1 for none. A point on a shared edge belongs to the region
        // listed first (§7.3).
        int RegionOf(double sa, double sb, double sc) const;
        std::vector<std::string> Validate() const;
    };

    namespace TernarySamples {
        TernaryModel SoilTexture();       // USDA, 12 classes, no points
        TernaryModel DeFinetti();         // AA / Aa / aa with the Hardy-Weinberg curve
        TernaryModel ThreePartyVotes();   // invented regions and elections
        std::vector<TernaryPoint> SoilSamples();   // invented field samples
    }

    // CSV: label,group,a,b,c[,size,note] - the header names the three parts.
    bool ImportTernaryCsv(const std::string& csv, TernaryModel& model, std::string* error = nullptr);
    std::string ExportTernaryCsv(const TernaryModel& model);
    std::string ExportTernaryJson(const TernaryModel& model, bool pretty = true);
    bool ImportTernaryJson(const std::string& text, TernaryModel& out, std::string* error = nullptr);
}
```

### 5.3 The chart

```cpp
class UltraCanvasTernaryChart : public UltraCanvasChartEngineElement {
public:
    UltraCanvasTernaryChart(const std::string& id, int x, int y, int w, int h);

    void SetModel(const TernaryModel& model);
    const TernaryModel& GetModel() const;

    void SetShowGrid(bool show);
    void SetShowRegionLabels(bool show);
    void SetReadingGuide(bool show);       // three lines through the pointer, with their shares
    void SetDensity(int subdivisions);     // Phase 3: triangular bins, 0 = off

    // A click on a point or a region.
    std::function<void(size_t series, size_t point)> onPointClick;
    std::function<void(int region)> onRegionClick;

protected:
    void DescribeAxes(ChartAxisSet& axes) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void RenderEngineGrid(IRenderContext* ctx) override;
    void RenderEngineAxes(IRenderContext* ctx) override;
};
```

Notes on the shape of the model:

* **Points hold amounts, not shares.** Data arrives as counts, grams or
  votes, and the tooltip should still be able to say "412 votes (38.1 %)".
  Normalising is the chart's job, and it is the same everywhere (§7.2).
* **Regions are in shares,** because published classification diagrams give
  their corners as percentages.
* **The corners are fixed: A at the apex, B left, C right.** Which data
  column goes to which corner is the model's choice. The reading direction
  is a separate flag (§7.1).

---

## 6. Feature list

### Projection and axes (A)

| Id | Feature |
|---|---|
| A1 | `ChartTernaryProjection`: the largest equilateral triangle in the plot area, round trip exact |
| A2 | Three edge axes with ticks every 10 % (or nice ticks), labels tilted along their edge |
| A3 | Reading direction: clockwise or anticlockwise (§7.1), with tick marks pointing along the grid line they belong to |
| A4 | Grid lines parallel to the edges, major and minor |
| A5 | Corner titles, and axis titles along the edges with an arrow showing the direction of increase |
| A6 | Zoom to a sub-triangle by axis minimums (`minimum` on each axis) |

### Points and series (S)

| Id | Feature |
|---|---|
| S1 | Scatter series from raw amounts, normalised; groups and the legend |
| S2 | Point labels through the label plan |
| S3 | Tooltips: label, group, the three shares and the raw amounts |
| S4 | Paths with optional arrowheads and per-point labels (trajectories) |
| S5 | Bubble size from a fourth value |
| S6 | Reference curves given as share triples (Hardy–Weinberg) |

### Regions (R)

| Id | Feature |
|---|---|
| R1 | Authored polygon regions in shares, filled, with a centred label |
| R2 | `RegionOf` and a per-point region in the tooltip, with the same geometry as the drawing |
| R3 | Region hover and `onRegionClick` |
| R4 | Presets: USDA soil texture, De Finetti |

### Interaction (I)

| Id | Feature |
|---|---|
| I1 | Hover and click points (`onPointClick`); legend toggling |
| I2 | Reading guide: three lines through the pointer to the axes, with their shares |
| I3 | Zoom to a sub-triangle by dragging a selection, and back with a double click |

### Density (D)

| Id | Feature |
|---|---|
| D1 | Triangular binning: each edge cut into `n` pieces, so `n²` small triangles coloured by count through `UltraCanvasColormap` |
| D2 | Iso-lines over the binned field, with the colour bar from the legend |

### Import / export (X)

| Id | Feature |
|---|---|
| X1 | CSV of points: `label,group,<A>,<B>,<C>[,size,note]`, dot-decimal whatever the locale |
| X2 | JSON round trip of the whole model, `"format": "ultracanvas-ternary"` |
| X3 | Image and vector export through the chart base and `UltraCanvasFileLoader`, as every chart |

---

## 7. The hard parts

### 7.1 Which way the axes read

The same point can be read two ways, depending on which way the tick labels
run along each edge, and both are in print. In the **clockwise** convention
(the USDA soil triangle), clay is labelled up the left edge, silt down the
right edge and sand right-to-left along the bottom. A share is read by
following the grid line from the point to the edge that carries its labels.
In the **anticlockwise** convention, the labels run the other way round
the triangle, and each share's grid lines lead to a different edge.

A chart that draws the labels one way and the grid the other way is the
classic misreading. The rules:

* One `TernaryReading` flag sets both together: which edge carries each
  axis's labels, and which way its numbers increase.
* The tick marks are drawn *along the grid line* they belong to, poking out
  of the triangle, so the eye follows the right line. A tick drawn
  perpendicular to the edge points at the wrong grid line.
* The axis title sits beside its edge with a small arrow showing the
  direction of increase.
* The render test checks the reading for one known point: 20 % A, 30 % B,
  50 % C must lie where all three of its grid lines meet.

Default: **clockwise**, as the soil triangle and most textbooks print it
(§9 Q2).

### 7.2 Normalising and rounding

Raw parts never sum exactly to 100: lab results sum to 99.7 %, and rounded
vote shares to 100.1 %. The chart always divides by the sum. It never
rejects a near-100 sum, and it never draws a point outside the triangle
because of rounding. A point with a negative part, a NaN or a zero sum is
not drawn, and `Validate()` names it. Tooltips print the shares to one
decimal place. They do not force the printed shares to add up to 100.0 by
adjusting a digit, because that would show a share that is not the
measurement.

### 7.3 Classification must match the drawing

The lesson of the importance–performance chart (its §7.1) applies: a point
on a region boundary must be classified as the drawing shows it. `RegionOf`
runs the point-in-polygon test in share space, on the same outline the
projection draws. A point exactly on an edge shared by two regions belongs
to the region listed first in the model. The USDA preset lists its classes
in the order its key gives them, so that tie-break matches the published
rule. The model test covers points on every shared edge and every corner of
the preset.

### 7.4 Labels in a triangle

The corners are narrow, and the edge labels are tilted at ±60°. Corner
titles go outside their corner. Long ones wrap, and they never overlap the
tick labels of the two edges that meet there. Region labels are centred on
the region's *pole of inaccessibility* (the inside point farthest from the
edges), not its centroid. A region that is not convex, such as the soil
triangle's "Silt loam", which wraps around "Silt", can have its centroid
outside itself. Labels that still do not fit
are left to the tooltip. All point and region labels go through the label
plan.

### 7.5 Sub-triangle zoom

Zooming in sets a minimum share on each axis. The visible area is then the
smaller triangle where `a ≥ aMin`, `b ≥ bMin` and `c ≥ cMin`. That only
works while `aMin + bMin + cMin < 1`, and the chart clamps a drag that would
break it. Inside the zoom the map is still affine: shares are rescaled as
`(a − aMin) / (1 − Σmin)`. Ticks are generated over the visible range of
each axis, so they read "20 … 40", not "0 … 100" squeezed together.

### 7.6 Colour

Region fills come from a palette whose lightness steps keep neighbouring
classes apart in greyscale. Every region also carries its name as a label or
in the tooltip, so colour is never the only signal. Point colours follow the
engine theme's categorical palette, as on every other chart.

---

## 8. What already exists to build on

| Need | Reuse |
|---|---|
| Normalised space, a projection per kind | `UltraCanvasChartProjection.h` (`IChartProjection`, `CreateChartProjection`) |
| Base class, theme, legend, tooltips, animation | `UltraCanvasChartEngineElement` |
| Markers | `DrawChartMarker`, `ChartMarkerShape` (`UltraCanvasChartSeries.h`) |
| Region fills, hulls, halos | `ChartHighlightShape::Polygon` and the other highlight shapes |
| Point and region labels | `ChartLabelBroker` (`UltraCanvasChartLabels.h`) |
| Hover and click on regions and points | `AddHitRegion` with a polygon, `OnHitRegionHoverChanged` |
| Ticks | `NiceNumber` and `ChartAxis::GenerateTicks` (`UltraCanvasChartAxis.h`) |
| Density colours and colour bar | `UltraCanvasColormap.h`, `LegendColorBar` |
| Iso-lines (D2) | `UltraCanvasMarchingSquares.h`, adapted from squares to triangles |
| CSV and JSON | `UltraCanvasCSVImport.h` (`CSVParse`, `CSVFormatRecord`), `UltraCanvasJSON.h`, following the performance matrix model's interchange |
| Numbers in files | `TryParseFloat` / `FormatFloatClassic` (AGENTS.md rule 2) |
| Loading and saving files | `UltraCanvasFileLoader`; the model functions take and return strings |
| Model tests | `Tests/MatrixModelTest.cpp` and `Tests/PerformanceMatrixModelTest.cpp` as templates |

Nothing new is needed from the render context: every element is a line, a
polygon, a marker or text.

---

## 9. Open questions (with recommendations)

**Q1. A projection in the engine, or maths in the chart?** **Recommendation:
the engine** (§4.1). It costs one class of about 60 lines. Highlights, hit
regions and the label plan then work without a ternary case. Any later
chart that needs a triangle (a QAPF diamond, a ternary heat map) gets it
for free.

**Q2. Which reading direction by default?** **Recommendation: clockwise**,
the soil-science and most textbook convention. Make it one flag, never two,
and have the demo show both with the same point marked.

**Q3. Should shares be stored or computed?** **Recommendation: computed.**
Points keep their raw amounts (§5.2 notes). Storing shares would drop the
"412 votes" in the tooltip and invite shares that do not sum to one.

**Q4. Is the soil triangle's data safe to ship?** The class boundaries are
published by the USDA Natural Resources Conservation Service. They are
facts, not artwork, and textbooks reprint them freely. **Recommendation:**
transcribe the corners from the Soil Survey Manual when Phase 2 is built,
cite it in the preset's comment, and check each class in the model test
against a point the manual names. The demo's sample points must be
invented.

**Q5. QAPF and other two-triangle diagrams?** The Streckeisen QAPF
diagram is two triangles that share their A–P edge (alkali feldspar to
plagioclase), with Q at the upper apex and F at the lower one. **Recommendation: out of scope for now.**
`SetApexDown` in the projection keeps the door open: a later
`UltraCanvasTernaryDiamondChart` can be two projections side by side.

**Q6. Charts section or Diagrams?** A ternary plot plots data on axes.
**Recommendation: Charts**, as `ternarychart`, on the engine with the other
charts.

---

## 10. Suggested phasing

| Phase | Delivers | Retires |
|---|---|---|
| **1** *(done)* | `ChartTernaryProjection` (A1) and its engine test; the model with normalisation and `Validate`; A2–A5; S1–S3; I1; the `ThreePartyVotes` sample; the demo page with scatter variants in both reading directions; the guide; model and render tests | Reference shape 1 |
| **2** | Regions R1–R4 with the USDA soil texture and De Finetti presets; paths, bubbles and reference curves (S4–S6); the reading guide (I2); X1 and X2 with demo Open / Save through `UltraCanvasFileLoader` | Reference shapes 2 and 3 |
| **3** | Sub-triangle zoom (A6, I3); triangular binning and iso-lines (D1, D2) | Reference shape 4 |

Every phase is lines, polygons, markers and text through one affine map.

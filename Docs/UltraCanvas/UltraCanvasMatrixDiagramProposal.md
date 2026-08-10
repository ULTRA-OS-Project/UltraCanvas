# UltraCanvasMatrixDiagram — Research & Feature Proposal

**Status:** **Not implemented.** A repository-wide search for `matrix diagram`,
`MatrixDiagram`, `MatrixChart`, `L-shaped`, `roof matrix`, `QFD` and
`house of quality` returns no element, no header, no demo slot and no test. The
only `matrix` hits are (a) `UltraCanvasQuadrantChart`, which uses the word for
its 2×2 *positioning* presets (BCG, Eisenhower, Risk, Ansoff, Priority), (b)
`UltraCanvasHeatmapChart`, whose file comment calls it a "2D heatmap / matrix
chart", and (c) `UltraCanvasSWOTDiagram`'s `SWOTDesign::Matrix` geometry preset.
None of the three is a matrix diagram in the sense investigated here.

**Author:** UltraCanvas Framework
**Last Modified:** 2026-08-10
**Related:** `UltraCanvasHeatmapChart` (implemented — the closest *geometric*
sibling: a labelled grid of cells, but scalar-valued),
`UltraCanvasAdjacencyDiagram` (implemented — holds *exactly* the data of
reference images 1 and 2 in node-link form; see §4.2),
`UltraCanvasSWOTDiagram` and `UltraCanvasFishboneDiagram` (implemented — the
closest *architectural* siblings, and the template this proposal follows),
`Docs/UltraCanvas/SWOTDiagramDesignVariants.md` (the same kind of survey done
for SWOT).

---

## 1. Summary — the headline question

The five reference images look like five different diagrams. They are **one**
diagram drawn in five *shapes*, and — unlike the fishbone survey, where the
variation was purely decorative — here the shape **is** the data model. Every
image reduces to the same three primitives:

> **item sets** (ordered lists of labelled things), **panels** (a grid formed
> by crossing one set with another), and **cell marks** (a symbol drawn at an
> intersection, taken from a small shared **relationship scale**).

What varies between images is only *how many sets there are and how the panels
are placed around them*:

| Image | Sets | Panels | Classical name |
|---|---|---|---|
| 1 (rooms, red/blue dots) | 1 (against itself) | 1, rotated 45° | Roof / triangular |
| 2 (rooms + Y/N attribute columns) | 1 + attribute columns | 1 rotated + a side table | Roof + attributes |
| 3 (X-shaped, four groups) | 4 | 4, pinwheeled round a hub | X-shaped |
| 4 (T-shaped, employees) | 3 | 2, sharing a row axis | T-shaped |
| 5 (Entry/Lobby/… bubbles) | 2 | 1, axis-aligned | L-shaped |

That taxonomy is not invented for this document — it is the standard one from
the *Seven Management and Planning Tools* (Mizuno / JUSE), where the matrix
diagram is one of the seven and is catalogued exactly as **L, T, Y, X, C and
roof shaped**. The images happen to sample four of the six.

**Recommendation: one new element, `UltraCanvasMatrixDiagram`, in
`Plugins/Diagrams/`,** a `UltraCanvasChartElementBase` subclass whose
`MatrixShape` enum selects the panel arrangement, modelled on
`UltraCanvasSWOTDiagram` / `UltraCanvasFishboneDiagram`. Split the data model
into a UI-free `UltraCanvasMatrixModel.h` so roll-up arithmetic and text
interchange can be unit-tested without the widget stack — the precedent
`UltraCanvasFishboneModel` and `UltraCanvasSequenceModel` already set.

**It is not a heatmap and must not be built on one.** See §4.1: the difference
is ordinal-symbol-plus-weight versus continuous-scalar-plus-colormap, and it
runs all the way down to the roll-up arithmetic.

---

## 2. What a matrix diagram is

A matrix diagram makes the *relationships between two or more lists* visible
and countable. Rows and columns are things; each intersection answers "is there
a relationship here, and how strong?" using a symbol from a legend of three to
five levels. Two properties distinguish it from every grid-shaped chart already
in the repository:

1. **The cell value is ordinal and named, not continuous.** The canonical QFD
   scale is `● Strong (9) / ○ Medium (3) / △ Weak (1) / blank None (0)`. The
   levels carry a glyph, a colour, a label *and* a numeric weight. Reference
   image 2 uses a five-level scale (MUST / SHOULD / RAISE / YES / NO / SEMI);
   image 1 uses two (must-be-adjacent, preferred-adjacent); image 4 uses three
   (high / medium / low).
2. **The weights are meant to be summed.** The whole point of the QFD form is
   the roll-up: `columnScore(j) = Σᵢ rowImportance(i) × cellWeight(i,j)`,
   then ranked and shown as a bar strip under the grid. A diagram that draws
   the dots but cannot compute the totals has delivered the decoration and not
   the tool.

### The six shapes

| Shape | Sets | Relates | Where seen |
|---|---|---|---|
| **L** | 2 (A, B) | A×B | The basic form; image 5, and the left half of image 4 |
| **T** | 3 (A, B, C) | B×A and A×C — A is the shared spine | Image 4 |
| **Y** | 3 | A×B, B×C, C×A — three L panels folded into a hexagon | — |
| **X** | 4 | A×B, B×C, C×D, D×A — four L panels pinwheeled | Image 3 |
| **C** | 3 | A×B×C simultaneously, as a cube | — (rarely drawn; see §9 Q3) |
| **Roof** | 1 (A against itself) | the upper triangle of A×A | Images 1, 2; the roof of a House of Quality |

**House of Quality** is the composite worth naming as a target: an L panel
(customer needs × technical requirements) with a roof triangle on top
(requirement×requirement correlations), an importance column on the left, a
competitive-assessment block on the right, and a target/score strip below. It
is five of the primitives above assembled by one layout rule, which is the
strongest argument for building the primitives properly rather than hard-coding
each shape.

---

## 3. What the five reference images demand

### Image 1 — hand-drawn room adjacency, 45° triangle

Eleven rooms down the left (MASTER BEDROOM … ENTRY); the grid is rotated 45° so
each row's cells run diagonally up-right and the whole thing forms a triangle
whose hypotenuse is the label column. Two mark levels: a **red dot** ("spaces
which must be next to each other") and a **blue dot** ("spaces which are
preferred to be next to each other"); no mark means no requirement. A legend
box sits in the free space to the right of the triangle.

*Demands:* `Roof` shape; a symmetric single-set panel; row labels on the
hypotenuse, horizontal (**not** rotated with the grid — the labels stay level
while the cells rotate, which is the one geometric subtlety of the whole
element); a legend placed in the triangle's free corner.

### Image 2 — roof matrix with an attribute table

The same triangle, but with a block of narrow **attribute columns** to the left
of the labels: Y/N flags per room and a square-footage figure. Marks are dots in
four colours plus letter codes, with a legend listing `MOST / SHOULD / RAISE`
(coloured dots) and `Y = YES / N = NO / S = SEMI` (letters).

*Demands:* everything from image 1, plus **row attribute columns** (arbitrary
per-row text/flag columns rendered as a table gutter beside the labels), and a
scale whose marks can be a **glyph, a letter, or a coloured dot** — the mark
renderer must accept text, not only shapes.

### Image 3 — X-shaped, four groups

A central grid with four sets on the four sides: *New Feature* (left, reading
into the grid), *Product Manager* (top), *Office Location* (right), *Principal
Engineer* (bottom). The centre square is blocked out; the four surrounding
blocks each carry one pairwise relation. Marks are purple/green discs (high /
low amount of time spent). Note that the left and bottom label bands are
themselves grids of coloured cells — the sets are *shown* as chip strips.

*Demands:* `X` shape; four panels placed around a dead hub; label bands drawn
as chip strips with per-set accent colours; a legend as a titled card.

### Image 4 — T-shaped, employees × (projects, skills)

A form-style header strip (PROJECT / AREA / TITLE / TEAM / DATE), then a T:
*Employees* down the centre-left as the shared row axis, *Projects* columns to
its left with black dots, *Skills* columns to its right with high/medium/low
coloured dots. Rotated column headers on both wings. An arrow-and-caption band
("← Projects | Skills →") sits above the axis labels.

*Demands:* `T` shape; **two panels sharing one row axis**; per-panel scales
(the left wing uses a one-level presence dot, the right wing a three-level
scale) — so the scale belongs to the *panel*, not the element; rotated column
headers; an optional title/metadata header band.

### Image 5 — L-shaped presentation slide

A 5×5 axis-aligned grid, the same set on both axes (Entry, Lobby, Reception,
Administration, Security), large filled circles at intersections in four
category colours, thin grid rules, and a description-plus-legend panel to the
left.

*Demands:* `L` shape; large disc marks whose size is a style parameter; a
left-side legend with descriptive body text; a light "rules only, no cell fill"
grid style.

### Cross-image constants

Everything below appears in every image and belongs in the core, not in a
preset:

- Ordered, labelled item sets with an optional accent colour per set.
- A relationship scale: 2–5 levels, each `{glyph|letter, colour, label, weight}`.
- A legend keyed to that scale, placeable in the free space the shape leaves.
- Cells that are usually *empty* — matrices are sparse, so the storage should be
  sparse and the API should be "set the marks you have", not "fill the grid".
- Grid rules drawn under the marks; marks never fill the cell (contrast with
  the heatmap, where the fill *is* the datum).

And two things that appear in only some images and therefore belong behind
flags: row attribute columns (image 2), and a form-style header band (image 4).

---

## 4. Where this belongs in the repository

### 4.1 The overlap that must be argued: `UltraCanvasHeatmapChart`

`UltraCanvasHeatmapChartElement` already draws a labelled grid of cells with
row/column labels, cell borders, cell value text, three cell shapes
(rectangle / rounded / circle), a triangular mask (`Lower`, `LowerNoDiagonal`,
`Upper`, `UpperNoDiagonal`) and a colour bar. On the face of it, image 5 is a
dot-heatmap and image 1 is an upper-triangular dot-heatmap. So: why not a
subclass, as `UltraCanvasContourChart` did?

Because the four things that make a matrix diagram a matrix diagram are all
places where the heatmap's design points the other way:

| | Heatmap | Matrix diagram |
|---|---|---|
| Cell datum | `double`, dense `std::vector<double>` sized `cols*rows` | ordinal level ID, sparse — most cells empty |
| Encoding | value → colormap → cell **fill** | level → **glyph + colour**, drawn *on* an unfilled cell |
| Legend | continuous colour bar | discrete symbol key with names |
| Panels | exactly one | one (L, Roof), two (T), three (Y), four (X) |
| Geometry | axis-aligned only | 45°-rotated panels with level labels |
| Derived values | none | weighted column/row roll-ups and rankings |

A subclass would inherit a dense buffer it does not want, a colour bar it must
suppress, an `Image` render mode that cannot draw glyphs, and a
single-plot-area layout that cannot express T/Y/X. `HeatmapTriangularMask`
masks *cells* inside a square grid; the roof shape needs the *panel* rotated and
the labels not rotated with it. The right relationship is sibling, not child.

**Keep the boundary explicit in both docs:** continuous cell value → use
`UltraCanvasHeatmapChartElement`; named relationship levels, several item sets,
or weighted roll-ups → use `UltraCanvasMatrixDiagram`.

### 4.2 The overlap that is an opportunity: `UltraCanvasAdjacencyDiagram`

Reference images 1 and 2 are architectural space-planning adjacency matrices.
`UltraCanvasAdjacencyDiagram` already models that domain — `AdjacencyRoom`
(id, label, area, function type) plus `AdjacencyLink` (source, target, type ∈
{`Direct`, `Secondary`, `ServiceOnly`}) — and renders it as the *bubble* form.
The matrix in image 1 and the bubble diagram are two views of one dataset, and
architects routinely draw both.

So the proposal is not only "add a matrix element" but **"add the second view of
a dataset the framework already holds"**: a free function

```cpp
// Plugins/Diagrams/UltraCanvasMatrixModel.h
MatrixModel BuildAdjacencyMatrix(const std::vector<AdjacencyRoom>& rooms,
                                 const std::vector<AdjacencyLink>& links);
```

that produces a `Roof`-shaped model with a three-level scale mapped from
`AdjacencyLinkType`. Cheap to write, and it turns two components into a pair.
(Keep it in the model header, which already depends on nothing UI-side; do not
make either element depend on the other.)

### 4.3 The other neighbours, and why none of them is this

- **`UltraCanvasQuadrantChart`** — "matrix" there means a 2×2 *positioning*
  space where items are plotted at continuous (x, y). No item sets, no cells,
  no scale. Different tool, colliding vocabulary. Worth one cross-reference
  line in each doc so a reader searching "matrix" lands correctly.
- **`UltraCanvasTableView`** — a data grid with columns, sorting and selection.
  A matrix diagram's attribute gutter (image 2) resembles it, but the diagram
  must render as one picture and export as one image; embedding a table view
  inside it would fight both. Draw the gutter; do not embed.
- **`UltraCanvasChordChart`** — consumes a square relationship matrix and draws
  it as arcs. Another *view* of the same primitive, and a second candidate for
  a `BuildMatrixFrom…` bridge later; out of scope for Phase 1.
- **`UltraCanvasSWOTDiagram` / `UltraCanvasFishboneDiagram`** — no data overlap,
  but they are the architectural template: `ChartElementBase` subclass, a design
  enum, a layout cache in local coordinates, a `…Ref` handle for hit testing,
  hover/selection callbacks, dark theme flag.

### 4.4 Registration checklist (what "done" touches)

1. `UltraCanvas/include/Plugins/Diagrams/UltraCanvasMatrixModel.h` — UI-free
   data model, scale, presets, roll-up arithmetic, interchange.
2. `UltraCanvas/include/Plugins/Diagrams/UltraCanvasMatrixDiagram.h` — element.
3. `UltraCanvas/Plugins/Diagrams/UltraCanvasMatrixModel.cpp`,
   `UltraCanvasMatrixDiagram.cpp` (split rendering out into
   `…DiagramRender.cpp` if it passes ~900 lines, as the requirement diagram
   does).
4. `UltraCanvas/CMakeLists.txt` — add the sources next to the other
   `Plugins/Diagrams/*.cpp` entries (~line 421).
5. `Apps/DemoApp/UltraCanvasMatrixDiagramExamples.cpp`, declared in
   `Apps/DemoApp/UltraCanvasDemo.h` (~line 340) and registered in
   `UltraCanvasDemo.cpp` via `infoBuilder.AddItem("matrixdiagram", …)` with one
   `AddVariant` per shape; add the source to the demo list in the root
   `CMakeLists.txt` (~line 205).
6. `Docs/UltraCanvas/UltraCanvasMatrixDiagram.md` (guide) and
   `UltraCanvasMatrixDiagramExamples.md`; cross-reference from
   `UltraCanvasHeatmapChart.md`, `UltraCanvasAdjacencyDiagramExamples.md` and
   `UltraCanvasQuadrantChartExamples.md`.
7. `Tests/MatrixModelTest.cpp` (+ its entry in `Tests/CMakeLists.txt`) — roll-up
   arithmetic and the CSV codecs, following `Tests/FishboneModelTest.cpp`.
8. `python3 scripts/generate_llms_txt.py`, and
   `python3 scripts/check_ui_reuse.py` before pushing.

Note that `Docs/UltraCanvas/UltraCanvasUIElements.md` needs **no** edit: the
catalogue covers interactive UI elements and explicitly delegates "charts,
diagrams and document views" to the plugin docs. Nor does the generated
`llms.txt` change for this proposal — `generate_llms_txt.py` excludes any file
whose name contains `Proposal`, `Plan` or `DesignVariants`. The guide and
examples docs of item 6 *are* indexed, so regenerate when those land.

The element is a **self-rendered view** in the sense of AGENTS.md ("charts"), so
painting its own cells and labels is within the stated exception; any editing
affordance added later (§9 Q5) must use real elements as children.

---

## 5. Data model sketch

```cpp
// include/Plugins/Diagrams/UltraCanvasMatrixModel.h  — no UI dependencies

namespace UltraCanvas {

// ---- Shapes ----------------------------------------------------------------
enum class MatrixShape {
    L,      // A x B                                (2 sets, 1 panel)
    T,      // B x A, A x C  — A is the shared spine (3 sets, 2 panels)
    Y,      // A x B, B x C, C x A                  (3 sets, 3 panels)
    X,      // A x B, B x C, C x D, D x A           (4 sets, 4 panels)
    Roof,   // A x A, upper triangle, rotated 45    (1 set,  1 panel)
    House   // L + Roof + importance + targets      (QFD composite)
};

// ---- Relationship scale ----------------------------------------------------
struct MatrixLevel {
    std::string id;                       // "strong"
    std::string label;                    // "Strong relationship"
    std::string glyph;                    // "●", "Y", "" (empty => shape only)
    MatrixMarkShape shape = MatrixMarkShape::Disc; // Disc, Ring, Triangle,
                                          // Square, Diamond, Cross, Text
    Color  color  = Color(200, 40, 40, 255);
    double weight = 9.0;                  // feeds the roll-ups
};

struct MatrixScale {
    std::string          title;           // legend heading
    std::vector<MatrixLevel> levels;      // ordered strongest -> weakest
    int IndexOf(const std::string& levelId) const;

    static MatrixScale QFD();             // ● 9 / ○ 3 / △ 1
    static MatrixScale Adjacency();       // must / preferred / avoid
    static MatrixScale HighMediumLow();
    static MatrixScale Presence();        // single level: a dot or nothing
};

// ---- Item sets -------------------------------------------------------------
struct MatrixItem {
    std::string id;
    std::string label;
    double      importance = 0.0;         // QFD weighting; 0 = unweighted
    Color       accent = Color(0, 0, 0, 0);   // transparent => inherit the set
    std::vector<std::string> attributes;  // parallel to MatrixItemSet::columns
    std::string note;
};

struct MatrixItemSet {
    std::string id;                       // "requirements"
    std::string title;                    // axis caption
    Color       accent = Color(90, 130, 200, 255);
    std::vector<MatrixItem>  items;
    std::vector<std::string> columns;     // attribute-gutter headers (image 2)
};

// ---- Cells (sparse) --------------------------------------------------------
struct MatrixCell {
    int  panel = 0;                       // index into MatrixModel::panels
    int  row = 0, col = 0;                // indices within that panel's sets
    std::string levelId;
    std::string note;                     // tooltip / annotation
};

struct MatrixPanel {
    std::string rowSetId, colSetId;
    MatrixScale scale;                    // per-panel: image 4 needs two
    bool        symmetric = false;        // Roof: rowSet == colSet, upper only
};

struct MatrixModel {
    std::string title, subtitle;
    MatrixShape shape = MatrixShape::L;
    std::vector<MatrixItemSet> sets;
    std::vector<MatrixPanel>   panels;
    std::vector<MatrixCell>    cells;

    // ---- mutation ----
    void SetCell(const std::string& rowId, const std::string& colId,
                 const std::string& levelId, int panel = 0);
    void ClearCell(const std::string& rowId, const std::string& colId, int panel = 0);
    const MatrixCell* FindCell(int panel, int row, int col) const;

    // ---- derived (the part worth unit-testing) ----
    double ColumnScore(int panel, int col) const;  // Σ row.importance × weight
    double RowScore(int panel, int row) const;
    std::vector<int> RankColumns(int panel) const; // descending ColumnScore
    MatrixValidation Validate() const;             // dangling ids, wrong set
                                                   // count for the shape, ...
};

// ---- Interop ---------------------------------------------------------------
MatrixModel BuildAdjacencyMatrix(const std::vector<AdjacencyRoom>&,
                                 const std::vector<AdjacencyLink>&);
std::string ExportMatrixCsv(const MatrixModel&, int panel = 0);
bool        ImportMatrixCsv(const std::string& csv, MatrixModel& out);

} // namespace UltraCanvas
```

Three deliberate choices, each with a reason:

- **Sparse cells.** Real matrices are 5–20 % populated. A dense buffer would
  make `SetCell` O(1) but force callers to size the grid before naming the
  items, and it makes the `Roof` half-grid awkward. Keep a `std::map`-backed
  index inside the element for O(log n) lookup during render.
- **Scale on the panel, not the model.** Image 4 settles this: the two wings of
  the T use different scales.
- **`importance` on the item, not a parallel array.** QFD weights live with the
  row they weight, which keeps CSV round-trips simple.

---

## 6. Feature list

Codes follow the fishbone/contour proposals so phasing can reference them.

### Shapes (D)
- **D1** `L` — one axis-aligned panel. *(Phase 1)*
- **D2** `Roof` — one 45°-rotated symmetric half panel. *(Phase 1)*
- **D3** `T` — two panels sharing the row axis. *(Phase 1)*
- **D4** `X` — four panels pinwheeled around a dead hub. *(Phase 2)*
- **D5** `Y` — three panels at 120°, hexagonal envelope. *(Phase 2)*
- **D6** `House` — QFD composite: L + roof + importance + target strip. *(Phase 3)*
- **D7** `C` — 3-set cube. *(Deferred — see §9 Q3)*

### Data & content (C)
- **C1** Item sets with per-item labels, accents and notes.
- **C2** Sparse cell marks with an optional per-cell note.
- **C3** Row importance weights.
- **C4** Attribute gutter columns (image 2).
- **C5** Scale presets: QFD, Adjacency, HighMediumLow, Presence.
- **C6** Three worked samples for the demo (QFD phone, room adjacency,
  employees × projects × skills).
- **C7** `Validate()` — dangling item ids, set count wrong for the shape,
  duplicate cells, level id not in the panel's scale.

### Layout (L)
- **L1** Automatic cell sizing from the widget bounds, with a minimum cell size
  and a scrollable/panning overflow beyond it.
- **L2** Rotated column headers (0° / 45° / 90°), auto-selected from the
  available header band height.
- **L3** Label ellipsis and wrapping with a measured gutter width.
- **L4** The roof geometry: cells rotated, labels level (§7).
- **L5** Legend placement in the shape's free corner, using `ChartLegend`.
- **L6** Optional form-style header band (image 4).
- **L7** Roll-up strip: column scores as a bar row under the grid, with rank
  numbers.

### Style (S)
- **S1** Grid rule width/colour; optional alternating row bands.
- **S2** Mark size as a fraction of the cell; per-level size multiplier.
- **S3** Mark shapes: disc, ring, triangle, square, diamond, cross, text glyph.
- **S4** Per-set accent colours tinting the label bands (image 3).
- **S5** Dark theme.
- **S6** Cell background tint by strongest mark (opt-in — the bridge to a
  heatmap look for readers who want it).
- **S7** Row/column highlight cross-hair bands on hover.

### Text (T)
- **T1** Title and subtitle via `SetChartTitle`.
- **T2** Axis captions per set.
- **T3** Value text in cells (the weight, or the level's letter).
- **T4** Label halo for readability where labels overrun their band — the
  `UltraCanvasAdjacencyDiagram::DrawTextWithHalo` pattern.

### Interaction (I)
- **I1** Hover: highlight the cell, its row and its column; tooltip with
  `row × column = level` and the cell note.
- **I2** Click callbacks: `onCellClick(MatrixRef)`, `onItemClick`,
  `onLegendClick`.
- **I3** Selection of a cell, a row or a column, with a `MatrixRef` handle
  (`{panel, row, col}`, −1 meaning "whole row/column").
- **I4** Legend filtering — click a level to dim every other mark.
- **I5** Pan/zoom for large matrices via `UltraCanvasDiagramViewport`.
- **I6** Keyboard navigation across cells. *(Phase 4)*

### Import / export (X)
- **X1** CSV round-trip of one panel (level ids in the cells).
- **X2** Mermaid has no matrix diagram; skip. Import the QFD-ish tabular form
  instead — header row of column labels, first column of row labels.
- **X3** JSON via `UltraCanvasJSON` for the whole multi-panel model. *(Phase 4)*

---

## 7. The hard parts

Three, in descending order of risk.

**7.1 The roof geometry (D2/L4).** The cells rotate 45°; the labels must not.
The clean formulation is to keep a unit-cell lattice in *matrix space* — cell
`(i, j)` is the unit square at `(j, i)` — and apply a rotation of −45° with a
scale of `cellSize / √2` when emitting cell geometry, while emitting label text
in unrotated element space at the projected position of each row's hypotenuse
edge. `IRenderContext` supports this directly: `ctx->Rotate(angle)` inside a
push/pop state pair, exactly as `UltraCanvasChartElementBase` already does for
rotated axis labels (`UltraCanvasChartElementBase.cpp:246`). The trap is hit
testing — do the inverse transform on the pointer rather than trying to test
rotated quads.

**7.2 The X and Y placement solver (D4/D5).** Four panels around a hub is not
four transformed copies of one panel: the shared axes mean panel A's column
extent *is* panel B's row extent, so the four panels' cell sizes are coupled and
the envelope is only square if the opposing sets happen to be the same length.
Solve it as: (a) compute each set's item count and label width; (b) pick one
global cell size that fits the tightest of the four directions; (c) lay the hub
out last, as the leftover. Y-shape adds a 120° rotation and is the same solver
in a hexagonal basis — implement X first and generalise.

**7.3 Label gutters and the measure-then-place cycle.** Column-header rotation
(L2) changes the header band height, which changes the cell size, which changes
whether the labels still fit. Break the cycle the way the repository already
does elsewhere: measure at the *unrotated* size, choose the rotation from the
available band once, then lay out — do not iterate to a fixed point. Cache the
whole layout in local coordinates and invalidate it on any data or style
mutation, as `UltraCanvasSWOTDiagram::SWOTLayout` does.

---

## 8. What already exists to build on

| Need | Reuse |
|---|---|
| Element base: title, tooltip plumbing, plot-area cache, render/hit entry points | `UltraCanvasChartElementBase` (`RenderChart`, `HandleChartMouseMove`) |
| Discrete symbol legend | `ChartLegend` + `ChartLegendEntry`; `LegendSwatch` covers Square/Circle/Marker — **add a `Glyph` swatch** for text marks (`●`, `Y`, `S`) rather than drawing a private legend |
| Rotated text | `IRenderContext::Rotate` with push/pop state; the precedent is `UltraCanvasChartElementBase.cpp:246` and `UltraCanvasMekkoChart.cpp:574` |
| Pan/zoom, minimap, fit-to-view (I5) | `UltraCanvasDiagramViewport` — element-local convention, already extracted for node/compositor diagrams |
| Readable labels over busy fills | the halo technique in `UltraCanvasAdjacencyDiagram::DrawTextWithHalo` |
| Colour ramps for S6 | `Plugins/Charts/UltraCanvasColormap.h` |
| Architecture, naming, layout cache, `…Ref` hit handle, dark theme flag | `UltraCanvasSWOTDiagram`, `UltraCanvasFishboneDiagram` |
| Model/element split for testability | `UltraCanvasFishboneModel`, `UltraCanvasSequenceModel` |
| Source data for the adjacency sample | `UltraCanvasAdjacencyDiagram`'s `AdjacencyRoom` / `AdjacencyLink` (§4.2) |

One gap worth flagging early: `LegendSwatch` has no glyph/text option, so
**either** extend that enum (preferred — chord, packet and node legends would
all benefit) **or** the matrix element draws its own legend and loses the
positioning, wrapping and hit-testing that `ChartLegend` already solves. Extend
the enum.

---

## 9. Open questions (with recommendations)

**Q1 — One element or one per shape?**
*Recommendation: one.* The shapes share the data model, the mark renderer, the
legend, the hit testing and the roll-ups; only placement differs. This is the
SWOT/fishbone finding again, and the opposite of the timeline finding — there,
two incompatible data models forced a split; here there is only one.

**Q2 — Subclass `UltraCanvasHeatmapChartElement`?**
*Recommendation: no.* §4.1. Sibling under `Plugins/Diagrams/`, with the
boundary documented in both guides.

**Q3 — Build the C-shape (3-set cube)?**
*Recommendation: defer, and say so in the doc.* It is the rarest of the six and
needs an isometric projection plus depth-ordered marks — a large amount of new
geometry for a form most users will never open. Revisit only if asked.

**Q4 — Separate `UltraCanvasMatrixModel.h`?**
*Recommendation: yes.* Not for separation-of-concerns aesthetics but because
`ColumnScore`/`RankColumns`/`Validate`/the CSV codecs are exactly the things
worth unit-testing, and a UI-free header is what lets `Tests/MatrixModelTest.cpp`
compile without the widget stack — the reasoning `UltraCanvasFishboneModel`
recorded after the fact. The element keeps all geometry.

**Q5 — Editable cells (click to cycle the level)?**
*Recommendation: Phase 4, opt-in via `SetEditable(true)`, and cycle-on-click
only.* Anything richer (a level picker popup) must use `UltraCanvasMenu` /
`UltraCanvasDropdown` as real child elements, per AGENTS.md — the self-rendered
exception covers the *content*, not the controls.

**Q6 — Where does `House` (QFD) live: a shape, or a preset builder?**
*Recommendation: a shape.* Its extra bands (importance column, competitive
assessment, target strip) are layout, not new data — the model already carries
importance on the item and scores are derived. A preset builder that fills a
`House` model with the standard captions is a nice extra on top.

---

## 10. Suggested phasing

**Phase 1 — the tool.** `UltraCanvasMatrixModel` (C1–C3, C5, C7) and
`UltraCanvasMatrixDiagram` with D1 `L`, D2 `Roof`, D3 `T`; L1–L5; S1–S5;
T1–T4; I1–I3; the `ChartLegend` `Glyph` swatch; `BuildAdjacencyMatrix`; demo
with the three samples of C6; guide and examples docs; catalogue entry;
`Tests/MatrixModelTest.cpp` covering the roll-ups. This alone reproduces
reference images 1, 4 and 5.

**Phase 2 — the remaining shapes.** D4 `X`, D5 `Y` and the §7.2 solver; C4
attribute gutter and L6 header band (finishing images 2 and 3); S6, S7; I4.

**Phase 3 — the analysis.** D6 `House`, L7 roll-up strip with rankings, X1/X2
CSV interchange.

**Phase 4 — polish.** I5 viewport, I6 keyboard navigation, X3 JSON, Q5
editing.

Phase 1 is the deliverable that makes the element worth shipping; each later
phase is independently useful and none of them changes the Phase 1 API.

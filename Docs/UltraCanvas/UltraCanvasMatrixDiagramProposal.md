# UltraCanvasMatrixDiagram — Research & Feature Proposal

**Status:** **Not implemented.** A repository-wide search for `matrix diagram`,
`MatrixDiagram`, `MatrixChart`, `L-shaped`, `roof matrix`, `QFD` and
`house of quality` returns no element, no header, no demo slot and no test. The
only `matrix` hits are (a) `UltraCanvasQuadrantChart`, which uses the word for
its 2×2 *positioning* presets (BCG, Eisenhower, Risk, Ansoff, Priority), (b)
`UltraCanvasHeatmapChart`, whose file comment calls it a "2D heatmap / matrix
chart", and (c) `UltraCanvasSWOTDiagram`'s `SWOTDesign::Matrix` geometry preset.
None of the three is a matrix diagram in the sense investigated here.

**Revision 3 (2026-08-10) — scope correction.** Reference images 1, 2 and 7 are
architectural room-adjacency matrices, and adjacency data is already modelled by
`UltraCanvasAdjacencyDiagram`. They are therefore **out of scope for this
element** and have moved to
[`UltraCanvasAdjacencyMatrixViewProposal.md`](UltraCanvasAdjacencyMatrixViewProposal.md),
which proposes a matrix *view* on that element. Revisions 1–2 had this wrong in
a way worth recording: they spotted the data overlap (§4.2) and concluded the
matrix element should own a standalone `Roof` shape plus a
`BuildAdjacencyMatrix()` bridge to convert the data across. That would have put
"these rooms must be adjacent" in two components and left callers syncing a
converted copy against the original. One dataset, two renderers — in the
component that owns the dataset. Consequences here: the `BuildAdjacencyMatrix`
bridge is gone, the standalone `Roof` shape is gone, and triangular geometry
survives only as the QFD roof inside `House` (D6, Phase 3). §3.6's
staircase-versus-rotated finding still stands — it simply belongs to the
adjacency proposal now, where it drives that element's phasing instead of this
one's.

**Revision 2 (2026-08-10)** — three further reference images were checked
against revision 1. The core model survived unchanged, but four things in it
were wrong or too narrow and have been corrected in place; §3.6 records what
changed and why. In short: relationship weights are **not** universally QFD's
9/3/1; the triangular shape has **two** renderings, not one, and the easy one
was missing; totals belong in **gutter cells**, not only in a bar strip; and the
X-shape hub is often **captioned**, not dead.

**Author:** UltraCanvas Framework
**Last Modified:** 2026-08-10
**Related:** `UltraCanvasHeatmapChart` (implemented — the closest *geometric*
sibling: a labelled grid of cells, but scalar-valued),
`UltraCanvasAdjacencyDiagram` (implemented — owns the data of reference images
1, 2 and 7, which are consequently **not** this element's problem; see §4.2 and
[`UltraCanvasAdjacencyMatrixViewProposal.md`](UltraCanvasAdjacencyMatrixViewProposal.md)),
`UltraCanvasSWOTDiagram` and `UltraCanvasFishboneDiagram` (implemented — the
closest *architectural* siblings, and the template this proposal follows),
`Docs/UltraCanvas/SWOTDiagramDesignVariants.md` (the same kind of survey done
for SWOT).

---

## 1. Summary — the headline question

The eight reference images look like eight different diagrams. They are **one**
diagram drawn in a handful of *shapes*, and — unlike the fishbone survey, where
the variation was purely decorative — here the shape **is** the data model.
Every image reduces to the same three primitives:

> **item sets** (ordered lists of labelled things), **panels** (a grid formed
> by crossing one set with another), and **cell marks** (a symbol drawn at an
> intersection, taken from a small shared **relationship scale**).

What varies between images is only *how many sets there are and how the panels
are placed around them*:

| Image | Sets | Panels | Classical name |
|---|---|---|---|
| 3 (X-shaped, four groups) | 4 | 4, pinwheeled round a hub | X-shaped |
| 4 (T-shaped, employees) | 3 | 2, sharing a row axis | T-shaped |
| 5 (Entry/Lobby/… bubbles) | 2 | 1, axis-aligned | L-shaped |
| 6 (improvement tools × departments) | 2 | 1, axis-aligned + totals gutters | L-shaped |
| 8 (X-shaped, suppliers/storage/customers/lines) | 4 | 4, round a **captioned** hub | X-shaped |
| ~~1, 2, 7 (room adjacency)~~ | ~~1~~ | ~~1, triangular~~ | *ceded — see revision 3* |

That taxonomy is not invented for this document — it is the standard one from
the *Seven Management and Planning Tools* (Mizuno / JUSE), where the matrix
diagram is one of the seven and is catalogued exactly as **L, T, Y, X, C and
roof shaped**. The images sample four of the six, twice over.

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

1. **The cell value is ordinal and named, not continuous.** The best-known
   scale is QFD's `● Strong (9) / ○ Medium (3) / △ Weak (1) / blank None (0)`.
   The levels carry a glyph, a colour, a label *and* a numeric weight. Reference
   image 2 uses a five-level scale (MUST / SHOULD / RAISE / YES / NO / SEMI);
   image 1 uses two (must-be-adjacent, preferred-adjacent); images 4, 7 and 8
   use three; image 6 uses three on a **5/3/1** scale, not 9/3/1. The weights
   are therefore data, not constants — see §3.6(a).
2. **The weights are meant to be summed.** The whole point of the form is the
   roll-up: `columnScore(j) = Σᵢ rowImportance(i) × cellWeight(i,j)`, shown
   either as numeric totals in a gutter (image 6) or, in QFD, ranked as a bar
   strip under the grid. Where no importances are given, importance is 1 and the
   roll-up is a plain sum of weights. A diagram that draws the dots but cannot
   compute the totals has delivered the decoration and not the tool.

### The six shapes

| Shape | Sets | Relates | Where seen |
|---|---|---|---|
| **L** | 2 (A, B) | A×B | The basic form; image 5, and the left half of image 4 |
| **T** | 3 (A, B, C) | B×A and A×C — A is the shared spine | Image 4 |
| **Y** | 3 | A×B, B×C, C×A — three L panels folded into a hexagon | — |
| **X** | 4 | A×B, B×C, C×D, D×A — four L panels pinwheeled | Image 3 |
| **C** | 3 | A×B×C simultaneously, as a cube | — (rarely drawn; see §9 Q3) |
| **Roof** | 1 (A against itself) | the upper triangle of A×A | Images 1, 2, 7 — all room adjacency, and so **not this element's** (§4.2); survives here only as the House of Quality correlation roof |

**House of Quality** is the composite worth naming as a target: an L panel
(customer needs × technical requirements) with a roof triangle on top
(requirement×requirement correlations), an importance column on the left, a
competitive-assessment block on the right, and a target/score strip below. It
is five of the primitives above assembled by one layout rule, which is the
strongest argument for building the primitives properly rather than hard-coding
each shape.

---

## 3. What the first five reference images demand

### Images 1 and 2 — room adjacency triangles *(ceded — revision 3)*

Both are architectural space-planning adjacency matrices: a set of spaces
crossed against itself, marks encoding required adjacency strength. Image 2 adds
a gutter of Y/N attribute columns and square footages. That is
`UltraCanvasAdjacencyDiagram`'s data, so the analysis of these two images now
lives in
[`UltraCanvasAdjacencyMatrixViewProposal.md`](UltraCanvasAdjacencyMatrixViewProposal.md)
§2 rather than here. See §4.2 for why.

One observation from image 2 is *not* adjacency-specific and stays in scope: its
marks are a mixture of coloured dots and **letter codes** (`Y = YES`, `N = NO`,
`S = SEMI`) sharing one legend. So the mark renderer must accept **text**, not
only shapes — which is where `MatrixMarkShape::Text` and the `ChartLegend`
`Glyph` swatch of §8 come from.

### Image 3 — X-shaped, four groups

A central grid with four sets on the four sides: *New Feature* (left, reading
into the grid), *Product Manager* (top), *Office Location* (right), *Principal
Engineer* (bottom). The centre square is blocked out (image 8 instead fills it
with set captions — see §3.6c); the four surrounding
blocks each carry one pairwise relation. Marks are purple/green discs (high /
low amount of time spent). Note that the left and bottom label bands are
themselves grids of coloured cells — the sets are *shown* as chip strips.

*Demands:* `X` shape; four panels placed around a blank hub; label bands drawn
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

And one thing that appears in only some images and therefore belongs behind a
flag: the form-style header band of images 4 and 8. (Revision 1 listed row
attribute columns here too, on the strength of image 2 — but image 2 is ceded,
so that feature lost its driver in this element; see C4.)

## 3.6 Second image set — what it changed

Three further images were checked against revision 1 of this document. None of
them broke the model of §1 — item sets, panels, cell marks — and none of them
introduced a new shape. But each falsified one specific claim, and one of them
falsified the hardest claim in the document.

### Image 6 — improvement tools × departments, with totals

An axis-aligned L: nine departments (HR … Store) as rows, eight improvement
tools (Graphical Analysis, SPC, KPIs, Data Collection Methods, Cause and Effect,
Process Mapping, Kaizen, 5S) as columns, three levels (red Strong, blue Medium,
grey Weak) drawn as equal-size discs. Column headers are **wrapped onto two or
three lines, not rotated**. A grey **gutter column on the right** carries the row
totals (18, 9, 8, 14, 32, 16, 34, 38, 8) and a grey **gutter row underneath**
carries the column totals (29, 18, 23, 32, 21, 21, 17, 18). Legend is a single
horizontal strip below the grid.

Two corrections come out of this image:

**(a) The weights are not 9/3/1.** Revision 1 treated QFD's `● 9 / ○ 3 / △ 1` as
the norm. Reading the marks off this image and summing them, the scale is
**Strong 5 / Medium 3 / Weak 1**: HR is 3 Medium + 1 Strong + 4 Weak =
9 + 5 + 4 = **18** ✓, Finance is 1 Strong + 1 Medium + 1 Weak = **9** ✓,
Purchasing is 5 Weak + 1 Medium = **8** ✓. Under 9/3/1, HR would come out at 22.
So `MatrixLevel::weight` being caller-settable is not a nicety, it is required,
and the presets need a `Strength531()` next to `QFD()`. (The published totals do
not quite reconcile — the row totals sum to 177 and the column totals to 179.
I may have misread a digit at this resolution, but it is the ordinary failure
mode of a hand-built slide, and it is the argument for `ColumnScore()` /
`RowScore()` being computed by the element rather than typed in: totals derived
from the marks are consistent by construction.)

**(b) `importance` must default to 1.0, not 0.0.** Revision 1's data model
defaulted `MatrixItem::importance` to `0.0` and defined
`ColumnScore = Σ rowImportance × cellWeight`. This image has no importance
column at all — its totals are a plain sum of cell weights — so under revision 1
every score would have evaluated to **zero**. Defaulting importance to `1.0`
makes the unweighted case fall out of the same formula, and QFD stays a matter
of setting real importances. This was a straightforward bug in the sketch.

### Image 7 — MUST / SHOULD / MAYBE room matrix *(ceded — revision 3)*

Twenty-odd spaces (ENTRY, LOBBY, RECEPTION … STORAGE) crossed against
themselves, three levels (red MUST, blue SHOULD, green MAYBE), legend top-left,
column labels rotated ~90° along the top. Structurally identical to images 1 and
2 — the same symmetric single-set half matrix — **but it is not rotated 45°.**
It is drawn as an ordinary axis-aligned grid whose populated region is a
right-triangle staircase.

That finding stands, and it was the most consequential in revision 2: the
triangular matrix has two renderings, and the cheap one (staircase, no new
geometry, scales past 20 items) is the one real programs use. But revision 3
moved this image, and the finding with it, to
[`UltraCanvasAdjacencyMatrixViewProposal.md`](UltraCanvasAdjacencyMatrixViewProposal.md)
§2.2 — it is adjacency data, and the finding now drives *that* element's
phasing. Recorded here only because it is why this document no longer has a
standalone `Roof` shape at all.

The residue for this element: when the correlation roof arrives with `House`
(D6, Phase 3), it is the **rotated** form — a QFD roof is drawn at 45° by
convention and is small (5–15 technical requirements), so the scaling argument
that favours the staircase does not apply there.

### Image 8 — X-shaped, suppliers / storage / customers / lines

The same citoolkit template family as image 4: form-style header band
(PROJECT / AREA / TITLE / TEAM / DATE), legend top-right (● HIGH black,
● MEDIUM yellow, ○ LOW hollow). Four sets pinwheeled — Suppliers (left, headers
rotated 90°), Storage Areas W/H 1–4 (top rows), Customers (right, part numbers),
Production Lines 1–4 (bottom rows) — giving Suppliers×Storage, Storage×Customers,
Customers×Lines, Lines×Suppliers. That is precisely the A×B, B×C, C×D, D×A
cycle §2 predicted, which is the one place the new images *confirmed* a
non-obvious claim rather than correcting it.

Two corrections:

**(c) The hub is not always dead.** Revision 1 said "the centre square is
blocked out" (§3, image 3) and specified D4 as "four panels pinwheeled around a
dead hub". Image 3 does block it out; image 8 fills it with the four set names
in captioned boxes with arrows pointing outward to the bands they name. Both are
common, so the hub is a mode — `MatrixHubMode { Blank, Captions }` — not a
constant.

**(d) A hollow mark level is load-bearing.** The LOW level here is a white disc
with an outline, distinguished from HIGH/MEDIUM by fill rather than by hue.
`MatrixMarkShape::Ring` was already in the revision 1 list; this promotes it
from "nice to have" to a level the presets must actually use, since a
three-level scale that degrades to greyscale printing needs one hollow step.

### What held

Worth recording explicitly, because it is the reason the model did not need
reworking: three-to-five ordinal levels, disc marks, per-set accent colours, the
discrete symbol legend, the form header band, rotated column headers, sparse
cell storage, the A×B/B×C/C×D/D×A structure of the X shape, and the
one-element-with-a-shape-enum recommendation all survived the second image set
unchanged. The four corrections above are all *within* the model, not to it.

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
| Panels | exactly one | one (L), two (T), three (Y), four (X), five (House) |
| Geometry | axis-aligned only | 45°-rotated panels with level labels |
| Derived values | none | weighted column/row roll-ups and rankings |

A subclass would inherit a dense buffer it does not want, a colour bar it must
suppress, an `Image` render mode that cannot draw glyphs, and a
single-plot-area layout that cannot express T/Y/X. `HeatmapTriangularMask`
masks *cells* inside a square grid. The right relationship is sibling, not
child.

**Keep the boundary explicit in both docs:** continuous cell value → use
`UltraCanvasHeatmapChartElement`; named relationship levels, several item sets,
or weighted roll-ups → use `UltraCanvasMatrixDiagram`.

### 4.2 The overlap that is *not* ours: `UltraCanvasAdjacencyDiagram`

Reference images 1, 2 and 7 are architectural space-planning adjacency
matrices. `UltraCanvasAdjacencyDiagram` already models that domain —
`AdjacencyRoom` (id, label, area, function type) plus `AdjacencyLink` (source,
target, type ∈ {`Direct`, `Secondary`, `ServiceOnly`}) — and renders it as the
*bubble* form. The matrix and the bubble diagram are two views of one dataset.

Revisions 1 and 2 of this document treated that as an opportunity for *this*
element: a `Roof` shape here, plus a `BuildAdjacencyMatrix()` free function to
convert `AdjacencyRoom`/`AdjacencyLink` into a `MatrixModel`. That was the wrong
call. A conversion function does not make two components into a pair — it makes
a copy, and a copy has to be regenerated every time the program changes, with
nothing in the type system to say when it went stale. Worse, it would give the
framework two answers to "where do I say these rooms must be adjacent?", which
is exactly the kind of duplication `AGENTS.md` exists to prevent.

**These three images are therefore out of scope here.** They are the subject of
[`UltraCanvasAdjacencyMatrixViewProposal.md`](UltraCanvasAdjacencyMatrixViewProposal.md),
which adds an `AdjacencyView::Matrix` render path to the element that already
owns the data. No bridge function, no shared model, and no dependency in either
direction between that element and this one.

What this element gives up as a result: the standalone `Roof` shape, and with it
the whole of the rotated-triangle geometry from Phase 1. Triangular geometry
returns only in Phase 3, as the correlation roof of `House` (D6) — which is a
requirement×requirement correlation, not adjacency data, and so is genuinely
this element's.

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
   `UltraCanvasHeatmapChart.md` and `UltraCanvasQuadrantChartExamples.md`. The
   guide must also point room-adjacency callers at
   `UltraCanvasAdjacencyDiagram` rather than at a shape here — that boundary
   only holds if both docs state it.
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
    House   // L + correlation roof + importance + targets  (QFD composite)
};
// No standalone Roof shape: every triangular reference image turned out to be
// adjacency data, which UltraCanvasAdjacencyDiagram owns (§4.2). The only
// symmetric half matrix left is the QFD correlation roof, which is internal to
// House and arrives with it in Phase 3.

// What sits in the middle of an X-shaped diagram: nothing (image 3) or the
// four set captions with arrows out to their bands (image 8).
enum class MatrixHubMode { Blank, Captions };

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
    static MatrixScale Strength531();     // strong 5 / medium 3 / weak 1 (image 6)
    static MatrixScale HighMediumLow();   // filled / filled / ring (image 8)
    static MatrixScale Presence();        // single level: a dot or nothing
};

// ---- Item sets -------------------------------------------------------------
struct MatrixItem {
    std::string id;
    std::string label;
    double      importance = 1.0;         // QFD weighting; 1.0 => the roll-ups
                                          // degrade to a plain sum of weights,
                                          // which is what image 6 needs
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
    bool        symmetric = false;        // rowSet == colSet, upper triangle
                                          // only (the House correlation roof)
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
// No BuildAdjacencyMatrix(): revision 3 removed it. This header must not know
// about AdjacencyRoom/AdjacencyLink -- adjacency data stays in the element that
// owns it and is drawn by that element's own matrix view (§4.2).
std::string ExportMatrixCsv(const MatrixModel&, int panel = 0);
bool        ImportMatrixCsv(const std::string& csv, MatrixModel& out);

} // namespace UltraCanvas
```

Three deliberate choices, each with a reason:

- **Sparse cells.** Real matrices are 5–20 % populated. A dense buffer would
  make `SetCell` O(1) but force callers to size the grid before naming the
  items, and it makes the `House` roof's half-grid awkward. Keep a `std::map`-backed
  index inside the element for O(log n) lookup during render.
- **Scale on the panel, not the model.** Image 4 settles this: the two wings of
  the T use different scales.
- **`importance` on the item, not a parallel array.** QFD weights live with the
  row they weight, which keeps CSV round-trips simple. It defaults to `1.0` so
  that an unweighted matrix (image 6) gets plain weight sums out of the same
  formula — see §3.6(b).

---

## 6. Feature list

Codes follow the fishbone/contour proposals so phasing can reference them.

### Shapes (D)
- **D1** `L` — one axis-aligned panel. *(Phase 1)*
- **D2** ~~`Roof`~~ — **withdrawn in revision 3.** Every triangular reference
  image is adjacency data; see §4.2. The QFD correlation roof survives inside
  D6 and arrives with it.
- **D3** `T` — two panels sharing the row axis. *(Phase 1)*
- **D4** `X` — four panels pinwheeled around a hub, `Blank` or `Captions`
  (`MatrixHubMode`). *(Phase 2)*
- **D5** `Y` — three panels at 120°, hexagonal envelope. *(Phase 2)*
- **D6** `House` — QFD composite: L + the correlation roof + importance +
  target strip. Brings the rotated-triangle geometry of §7.1 with it, and is
  the only thing that needs it. *(Phase 3)*
- **D7** `C` — 3-set cube. *(Deferred — see §9 Q3)*

### Data & content (C)
- **C1** Item sets with per-item labels, accents and notes.
- **C2** Sparse cell marks with an optional per-cell note.
- **C3** Row importance weights.
- **C4** Attribute gutter columns — **demoted in revision 3.** Image 2 was its
  only driver and is now ceded, and the adjacency proposal picks the feature up
  as its own §2.4. Keep the slot, build it when a matrix-element caller asks.
  *(Deferred)*
- **C5** Scale presets: QFD (9/3/1), Strength531 (5/3/1), HighMediumLow,
  Presence — with caller-settable weights throughout (§3.6a).
- **C6** Three worked samples for the demo: improvement tools × departments
  with totals (image 6), employees × projects × skills (image 4), and
  suppliers/storage/customers/lines (image 8). No room-adjacency sample — that
  one belongs to the adjacency diagram's demo (§4.2).
- **C7** `Validate()` — dangling item ids, set count wrong for the shape,
  duplicate cells, level id not in the panel's scale.

### Layout (L)
- **L1** Automatic cell sizing from the widget bounds, with a minimum cell size
  and a scrollable/panning overflow beyond it.
- **L2** Column header fitting: wrap onto N lines (image 6) *or* rotate
  (0° / 45° / 90°, images 7 and 8), auto-selected from the available band height.
  Wrapping is the better default for short labels and must not be an
  afterthought — image 6 uses it in preference to rotation.
- **L3** Label ellipsis and wrapping with a measured gutter width.
- **L4** The correlation-roof geometry: cells rotated 45°, labels level (§7.1).
  *(Phase 3, with D6 — nothing before `House` needs it.)*
- **L5** Legend placement in the shape's free corner, using `ChartLegend`.
- **L6** Optional form-style header band (image 4).
- **L7** Roll-up **gutters**: a numeric total cell per column along the bottom
  edge and/or per row along the right edge, on a tinted band (image 6). This is
  the common presentation and belongs in Phase 1.
- **L8** Roll-up **bar strip**: the same column scores drawn as bars with rank
  numbers — the QFD presentation, an alternative to L7's numbers, not a
  replacement. *(Phase 3, with `House`)*

### Style (S)
- **S1** Grid rule width/colour; optional alternating row bands.
- **S2** Mark size as a fraction of the cell; per-level size multiplier.
- **S3** Mark shapes: disc, ring, triangle, square, diamond, cross, text glyph.
  At least one preset level must be a `Ring` so a three-level scale stays
  readable in greyscale (§3.6d).
- **S4** Per-set accent colours tinting the label bands (image 3).
- **S5** Dark theme.
- **S6** Cell background tint by strongest mark (opt-in — the bridge to a
  heatmap look for readers who want it).
- **S7** Row/column highlight cross-hair bands on hover.
- **S8** X-shape hub captions: the four set names in boxes with arrows out to
  their bands (`MatrixHubMode::Captions`, image 8).

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

**7.1 The correlation-roof geometry (D6/L4).** *Revision 3 note: this is no
longer on the critical path at all. Revision 1 made it a Phase 1 prerequisite;
revision 2 demoted it to Phase 2 after image 7 showed the staircase alternative;
revision 3 removed the standalone roof shape entirely (§4.2), so the only thing
that still needs the rotation is the QFD correlation roof, which arrives with
`House` in Phase 3. Phases 1 and 2 are entirely axis-aligned.*

The cells rotate 45°; the labels must not.
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

**7.3 Label gutters and the measure-then-place cycle.** Column-header wrapping
or rotation (L2) changes the header band height, which changes the cell size,
which changes whether the labels still fit. Break the cycle the way the
repository already does elsewhere: measure at the *unwrapped, unrotated* size,
choose wrap-lines-or-rotation from the available band once, then lay out — do
not iterate to a fixed point. Cache the
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

**Q7 — Should this element ever grow a general symmetric (one-set) shape?**
*Recommendation: not on present evidence, and not without a reference case that
is genuinely not adjacency.* All three triangular images were room adjacency,
which `UltraCanvasAdjacencyDiagram` owns (§4.2). A generic symmetric shape here
would be a solution looking for a problem, and its first user would probably be
someone who should have used the adjacency element. If a real non-adjacency case
turns up — team-to-team contact frequency, module coupling — revisit it then;
`House`'s correlation roof will by that point have supplied most of the
machinery.

---

## 10. Suggested phasing

**Phase 1 — the tool.** `UltraCanvasMatrixModel` (C1–C3, C5, C7) and
`UltraCanvasMatrixDiagram` with D1 `L` and D3 `T`; L1–L3, L5, **L7 totals
gutters**; S1–S5; T1–T4; I1–I3; the `ChartLegend` `Glyph` swatch; demo with the
samples of C6; guide and examples docs; `Tests/MatrixModelTest.cpp` covering the
roll-ups. This reproduces reference images 4, 5 and 6, and contains **no rotated
geometry at all**.

**Phase 2 — the remaining shapes.** D4 `X` (both hub modes, S8), D5 `Y` and the
§7.2 solver; L6 header band. Finishes images 3 and 8.
Plus S6, S7, I4.

**Phase 3 — the analysis.** D6 `House` — which is where the rotated correlation
roof (§7.1, L4) finally arrives — L8 roll-up bar strip with rankings, X1/X2 CSV
interchange.

**Phase 4 — polish.** I5 viewport, I6 keyboard navigation, X3 JSON, Q5
editing.

Phase 1 is the deliverable that makes the element worth shipping; each later
phase is independently useful and none of them changes the Phase 1 API. Note
that revision 3 pushed all rotated geometry out to Phase 3: Phases 1 and 2 are
now entirely axis-aligned, and the single hardest problem in the original plan
does not block anything until `House`.

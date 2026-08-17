# UltraCanvasAdjacencyDiagram — Matrix View: Research & Feature Proposal

**Status:** **Phase 1 and the §2.2 `Rotated45` style are implemented** — see
[`UltraCanvasAdjacencyDiagramExamples.md`](UltraCanvasAdjacencyDiagramExamples.md)
for the guide. Delivered: `AdjacencyView { Bubble, Matrix }`, the
`AdjacencyPriority` field of §2.1 (Must/Should/Maybe; `Avoid` still awaits the
maintainer's decision of Q3), both triangular geometries of §2.2
(`Staircase` and the classic `Rotated45` diamond triangle — the demo's
*Matrix view* and *Triangle 45°* tabs show one of each), the legend of §2.3 for
both views, and the attribute columns of §2.4. One finding from implementation
worth recording: the rotated form needs **no render transform after all** — in
diagonal coordinates `u = s+t, v = s−t` every diamond is an axis-aligned unit
square, so hit testing is two additions and a floor, and the §2.2 table's
"transform *and* its inverse" cost never materialised. The scaling argument for
Staircase as the default still stands. Still open: `Avoid` (Q3) and the
pan/zoom question (Q4).

This document remains the research write-up behind the design.

**Author:** UltraCanvas Framework
**Last Modified:** 2026-08-12
**Related:** `UltraCanvasAdjacencyDiagram` (implemented — the element this
extends; see [`UltraCanvasAdjacencyDiagramExamples.md`](UltraCanvasAdjacencyDiagramExamples.md)),
[`UltraCanvasMatrixDiagramProposal.md`](UltraCanvasMatrixDiagramProposal.md)
(the sibling proposal that **ceded** these three reference images to this one —
see its revision 3 note and §4.2), `UltraCanvasHeatmapChart` (whose
`HeatmapTriangularMask::Upper` is the same populated-region idea).

---

## 1. Summary

Three of the eight matrix-diagram reference images surveyed in
`UltraCanvasMatrixDiagramProposal.md` are architectural space-planning
adjacency matrices:

| Image | Content |
|---|---|
| 1 | Hand-drawn: 11 domestic spaces, 45°-rotated triangle, red "must be next to each other" / blue "preferred" |
| 2 | 20-odd civic spaces, 45°-rotated triangle, plus a Y/N attribute table and a square-footage column on the left |
| 7 | 20 museum/theatre spaces, **axis-aligned staircase** triangle, red MUST / blue SHOULD / green MAYBE |

All three encode one thing: **a set of spaces, and the required strength of
adjacency between each pair.** That is precisely what
`UltraCanvasAdjacencyDiagram` already stores — `AdjacencyRoom` (id, label, area,
function type, floor) plus `AdjacencyLink` (source, target, type, weight,
label). The matrix and the bubble diagram are two drawings of one dataset, and
architects produce both from the same program: the matrix to *decide* the
adjacencies, the bubble diagram to *arrange* them.

**Recommendation: add a view mode to `UltraCanvasAdjacencyDiagram`**, not a
shape to the matrix element:

```cpp
enum class AdjacencyView { Bubble, Matrix };
void SetView(AdjacencyView v);
```

The revision-1 draft of the matrix proposal got this wrong. It noticed the data
overlap and drew the wrong conclusion — that the matrix element should own a
`Roof` shape and gain a `BuildAdjacencyMatrix()` bridge function to import the
adjacency data. That would have split one dataset across two components, given
the framework two places to express "these rooms must be adjacent", and left
callers to keep a converted copy in sync with the original. The right shape of
the change is one dataset, two renderers.

---

## 2. What the three images demand beyond what exists

Four gaps, one of which is a genuine modelling issue rather than a rendering
one.

### 2.1 The modelling gap: strength is not the same axis as kind

`AdjacencyLinkType` today encodes the **kind** of connection:

| Value | Meaning today (from the header) |
|---|---|
| `Direct` | solid line — rooms must be directly adjacent / share a wall |
| `Secondary` | dashed — rooms should be nearby / indirect access |
| `ServiceOnly` | dotted — service / back-of-house connection only |

The matrices encode **priority**: MUST / SHOULD / MAYBE (image 7),
must-be-adjacent / preferred (image 1), MOST / SHOULD / RAISE (image 2). Two of
the three values line up — `Direct` ↔ MUST, `Secondary` ↔ SHOULD — but the third
does not. `ServiceOnly` is a statement about *what kind of link it is* (a
back-of-house route), not about *how strongly it is wanted*. A service link can
itself be mandatory or merely preferred, and image 7's MAYBE is a weak
requirement, not a service route.

So the two are orthogonal axes that happen to overlap on two values.
Overloading `AdjacencyLinkType` with a MAYBE value would make the enum mean two
things at once and would change what existing bubble-form callers get.

*Recommendation:* add a second, defaulted field rather than touching the enum —
non-breaking, and it lets a link be both "service" and "must":

```cpp
enum class AdjacencyPriority {
    Must,       // red   — must be adjacent
    Should,     // blue  — should be adjacent / preferred
    Maybe,      // green — desirable if it works out
    Avoid       // separation is required (see below)
};

struct AdjacencyLink {
    // ... existing fields unchanged ...
    AdjacencyPriority priority = AdjacencyPriority::Must;
};
```

`Must` is the default so every existing caller keeps its current meaning: the
bubble view ignores the field, and a matrix view of untouched existing data
shows every link at full strength, which is the honest reading of data that
never expressed a priority.

`Avoid` is not in any of the three images, and I would normally leave it out on
that basis. It is included because a negative level is standard in this specific
tool — the reason architects draw the matrix at all is to catch pairs that must
*not* touch (kitchen/toilets, plant room/reading room), and there is currently
no way to say that in the model: an `AdjacencyLink` that means "keep these
apart" would render in the bubble view as a line *connecting* them. Flag this
one for the maintainer's decision; it is the only item here that adds meaning
rather than presentation.

### 2.2 Triangular geometry — and which form to build first

The images disagree on how to draw the half matrix, and the disagreement is
worth more than it looks:

| | Staircase (image 7) | Rotated45 (images 1, 2) |
|---|---|---|
| Grid | ordinary axis-aligned cells | rotated −45°, labels kept level |
| Populated region | upper-right staircase | diamond / roof triangle |
| New geometry needed | **none** — a populated-region test in a normal grid layout | a transform for drawing *and* its inverse for hit testing |
| Scales to | 20+ spaces comfortably (image 7 does exactly this) | ~12 before it sprawls |

`Staircase` should be the default and should ship first. It needs no rotation
machinery at all, and it is the form that handles the room counts real programs
have — image 7 fits 20 spaces; image 2 needs a whole page for the same number.
`Rotated45` is the traditional hand-drawn look and can follow.

```cpp
enum class AdjacencyMatrixStyle { Staircase, Rotated45 };
```

### 2.3 A legend

All three images carry one, and the bubble view has none today. The element is a
`UltraCanvasUIElement`, not a `UltraCanvasChartElementBase` — but `ChartLegend`
is a standalone class (`Measure(ctx, area)` / `Render(ctx, area)` /
`HitTest(point)`, no base-class dependency), so the element can simply own one.
Use it rather than hand-painting a key; it brings placement, wrapping and hit
testing along.

This is worth doing for **both** views: a bubble diagram colour-coded by
`RoomFunctionType` with no key is a diagram the reader has to guess at, and the
five function-type colours are already in `AdjacencyDiagramStyle`.

### 2.4 Attribute columns (image 2 only)

Image 2 puts a table to the left of the row labels: several Y/N flag columns and
a square-footage figure per space. `AdjacencyRoom` already holds `areaSqM`,
`floorId` and `note`, so the area column is free. The Y/N flags need a generic
per-room slot:

```cpp
struct AdjacencyRoom {
    // ... existing fields unchanged ...
    std::vector<std::string> attributes;   // parallel to the diagram's headers
};

void SetAttributeColumns(const std::vector<std::string>& headers);
```

Lowest priority of the four — it is one image, and it is a table beside a
diagram rather than part of the diagram.

---

## 3. What this does *not* need

Worth stating, because the temptation is to build the general thing:

- **No shape enum.** A room adjacency matrix is always one set against itself.
  There is no L, T, Y or X case here — those are the matrix element's business.
- **No weighted roll-ups.** `Σ importance × weight` is a QFD idea. Counting a
  room's MUST links has some value ("this space is the hub"), but none of the
  three images shows a total, and the bubble view already answers "what is
  connected to what" better than a number would. Leave it out until asked.
- **No new import/export.** The data already round-trips however the caller's
  existing adjacency data does.

If a future caller genuinely needs a symmetric matrix over something that is
*not* rooms — team-to-team contact, module coupling — that is the moment to
revisit whether `UltraCanvasMatrixDiagram` should grow a general symmetric
shape. Nothing here forecloses it; the machinery would be independent.

---

## 4. API sketch

Additive throughout — every existing call keeps its behaviour.

```cpp
// ===== VIEW =====
enum class AdjacencyView { Bubble, Matrix };

void          SetView(AdjacencyView v);
AdjacencyView GetView() const;

// ===== MATRIX VIEW OPTIONS =====
void SetMatrixStyle(AdjacencyMatrixStyle s);      // Staircase (default) | Rotated45
void SetMatrixOrder(const std::vector<std::string>& roomIds);  // explicit row/col order
void SetShowMatrixDiagonal(bool on);              // default false
void SetAttributeColumns(const std::vector<std::string>& headers);

// ===== LEGEND (both views) =====
void SetShowLegend(bool on);
void SetLegendPosition(ChartLegendPosition p);

// ===== CALLBACKS =====
// Reuses the existing onLinkClick for a populated cell; a new callback covers
// empty cells, which the bubble view has no equivalent of.
std::function<void(const std::string& rowId, const std::string& colId)> onMatrixCellClick;
```

Style additions on `AdjacencyDiagramStyle` (mark colours per priority, cell
size, grid rule colour, header rotation) follow the existing field-per-knob
convention of that struct.

Row/column order deserves a note: the bubble view is position-based
(`room.x/room.y`), which gives no ordering for a matrix. Default to insertion
order — it is what the caller typed, and every image's ordering is meaningful
(public spaces first, service last). `SetMatrixOrder` is the override.

---

## 5. Where this touches the repository

1. `UltraCanvas/include/Plugins/Diagrams/UltraCanvasAdjacencyDiagram.h` —
   `AdjacencyView`, `AdjacencyMatrixStyle`, `AdjacencyPriority`, the new
   `AdjacencyLink::priority` and `AdjacencyRoom::attributes` fields, the API of
   §4, the matrix style block.
2. `UltraCanvas/Plugins/Diagrams/UltraCanvasAdjacencyDiagram.cpp` — the matrix
   render path and its hit testing. The file is 904 lines today; if the matrix
   path pushes it much past ~1200, split it as
   `UltraCanvasAdjacencyMatrix.cpp`, following the requirement diagram's
   model/layout/render split.
3. `Apps/DemoApp/UltraCanvasAdjacencyDiagramExamples.cpp` — a third tab. The
   demo has two tabs today (Library, Residence), both bubble-form. Add a
   **Matrix** tab that renders the *existing* Library program as a staircase
   matrix, so the demo shows one dataset in two views side by side — that is
   the whole argument for this design, made visible in ten seconds. A fourth
   tab reproducing image 7's museum/theatre program at 20 spaces would prove
   the scaling claim of §2.2.
4. `Apps/DemoApp/UltraCanvasDemo.cpp` — the adjacency entry at ~line 1407 has
   no variants today; add `AddVariant("adjacencydiagrams", …)` for Bubble and
   Matrix.
5. `Docs/UltraCanvas/UltraCanvasAdjacencyDiagramExamples.md` — new sections
   under *Data Structures* (priority, attributes) and *Usage Examples* (the
   matrix view), and a line in *Features*. The doc's *Clear / View / Selection*
   section is the natural home for `SetView`.
6. `python3 scripts/generate_llms_txt.py` (the Examples doc is indexed; this
   proposal is not — `Proposal` is an excluded pattern), and
   `python3 scripts/check_ui_reuse.py`.

No CMake change: both files are already built.

---

## 6. Phasing

**Phase 1 — the view.** `AdjacencyView`, `AdjacencyPriority` (Must/Should/Maybe),
`AdjacencyMatrixStyle::Staircase`, the legend for both views, insertion-order
rows with `SetMatrixOrder`, cell hover/click. Demo gains the Matrix tab on the
existing Library data. Reproduces image 7. No rotated geometry.

**Phase 2 — the traditional look and the wide program.**
`AdjacencyMatrixStyle::Rotated45` (reproduces images 1 and 2's geometry), the
20-space demo sample, attribute columns (§2.4, finishing image 2).

**Phase 3 — if wanted.** `AdjacencyPriority::Avoid` and its bubble-view
rendering (§2.1), which is the one item that changes what the model can say and
so wants a maintainer's decision first.

---

## 7. Open questions

**Q1 — View mode on one element, or a second element?**
*Recommendation: one element, two views.* Two elements would re-split the
dataset this proposal exists to keep together. The precedent is
`UltraCanvasSWOTDiagram`, where six geometries live behind one `SWOTDesign`
enum on one element.

**Q2 — Should the bubble view also gain the legend?**
*Recommendation: yes*, and it is worth doing even if the matrix view slips. The
function-type colours are already there and currently unexplained.

**Q3 — Is `Avoid` in scope?**
*Recommendation: maintainer's call, defaulted to no for Phase 1.* It is the
only change here that adds meaning to the model rather than a way of drawing it,
and it needs a bubble-view answer (a red barred line?) before it can land
coherently.

**Q4 — Does the matrix view need pan/zoom for large programs?**
*Recommendation: not in Phase 1.* The element already has drag-panning; a
20-space staircase fits a normal widget. If 50-space programs turn up,
`UltraCanvasDiagramViewport` is the shared answer, as it was for the node and
compositor diagrams.

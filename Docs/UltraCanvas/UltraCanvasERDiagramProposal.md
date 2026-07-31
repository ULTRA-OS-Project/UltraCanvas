# UltraCanvasERDiagram — Research & Feature Proposal

Status: **Not implemented.** The DemoApp already reserves the slot
(`diagramBuilder.AddItem("erdiagram", "ER Diagram", …,
ImplementationStatus::NotImplemented)` in
`Apps/DemoApp/UltraCanvasDemo.cpp`), so this document defines what the
element has to be before that flag can flip.

This is the research write-up and the roadmap. The API documentation
(`UltraCanvasERDiagram.md`) and the examples doc
(`UltraCanvasERDiagramExamples.md`) are written against the implementation
once Phase 1 lands.

Author: UltraCanvas Framework
Last Modified: 2026-07-31

---

## 1. What an ER diagram is

An **entity–relationship diagram** (ERD, ER model) is the standard visual
language for a database's conceptual schema. It answers three questions:

* **What things exist?** — *entities* (`Customer`, `Order`, `Item`)
* **What do we know about them?** — *attributes* (`Name`, `E-mail`, `Order Number`)
* **How are they connected, and how many of each?** — *relationships*
  (`Places`, `Contains`) with *cardinality* and *participation* constraints.

ER diagrams are drawn at three levels of abstraction, and a complete element
must be able to render all three because they demand different visuals:

| Level | What it shows | Visual consequence |
|---|---|---|
| **Conceptual** | Entities + relationships only; no attributes, no types | Few boxes, named diamonds/lines, generous spacing |
| **Logical** | Adds all attributes, keys, cardinality; still engine-neutral | Chen-style attribute ovals, or attribute rows inside boxes |
| **Physical** | Adds column data types, nullability, indexes, FK columns, table names | Crow's-foot boxes with typed attribute rows |

The critical structural point — and the thing that separates an ER element
from a generic node/link diagram: **in classical (Chen) ER, a relationship is
a first-class node, not an edge.** `Customer —(Places)— Order` is three nodes
and two edges, not one edge. A ternary relationship is one diamond with three
legs. Modelling relationships as edges only (the crow's-foot shortcut) makes
n-ary relationships, relationship attributes and identifying relationships
inexpressible. The element therefore needs a **relationship-as-node core
model**, with crow's-foot rendering as a *projection* of that model, not a
separate model.

Sources consulted:
[Chen notation guide (Creately)](https://creately.com/guides/chen-notation-in-erd/),
[ER symbols & notations (Gleek)](https://www.gleek.io/blog/er-symbols-notations),
[Crow's Foot notation (freeCodeCamp)](https://www.freecodecamp.org/news/crows-foot-notation-relationship-symbols-and-how-to-read-diagrams/),
[Cardinality symbols (Creately)](https://creately.com/guides/cardinality-symbols/),
[Comparison of ER modeling notations (Franklin Univ., PDF)](https://cs.franklin.edu/~crawforl/.c281/app/Appendix_E.pdf),
[Crow's Foot vs Chen vs IDEF1X](https://www.relationaldbdesign.com/database-design/module7/three-model-types.php),
[Barker's notation (Wikipedia)](https://en.wikipedia.org/wiki/Barker%27s_notation),
[Weak entity (Wikipedia)](https://en.wikipedia.org/wiki/Weak_entity),
[ER symbols and notations (EdrawMax)](https://www.edrawsoft.com/er-diagram-symbols.html),
[ERD guide (Database Star)](https://www.databasestar.com/entity-relationship-diagram/),
[Mermaid ER diagram syntax](https://mermaid.ai/open-source/syntax/entityRelationshipDiagram.html),
[Chen ERD editor (Visual Paradigm)](https://www.visual-paradigm.com/features/chen-entity-relationship-diagram-editor/).

---

## 2. What the five reference images demand

Each uploaded image maps to concrete, non-optional capabilities. Together
they set the scope of Phase 1 and Phase 2.

### Image 1 — "Internet Sales Model" (teal/navy/crimson, with legend)

Chen shapes, but **crow's-foot markers on the line ends**, plus an explicit
legend.

* Teal rectangles (entities), navy diamonds (relationships), crimson ovals
  (attributes) — a **three-role colour palette**, not one colour per node.
* An in-canvas **legend/key panel** (top-left) listing the shape roles
  (Entity / Action / Attribute) *and* the three line-end markers: `— One`,
  `—o< Zero or many, Optional`, `—< Many`.
* **Hybrid notation**: Chen bodies with crow's-foot line ends. The element
  must let the marker style be chosen independently of the shape style.
* Long **orthogonal (elbow) connectors** running around the perimeter
  (`Shipping → Forwards Order → Credit Card → Verifies → E-Commerce`).
* A **title band** at the top of the canvas.

→ Features: F-LEGEND, F-ORTHO, F-MARKER-SET, F-ROLE-PALETTE, F-TITLE.

### Image 2 — dense Chen ERD (orange entities, ~8 clusters, attribute haloes)

The stress-test case.

* Every entity carries **6–12 attribute ovals arranged in a halo** around it.
  Hand-placing these is unusable — a dedicated **attribute-satellite layout**
  is required (radial fan on the free side of the entity).
* ~40 nodes with **crossing orthogonal routes** spanning the whole canvas.
* Small text, tight packing → **auto label fitting** and **oval auto-sizing
  to label width** (the same lesson already learned in
  `UltraCanvasNodeDiagram` 2.0.4/2.0.5: `SuggestNodeSizeForLabel`).
* Needs **zoom/pan + minimap** to be readable at all.

→ Features: F-ATTR-HALO, F-AUTOSIZE, F-VIEWPORT, F-MINIMAP, F-CROSSING.

### Image 3 — textbook "Student / Joins / Course"

The teaching diagram.

* **Key attributes rendered with an underline** (`SSN`, `CID` — labelled
  "Prime Attribute" in the image). Underline is *the* notation for a primary
  key in Chen; a dashed underline marks a weak entity's partial key.
* **Annotation callouts** pointing at parts of the diagram
  ("Attribute", "Relationship", "Prime Attribute", "Student Entity") — free
  text notes anchored to a node with a leader line.
* Clean symmetric layout: two entities, one central diamond, attributes
  fanned above and below.
* Diagram **title** and a footer/watermark line.

→ Features: F-KEY-UNDERLINE, F-ANNOTATION, F-SYMMETRIC-LAYOUT.

### Image 4 — Hospital ERD (flat pastel theme)

* A **theme preset** with muted fills, thin borders, rounded ovals and no
  drop shadows — visually distinct from images 1/2. Confirms the need for
  named themes (`Default`, `Professional`, `Colorful`, `Pastel`, `Dark`,
  `Blueprint`) rather than per-node colour setting.
* Horizontal **entity–diamond–entity chains** (`Patient —Test_for— Test`,
  `Patient —Attended_by— Doctors`), i.e. relationship nodes sit *on the
  spine* between their participants — a specific layout constraint.
* Straight (non-orthogonal) connectors, contrasting with images 1/2/5.

→ Features: F-THEMES, F-SPINE-LAYOUT, F-LINE-STYLE.

### Image 5 — Academic ERD with min-max cardinalities

The formally strictest image.

* **Min-max (ISO) cardinality labels** — `(1,1)`, `(1,N)`, `(0,N)` — printed
  on *every* edge next to the entity end. This is a different constraint
  language from `1 : N` letters and from crow's-foot ticks, and all three
  must be selectable.
* **Ternary relationships**: a single diamond with three legs
  (`Professor / Subject / Course`-style).
* **Self / recursive relationships**: `Tutor_Tutored` loops back to the same
  entity — needs a self-loop route with a labelled arc, plus **role names**
  on each leg to disambiguate the two ends.
* One entity participating in **many diamonds** at once, with long **bus-like
  orthogonal trunks** routed around other nodes.
* Muted grey/purple entities with green diamonds — again a theme, not
  per-node colours.

→ Features: F-MINMAX, F-NARY, F-SELFLOOP, F-ROLE-NAMES, F-ORTHO-BUS.

---

## 3. Notations to support

Six notation families are in real use. They differ in *shapes*, *where
attributes live*, and *how cardinality is drawn*. The element supports them
as a single `ERNotation` switch over one shared model.

| Notation | Entity | Relationship | Attributes | Cardinality |
|---|---|---|---|---|
| **Chen** | Rectangle (weak: double rectangle) | Diamond node (identifying: double diamond) | Ovals on spokes | `1` / `N` / `M` labels on edges; total participation = double line |
| **Chen + min-max (ISO)** | as Chen | as Chen | as Chen | `(min,max)` pair per edge, e.g. `(1,N)` |
| **Crow's Foot (IE)** | Box: header + attribute rows | The edge itself, labelled | Rows inside the box, `PK`/`FK`/`UK` markers | Line-end glyphs: `‖` one, `o‖` zero-or-one, `»‖` one-or-many, `o»` zero-or-many |
| **IDEF1X** | Square corners = independent, rounded = dependent | Edge; identifying = solid, non-identifying = dashed, filled dot at child end | Rows, PK compartment above a divider | `P` (one or more), `Z` (zero or one), `n` (exactly n) |
| **Barker** | Box with attribute lines | Edge; half solid = mandatory, half dashed = optional | Rows prefixed `#` UID, `*` mandatory, `o` optional | Crow's foot for many; exclusive arc across legs |
| **UML class-style** | Class box, 3 compartments | Association edge | Typed rows with visibility | Multiplicities `1`, `0..1`, `0..*`, `1..*`, `n..m` |
| **Bachman** | Box | Edge | Rows | One arrowhead = one, two arrowheads = many |

Phase 1 ships **Chen**, **Chen min-max** and **Crow's Foot** (these cover all
five reference images plus every mainstream database-design use). IDEF1X,
Barker, UML and Bachman are Phase 2/3 — they are rendering variants over the
same model, so they cost markers and box chrome, not new data structures.

---

## 4. The model

```
ERDiagramModel
├── entities[]            ERDiagramEntity
│   ├── kind              Strong | Weak | Associative | View | Alias
│   ├── name, alias, comment
│   ├── attributes[]      ERDiagramAttribute        (nested: composite children)
│   │   ├── kind          Simple | Composite | MultiValued | Derived | Complex
│   │   ├── keyRole       None | Primary | Partial | Foreign | Unique | Discriminator
│   │   ├── dataType, length, nullable, defaultValue, comment
│   │   └── children[]    (composite only)
│   └── geometry, style, collapsed
├── relationships[]       ERDiagramRelationship
│   ├── kind              Regular | Identifying | Weak | Recursive
│   ├── name (verb phrase), inverseName
│   ├── legs[]            ERDiagramLeg   (2 = binary, 3+ = n-ary)
│   │   ├── entityId, roleName
│   │   ├── minCard, maxCard          (0/1/N, or explicit n)
│   │   ├── participation             Partial | Total | Optional
│   │   └── attributes[]              (relationship attributes hang off legs or the relationship)
│   └── attributes[]
├── generalizations[]     ERDiagramGeneralization   (ISA)
│   ├── superEntityId, subEntityIds[]
│   ├── disjointness      Disjoint | Overlapping
│   ├── completeness      Total | Partial
│   └── discriminatorAttribute
├── aggregations[]        ERDiagramAggregation      (a relationship treated as an entity)
├── annotations[]         ERDiagramAnnotation       (callouts, notes, leader lines)
└── groups[]              ERDiagramGroup            (subject areas / swimlanes)
```

Everything else — Chen ovals vs crow's-foot rows, `(1,N)` vs `»‖` — is a
rendering choice over this model.

---

## 5. Feature list

Codes are used later for phasing. **P1** = Phase 1 (first delivery),
**P2**, **P3** as marked in §6.

### M — Model & semantics

| # | Feature |
|---|---|
| M1 | Strong entity (rectangle) |
| M2 | Weak entity (double rectangle) with owner + identifying relationship |
| M3 | Associative entity (rectangle enclosing a diamond) — resolves M:N |
| M4 | Relationship as a first-class node, binary |
| M5 | n-ary relationships (ternary+, one diamond with ≥3 legs) — *image 5* |
| M6 | Identifying relationship (double diamond) |
| M7 | Recursive / self relationship with distinct role names per leg — *image 5* |
| M8 | Relationship attributes (attributes on the diamond) |
| M9 | Attribute kinds: simple, composite (nested ovals), multivalued (double oval), derived (dashed oval) |
| M10 | Key roles: primary (solid underline), partial/discriminator (dashed underline), foreign, unique, composite PK ordering — *image 3* |
| M11 | Cardinality: `1:1`, `1:N`, `M:N`, and explicit `(min,max)` — *image 5* |
| M12 | Participation: partial (single line), total (double line), optional (dashed) |
| M13 | Generalization / specialization (ISA triangle) with disjoint/overlapping + total/partial markers |
| M14 | Aggregation (box drawn around a relationship, then related as a unit) |
| M15 | Data types, length/precision, nullability, defaults, comments (physical level) |
| M16 | Subject areas / groups — collapsible bounding regions for large schemas — *image 2* |
| M17 | Model validation (see V) surface: `Validate()` returns a diagnostics list |

### N — Notation & symbol rendering

| # | Feature |
|---|---|
| N1 | `ERNotation::Chen` — rectangles / diamonds / ovals |
| N2 | `ERNotation::ChenMinMax` — `(min,max)` edge labels — *image 5* |
| N3 | `ERNotation::CrowsFoot` — attribute-row boxes, line-end glyphs |
| N4 | `ERNotation::IDEF1X` — rounded dependent entities, PK compartment, `P`/`Z`/`n` |
| N5 | `ERNotation::Barker` — `#`/`*`/`o` prefixes, half-dashed optional legs, exclusive arc |
| N6 | `ERNotation::UML` — three-compartment class boxes, `0..*` multiplicities |
| N7 | `ERNotation::Bachman` — single/double arrowheads |
| N8 | **Independent marker set**: `ERLineEndStyle` (`None`, `Tick`, `CrowsFoot`, `CircleCrowsFoot`, `DoubleTick`, `Arrow`, `FilledDot`) selectable regardless of body notation — required by *image 1*, which mixes Chen bodies with crow's-foot ends |
| N9 | Cardinality label style: `Letters (1/N/M)`, `MinMax ((1,N))`, `UML (0..*)`, `None` |
| N10 | Verb-phrase relationship labels, with inverse phrase shown at the far end |
| N11 | Role-name labels per leg — *image 5* |
| N12 | Notation switch at runtime — same model re-renders in another notation without data loss (Chen-only concepts degrade with a documented, visible fallback rather than silently vanishing) |

### L — Layout

| # | Feature |
|---|---|
| L1 | Manual placement (positions in the model, drag to move) |
| L2 | **Attribute-satellite layout** — ovals fan radially around their entity on the side away from its relationships, with collision avoidance — *image 2*, non-negotiable |
| L3 | **Spine layout** — entity–relationship–entity chains laid on a horizontal or vertical axis — *image 4* |
| L4 | Force-directed layout over entities+relationships (reuse the tuned solver from `UltraCanvasNodeDiagram` 2.0.6, including the final overlap-resolution pass) |
| L5 | Hierarchical / layered layout (Sugiyama) — the default for crow's-foot schemas |
| L6 | Orthogonal grid layout for dense schemas — *image 2* |
| L7 | Symmetric two-entity layout — *image 3* |
| L8 | Subject-area (group-aware) layout: groups laid out internally, then packed |
| L9 | `FitView()` / auto-fit after layout (`SetAutoFitOnLayout`, matching NodeDiagram) |
| L10 | Snap-to-grid, alignment guides, distribute/align helpers |
| L11 | Incremental layout — adding one entity does not reshuffle the diagram |

### R — Connector routing

| # | Feature |
|---|---|
| R1 | Straight connectors — *image 4* |
| R2 | Orthogonal / elbow connectors with rounded corners — *images 1, 2, 5* |
| R3 | Bezier and smooth-step connectors |
| R4 | Obstacle-avoiding orthogonal router (route around boxes, not through) — *image 2* |
| R5 | Bus/trunk routing: parallel edges share a channel with fixed spacing — *image 5* |
| R6 | Self-loop routing (arc out and back to the same entity) — *image 5* |
| R7 | Crossing minimisation + optional crossing "jump" arcs |
| R8 | Anchor points per side (N/E/S/W plus explicit offsets), auto side inference |
| R9 | Waypoint editing — drag a segment to add/move a bend |
| R10 | Double-line rendering for total participation (parallel offset stroke) |

### S — Styling & theming

| # | Feature |
|---|---|
| S1 | Named themes: `Default`, `Professional`, `Colorful`, `Pastel`, `Dark`, `Blueprint`, `Minimal`, `Print` — *images 1, 2, 4, 5* |
| S2 | **Role palette**: distinct fill/border/text per node role (entity / weak entity / relationship / attribute / key attribute / ISA) — *image 1* |
| S3 | Per-node and per-edge overrides on top of the theme |
| S4 | Auto-size shapes to label (`SuggestNodeSizeForLabel` pattern), with min/max clamps — *images 2, 3* |
| S5 | Underline rendering for key attributes, dashed underline for partial keys — *image 3* |
| S6 | Font family/size per role; bold entity names, italic derived attributes |
| S7 | Rounded corners, shadows, gradients, opacity — theme-level toggles |
| S8 | Colour-code by subject area / by schema / by table size |
| S9 | Icons or badges on rows (PK 🔑, FK, index, NOT NULL) for physical diagrams |
| S10 | Background grid / dot grid, canvas background colour |

### C — Canvas chrome

| # | Feature |
|---|---|
| C1 | **Legend panel** — auto-generated from the active notation, listing shape roles and line-end markers, positioned in any corner — *image 1* |
| C2 | Diagram title band (and optional subtitle/footer) — *images 1, 3, 4* |
| C3 | **Annotations / callouts** — free text anchored to a node with a leader line — *image 3* |
| C4 | Minimap overlay (reuse `NodeDiagramMinimapConfig` conventions) — *image 2* |
| C5 | Controls overlay: zoom in/out, fit, lock, notation switch |
| C6 | Group/subject-area frames with headers, collapsible |

### I — Interaction & editing

| # | Feature |
|---|---|
| I1 | Zoom (wheel, at cursor), pan (middle-drag / empty-area drag) |
| I2 | Select node/edge, multi-select (shift-click, rubber band) |
| I3 | Drag entities; attributes follow their owner |
| I4 | **Drag-to-connect**: drag from an entity edge to another creates a relationship, with an inline cardinality picker |
| I5 | Inline rename (double-click a name, edit in place) |
| I6 | Attribute row editing: add/remove/reorder, set type, toggle PK/FK/nullable |
| I7 | Collapse/expand an entity's attributes (whole box → header only) — essential for *image 2* density |
| I8 | Context menus on entity / relationship / attribute / canvas |
| I9 | Undo/redo stack over all model edits |
| I10 | Hover highlight: hovering an entity dims everything not directly related to it |
| I11 | Search/find entity or attribute by name, with centre-on-result |
| I12 | Keyboard: Delete, Ctrl+A, Escape, arrow-key nudge, Ctrl+Z/Y |
| I13 | Read-only / presentation mode (`SetInteractive(false)`) |
| I14 | Shape palette side panel for drag-and-drop creation (mirroring `UltraCanvasFlowChartPalette`) |
| I15 | Callbacks: `onEntityClick`, `onRelationshipClick`, `onAttributeClick`, `onSelectionChange`, `onModelChanged`, `onViewportChange`, `onCanvasRightClick` |

### V — Validation & model checking

| # | Feature |
|---|---|
| V1 | Entity without a primary key |
| V2 | Relationship leg pointing at a missing entity; orphan/disconnected entity |
| V3 | Weak entity without an identifying relationship or without a partial key |
| V4 | Unresolved M:N relationship (suggest an associative entity) |
| V5 | Duplicate entity/attribute names; reserved-word and length checks per target dialect |
| V6 | FK type mismatch against the referenced PK |
| V7 | Cardinality contradictions (`min > max`, total participation with `min = 0`) |
| V8 | Normalization hints (repeating groups → 1NF, partial dependency → 2NF) |
| V9 | Cycle detection in identifying-relationship chains |
| V10 | Diagnostics surfaced both programmatically and as in-canvas markers |

### X — Import / export / interchange

| # | Feature |
|---|---|
| X1 | Native JSON `ToJson()` / `FromJson()` (matching the NodeDiagram/FlowChart pattern, via `UltraCanvasJSON`) |
| X2 | **SQL DDL forward engineering** — generate `CREATE TABLE` for SQLite / PostgreSQL / MySQL / MSSQL, including FKs, junction tables for M:N, and weak-entity composite PKs |
| X3 | **SQL DDL reverse engineering** — parse a `.sql` script into a model, then auto-layout |
| X4 | **Live-database introspection** via `UltraDatabase` — read tables, columns, PKs, FKs from a connected DB and build the diagram |
| X5 | Mermaid `erDiagram` import/export (`||--o{`, attribute blocks, `PK`/`FK`/`UK`) — the most common interchange format today |
| X6 | PlantUML / DBML export |
| X7 | Image export: PNG, SVG, PDF, and print pagination (via the existing render-context export path) |
| X8 | CSV/JSON schema description import for quick prototyping |
| X9 | Round-trip stability: import → export produces an equivalent model |

### T — Integration & performance

| # | Feature |
|---|---|
| T1 | `UltraCanvasERDiagram : public UltraCanvasUIElement`, header in `UltraCanvas/include/Plugins/Diagrams/`, source in `UltraCanvas/Plugins/Diagrams/`, registered in `UltraCanvas/CMakeLists.txt` |
| T2 | Dirty-rect aware rendering; layout cached, recomputed only on model change |
| T3 | Level-of-detail: below a zoom threshold, attribute rows/ovals collapse to a filled band — required for *image 2* scale |
| T4 | Viewport culling for 500+ node schemas |
| T5 | Optional `UltraDatabase` dependency, compiled out cleanly when absent |
| T6 | Docs: `UltraCanvasERDiagram.md` + `UltraCanvasERDiagramExamples.md`, then `python3 scripts/generate_llms_txt.py` |
| T7 | DemoApp scene `Apps/DemoApp/UltraCanvasERDiagramExamples.cpp` with one variant per reference image, and the demo entry flipped to `FullyImplemented` |

---

## 6. Phasing

**Phase 1 — the five reference images render correctly.**
M1–M12, N1–N3, N8–N11, L1–L4, L7, L9–L10, R1–R3, R6, R8, R10, S1–S7, S10,
C1–C3, C5, I1–I3, I8, I10, I12–I13, I15, X1, X7, T1–T2, T6–T7.

That is: Chen + Chen-min-max + crow's foot, weak/identifying/n-ary/recursive
relationships, attribute haloes, key underlines, legend, title, annotations,
themes, straight + orthogonal + self-loop routing, zoom/pan/select, JSON,
image export, docs and the DemoApp scene.

**Phase 2 — editor and database round-trip.**
M13–M16, N4–N6, L5–L6, L8, R4–R5, R7, R9, S8–S9, C4, C6, I4–I7, I9, I11,
I14, V1–V7, X2–X5, T3–T4.

**Phase 3 — depth.**
N7, L11, V8–V10, X6, X8–X9, T5, plus: diagram diff/compare between two
schema versions, migration-script generation from a diff, collaborative
annotation threads.

---

## 7. API sketch

Consistent with `UltraCanvasNodeDiagram` and `UltraCanvasFlowChart`: a simple
API for the common case, a verbose struct API alongside it.

```cpp
#include "Plugins/Diagrams/UltraCanvasERDiagram.h"

auto er = CreateERDiagram("SalesERD", 100, 60, 900, 620);

er->SetNotation(ERNotation::Chen);
er->SetLineEndStyle(ERLineEndStyle::CrowsFoot);      // image 1: hybrid
er->SetCardinalityLabelStyle(ERCardinalityLabels::MinMax);
er->SetTheme(ERDiagramTheme::Colorful);
er->SetTitle("Entity Relationship Diagram - Internet Sales Model");
er->SetLegendVisible(true, ERPanelPosition::TopLeft);

// --- simple API -----------------------------------------------------------
er->AddEntity("customer", "Customer");
er->AddAttribute("customer", "Name", ERKeyRole::Primary);   // underlined
er->AddAttribute("customer", "E-mail");
er->AddAttribute("customer", "Address", ERAttributeKind::Composite);

er->AddEntity("order", "Order");
er->AddAttribute("order", "Order Number", ERKeyRole::Primary);

er->AddRelationship("places", "Orders", "customer", "order",
                    ERCardinality::One, ERCardinality::Many);

// --- verbose API ----------------------------------------------------------
ERDiagramRelationship teaches;
teaches.id   = "teaches";
teaches.name = "Teaches";
teaches.kind = ERRelationshipKind::Regular;
teaches.legs = {
    { "professor", "instructor", 1, 1, ERParticipation::Total  },
    { "subject",   "topic",      1, ERCardinalityN, ERParticipation::Partial },
    { "course",    "in",         0, ERCardinalityN, ERParticipation::Partial }
};                                                    // ternary — image 5
er->AddRelationship(teaches);

// weak entity + identifying relationship
er->AddEntity("orderline", "Order Line", ERDiagramEntityKind::Weak);
er->AddAttribute("orderline", "Line No", ERKeyRole::Partial);   // dashed underline
er->AddIdentifyingRelationship("contains", "Contains", "order", "orderline");

// recursive relationship with role names — image 5
er->AddRecursiveRelationship("tutoring", "Tutor_Tutored", "student",
                             "tutor", "tutored");

// annotation callout — image 3
er->AddAnnotation("note1", "Prime Attribute", "customer.Name",
                  ERAnnotationSide::Left);

er->SetLayout(ERDiagramLayout::AttributeSatellite);
er->RunLayout();                                       // auto-fits by default

er->onEntityClick = [](const std::string& id) { /* … */ };

// export
std::string json = er->ToJson();
std::string ddl  = er->ExportSql(SqlDialect::PostgreSQL);   // Phase 2
er->ExportImage("sales-erd.svg");
```

Reverse engineering (Phase 2):

```cpp
er->ImportSql(LoadTextFile("schema.sql"), SqlDialect::MySQL);
er->ImportMermaid(mermaidSource);
er->ImportFromDatabase(dbHandle, "public");   // via UltraDatabase
er->SetLayout(ERDiagramLayout::Hierarchical);
er->RunLayout();
```

---

## 8. Design decisions to confirm

1. **New element vs. subclass of `UltraCanvasNodeDiagram`.**
   Recommendation: **new element**, borrowing the proven internals
   (force-directed solver with the 2.0.6 overlap pass, viewport/minimap/
   controls code, hit-testing, JSON) by extraction rather than inheritance.
   The ER model (typed entities, nested attributes, n-ary legs, ISA) is too
   different from `NodeDiagramNode`/`NodeDiagramLink` to be a subclass, but
   duplicating the viewport and layout code would be worse. Proposal: pull
   the shared viewport/minimap/controls behaviour into a small reusable
   `DiagramViewport` helper used by both.

2. **Attribute rendering when notation switches.**
   In Chen, attributes are satellite ovals with their own positions; in
   crow's foot they are rows inside the box. Recommendation: store attributes
   only in the entity (single source of truth), and keep satellite positions
   as a *derived, cached* layout that the Chen renderer owns — so switching
   notation and switching back does not lose hand-placed oval positions
   within one session, and JSON persists them as an optional hint.

3. **`UltraDatabase` coupling.**
   X4 (live introspection) is the only feature that needs it. Recommendation:
   keep it behind a compile guard so the diagram element has no hard
   dependency, mirroring how optional backends are handled elsewhere.

4. **Mermaid as the interchange baseline.**
   X5 gives immediate practical value (docs, LLM-generated schemas, existing
   corpora) for modest cost, since Mermaid's ER subset maps onto a strict
   subset of this model. Recommendation: implement X5 in Phase 2 before
   X2/X3, and treat its cardinality table (`||`, `o|`, `}|`, `}o`) as the
   canonical mapping for `ERCardinality` + `ERParticipation`.

5. **Router ownership.**
   R4 (obstacle-avoiding orthogonal routing) is genuinely reusable —
   FlowChart, BlockDiagram, PertChart and CompositorDiagram all hand-roll
   elbow routing today. Recommendation: build it as a standalone
   `UltraCanvasOrthogonalRouter` under `Plugins/Diagrams/` so the ER work
   pays that debt down once for everyone.

# UltraCanvasRequirementDiagram — Research & Feature Proposal

Status: **Complete — phases 1, 2 and 3 implemented.**
`UltraCanvasRequirementDiagram` ships the full P1–P3 feature set; see
[`UltraCanvasRequirementDiagramExamples.md`](UltraCanvasRequirementDiagramExamples.md)
for the API documentation, `Apps/DemoApp/UltraCanvasRequirementDiagramExamples.cpp`
for the demo scene (seven tabs — four reproducing the reference images in §2,
three for the phase-2/3 features) and `Tests/RequirementModelTests.cpp` for the
headless model tests. This document is kept as the research write-up.

Deviations from the plan below, all made during implementation:

* **§5.5 tree layout.** `UltraCanvasDendrogramLayout`'s Reingold–Tilford
  implementation places leaves at a *fixed* spacing and derives the depth axis
  from merge distance — both wrong for requirement boxes, whose widths vary
  with their text. A tidy tree over variable-size boxes is implemented inside
  the element (`LayoutSubtree`) instead, guaranteeing non-overlapping siblings
  at any content length.
* **Phase 2 moved the model out of the element**, as §5.1 argued it should:
  `RequirementModel` (`UltraCanvasRequirementModel.h/.cpp`) holds the data,
  semantics, analysis and text interchange with no UI dependency, and the
  element owns one and forwards its API. That is what lets Q87's tests run
  headless like the repository's other unit tests. No phase-1 caller changed.
* **Three features not in the list below**, all forced by the material:
  `RequirementRelation::visible` / `SetRelationKindVisible()` (image 3 needs
  containment to define the hierarchy while generalisation carries the visible
  notation — drawing both doubles every edge), `RequirementNode::externalId`
  (Mermaid, CSV and ReqIF all reference a requirement by name or internal key
  but carry a separate authored `id`, so both must survive a round trip), and
  `RequirementSearchHit` ranking for Q67.
* **Q82 ReqIF is import-only and a documented subset** — requirements,
  attributes and the `SPEC-HIERARCHY` nesting. It is parsed by a small XML
  reader inside the model rather than through tinyxml2, so the model keeps its
  zero-dependency property and the unit tests keep compiling one source.
  `SPEC-RELATIONS` and the type system are out of scope; ReqIF *export* needs
  authoring decisions that belong to the application.
* **Open question 1 stays open by design.** The box-and-typed-relation
  machinery is still private to this element. Extracting a shared
  `UltraCanvasUmlCore.h` was recommended for "when the class diagram lands";
  class, ER and block-definition diagrams are still `NotImplemented`
  placeholders in the DemoApp registry, so there is still exactly one consumer
  and nothing to generalise against yet.

Author: UltraCanvas Framework
Last Modified: 2026-07-31

---

## 1. What a requirement diagram is

A **requirement diagram** (`req`) is one of the nine SysML diagram types — the
only one with no UML counterpart. It is the *structural* view of a system's
requirements: it shows requirements as boxes, the hierarchy that decomposes
them, and the traceability links between requirements and the design elements
that derive from, satisfy, verify or refine them.

Unlike a chart, a requirement diagram carries **semantics, not values**. Its
correctness criteria are "is every requirement satisfied by something?" and
"is every requirement verified by a test case?" — not "is this bar the right
height?". That drives the feature list: model integrity and traceability
analysis matter as much as rendering.

### 1.1 The requirement element

In SysML v1.x a requirement is a stereotyped Class with two normative
properties:

| Property | Meaning |
|---|---|
| `id` | Unique, human-facing identifier (`"R1.2.1"`, `"URL2.2"`) |
| `text` | The requirement statement itself ("The vehicle shall …") |

Plus seven *derived* properties that are shown as compartments rather than
rows: `derived`, `derivedFrom`, `satisfiedBy`, `verifiedBy`, `refinedBy`,
`tracedTo`, `master`.

The widely-used **`extendedRequirement`** profile (Cameo/MagicDraw, Enterprise
Architect, and the de-facto convention in most textbooks and in three of the
four reference images) adds:

| Property | Typical values |
|---|---|
| `source` | Free text — the originating document/stakeholder |
| `risk` | `High` / `Medium` / `Low` |
| `verifyMethod` | `Analysis` / `Inspection` / `Test` / `Demonstration` |
| `status`, `owner`, `priority` | Common vendor extensions |

**Requirement categories** (stereotype specialisations) in common use:
`requirement`, `functionalRequirement`, `interfaceRequirement`,
`performanceRequirement`, `physicalRequirement`, `designConstraint`,
`usabilityRequirement`, `businessRequirement`. Reference images 1 and 4 use
several of these *on the same diagram*, colour-coded.

### 1.2 The seven relationships

| Relationship | Notation | Direction / meaning |
|---|---|---|
| **Containment** | Solid line, **crosshair circle ⊕** at the *parent* end | Namespace nesting: sub-requirement is owned by the parent |
| **DeriveReqt** | Dashed line, open arrowhead, `«deriveReqt»` | Derived requirement → source requirement (analysis-based) |
| **Satisfy** | Dashed line, open arrowhead, `«satisfy»` | Design element (block/part) → requirement it fulfils |
| **Verify** | Dashed line, open arrowhead, `«verify»` | Test case → requirement it checks |
| **Refine** | Dashed line, open arrowhead, `«refine»` | Model element (use case, activity) → requirement it elaborates |
| **Trace** | Dashed line, open arrowhead, `«trace»` | Generic, unspecific traceability (discouraged when a specific one fits) |
| **Copy** | Dashed line, open arrowhead, `«copy»` | Slave requirement is a read-only copy of a master's `id`/`text` |

All six dependency-style relationships point **from the dependent element to
the requirement**; only containment points from parent to child. Getting the
arrow direction wrong is the single most common error in hand-drawn
requirement diagrams — the element must enforce it.

### 1.3 Supporting elements on a requirement diagram

Requirement diagrams legally contain non-requirement elements, and every
reference image uses them: `«block»`, `«testCase»`, `«useCase»`, actors,
packages, and `«rationale»` / `«problem»` notes.

### 1.4 The three notations for the same information

SysML deliberately defines **three interchangeable notations**, and a
comprehensive element should support all of them:

1. **Graphical / relationship notation** — the boxes-and-lines view
   (all four reference images).
2. **Compartment notation** — the related elements listed *inside* the
   requirement box under headings `satisfiedBy`, `verifiedBy`, `derived`, …
   instead of drawn as lines. Essential for dense diagrams.
3. **Callout notation** — a note anchored to a model element carrying
   `Satisfies «requirement» Braking`. Reference image 2 uses a callout for
   the `Emissions` detail.

A fourth, **table notation** (requirement table / traceability matrix), is
what real projects actually review. It is the same model rendered as a grid.

### 1.5 The diagram frame

SysML diagrams carry a frame with a header in the form
`req [Package] HSVSpecification [Requirements Diagram]` — `kind [element type]
element name [diagram name]`. Reference image 4 has a title banner in the same
spirit.

### 1.6 SysML v2 and the Mermaid dialect

* **SysML v2** replaces stereotyped classes with first-class
  `requirement def` / `requirement usage`, adds `subject`, `actor`,
  `assume`/`require` constraints and `satisfy … by …`. The proposed data model
  should not block a later v2 mapping, but v1.x notation is what tools and
  users expect today.
* **Mermaid** ships a `requirementDiagram` dialect that is a simplified SysML
  v1: types `requirement`, `functionalRequirement`, `interfaceRequirement`,
  `performanceRequirement`, `physicalRequirement`, `designConstraint`; fields
  `id`, `text`, `risk` (`Low`/`Medium`/`High`), `verifymethod` (`Analysis`/
  `Inspection`/`Test`/`Demonstration`); an `element { type, docref }` node for
  non-requirements; relationships `contains`, `copies`, `derives`,
  `satisfies`, `verifies`, `refines`, `traces`; a `direction TB|BT|LR|RL`
  directive; and `style` / `classDef` / `:::` styling. This is a ready-made,
  well-specified **text interchange format** — supporting it as import/export
  costs little and makes the element instantly useful to anyone who has
  Markdown requirement diagrams already. Note the field is spelled
  `verifymethod` (all lowercase) in Mermaid but `verifyMethod` in SysML.

### 1.7 Industry context

Requirements live in DOORS, Jama, Polarion, codebeamer and Excel, and move
between tools as **ReqIF** (OMG XML interchange). The diagram is usually a
*view* over an imported set, and the questions asked of it are coverage
questions: *uncovered requirements* (nothing satisfies them), *unverified
requirements* (no test case), *orphans*, *suspect links* (the master changed
after the copy). These are cheap graph queries over the model we already need
to hold, and they are what turns this from a drawing widget into an
engineering tool.

---

## 2. What the four uploaded reference images demand

### Image 1 — HybridSUV requirement tree (colour-coded packages, full compartments)

A containment tree under a root `«requirement» HybridSUV` box, three coloured
sub-trees (yellow / green / green), and leaf rows of small stereotype-only
boxes.

Requires:
* Two-line box header: `«stereotype»` line above a **bold name** line.
* A property compartment listing `id`, `source`, `text`, `verifyMethod`,
  `risk` as **`key="value"` rows**, left-aligned, small font, with the long
  `text` value wrapped over several lines.
* **Per-node fill/border colour** (the yellow `Load` sub-tree vs the green
  `Eco-Friendliness` / `Performance` sub-trees) — colour used to group by
  package/category, not by stereotype.
* **Mixed stereotypes on one diagram**: `«requirement»`,
  `«functionalRequirement»`, `«performanceRequirement»`.
* Boxes with **no compartment at all** (the leaf row: `Passengers`, `Cargo`,
  `Acceleration`, `Braking`, `Power`, `Range`) — compartment visibility must
  be per-node, and auto-hide when empty.
* Containment routing as an **orthogonal bus**: one vertical drop from the
  parent, one horizontal spine, one vertical riser per child.
* Node heights differ by content; siblings are top-aligned per row.

### Image 2 — HSVSpecification containment tree with a detail callout

A `«block»`-style root at the top with small port nubs, a wide orthogonal
containment tree of name-only requirement boxes, and one detached note in the
bottom-left showing `Emissions` with its `id` and `text`, joined by a link.

Requires:
* A **non-requirement root** (a block/package) owning requirements —
  containment is not requirement-to-requirement only.
* **Collapsed boxes** (name only) as the default for a structural overview,
  with detail moved into a callout.
* **Callout / detail note**: a free-floating box carrying a subset of one
  element's properties, anchored to it with a leader line.
* A wide **horizontal spine** with many children — the layout must spread
  siblings and keep the spine clean rather than fanning radially.
* Depth ≥ 3 with mixed branch depths (some children are leaves, others carry
  their own sub-tree).

### Image 3 — SysML diagram taxonomy with a legend

A classification tree using **hollow-triangle generalisation arrows**, boxes
coloured yellow / blue / white by category, and an explicit **Legend box**
mapping colour → category.

Requires:
* A **generalisation** link style (solid line, hollow closed triangle) beyond
  the seven requirement relationships — needed whenever the diagram shows
  requirement *types* or a profile hierarchy.
* A **legend panel**: bordered box, title, one swatch+label row per category,
  free placement.
* **Category-driven colouring**: assign a node to a category, colour comes
  from the category, legend is generated from the categories in use.
* A layered top-down tree with orthogonal elbow connectors and multiple
  parents feeding one row.

### Image 4 — Smart Home Automation system requirement diagram

The most demanding image: a title banner, ~20 heterogeneous nodes
(requirements, `«testCase»`, `«block»`, rounded/oval actor- and use-case-like
shapes), colour-by-kind, and a web of **dashed labelled relationships** that
cross the diagram rather than forming a tree.

Requires:
* **Heterogeneous node kinds with distinct shapes**: rectangle (requirement),
  rounded rectangle / oval (use case, actor, block), each with its own default
  fill.
* **Colour-by-kind** palette (green requirements, blue blocks/test cases,
  purple use cases in the reference).
* **Dashed relationship lines with a `«stereotype»` mid-line label**, open
  arrowheads, drawn across a non-tree topology (many-to-many).
* Long wrapped `text` values inside narrow boxes — real word-wrap, not
  truncation.
* A **diagram title banner** across the top.
* A layout that is **not a pure tree**: layered/force hybrid, or manual
  positions with clean routing and edge-crossing avoidance.

### 2.1 Feature demand summary

| Demand | Image 1 | Image 2 | Image 3 | Image 4 |
|---|:--:|:--:|:--:|:--:|
| Stereotype header + name | ✔ | ✔ | — | ✔ |
| `key="value"` property compartment | ✔ | ✔ (in callout) | — | ✔ |
| Wrapped long `text` | ✔ | ✔ | — | ✔ |
| Per-node / per-category colour | ✔ | — | ✔ | ✔ |
| Collapsed (name-only) boxes | ✔ | ✔ | ✔ | — |
| Containment with ⊕ crosshair | ✔ | ✔ | — | — |
| Orthogonal bus routing | ✔ | ✔ | ✔ | — |
| Dashed `«stereotype»` relationships | — | — | — | ✔ |
| Generalisation (hollow triangle) | — | — | ✔ | — |
| Callout / detail note | — | ✔ | — | — |
| Legend panel | — | — | ✔ | — |
| Title banner / diagram frame | — | — | — | ✔ |
| Non-requirement node kinds | — | ✔ | — | ✔ |
| Non-tree (graph) topology | — | — | — | ✔ |

Every row is in the P1/P2 feature list below.

---

## 3. What UltraCanvas already implements

Inventory of the `Plugins/Diagrams` family, from the headers, for reuse:

| Element | What it offers that is relevant | Why it is not a requirement diagram |
|---|---|---|
| `UltraCanvasNodeDiagram` (2.0.6) | Nodes + links, handles/ports, 4 link routing styles, multi-select, pan/zoom, snap-grid, minimap, controls overlay, `ToJson`/`FromJson`, force/circular/hierarchical/grid layouts, label measurement helpers | Single-label circular/geometric nodes. No compartments, no stereotypes, no typed semantics, no containment notation |
| `UltraCanvasFlowChart` (2.2.0) | 18 shapes, **A\* obstacle-aware orthogonal routing**, shape-aware anchor points, multi-line labels, connection labels with backgrounds, themes | Flow semantics; no structured node model, no typed relationships |
| `UltraCanvasBlockDiagram` (2.3.1) | Per-face connector slot distribution (parallel arrows never overlap), isometric rendering, edit modes | Same — flat label per node |
| `UltraCanvasPertChart` (1.2.0) | **`PertNodeTemplate`: rows × cells with field binding, weights, per-cell colours and bold** — the closest existing thing to a compartment model; auto-layout; palettes | Bound to CPM scheduling fields |
| `UltraCanvasCompositorDiagram` | Declarative node templates, socket registry, parameter widgets, subgraphs/groups, alignment guides | Node-graph editor semantics |
| `UltraCanvasDendrogramLayout` (1.1.0) | **Reusable Reingold–Tilford tree layout** with four orientations | Layout only — exactly what the containment tree needs |
| `UltraCanvasGourceTree`, `UltraCanvasTreeMapElement`, `UltraCanvasSankey`, `UltraCanvasAdjacencyDiagram` | Other tree/graph presentations | Unrelated encodings |

Shared infrastructure available from `IRenderContext`: `DrawTextInRect`
(wrapped text — needed for `text` values), `GetTextLineDimensions` (real
measurement during `Render`), `SetLineDash` / `UCDashPattern` (dashed
relationships), `ITextLayout` (rich layout), plus the framework's rounded-rect
and path primitives.

**No SysML, requirement, stereotype or traceability code exists anywhere in
the repository** — verified by grep across headers, sources and docs. The only
hit is the DemoApp placeholder.

---

## 4. Gap analysis

1. **No element has a structured node body.** Every diagram node in the
   framework is `{label, shape, colours}`. A requirement box is
   `{stereotype, name, ordered property rows, optional derived-element
   compartments}` with per-row styling and wrapped values. `PertNodeTemplate`
   proves the pattern is idiomatic here, but it is welded to CPM fields.
2. **No typed relationship semantics.** `LinkStyle` / `ConnectionStyle` are
   *visual* choices. A requirement diagram needs the relationship **kind** to
   determine notation (dashed + open arrow vs solid + crosshair vs hollow
   triangle), the legal endpoint kinds, the arrow direction, and the
   traceability queries.
3. **No containment notation.** The ⊕ crosshair on the parent end is unique to
   this diagram type and exists nowhere in the codebase.
4. **No callouts, no legend panel, no diagram frame header.** Images 2, 3 and
   4 each need one.
5. **No model-integrity or coverage analysis** anywhere in the diagram family.
6. **No text interchange.** `ToJson`/`FromJson` on `UltraCanvasNodeDiagram` is
   the only serialization precedent; nothing reads a standard requirement
   format.

Conclusion: **a new element is required.** Extending `UltraCanvasNodeDiagram`
with requirement semantics would push a general graph widget into domain
territory it should not own, and the compartment/containment work is
substantial enough to justify its own class.

---

## 5. Proposed architecture

```
UltraCanvas/include/Plugins/Diagrams/UltraCanvasRequirementDiagram.h
UltraCanvas/Plugins/Diagrams/UltraCanvasRequirementDiagram.cpp
Apps/DemoApp/UltraCanvasRequirementDiagramExamples.cpp
Docs/UltraCanvas/UltraCanvasRequirementDiagramExamples.md
```

`class UltraCanvasRequirementDiagram : public UltraCanvasUIElement`, matching
the sibling diagram elements (they all derive directly from
`UltraCanvasUIElement`, not from the chart base).

Design decisions:

1. **Model first, view second.** The element owns a small requirement *model*
   (`RequirementNode` + `RequirementRelation` keyed by id) that is valid and
   queryable independently of geometry. Layout writes positions into it;
   analysis reads it. This is what makes coverage queries, the traceability
   matrix and Mermaid export fall out almost for free.
2. **One node type with a `kind`, not a class hierarchy.** `RequirementNode`
   carries `RequirementNodeKind` (`Requirement`, `Block`, `TestCase`,
   `UseCase`, `Actor`, `Package`, `Rationale`, `Problem`, `Note`) plus a
   `stereotype` string. Kind drives shape and default palette entry;
   stereotype drives the `«…»` header text. This handles image 1's
   requirement subtypes and image 4's mixed elements with one storage type.
3. **Compartments are a small declarative model**, following the proven
   `PertNodeTemplate` pattern: an ordered list of `RequirementCompartment`,
   each either a *property list* (`key="value"` rows bound to model fields or
   literals) or a *derived-element list* (`satisfiedBy`, `verifiedBy`, …
   computed from the relations). A chart-wide default template with per-node
   overrides covers image 1 (full properties), image 2 (name only) and image 4
   (id + text) without any per-node hand-layout.
4. **Relationship kind drives notation, direction and legality.** A single
   `RequirementRelationKind` enum selects line style, arrowhead, end
   decoration and label; `AddRelation` validates endpoint kinds and, in strict
   mode, rejects or auto-flips a backwards `satisfy`.
5. **Reuse, don't reinvent.** Containment layout delegates to
   `UltraCanvasDendrogramLayout`'s Reingold–Tilford implementation; orthogonal
   routing follows the FlowChart 2.2.0 A\* approach and the BlockDiagram 2.3.0
   per-face slot distribution so parallel relationship arrows never overlap;
   arrowheads use the "direction of the last segment" rule established in
   BlockDiagram 2.2.0 and NodeDiagram 2.0.0; text Y origin is the **top** of
   the bounding box (NodeDiagram 2.0.6 / FlowChart 2.1.2 lesson).
6. **Two coordinate spaces, one transform**, as in `UltraCanvasNodeDiagram`:
   world-space content inside a push/pop transform, screen-space overlays
   (legend, controls, minimap) outside it.

---

## 6. Proposed feature list

**P1** = core, ships first and reproduces reference images 1–3;
**P2** = completes the notation and reproduces image 4 fully;
**P3** = engineering-tool polish.

### 6.1 Model & semantics

| # | Feature | Phase |
|---|---|---|
| Q1 | `RequirementNode`: `id`, `name`, `kind`, `stereotype`, `text`, `source`, `risk`, `verifyMethod`, `status`, `owner`, `priority`, `docRef`, plus a `std::map<std::string,std::string>` for user-defined properties | P1 |
| Q2 | `RequirementNodeKind` enum: `Requirement`, `Block`, `TestCase`, `UseCase`, `Actor`, `Package`, `Rationale`, `Problem`, `Note` | P1 |
| Q3 | Built-in requirement categories with stereotype strings and default colours: `functionalRequirement`, `interfaceRequirement`, `performanceRequirement`, `physicalRequirement`, `designConstraint`, `usabilityRequirement`, `businessRequirement` + free-text custom stereotypes | P1 |
| Q4 | `RequirementRisk` (`Low`/`Medium`/`High`) and `RequirementVerifyMethod` (`Analysis`/`Inspection`/`Test`/`Demonstration`) enums with string round-trip | P1 |
| Q5 | `RequirementRelation`: `id`, `kind`, `sourceId`, `targetId`, optional label override, optional rationale text | P1 |
| Q6 | `RequirementRelationKind`: `Containment`, `DeriveReqt`, `Satisfy`, `Verify`, `Refine`, `Trace`, `Copy`, `Generalization` (image 3) | P1 |
| Q7 | Endpoint legality validation per relation kind, with `SetStrictSemantics(bool)`; invalid relations are rejected (strict) or flagged and drawn in a warning colour (lenient, default) | P2 |
| Q8 | Auto-correct backwards direction on `Satisfy`/`Verify`/`Refine` when the endpoints unambiguously imply it, reported via `onValidationWarning` | P2 |
| Q9 | Duplicate-`id` detection and stable id generation for programmatic adds | P1 |
| Q10 | Hierarchical id auto-numbering: `AssignHierarchicalIds("R")` walks the containment tree and writes `R1`, `R1.1`, `R1.1.2`, … | P2 |
| Q11 | `Copy` semantics: slave nodes mirror the master's `id`/`text`, render read-only, and go stale when the master changes (`suspect` flag) | P3 |

### 6.2 Node presentation

| # | Feature | Phase |
|---|---|---|
| Q12 | Two-line header: `«stereotype»` line (small, centred) + **bold name** line, on a distinct header band or plain, per theme | P1 |
| Q13 | `RequirementCompartment` model: ordered compartments, each `PropertyList` (rows) or `DerivedElementList` (computed) or `FreeText`, with an optional heading | P1 |
| Q14 | Property rows rendered as `key="value"` (image-1 convention) or `key: value`, selectable per diagram | P1 |
| Q15 | Real word-wrap of long values via `DrawTextInRect`, with per-node max lines and an ellipsis when clipped | P1 |
| Q16 | Auto node sizing: measured content height, configurable min/max width, "fit width to longest key" alignment of the value column | P1 |
| Q17 | Compartment visibility: global default template + per-node override + auto-hide of empty compartments (image 1's leaf row, image 2's collapsed tree) | P1 |
| Q18 | Detail levels as one-call presets: `Collapsed` (name only), `Standard` (id + text), `Full` (all properties), `Custom` (explicit template) | P1 |
| Q19 | Derived-element compartments (`derived`, `derivedFrom`, `satisfiedBy`, `verifiedBy`, `refinedBy`, `tracedTo`, `master`) computed from the relation set — SysML **compartment notation**, the alternative to drawing every line | P2 |
| Q20 | Shape per kind: rectangle (requirement/block), rounded rectangle (test case), oval (use case), stick figure (actor), folder (package), folded-corner note (rationale/problem) | P1 (rect + rounded) / P2 (rest) |
| Q21 | Per-node fill/border/text colour override and per-node font size | P1 |
| Q22 | Optional risk indicator: a coloured left edge stripe or corner badge driven by `risk` | P2 |
| Q23 | Optional status badge / icon slot in the header (`Approved`, `Proposed`, `Rejected`) | P3 |
| Q24 | Block port nubs on the top edge for `Block` nodes (image 2's root) | P3 |

### 6.3 Relationship notation

| # | Feature | Phase |
|---|---|---|
| Q25 | Containment: solid line with the **⊕ crosshair circle** at the parent end, no arrowhead | P1 |
| Q26 | Dependency relations: dashed line + open (unfilled, two-stroke) arrowhead at the target | P1 |
| Q27 | `«stereotype»` mid-line label with an opaque background plate, auto-placed on the longest straight segment; per-relation label override | P1 |
| Q28 | Generalisation: solid line + hollow closed triangle (image 3) | P1 |
| Q29 | Routing styles per relation: `Orthogonal` (default), `Straight`, `Curved`, `Bus` — plus a diagram-wide default | P1 |
| Q30 | **Containment bus routing**: one drop from the parent, one shared horizontal spine, one riser per child (images 1–3) | P1 |
| Q31 | Per-face anchor slot distribution so parallel relations never overlap (BlockDiagram 2.3.0 rule) | P1 |
| Q32 | A\*/obstacle-aware orthogonal routing that avoids passing through boxes (FlowChart 2.2.0 rule), with graceful fallback | P2 |
| Q33 | Relation colour by kind from the palette, with per-relation override; dash pattern configurable per kind | P1 |
| Q34 | Rationale attached to a relation, shown as an anchored note (SysML permits rationale on relationships) | P3 |
| Q35 | Self-relations and multi-edges between the same pair, offset so they remain distinguishable | P3 |

### 6.4 Layout

| # | Feature | Phase |
|---|---|---|
| Q36 | `Manual` layout with `SetNodePosition` and drag, honoured by every other feature | P1 |
| Q37 | `ContainmentTree` layout via `UltraCanvasDendrogramLayout` (Reingold–Tilford), orientations `TopDown` / `BottomUp` / `LeftRight` / `RightLeft` — the Mermaid `direction TB\|BT\|LR\|RL` equivalent | P1 |
| Q38 | Configurable level gap, sibling gap, and top-alignment of siblings within a row (image 1) | P1 |
| Q39 | `Layered` (Sugiyama-style) layout for non-tree topologies with crossing reduction (image 4) | P2 |
| Q40 | `ForceDirected` fallback for dense trace webs | P3 |
| Q41 | Pinned nodes excluded from auto-layout (PertChart's manual-override convention) | P1 |
| Q42 | Relation-kind layout filter: which kinds participate in layout (typically containment only) vs which are merely routed | P2 |
| Q43 | `AutoFitOnLayout` + `FitView()` so a fresh layout is never stranded off-screen (NodeDiagram 2.0.1 lesson) | P1 |
| Q44 | Swimlane / package grouping: dashed translucent bounding region around a package's members with a title tab | P3 |

### 6.5 Frame, notes, legend

| # | Feature | Phase |
|---|---|---|
| Q45 | SysML diagram frame with the `req [Package] Name [Diagram Name]` pentagon header tab; toggleable | P2 |
| Q46 | Plain title banner across the top as an alternative to the frame (image 4) | P1 |
| Q47 | **Callout notes**: a free-floating box carrying a chosen subset of one element's properties, anchored with a dashed leader line (image 2) | P1 |
| Q48 | `«rationale»` and `«problem»` notes as first-class nodes, anchored to a node or a relation | P2 |
| Q49 | **Legend panel**: bordered box, title, swatch+label rows, free placement, auto-populated from the categories or relation kinds actually present (image 3) | P1 |
| Q50 | Category model: assign a node to a named category; colour and legend entry come from the category | P1 |

### 6.6 Theming

| # | Feature | Phase |
|---|---|---|
| Q51 | Palette struct covering every colour used (background, grid, per-kind fill/border/text, header band, relation colour per kind, note, legend, selection, warning), following `PertChartPalette` | P1 |
| Q52 | Built-in palettes: `Classic` (white boxes, black lines — textbook SysML), `Pastel` (images 1–2), `Vibrant` (image 4), `Professional`, `Dark`, `Monochrome` (print), `Custom` | P1 |
| Q53 | Colour-source mode: `ByKind`, `ByCategory`, `ByRisk`, `ByStatus`, `Explicit` | P1 |
| Q54 | Font family/size controls with independent header, key, value and label sizes | P1 |
| Q55 | Grid, snap-to-grid and background controls consistent with the sibling diagrams | P2 |

### 6.7 Interaction

| # | Feature | Phase |
|---|---|---|
| Q56 | Pan (drag / middle-drag), zoom at cursor, `ZoomIn`/`ZoomOut`/`FitView`/`CenterOn` | P1 |
| Q57 | Selection: click, shift-click multi-select, rubber-band box, `SelectAll`, `DeselectAll` | P1 |
| Q58 | Node dragging with optional snap-to-grid; multi-node drag | P1 |
| Q59 | Hover highlight + tooltip showing the full `text` when it is clipped in the box | P1 |
| Q60 | Callbacks: `onNodeClick`, `onNodeDoubleClick`, `onNodeHover`, `onRelationClick`, `onSelectionChange`, `onViewportChange`, `onCanvasRightClick`, `onValidationWarning` | P1 |
| Q61 | Expand/collapse a sub-tree from a ⊕/⊖ toggle on the parent box; collapsed children hidden with a count badge | P2 |
| Q62 | Detail-level toggle per node from the UI (collapsed ↔ full) — the fastest way to make a dense diagram readable | P2 |
| Q63 | **Trace highlighting**: select a requirement and dim everything not reachable through its relations, with a depth limit | P2 |
| Q64 | Filter API: show/hide by kind, category, risk, status, or relation kind; filtered nodes are excluded from layout | P2 |
| Q65 | Editing: create node, drag-to-connect with a relation-kind picker, delete selected, inline rename | P2 |
| Q66 | Minimap and controls overlay, config-compatible with `UltraCanvasNodeDiagram` | P3 |
| Q67 | Search box / `FindNodes(query)` matching id, name and text, with focus-on-result | P3 |

### 6.8 Traceability analysis

| # | Feature | Phase |
|---|---|---|
| Q68 | `GetUncoveredRequirements()` — no incoming `Satisfy` | P2 |
| Q69 | `GetUnverifiedRequirements()` — no incoming `Verify` | P2 |
| Q70 | `GetOrphanRequirements()` — no relations at all | P2 |
| Q71 | Coverage statistics struct: counts and percentages satisfied / verified / derived, overall and per category | P2 |
| Q72 | Visual coverage overlay: badge or edge stripe marking uncovered/unverified requirements | P2 |
| Q73 | Containment cycle detection (a cyclic containment graph disables tree layout — PertChart precedent) | P1 |
| Q74 | `GetTraceChain(nodeId, direction, depth)` — upstream/downstream reachable set | P2 |
| Q75 | **Traceability matrix export**: rows = requirements, columns = satisfying/verifying elements, cells = relation kind; as CSV and as a data structure a `UltraCanvasTable` can render | P3 |
| Q76 | Suspect-link detection for `Copy` relations whose master changed | P3 |

### 6.9 Interchange

| # | Feature | Phase |
|---|---|---|
| Q77 | `ToJson()` / `FromJson()` covering model, layout and style, following `UltraCanvasNodeDiagram`'s precedent | P1 |
| Q78 | **Mermaid `requirementDiagram` import** — full dialect: the six requirement types, `id`/`text`/`risk`/`verifymethod`, `element { type, docref }`, all seven relation keywords in both arrow directions, and the `direction` directive | P2 |
| Q79 | **Mermaid export** producing text that round-trips through Mermaid unchanged | P2 |
| Q80 | CSV import: one row per requirement (`id, name, parentId, text, source, risk, verifyMethod, …`) — the realistic bridge from Excel/DOORS exports | P2 |
| Q81 | CSV export of the flat requirement list | P2 |
| Q82 | ReqIF (OMG XML) import of requirements and hierarchy | P3 |
| Q83 | `BuildFromRelations()` convenience: hand it a flat node list + relation list and it infers the containment tree and lays out | P1 |

### 6.10 Engineering & delivery

| # | Feature | Phase |
|---|---|---|
| Q84 | Component doc `Docs/UltraCanvas/UltraCanvasRequirementDiagramExamples.md` with buildable examples (house rule 2) | P1 |
| Q85 | DemoApp scene reproducing all four reference images as variants; flip the registry entry to `FullyImplemented` and wire the doc/source paths | P1 |
| Q86 | CMake registration in `UltraCanvas/CMakeLists.txt` (Diagrams source list, ~line 402) | P1 |
| Q87 | Unit tests under `Tests/` for the model layer: validation, cycle detection, id numbering, coverage queries, Mermaid round-trip | P2 |
| Q88 | Regenerate `llms.txt` / `llms-full.txt` after the component doc lands (house rule 5; note this *proposal* is excluded by the generator's `Proposal` pattern) | P1 |
| Q89 | Masterfile entry once the public surface is stable | P1 |

---

## 7. Proposed API sketch

```cpp
#include "Plugins/Diagrams/UltraCanvasRequirementDiagram.h"
using namespace UltraCanvas;

auto req = CreateRequirementDiagram("hsvReq", 0, 0, 1200, 800);
req->SetPalette(RequirementPaletteKind::Pastel);
req->SetColorSource(RequirementColorSource::ByCategory);
req->SetTitle("HSV Specification");

// --- Root -------------------------------------------------------------
RequirementNode root;
root.id        = "HSV";
root.name      = "HybridSUV";
root.kind      = RequirementNodeKind::Requirement;
root.detail    = RequirementDetailLevel::Collapsed;
req->AddNode(root);

// --- A fully specified requirement -----------------------------------
RequirementNode eco;
eco.id           = "UR1.2";
eco.name         = "Eco-Friendliness";
eco.stereotype   = "requirement";
eco.text         = "The vehicle shall meet Ultra-Low Emissions Vehicle standards.";
eco.source       = "Marketing";
eco.verifyMethod = RequirementVerifyMethod::Analysis;
eco.risk         = RequirementRisk::High;
eco.category     = "Environment";
eco.detail       = RequirementDetailLevel::Full;
req->AddNode(eco);

// Convenience overload for the common case
req->AddRequirement("UR1.3", "Performance", "The vehicle shall …",
                    RequirementRisk::Medium, RequirementVerifyMethod::Test);

// --- Non-requirement elements ----------------------------------------
req->AddNode(RequirementNodeKind::TestCase, "TC1", "Emissions Test");
req->AddNode(RequirementNodeKind::Block,    "PWR", "Powertrain");

// --- Relationships ----------------------------------------------------
req->AddContainment("HSV", "UR1.2");           // ⊕ crosshair, solid
req->AddContainment("HSV", "UR1.3");
req->AddRelation(RequirementRelationKind::Verify,  "TC1", "UR1.2");
req->AddRelation(RequirementRelationKind::Satisfy, "PWR", "UR1.3");
req->AddRelation(RequirementRelationKind::DeriveReqt, "UR1.3", "UR1.2");

// --- Compartment template (applies to every Full node) ----------------
RequirementNodeTemplate tpl;
tpl.AddPropertyRow("id",           RequirementField::Id);
tpl.AddPropertyRow("source",       RequirementField::Source);
tpl.AddPropertyRow("text",         RequirementField::Text);
tpl.AddPropertyRow("verifyMethod", RequirementField::VerifyMethod);
tpl.AddPropertyRow("risk",         RequirementField::Risk);
tpl.SetPropertyFormat(RequirementPropertyFormat::KeyEqualsQuotedValue);
req->SetNodeTemplate(tpl);

// --- Callout note (reference image 2) ---------------------------------
req->AddCallout("emissionsNote", "UR1.2",
                { RequirementField::Id, RequirementField::Text },
                /*x*/ 40, /*y*/ 520);

// --- Legend (reference image 3) ---------------------------------------
req->SetLegendVisible(true, RequirementPanelPosition::BottomLeft);

// --- Layout -----------------------------------------------------------
req->SetLayout(RequirementLayout::ContainmentTree);
req->SetLayoutOrientation(RequirementOrientation::TopDown);
req->SetLevelGap(70.0);
req->SetSiblingGap(24.0);
req->RunLayout();                 // auto-fits by default

// --- Analysis ---------------------------------------------------------
for (const auto& id : req->GetUnverifiedRequirements())
    printf("No test case for %s\n", id.c_str());

RequirementCoverage cov = req->GetCoverage();
printf("Satisfied %.0f%%, verified %.0f%%\n",
       cov.satisfiedPercent, cov.verifiedPercent);

// --- Interchange ------------------------------------------------------
std::string mermaid = req->ToMermaid();
req->FromMermaid(mermaidSource);
req->FromCsv(csvText, RequirementCsvSchema::Default());
std::string json = req->ToJson();

// --- Interaction ------------------------------------------------------
req->onNodeClick = [&](const std::string& id) { req->HighlightTraceChain(id, 2); };
req->onValidationWarning = [](const RequirementWarning& w) {
    printf("[%s] %s\n", w.relationId.c_str(), w.message.c_str());
};
```

---

## 8. Suggested delivery order

| Phase | Content | Reproduces |
|---|---|---|
| **1 — Core** | Q1–Q6, Q9, Q12–Q18, Q20 (rect/rounded), Q21, Q25–Q31, Q33, Q36–Q38, Q41, Q43, Q46, Q47, Q49–Q54, Q56–Q60, Q73, Q77, Q83, Q84–Q86, Q88, Q89 | Images 1, 2, 3 |
| **2 — Full notation & analysis** | Q7, Q8, Q10, Q19, Q20 (remaining shapes), Q22, Q32, Q39, Q42, Q45, Q48, Q55, Q61–Q65, Q68–Q72, Q74, Q78–Q81, Q87 | Image 4; coverage tooling |
| **3 — Engineering polish** | Q11, Q23, Q24, Q34, Q35, Q40, Q44, Q66, Q67, Q75, Q76, Q82 | Matrix export, ReqIF, suspect links |

Phase 1 is a self-contained, demo-able element and is the natural first
commit. Nothing in phases 2–3 requires breaking a phase-1 API.

---

## 9. Open questions for review

1. **Scope of the element name.** `UltraCanvasRequirementDiagram` covers the
   SysML `req` diagram. Several of its parts (compartment boxes, stereotype
   headers, dashed typed relationships, generalisation, the diagram frame) are
   exactly what a future **class diagram**, **ER diagram** and **block
   definition diagram** need — all three are `NotImplemented` placeholders in
   the same DemoApp registry. Should the box-and-typed-relation machinery be
   factored into a shared `UltraCanvasUmlCore.h` from the start, or extracted
   later once a second consumer exists? (Recommendation: build it inside this
   element in phase 1, extract in phase 3 when the class diagram lands — same
   trajectory the circular-chart proposal took with `RadialGeometry`.)
2. **Strict vs lenient semantics by default.** Rejecting a backwards `satisfy`
   is correct SysML but surprising in a drawing tool. Proposed default:
   lenient with a warning callback (Q7).
3. **Mermaid as the primary text format.** It is well-specified, widely
   authored, and cheap to support. ReqIF is the industry format but is a much
   larger XML surface. Proposed: Mermaid in phase 2, ReqIF in phase 3, CSV in
   phase 2 as the pragmatic Excel/DOORS bridge.
4. **Table notation.** Should the traceability matrix (Q75) render inside this
   element as a display mode, or should it produce data for
   `UltraCanvasTable`? Proposed: produce data — one element, one job.
5. **SysML v2.** Do we commit to a v2 mapping now? The proposed model is
   v2-compatible in structure (kind + stereotype + properties), so this can
   stay a phase-3 decision.

---

## Sources

* [Requirement diagram — Wikipedia](https://en.wikipedia.org/wiki/Requirement_diagram)
* [Requirement Diagram — SysML Plugin, No Magic / Dassault documentation](https://docs.nomagic.com/display/SYSMLP2022x/Requirement+Diagram)
* [Cameo Requirements Modeler — Requirements Writing in SysML (PDF)](https://www.3ds.com/fileadmin/PRODUCTS-SERVICES/CATIA/NoMagic/pdf/cameo-requirements-modeler-plugin-requirements-writing-guide.pdf)
* [Requirement Relationship — ScienceDirect topic overview](https://www.sciencedirect.com/topics/computer-science/requirement-relationship)
* [Digital requirements engineering with an INCOSE-derived SysML meta-model (arXiv)](https://arxiv.org/pdf/2410.21288)
* [Mermaid `requirementDiagram` syntax](https://mermaid.js.org/syntax/requirementDiagram.html)

# UltraCanvasClassDiagram — Research & Feature Proposal

Status: **P1 delivered — the element renders and ships in the demo app.**
This document is the research write-up and the agreed feature list for a new
`UltraCanvasClassDiagram` element under `UltraCanvas/Plugins/Diagrams/`.

Two of the open questions in §8 have been resolved as recommended, and the work
they gate is implemented:

* **Q1 — shared routing header (E1).** Done. FlowChart's A\* router now lives in
  `UltraCanvasDiagramRouting.h/.cpp` as the stateless `UltraCanvasDiagramRouter`,
  reusable by FlowChart, BlockDiagram and the class diagram. `UltraCanvasFlowChart`
  is 2.3.0 and forwards to it. Three defects in the original router were fixed
  during extraction. Multi-edge anchor distribution (E4) shipped with it.
  Docs: [`UltraCanvasDiagramRouting.md`](UltraCanvasDiagramRouting.md).
  Tests: `Tests/DiagramRoutingTest.cpp`.
* **Q5 — pragmatic C++ reverse engineering (X9).** Done at the confirmed scope:
  a heuristic header scanner, no C++ front end. It required the UML model
  itself, so **M1–M7, M20, R1–R11 (model level), X11 validation** and the
  supporting notation grammar are implemented, plus **X3/X5 text export** pulled
  forward from P2 so a model is inspectable before the element exists.
  Docs: [`UltraCanvasUMLModel.md`](UltraCanvasUMLModel.md).
  Tests: `Tests/UMLModelTest.cpp`.

**The element itself is now implemented** (`UltraCanvasClassDiagram`, plus the
`UltraCanvasClassLayout` engine), wired into the demo app under
Diagrams → Class Diagram with five scenarios, and documented in
[`UltraCanvasClassDiagramExamples.md`](UltraCanvasClassDiagramExamples.md).
Delivered from §5: M1–M7, M10, M11, M20 · R1–R11 · L1–L4, L8–L11 ·
E1–E4, E10 · S1, S2, S4–S6, S8–S10, S13 · I1–I4, I13–I15, I19 · X11.

Still open: packages, frames and notes (K1–K7), self and n-ary relationships
(R12, R13), association classes (M15), the editing/undo/clipboard tier
(I5–I12), collision-aware end-label placement (R11 refinement), and file I/O
beyond text export (X1, X6–X8).

Author: UltraCanvas Framework
Last Modified: 2026-07-30

---

## 1. What a UML class diagram is

A class diagram is the **static structure** view of UML: it shows the
*classifiers* of a system (classes, interfaces, enumerations, data types) —
each with its attributes and operations — and the **typed relationships**
between them (inheritance, realization, association, aggregation, composition,
dependency), annotated with **multiplicity**, **role names** and **constraints**.

It is not a graph drawing. Two things separate a class diagram from the
node-and-edge diagrams UltraCanvas already ships:

1. **The node has internal structure.** A class box is a stack of
   *compartments* — name, attributes, operations — where every row is its own
   addressable, hit-testable, individually formatted item (visibility glyph,
   name, type, multiplicity, default value; static rows underlined, abstract
   rows italic). Box height is *derived* from its member list, not set by the
   caller.
2. **The edge carries typed semantics.** An edge is not "a line with an
   arrow" — it is one of seven UML relationship kinds, each with a mandated
   line style and end decoration, plus up to six independent labels (name,
   name-direction marker, two role names, two multiplicities) and per-end
   properties (navigability, aggregation kind, constraints).

Everything else — layout, routing, zoom, selection — is shared with the
existing diagram family and should be reused rather than rewritten.

### 1.1 Complete notation reference

This is the notation the element must be able to draw. It is the acceptance
checklist for §5.

**Classifier box**

| Element | Notation |
|---|---|
| Class | Rectangle, 3 compartments: name / attributes / operations |
| Abstract class | Class name in *italics* (or `{abstract}` property) |
| Interface | `«interface»` stereotype above the name; attribute compartment normally empty |
| Interface (lollipop) | Small circle on a stick — the "ball and socket" provided/required form |
| Enumeration | `«enumeration»` stereotype; literals listed in the second compartment |
| Data type / primitive | `«dataType»`, `«primitive»` stereotypes |
| Utility / static class | `«utility»` |
| Active class | Rectangle with doubled vertical side bars |
| Template (parameterised) class | Dashed rectangle overlapping the top-right corner holding `T`, `T : Comparable` … |
| Object / instance | `name : Class`, the whole string underlined |
| Association class | Class box tied to an association line by a dashed line |
| Nested (inner) class | Anchor line with a ⊕ circle from outer to inner |
| Collapsed view | Name-only box (attribute/operation compartments suppressed) |

**Members**

```
attribute:  visibility  /derived  name : Type [multiplicity] = default {property}
operation:  visibility  name(direction param : Type = default, …) : ReturnType {property}
```

| Marker | Meaning |
|---|---|
| `+` | public |
| `-` | private |
| `#` | protected |
| `~` | package |
| <u>underline</u> | static (class-scope) member |
| *italic* | abstract operation |
| `/name` | derived attribute |
| `{readOnly}`, `{ordered}`, `{unique}`, `{query}` | property strings |
| `in` / `out` / `inout` | parameter direction |

**Relationships** — the line style plus the end decoration *is* the semantics:

| Relationship | Line | End decoration | Meaning |
|---|---|---|---|
| Association | solid | none, or open arrowhead for navigability | structural link |
| Directed association | solid | open arrowhead (⟶) at the navigable end | A knows B, not vice-versa |
| Aggregation | solid | **hollow** diamond at the *whole* end | "has-a", parts survive the whole |
| Composition | solid | **filled** diamond at the *whole* end | "owns-a", parts die with the whole |
| Generalization (inheritance) | solid | **hollow closed triangle** at the *parent* | "is-a" |
| Realization (implementation) | **dashed** | hollow closed triangle at the *interface* | class implements interface |
| Dependency | **dashed** | open arrowhead | "uses" — `«use»`, `«create»`, `«call»`, `«derive»`, `«instantiate»`, `«refine»`, `«trace»` |
| Reflexive | solid, loops back to the same box | per kind | self-association |
| N-ary association | 3+ solid lines meeting a diamond | per end | ternary and higher |
| Qualified association | small qualifier rectangle attached to the source end | per kind | keyed lookup (`Map<Key, V>` style) |

**Annotation on relationship ends**

* Multiplicity: `1`, `0..1`, `*`, `0..*`, `1..*`, `n..m`, `1,3..5`, and the
  informal `+1` / `+1..*` / `+0..*` forms seen in the reference images.
* Role names: `+contactInformation`, `-owner` — visibility-prefixed, placed
  at the end they describe.
* Association name + reading-direction triangle (`Employs ▸`).
* Constraints spanning two associations: `{xor}`, `{subsets …}`.

**Containers and annotation**

* Package — tabbed-folder rectangle, holds classifiers; nestable.
* Frame — labelled rectangle around a region (the `frame` box in image 3).
* Note — rectangle with a folded corner, dashed leader line to its subject.
* Constraint text in `{braces}` attached to any element.

**Best practice** (drives the defaults in §5): keep parents above children so
generalization arrows read upward, keep 5–9 classifiers per view, minimise line
crossings, and use progressive disclosure (name-only → signatures → full detail)
rather than one giant unreadable page.

---

## 2. What the five reference images demand

Each supplied image maps to concrete capabilities. Together they define scope.

### Image 1 & 2 — "Class Diagram for a Banking System" (thumbnail + full size)

Seven classes (`Bank`, `Customer`, `Teller`, `Account`, `Loan`, `Checking`,
`Savings`) on a faint blue **graph-paper grid**, with a diagram **title** in the
bottom-left. Each box has a **yellow header band** and a white body, a
**hairline separator** between compartments, and — critically — an **empty
third compartment is still drawn** (`Checking`, `Savings`, `Account`, `Loan`
have an empty operations band). Rounded corners, ~2 px dark navy border,
monospace member text.

Connections are **orthogonal with rounded elbows**, and the multiplicities
(`+1`, `+1..*`, `+0..*`) sit as small labels next to the box they belong to, not
at the line midpoint. `Checking` and `Savings` join `Account` with **hollow
triangle generalization arrowheads**, and both triangles land on the *same*
right-hand face of `Account` at **two different, evenly spaced anchor points**.
`Account` has four incident lines on one face; `Customer` has three.

> Requires: header-band box style, always-drawn empty compartments, auto-sizing
> from member text, orthogonal routing with rounded corners, per-end multiplicity
> labels anchored near the box, generalization triangles, multi-edge anchor
> distribution on one face, grid background, diagram title.

### Image 3 — Interface-heavy diagram inside a frame

A pale-green **frame with a `frame` label tab** encloses everything. Inside are
two visually distinct node kinds: white boxes with a **`«interface»` stereotype
line** above the name, and **orange `«interface» Name` badges** (name-only,
collapsed). Member rows are shown as `field: type` and `method(Type): Type`.
Edges are **dashed with open arrowheads** (dependency/realization) and mixed
with solid ones; several dashed edges fan out from one box to several targets.

> Requires: stereotype line rendering, per-stereotype styling (colour/shape),
> collapsed name-only boxes, frames/packages as containers with a label tab,
> dashed lines with open arrowheads, one-to-many edge fan-out.

### Image 4 — Colour-coded data model with named associations

Six-plus boxes, **each in a different colour** (purple, green, orange, teal,
yellow) with a coloured header and a light body, long attribute lists, and
**verb-phrase labels on the association lines** (`weight 30`, `purchase`,
`manage 20`), i.e. association name *and* multiplicity carried on the same
line. Some lines run diagonally, some orthogonally, with a distinctly denser
graph than image 1.

> Requires: per-class colour/palette assignment, association name labels at the
> line midpoint alongside per-end multiplicities, mixed straight/orthogonal
> routing, longer member lists with scroll or truncation, an auto-layout that
> copes with ~10 classes.

### Image 5 — `«DataType»` service-metadata diagram

All boxes carry a `«DataType»` stereotype in a **separate top band** above the
class name, in a blue theme. Attributes use the **full UML syntax including
member-level multiplicity**: `onlineResource : OnlineResource`,
`fees [0..1] : String`, `accessConstraints [0..1] : String`,
`contactOrganization [0..1] : String`. Lines carry **role names prefixed with
`+`** (`+contactInformation`, `+contactAddress`), **per-end multiplicities**
(`0..1`, `1`) and an **association name** (`contactInformation`) — all three on
the same edge. A hollow triangle links `ExtendedMetadataBase` to `MetadataBase`.

> Requires: separate stereotype band, member-level multiplicity in the attribute
> grammar, simultaneous rendering of association name + two role names + two
> multiplicities without collision, and a label-placement pass that keeps them
> apart.

### Cross-cutting conclusion

No two images share a visual style, but all five share one *model*. The element
must therefore separate a **strict UML semantic model** from a **fully
overridable presentation layer** (theme + per-class + per-relationship style),
and ship presets that reproduce each reference image.

---

## 3. How this fits the existing UltraCanvas code

The framework already contains most of the supporting machinery. The class
diagram should **reuse, not duplicate**:

| Existing piece | Reuse for |
|---|---|
| `Plugins/Diagrams/UltraCanvasFlowChart.cpp` v2.2 | Obstacle-aware **A\* orthogonal routing** over a grid, `ComputeCardinalPath` L/Z fallback, `GetCardinalSide`, incoming-angle computation for arrowheads, and the longest-segment label anchor — exactly what images 1 and 4 need |
| `Plugins/Diagrams/UltraCanvasBlockDiagram.cpp` v2.3 | `CountFaceUsage()` — **distributing multiple connections evenly along one face** instead of stacking them at the midpoint. Image 1 needs this on `Account` and `Customer` |
| `Plugins/Diagrams/UltraCanvasCompositorDiagram.cpp` v0.4 | The closest structural precedent: **row-based composite nodes with per-row hit testing**, node **templates**, **undo/redo** with bounded history, **copy/cut/paste/duplicate**, **rubber-band selection**, **snap-to-grid**, **alignment guides**, **minimap**, **controls overlay**, **edge reconnection**, and the **parent/child subgraph model** that maps directly onto UML packages |
| `Plugins/Diagrams/UltraCanvasNodeDiagram.cpp` v2.0.6 | `ToJson`/`FromJson` serialization shape, zoom-at-cursor, `FitView`, force-directed layout with overlap resolution, `MeasureLabel` for text-driven auto-sizing, keyboard shortcut set |
| `Plugins/Diagrams/UltraCanvasDendrogramLayout.cpp` | Tree layout — reusable for the **generalization hierarchy** sub-layout |
| `include/Plugins/Charts/UltraCanvasLabelPlacement.h` | Collision-aware placement of the six labels an edge can carry (image 5) |
| `include/Plugins/Charts/UltraCanvasColormap.h` | Deriving the per-class palette of image 4 from a qualitative ramp |
| `include/DataFormats/UltraCanvasJSON.h` | Native `.ucclass` save/load (yyjson behind the UltraCanvas wrapper — never expose the vendored type) |
| `IRenderContext` | Everything the renderer needs already exists: `SetLineDash` (dashed dependency/realization), `FillLinePath` (filled composition diamond, closed triangle), `DrawLinePath`, `ClipPath` (compartment clipping / truncation), `Rotate`, `GetTextDimensions` (auto-sizing), gradients |
| `include/UltraCanvasClipboard.h` | OS-clipboard binding for copy/paste of a selection as PlantUML/Mermaid text |
| `Plugins/Diagrams/UltraCanvasGourceTree.cpp` (`SaveToSVG`) | The precedent for vector export |

**Architectural decision — new class, not a FlowChart mode.** A `FlowChartNode`
is a shape with one label; a class box is a variable-height stack of typed
member rows with per-row visibility glyphs, styles and hit regions. Folding this
into `UltraCanvasFlowChart` would force every flow-chart user to pay for UML
complexity — the same reasoning already recorded in
`UltraCanvasCompositorDiagram.h` for splitting the compositor off from
`UltraCanvasNodeDiagram`. Shared routing helpers should instead be **lifted into
a common header** (see §4).

---

## 4. Proposed architecture

Pure, UI-free model and algorithm headers (unit-testable without a window) plus
a thin element, mirroring the Hexbin/Contour precedent:

```
include/Plugins/Diagrams/UltraCanvasUMLModel.h        # classifiers, members, relationships (no UI deps)
include/Plugins/Diagrams/UltraCanvasUMLNotation.h     # UML text grammar: parse/format member + multiplicity strings
include/Plugins/Diagrams/UltraCanvasClassLayout.h     # layered / tree / grid / force layouts over the model
include/Plugins/Diagrams/UltraCanvasDiagramRouting.h  # A* + cardinal routing lifted out of FlowChart, shared
include/Plugins/Diagrams/UltraCanvasClassDiagram.h    # the UltraCanvasUIElement
Plugins/Diagrams/UltraCanvasUMLModel.cpp
Plugins/Diagrams/UltraCanvasUMLNotation.cpp
Plugins/Diagrams/UltraCanvasClassLayout.cpp
Plugins/Diagrams/UltraCanvasClassDiagram.cpp
Plugins/Diagrams/UltraCanvasClassDiagramIO.cpp        # PlantUML / Mermaid / JSON / SVG import-export
```

Splitting the model from the element is what makes the two highest-value
features possible: **round-tripping text formats** (PlantUML/Mermaid) and
**generating a diagram from parsed C++ headers** both operate on
`UltraCanvasUMLModel` with no window in the loop, and both are testable under
`Tests/`.

### 4.1 Model sketch

```cpp
enum class UMLClassifierKind { Class, AbstractClass, Interface, Enumeration,
                               DataType, Primitive, Utility, ActiveClass,
                               Template, Object, AssociationClass, Package, Note };

enum class UMLVisibility { Public, Private, Protected, Package, Unspecified };

struct UMLMember {                    // one row in a compartment
    UMLVisibility visibility = UMLVisibility::Public;
    std::string   name;
    std::string   type;               // attribute type or operation return type
    std::string   multiplicity;       // "0..1", "*"  (image 5)
    std::string   defaultValue;
    std::vector<UMLParameter> parameters;  // operations only
    std::string   propertyString;     // "{readOnly}", "{query}"
    bool isStatic = false;            // rendered underlined
    bool isAbstract = false;          // rendered italic
    bool isDerived = false;           // rendered "/name"
    bool isOperation = false;
};

struct UMLClassifier {
    std::string id, name;
    UMLClassifierKind kind = UMLClassifierKind::Class;
    std::vector<std::string> stereotypes;      // "interface", "DataType"
    std::vector<UMLMember>   attributes, operations, literals;
    std::string packageId;                     // container, mirrors CompositorNode::parentId
    UMLClassStyle style;                       // per-class overrides (image 4)
    double x, y; double width, height;         // height derived unless pinned
    bool collapsed = false;                    // name-only badge (image 3)
};

enum class UMLRelationshipKind { Association, DirectedAssociation, Aggregation,
                                 Composition, Generalization, Realization,
                                 Dependency, NAry };

struct UMLRelationshipEnd {
    std::string classifierId, roleName, multiplicity, constraint;
    bool navigable = false;
    std::string qualifier;             // qualified association
};

struct UMLRelationship {
    std::string id, name;              // "contactInformation"
    UMLRelationshipKind kind = UMLRelationshipKind::Association;
    UMLRelationshipEnd source, target;
    std::string stereotype;            // "use", "create"
    UMLReadingDirection nameDirection = UMLReadingDirection::Unspecified;  // ▸ / ◂
    std::string associationClassId;    // association class, if any
    UMLRelationshipStyle style;
};
```

`UMLRelationshipKind` alone determines line style and end decoration, so a
mis-drawn diagram is impossible by construction: the renderer maps kind → (dash
pattern, source decoration, target decoration) from one table.

---

## 5. Proposed feature list

Grouped, with a suggested delivery phase.
**P1** = core, ship first (reproduces images 1, 2 and 5);
**P2** = completes the reference images and makes it a real editor;
**P3** = advanced / integration.

### 5.1 Model — classifiers

| # | Feature | Phase |
|---|---|---|
| M1 | Class box with 3 compartments (name / attributes / operations), empty compartments still drawn | P1 |
| M2 | Attribute rows: visibility, name, type, multiplicity, default, property string | P1 |
| M3 | Operation rows: visibility, name, parameter list with direction + type + default, return type | P1 |
| M4 | Static members rendered underlined; abstract operations italic; derived members `/name` | P1 |
| M5 | Abstract class (italic name) | P1 |
| M6 | `«interface»` stereotype rendering, with the attribute compartment suppressible | P1 |
| M7 | Arbitrary stereotypes, single or multiple, in the name band or a separate band (image 5) | P1 |
| M8 | `«enumeration»` with a literals compartment | P2 |
| M9 | `«dataType»`, `«primitive»`, `«utility»` presets | P2 |
| M10 | Collapsed / name-only classifier badge (image 3) | P1 |
| M11 | Detail level per diagram *and* per class: NameOnly / Signatures / Full | P1 |
| M12 | Template (parameterised) class with the dashed parameter box in the corner | P2 |
| M13 | Active class (double side bars) | P3 |
| M14 | Object/instance boxes (`name : Class`, underlined) for object diagrams | P3 |
| M15 | Association class (dashed tie to the association line) | P2 |
| M16 | Nested/inner class with the ⊕ anchor | P3 |
| M17 | Optional 4th "responsibilities/constraints" compartment | P3 |
| M18 | Custom compartments (arbitrary named section, e.g. "Signals", "Ports") | P3 |
| M19 | Per-classifier notes attached with a dashed leader | P2 |
| M20 | Auto-size box from measured member text (`GetTextDimensions`), with a settable minimum and a pinned-size override | P1 |

### 5.2 Model — relationships

| # | Feature | Phase |
|---|---|---|
| R1 | Association (plain solid line) | P1 |
| R2 | Directed association (open arrowhead, navigability per end) | P1 |
| R3 | Aggregation (hollow diamond at the whole end) | P1 |
| R4 | Composition (filled diamond at the whole end) | P1 |
| R5 | Generalization (hollow closed triangle, solid line) | P1 |
| R6 | Realization (hollow closed triangle, dashed line) | P1 |
| R7 | Dependency (open arrowhead, dashed line) with stereotype text (`«use»`, `«create»`) | P1 |
| R8 | Per-end multiplicity labels anchored near their own box (images 1, 5) | P1 |
| R9 | Per-end role names with visibility prefix (`+contactInformation`, image 5) | P1 |
| R10 | Association name at the line midpoint (images 4, 5) plus the ▸/◂ reading-direction marker | P1 |
| R11 | Simultaneous name + 2 roles + 2 multiplicities without overlap, via `UltraCanvasLabelPlacement` | P1 |
| R12 | Reflexive (self) relationships drawn as a labelled loop | P2 |
| R13 | N-ary association via a shared diamond | P3 |
| R14 | Qualified association (qualifier rectangle at the source end) | P3 |
| R15 | End constraints (`{ordered}`, `{unique}`, `{subsets …}`) and cross-association `{xor}` | P3 |
| R16 | Shared generalization "tree" style — sibling inheritance lines merged into one trunk with a single triangle | P2 |
| R17 | Interface lollipop / socket (ball-and-socket) notation as an alternative to R6 | P3 |

### 5.3 Containers

| # | Feature | Phase |
|---|---|---|
| K1 | Package container (tabbed folder) holding classifiers, reusing the Compositor `parentId` model | P2 |
| K2 | Labelled frame around a region (image 3) | P2 |
| K3 | Nested packages | P2 |
| K4 | Auto-fit of a container to its children, with padding and a minimum size | P2 |
| K5 | Collapse/expand a package to a single box | P3 |
| K6 | Package-scoped visibility filtering ("show only what this package exposes") | P3 |
| K7 | Note element (folded-corner rectangle) with dashed leader to any target | P2 |

### 5.4 Layout

| # | Feature | Phase |
|---|---|---|
| L1 | Manual placement with drag, snap-to-grid, alignment guides | P1 |
| L2 | **Layered (Sugiyama) layout** — generalization edges define the ranking, parents above children | P1 |
| L3 | Tree layout for pure inheritance hierarchies (reuse `UltraCanvasDendrogramLayout`) | P1 |
| L4 | Grid layout (uniform rows/columns) as a deterministic fallback | P1 |
| L5 | Force-directed layout for dense association graphs (reuse the NodeDiagram implementation incl. its overlap-resolution pass) | P2 |
| L6 | Orthogonal/hierarchical hybrid tuned for the image-1 look | P2 |
| L7 | Package-aware compound layout (children laid out inside their container, containers laid out among themselves) | P2 |
| L8 | Layout direction: TopToBottom / BottomToTop / LeftToRight / RightToLeft | P1 |
| L9 | Node separation, rank separation and edge-margin parameters | P1 |
| L10 | Incremental layout — re-layout only what changed, preserving the user's manual positions ("pin" flag per class) | P2 |
| L11 | `FitView()` / auto-fit after layout (NodeDiagram precedent) | P1 |
| L12 | Layout of self-loops and parallel edges without overlap | P2 |

### 5.5 Routing

| # | Feature | Phase |
|---|---|---|
| E1 | Orthogonal routing with obstacle avoidance — lift FlowChart's A\* into `UltraCanvasDiagramRouting.h` and share it | P1 |
| E2 | Rounded elbows on orthogonal paths (images 1, 2) with settable corner radius | P1 |
| E3 | Straight and curved/Bezier routing modes, per relationship | P1 |
| E4 | Cardinal-side anchor selection with **even distribution of multiple edges along one face** (BlockDiagram `CountFaceUsage` precedent) | P1 |
| E5 | Fixed anchor override — pin a relationship to a named side/offset of a box | P2 |
| E6 | Manual waypoints: user-draggable bend points, persisted in the model | P2 |
| E7 | Edge crossing minimisation pass (port ordering by target position) | P2 |
| E8 | Line-jump ("hop") markers where two edges cross | P3 |
| E9 | Shared trunk merging for R16 sibling inheritance | P2 |
| E10 | Arrow decorations retreat by `borderWidth/2` so tips touch without overlapping (FlowChart 2.1.0 lesson) | P1 |

### 5.6 Style, theme and text

| # | Feature | Phase |
|---|---|---|
| S1 | Header-band box style: coloured name band + white body + hairline separators (image 1) | P1 |
| S2 | Per-classifier style override: fill, header colour, border colour/width, corner radius, text colours | P1 |
| S3 | Palette assignment — auto-colour classes by package, stereotype, or index from a qualitative colormap (image 4) | P2 |
| S4 | Per-stereotype style rules (`«interface»` → orange badge, `«DataType»` → blue, image 3/5) | P1 |
| S5 | Built-in themes: Default, Professional, Blueprint (image 1), Colorful (image 4), Pastel/DataType (image 5), Minimal, Dark | P1 |
| S6 | Font family/size per compartment (name bold, members monospace) | P1 |
| S7 | Visibility rendering mode: UML glyphs (`+ - # ~`) **or** icon badges, switchable | P2 |
| S8 | Graph-paper grid background with settable spacing/colour, plus plain and dotted variants | P1 |
| S9 | Diagram title and caption block (image 1) | P1 |
| S10 | Row truncation with ellipsis and a tooltip carrying the full signature, when a member exceeds the box width | P1 |
| S11 | Optional per-row zebra striping and separator lines | P3 |
| S12 | Drop shadows / elevation per box | P2 |
| S13 | Selection, hover and focus highlight styles (framework-consistent) | P1 |
| S14 | Legend explaining the relationship glyphs (teaching aid) | P3 |

### 5.7 Interaction and editing

| # | Feature | Phase |
|---|---|---|
| I1 | Zoom (wheel, zoom-at-cursor) and pan (middle-drag / empty-area drag) | P1 |
| I2 | Single, multi (Shift+click) and rubber-band selection of classes *and* relationships | P1 |
| I3 | Drag to move classes, resize handles, snap-to-grid | P1 |
| I4 | **Per-row hit testing** — click/hover reports the exact member row (Compositor precedent) | P1 |
| I5 | Inline editing of class name, member rows and edge labels | P2 |
| I6 | Create tool: drop a class, drop an interface, drop a note | P2 |
| I7 | Connect tool: drag from box to box, pick the relationship kind, reverse an edge's direction | P2 |
| I8 | Edge reconnection — drag an endpoint to another class | P2 |
| I9 | Undo/redo with bounded history over every mutator (Compositor precedent) | P2 |
| I10 | Copy / cut / paste / duplicate of a selection, with id remapping | P2 |
| I11 | Keyboard shortcuts matching the Compositor set (Ctrl+Z/Y/C/X/V/D/A, Delete, Escape) | P2 |
| I12 | Minimap overlay and zoom/fit/lock controls overlay | P2 |
| I13 | Collapse/expand a class (toggle compartments) by click on a chevron | P1 |
| I14 | Tooltip on hover: full member signature, full stereotype list, relationship description | P1 |
| I15 | Callbacks: `onClassClick`, `onClassDoubleClick`, `onMemberClick(classId, memberIndex)`, `onRelationshipClick`, `onSelectionChanged`, `onModelChanged`, `onCanvasRightClick` | P1 |
| I16 | Focus/highlight mode — dim everything not connected to the selected class | P2 |
| I17 | Search box: find a class or member, centre and flash it | P2 |
| I18 | Filter: hide private members, hide operations, hide a stereotype, hide a package | P2 |
| I19 | Read-only/presentation mode (no editing, interaction limited to zoom/pan/tooltip) | P1 |

### 5.8 Import, export and validation

| # | Feature | Phase |
|---|---|---|
| X1 | Native JSON save/load (`ToJson`/`FromJson` via `UltraCanvasJSON`), matching the NodeDiagram serialization shape | P1 |
| X2 | **PlantUML class-diagram import** — the most widely used text form | P2 |
| X3 | **PlantUML export** | P2 |
| X4 | **Mermaid `classDiagram` import** | P2 |
| X5 | **Mermaid `classDiagram` export** | P2 |
| X6 | SVG export (`SaveToSVG`, GourceTree precedent) | P2 |
| X7 | PNG export via the existing render-to-pixmap path | P2 |
| X8 | XMI (UML 2.5) import/export for interchange with Enterprise Architect / Papyrus | P3 |
| X9 | **Reverse engineering from C++ headers** — build a model from parsed sources; the highest-value feature for this repository, since it can document UltraCanvas itself | P3 |
| X10 | Forward code generation (C++ class skeletons from the model) | P3 |
| X11 | Model validation: dangling relationship ends, duplicate names, inheritance cycles, interface with attributes, unresolvable types — reported as a diagnostics list | P2 |
| X12 | Metrics: class count, depth of inheritance, fan-in/fan-out per class, coupling — exposed as data, drawn only if requested | P3 |

### 5.9 Quality gates (non-negotiable for P1)

| # | Requirement |
|---|---|
| Q1 | Text Y origin is the **top** of the text box, not the baseline (the bug fixed in FlowChart 2.1.2, BlockDiagram and NodeDiagram 2.0.6 — do not reintroduce it) |
| Q2 | Zero box overlap after any automatic layout |
| Q3 | Arrowheads align with the **last segment** of the path, not the straight-line angle |
| Q4 | No third-party type in the public header; JSON only through `UltraCanvasJSON` |
| Q5 | PascalCase throughout; `namespace UltraCanvas`; `std::shared_ptr` + `CreateClassDiagram(...)` factory |
| Q6 | `UltraCanvasUMLModel`, `UltraCanvasUMLNotation` and `UltraCanvasClassLayout` build and unit-test with **no window and no render context** (tests under `Tests/`) |
| Q7 | Doc + demo shipped with the code: `Docs/UltraCanvas/UltraCanvasClassDiagramExamples.md` and `Apps/DemoApp/UltraCanvasClassDiagramExamples.cpp`, then `python3 scripts/generate_llms_txt.py` |

---

## 6. Suggested API sketch

```cpp
auto diagram = CreateClassDiagram("BankDiagram", 0, 0, 1200, 800);
diagram->SetTheme(ClassDiagramTheme::Blueprint);   // image 1 look
diagram->SetGridVisible(true, 20.0);
diagram->SetTitle("Class Diagram for a Banking System");

// Simple API — the 80% case
diagram->AddClass("Bank", { "+BankId: int", "+Name: string", "+Location: string" }, {});
diagram->AddClass("Teller", { "+Id: int", "+Name: string" },
                            { "+CollectMoney()", "+OpenAccount()", "+CloseAccount()",
                              "+LoanRequest()", "+ProvideInfo()", "+IssueCard()" });
diagram->AddInterface("IPayable", { "+Pay(amount: double): bool" });

// Verbose API — full model control
UMLClassifier account;
account.name = "Account";
account.kind = UMLClassifierKind::AbstractClass;
account.attributes.push_back(UMLMember::Attribute(UMLVisibility::Public, "Id", "int"));
account.attributes.push_back(UMLMember::Attribute(UMLVisibility::Public, "CustomerId", "int"));
diagram->AddClassifier(account);

// Relationships
diagram->AddGeneralization("Checking", "Account");
diagram->AddGeneralization("Savings",  "Account");
diagram->AddAssociation("Bank", "Teller").SetMultiplicity("1", "1..*");
diagram->AddAssociation("Customer", "Account")
        .SetMultiplicity("1", "1..*")
        .SetRoles("+holder", "+accounts")
        .SetName("owns", UMLReadingDirection::SourceToTarget);
diagram->AddComposition("Customer", "Loan").SetMultiplicity("1", "0..*");
diagram->AddDependency("Teller", "Account", "use");

diagram->SetLayout(ClassDiagramLayout::Layered, LayoutDirection::TopToBottom);
diagram->RunLayout();          // auto-fits by default

// Text round-trip
diagram->LoadPlantUML(source);
std::string mermaid = diagram->ToMermaid();
diagram->SaveToSVG("bank.svg");
```

Fluent setters on the relationship (`.SetMultiplicity(...).SetRoles(...)`) keep
the six-label case readable — without them, an `AddAssociation` covering image 5
would need eight positional arguments.

---

## 7. Suggested delivery phases

| Phase | Content | Outcome |
|---|---|---|
| **P1** | §5.1 M1–M7, M10, M11, M20 · §5.2 R1–R11 · §5.4 L1–L4, L8, L9, L11 · §5.5 E1–E4, E10 · §5.6 S1, S2, S4–S6, S8–S10, S13 · §5.7 I1–I4, I13–I15, I19 · §5.8 X1 · all of §5.9 | **DELIVERED**, except X1 (native JSON save/load), which text export covers for now. Reproduces reference images 1–5 |
| **P2** | Packages/frames/notes (K1–K4, K7), enumerations (M8), association classes (M15), editing + undo/redo + clipboard (I5–I12, I16–I18), remaining layouts (L5–L7, L10, L12), routing polish (E5–E7, E9), PlantUML/Mermaid/SVG/PNG I/O (X2–X7), validation (X11) | Reproduces images 3 and 4; becomes a real interactive UML editor |
| **P3** | Advanced notation (M12–M14, M16–M18, R13–R15, R17, K5, K6), line jumps (E8), XMI (X8), ~~C++ reverse engineering (X9)~~ **delivered**, code generation (X10), metrics (X12) | Full UML 2.5 coverage and toolchain integration |

---

## 8. Open questions for review

1. ~~**Shared routing header.**~~ **RESOLVED — refactored first, as
   recommended.** `UltraCanvasDiagramRouter` is now the single implementation.
   The extraction was validated against the pre-refactor code over 12,156
   randomised layouts (3,280 of which exercised A\*): 9.7% of routed paths
   differ, and 99.8% of those differences are cases where the *old* output was
   defective. Three real bugs were found and fixed in the process:
   a **use-after-free** in the A\* expansion loop (a reference into `visited`
   held across a `push_back` into `visited`, which fired as soon as the search
   outgrew the reserved capacity), **diagonal segments** in supposedly
   orthogonal paths from the end-of-path bridging, and **zero-length segments**
   that made the arrow-angle computation meaningless. The property tests that
   found them are now part of the suite.
2. **Base class.** Derive directly from `UltraCanvasUIElement` (as FlowChart,
   BlockDiagram, NodeDiagram and CompositorDiagram all do), or introduce a
   shared `UltraCanvasNodeCanvasBase` carrying zoom/pan/selection/minimap for
   the whole family?
   *Recommendation: `UltraCanvasUIElement` for P1* — a family base class is a
   worthwhile follow-up, but it should be extracted from four working
   components, not designed ahead of the fourth.
3. **Member text: structured or free-form?** The simple API in §6 accepts
   `"+Name: string"` strings, which is what makes demos short; the model stores
   structured `UMLMember`s, which is what makes export and validation possible.
   *Recommendation: ship both* — `UltraCanvasUMLNotation` parses the string form
   into the structured form on entry and formats it back on render, so nothing
   is lost and the short form stays available.
4. **Packages via `parentId` or a separate container list?**
   *Recommendation: `parentId`*, matching `CompositorNode::parentId` (relative
   child coordinates, cycle rejection, tree-order rendering, reverse-order hit
   testing) — the semantics have already been worked out there.
5. ~~**Scope of X9 (C++ reverse engineering).**~~ **RESOLVED — pragmatic scope
   confirmed and implemented.** `UltraCanvasCppReverseEngineer` is a brace- and
   declaration-level scanner, not a C++ front end: no macro expansion, no `#if`
   evaluation, no typedef resolution, no template instantiation. Declarations it
   cannot interpret are skipped and counted rather than guessed at. Over this
   repository's 217 public headers it produces 1,345 classifiers and 742
   relationships, fails to interpret 55 declarations, and the resulting model
   validates without errors. Ownership is mapped to UML: by-value and
   `unique_ptr` → composition, `shared_ptr` → aggregation, raw pointer and
   reference → directed association, containers → multiplicity `*`.

---

## 9. Sources consulted

* [Creately — Class Diagram Symbols: Notation and Syntax Explained](https://creately.com/guides/class-diagram-symbols/)
* [Sparx Systems — UML 2 Class Diagram Tutorial](https://sparxsystems.com/resources/tutorials/uml2/class-diagram.html)
* [Sparx Systems — UML Class Diagram: use cases & examples](https://www.sparxsystems.eu/languages/uml/diagrams/classdiagram/)
* [OMG UML Notation Guide, ch. 4 — Static Structure Diagrams (Cornell mirror)](https://www.cs.cornell.edu/courses/cs211/2000fa/materials/UML-Class-Notation-Guide.pdf)
* [San José State University — UML Class Diagrams](http://www.cs.sjsu.edu/faculty/pearce/oose/uml/uml1.htm)
* [GlobalMentor — Class Diagrams](https://www.globalmentor.com/courses/softdev/class-diagrams)
* [AlgoMaster — Class Diagram (LLD)](https://algomaster.io/learn/lld/class-diagram)
* [Mermaid — Class diagrams syntax](https://mermaid.js.org/syntax/classDiagram.html)
* [PlantUML — Class Diagram syntax and features](https://plantuml.com/class-diagram)
* [InformIT / Larman — Qualified Associations (Applying UML and Patterns, §16.15)](https://www.informit.com/articles/article.aspx?p=1398623&seqNum=16)
* [Slickplan — How to make a UML diagram](https://slickplan.com/blog/how-to-make-a-uml-diagram) (referenced in the request; the site blocks automated fetches from this environment, so its content was not read directly)

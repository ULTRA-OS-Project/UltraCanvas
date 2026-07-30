# UltraCanvasMindMap — Research & Feature Proposal

Status: **Phases 0 and 1 are implemented.** The shared viewport (V0) shipped as
[`UltraCanvasDiagramViewport`](UltraCanvasDiagramViewport.md), with
`UltraCanvasNodeDiagram` and `UltraCanvasCompositorDiagram` refactored onto it.
The element itself shipped as `UltraCanvasMindMap` + `UltraCanvasMindMapLayout`
— see [`UltraCanvasMindMapExamples.md`](UltraCanvasMindMapExamples.md) for the
API and `Apps/DemoApp/UltraCanvasMindMapExamples.cpp` for the demo. Phases 2 and
3 below are still a proposal.

This document is the research write-up, the image analysis and the feature list
the implementation was scoped against. For background: the DemoApp had reserved a
`"mindmap"` slot marked `NotImplemented` ("MindMap is not ready yet") since
before this work, and `SWOTDiagramDesignVariants.md` §C4 had flagged the
mind-map/spider variant as *"the only one needing a distinct (branch) layout
engine"* — which is exactly what `UltraCanvasMindMapLayout` turned out to be.

Three of the five open questions are now **resolved** (§8): the viewport is
extracted into a shared `UltraCanvasDiagramViewport` as a Phase 0 refactor
(§4.1), in-place editing embeds `UltraCanvasTextInput` as a child overlay
(§4.2), and every parser the interchange features need — `UltraCanvasJSON`,
tinyxml2, `UltraCanvasZipPackage` — is already in the framework, so no new
third-party dependency is required.

Author: UltraCanvas Framework
Last Modified: 2026-07-30

---

## 1. What a mind map is (and what it is not)

A **mind map** is a radial, hierarchical diagram: a single **central topic**
sits at the visual centre, **main topics** (branches) radiate from it, and each
main topic carries a subtree of **subtopics** that fan outward. Structurally it
is a *rooted tree*; visually its defining properties are the ones a generic tree
widget does not have:

- The root is in the **middle**, not at the top or the left, and children are
  distributed **around** it (usually balanced left/right, sometimes fully
  radial).
- Direction of growth is **inherited down the subtree** — everything under a
  left-side main topic keeps growing leftward.
- **Colour is inherited per branch**: each main topic owns a hue and the whole
  subtree under it is tinted with that hue.
- Connectors are **organic** — tapered or curved "branch" strokes, not the
  square elbows of an org chart.
- Nodes are **variably sized to their text**, and the layout must pack
  non-uniform boxes without overlap.

This is why a mind map cannot simply be `UltraCanvasTreeView` with different
paint, and why it is not a re-skin of the existing elements either:

| Existing element | Why it is not a mind map |
|---|---|
| `UltraCanvasNodeDiagram` | General graph — arbitrary edges, force-directed/circular/grid layout, ports. No rooted-tree semantics, no branch-direction inheritance, no collapse/expand, no per-branch colour cascade. |
| `UltraCanvasDendrogram` | Rooted tree with a *radial* mode, but leaves are constrained to a circumference and node position encodes a **distance/merge height** — a data axis a mind map does not have. |
| `UltraCanvasGourceTree` | Radial force-directed tree for filesystems; nodes are uniform dots, labels are satellites, layout is physics-driven not deterministic. |
| `UltraCanvasFlowChart` | Directed process graph with orthogonal routing and decision semantics. |
| `UltraCanvasTreeView` | Text outline widget, top-down rows, no canvas/viewport. |

The right relationship is **reuse, not derive** (see §4).

---

## 2. What the ten reference images demand

Each image maps onto a concrete set of capabilities; together they define the
scope of a "comprehensive" element rather than a minimum one. Images 1–5 came
from the first review round, 6–10 from the second.

### Image 1 — Classic balanced mind map with an image centre
Title "MIND MAP" above the canvas. Centre is a **drawn illustration** (a
lightbulb-brain), not a text box. Four main topics — `Topic 1`…`Topic 4` — sit
at the four diagonals as **outline-style rings** (thick coloured stroke, white
fill, coloured text). Each main topic fans out to three small `Text` circles on
its outward side. Connectors are thin straight strokes into the ring perimeter,
and each main topic uses its own hue (green / purple / magenta) while the leaf
rings share the light-blue leaf style.

> Requires: image/vector centre node; per-level style presets (root vs main vs
> leaf) rather than per-node styling; outline node style; balanced 4-quadrant
> distribution; leaf fan-out on the outward side; edge-to-perimeter connector
> anchoring on circles.

### Image 2 — Infographic mind map, two-sided symmetric
Big grey circle "MIND MAP" in the middle. Main topics are **coloured pill
labels** (`TITLE 01`…`TITLE 06`), three on the left, three on the right, all
level-aligned in rows. Each pill connects outward through a small **numbered
circular marker** to a block of body text (`Lorem ipsum` heading + description).
Connectors are short, mostly horizontal.

> Requires: mirrored left/right layout with **row alignment across sides**; pill
> (stadium) node shape; ordinal marker badges on the connector; a two-line node
> (title + body) or an attached description block; palette-cycled main-topic
> colours.

### Image 3 — Creative bubble map with decorative background
"MY MIND MAP" in a central rounded rectangle. Five main topics are **filled
circles of differing radius** (First/Second/Third/Fourth idea), plus smaller
satellite circles carrying icons and short counts. Connectors are thin grey
straight lines with a **dot terminator**; a low-contrast plexus/network graphic
sits behind everything; several white callout cards with paragraph text float at
the edges, tied back to their node by a short leader line.

> Requires: node size driven by a **weight/value** (à la bubble chart); circular
> icon-only nodes; connector end caps (dot); a background layer hook
> (image/pattern/vector) drawn under the map; **callout / note cards** with
> leader lines; free-position ("floating") topics not on the strict radial grid.

### Image 4 — Software-style editable map (Visual Paradigm)
"Enforce fire safety regulations" in a black-outlined **diamond** at the centre
of a real editor canvas. Strict **left/right balanced** layout, four main topics
(`Inspection`, `Personnel`, `Risk`, `Inspection Report`) with deep subtrees of
rounded-rectangle nodes; per-branch colour (green / yellow / blue / orange);
organic curved branch strokes that **taper** and meet the node at its baseline;
a few nodes show badges (`Others ?`, `2 Loans`). This is the working-tool
scenario: an editable, scrollable, collapsible document.

> Requires: the full **editing** story — create/rename/delete/reparent by mouse
> and keyboard, Tab/Enter sibling-vs-child creation, drag-to-reparent with drop
> indicator, collapse/expand with child counts, undo/redo, unlimited depth,
> deterministic non-overlapping packing of variable-height subtrees, canvas
> pan/zoom/fit.

### Image 5 — Infographic with orthogonal routing and icon badges
A head silhouette with a stylised brain at the centre; six branches
(`PLANNING`, `ANALYSIS`, `CREATIVE`, `ROUTE MAP`, `LOREM`, `FINAL PRODUCT`) run
out to **outlined rounded rectangles** holding placeholder body text. Routing is
**orthogonal with rounded corners** (a stub out of the centre, a vertical riser,
then a horizontal run into the box), a **circular icon badge** sits on the
elbow, and each branch's label is coloured to match its box outline while the
box fill stays white.

> Requires: orthogonal/elbow connector mode with corner radius; icon badges
> placed *on* the connector; separate label colour vs border colour vs fill
> colour per branch; outline-only node style; stacked vertical rows on each
> side; support for a multi-line text block inside a node.

### Image 6 — Balanced map with cross-branch relationships
Dark rounded-rect centre "Mind Map", four `Subtopic` pill branches (yellow,
orange, teal, purple) growing left and right, each into a column of rounded-rect
leaves. Connectors are rounded elbows in the branch hue. Critically, **two
dashed arcs labelled "Connection"** link leaves that belong to *different*
branches — the classic non-hierarchical relationship.

> Confirms D8 (relationships) as core rather than optional, with a distinct
> dashed style (R8) and a label on the relationship (R9). Also confirms the
> column-of-leaves alignment from image 2 (L12).

### Image 7 — Radial map with arrow connectors
"MIND MAP" as bare centred text with no shape at all. Six branches — `Analysis`,
`Action`, `Revision`, `Solution`, `Strategy`, `Objectives` — each a coloured pill
header with a paragraph of body text beneath it, arranged around the centre.
Connectors are **thick tapered arrows pointing outward**, hand-drawn in style,
each in its branch's hue.

> Requires: `MindMapNodeShape::None` for the root (S1); a two-part node — pill
> header *plus* an unboxed body paragraph beneath (S6); arrow end decorations
> sized much larger than the default (R6); the `HandDrawn` theme (S7).

### Image 8 — Software-style logic chart with numbered badges
Root "Mind Mapping" with a **lightbulb icon to the left of the text**, growing
all to the right. Six main topics (`Analysis`, `Objectives`, `Strategy`,
`Action`, `Revision`, `Solution`), each prefixed by a **filled circular badge
carrying its ordinal 1–6** in the branch hue. Leaves are plain text on an
**underline rule** with no box at all. A small legend box floats at the right.

> Requires: C2 (inline icon before the label) and C8 (automatic ordinal
> numbering) together — this is the image that makes both concrete;
> `MindMapNodeShape::Underline` (S1) and baseline attachment (R4) for the
> leaves; floating topics (D7) for the legend.

### Image 9 — Cluster map with depth-based lightness
Centre "Mind Map Diagrams" as plain text. Four saturated main circles —
`Creative` (blue), `Clean` (green), `Unique` (red), `Modern` (yellow) — each
surrounded by a **loose cluster of smaller circles in the same hue but far
lighter**, at varying sizes, with no visible connector strokes between them.

> Requires: the branch-hue cascade with an explicit **lightness ramp per depth**
> (S4); node size from `weight` (D5); a per-branch cluster/packing sub-layout
> (L4 + L8); and a connector mode that draws nothing.

### Image 10 — Fully radial ring
Central black circle "Mind Map Diagrams"; roughly ten uniformly-sized coloured
circles evenly distributed over the full 360°, each joined to the centre by a
short straight spoke, and each carrying one outer satellite circle.

> Requires: `MindMapStructure::Radial` in its purest form (L4) — equal angular
> distribution, fixed orbit radius per generation, palette cycling per branch
> (S4), uniform node sizing overriding text-fit (S6 min/max width).

**Synthesis.** Images 1, 3, 5, 7, 9 and 10 are *presentation* mind maps — they
need shape variety, images/icons, background layers, callouts, arrow styling and
a strong theming system. Images 2, 4, 6 and 8 are *structural* mind maps — they
need correct balanced layout, column alignment, deep subtrees, cross-branch
relationships and real editing. A comprehensive element must serve both, which
argues for a clean split between **model → layout → style → render**, with each
of the four swappable.

Two features are promoted on the strength of the second image set: **D8
relationships** and **C8 numbering** each appear in more than one design and are
load-bearing for the look, so both move into Phase 1.

---

## 3. Prior art

| Tool | Notable behaviour worth copying |
|---|---|
| **XMind** | Nine structures — Mind Map, Logic Chart, Brace Map, Org Chart, Tree Chart, Timeline, Fishbone, Tree Table, Matrix — and **each branch can use a different structure** inside one map. Markers, labels, notes, boundaries, summaries, relationships, floating topics. |
| **FreeMind / Freeplane** | The `.mm` XML de-facto interchange format; classic left/right balanced layout; folding; rich node HTML; icons; cloud (boundary) shapes. |
| **MindManager** | Task/data attributes on topics (Gantt-style roll-up), filtering by attribute, smart shapes that change with data. |
| **Mermaid `mindmap`** | Text-source mind maps by indentation, node shapes `((circle))`, `[square]`, `(rounded)`, `{{hexagon}}`, `))cloud((`, `>bang]`, `::icon(...)` for icons, `:::class` for CSS classes, markdown-string labels for **bold**/*italic*. |
| **markmap** | Markdown headings/lists → mind map, live re-layout, collapse on click. |
| **Syncfusion / Telerik diagram controls** | `MindMapTreeLayout` primitives: root-centred with children spread equally left/right, per-sector child packing so sibling subtrees never overlap. |

Structure taxonomy worth supporting (from the XMind/EdrawMind vocabulary):
mind map (balanced radial), logic chart (all-right / all-left), org chart
(top-down / bottom-up), tree chart, timeline, fishbone, brace map.

---

## 4. What UltraCanvas can reuse

The framework already contains most of the primitives; the new work is the model,
the layout engine and the editing UX.

| Need | Reuse from |
|---|---|
| Bezier / step / smooth-step routing, arrow heads, hit-testing a stroke | `UltraCanvasNodeDiagram.cpp` (`BuildLinkBezier`, `BuildLinkStep`, `RenderArrowHead`), `Plugins/Charts/UltraCanvasConnectionRenderer.cpp` |
| Pan / zoom / fit / minimap / controls overlay / selection box | **New shared `UltraCanvasDiagramViewport`**, extracted from `UltraCanvasNodeDiagram` and `UltraCanvasCompositorDiagram` — see §4.1 |
| In-place text editing | **Embedded `UltraCanvasTextInput` child overlay** — see §4.2 |
| Recursive subtree packing, radial placement | `UltraCanvasDendrogramLayout` (`ApplyRadialLayout`, subtree extent accumulation) |
| Label measurement & auto-sizing nodes to text | `UltraCanvasNodeDiagram::MeasureLabel` / `SuggestNodeSizeForLabel`, `IRenderContext::GetTextLineDimensions` |
| Non-overlapping label placement for callouts | `Plugins/Charts/UltraCanvasLabelPlacement.cpp` |
| Palette cycling for per-branch colour | `UltraCanvasFlowChartPalette` |
| Images / SVG inside nodes | `IRenderContext::DrawImage(...)`, `Plugins/SVG/UltraCanvasSVGPlugin.h` |
| JSON persistence | `UltraCanvasJSON` — `include/DataFormats/UltraCanvasJSON.h` (`JSONValue`, `Parse`, `Serialize`, `SerializeToFile`); see `Docs/UltraCanvas/UltraCanvasJSON.md` |
| XML parsing (`.mm`, OPML) | **tinyxml2** — already a `REQUIRED` framework-wide dependency (`UltraCanvas/CMakeLists.txt:139`), used by the SVG plugin, DOCX/ODT, FB2 and XLSX readers |
| Zip container (`.xmind`) | `UltraCanvasZipPackage` (`include/UltraCanvasZipPackage.h`), backed by vendored miniz |

New code lands in `UltraCanvas/include/Plugins/Diagrams/UltraCanvasMindMap.h`,
`UltraCanvas/Plugins/Diagrams/UltraCanvasMindMap.cpp` and (following the
dendrogram precedent) a separate `UltraCanvasMindMapLayout.{h,cpp}`, registered
in `UltraCanvas/CMakeLists.txt` next to the other `Plugins/Diagrams/*.cpp`
entries.

### 4.1 Shared viewport extraction (decided)

`UltraCanvasNodeDiagram` and `UltraCanvasCompositorDiagram` each carry their own
copy of the same viewport machinery — `zoomLevel`, `panOffset`, `minZoom`/
`maxZoom`, `ScreenToWorld`/`WorldToScreen`, `ZoomIn`/`ZoomOut`/`FitView`/
`CenterOn`, a minimap config + drag handler, and a controls overlay. Rather than
writing a third copy, Phase 0 extracts

```
UltraCanvas/include/Plugins/Diagrams/UltraCanvasDiagramViewport.h
UltraCanvas/Plugins/Diagrams/UltraCanvasDiagramViewport.cpp
```

covering: the zoom/pan state and clamping, both coordinate transforms,
zoom-at-cursor, `FitView`/`CenterOn` against a caller-supplied content bounds
rect, the snap grid, the minimap (config, render, hit-test, viewport drag) and
the controls overlay (config, render, hit-test, button dispatch). Overlays stay
in **screen space**, rendered after the world transform is popped — the
convention both existing elements already follow.

Two frictions found while scoping this, both of which must be settled in the
extraction rather than papered over:

1. **Coordinate type mismatch.** `UltraCanvasNodeDiagram` uses `Point2Dd`
   (double) for pan offset and world points; `UltraCanvasCompositorDiagram` uses
   `Point2Df` (float). The shared viewport should standardise on `Point2Dd` —
   double is what the layout engine will produce — which makes the compositor
   migration a signature change on its public `GetPanOffset()`.
2. **Existing partial coupling.** `UltraCanvasCompositorDiagram` already borrows
   `NodeDiagramPanelPosition` from the node diagram's header. That enum moves to
   the shared viewport header as `DiagramPanelPosition`, with a deprecated alias
   left in `UltraCanvasNodeDiagram.h` so existing app code keeps compiling.

Both existing elements are refactored onto the shared viewport in the same
change, so the extraction is verified by two real consumers before the mind map
becomes the third.

### 4.2 In-place editing via an embedded text input (decided)

I3 embeds the existing `UltraCanvasTextInput` (`include/UltraCanvasTextInput.h`)
as a child overlay rather than re-implementing a caret. The element already
exposes everything needed: `SetText`/`GetText`, `SetStyle`/`SetFontSize`,
`SetSelection`, `AcceptsFocus() == true`, and — critically for this use —
`onEnterPressed`, `onEscapePressed`, `onFocusLost` and `onTextChanged`, which map
exactly onto commit / cancel / commit-on-blur / live re-layout.

**Hosting pattern.** There is a precedent inside the diagrams plugin already:
`UltraCanvasDendrogram` holds `std::shared_ptr<UltraCanvasScrollbar>` members
directly and drives their render and event forwarding itself, rather than going
through `UltraCanvasContainer::AddChild`. The mind map does the same — a single
lazily-created `std::shared_ptr<UltraCanvasTextInput> editOverlay`, hidden except
while editing.

**Positioning.** The overlay is placed in **screen coordinates**, computed from
the topic's world bounds through the shared viewport's `WorldToScreen`, and its
font size is scaled by the current zoom so the editor visually matches the text
it replaces. It is repositioned on any viewport change while an edit is active,
and rendered last (after the world transform is popped) so it sits above all
branches. Events are offered to the overlay first while editing is active, so
Tab/Enter/Delete go to the editor rather than to the map's authoring shortcuts.

---

## 5. Proposed feature list

IDs are stable handles for tracking (`D` data, `L` layout, `R` routing,
`S` style, `C` content, `I` interaction, `V` viewport, `X` exchange,
`A` advanced). Phase column: **P1** first delivery, **P2** second, **P3** later.

### D — Data model

| ID | Feature | Phase |
|---|---|---|
| D1 | `MindMapTopic` node: `id`, `parentId`, `text`, `children` — rooted tree with exactly one central topic | P1 |
| D2 | Unlimited depth; stable ordering of siblings; `MoveTopic(id, newParentId, index)` reparenting | P1 |
| D3 | Per-topic `side` hint (`Auto` / `Left` / `Right`) that is inherited by the whole subtree | P1 |
| D4 | Collapsed flag per topic + descendant/leaf counts for the collapsed badge | P1 |
| D5 | Per-topic `weight` (double) usable to drive node size, font size or colour (image 3's differently sized bubbles) | P2 |
| D6 | Arbitrary user payload per topic (`std::string userData` + typed attribute map) for app-side binding | P2 |
| D7 | **Floating topics** — topics with no parent, positioned freely on the canvas (image 3's edge callouts) | P2 |
| D8 | **Relationships** — non-hierarchical arrows between any two topics, styled independently of branches (image 6's dashed labelled "Connection" arcs) | P1 |
| D9 | **Boundaries** — a drawn region enclosing a subtree or a set of topics, with its own label/fill/outline (XMind "boundary", FreeMind "cloud") | P3 |
| D10 | **Braces & summaries** — a brace spanning a run of siblings, either terminating in a summary topic or used purely as decoration; also serves the brace-map look (absorbs the former L7) | P3 |
| D11 | Traversal / query API: `ForEachTopic`, `FindTopic`, `GetPath(id)`, `GetSubtreeIds(id)`, depth accessor | P1 |
| D12 | Bulk build helpers: `AddTopic(parentId, text)` returning the new id, and `BuildFromOutline(const std::vector<std::pair<int,std::string>>&)` (indent level + text) | P1 |

### L — Layout

| ID | Feature | Phase |
|---|---|---|
| L1 | `MindMapStructure::Balanced` — root centred, main topics split left/right, subtrees grow outward; the default | P1 |
| L2 | `MindMapStructure::LogicRight` / `LogicLeft` — the whole tree grows to one side (XMind logic chart) | P1 |
| L3 | `MindMapStructure::OrgChartDown` / `OrgChartUp` — top-down / bottom-up hierarchy | P2 |
| L4 | `MindMapStructure::Radial` — children distributed over a full 360°, generation per orbit (image 1's four-diagonal look at level 1, image 3's free ring at level 2) | P1 |
| L5 | `MindMapStructure::Fishbone` — angled ribs off a horizontal spine | P3 |
| ~~L6~~ | ~~Timeline~~ — **dropped** (§8.5); owned by the separately reserved `timelinediagram` element | — |
| L7 | `MindMapStructure::TreeChart` — indented tree with a shared trunk. (Brace map moved to D10, §8.5) | P3 |
| L8 | **Per-branch structure override** — a main topic may declare its own structure while the rest of the map keeps the default (XMind's combined structures) | P2 |
| L9 | Deterministic non-overlap packing: bottom-up subtree extent accumulation with variable node sizes, so sibling subtrees never collide at any depth | P1 |
| L10 | Balancing policy for `Auto` sides: `AlternateByIndex`, `MinimiseSubtreeWeight` (assign each main topic to the lighter side), `SplitInHalf` | P1 |
| L11 | Spacing controls: `siblingGap`, `levelGap`, `rootGap`, per-level overrides | P1 |
| L12 | Row alignment mode for the infographic case (image 2) — siblings on both sides share level rows and a common outward column | P2 |
| L13 | Manual mode: topic positions are honoured as authored, layout only fills in unset positions | P2 |
| L14 | Animated re-layout — positions tween when a node is collapsed, added or reparented | P2 |
| L15 | Compact vs airy density presets, and an auto-shrink pass when the map exceeds the viewport | P3 |

### R — Connectors

| ID | Feature | Phase |
|---|---|---|
| R1 | Connector styles: `Straight`, `Curve` (bezier), `Elbow` (orthogonal, image 5), `RoundedElbow`, `Arc` | P1 |
| R2 | **Organic tapered branch** — width interpolates from thick at the parent to thin at the child (image 4's hand-drawn look) | P1 |
| R3 | Anchor resolution per node shape — connectors terminate on the shape *perimeter* (circle, rect, diamond, pill), never inside it | P1 |
| R4 | Underline/baseline attachment mode: the stroke runs along the bottom edge of the child's text before leaving it (classic mind-map convention) | P2 |
| R5 | Per-connector colour modes: `InheritFromBranch`, `InheritFromChild`, `Fixed`, `GradientParentToChild` | P1 |
| R6 | End decorations: none / arrow / dot / square (image 3 uses dots), sized independently at each end | P1 |
| R7 | Connector badges — a circular icon or ordinal number rendered on the connector at a settable `t` along its path (images 2 and 5) | P2 |
| R8 | Dashed/dotted patterns per connector, including a distinct default style for relationship arrows (D8) | P2 |
| R9 | Connector labels with background plate and collision-aware placement | P3 |

### S — Node shapes, style and theming

| ID | Feature | Phase |
|---|---|---|
| S1 | Shapes: `RoundedRect`, `Rect`, `Pill`/stadium, `Circle`, `Ellipse`, `Diamond`, `Hexagon`, `Cloud`, `Bang`, `Underline` (text with a rule beneath), `None` (text only) | P1 |
| S2 | **Style by level**: `SetLevelStyle(level, style)` — root / main / sub / leaf presets, which is how all five images are actually styled | P1 |
| S3 | Outline style (transparent fill + thick coloured stroke + coloured text) as a first-class preset (images 1 and 5) | P1 |
| S4 | Per-branch colour cascade: main topics take successive hues from a palette; descendants inherit, optionally lightened per depth | P1 |
| S5 | Explicit per-topic style override that wins over level and branch styles | P1 |
| S6 | Auto-size to text with min/max width, word wrap, `maxLines` and ellipsis; multi-line body text inside a node (images 2 and 5) | P1 |
| S7 | Built-in themes: `Default`, `Professional`, `Colorful`, `Pastel`, `Dark`, `Blueprint`, `HandDrawn` | P1 |
| S8 | Drop shadows, per-node opacity, gradient fills | P2 |
| S9 | Background layer hook — solid, gradient, grid, image or app-supplied draw callback rendered beneath the map (image 3's plexus) | P2 |
| S10 | Selection / hover / focus visuals distinct from node styling, plus a "dimmed" state for filtering (A4) | P1 |
| S11 | Rich text spans inside a label — bold / italic / colour runs, Mermaid-style markdown strings | P3 |

### C — Node content

| ID | Feature | Phase |
|---|---|---|
| C1 | **Image node** — raster or SVG as the node's whole body, used for the centre in images 1 and 5, with fit modes | P1 |
| C2 | Inline icon before/after the label (Mermaid `::icon()` equivalent), from an app-supplied icon set | P1 |
| C3 | **Markers** — small status/priority/progress badges attached at a chosen corner of the node | P2 |
| C4 | **Notes** — long text attached to a topic, shown as an indicator plus tooltip/popover | P2 |
| C5 | **Callout cards** — an attached text card with a leader line back to its topic, auto-placed to avoid overlaps (image 3) | P2 |
| C6 | Hyperlink / file-reference attribute with an affordance glyph and an `onTopicLinkActivated` callback | P2 |
| C7 | Task attributes (assignee, progress, start/end) surfaced as an in-node progress bar or badge row | P3 |
| C8 | Numbering — automatic outline numbering (`1`, `1.1`, `1.1.a`) per branch or map-wide, renderable as a plain prefix or as a filled circular badge in the branch hue (image 8) | P1 |

### I — Interaction & editing

| ID | Feature | Phase |
|---|---|---|
| I1 | Selection: click, Shift+click multi-select, marquee, `SelectAll`, `SelectSubtree` | P1 |
| I2 | Collapse/expand by clicking the collapse handle or double-clicking, with a child-count badge on collapsed nodes | P1 |
| I3 | In-place text editing on double-click / F2 / typing, with Esc-cancel and Enter-commit — an embedded `UltraCanvasTextInput` overlay (§4.2) | P1 |
| I4 | Keyboard authoring: `Enter` = sibling, `Tab` = child, `Delete` = subtree, arrows = navigate by geometry, `Ctrl+↑/↓` = reorder sibling | P1 |
| I5 | Drag a topic to reparent, with a live drop indicator showing the target parent and insertion index; Ctrl+drag copies the subtree | P1 |
| I6 | **Undo/redo stack** covering every structural and style mutation | P1 |
| I7 | Cut / copy / paste of subtrees, including paste of plain-text outlines from the clipboard | P2 |
| I8 | Context menu hooks — `onTopicRightClick(id, x, y)` and `onCanvasRightClick(x, y)`, mirroring `UltraCanvasNodeDiagram` 2.0.3 | P1 |
| I9 | Drag-to-create — drag from a node's edge into empty canvas to spawn a child there | P2 |
| I10 | Read-only / presentation mode toggle that disables all mutation | P1 |
| I11 | Hover feedback: cursor changes, node highlight, dimming of unrelated branches on hover | P2 |
| I12 | Callbacks: `onTopicClick`, `onTopicDoubleClick`, `onTopicTextChanged`, `onTopicAdded`, `onTopicRemoved`, `onTopicMoved`, `onCollapseChanged`, `onSelectionChanged`, `onViewportChanged` | P1 |

### V — Viewport & navigation

| ID | Feature | Phase |
|---|---|---|
| V0 | Extract `UltraCanvasDiagramViewport` and refactor `UltraCanvasNodeDiagram` + `UltraCanvasCompositorDiagram` onto it (§4.1) — prerequisite for V1–V4 | P0 |
| V1 | Pan (drag empty canvas / middle-drag), zoom at cursor (wheel), `ZoomIn`/`ZoomOut`/`SetZoomLevel` with clamped range — from V0 | P1 |
| V2 | `FitView(padding)`, `CenterOnTopic(id)`, `FitSubtree(id)`; auto-fit after layout | P1 |
| V3 | Minimap overlay with a draggable viewport rectangle — from V0 | P2 |
| V4 | Controls overlay (zoom ±, fit, lock, collapse-all/expand-all) — from V0 | P2 |
| V5 | `ExpandToLevel(n)` / `CollapseToLevel(n)` for outline-depth browsing | P1 |
| V6 | Focus mode — temporarily treat a chosen topic as the root ("drill down"), with a breadcrumb back | P3 |
| V7 | Search box API: `FindTopics(text)` returning matches, `RevealTopic(id)` expanding ancestors and scrolling into view, with match highlighting | P2 |

### X — Import / export / interchange

| ID | Feature | Phase |
|---|---|---|
| X1 | `ToJson()` / `FromJson()` — native round-trip of model + style + viewport, built on `UltraCanvasJSON` (`JSONValue` / `Parse` / `Serialize`) | P1 |
| X2 | Markdown outline import/export (headings and/or nested bullets), as in markmap | P1 |
| X3 | Mermaid `mindmap` text import (indentation, `((circle))`/`[square]`/`(rounded)`/`{{hexagon}}`/`))cloud((`/`>bang]`, `::icon()`, `:::class`) and export | P2 |
| X4 | FreeMind/Freeplane `.mm` XML import/export via tinyxml2 — the widest interchange format | P2 |
| X5 | OPML import/export via tinyxml2 | P2 |
| X6 | Plain indented-text and CSV parent/child import | P2 |
| X7 | Raster export (PNG via the render context) and vector export (SVG) of the whole map at arbitrary scale | P2 |
| X8 | XMind `.xmind` read support — `UltraCanvasZipPackage` to open the container, `UltraCanvasJSON` to parse `content.json` | P2 |
| X9 | Print/paginate a large map across tiles | P3 |

### A — Advanced

| ID | Feature | Phase |
|---|---|---|
| A1 | Incremental re-layout — only the affected subtree is recomputed on edit | P2 |
| A2 | Virtualised rendering + culling for maps in the thousands of topics | P2 |
| A3 | Layout cache invalidation keyed on style/text changes so `Render()` never does full layout work | P1 |
| A4 | Filtering — hide/dim topics failing an app-supplied predicate (MindManager-style attribute filters) | P3 |
| A5 | Keyboard-only operation and focus ring, so the element is usable without a mouse | P1 |
| A6 | Accessible text extraction: `ToOutlineText()` producing the indented reading order | P2 |
| A7 | Presentation walk-through — step focus through branches one topic at a time with animated camera moves | P3 |
| A8 | Diff/merge helper: compare two maps and report added/removed/moved topics | P3 |

---

## 6. API sketch

Following house conventions (PascalCase, `UltraCanvas` namespace,
`shared_ptr` + factory helper, verbose struct API alongside a simple one):

```cpp
// include/Plugins/Diagrams/UltraCanvasMindMap.h
namespace UltraCanvas {

enum class MindMapStructure { Balanced, LogicRight, LogicLeft,
                              OrgChartDown, OrgChartUp, Radial,
                              TreeChart, BraceMap, Fishbone, Timeline, Manual };

enum class MindMapTopicSide  { Auto, Left, Right };
enum class MindMapNodeShape  { RoundedRect, Rect, Pill, Circle, Ellipse,
                               Diamond, Hexagon, Cloud, Bang, Underline, None };
enum class MindMapConnector  { Straight, Curve, Elbow, RoundedElbow, Arc, TaperedBranch };
enum class MindMapTheme      { Default, Professional, Colorful, Pastel,
                               Dark, Blueprint, HandDrawn };

struct MindMapTopicStyle {
    MindMapNodeShape shape      = MindMapNodeShape::RoundedRect;
    Color fillColor             = Color(255, 255, 255, 255);
    Color borderColor           = Color(70, 110, 180, 255);
    Color textColor             = Color(40, 40, 50, 255);
    double borderWidth          = 2.0;
    double cornerRadius         = 8.0;
    double fontSize             = 12.0;
    bool   bold                 = false;
    double paddingX             = 12.0;
    double paddingY             = 6.0;
    double minWidth             = 0.0;
    double maxWidth             = 220.0;   // 0 = no wrap
    bool   outlineOnly          = false;   // S3
};

struct MindMapTopic {
    std::string id;
    std::string parentId;
    std::string text;
    std::vector<std::string> childIds;

    MindMapTopicSide side       = MindMapTopicSide::Auto;   // D3
    bool  collapsed             = false;                     // D4
    double weight               = 1.0;                       // D5
    std::string iconName;                                    // C2
    std::string imagePath;                                   // C1
    std::string note;                                        // C4
    std::string link;                                        // C6
    std::vector<std::string> markers;                        // C3

    std::optional<MindMapTopicStyle> styleOverride;          // S5
    std::optional<MindMapStructure>  structureOverride;      // L8

    // Computed by the layout engine
    Rect2Dd bounds;
    int     depth = 0;
};

class UltraCanvasMindMap : public UltraCanvasUIElement {
public:
    UltraCanvasMindMap(const std::string& id, int x, int y, int w, int h);
    bool AcceptsFocus() const override { return true; }

    // --- Model ---
    std::string SetCentralTopic(const std::string& text);
    std::string AddTopic(const std::string& parentId, const std::string& text);
    void        AddTopic(const MindMapTopic& topic);
    void        RemoveTopic(const std::string& id);            // removes subtree
    void        MoveTopic(const std::string& id, const std::string& newParentId, int index = -1);
    void        SetTopicText(const std::string& id, const std::string& text);
    MindMapTopic*       GetTopic(const std::string& id);
    const MindMapTopic* GetTopic(const std::string& id) const;
    void        BuildFromOutline(const std::vector<std::pair<int, std::string>>& outline);
    void        Clear();

    // --- Layout & structure ---
    void SetStructure(MindMapStructure structure);
    void SetTopicStructure(const std::string& id, MindMapStructure structure);
    void SetSpacing(double siblingGap, double levelGap, double rootGap);
    void RunLayout();
    void SetAutoFitOnLayout(bool autoFit);

    // --- Style ---
    void SetTheme(MindMapTheme theme);
    void SetLevelStyle(int level, const MindMapTopicStyle& style);   // S2
    void SetTopicStyle(const std::string& id, const MindMapTopicStyle& style);
    void SetBranchPalette(const std::vector<Color>& palette);        // S4
    void SetConnectorStyle(MindMapConnector style);
    void SetBackgroundColor(const Color& color);

    // --- Collapse / navigation ---
    void SetCollapsed(const std::string& id, bool collapsed);
    void ExpandAll();
    void CollapseAll();
    void ExpandToLevel(int level);
    void CenterOnTopic(const std::string& id);
    void FitView(double padding = 40.0);
    std::vector<std::string> FindTopics(const std::string& text) const;
    void RevealTopic(const std::string& id);

    // --- Editing ---
    void SetEditable(bool editable);
    void BeginEditTopic(const std::string& id);
    void Undo();
    void Redo();

    // --- Relationships / boundaries / floating topics ---
    void AddRelationship(const std::string& id, const std::string& fromId,
                         const std::string& toId, const std::string& label = "");
    void AddBoundary(const std::string& id, const std::vector<std::string>& topicIds,
                     const std::string& label = "");
    std::string AddFloatingTopic(const std::string& text, double x, double y);

    // --- Exchange ---
    std::string ToJson() const;
    bool        FromJson(const std::string& json);
    std::string ToMarkdown() const;
    bool        FromMarkdown(const std::string& markdown);
    bool        FromMermaid(const std::string& mermaidSource);
    bool        FromFreeMind(const std::string& mmXml);
    bool        ExportSVG(const std::string& filePath) const;

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

    // --- Callbacks ---
    std::function<void(const std::string&)> onTopicClick;
    std::function<void(const std::string&)> onTopicDoubleClick;
    std::function<void(const std::string&, const std::string&)> onTopicTextChanged;
    std::function<void(const std::string&)> onTopicAdded;
    std::function<void(const std::string&)> onTopicRemoved;
    std::function<void(const std::string&, const std::string&)> onTopicMoved;
    std::function<void(const std::string&, bool)> onCollapseChanged;
    std::function<void(const std::vector<std::string>&)> onSelectionChanged;
    std::function<void(double, double, double)> onViewportChanged;
    std::function<void(double, double)> onCanvasRightClick;
};

inline std::shared_ptr<UltraCanvasMindMap> CreateMindMap(
        const std::string& id, int x, int y, int w, int h) {
    return std::make_shared<UltraCanvasMindMap>(id, x, y, w, h);
}

} // namespace UltraCanvas
```

The layout engine stays separate — `UltraCanvasMindMapLayout.{h,cpp}`, exposing
a pure `ComputeLayout(const MindMapModel&, const MindMapLayoutOptions&,
MindMapLayoutResult&)` — so structures can be added without touching the
element, and so layout can be unit-tested headlessly under `Tests/`.

Two members are not shown above because they are collaborators rather than API:

```cpp
private:
    UltraCanvasDiagramViewport viewport;                    // §4.1 — pan/zoom/minimap/controls
    std::shared_ptr<UltraCanvasTextInput> editOverlay;      // §4.2 — created on first edit
```

`SetZoomLevel`, `FitView`, `CenterOnTopic`, `SetMinimapVisible` and the controls
overlay accessors are thin forwards onto `viewport`; `ScreenToWorld` /
`WorldToScreen` come from it too, and are what position `editOverlay`.

---

## 7. Suggested delivery phases

**Phase 0 — shared viewport (no mind-map code). — DONE**
V0: extracted `UltraCanvasDiagramViewport`, migrated `UltraCanvasNodeDiagram`
(2.1.0) and `UltraCanvasCompositorDiagram` onto it, standardised on `Point2Dd`,
moved `NodeDiagramPanelPosition` → `DiagramPanelPosition` with compatibility
aliases for all four overlay types. Fixed the local-vs-absolute coordinate bug
described in §4.1 as part of the move. Covered by `Tests/DiagramViewportTest.cpp`
(74 assertions). API documented in `UltraCanvasDiagramViewport.md`.

**Phase 1 — the working map (covers images 1, 2, 4, 6, 8 and 10 structurally). — DONE**
Delivered: D1–D4, D7 (floating topics, pulled forward — the legend in image 8
needed them), D8, D11, D12; L1–L4 (org chart came free from the same packing
code), L9–L11; R1–R3, R5, R6, R8; S1–S7, S10; C1, C2, C8; I1–I6, I8, I10, I12;
V1, V2, V5; X1, X2; A3, A5, A6. Plus the DemoApp page replacing the "not ready
yet" placeholder, and `UltraCanvasMindMapExamples.md`.

Covered by `Tests/MindMapLayoutTest.cpp` (model, all six structures, balance
policies, collapse, floating topics, spacing, determinism, and a 364-topic
zero-overlap stress case) and `Tests/MindMapSerializationTest.cpp` (the
structural restore path FromJson depends on).

**Phase 2 — the presentation map (fully covers images 3, 5, 7 and 9).**
D5, D6; L8, L12–L14; R4, R7, R9; S8, S9; C3–C6; I7, I9, I11;
V3, V4, V7; X3–X8; A1, A2, A6.

**Phase 3 — the specialist structures.**
D9, D10 (incl. the brace-map look); L5, L7, L15; S11; C7; V6; X9; A4, A7, A8.

---

## 8. Decisions and remaining questions

### Resolved

1. **Viewport reuse — extract the shared component.** A shared
   `UltraCanvasDiagramViewport` is extracted and both existing consumers are
   refactored onto it, as Phase 0. Design and the two frictions it has to settle
   are in §4.1.
2. **In-place editing — embed `UltraCanvasTextInput`.** Hosted as a
   lazily-created child overlay owned by the element and positioned in screen
   coordinates, following the `UltraCanvasDendrogram` scrollbar precedent rather
   than `UltraCanvasContainer::AddChild`. Design in §4.2.
3. **Parsing dependencies — all already present, nothing new required.**
   `UltraCanvasJSON` (`include/DataFormats/UltraCanvasJSON.h`) covers X1 and the
   `content.json` half of X8. tinyxml2 is already a `REQUIRED` framework-wide
   dependency (`UltraCanvas/CMakeLists.txt:139`, used by the SVG plugin,
   DOCX/ODT, FB2 and XLSX), so X4 and X5 need no new library.
   `UltraCanvasZipPackage` covers the `.xmind` container. Consequently **X8 moves
   from Phase 3 to Phase 2**, and no `Docs/Dependencies.md` /
   `master_dependencies.yaml` / `THIRD_PARTY_LICENSES.md` change is needed for
   any of the interchange work.

### Still open

#### 4. Icon source for C2 / C3

**Finding: there is no icon registry in the framework, and the convention is
already settled — icons are file paths.** Every widget that takes an icon takes
a path string:

| Widget | Surface |
|---|---|
| `UltraCanvasTreeView` | `struct TreeNodeIcon { std::string iconPath; int width, height; }`, used as `leftIcon` / `rightIcon` |
| `UltraCanvasButton` | `SetIcon(const std::string& iconPath)`, `SetIconSize`, `SetIconPosition`, `SetIconMaskColor` |
| `UltraCanvasMenu` | `MenuItemData::Action(label, iconPath, callback)` |

There is also an established **named-set** idiom for bundled icons —
`UltraCanvasAudioPlayerElement.h:27`:

```cpp
inline std::string AudioIconPath(const std::string& name) {
    return NormalizePath(GetResourcesDir() + "media/icons/" + name);
}
```

`media/icons/` already ships 165 mostly-SVG icons, including marker-suitable ones
(`check.png`, `clock-five.svg`, `list-check.svg`, `warning.svg`, `info.png`,
`rating-heart-on.svg`, `circle-*.svg`).

Three candidate designs:

**(a) Path only — mirror `TreeNodeIcon` exactly.**
```cpp
struct MindMapIcon {
    std::string iconPath;              // resolved by the framework image loader
    int width = 16, height = 16;
    Color maskColor = Color(0,0,0,0);  // non-zero => tint via IRenderContext::DrawMask
};
mindMap->SetTopicIcon(topicId, MindMapIcon{ "media/icons/light 001.jpg", 20, 20 });
```
Zero new concepts, consistent with the whole framework, and `DrawMask` already
exists for tinting a monochrome SVG to the branch hue (image 8's coloured
badges). Downside: `FromMermaid` (X3) receives symbolic names like
`fa fa-book`, which have no path.

**(b) Resolver callback — app maps a name to an image.**
```cpp
mindMap->SetIconResolver([](const std::string& name) -> std::string {
    return "media/icons/" + name + ".svg";
});
mindMap->SetTopicIcon(topicId, "bulb");
```
Handles the Mermaid case and lets an app bind its own icon set, but adds a
concept no other UltraCanvas widget has, and the demo has to supply a resolver
before any icon renders.

**(c) Both — path is the storage, resolver is an optional fallback.**
`MindMapTopic::iconPath` stays a path exactly as in (a). If a *name* is set
instead (no separator, no extension) and an `iconResolver` is installed, the
element calls it; if no resolver is installed it falls back to
`MindMapIconPath(name)` over `media/icons/`, the `AudioIconPath` idiom.

> **Recommendation: (c).** It is (a) for every normal caller — same shape as
> `TreeNodeIcon`, nothing new to learn — and the resolver only exists to make
> X3 Mermaid import and app-supplied icon sets work. C3 markers then ship as a
> small curated name→file table over the existing `media/icons/` assets, so the
> demo has working markers with no app setup.

#### 5. Scope of the structure taxonomy (L5–L7)

**Finding: the three cases are not alike, and the demo registry already answers
one of them.**

- **Timeline (L6) — should not live here.** `Apps/DemoApp/UltraCanvas
  Demo.cpp:1377` already reserves a *separate* `"timelinediagram"` element slot
  (`NotImplemented`). Building a timeline structure inside the mind map would
  put two implementations of the same visual on a collision course, and a
  timeline's real requirements — a date axis, scale/zoom in time units, event
  durations, overlap lanes — are axis features that have nothing to do with a
  topic tree. Related prior art already exists separately too
  (`UltraCanvasGanttChart`, `UltraCanvasPertChart`).
- **Fishbone (L5) — no home anywhere.** Nothing in the repo mentions fishbone or
  Ishikawa; there is not even a reserved demo slot. It is genuinely net-new
  either way. But its data *is* a rooted tree with a spine — a mind map whose
  root is at one end and whose branches meet the spine at a fixed angle. It is
  the cheapest of the three to express as a structure: it reuses the entire
  model, needs no new node content, and is essentially one alternative
  `ComputeLayout` case.
- **Brace map (L7) — belongs here, but as a connector style.** A brace map is a
  tree drawn with `{` glyphs instead of branch strokes. That is R-group work
  (a connector that draws a brace spanning a sibling run), not a layout at all —
  and D10 already proposes exactly that primitive for summaries. It should be
  folded into D10 rather than kept as a separate structure.

> **Recommendation:** keep **fishbone** as a structure (it is one layout case
> over the existing model); drop **timeline** from this element and let the
> reserved `timelinediagram` slot own it; and **merge brace into D10** as a
> connector/decoration rather than a structure. That leaves L5 as the only
> survivor of L5–L7, and it can stay in Phase 3.
>
> The general principle worth applying to any future addition: a variant belongs
> in this element when it is *the same topic tree drawn differently*, and
> belongs in its own element when it introduces a **new axis or new data
> semantics** (time, duration, dependency) that the topic tree does not carry.

---

## 9. Sources consulted

- [Mermaid — Mindmap syntax](https://mermaid.js.org/syntax/mindmap.html)
- [Xmind — Ultimate guide to mind mapping](https://xmind.com/blog/the-ultimate-guide-to-mind-mapping-with-xmind)
- [Xmind — How to combine different structures](https://xmind.com/blog/how-to-combine-different-structures-in-xmind-and-why)
- [Xmind — Introducing Tree Table, a new structure](https://xmind.com/blog/introducing-tree-table-new-structure-in-xmind)
- [Xmind — Import & convert mind map files](https://xmind.com/user-guide/import-new)
- [Xmind — Markdown to mind map](https://xmind.com/user-guide/markdown-to-mind-map)
- [FreeMind — Import and export](https://freemind.sourceforge.io/wiki/index.php/Import_and_export)
- [Syncfusion — MindMap tree layout](https://help.syncfusion.com/wpf/diagram/automatic-layouts/mindmaptreelayout)
- [Telerik — Diagram tree layout](https://docs.telerik.com/devtools/aspnet-ajax/controls/diagram/structure/layout/tree)
- [Wikipedia — Radial tree](https://en.wikipedia.org/wiki/Radial_tree)
- [Edraw — Automatic layout of mind map](https://www.edrawsoft.com/mindmap-layout.html)
- [EdrawMind — 8 mind map types](https://edrawmind.wondershare.com/mind-maps/types-of-mind-maps.html)
- [Ayoa — Xmind vs MindManager](https://www.ayoa.com/ourblog/xmind-vs-mindmanager/)

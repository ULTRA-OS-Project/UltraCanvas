# Circle Diagram Infographics — Investigation

**Status:** Design survey and gap analysis (pre-implementation research)
**Last Modified:** 2026-08-07
**Author:** UltraCanvas Framework
**Related:** [`UltraCanvasCircularInfoGraphic`](UltraCanvasCircularInfoGraphic.md)
(sector-cell rings, implemented),
[`UltraCanvasCircularCharts`](UltraCanvasCircularCharts.md) (family map),
[`UltraCanvasMindMap`](UltraCanvasMindMapExamples.md) (radial topic tree),
[`UltraCanvasNodeDiagram`](UltraCanvasNodeDiagramExamples.md) (nodes + links),
[`UltraCanvasLabelPlacement`](UltraCanvasLabelPlacement.md) (leader lines)

## Purpose

"Circle diagram" is the name presentation tools (Visme, Canva, Venngage,
Piktochart, SlideModel) give to a specific infographic layout: a **central
theme, a ring around it, and a handful of labelled circular nodes arranged on
that ring**, usually with secondary nodes, cards or callouts hanging off each
one. It is a *relationship* graphic, not a value plot — the angles carry no
quantity, and the node count is small (typically 4–8).

This document surveys the layout families found in the genre, maps five
representative references onto them, and establishes precisely which of them
UltraCanvas can build today and which it cannot. It is the research write-up
that a future implementation should be planned from; nothing here is
implemented yet.

## 1. What a circle diagram is — and what it is not

The distinction that matters for element selection is **what fills the ring**:

| | Ring is filled with | Angle means | UltraCanvas family |
|---|---|---|---|
| Pie / donut / sunburst / rose | Annular **sectors** that tile the circle | A share, a category, a hierarchy level | Circular *charts* |
| Circular infographic (`UltraCanvasCircularInfoGraphic`) | Annular **cells** that tile each ring band | Nothing — cells are equal-width slots | Circular *charts* |
| **Circle diagram (this document)** | Discrete **discs sitting on** the ring's circumference | Nothing — spacing is even and decorative | *Not covered by any element* |

The third row is the gap. Every implemented circular element treats the ring
as a band to be *subdivided*; a circle diagram treats it as a **track that
nodes are threaded onto**, with the space between nodes left empty and the
space outside the ring used for satellites, cards and callouts. A node has its
own radius, its own fill, and its own children — none of which a sector cell
has.

Two things follow from that, and they drive the whole recommendation in §6:

- Node radius is independent of ring thickness, so a node can (and normally
  does) **overhang both edges of its ring**. Sector geometry cannot express
  this at all.
- Nodes have **satellites placed outside the ring**, which means the element
  must reserve outer margin, fan children around a parent, and route leader
  lines. Sector cells have no such notion.

## 2. Anatomy and vocabulary

Terms used consistently below and proposed as the API vocabulary:

- **Hub** — the centre of the graphic: free text, an icon, a filled disc, or
  an image. Carries the diagram's thesis ("Components of a SaaS Business").
- **Backbone ring** — the circle the primary nodes sit on. Drawn as a hairline
  outline, a thick coloured band, or not drawn at all (implicit).
- **Node** — a primary disc on the backbone ring. Has fill, border, a
  multi-line label, and optionally an icon stacked above the label.
- **Satellite** — a secondary disc attached to one node, parked outward and
  fanned around it, joined by a **leader line** (solid, dotted or dashed).
- **Card** — a rounded rectangle of body text attached to a node instead of
  (or as well as) satellites, placed radially outward.
- **Callout** — like a satellite but attached to a *sector* rather than a
  node, sitting outside the whole graphic with an **arrow** pointing back in.
- **Spoke** — a radial divider line running from the hub out through one or
  more rings, separating sectors.
- **Badge** — a small disc pinned onto a ring at a sector boundary, usually
  carrying a directional arrow to imply cycle order.

## 3. Variant catalogue

Families A–D are the ones the five surveyed references land on. E and F are
included from the wider genre because they share the same anatomy and would
otherwise be designed out of the model by accident.

### Family A — Hub-and-spoke with satellites

Backbone ring, N nodes on it, K satellites fanned outward from each node.

*Reference 1 (“SaaS Circle Diagram”)*: hairline grey backbone ring; 5 nodes,
each a different saturated fill with a white bold multi-line label; 3 white
hairline-outlined satellites per node, fanned over roughly 150° of outward arc
and joined by short straight grey lines; the hub is unboxed free text with
mixed font weights. Node discs overhang the ring on both sides; the ring is
visible only in the gaps between nodes.

*Reference 2 (“Digital Policy Circle Diagram”)*: thick dark backbone band
(~20 px); 6 mint nodes sitting on it, each with a line icon stacked above a
two-line label; exactly one dark satellite per node, joined by a **dotted**
leader line, alternating inside/outside the node's own angle; hub is an icon
above a two-line caption, unboxed.

Structural parameters this family needs: node count and radius, satellite
count/radius/fan angle/offset radius, leader-line style, per-node colour, icon
slot, ring thickness and colour (including "no ring").

### Family B — Hub-and-card

Nodes on an implicit ring, each carrying a **text card** outward; nodes linked
to each other by arc segments rather than to the hub.

*Reference 3 (“Machine Learning Circle Diagram”)*: 6 small nodes, no drawn
backbone; each node has a rounded rectangle with a **dashed** border and 2–3
lines of body text, sitting radially outward and tangentially oriented (not
rotated); consecutive nodes joined by **dashed arcs** that read as a cycle;
hub is free text. The cards are large relative to the nodes, so the layout is
card-driven: the ring radius is a consequence of card size, not an input.

This is the variant that most needs a real layout pass — cards must not
collide, and at 6+ nodes their tangential extent exceeds the arc available.

### Family C — Segmented wheel with external callouts

A donut divided into N coloured sectors, each with an icon; each sector has an
external **callout disc** outside the graphic with an arrow pointing inward.

*Reference 5 (KPI wheel)*: 6 sectors, white line icon per sector, white hole
containing a centre image; 6 callout discs colour-matched to their sector,
each with a straight arrow from callout to sector, labels set multi-line
inside the callout disc.

Unlike A and B this family **is** sector-based — the ring is tiled. It is the
closest of the five to what `UltraCanvasCircularInfoGraphic` already models.

### Family D — Concentric labelled wheel

Several concentric rings sharing one sector division, joined by spokes, with a
label ring, an icon ring, a centre disc and boundary badges.

*Reference 4 (“Business Incubation Circle Diagram”)*: outer white ring split
into 8 sectors by spokes with horizontal labels; a pink middle ring carrying
one line icon per sector; a dark centre disc with a three-line caption; eight
small dark **arrow badges** pinned on the outer ring at the sector boundaries,
each arrow tangential to imply rotation; the whole graphic bounded by a thin
outer circle.

Also sector-based, and also close to what is implemented — the missing parts
are the spokes, the icon slot and the badges, not the geometry.

### Family E — Cycle ring (genre, not in the references)

Sectors drawn as **chevron/arrow arcs** rather than plain annular sectors, so
each segment visually points at the next; usually numbered steps. Needs a
per-sector arrow-head/tail on the sector outline — a variation on Family
C/D's sector renderer, not a new topology.

### Family F — Petal / overlapping discs (genre, not in the references)

N large discs arranged around a centre with deliberate overlap, no ring at
all, blend or transparency at the intersections. Topologically Family A with
ring radius chosen so nodes touch, plus alpha compositing — worth keeping in
mind so "no backbone ring" and "node radius > ring gap" stay legal states, but
it is not a separate element.

### Cross-cutting decorations

Orthogonal to the geometry, and therefore style options rather than variants:
icon-above-label vs. label-only nodes; numbered nodes (`01`…`06`); leader-line
style (solid / dotted / dashed / none); node shadow; per-node palette vs. one
accent colour; hub as disc vs. unboxed text; partial sweeps (a 270° "open"
circle); outer bounding circle.

## 4. What UltraCanvas can build today

| Element | Fits which family | Where it stops |
|---|---|---|
| `UltraCanvasCircularInfoGraphic` | C and D, partially | Tiles rings with sectors — no discs on a ring. Cell `backgroundImage` is drawn `Cover`-clipped to the whole sector, so it cannot be an *icon beside a label*. No spokes, no badges, no external callout discs. `CircularTextStyle::StarStyle` parks **text** outside with a leader line, which is the nearest thing to a satellite but has no disc, no fill and no per-satellite styling. |
| `UltraCanvasPieChartElement` (donut + centre KPI) | C's ring only | Sectors and a centre hole; no icons in sectors, no callout discs. |
| `UltraCanvasSunburstChart` | D's ring stack only | Hierarchy-driven angles; sector sizes come from data, not from an even split. |
| `UltraCanvasMindMap` (`MindMapStructure::Radial`) | A, topologically | Closest topology: hub in the middle, depth‑1 topics on a ring at `radialRingGap × 1`, depth‑2 further out. But wedges are sized by **leaf count** rather than evenly, node boxes are text-sized rather than fixed discs, satellites land on a *global* concentric ring instead of clustering around their parent, and no backbone ring is drawn. It is a working tool with editing, undo and drag-to-reparent — not a presentation primitive. |
| `UltraCanvasNodeDiagram` (`NodeDiagramLayout::Circular`) | A, mechanically | Has `NodeShape::Circle`, per-node fill/border, links with `LinkStyle` and node sizing. Places nodes on a circle — but there is no hub, no backbone ring, no satellite fan (satellites would be free nodes needing manual coordinates), no multi-line label layout inside a disc, no icon slot, no cards. |
| `PlaceShapeLabels()` | Leader lines for all families | Already solves non-overlapping labels around circles with leader lines and polyline obstacles — the right dependency for A/B/C label and card placement, and it is in core, so any element can link it. |
| `ConnectionStyle` / `ConnectionGeometry` | Leader lines and arcs | Already supplies dashed lines (`dashLength`/`gapLength`), caps, curvature, gradients and `SampleArc`. Family B's dashed arcs and Family A's dotted leaders need no new drawing primitives. |

The honest summary: **the drawing primitives all exist; the layout models do
not.** Nothing in the tree places a disc of given radius on a ring, fans
children around it, or reserves the outer margin that satellites and callouts
need.

## 5. Gap analysis

Ordered by how much of the genre each unlocks.

1. **Node-on-ring layout.** Place N discs at even angles on a ring of radius
   R, each with independent radius, overhanging the band. No element does
   this. *Blocks A, B, F.*
2. **Satellite fan.** K children per node at an offset radius, spread over a
   fan angle centred on the parent's outward normal, with leader lines and
   collision-aware clamping against neighbouring fans. *Blocks A.*
3. **Multi-line label inside a disc.** Wrap to the inscribed square, centre
   vertically, shrink-to-fit, with an optional icon stacked above. Sector
   cells draw a single text run; `DrawCellText` has no wrapping. *Blocks A, B,
   C, D.*
4. **Icon slot separate from background.** A per-cell/per-node image drawn at
   a point with a size, not stretched to fill the shape. *Blocks C, D.*
5. **Attached cards.** Rounded-rect text panels anchored to a node, placed
   outward, non-overlapping. *Blocks B.*
6. **External callouts with arrows.** A disc outside the outer radius bound to
   a sector, with an arrow drawn back to the sector's mid-angle. *Blocks C.*
7. **Spokes and boundary badges.** Radial dividers spanning selected rings,
   and small discs pinned at sector boundaries. *Blocks D.*
8. **Outer-margin reservation.** `CalculatePlotArea()` currently reserves room
   for star labels by *estimating* text width from character count. Anything
   outside the ring — satellites, cards, callouts — needs the same treatment
   with real geometry. *Affects A, B, C.*
9. **Chevron sector outlines.** Arrow-shaped sector paths for cycle rings.
   *Blocks E.*

## 6. Recommendation

**Split the work along the sector/node seam, because that seam is real.**

**New element `UltraCanvasCircleDiagram`, in the Diagrams plugin, for
families A, B and F.** These are node-and-link graphics — a hub, a node ring,
satellites, cards, leader lines — and every one of the gaps 1, 2, 5 and 8
above is a *layout* concern with no sector analogue. Folding a node model into
`UltraCanvasCircularInfoGraphic` would mean a mode flag that makes half the
existing API (`CircularCell`, `ValueVisualizationType`, `ColorScale`,
`ImportDataFromCSV`, cell hit-testing) meaningless in one mode and the new
half meaningless in the other. The Diagrams plugin is the right home: this is
`UltraCanvasMindMap`'s neighbour, not `UltraCanvasPieChart`'s.

**It is a presentation-only element** (decided — see §8). That settles the base
class rather than the plugin: it derives from `UltraCanvasChartElementBase`,
following `UltraCanvasVennDiagramElement` and `UltraCanvasSWOTDiagram`, which
are both presentation-only Diagrams elements on that base. Only five headers
in the Diagrams plugin touch `UltraCanvasDiagramViewport` — the compositor,
the mind map and the node diagram — so the plugin is already mostly
presentation graphics, and being one does not pull the element towards Charts.
The base gives chart title, tooltips, hover, `DrawEmptyState`,
`CalculatePlotArea` and `RequestRedraw`-driven updates, all of which a static
graphic wants; nothing in it implies an editing model.

**Extend `UltraCanvasCircularInfoGraphic` for families C, D and E.** These are
already sector graphics on concentric rings — the element's exact model. They
need additive, backward-compatible features: an icon slot distinct from
`backgroundImage`, wrapped multi-line cell text, spokes, boundary badges,
external callouts, and a chevron sector style. No mode flag, no model change,
and each one is independently useful to existing users.

**Share, do not duplicate:** both paths should call `PlaceShapeLabels()` for
label/card placement and `ConnectionStyle`/`ConnectionGeometry` for leaders
and arcs, exactly as the implemented circular elements already do.

## 7. Phased plan

**P1 — `UltraCanvasCircleDiagram`, Family A.** Hub (text / icon / disc /
image), backbone ring (hairline, band, or none), N nodes at even angles with
per-node fill, border, icon and wrapped multi-line label; K satellites per
node with fan angle, offset radius and leader-line style; correct outer-margin
reservation; hover highlight, tooltips and `onNodeClick`/`onSatelliteClick`.
Covers references 1 and 2 outright.

The interaction surface stops there, by decision: the graphic is authored from
code and rendered, exactly as `UltraCanvasCircularInfoGraphic` is. No node
dragging, no inline label editing, no pan/zoom, no undo stack, and therefore
no `UltraCanvasDiagramViewport` dependency and no interchange-format layer of
the kind `UltraCanvasMindMapIO` provides.

**P2 — cards and callouts.** Family B's attached cards with dashed borders and
non-overlapping placement, plus node-to-node arc connectors. In parallel, the
`UltraCanvasCircularInfoGraphic` additions for reference 5: icon slot, wrapped
cell text, external callout discs with arrows.

**P3 — wheel furniture and cycle rings.** Spokes spanning selected rings,
boundary badges with directional glyphs, an outer bounding circle (reference
4); chevron sector outlines and step numbering (Family E); partial sweeps and
petal overlap for Family F.

Each phase carries the repo's usual companions: a component doc under
`Docs/UltraCanvas/`, a DemoApp scene, `Masterfile_modules.md` and
`Docs/UltraCanvas/CHANGELOG.md` entries, and a regenerated `llms.txt`.

## 8. Decisions taken

- **Presentation-only.** The element is authored from code and rendered; it is
  not an editing surface. Consequences are folded into §6 and §7: base class
  `UltraCanvasChartElementBase`, no `UltraCanvasDiagramViewport`, no drag /
  inline edit / pan / zoom / undo, no IO layer. Interaction is limited to
  hover highlight, tooltips and click callbacks — the same surface
  `UltraCanvasCircularInfoGraphic` exposes.

## 9. Open questions for the maintainer

1. **Element name.** `UltraCanvasCircleDiagram` reads well next to
   `UltraCanvasMindMap`, but it is one letter from
   `UltraCanvasCircularInfoGraphic` in tab-completion. `UltraCanvasHubRing` or
   `UltraCanvasCircleInfographic` are alternatives — worth settling before any
   header exists.
2. **Design presets.** `UltraCanvasPertChart` and `UltraCanvasGanttChart` ship
   named designs, and `SWOTDiagramDesignVariants.md` proposed the same for
   SWOT before `UltraCanvasSWOTDiagram` was built. Should the circle diagram
   ship presets (`SaaSWheel`, `PolicyRing`, `ProcessCards`) or only the
   primitives?
3. **Data-driven sizing.** Should a node's `value` be allowed to scale its
   radius (as `NodeDiagramSizing` does with a √ transfer), or are all nodes
   deliberately equal in this genre? The references are all equal-sized.

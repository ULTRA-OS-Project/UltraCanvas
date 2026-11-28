# Radial Cluster Map — Research & Feature Proposal

**Status:** Phase 1 implemented (2026-11-16): the `Balloon` structure with
fans and full rings (L1-L4, L6), the `Ring` connector (R1), widths and sizes
by depth (R3, S1), the `Triangle` shape (S2), JSON (X2), the demo's
**Radial cluster** tab and the tests. Phases 2 and 3 are proposals; the
sketch fields marked Phase 2 in §5 do not exist yet. Guide:
[UltraCanvasMindMapExamples.md](UltraCanvasMindMapExamples.md#radial-cluster-map-balloon).

A radial cluster map (a "balloon tree") draws a large topic tree around one
centre: a handful of coloured main branches leave the hub as thick trunks,
and every sub-topic carries its own children in a ring around it, so the
map grows as clusters of clusters instead of as rings of ever more crowded
leaves. Large planning posters, "everything you need for ..." overviews and
skill or curriculum maps use it: a reader finds a branch by colour, a
cluster by its hub, and a detail by the ring around the hub. Typical marks:

* circles whose size and shade follow the depth: big saturated hubs, small
  pale leaves;
* a thin ring around a hub that its children sit on;
* the branch's name written along its trunk;
* short notes curved around a node ("short shelf life", "long term");
* small triangles and diamonds for conditions ("requires water");
* dashed links between branches (a cross-reference).

UltraCanvas cannot draw this today. This proposal adds it to the existing
mind map, `UltraCanvasMindMap`, as a new layout structure and a few marks.

**Author:** UltraCanvas Framework

---

## Contents

- [1. Summary — the headline question](#1-summary--the-headline-question)
- [2. What a radial cluster map is](#2-what-a-radial-cluster-map-is)
- [3. Reference shapes](#3-reference-shapes)
- [4. Where this belongs in the repository](#4-where-this-belongs-in-the-repository)
  - [4.1 A mind map structure, not a new diagram](#41-a-mind-map-structure-not-a-new-diagram)
  - [4.2 A shared text-on-path helper](#42-a-shared-text-on-path-helper)
  - [4.3 Registration checklist (what "done" touches)](#43-registration-checklist-what-done-touches)
- [5. Data model sketch](#5-data-model-sketch)
- [6. Feature list](#6-feature-list)
- [7. The hard parts](#7-the-hard-parts)
  - [7.1 The balloon layout](#71-the-balloon-layout)
  - [7.2 Overlaps between clusters](#72-overlaps-between-clusters)
  - [7.3 Trunks like metro lines](#73-trunks-like-metro-lines)
  - [7.4 Text along a line and around a circle](#74-text-along-a-line-and-around-a-circle)
  - [7.5 Labels inside small circles](#75-labels-inside-small-circles)
  - [7.6 Size](#76-size)
- [8. What already exists to build on](#8-what-already-exists-to-build-on)
- [9. Open questions (with recommendations)](#9-open-questions-with-recommendations)
- [10. Suggested phasing](#10-suggested-phasing)

## 1. Summary — the headline question

**Can anything we have draw a radial cluster map?** Partly. The mind map
has the tree, the colours and most of the marks; it lacks the layout.

| Need | `UltraCanvasMindMap` today | Gap |
|---|---|---|
| A topic tree from one centre, a colour per main branch | Tree model, branch hues, `MindMapStructure::Radial` | — |
| Circles, diamonds; a centre image | `MindMapNodeShape::Circle`, `Diamond`, `Hexagon`, ...; `imagePath` as the node body | No triangle |
| Shade by depth | `depthLightenStep` lightens the branch hue per depth | — |
| Size by depth | Size from `weight` only | Size by depth (S1) |
| **Children in a ring around their parent, every cluster in its own circle** | No: `Radial`, the dendrogram's `Radial` and the Gource tree all put a whole depth on one ring around the centre, which crowds a tree of a hundred or more topics | **A balloon layout (L1-L4)** — the main gap |
| A ring around a hub that its children sit on | No | Ring connector (R1) |
| Thick trunks from the centre, octilinear like metro lines, thinning with depth | Tapered and rounded-elbow connectors | Metro trunk routing (R2), width by depth (R3) |
| The branch's name along its trunk | Connector labels, short and horizontal | Text along the trunk (T1) |
| Notes curved around a node | No | Arc notes (T2) |
| Dashed links between branches | `MindMapRelationship`, dashed by default | — |
| Zoom and pan over a poster-sized map, collapse, editing, import / export | `UltraCanvasDiagramViewport`, collapse, in-place editing, Mermaid / FreeMind / OPML / CSV / XMind import, JSON, SVG export | SVG export of the new marks (X1) |

**Recommendation: extend `UltraCanvasMindMap`** with a `Balloon` layout
structure (and a per-topic override, so one branch can be a balloon and
another a list), a ring connector, metro trunks, text along trunks and
around nodes, a triangle shape and size by depth. Everything else - the
model, editing, collapse, files, viewport, themes - comes with it.

Three phases (§10). Phase 1 draws a hundred-topic map in clusters with
rings, sized and shaded by depth.

---

## 2. What a radial cluster map is

* **A tree around a hub.** Depth 0 is the centre; depth 1 are the main
  branches, each with a hue; deeper topics take their branch's hue, lighter.
* **Clusters, not rings.** Each topic's children are placed on a circle
  around *that topic*, not around the centre. A subtree therefore occupies
  a disc of its own (a "balloon"), and a deep subtree simply makes a bigger
  disc.
* **Trunks.** The main branches leave the centre as thick lines that travel
  outward before they reach their hub, often bending at 45 or 90 degrees and
  running side by side, like a metro map; the hub sits at the end of the
  trunk, and its cluster beyond it.
* **Reading.** Colour finds the branch, the hub finds the cluster, the ring
  finds the detail. Dashed links say "see also" across branches.

What it is not: a sunburst (sizes are proportions), a dendrogram (leaves on
one line or circle), a force-directed graph (no tree), or a concentric
radial mind map (one ring per depth).

---

## 3. Reference shapes

The request came with a large planning poster: about ten branches and four
hundred topics. Its content and layout belong to their publisher; the demo
uses its *shapes*, with invented content (§9, Q4).

| # | Shape | Needs |
|---|---|---|
| 1 | A hub with its children in a full ring around it, a thin circle through them ("first aid" with ten children) | Balloon with a full ring (L1), ring connector (R1) |
| 2 | A hub with its children in a fan pointing away from its parent | Balloon with a fan (L2) |
| 3 | A cluster of clusters: a ring child that has its own ring | Recursion of L1/L2 with radii from §7.1 |
| 4 | Thick trunks from the centre with 45 / 90 degree bends, a branch name written along each | Metro trunks (R2), width by depth (R3), text along the trunk (T1) |
| 5 | Small curved notes around a node | Arc notes (T2) |
| 6 | Small triangles and diamonds for conditions, dashed links across | Triangle shape (S2), relationships (exist) |
| 7 | Big saturated hubs, small pale leaves | Size by depth (S1), lightening (exists) |

---

## 4. Where this belongs in the repository

### 4.1 A mind map structure, not a new diagram

A separate `UltraCanvasClusterMap` was considered. It would need its own
topic tree, editing, collapse, files, themes and viewport - all of which
`UltraCanvasMindMap` has, with tests. The poster's look is a layout and a
few marks on the same tree. `MindMapStructure` already switches between
Balanced, Logic, OrgChart, Radial and Manual, and a topic can override the
structure of its own subtree (`structureOverride`), so a map can be a
balloon at the top and plain lists inside a cluster.

### 4.2 A shared text-on-path helper

Text along a trunk and notes around a circle both draw a string glyph by
glyph along a path. `UltraCanvasCircularInfoGraphic` already does this
along a ring (`CircularTextStyle::Circular`), privately, and the dendrogram
draws rotated arc text. **Recommendation:** move the glyph placement into
one helper, `DrawTextAlongPath(ctx, text, polyline, offset, align)`
(`UltraCanvasTextOnPath.h` in core), used by the mind map, the circular
infographic and the dendrogram, with its own test.

### 4.3 Registration checklist (what "done" touches)

1. `UltraCanvasMindMapLayout.h/.cpp`: `MindMapStructure::Balloon` and its
   options (§5), the balloon layout (§7.1), overlap repair (§7.2), trunk
   routing (§7.3).
2. `UltraCanvasMindMap.h/.cpp`: the ring connector, metro trunks, width by
   depth, size by depth, the triangle shape, trunk labels and arc notes;
   SVG export of each.
3. `UltraCanvas/include/UltraCanvasTextOnPath.h` and
   `core/UltraCanvasTextOnPath.cpp` (§4.2); the circular infographic and the
   dendrogram moved onto it.
4. `UltraCanvasMindMap::ToJson` / `FromJson`: the new topic fields and
   options, with `MindMapSerializationTest` covering the round trip.
5. The demo: a **Radial cluster map** variant on the **Diagrams → Mind Map**
   page (`UltraCanvasMindMapExamples.cpp`), with invented content.
6. Docs: a section in `UltraCanvasMindMapExamples.md`; the mind map
   proposal's L and R lists cross-referenced; a changelog entry.
7. Tests: `MindMapLayoutTest` (balloon geometry, no overlaps, determinism),
   `TextOnPathTest`, and a render test of the new marks.

---

## 5. Data model sketch

Phase 1 built this sketch as written, except the fields marked *Phase 2*,
which do not exist yet.

```cpp
// UltraCanvasMindMapLayout.h
enum class MindMapStructure { Balanced, LogicRight, LogicLeft, OrgChartDown, OrgChartUp, Radial, Manual,
                              Balloon };      // children in a ring around their parent

enum class MindMapBalloonRing {
    Full,    // a whole circle (the hub's parent sits outside it, next to it)
    Fan      // an arc facing away from the parent (balloonFanSweep degrees)
};

struct MindMapLayoutOptions {
    // ... the existing options ...
    MindMapBalloonRing balloonRing = MindMapBalloonRing::Fan;
    double balloonFanSweep = 300.0;           // degrees, Fan only
    double balloonGap = 8.0;                  // between neighbouring child discs
    double balloonTrunkLength = 60.0;         // extra distance from the centre to each main hub
    bool   balloonMetroTrunks = false;        // octilinear main trunks (R2) - Phase 2
    bool   balloonRepairOverlaps = true;      // §7.2 - retired, not built
};

struct MindMapTopicStyle {
    // ... the existing fields ...
    std::string arcNote;                      // T2: drawn around the node, outside it - Phase 2
};

// UltraCanvasMindMap.h
enum class MindMapConnectorStyle { Straight, Curve, Elbow, RoundedElbow, TaperedBranch,
                                   Ring };    // a circle around the parent through its children

struct MindMapStyle {
    // ... the existing fields ...
    std::vector<double> sizeByDepth;          // S1: circle diameters for depth 0, 1, 2, ...; empty = off
    std::vector<double> connectorWidthByDepth;// R3: trunk widths from depth 0 -> 1, 1 -> 2, ...
    bool branchLabelAlongTrunk = false;       // T1: depth-1 topic's text along its trunk - Phase 2
};

enum class MindMapNodeShape { /* ... */ Triangle };   // S2
```

---

## 6. Feature list

| ID | Feature | Phase |
|---|---|---|
| L1 | Balloon layout, full ring: children on a circle around their parent | 1 |
| L2 | Balloon layout, fan: children on an arc facing away from the parent | 1 |
| L3 | Ring radii from the children's discs, so siblings never overlap (§7.1) | 1 |
| L4 | Per-topic structure override works with Balloon (a balloon branch beside a list branch) | 1 |
| L5 | Overlap repair between non-sibling clusters (§7.2) - retired: the Phase 1 wedge packing cannot overlap | — |
| L6 | Main hubs pushed out along their trunks (`balloonTrunkLength`) | 1 |
| R1 | Ring connector: a thin circle around a hub through its children, with short spokes | 1 |
| R2 | Metro trunks: octilinear main trunks from the centre, parallel lanes, rounded corners | 2 |
| R3 | Connector width by depth (thick trunks, thin twigs) | 1 |
| S1 | Node size by depth | 1 |
| S2 | Triangle node shape | 1 |
| T1 | A main branch's name along its trunk | 2 |
| T2 | Arc notes: short text curved around a node | 2 |
| T3 | The shared text-on-path helper (§4.2), with the circular infographic and the dendrogram moved onto it | 2 |
| X1 | SVG export of rings, metro trunks and text on paths | 2 |
| X2 | JSON round trip of the new topic fields and options | 1 |
| P1 | Large maps: layout in under 50 ms for 1,000 topics; drawing culled to the viewport | 3 |
| P2 | Animated re-layout when a cluster is collapsed or expanded | 3 |
| P3 | Dragging a hub moves its whole cluster (Manual positions for the subtree) | 3 |

---

## 7. The hard parts

### 7.1 The balloon layout

Bottom-up, each topic gets the radius of the disc its subtree needs:

1. A leaf's disc is its node's circle, `r = size / 2`.
2. A topic with children places them on a ring of radius `R` around itself.
   Each child `i` with disc radius `r_i` needs an angle of
   `2 * asin((r_i + gap / 2) / R)` on that ring. The smallest `R` at which the
   angles fit in the ring's sweep (360 degrees, or `balloonFanSweep` for a
   fan) is found by bisection; `R` is at least the topic's own radius plus
   the largest `r_i` plus the gap, so no child touches the hub.
3. The topic's disc is `R + max(r_i)` - the ring and the discs on it.
4. Top-down, each child's angle is the middle of its share of the sweep,
   centred on the direction away from the parent (a fan opens away from the
   parent; a full ring leaves the parent's direction free).

**As built (Phase 1).** Enclosing discs double in radius at every level of a
chain of hubs (a hub's disc is its ring plus its largest child's disc, and
the ring itself must clear that disc), which made a 150-topic map several
times larger than it needs to be. The implementation keeps each subtree as
the circles of its nodes in its topic's own frame instead: the ring radius
is the smallest at which no child's circles come near the topic and the
wedges the children's subtrees cover, seen from the topic, fit in the sweep.
Disjoint wedges still mean disjoint subtrees, so siblings never overlap. A
subtree laid out in another structure (L4) stays one enclosing disc, because
it does not rotate with its topic; a one-sided one is mirrored to grow away
from the centre.

The root is a full ring of the main branches, pushed out by
`balloonTrunkLength` so the trunks have room. Children keep their order
(clockwise from the parent's direction), so a reader's order survives. The
layout is deterministic: no randomness, no iteration count that depends on
timing (Lin and Yen, *On balloon drawings of rooted trees*, 2005; Grivet et
al., *Bubble Tree Drawing Algorithm*, 2006).

### 7.2 Overlaps between clusters

*Phase 1 note:* with the wedge packing of §7.1 (as built) this pass is not
needed. Every topic's children - the centre's main branches included - cover
disjoint wedges seen from it, and each subtree stays inside its wedge, so no
two clusters of the whole map can overlap; `MindMapLayoutTest` checks every
node against every other. L5 is retired.

*The original plan, for enclosing discs:* sibling discs never overlap by
construction; clusters from different branches can, where a deep fan swings
round towards its neighbour. A repair pass checks disc pairs between
different main branches (a sweep over discs sorted by angle keeps it
near-linear) and pushes the smaller subtree outward along its own trunk
until it clears, then re-checks that branch. It moves whole subtrees only,
so the shape inside a cluster never changes.

### 7.3 Trunks like metro lines

A metro trunk runs from the centre to a main hub in at most two straight
segments whose directions are multiples of 45 degrees, with a rounded
corner. Trunks leaving the centre in similar directions share a corridor
side by side with a fixed spacing instead of crossing, ordered by their
angle. The router works on the main trunks only (ten or so lines), so a
simple greedy order with a crossing check suffices; deeper connectors stay
straight or curved.

### 7.4 Text along a line and around a circle

The shared helper (§4.2) measures each glyph, walks the path, and rotates
each glyph to the path's direction at its middle. Text that would read
upside down (a path running right to left) is drawn along the reversed path
instead. A trunk's label sits beside the line, offset by half the line's
width plus a gap; an arc note sits outside its node's circle, on the side
facing away from the parent, and is shortened with an ellipsis if it would
run past half the circle.

### 7.5 Labels inside small circles

A leaf's circle is small; its label must fit inside or beside it. The label
is wrapped to the circle's inner square, then shrunk down to a minimum font
size (8), then - if it still does not fit - placed outside the circle on the
side away from the parent, as the poster's smallest leaves are.

### 7.6 Size

A poster of four hundred topics is far larger than a window. It relies on
the existing viewport (zoom, pan, fit) and collapse; Phase 3 adds culling
and timing targets (P1).

---

## 8. What already exists to build on

| Need | Reuse |
|---|---|
| Topic tree, branches, depths, collapse, editing, relationships | `UltraCanvasMindMap`, `MindMapModel` (`UltraCanvasMindMapLayout.h`) |
| Per-subtree structure | `MindMapTopic::structureOverride` |
| Branch hues and lightening by depth | `MindMapStyle::depthLightenStep`, `GetBranchColor` |
| Circle, diamond and other node shapes; an image as the centre | `MindMapNodeShape`, `imagePath` |
| Tapered connectors (width along a line) | `MindMapConnectorStyle::TaperedBranch` |
| Zoom, pan, fit | `UltraCanvasDiagramViewport` |
| Text along a ring | `UltraCanvasCircularInfoGraphic` (`CircularTextStyle::Circular`), to be moved into the shared helper |
| Import (Mermaid, FreeMind, OPML, CSV, XMind), JSON, SVG export | `UltraCanvasMindMapIO`, serialization, `ExportSvg` |
| Tests | `Tests/MindMapLayoutTest.cpp`, `MindMapSerializationTest.cpp` |

Nothing new is needed from the render context: glyph-by-glyph text uses
`Translate` and `Rotate`, which it has.

---

## 9. Open questions (with recommendations)

**Q1. A new diagram, or the mind map?** **Recommendation: the mind map**
(§4.1).

**Q2. Fan or full ring by default?** **Recommendation: Fan (300 degrees).**
A full ring puts some children on the parent's side, next to the trunk
coming in; a fan keeps them clear. A full ring stays one option away (the
poster uses both).

**Q3. Metro trunks in Phase 1?** **Recommendation: no.** Straight or tapered
trunks already read well; octilinear routing with shared corridors is the
riskiest piece and comes once the layout is settled (Phase 2).

**Q4. Can the demo use the reference poster's content?** No: its text,
branch structure and look are the publisher's. **Recommendation:** an
invented *family emergency plan* (water, food, first aid, power,
communication, documents, evacuation, ...) of about 150 topics, written for
the demo, in the same shapes.

**Q5. Where does the shared text-on-path helper live?** **Recommendation:**
core (`UltraCanvasTextOnPath.h`), because the mind map, the circular
infographic and the dendrogram live in different plugins.

---

## 10. Suggested phasing

| Phase | Delivers | Retires |
|---|---|---|
| **1** | The Balloon structure with full ring and fan (L1-L4, L6); the ring connector (R1); width by depth (R3); size by depth (S1); the triangle shape (S2); JSON (X2); the demo variant with the invented emergency-plan map; layout and render tests | Reference shapes 1, 2, 3, 6, 7 |
| **2** | Metro trunks (R2); the shared text-on-path helper (T3) with trunk labels (T1) and arc notes (T2); SVG export of the new marks (X1) | Reference shapes 4 and 5 |
| **3** | Large-map performance and culling (P1); animated re-layout (P2); dragging a cluster (P3) | — |

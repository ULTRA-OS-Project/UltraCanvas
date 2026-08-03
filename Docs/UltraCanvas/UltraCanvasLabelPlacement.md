# UltraCanvasLabelPlacement Documentation

## Overview

`PlaceShapeLabels()` is the framework's shared solver for placing annotation
labels around shapes without overlaps. It is **part of the layout engine**
(`UltraCanvas/core/UltraCanvasLabelPlacement.cpp`, header
`UltraCanvas/include/UltraCanvasLabelPlacement.h`), not part of any chart or
diagram — every chart and diagram links against the one implementation in the
core library, so the behaviour stays consistent and the charts remain free to
move to a plugin model later.

**Version:** 2.0.0
**Last Modified:** 2026-08-01
**Author:** UltraCanvas Framework
**Namespace:** `UltraCanvas`

Given a list of shapes (circles and rectangles) and a list of labels tied to
those shapes, the solver computes a position for each label such that labels
do not overlap each other, keep clear of caller-supplied obstacles, avoid
covering other shapes where possible, and stay within the bounds.

Version 2.0 adds a persistent `LabelPlacementSession` so several passes share
one collision world, a spatial index so the solver scales to chart-sized label
counts, rotated labels, polyline obstacles, explicit priorities, suppression
instead of overprinting, a minimum-separation rule, leader lines, point anchors
and a 1-D fast path for axis ticks. Everything added is opt-in: the defaults
reproduce the 1.3 behaviour exactly.

Live, interactive demo: **Layout System → Label placement** tab in the demo
application (`Apps/DemoApp/UltraCanvasLabelPlacementExamples.cpp`).
Unit tests: `Tests/LabelPlacementTest.cpp` (`ctest -R LabelPlacementTest`).

## Model

```cpp
#include "UltraCanvasLabelPlacement.h"

std::vector<LabelShape> shapes;   // the geometry labels attach to
std::vector<ShapeLabel> labels;   // one entry per label to place
LabelPlacementOptions opts;

std::vector<PlacedShapeLabel> placed = PlaceShapeLabels(shapes, labels, opts);
for (size_t i = 0; i < placed.size(); ++i) {
    ctx->DrawText(labels[i].text, placed[i].bounds.TopLeft());
}
```

Results come back in the same order as the input labels.

### LabelShape

| Field | Meaning |
|---|---|
| `type` | `Circle` or `Rectangle` (rectangles also cover rounded rects) |
| `center`, `radius`, `size` | Geometry; `radius` for circles, `size` for rectangles |
| `keepLabelInside` | `true` = the label belongs inside this shape |
| `isContainer` | Background enclosure (e.g. a hierarchy's parent circle): labels are not pushed off it, and it can be named as a containment target |

### ShapeLabel

| Field | Meaning |
|---|---|
| `text`, `shapeIndex` | The label and the shape it names |
| `textSize` | Measured extents — fill from `ctx->GetTextLineDimensions()` with the drawing font |
| `preferredSide` | `Auto`, `Top`, `Bottom`, `Left`, `Right`, `Inside` or `Border` |
| `anchorPriority` | Inside placement: ordered `LabelAnchor` list, most-preferred first |
| `borderAngles` | Straddle positions in clock-face degrees (0 = 12 o'clock, 60 = 2 o'clock) |
| `containerShape` | Index of a shape the label must stay within (`-1` = none) |
| `tolerateShapeOverflow` | Inside placement: do not penalise text spilling over the outline |
| `allowOutsideFallback` | Inside placement: offer the outside sides as last resort |
| `rotationDegrees` | Rotation of the drawn text about its centre; candidates reserve the rotated footprint and collisions use the exact rotated quad |
| `priority` | Higher is placed first, regardless of array position; ties keep input order |
| `allowSuppress` | Drop the label rather than overlap a neighbour or leave the bounds |
| `wantLeaderLine` | Ask for a connector when the label lands far from its shape |
| `usePointAnchor`, `anchorPoint` | Place around a bare point instead of a shape from the array |

### LabelPlacementOptions

| Field | Meaning |
|---|---|
| `bounds` | Area labels are kept within (zero size = unbounded) |
| `shapeMargin` | Gap between a label and its shape's edge |
| `labelMargin` | Minimum gap between neighbouring labels |
| `avoidOtherShapes` | Penalise labels covering shapes other than their own |
| `obstacles` | Keep-out rectangles: legends, axis titles, connector lines, markers |
| `minLabelSeparation` | Minimum clear distance between labels; the larger of this and `labelMargin` wins (0 = unchanged behaviour) |
| `declutter` | `PlaceAll` (default) or `SuppressColliding` to drop badly-placed labels without per-label opt-in |

### PlacedShapeLabel

| Field | Meaning |
|---|---|
| `bounds` | Final rectangle. For a rotated label this is the axis-aligned box: draw the text centred in it, rotated by `rotationDegrees` |
| `side` | Which kind of position was used |
| `fitted` | `false` when the label could not be fully resolved |
| `suppressed` | `true` when the label was dropped — **do not draw it**; it claims no space |
| `rotationDegrees` | Echoed back for the drawing code |
| `hasLeader`, `leaderFrom` | Draw a connector from this point on the shape edge to the label |

## Sessions: solve once, draw many times

`PlaceShapeLabels()` solves one batch and forgets it. Anything placing labels in
several passes — axis furniture, then series values, then a legend, then
annotations — should hold a `LabelPlacementSession` instead, so each pass sees
what the earlier ones claimed:

```cpp
LabelPlacementSession session(opts);
session.AddShapes(pointShapes);
session.AddObstacleRect(legendRect);
session.AddObstaclePolyline(seriesPolyline, 3.0);   // the line itself is keep-out

auto valueLabels  = session.Place(valueLabelRequests);
auto seriesLabels = session.Place(seriesNameRequests);   // avoids the value labels
```

**Solve at build time, not at draw time.** The intended lifetime is the chart,
not the frame: build the session when the chart is created, keep the solved
rectangles, and draw from them on every redraw. `Reset()` it only when something
that moves labels changes — data, geometry, fonts, axis ranges. Running the
solver inside a paint handler is the thing this class exists to avoid.

## Priority

Labels are placed **greedily in priority order, then input order**, so with all
priorities equal the input order *is* the priority order: an earlier label
claims its preferred spot and later labels are steered around it. Submit the
most constrained labels first — charts submit the smallest shapes first, because
a label on a small mark has the least room to move. Set `priority` explicitly
when a label must survive regardless of where it sits in the array — the
minimum, maximum, latest or pinned values in a chart.

Within one label, the candidate order expresses the preference: the first
anchor/angle/side wins unless it collides, then the solver falls back to the
next one. `PlacedShapeLabel::side` reports which kind of position was used, and
`fitted` is `false` when no candidate could be fully resolved.

## The three conventions

**Outside** — the label sits beside the shape, never on it. The default for
data-point labels, packed-bubble names and radar axis names.

```cpp
ShapeLabel l;
l.preferredSide = LabelSide::Top;     // fallback sides are tried automatically
```

**Inside** — the label is centred on the shape. Node-style diagrams accept text
spilling over the outline (draw it with a halo); set `tolerateShapeOverflow` so
that overflow is not scored as a defect, and `allowOutsideFallback` so a label
can still step off the node rather than land on another label.

```cpp
l.preferredSide = LabelSide::Inside;
l.tolerateShapeOverflow = true;   // text across the outline is the intended look
l.allowOutsideFallback  = true;   // ... but never text on top of text
```

**Border** — the label straddles the outline at a chosen clock-face angle, which
reads better than an axis-aligned side in crowded circle packings.

```cpp
l.preferredSide = LabelSide::Border;
l.borderAngles  = {60.0, 120.0, 300.0, 240.0};   // 2, 4, 10, 8 o'clock
```

## Obstacles

Anything the labels must not cover becomes a rectangle in `options.obstacles`;
obstacles repel labels exactly like already-placed labels do.

```cpp
opts.obstacles.push_back(legendRect);          // an opaque legend box
opts.obstacles.push_back(quadrantTitleRect);   // chrome drawn earlier
```

A line is approximated by the bounding rects of short sub-segments, which fits
a diagonal far more tightly than one box around the whole line:

```cpp
for (int i = 0; i < steps; ++i) {
    // ... rect around segment i of the line ...
    opts.obstacles.push_back(segmentRect);
}
```

## Containment

A label can be required to stay inside a shape, which acts as a second, shaped
bounds. Hierarchical packings use it to keep a child's label within its parent
circle:

```cpp
LabelShape parent;                 // the enclosing circle
parent.isContainer = true;         // not a mark labels must avoid covering
shapes.push_back(parent);

label.containerShape = static_cast<int>(shapes.size() - 1);
```

## Decluttering dense labels

A chart with hundreds of value labels cannot draw them all. Offer them all and
let the solver decide:

```cpp
for (...) {
    ShapeLabel l;
    l.usePointAnchor = true;
    l.anchorPoint    = screenPos;
    l.textSize       = measured;
    l.allowSuppress  = true;          // drop rather than overprint
    l.priority       = isExtreme ? 10 : 0;   // keep min/max whatever happens
    requests.push_back(l);
}
opts.minLabelSeparation = 12.0;       // visible breathing space, not just no overlap

for (const PlacedShapeLabel& p : session.Place(requests)) {
    if (p.suppressed) continue;       // <- the whole point
    ctx->DrawText(text, p.bounds.TopLeft());
}
```

A label is suppressed when it would overlap something already claimed **or**
would have to hang outside the bounds to avoid doing so. Suppressed labels claim
no space, so later labels can use the room.

## Axis ticks: use the 1-D path

Tick labels sit at constructed positions along one axis and only ever collide
with their own neighbours, so they do not belong in the 2-D solver:

```cpp
std::vector<bool> visible = DeclutterAxisTicks(tickCentres, tickWidths,
                                               /*minGap*/ 6.0,
                                               TickDeclutterPolicy::KeepEveryNth);
```

| Policy | Behaviour |
|---|---|
| `KeepEveryNth` | Smallest stride that fits — evenly spaced survivors, the classic axis look |
| `Greedy` | Keep everything that clears the last kept label — more labels, uneven spacing |
| `PriorityGreedy` | Highest-priority ticks first (min/max/current), then whatever still fits |

## Users in the framework

| Component | Convention |
|---|---|
| Venn diagram | Outside for overlapping layouts, inside band for nested |
| Nested area chart | Inside, per-alignment anchor priority, smallest shape first |
| Bubble chart | Outside; hierarchical mode straddles the rim at 2 o'clock, contained by the group circle |
| Quadrant chart | Outside (above the point), quadrant titles as obstacles |
| Radar chart | Outside, radially outward, legend as obstacle |
| Adjacency diagram | Inside with overflow tolerated and outside fallback |

## Best Practices

1. **Measure text with the font you will draw with** — pass the result in
   `textSize`; the solver never measures text itself.
2. **Submit constrained labels first** — order is priority.
3. **Reserve real space in your layout too.** The solver can only choose among
   positions that exist; if the element leaves no room for a label, it can only
   pick the least bad spot. Charts that reserve margin from measured text get
   markedly better results.
4. **Register your chrome as obstacles** — legends, titles and axis captions
   are invisible to the solver otherwise.
5. **Pair overflow-tolerant labels with a halo** so text crossing an outline
   stays readable on any background.

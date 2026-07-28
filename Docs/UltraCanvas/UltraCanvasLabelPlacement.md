# UltraCanvas Label Placement Solver

**Version:** 1.1.0
**Last Modified:** 2026-07-24
**Author:** UltraCanvas Framework

## Overview

`PlaceShapeLabels` is a shared, diagram-agnostic solver that computes the best position for text labels attached to shapes. Any diagram or chart can feed it a list of shapes and a list of labels and get back non-overlapping positions that respect a configurable margin between labels, keep the required distance from each label's shape, avoid covering other shapes where possible, and stay inside the diagram bounds.

It is currently used by the Venn diagram (set labels, outside or nested-inside), the Sankey diagram (node labels beside the bars), and the flow chart (connection labels on their carrier segment).

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Charts/UltraCanvasLabelPlacement.h`
**Implementation:** `Plugins/Charts/UltraCanvasLabelPlacement.cpp`

## Core Concepts

Every label belongs to exactly one **shape** (`ShapeLabel::shapeIndex`). Shapes come in three geometries:

| `LabelShapeType` | Geometry | Typical use |
|---|---|---|
| `Circle` | `center` + `radius` | Venn sets, graph nodes, gauge faces |
| `Rectangle` | `center` + `size` (also covers rounded rects) | Boxes, bars, cells |
| `Segment` | `p1` + `p2` + `thickness` | Edges / connectors |

Each shape carries a **keep-inside / keep-outside flag** (`keepLabelInside`): overlap-style diagrams keep labels fully outside their shape; containment/nested diagrams keep them inside. Shapes that no label references act purely as **obstacles** — other labels avoid them (this is how flow-chart nodes keep connection labels away without receiving labels themselves).

Each label may state a **preferred location** (`LabelSide`): `Top`, `Bottom`, `Left`, `Right`, `Inside`, `Radial`, or `Auto` to let the solver pick the least crowded side. For `Segment` shapes, `Inside` (and `Auto`) means centred **on** the line — the classic connector pill; concrete sides prefer the perpendicular offset pointing that way.

## API

```cpp
std::vector<PlacedShapeLabel> PlaceShapeLabels(
    const std::vector<LabelShape>& shapes,
    const std::vector<ShapeLabel>& labels,
    const LabelPlacementOptions& options);
```

Results are returned in the same order as the input labels. Draw each label at `result.bounds.TopLeft()` (see below for rotated labels).

### ShapeLabel

```cpp
struct ShapeLabel {
    std::string text;
    size_t shapeIndex;              // shape this label belongs to
    LabelSide preferredSide;        // Auto | Top | Bottom | Left | Right | Inside | Radial
    Size2Dd textSize;               // measured with IRenderContext::GetTextLineDimensions()
    double angleDeg;                // rotation about the label centre (default 0)
    double radialAngleDeg;          // anchor angle for LabelSide::Radial
    double priority;                // higher = placed first, hidden last
};
```

Measure `textSize` with the same font that will be used for drawing.

### LabelPlacementOptions

```cpp
struct LabelPlacementOptions {
    Rect2Dd bounds;          // keep labels inside this area (empty = unbounded)
    double shapeMargin;      // gap between a label and its shape's edge (default 6)
    double labelMargin;      // minimum gap between neighbouring labels (default 6)
    bool avoidOtherShapes;   // penalise labels covering other shapes (default true)
    bool allowHide;          // drop unresolvable labels instead of force-placing
    double leaderThreshold;  // > 0: flag labels farther than this for a leader line
};
```

Leave `bounds` empty (zero size) when the diagram renders in a panned/zoomed world space whose visible area does not match element-local coordinates.

### PlacedShapeLabel

```cpp
struct PlacedShapeLabel {
    Rect2Dd bounds;        // final label rect (AABB for rotated labels)
    LabelSide side;        // side actually used
    bool fitted;           // false if some overlap could not be resolved
    bool hidden;           // true when the label was dropped (allowHide)
    Point2Dd anchorPoint;  // closest point on the own shape's boundary
    bool needsLeader;      // gap to shape exceeded leaderThreshold
};
```

## Features

- **Side selection:** an explicit `preferredSide` wins when it is free; `Auto` prefers the vertical side facing away from the diagram's centroid (wide, short labels read best above/below), then the outward horizontal side, then the remaining sides.
- **Slide-apart collision resolution:** on each side the label may slide along the shape so neighbouring labels sit next to each other separated by `labelMargin`; remaining conflicts slide the label along its free axis until clear.
- **Segment labels:** on-line pill at several fractions along the segment, or perpendicular offsets on either side; the carrier segment also acts as an obstacle for other labels (sampled as a capsule of `thickness`).
- **Radial placement** (`LabelSide::Radial` + `radialAngleDeg`): the label sits just outside the shape along the given angle, with small angular jitters and growing radial offsets as fallbacks — gauge-tick / pie-slice style.
- **Rotated labels** (`angleDeg`): the solver collides using the rotated bounding box. Draw by translating to `bounds.Center()`, rotating by `angleDeg`, and drawing the text at `(-textSize.width/2, -textSize.height/2)`.
- **Priority and hiding:** labels are placed in descending `priority` order (ties keep input order). With `allowHide`, a label whose overlap cannot be resolved is marked `hidden` instead of being force-placed — low-priority labels are dropped first.
- **Leader lines:** with `leaderThreshold > 0`, a label that ended up farther than the threshold from its shape gets `needsLeader = true`; draw the leader from `anchorPoint` to the label rect.

## Usage Example

```cpp
#include "Plugins/Charts/UltraCanvasLabelPlacement.h"

std::vector<LabelShape> shapes;
LabelShape bar;
bar.type = LabelShapeType::Rectangle;
bar.center = Point2Dd(120, 200);
bar.size = Size2Dd(15, 140);
shapes.push_back(bar);

std::vector<ShapeLabel> labels;
ShapeLabel l;
l.text = "Revenue";
l.shapeIndex = 0;
l.preferredSide = LabelSide::Left;
l.textSize = Size2Dd(ctx->GetTextLineDimensions(l.text));
labels.push_back(l);

LabelPlacementOptions opts;
opts.bounds = Rect2Dd(GetLocalBounds());
opts.shapeMargin = 8.0;
opts.labelMargin = 4.0;

auto placed = PlaceShapeLabels(shapes, labels, opts);
for (size_t i = 0; i < placed.size(); ++i) {
    if (!placed[i].hidden) {
        ctx->DrawText(labels[i].text, placed[i].bounds.TopLeft());
    }
}
```

## Adopters

| Element | What it solves |
|---|---|
| `UltraCanvasVennDiagramElement` | Set labels outside overlapping circles/boxes; inside the top band in the nested layout; per-set preferred side via `SetCircleLabelSide` |
| `UltraCanvasSankeyDiagram` | Node labels beside the bars (left for sources, right for sinks) without label-on-label collisions |
| `UltraCanvasFlowChart` | Connection labels: centred pill on the longest path segment when free, sliding along/off the line when crowded; nodes act as obstacles |
| `UltraCanvasRadarChartElement` | Axis labels placed radially at their axis angle, kept apart and clamped inside the element (no more clipped names); the legend auto-places on the least crowded side treating axis labels as obstacles |

## Protected Primary / Movable Secondary Labels

A common requirement (quadrant charts, annotated scatter plots): fixed **primary** labels — quadrant titles, headings — must never be overwritten, while **secondary** labels (point annotations) move out of the way. Model this by passing the primary labels' rects as **obstacle-only shapes** (shapes no label references) and attaching the secondary labels to their markers:

```cpp
std::vector<LabelShape> shapes;
// Primary: quadrant title rects, drawn by the chart itself, obstacle-only.
LabelShape titleRect;
titleRect.type = LabelShapeType::Rectangle;
titleRect.center = /* title text centre */;
titleRect.size   = /* measured title size */;
shapes.push_back(titleRect);
// Secondary: one small circle per data marker.
LabelShape marker;
marker.type = LabelShapeType::Circle;
marker.center = pointPos;
marker.radius = pointRadius;
shapes.push_back(marker);

ShapeLabel pointLabel;
pointLabel.shapeIndex = 1;                  // the marker, not the title
pointLabel.preferredSide = LabelSide::Top;  // classic annotation position
```

The solver then guarantees point labels never cover the titles, never cover their own or other markers, and never overlap each other. Combine with `priority` + `allowHide` when there are more annotations than space.

## Performance Notes

The solver is O(labels × candidates × (shapes + placed labels)); each label considers a few dozen candidates. This is negligible for typical diagrams (tens of labels) but is **not** intended to run per frame over hundreds of animated labels (e.g. the Gource tree), where visibility gating remains the right tool.

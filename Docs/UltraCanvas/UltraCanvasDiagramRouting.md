# UltraCanvasDiagramRouter

Shared orthogonal connection routing for the diagram family.

- Header: `include/Plugins/Diagrams/UltraCanvasDiagramRouting.h`
- Source: `Plugins/Diagrams/UltraCanvasDiagramRouting.cpp`
- Tests: `Tests/DiagramRoutingTest.cpp` (target `DiagramRoutingTest`)
- Version: 1.0.0

## What it is

The obstacle-aware orthogonal router that grew inside `UltraCanvasFlowChart`
(v2.1.4 cardinal L/Z routing, v2.2.0 A\*), lifted into a standalone unit so
every node-and-edge component can share one implementation.

It is **UI-free** — it includes only `UltraCanvasCommonTypes.h`. There is no
render context, no element, no node type. That is what makes it unit-testable
without a window, and what lets `UltraCanvasFlowChart`, `UltraCanvasBlockDiagram`
and the forthcoming `UltraCanvasClassDiagram` all call the same code.

Every entry point is `static`: routing is a pure function of its inputs.

## Model

The router knows nothing about your nodes. You give it:

| Input | Meaning |
|---|---|
| `start`, `end` | The exact points the line must touch, normally on a box border |
| `sourceSide`, `targetSide` | `DiagramCardinalSide` — the box faces the line leaves from and arrives at, so it meets both boxes head-on |
| `obstacles` | `std::vector<DiagramObstacle>` — rectangles to route around. **Pre-filtered by the caller**: leave out the source and target boxes, or the line can never leave or enter them |
| `options` | `DiagramRoutingOptions` — grid size, routing area, turn penalty, expansion cap, obstacle insets |

## Basic use

```cpp
#include "Plugins/Diagrams/UltraCanvasDiagramRouting.h"

// 1. Turn your own nodes into obstacles, skipping the two being connected.
std::vector<DiagramObstacle> obstacles;
for (const auto& [id, node] : nodes) {
    if (id == sourceId || id == targetId) continue;
    obstacles.emplace_back(node.x, node.y, node.width, node.height);
}

// 2. Describe the grid and the area the router may use.
DiagramRoutingOptions options;
options.gridSize    = 20.0;
options.routingArea = Rect2Dd(0, 0, elementWidth, elementHeight);

// 3. Pick the faces, then route.
Rect2Dd sourceBox(source.x, source.y, source.width, source.height);
Rect2Dd targetBox(target.x, target.y, target.width, target.height);
Point2Dd sourceCenter(sourceBox.x + sourceBox.width * 0.5,
                      sourceBox.y + sourceBox.height * 0.5);
Point2Dd targetCenter(targetBox.x + targetBox.width * 0.5,
                      targetBox.y + targetBox.height * 0.5);

auto sourceSide = UltraCanvasDiagramRouter::GetCardinalSide(sourceCenter, targetCenter);
auto targetSide = UltraCanvasDiagramRouter::GetCardinalSide(targetCenter, sourceCenter);

Point2Dd start = UltraCanvasDiagramRouter::GetCardinalPoint(sourceBox, sourceSide);
Point2Dd end   = UltraCanvasDiagramRouter::GetCardinalPoint(targetBox, targetSide);

std::vector<Point2Dd> path = UltraCanvasDiagramRouter::ComputeOrthogonalPath(
    start, end, sourceSide, targetSide, obstacles, options);

// 4. Draw it, anchor the label, aim the arrowhead.
for (size_t i = 1; i < path.size(); ++i) ctx->DrawLine(path[i - 1], path[i]);

Point2Dd labelAnchor = UltraCanvasDiagramRouter::ComputeLongestSegmentAnchor(path);
double arrowAngle    = UltraCanvasDiagramRouter::ComputeApproachAngle(targetSide);
```

## Guarantees

`ComputeOrthogonalPath` always returns a path that

- begins exactly at `start` and ends exactly at `end`;
- has every segment axis-aligned;
- contains no duplicate consecutive waypoints and no collinear redundancy —
  one waypoint per real corner;
- leaves along `sourceSide`'s axis and arrives along `targetSide`'s axis;
- is never empty. If A\* cannot find a route (target fully enclosed, expansion
  cap reached) the cheap cardinal path is returned instead: a visible
  connection beats no connection.

These are asserted as properties over 2,000 randomised layouts in
`Tests/DiagramRoutingTest.cpp`.

## Strategy

1. `ComputeCardinalPath` builds the cheap L-shape (source and target faces on
   different axes, one corner) or Z-shape (same axis, bend at the midpoint).
2. `PathHasObstacles` tests it. If nothing is in the way, that path is the
   answer — no search runs.
3. Otherwise `RouteAStar` searches a uniform grid, 4-connected, with a turn
   penalty (default 5× a straight step) so paths prefer long straight runs, and
   a Manhattan heuristic. The search starts one cell outside the source in the
   direction of `sourceSide` and must reach the goal travelling perpendicular
   into `targetSide`.
4. The cell path is simplified to corners, stitched to the exact endpoints,
   orthogonalised and normalised.

## API

### Routing

| Function | Purpose |
|---|---|
| `ComputeOrthogonalPath(start, end, sourceSide, targetSide, obstacles, options)` | Top-level entry point (steps 1–4 above) |
| `ComputeCardinalPath(start, end, sourceSide, targetSide)` | Obstacle-unaware L/Z path; 3 or 4 waypoints |
| `PathHasObstacles(path, obstacles, inset = 1.0)` | True if any axis-aligned segment crosses an obstacle |
| `RouteAStar(start, end, sourceSide, targetSide, obstacles, options)` | The search alone; empty vector when no route exists |

### Geometry

| Function | Purpose |
|---|---|
| `GetCardinalSide(boxCenter, otherCenter)` | Which face of a box points at another box |
| `GetCardinalPoint(box, side)` | Midpoint of that face |
| `GetDistributedCardinalPoint(box, side, index, count, spreadFraction = 0.6)` | Anchor *index* of *count* spread evenly along one face, so parallel connections into the same face do not overlap. `count == 1` is identical to `GetCardinalPoint` |
| `IsHorizontalSide(side)` | True for `Left`/`Right` |
| `ComputeLongestSegmentAnchor(path)` | Label anchor: midpoint of the longest segment, never a corner |
| `ComputeApproachAngle(targetSide)` | Arrow angle for an orthogonal path, from the face alone |
| `ComputeFinalSegmentAngle(path)` | Arrow angle for straight/curved connections, from the last segment |

### `DiagramRoutingOptions`

| Field | Default | Meaning |
|---|---|---|
| `gridSize` | `20.0` | A\* cell size in world units |
| `routingArea` | empty | World region A\* may use; cells outside are blocked |
| `turnPenalty` | `5.0` | Extra g-cost per direction change. `0` = shortest path however jagged |
| `maxExpansions` | `20000` | Hard cap so a pathological graph cannot stall a render pass |
| `obstacleInset` | `1.0` | Tolerance in `PathHasObstacles`, so a line along a border is not a collision |
| `obstaclePaddingCells` | `0.5` | Padding around an obstacle when blocking A\* cells, as a fraction of `gridSize` |

## Multiple connections into one face

When several relationships land on the same side of a box, anchoring them all
at the face midpoint makes the arrows overlap. Count the connections per face
first, then ask for a distributed anchor:

```cpp
int index = IndexOfThisConnectionOnFace(targetId, targetSide);
int count = ConnectionsOnFace(targetId, targetSide);
Point2Dd end = UltraCanvasDiagramRouter::GetDistributedCardinalPoint(
    targetBox, targetSide, index, count);
```

## Notes for callers

- **Pre-filter the obstacle list.** Including the source or target box means A\*
  has nowhere to start and the router silently falls back to the cardinal path.
- **`routingArea` bounds the search**, not the drawing. Points outside it are
  still routed to; only A\* cells outside are treated as blocked.
- **`gridSize` is a cost/quality dial.** Smaller grids route more tightly and
  cost more; the FlowChart convention is to reuse the chart's visual grid
  spacing.
- Use `ComputeApproachAngle` for orthogonal and curved connections and
  `ComputeFinalSegmentAngle` for straight ones — the first is correct even for a
  degenerate two-point path, where the second has no segment to measure.

## History

Extracted from `UltraCanvasFlowChart.cpp` at FlowChart 2.3.0. The algorithm and
its constants are unchanged. Three defects present in the original were fixed
during extraction, all found by the new property tests and confirmed against the
pre-extraction code over 12k randomised layouts (9.7% of paths differ; 99.8% of
those differences are one of these cases):

1. **Use-after-free.** The A\* loop held a reference into the `visited` vector
   across a `push_back` into that same vector; once the search outgrew the
   reserved capacity, it read freed memory.
2. **Diagonal segments.** End-of-path bridging assumed the preceding waypoint
   already shared an axis with the bridge point; when it did not, an
   "orthogonal" path contained a visible diagonal jog.
3. **Degenerate waypoints.** Endpoints already sharing an axis produced
   zero-length segments, which make the arrow-angle computation meaningless.

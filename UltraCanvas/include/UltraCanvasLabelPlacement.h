// include/UltraCanvasLabelPlacement.h
// Shared shape-label placement solver for diagrams (Venn, block, node, ...).
// Given a list of shapes (circle or rectangle, each flagged keep-inside or
// keep-outside) and a list of labels tied to those shapes, computes the best
// non-overlapping position for every label, honouring an optional preferred
// side (top, bottom, left, right, inside) and an optional prioritised list
// of inside anchors (top-left, centre-top, ...) or border angles (clock-face
// positions straddling the shape's edge) with automatic fallback to the next
// position when the preferred one collides.
// Version: 2.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <memory>
#include <string>
#include <vector>
#include <cstddef>

namespace UltraCanvas {

// =============================================================================
// LABEL PLACEMENT DATA STRUCTURES
// =============================================================================

enum class LabelShapeType {
    Circle,
    Rectangle           // Also covers rounded rectangles
};

// Preferred (input) or resolved (output) location of a label relative to its
// shape. Auto lets the solver pick the least crowded side. Border means the
// label straddles the shape's edge, centred on the border point given by the
// label's borderAngles (acceptable for node-style diagrams where the text is
// clearly visible half inside, half outside the shape - pair it with a halo
// behind the text for readability).
enum class LabelSide {
    Auto,
    Top,
    Bottom,
    Left,
    Right,
    Inside,
    Border
};

// Anchor position of an inside label within its shape. Used to build the
// prioritised candidate list for inside placement: the caller lists anchors
// most-preferred first and the solver falls back to later ones when earlier
// ones collide with other labels, shapes, obstacles or the bounds.
// For circles the anchors map onto offsets within the disc (Center = circle
// centre, TopCenter = above centre, corner anchors = diagonal offsets).
enum class LabelAnchor {
    TopLeft, TopCenter, TopRight,
    CenterLeft, Center, CenterRight,
    BottomLeft, BottomCenter, BottomRight
};

struct LabelShape {
    LabelShapeType type = LabelShapeType::Circle;
    Point2Dd center;
    double radius = 0.0;            // Used when type == Circle
    Size2Dd size;                   // Full extents, used when type == Rectangle
    // true: the label belongs inside the shape (containment/nested diagrams);
    // false: the label must stay fully outside the shape (overlap diagrams).
    bool keepLabelInside = false;
    // Containers are background enclosures (e.g. the parent circle of a
    // hierarchical packing): labels are not pushed off them, and labels that
    // name one via ShapeLabel::containerShape are kept within it.
    bool isContainer = false;

    Rect2Dd BoundingRect() const {
        if (type == LabelShapeType::Circle) {
            return Rect2Dd(center.x - radius, center.y - radius, radius * 2.0, radius * 2.0);
        }
        return Rect2Dd(center.x - size.width * 0.5, center.y - size.height * 0.5,
                       size.width, size.height);
    }
};

struct ShapeLabel {
    std::string text;
    size_t shapeIndex = 0;                      // Shape this label belongs to
    LabelSide preferredSide = LabelSide::Auto;  // Optional preferred location
    // Measured text extents; fill from IRenderContext::GetTextLineDimensions()
    // with the font that will be used for drawing.
    Size2Dd textSize;
    // Prioritised anchors for inside placement, most-preferred first. Charts
    // with different label conventions supply their own order (e.g. a nested
    // chart prefers the corner opposite its shared anchor corner). Empty uses
    // the per-shape-type default: rectangles top-left / top-centre /
    // top-right / bottom row / centre row, circles centre / above / below.
    std::vector<LabelAnchor> anchorPriority;
    // Border-straddle positions, clock-face degrees (0 = 12 o'clock,
    // clockwise, so 60 = the 2 o'clock position). When non-empty, candidates
    // centred on the shape's border at these angles are tried first, in list
    // order, with the regular outside sides as fallback. Straddling the own
    // shape is not penalised for these candidates. Setting preferredSide to
    // Border with an empty list uses a default angle order starting at
    // 2 o'clock. Ignored for inside placement.
    std::vector<double> borderAngles;
    // Optional index of a shape (usually flagged isContainer) this label must
    // stay within; the part of the label sticking out of it is penalised like
    // an out-of-bounds excursion. -1 = no containment.
    int containerShape = -1;
    // Inside placement only: do not penalise the part of the label sticking
    // out of its own shape. Node-style diagrams draw the name across the node
    // outline on purpose (pair it with a text halo), so the overflow is the
    // intended look rather than a defect to score against.
    bool tolerateShapeOverflow = false;
    // Inside placement only: also offer the outside sides as last-resort
    // candidates, priced well above every inside anchor. Lets a node label
    // step off its node when all the inside anchors are blocked, instead of
    // being stacked on top of a label that is already there.
    bool allowOutsideFallback = false;
    // Rotation of the drawn text in degrees, clockwise about the label's
    // centre (45 = a tilted axis tick label). Candidate positions reserve the
    // rotated footprint and collisions use the exact rotated quad, so tilted
    // labels pack correctly against upright ones.
    double rotationDegrees = 0.0;
    // Placement priority. Labels are placed most-important first regardless of
    // their position in the input array, with input order as the tie-break, so
    // a chart can guarantee that the min/max/pinned labels claim their spot
    // before the ordinary ones. All-equal priorities reproduce input order
    // exactly.
    int priority = 0;
    // When the label cannot be drawn cleanly - it would overlap something
    // already claimed, or would have to hang outside the bounds to avoid doing
    // so - drop it instead. The result is flagged `suppressed` and the label
    // claims no space, so later labels may use it. This is how a chart thins
    // dense value labels: offer them all and draw what survives.
    bool allowSuppress = false;
    // Ask for a connector when the label ends up far from its shape: the
    // result carries `hasLeader` and the `leaderFrom` point on the shape edge.
    bool wantLeaderLine = false;
    // Point-anchored label: place around `anchorPoint` instead of a shape from
    // the shapes array. Charts labelling data points use this instead of
    // fabricating a zero-radius shape each frame. `shapeIndex` is ignored.
    bool usePointAnchor = false;
    Point2Dd anchorPoint;
};

struct PlacedShapeLabel {
    Rect2Dd bounds;                    // Final label rect; draw at bounds.TopLeft()
    LabelSide side = LabelSide::Top;   // Side that was actually used
    bool fitted = true;                // false if some overlap could not be resolved
    // true when the label was dropped rather than overprinted (only possible
    // with ShapeLabel::allowSuppress or LabelDeclutterPolicy::SuppressColliding).
    // A suppressed label must not be drawn; its bounds are the last position
    // tried and it occupies no space in the collision world.
    bool suppressed = false;
    // Rotation to draw the text with, echoed from the request. `bounds` is the
    // axis-aligned box of the rotated label: draw the text centred in it and
    // rotated by this angle.
    double rotationDegrees = 0.0;
    // Connector request result (see ShapeLabel::wantLeaderLine): draw a line
    // from `leaderFrom` on the shape edge to the nearest edge of `bounds`.
    bool hasLeader = false;
    Point2Dd leaderFrom;
};

// What to do with a label that cannot be placed cleanly - one that would
// overlap a neighbour, or leave the bounds in order not to.
enum class LabelDeclutterPolicy {
    PlaceAll,           // Keep every label, drawing it badly if it must (pre-2.0 behaviour)
    SuppressColliding   // Drop such labels, whether or not they opted in
};

struct LabelPlacementOptions {
    // Area labels are kept within (usually the diagram's local bounds).
    // Ignored when width or height is zero.
    Rect2Dd bounds;
    double shapeMargin = 6.0;       // Gap between a label and its shape's edge
    double labelMargin = 6.0;       // Minimum gap between neighbouring labels
    bool avoidOtherShapes = true;   // Penalise labels covering other shapes
    // Keep-out rectangles labels are expelled from exactly like from already
    // placed labels (kept labelMargin away). Use for connector lines, leader
    // lines, axes, markers or any other geometry labels must not cover.
    // Polylines are better added through LabelPlacementSession::
    // AddObstaclePolyline(), which keeps each segment tight instead of using
    // the (often huge) bounding rect of a diagonal run.
    std::vector<Rect2Dd> obstacles;
    // Minimum clear distance between neighbouring labels. Whichever of this and
    // labelMargin is larger wins, so leaving it at 0 keeps the pre-2.0
    // behaviour. Charts drawing many value labels raise it to force visible
    // breathing space rather than merely non-overlapping text.
    double minLabelSeparation = 0.0;
    LabelDeclutterPolicy declutter = LabelDeclutterPolicy::PlaceAll;
};

// =============================================================================
// SOLVER
// =============================================================================

// Computes a position for every label so that labels do not overlap each other
// (kept at least labelMargin apart), sit shapeMargin away from their shape's
// edge (outside labels) or inside it (keep-inside shapes / Inside preference),
// avoid covering other shapes where possible, and stay within bounds.
// Results are returned in the same order as the input labels.
// Labels are placed greedily in priority order, falling back to input order for
// equal priorities, so the input order doubles as a priority the way it always
// has: earlier labels claim their preferred spot and later labels are steered
// around them. Callers should submit the most constrained labels first (e.g.
// nested charts submit the smallest, innermost shapes first).
//
// This is a convenience wrapper over LabelPlacementSession for callers that
// solve all their labels in one pass. Anything that places labels in several
// passes - axis furniture, then series values, then a legend, then annotations
// - should hold a session instead, so each pass sees what the earlier ones
// claimed.
std::vector<PlacedShapeLabel> PlaceShapeLabels(
    const std::vector<LabelShape>& shapes,
    const std::vector<ShapeLabel>& labels,
    const LabelPlacementOptions& options);

// A persistent collision world: shapes, obstacles and every label placed so
// far, backed by a uniform-grid spatial index so cost stays near-linear in the
// label count instead of quadratic.
//
// The intended lifetime is the *chart*, not the frame: build it when the chart
// is created, Place() each group of labels as it becomes known, keep the solved
// rectangles, and draw from them on every redraw. Reset() it only when
// something that moves labels changes - data, geometry, fonts, axis ranges.
// Solving during a redraw is exactly what this class exists to avoid.
class LabelPlacementSession {
public:
    explicit LabelPlacementSession(const LabelPlacementOptions& options);
    ~LabelPlacementSession();

    LabelPlacementSession(const LabelPlacementSession&) = delete;
    LabelPlacementSession& operator=(const LabelPlacementSession&) = delete;

    // Shapes labels can belong to. Returns the index of the first shape added,
    // so a caller adding several batches can map its own items onto
    // ShapeLabel::shapeIndex.
    size_t AddShapes(const std::vector<LabelShape>& shapes);
    size_t AddShape(const LabelShape& shape);

    // Keep-out geometry. Labels are pushed off these exactly as they are off
    // already-placed labels.
    void AddObstacleRects(const std::vector<Rect2Dd>& rects);
    void AddObstacleRect(const Rect2Dd& rect);
    // Each segment of the polyline becomes its own keep-out quad of the given
    // half width, so a diagonal run costs a thin band rather than the bounding
    // box of the whole line.
    void AddObstaclePolyline(const std::vector<Point2Dd>& points, double halfWidth);

    // Places one group of labels against everything the session already knows,
    // and remembers the result for the next group. Results come back in the
    // order the labels were passed in.
    std::vector<PlacedShapeLabel> Place(const std::vector<ShapeLabel>& labels);

    // Rectangles claimed so far (obstacles first, then placed labels in
    // placement order). Suppressed labels are not included - they claim nothing.
    const std::vector<Rect2Dd>& ClaimedRects() const;

    size_t ShapeCount() const;
    // Drops every shape, obstacle and placed label; keeps the options.
    void Reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// =============================================================================
// AXIS TICK DECLUTTER (1-D fast path)
// =============================================================================

// Tick labels sit at constructed positions along one axis and only ever
// collide with their own neighbours, so they do not belong in the 2-D solver.
// This resolves them as an interval problem instead.
enum class TickDeclutterPolicy {
    KeepEveryNth,   // Uniform thinning: keep every n-th label, smallest n that fits.
                    // Regular spacing, the classic axis look.
    Greedy,         // Walk the axis keeping every label that clears the last kept
                    // one. Keeps more labels, at uneven spacing.
    PriorityGreedy  // Keep the highest-priority labels first (min/max/current),
                    // then fill in whatever else still fits.
};

// Decides which tick labels to draw. `positions` are the label centres along
// the axis and `extents` their sizes along the same axis (width for a
// horizontal axis, height for a vertical one), both in the axis' own order.
// Neighbouring kept labels end up at least `minGap` apart. `priorities` is used
// by PriorityGreedy only and may be null; higher values are kept first.
// Returns one flag per input label, in input order.
std::vector<bool> DeclutterAxisTicks(const std::vector<double>& positions,
                                     const std::vector<double>& extents,
                                     double minGap,
                                     TickDeclutterPolicy policy,
                                     const std::vector<int>* priorities = nullptr);

} // namespace UltraCanvas

// include/Plugins/Charts/UltraCanvasLabelPlacement.h
// Shared shape-label placement solver for diagrams (Venn, block, node, ...).
// Given a list of shapes (circle or rectangle, each flagged keep-inside or
// keep-outside) and a list of labels tied to those shapes, computes the best
// non-overlapping position for every label, honouring an optional preferred
// side (top, bottom, left, right, inside) and an optional prioritised list
// of inside anchors (top-left, centre-top, ...) or border angles (clock-face
// positions straddling the shape's edge) with automatic fallback to the next
// position when the preferred one collides.
// Version: 1.2.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
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
};

struct PlacedShapeLabel {
    Rect2Dd bounds;                    // Final label rect; draw at bounds.TopLeft()
    LabelSide side = LabelSide::Top;   // Side that was actually used
    bool fitted = true;                // false if some overlap could not be resolved
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
    // lines, axes, markers or any other geometry labels must not cover;
    // approximate a line by the bounding rects of its segments.
    std::vector<Rect2Dd> obstacles;
};

// =============================================================================
// SOLVER
// =============================================================================

// Computes a position for every label so that labels do not overlap each other
// (kept at least labelMargin apart), sit shapeMargin away from their shape's
// edge (outside labels) or inside it (keep-inside shapes / Inside preference),
// avoid covering other shapes where possible, and stay within bounds.
// Results are returned in the same order as the input labels.
// Labels are placed greedily in input order, so the order doubles as a
// priority: earlier labels claim their preferred spot and later labels are
// steered around them. Callers should submit the most constrained labels
// first (e.g. nested charts submit the smallest, innermost shapes first).
std::vector<PlacedShapeLabel> PlaceShapeLabels(
    const std::vector<LabelShape>& shapes,
    const std::vector<ShapeLabel>& labels,
    const LabelPlacementOptions& options);

} // namespace UltraCanvas

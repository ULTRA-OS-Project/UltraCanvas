// include/Plugins/Charts/UltraCanvasLabelPlacement.h
// Shared shape-label placement solver for diagrams (Venn, Sankey, flowchart,
// block, node, ...). Given a list of shapes (circle, rectangle or line
// segment, each flagged keep-inside or keep-outside) and a list of labels
// tied to those shapes, computes the best non-overlapping position for every
// label, honouring an optional preferred side (top, bottom, left, right,
// inside, radial). Supports rotated labels, per-label priorities with
// optional hiding, and leader-line suggestions for labels pushed far from
// their shape.
// Version: 1.1.0
// Last Modified: 2026-07-24
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
    Rectangle,          // Also covers rounded rectangles
    Segment             // Line segment (edge/connector); also usable as an obstacle
};

// Preferred (input) or resolved (output) location of a label relative to its
// shape. Auto lets the solver pick the least crowded side. Inside places the
// label within the shape — for Segment shapes that means centred on the line
// (the classic connector-label pill). Radial places the label outside the
// shape along ShapeLabel::radialAngleDeg (gauge/pie style).
enum class LabelSide {
    Auto,
    Top,
    Bottom,
    Left,
    Right,
    Inside,
    Radial
};

struct LabelShape {
    LabelShapeType type = LabelShapeType::Circle;
    Point2Dd center;
    double radius = 0.0;            // Used when type == Circle
    Size2Dd size;                   // Full extents, used when type == Rectangle
    Point2Dd p1, p2;                // Endpoints, used when type == Segment
    double thickness = 2.0;         // Segment stroke thickness (obstacle width)
    // true: the label belongs inside the shape (containment/nested diagrams);
    // false: the label must stay fully outside the shape (overlap diagrams).
    // Ignored for Segment shapes (Inside there means "on the line").
    bool keepLabelInside = false;

    // Shapes without any label attached act purely as obstacles: labels of
    // other shapes avoid them but they receive no label themselves.

    Rect2Dd BoundingRect() const {
        switch (type) {
            case LabelShapeType::Circle:
                return Rect2Dd(center.x - radius, center.y - radius, radius * 2.0, radius * 2.0);
            case LabelShapeType::Segment: {
                double half = thickness * 0.5;
                double left = (p1.x < p2.x ? p1.x : p2.x) - half;
                double top = (p1.y < p2.y ? p1.y : p2.y) - half;
                double right = (p1.x > p2.x ? p1.x : p2.x) + half;
                double bottom = (p1.y > p2.y ? p1.y : p2.y) + half;
                return Rect2Dd(left, top, right - left, bottom - top);
            }
            default:
                return Rect2Dd(center.x - size.width * 0.5, center.y - size.height * 0.5,
                               size.width, size.height);
        }
    }
};

struct ShapeLabel {
    std::string text;
    size_t shapeIndex = 0;                      // Shape this label belongs to
    LabelSide preferredSide = LabelSide::Auto;  // Optional preferred location
    // Measured text extents; fill from IRenderContext::GetTextLineDimensions()
    // with the font that will be used for drawing.
    Size2Dd textSize;
    // Rotation of the label around its centre, degrees clockwise (screen
    // coordinates). The solver collides using the rotated bounding box; draw
    // by translating to bounds.Center(), rotating, and drawing the text at
    // (-textSize/2, -textSize/2).
    double angleDeg = 0.0;
    // Anchor angle for LabelSide::Radial, degrees (0 = +x, 90 = down).
    double radialAngleDeg = 0.0;
    // Higher priority labels are placed first and hidden last. Ties keep the
    // input order.
    double priority = 0.0;
};

struct PlacedShapeLabel {
    // Final label rect (axis-aligned bounding box for rotated labels).
    // For unrotated labels draw at bounds.TopLeft().
    Rect2Dd bounds;
    LabelSide side = LabelSide::Top;   // Side that was actually used
    bool fitted = true;                // false if some overlap could not be resolved
    bool hidden = false;               // true when the label was dropped (allowHide)
    // Closest point on the own shape's boundary to the label — the natural
    // attachment point for a leader line.
    Point2Dd anchorPoint;
    // true when the gap between shape and label exceeds
    // LabelPlacementOptions::leaderThreshold (and that threshold is > 0).
    bool needsLeader = false;
};

struct LabelPlacementOptions {
    // Area labels are kept within (usually the diagram's local bounds).
    // Ignored when width or height is zero.
    Rect2Dd bounds;
    double shapeMargin = 6.0;       // Gap between a label and its shape's edge
    double labelMargin = 6.0;       // Minimum gap between neighbouring labels
    bool avoidOtherShapes = true;   // Penalise labels covering other shapes
    // When true, a label whose overlap cannot be resolved is hidden
    // (PlacedShapeLabel::hidden) instead of being force-placed. Lower
    // priority labels are hidden first because they are placed last.
    bool allowHide = false;
    // When > 0, labels whose distance to their shape exceeds this value get
    // needsLeader = true so the caller can draw a leader line to anchorPoint.
    double leaderThreshold = 0.0;
};

// =============================================================================
// SOLVER
// =============================================================================

// Computes a position for every label so that labels do not overlap each other
// (kept at least labelMargin apart), sit shapeMargin away from their shape's
// edge (outside labels), inside it (keep-inside shapes / Inside preference),
// or on it (Segment shapes with Inside preference), avoid covering other
// shapes where possible, and stay within bounds.
// Results are returned in the same order as the input labels.
std::vector<PlacedShapeLabel> PlaceShapeLabels(
    const std::vector<LabelShape>& shapes,
    const std::vector<ShapeLabel>& labels,
    const LabelPlacementOptions& options);

} // namespace UltraCanvas

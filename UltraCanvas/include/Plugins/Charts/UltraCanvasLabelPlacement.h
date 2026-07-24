// include/Plugins/Charts/UltraCanvasLabelPlacement.h
// Shared shape-label placement solver for diagrams (Venn, block, node, ...).
// Given a list of shapes (circle or rectangle, each flagged keep-inside or
// keep-outside) and a list of labels tied to those shapes, computes the best
// non-overlapping position for every label, honouring an optional preferred
// side (top, bottom, left, right, inside).
// Version: 1.0.0
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
    Rectangle           // Also covers rounded rectangles
};

// Preferred (input) or resolved (output) location of a label relative to its
// shape. Auto lets the solver pick the least crowded side.
enum class LabelSide {
    Auto,
    Top,
    Bottom,
    Left,
    Right,
    Inside
};

struct LabelShape {
    LabelShapeType type = LabelShapeType::Circle;
    Point2Dd center;
    double radius = 0.0;            // Used when type == Circle
    Size2Dd size;                   // Full extents, used when type == Rectangle
    // true: the label belongs inside the shape (containment/nested diagrams);
    // false: the label must stay fully outside the shape (overlap diagrams).
    bool keepLabelInside = false;

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
};

// =============================================================================
// SOLVER
// =============================================================================

// Computes a position for every label so that labels do not overlap each other
// (kept at least labelMargin apart), sit shapeMargin away from their shape's
// edge (outside labels) or inside it (keep-inside shapes / Inside preference),
// avoid covering other shapes where possible, and stay within bounds.
// Results are returned in the same order as the input labels.
std::vector<PlacedShapeLabel> PlaceShapeLabels(
    const std::vector<LabelShape>& shapes,
    const std::vector<ShapeLabel>& labels,
    const LabelPlacementOptions& options);

} // namespace UltraCanvas

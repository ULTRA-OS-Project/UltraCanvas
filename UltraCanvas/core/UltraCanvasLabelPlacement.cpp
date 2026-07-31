// core/UltraCanvasLabelPlacement.cpp
// Shared shape-label placement solver for diagrams
// Version: 1.3.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework

#include "UltraCanvasLabelPlacement.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

namespace {

// A concrete position being considered for one label. baseCost encodes the
// static preference (side order, slide distance); the dynamic score adds
// overlap and out-of-bounds penalties on top.
struct Candidate {
    Rect2Dd rect;
    LabelSide side;
    double baseCost;
};

Rect2Dd Expand(const Rect2Dd& r, double m) {
    return Rect2Dd(r.x - m, r.y - m, r.width + m * 2.0, r.height + m * 2.0);
}

double OverlapArea(const Rect2Dd& a, const Rect2Dd& b) {
    double w = std::min(a.Right(), b.Right()) - std::max(a.Left(), b.Left());
    double h = std::min(a.Bottom(), b.Bottom()) - std::max(a.Top(), b.Top());
    return (w > 0.0 && h > 0.0) ? w * h : 0.0;
}

// Approximate area of shape ∩ rect. Exact for rectangles and for rects fully
// inside / fully outside a circle; partial circle overlaps use the bounding
// box intersection scaled by the circle/square area ratio, which is accurate
// enough for scoring.
double ShapeOverlapArea(const LabelShape& s, const Rect2Dd& rect) {
    double area = OverlapArea(s.BoundingRect(), rect);
    if (area <= 0.0) return 0.0;
    if (s.type == LabelShapeType::Circle) {
        double r2 = s.radius * s.radius;
        // Closest point of the rect to the circle centre: outside the radius
        // means the rect only clips the empty corners of the bounding box.
        double cx = std::clamp(s.center.x, rect.Left(), rect.Right());
        double cy = std::clamp(s.center.y, rect.Top(), rect.Bottom());
        double dx = cx - s.center.x, dy = cy - s.center.y;
        if (dx * dx + dy * dy >= r2) return 0.0;
        // Farthest corner inside the radius means the rect is fully contained.
        double fx = std::max(std::abs(rect.Left() - s.center.x), std::abs(rect.Right() - s.center.x));
        double fy = std::max(std::abs(rect.Top() - s.center.y), std::abs(rect.Bottom() - s.center.y));
        if (fx * fx + fy * fy <= r2) return rect.width * rect.height;
        area *= M_PI / 4.0;
    }
    return area;
}

double Score(const Candidate& c, size_t ownIndex, int containerIndex,
             bool tolerateOverflow,
             const std::vector<LabelShape>& shapes,
             const std::vector<Rect2Dd>& placed,
             const LabelPlacementOptions& opt) {
    double labelArea = std::max(1.0, c.rect.width * c.rect.height);
    double cost = c.baseCost;

    const LabelShape& own = shapes[ownIndex];
    if (c.side == LabelSide::Border || (c.side == LabelSide::Inside && tolerateOverflow)) {
        // Straddling the own shape's edge is the intent - no own-shape penalty.
    } else if (c.side == LabelSide::Inside) {
        // Penalise the part of the label sticking out of its own shape.
        cost += 200.0 * (labelArea - ShapeOverlapArea(own, c.rect)) / labelArea;
    } else {
        cost += 400.0 * ShapeOverlapArea(own, c.rect) / labelArea;
    }

    if (opt.avoidOtherShapes) {
        for (size_t j = 0; j < shapes.size(); ++j) {
            if (j == ownIndex || shapes[j].isContainer) continue;
            cost += 60.0 * ShapeOverlapArea(shapes[j], c.rect) / labelArea;
        }
    }

    if (opt.bounds.width > 0.0 && opt.bounds.height > 0.0) {
        cost += 300.0 * (labelArea - OverlapArea(opt.bounds, c.rect)) / labelArea;
    }

    if (containerIndex >= 0 && static_cast<size_t>(containerIndex) < shapes.size()) {
        // Keep the label within its container shape (e.g. the parent circle
        // of a hierarchical packing), like a second, shaped bounds.
        cost += 300.0 * (labelArea - ShapeOverlapArea(shapes[containerIndex], c.rect))
                / labelArea;
    }

    for (const Rect2Dd& p : placed) {
        double o = OverlapArea(Expand(p, opt.labelMargin), c.rect);
        if (o > 0.0) cost += 150.0 + 500.0 * o / labelArea;
    }

    return cost;
}

// Candidates outside the shape on one side. Besides the centred position the
// label may slide along that side so neighbouring labels can sit next to each
// other; positions are emitted preferring small slides, biased away from the
// diagram centre first.
void AddOutsideCandidates(std::vector<Candidate>& out, const LabelShape& s,
                          const Size2Dd& text, LabelSide side,
                          double margin, double sideCost, double slideBias) {
    Rect2Dd bb = s.BoundingRect();
    double halfW = bb.width * 0.5;
    double halfH = bb.height * 0.5;
    bool horizontal = (side == LabelSide::Top || side == LabelSide::Bottom);
    double slideMax = horizontal ? halfW : halfH;

    static const double fractions[] = {0.0, 0.35, 0.7, 1.0};
    double sign = (slideBias < 0.0) ? -1.0 : 1.0;

    for (double f : fractions) {
        for (int flip = 0; flip < (f == 0.0 ? 1 : 2); ++flip) {
            double offset = f * slideMax * (flip == 0 ? sign : -sign);
            Rect2Dd r;
            switch (side) {
                case LabelSide::Top:
                    r = Rect2Dd(s.center.x - text.width * 0.5 + offset,
                                s.center.y - halfH - margin - text.height,
                                text.width, text.height);
                    break;
                case LabelSide::Bottom:
                    r = Rect2Dd(s.center.x - text.width * 0.5 + offset,
                                s.center.y + halfH + margin,
                                text.width, text.height);
                    break;
                case LabelSide::Left:
                    r = Rect2Dd(s.center.x - halfW - margin - text.width,
                                s.center.y - text.height * 0.5 + offset,
                                text.width, text.height);
                    break;
                default: // Right
                    r = Rect2Dd(s.center.x + halfW + margin,
                                s.center.y - text.height * 0.5 + offset,
                                text.width, text.height);
                    break;
            }
            out.push_back({r, side, sideCost + f * 2.0 + (flip == 0 ? 0.0 : 0.5)});
        }
    }
}

// Rect for one inside anchor. Rectangle shapes pin the label into the
// corresponding corner/edge with a margin inset; circle shapes offset the
// label from the centre by half the radius (corners diagonally by
// 0.5r/sqrt(2) so the label stays within the disc).
Rect2Dd InsideAnchorRect(const LabelShape& s, const Size2Dd& text,
                         LabelAnchor anchor, double margin) {
    int col = static_cast<int>(anchor) % 3 - 1;   // -1 left, 0 centre, 1 right
    int row = static_cast<int>(anchor) / 3 - 1;   // -1 top,  0 centre, 1 bottom
    if (s.type == LabelShapeType::Rectangle) {
        Rect2Dd bb = s.BoundingRect();
        double xIn = margin * 2.0;
        double yIn = margin;
        double x = (col < 0) ? bb.Left() + xIn
                 : (col > 0) ? bb.Right() - xIn - text.width
                             : s.center.x - text.width * 0.5;
        double y = (row < 0) ? bb.Top() + yIn
                 : (row > 0) ? bb.Bottom() - yIn - text.height
                             : s.center.y - text.height * 0.5;
        return Rect2Dd(x, y, text.width, text.height);
    }
    double f = (col != 0 && row != 0) ? 0.5 * M_SQRT1_2 : 0.5;
    return Rect2Dd(s.center.x + col * f * s.radius - text.width * 0.5,
                   s.center.y + row * f * s.radius - text.height * 0.5,
                   text.width, text.height);
}

// Candidates inside the shape, in the caller's priority order (earlier
// anchors get lower base cost, so they win unless they collide). Without an
// explicit priority, rectangles prefer the top-left corner (the classic
// set-hierarchy look), then the other edges and the centre; circles prefer
// the centre, then upper and lower positions.
void AddInsideCandidates(std::vector<Candidate>& out, const LabelShape& s,
                         const Size2Dd& text, double margin,
                         const std::vector<LabelAnchor>& priority) {
    static const std::vector<LabelAnchor> rectDefault = {
        LabelAnchor::TopLeft, LabelAnchor::TopCenter, LabelAnchor::TopRight,
        LabelAnchor::BottomLeft, LabelAnchor::BottomCenter, LabelAnchor::BottomRight,
        LabelAnchor::CenterLeft, LabelAnchor::Center, LabelAnchor::CenterRight,
    };
    static const std::vector<LabelAnchor> circleDefault = {
        LabelAnchor::Center, LabelAnchor::TopCenter, LabelAnchor::BottomCenter,
    };
    const std::vector<LabelAnchor>& anchors =
        !priority.empty() ? priority
        : (s.type == LabelShapeType::Rectangle) ? rectDefault : circleDefault;

    double cost = 0.0;
    for (LabelAnchor anchor : anchors) {
        out.push_back({InsideAnchorRect(s, text, anchor, margin),
                       LabelSide::Inside, cost});
        cost += 0.5;
    }
}

// Candidates straddling the shape's border, centred on the border point at
// each clock-face angle (0 = 12 o'clock, clockwise; 60 = 2 o'clock), in the
// caller's priority order.
void AddBorderCandidates(std::vector<Candidate>& out, const LabelShape& s,
                         const Size2Dd& text, const std::vector<double>& angles) {
    double cost = 0.0;
    for (double deg : angles) {
        double rad = deg * (M_PI / 180.0);
        double dx = std::sin(rad);
        double dy = -std::cos(rad);
        Point2Dd p = s.center;
        if (s.type == LabelShapeType::Circle) {
            p.x += dx * s.radius;
            p.y += dy * s.radius;
        } else {
            // Ray-to-border intersection of the rectangle.
            double tx = (std::abs(dx) > 1e-9) ? (s.size.width * 0.5) / std::abs(dx) : 1e18;
            double ty = (std::abs(dy) > 1e-9) ? (s.size.height * 0.5) / std::abs(dy) : 1e18;
            double t = std::min(tx, ty);
            p.x += dx * t;
            p.y += dy * t;
        }
        out.push_back({Rect2Dd(p.x - text.width * 0.5, p.y - text.height * 0.5,
                               text.width, text.height),
                       LabelSide::Border, cost});
        cost += 1.0;
    }
}

LabelSide OppositeSide(LabelSide side) {
    switch (side) {
        case LabelSide::Top:    return LabelSide::Bottom;
        case LabelSide::Bottom: return LabelSide::Top;
        case LabelSide::Left:   return LabelSide::Right;
        default:                return LabelSide::Left;
    }
}

// Shift rect into bounds where possible (labels may be taller/wider than the
// bounds; then the top/left edge wins).
Rect2Dd ClampToBounds(Rect2Dd r, const LabelPlacementOptions& opt) {
    if (opt.bounds.width <= 0.0 || opt.bounds.height <= 0.0) return r;
    r.x = std::min(r.x, opt.bounds.Right() - r.width);
    r.y = std::min(r.y, opt.bounds.Bottom() - r.height);
    r.x = std::max(r.x, opt.bounds.Left());
    r.y = std::max(r.y, opt.bounds.Top());
    return r;
}

bool InsideBounds(const Rect2Dd& r, const LabelPlacementOptions& opt) {
    if (opt.bounds.width <= 0.0 || opt.bounds.height <= 0.0) return true;
    return r.Left() >= opt.bounds.Left() - 0.5 && r.Right() <= opt.bounds.Right() + 0.5 &&
           r.Top() >= opt.bounds.Top() - 0.5 && r.Bottom() <= opt.bounds.Bottom() + 0.5;
}

// Slide rect along its free axis (x for top/bottom/inside labels, y for
// left/right) in one direction until it clears every already placed label by
// labelMargin. Returns the shifted rect and the total distance moved, or a
// negative distance when no clear position was found.
std::pair<Rect2Dd, double> SlideClear(Rect2Dd r, bool horizontal, double sign,
                                      const std::vector<Rect2Dd>& placed,
                                      const LabelPlacementOptions& opt) {
    double total = 0.0;
    for (int iter = 0; iter < 16; ++iter) {
        bool collided = false;
        for (const Rect2Dd& p : placed) {
            Rect2Dd pe = Expand(p, opt.labelMargin);
            if (OverlapArea(pe, r) <= 0.0) continue;
            double delta = horizontal
                ? ((sign > 0.0) ? pe.Right() - r.Left() : r.Right() - pe.Left())
                : ((sign > 0.0) ? pe.Bottom() - r.Top() : r.Bottom() - pe.Top());
            delta += 0.5;
            if (horizontal) r.x += sign * delta; else r.y += sign * delta;
            total += delta;
            collided = true;
        }
        if (!collided) return {r, total};
    }
    return {r, -1.0};
}

} // namespace

std::vector<PlacedShapeLabel> PlaceShapeLabels(
    const std::vector<LabelShape>& shapes,
    const std::vector<ShapeLabel>& labels,
    const LabelPlacementOptions& options) {

    std::vector<PlacedShapeLabel> results(labels.size());
    if (shapes.empty()) return results;

    Point2Dd centroid(0.0, 0.0);
    for (const LabelShape& s : shapes) {
        centroid.x += s.center.x;
        centroid.y += s.center.y;
    }
    centroid.x /= shapes.size();
    centroid.y /= shapes.size();

    // Obstacles behave exactly like labels that were already placed: they
    // repel candidates in scoring and are avoided by the slide resolution.
    std::vector<Rect2Dd> placed(options.obstacles.begin(), options.obstacles.end());
    placed.reserve(placed.size() + labels.size());

    for (size_t i = 0; i < labels.size(); ++i) {
        const ShapeLabel& label = labels[i];
        PlacedShapeLabel& result = results[i];
        if (label.shapeIndex >= shapes.size()) {
            result.bounds = Rect2Dd(options.bounds.x, options.bounds.y,
                                    label.textSize.width, label.textSize.height);
            result.fitted = false;
            continue;
        }

        const LabelShape& s = shapes[label.shapeIndex];
        double dx = s.center.x - centroid.x;
        double dy = s.center.y - centroid.y;

        std::vector<Candidate> candidates;
        bool wantInside = s.keepLabelInside || label.preferredSide == LabelSide::Inside;

        // The four outside sides, most promising first: the preferred side if
        // one was given, otherwise the vertical side facing away from the
        // diagram centre (wide, short labels read best above/below), then the
        // outward horizontal side, then the two remaining sides.
        auto addOutsideSides = [&](double sideCost) {
            LabelSide vertical = (dy <= 0.0) ? LabelSide::Top : LabelSide::Bottom;
            LabelSide horizontal = (dx <= 0.0) ? LabelSide::Left : LabelSide::Right;
            std::vector<LabelSide> order;
            if (label.preferredSide != LabelSide::Auto &&
                label.preferredSide != LabelSide::Border &&
                label.preferredSide != LabelSide::Inside) {
                order.push_back(label.preferredSide);
            }
            for (LabelSide side : {vertical, horizontal, OppositeSide(vertical), OppositeSide(horizontal)}) {
                if (std::find(order.begin(), order.end(), side) == order.end()) {
                    order.push_back(side);
                }
            }
            for (LabelSide side : order) {
                double bias = (side == LabelSide::Top || side == LabelSide::Bottom) ? dx : dy;
                AddOutsideCandidates(candidates, s, label.textSize, side,
                                     options.shapeMargin, sideCost, bias);
                sideCost += 8.0;
            }
        };

        if (wantInside) {
            AddInsideCandidates(candidates, s, label.textSize, options.shapeMargin,
                                label.anchorPriority);
            if (label.allowOutsideFallback) {
                // Priced above every inside anchor (which start at 0 and step
                // by 0.5) so stepping off the shape only wins over a genuine
                // collision, never over a merely imperfect inside anchor.
                addOutsideSides(40.0);
            }
        } else {
            // Border-straddle positions first when the label asks for them,
            // then the outside sides as fallback.
            std::vector<double> borderAngles = label.borderAngles;
            if (borderAngles.empty() && label.preferredSide == LabelSide::Border) {
                borderAngles = {60.0, 300.0, 120.0, 240.0, 0.0, 180.0};
            }
            AddBorderCandidates(candidates, s, label.textSize, borderAngles);
            addOutsideSides(borderAngles.empty() ? 0.0 : borderAngles.size() + 6.0);
        }

        const Candidate* best = nullptr;
        double bestScore = 0.0;
        for (const Candidate& c : candidates) {
            double score = Score(c, label.shapeIndex, label.containerShape,
                                 label.tolerateShapeOverflow,
                                 shapes, placed, options);
            if (!best || score < bestScore) {
                best = &c;
                bestScore = score;
            }
        }

        Rect2Dd rect = ClampToBounds(best->rect, options);
        result.side = best->side;
        result.fitted = true;

        // The scoring already steers labels apart; if the winner still touches
        // an earlier label, slide it along its free axis until the labels sit
        // next to each other separated by labelMargin.
        bool slideHorizontal = (best->side != LabelSide::Left && best->side != LabelSide::Right);
        auto posDir = SlideClear(rect, slideHorizontal, 1.0, placed, options);
        auto negDir = SlideClear(rect, slideHorizontal, -1.0, placed, options);
        bool posOk = posDir.second >= 0.0 && InsideBounds(posDir.first, options);
        bool negOk = negDir.second >= 0.0 && InsideBounds(negDir.first, options);
        if (posOk && (!negOk || posDir.second <= negDir.second)) {
            rect = posDir.first;
        } else if (negOk) {
            rect = negDir.first;
        } else if (posDir.second >= 0.0 || negDir.second >= 0.0) {
            // Cleared the other labels but left the bounds: keep the smaller
            // excursion rather than overlapping text.
            rect = (posDir.second >= 0.0 &&
                    (negDir.second < 0.0 || posDir.second <= negDir.second))
                       ? posDir.first : negDir.first;
        } else {
            result.fitted = false;
        }

        result.bounds = rect;
        placed.push_back(rect);
    }

    return results;
}

} // namespace UltraCanvas

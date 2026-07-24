// Plugins/Charts/UltraCanvasLabelPlacement.cpp
// Shared shape-label placement solver for diagrams
// Version: 1.1.0 (Segment anchors, rotation, radial mode, priority/hide, leaders)
// Last Modified: 2026-07-24
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasLabelPlacement.h"
#include <algorithm>
#include <cmath>
#include <numeric>

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

Rect2Dd RectAround(const Point2Dd& center, const Size2Dd& size) {
    return Rect2Dd(center.x - size.width * 0.5, center.y - size.height * 0.5,
                   size.width, size.height);
}

double OverlapArea(const Rect2Dd& a, const Rect2Dd& b) {
    double w = std::min(a.Right(), b.Right()) - std::max(a.Left(), b.Left());
    double h = std::min(a.Bottom(), b.Bottom()) - std::max(a.Top(), b.Top());
    return (w > 0.0 && h > 0.0) ? w * h : 0.0;
}

Point2Dd ClosestPointOnSegment(const Point2Dd& a, const Point2Dd& b, const Point2Dd& q) {
    double dx = b.x - a.x, dy = b.y - a.y;
    double len2 = dx * dx + dy * dy;
    if (len2 <= 1e-12) return a;
    double t = std::clamp(((q.x - a.x) * dx + (q.y - a.y) * dy) / len2, 0.0, 1.0);
    return Point2Dd(a.x + t * dx, a.y + t * dy);
}

Point2Dd ClampPointToRect(const Point2Dd& p, const Rect2Dd& r) {
    return Point2Dd(std::clamp(p.x, r.Left(), r.Right()),
                    std::clamp(p.y, r.Top(), r.Bottom()));
}

// The collision extents of a label: the axis-aligned bounding box of the text
// box rotated by angleDeg about its centre.
Size2Dd EffectiveSize(const ShapeLabel& label) {
    if (label.angleDeg == 0.0) return label.textSize;
    double a = label.angleDeg * M_PI / 180.0;
    double c = std::abs(std::cos(a)), s = std::abs(std::sin(a));
    return Size2Dd(label.textSize.width * c + label.textSize.height * s,
                   label.textSize.width * s + label.textSize.height * c);
}

// Approximate area of shape ∩ rect. Exact for rectangles and for rects fully
// inside / fully outside a circle; partial circle overlaps use the bounding
// box intersection scaled by the circle/square area ratio; segments sample
// the capsule around the line. Accurate enough for scoring.
double ShapeOverlapArea(const LabelShape& s, const Rect2Dd& rect) {
    if (s.type == LabelShapeType::Segment) {
        double half = std::max(s.thickness, 2.0) * 0.5;
        Rect2Dd er = Expand(rect, half);
        if (OverlapArea(er, s.BoundingRect()) <= 0.0) return 0.0;
        constexpr int kSamples = 17;
        int cnt = 0;
        for (int i = 0; i < kSamples; ++i) {
            double t = static_cast<double>(i) / (kSamples - 1);
            Point2Dd p(s.p1.x + (s.p2.x - s.p1.x) * t, s.p1.y + (s.p2.y - s.p1.y) * t);
            if (er.Contains(p)) ++cnt;
        }
        if (cnt == 0) return 0.0;
        double len = std::hypot(s.p2.x - s.p1.x, s.p2.y - s.p1.y);
        double area = (static_cast<double>(cnt) / kSamples) * len * half * 2.0;
        return std::min(area, rect.width * rect.height);
    }

    double area = OverlapArea(s.BoundingRect(), rect);
    if (area <= 0.0) return 0.0;
    if (s.type == LabelShapeType::Circle) {
        double r2 = s.radius * s.radius;
        // Closest point of the rect to the circle centre: outside the radius
        // means the rect only clips the empty corners of the bounding box.
        Point2Dd cp = ClampPointToRect(s.center, rect);
        double dx = cp.x - s.center.x, dy = cp.y - s.center.y;
        if (dx * dx + dy * dy >= r2) return 0.0;
        // Farthest corner inside the radius means the rect is fully contained.
        double fx = std::max(std::abs(rect.Left() - s.center.x), std::abs(rect.Right() - s.center.x));
        double fy = std::max(std::abs(rect.Top() - s.center.y), std::abs(rect.Bottom() - s.center.y));
        if (fx * fx + fy * fy <= r2) return rect.width * rect.height;
        area *= M_PI / 4.0;
    }
    return area;
}

double Score(const Candidate& c, size_t ownIndex,
             const std::vector<LabelShape>& shapes,
             const std::vector<Rect2Dd>& placed,
             const LabelPlacementOptions& opt) {
    double labelArea = std::max(1.0, c.rect.width * c.rect.height);
    double cost = c.baseCost;

    const LabelShape& own = shapes[ownIndex];
    if (c.side == LabelSide::Inside) {
        // A label on its own segment (connector pill) is intentional; for
        // area shapes penalise the part sticking out of the shape.
        if (own.type != LabelShapeType::Segment) {
            cost += 200.0 * (labelArea - ShapeOverlapArea(own, c.rect)) / labelArea;
        }
    } else {
        cost += 400.0 * ShapeOverlapArea(own, c.rect) / labelArea;
    }

    if (opt.avoidOtherShapes) {
        for (size_t j = 0; j < shapes.size(); ++j) {
            if (j == ownIndex) continue;
            cost += 60.0 * ShapeOverlapArea(shapes[j], c.rect) / labelArea;
        }
    }

    if (opt.bounds.width > 0.0 && opt.bounds.height > 0.0) {
        cost += 300.0 * (labelArea - OverlapArea(opt.bounds, c.rect)) / labelArea;
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

// Candidates inside the shape. Rectangles prefer the top-left corner (the
// classic set-hierarchy look), then the other edges and the centre; circles
// prefer the centre, then upper and lower positions.
void AddInsideCandidates(std::vector<Candidate>& out, const LabelShape& s,
                         const Size2Dd& text, double margin) {
    if (s.type == LabelShapeType::Rectangle) {
        Rect2Dd bb = s.BoundingRect();
        double xIn = margin * 2.0;
        double yIn = margin;
        double lX = bb.Left() + xIn;
        double cX = s.center.x - text.width * 0.5;
        double rX = bb.Right() - xIn - text.width;
        double tY = bb.Top() + yIn;
        double cY = s.center.y - text.height * 0.5;
        double bY = bb.Bottom() - yIn - text.height;
        const struct { double x, y, cost; } anchors[] = {
            {lX, tY, 0.0}, {cX, tY, 0.5}, {rX, tY, 1.0},
            {lX, bY, 1.5}, {cX, bY, 2.0}, {rX, bY, 2.5},
            {lX, cY, 3.0}, {cX, cY, 3.5}, {rX, cY, 4.0},
        };
        for (const auto& a : anchors) {
            out.push_back({Rect2Dd(a.x, a.y, text.width, text.height),
                           LabelSide::Inside, a.cost});
        }
    } else {
        const struct { double dy, cost; } anchors[] = {
            {0.0, 0.0}, {-0.5, 0.5}, {0.5, 1.0},
        };
        for (const auto& a : anchors) {
            out.push_back({RectAround(Point2Dd(s.center.x, s.center.y + a.dy * s.radius), text),
                           LabelSide::Inside, a.cost});
        }
    }
}

// Candidates for a Segment shape: on the line at several fractions along it
// (the classic connector pill), and offset perpendicular to the line on both
// sides. A concrete Top/Bottom/Left/Right preference favours the
// perpendicular side pointing that way.
void AddSegmentCandidates(std::vector<Candidate>& out, const LabelShape& s,
                          const Size2Dd& text, double margin, LabelSide preferred) {
    double dx = s.p2.x - s.p1.x, dy = s.p2.y - s.p1.y;
    double len = std::hypot(dx, dy);
    if (len < 1e-9) { dx = 1.0; dy = 0.0; len = 1.0; }
    Point2Dd n(-dy / len, dx / len);

    auto classify = [](const Point2Dd& v) {
        if (std::abs(v.y) >= std::abs(v.x)) return (v.y < 0.0) ? LabelSide::Top : LabelSide::Bottom;
        return (v.x < 0.0) ? LabelSide::Left : LabelSide::Right;
    };
    LabelSide posSide = classify(n);
    LabelSide negSide = classify(Point2Dd(-n.x, -n.y));

    double support = std::abs(n.x) * text.width * 0.5 + std::abs(n.y) * text.height * 0.5;
    double offset = margin + std::max(s.thickness, 2.0) * 0.5 + support;

    double onLineBase, posBase, negBase;
    if (preferred == LabelSide::Auto || preferred == LabelSide::Inside) {
        onLineBase = 0.0; posBase = 6.0; negBase = 6.5;
    } else {
        onLineBase = 8.0;
        posBase = (posSide == preferred) ? 0.0 : 8.0;
        negBase = (negSide == preferred) ? 0.0 : 8.0;
    }

    static const double fractions[] = {0.5, 0.38, 0.62, 0.25, 0.75};
    for (double t : fractions) {
        double fCost = std::abs(t - 0.5) * 8.0;
        Point2Dd c(s.p1.x + (s.p2.x - s.p1.x) * t, s.p1.y + (s.p2.y - s.p1.y) * t);
        out.push_back({RectAround(c, text), LabelSide::Inside, onLineBase + fCost});
        out.push_back({RectAround(Point2Dd(c.x + n.x * offset, c.y + n.y * offset), text),
                       posSide, posBase + fCost});
        out.push_back({RectAround(Point2Dd(c.x - n.x * offset, c.y - n.y * offset), text),
                       negSide, negBase + fCost});
    }
}

// Candidates just outside the shape along a fixed angle (gauge/pie style),
// with small angular jitters and increasing radial offsets as fallbacks.
void AddRadialCandidates(std::vector<Candidate>& out, const LabelShape& s,
                         const Size2Dd& text, double margin, double angleDeg) {
    static const double jitters[] = {0.0, 8.0, -8.0, 16.0, -16.0};
    static const double extras[] = {0.0, 0.4, 0.8};
    double extraUnit = std::min(text.width, text.height);

    for (double j : jitters) {
        double a = (angleDeg + j) * M_PI / 180.0;
        Point2Dd d(std::cos(a), std::sin(a));
        // Distance from the shape centre to its boundary along d (support
        // function; slightly generous for rectangle diagonals).
        double base;
        switch (s.type) {
            case LabelShapeType::Circle:    base = s.radius; break;
            case LabelShapeType::Rectangle: base = std::abs(d.x) * s.size.width * 0.5 +
                                                   std::abs(d.y) * s.size.height * 0.5; break;
            default:                        base = std::max(s.thickness, 2.0) * 0.5; break;
        }
        double support = std::abs(d.x) * text.width * 0.5 + std::abs(d.y) * text.height * 0.5;
        for (size_t e = 0; e < 3; ++e) {
            double dist = base + margin + support + extras[e] * extraUnit;
            out.push_back({RectAround(Point2Dd(s.center.x + d.x * dist, s.center.y + d.y * dist), text),
                           LabelSide::Radial, std::abs(j) / 8.0 + e * 2.0});
        }
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

// Slide rect along its free axis in one direction until it clears every
// already placed label by labelMargin. Returns the shifted rect and the total
// distance moved, or a negative distance when no clear position was found.
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

// Axis along which an overlapping label may slide: along the side it sits on
// (horizontal for top/bottom, vertical for left/right); radial labels slide
// tangentially, segment pills along the segment's dominant axis.
bool SlideAxisHorizontal(const Candidate& c, const LabelShape& own) {
    switch (c.side) {
        case LabelSide::Left:
        case LabelSide::Right:
            return false;
        case LabelSide::Radial: {
            double dx = c.rect.Center().x - own.center.x;
            double dy = c.rect.Center().y - own.center.y;
            return std::abs(dy) >= std::abs(dx);
        }
        case LabelSide::Inside:
            if (own.type == LabelShapeType::Segment) {
                return std::abs(own.p2.x - own.p1.x) >= std::abs(own.p2.y - own.p1.y);
            }
            return true;
        default:
            return true;
    }
}

Point2Dd ClosestBoundaryPoint(const LabelShape& s, const Point2Dd& from) {
    switch (s.type) {
        case LabelShapeType::Circle: {
            double dx = from.x - s.center.x, dy = from.y - s.center.y;
            double len = std::hypot(dx, dy);
            if (len < 1e-9) return Point2Dd(s.center.x + s.radius, s.center.y);
            return Point2Dd(s.center.x + dx / len * s.radius, s.center.y + dy / len * s.radius);
        }
        case LabelShapeType::Segment:
            return ClosestPointOnSegment(s.p1, s.p2, from);
        default: {
            Rect2Dd bb = s.BoundingRect();
            Point2Dd q = ClampPointToRect(from, bb);
            if (q.x != from.x || q.y != from.y) return q;   // from is outside
            // from is inside the rect: project to the nearest edge.
            double dLeft = from.x - bb.Left(), dRight = bb.Right() - from.x;
            double dTop = from.y - bb.Top(), dBottom = bb.Bottom() - from.y;
            double m = std::min(std::min(dLeft, dRight), std::min(dTop, dBottom));
            if (m == dLeft)  return Point2Dd(bb.Left(), from.y);
            if (m == dRight) return Point2Dd(bb.Right(), from.y);
            if (m == dTop)   return Point2Dd(from.x, bb.Top());
            return Point2Dd(from.x, bb.Bottom());
        }
    }
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

    // Place high priority labels first so they get the best positions and,
    // with allowHide, low priority labels are the ones dropped.
    std::vector<size_t> order(labels.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [&labels](size_t a, size_t b) {
        return labels[a].priority > labels[b].priority;
    });

    std::vector<Rect2Dd> placed;
    placed.reserve(labels.size());

    for (size_t idx : order) {
        const ShapeLabel& label = labels[idx];
        PlacedShapeLabel& result = results[idx];
        Size2Dd text = EffectiveSize(label);
        if (label.shapeIndex >= shapes.size()) {
            result.bounds = Rect2Dd(options.bounds.x, options.bounds.y,
                                    text.width, text.height);
            result.fitted = false;
            continue;
        }

        const LabelShape& s = shapes[label.shapeIndex];
        double dx = s.center.x - centroid.x;
        double dy = s.center.y - centroid.y;

        std::vector<Candidate> candidates;
        if (label.preferredSide == LabelSide::Radial) {
            AddRadialCandidates(candidates, s, text, options.shapeMargin, label.radialAngleDeg);
        } else if (s.type == LabelShapeType::Segment) {
            AddSegmentCandidates(candidates, s, text, options.shapeMargin, label.preferredSide);
        } else if (s.keepLabelInside || label.preferredSide == LabelSide::Inside) {
            AddInsideCandidates(candidates, s, text, options.shapeMargin);
        } else {
            // Side order: the preferred side first if given; otherwise the
            // vertical side facing away from the diagram centre (wide, short
            // labels read best above/below), then the outward horizontal side,
            // then the two remaining sides as fallbacks.
            LabelSide vertical = (dy <= 0.0) ? LabelSide::Top : LabelSide::Bottom;
            LabelSide horizontal = (dx <= 0.0) ? LabelSide::Left : LabelSide::Right;
            std::vector<LabelSide> sideOrder;
            if (label.preferredSide != LabelSide::Auto) sideOrder.push_back(label.preferredSide);
            for (LabelSide side : {vertical, horizontal, OppositeSide(vertical), OppositeSide(horizontal)}) {
                if (std::find(sideOrder.begin(), sideOrder.end(), side) == sideOrder.end()) {
                    sideOrder.push_back(side);
                }
            }
            double sideCost = 0.0;
            for (LabelSide side : sideOrder) {
                double bias = (side == LabelSide::Top || side == LabelSide::Bottom) ? dx : dy;
                AddOutsideCandidates(candidates, s, text, side,
                                     options.shapeMargin, sideCost, bias);
                sideCost += 8.0;
            }
        }

        const Candidate* best = nullptr;
        double bestScore = 0.0;
        for (const Candidate& c : candidates) {
            double score = Score(c, label.shapeIndex, shapes, placed, options);
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
        bool slideHorizontal = SlideAxisHorizontal(*best, s);
        auto posDir = SlideClear(rect, slideHorizontal, 1.0, placed, options);
        auto negDir = SlideClear(rect, slideHorizontal, -1.0, placed, options);
        bool posOk = posDir.second >= 0.0 && InsideBounds(posDir.first, options);
        bool negOk = negDir.second >= 0.0 && InsideBounds(negDir.first, options);
        if (posOk && (!negOk || posDir.second <= negDir.second)) {
            rect = posDir.first;
        } else if (negOk) {
            rect = negDir.first;
        } else if (options.allowHide) {
            // Reaching here means an unresolvable conflict; drop the label
            // instead of force-placing it.
            result.hidden = true;
            result.fitted = false;
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
        result.anchorPoint = ClosestBoundaryPoint(s, rect.Center());
        if (!result.hidden) {
            if (options.leaderThreshold > 0.0) {
                Point2Dd nearest = ClampPointToRect(result.anchorPoint, rect);
                double gap = std::hypot(nearest.x - result.anchorPoint.x,
                                        nearest.y - result.anchorPoint.y);
                result.needsLeader = gap > options.leaderThreshold;
            }
            placed.push_back(rect);
        }
    }

    return results;
}

} // namespace UltraCanvas

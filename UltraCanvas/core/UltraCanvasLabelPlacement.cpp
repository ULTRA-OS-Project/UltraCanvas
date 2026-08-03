// core/UltraCanvasLabelPlacement.cpp
// Shared shape-label placement solver for diagrams and charts
// Version: 2.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework

#include "UltraCanvasLabelPlacement.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <unordered_map>

namespace UltraCanvas {

namespace {

// =============================================================================
// ORIENTED RECTANGLES
// =============================================================================
// Every claimed area - an obstacle, a shape's bounding box, an already placed
// label - is stored as a rectangle plus a rotation about its own centre. An
// unrotated one takes the plain axis-aligned path everywhere, so upright labels
// behave exactly as they did before rotation existed.

struct OrientedRect {
    Point2Dd center;
    Size2Dd  size;
    double   rotationDegrees = 0.0;
    Rect2Dd  aabb;                 // conservative bounds, kept in step with the above
    uint32_t payload = 0;          // shape index for the shape grid, unused for labels
};

Size2Dd RotatedExtents(const Size2Dd& s, double degrees) {
    if (degrees == 0.0) return s;
    const double r = degrees * (M_PI / 180.0);
    const double c = std::abs(std::cos(r));
    const double n = std::abs(std::sin(r));
    return Size2Dd(s.width * c + s.height * n, s.width * n + s.height * c);
}

void CornersOf(const OrientedRect& o, Point2Dd out[4]) {
    const double r = o.rotationDegrees * (M_PI / 180.0);
    const double c = std::cos(r), s = std::sin(r);
    const double hw = o.size.width * 0.5, hh = o.size.height * 0.5;
    static const double dx[4] = {-1.0,  1.0, 1.0, -1.0};
    static const double dy[4] = {-1.0, -1.0, 1.0,  1.0};
    for (int i = 0; i < 4; ++i) {
        const double lx = dx[i] * hw, ly = dy[i] * hh;
        out[i].x = o.center.x + lx * c - ly * s;
        out[i].y = o.center.y + lx * s + ly * c;
    }
}

Rect2Dd AabbOf(const OrientedRect& o) {
    if (o.rotationDegrees == 0.0) {
        return Rect2Dd(o.center.x - o.size.width * 0.5, o.center.y - o.size.height * 0.5,
                       o.size.width, o.size.height);
    }
    Point2Dd c[4];
    CornersOf(o, c);
    double lo_x = c[0].x, hi_x = c[0].x, lo_y = c[0].y, hi_y = c[0].y;
    for (int i = 1; i < 4; ++i) {
        lo_x = std::min(lo_x, c[i].x); hi_x = std::max(hi_x, c[i].x);
        lo_y = std::min(lo_y, c[i].y); hi_y = std::max(hi_y, c[i].y);
    }
    return Rect2Dd(lo_x, lo_y, hi_x - lo_x, hi_y - lo_y);
}

OrientedRect MakeOriented(const Rect2Dd& r, double degrees = 0.0, uint32_t payload = 0) {
    OrientedRect o;
    o.center = Point2Dd(r.x + r.width * 0.5, r.y + r.height * 0.5);
    o.size = Size2Dd(r.width, r.height);
    o.rotationDegrees = degrees;
    o.payload = payload;
    o.aabb = AabbOf(o);
    return o;
}

// Grow by `m` on every side, in the rectangle's own frame.
OrientedRect Expand(const OrientedRect& o, double m) {
    OrientedRect e = o;
    e.size = Size2Dd(o.size.width + m * 2.0, o.size.height + m * 2.0);
    e.aabb = AabbOf(e);
    return e;
}

Rect2Dd Expand(const Rect2Dd& r, double m) {
    return Rect2Dd(r.x - m, r.y - m, r.width + m * 2.0, r.height + m * 2.0);
}

double OverlapArea(const Rect2Dd& a, const Rect2Dd& b) {
    const double w = std::min(a.Right(), b.Right()) - std::max(a.Left(), b.Left());
    const double h = std::min(a.Bottom(), b.Bottom()) - std::max(a.Top(), b.Top());
    return (w > 0.0 && h > 0.0) ? w * h : 0.0;
}

double PolygonArea(const Point2Dd* p, int n) {
    double a = 0.0;
    for (int i = 0; i < n; ++i) {
        const Point2Dd& u = p[i];
        const Point2Dd& v = p[(i + 1) % n];
        a += u.x * v.y - v.x * u.y;
    }
    return std::abs(a) * 0.5;
}

// Intersection polygon of two convex polygons (Sutherland-Hodgman). Used only
// for the rotated cases; upright pairs never reach it.
void ConvexIntersect(const Point2Dd* subject, int ns, const Point2Dd* clip, int nc,
                     std::vector<Point2Dd>& result) {
    std::vector<Point2Dd> poly(subject, subject + ns);
    std::vector<Point2Dd> next;

    // Normalise the clip winding so the half-plane test has a fixed sign.
    std::vector<Point2Dd> cw(clip, clip + nc);
    double signedArea = 0.0;
    for (int i = 0; i < nc; ++i) {
        const Point2Dd& u = cw[i];
        const Point2Dd& v = cw[(i + 1) % nc];
        signedArea += u.x * v.y - v.x * u.y;
    }
    if (signedArea < 0.0) std::reverse(cw.begin(), cw.end());

    for (int i = 0; i < nc && !poly.empty(); ++i) {
        const Point2Dd& e0 = cw[i];
        const Point2Dd& e1 = cw[(i + 1) % nc];
        const double ex = e1.x - e0.x, ey = e1.y - e0.y;

        next.clear();
        for (size_t j = 0; j < poly.size(); ++j) {
            const Point2Dd& cur = poly[j];
            const Point2Dd& nxt = poly[(j + 1) % poly.size()];
            const double sc = ex * (cur.y - e0.y) - ey * (cur.x - e0.x);
            const double sn = ex * (nxt.y - e0.y) - ey * (nxt.x - e0.x);
            const bool inCur = sc >= 0.0, inNext = sn >= 0.0;
            if (inCur) next.push_back(cur);
            if (inCur != inNext) {
                const double t = sc / (sc - sn);
                next.push_back(Point2Dd(cur.x + t * (nxt.x - cur.x),
                                        cur.y + t * (nxt.y - cur.y)));
            }
        }
        poly.swap(next);
    }
    result.swap(poly);
    if (result.size() < 3) result.clear();
}

// Intersection region of two oriented rectangles, as an axis-aligned box.
// Empty (zero width and height) when they do not touch.
Rect2Dd OverlapRegion(const OrientedRect& a, const OrientedRect& b) {
    if (OverlapArea(a.aabb, b.aabb) <= 0.0) return Rect2Dd(0, 0, 0, 0);
    if (a.rotationDegrees == 0.0 && b.rotationDegrees == 0.0) {
        const double x0 = std::max(a.aabb.Left(), b.aabb.Left());
        const double y0 = std::max(a.aabb.Top(), b.aabb.Top());
        const double x1 = std::min(a.aabb.Right(), b.aabb.Right());
        const double y1 = std::min(a.aabb.Bottom(), b.aabb.Bottom());
        return Rect2Dd(x0, y0, std::max(0.0, x1 - x0), std::max(0.0, y1 - y0));
    }
    Point2Dd ca[4], cb[4];
    CornersOf(a, ca);
    CornersOf(b, cb);
    std::vector<Point2Dd> poly;
    ConvexIntersect(ca, 4, cb, 4, poly);
    if (poly.size() < 3) return Rect2Dd(0, 0, 0, 0);
    double lo_x = poly[0].x, hi_x = poly[0].x, lo_y = poly[0].y, hi_y = poly[0].y;
    for (const Point2Dd& p : poly) {
        lo_x = std::min(lo_x, p.x); hi_x = std::max(hi_x, p.x);
        lo_y = std::min(lo_y, p.y); hi_y = std::max(hi_y, p.y);
    }
    return Rect2Dd(lo_x, lo_y, hi_x - lo_x, hi_y - lo_y);
}

double OverlapArea(const OrientedRect& a, const OrientedRect& b) {
    if (OverlapArea(a.aabb, b.aabb) <= 0.0) return 0.0;          // cheap reject
    if (a.rotationDegrees == 0.0 && b.rotationDegrees == 0.0) {
        return OverlapArea(a.aabb, b.aabb);                       // exact, no clipping
    }
    Point2Dd ca[4], cb[4];
    CornersOf(a, ca);
    CornersOf(b, cb);
    std::vector<Point2Dd> poly;
    ConvexIntersect(ca, 4, cb, 4, poly);
    if (poly.size() < 3) return 0.0;
    return PolygonArea(poly.data(), static_cast<int>(poly.size()));
}

// =============================================================================
// UNIFORM GRID INDEX
// =============================================================================
// Scoring a candidate used to walk every shape and every already-placed label,
// which made the solver quadratic in the label count and kept it out of the
// dense charts. Claimed areas go into a uniform grid instead and a candidate
// only ever tests what shares a cell with it.

class SpatialGrid {
public:
    void Clear() {
        items.clear();
        buckets.clear();
        oversized.clear();
        visited.clear();
        epoch = 0;
    }

    void Insert(const OrientedRect& r) {
        const uint32_t id = static_cast<uint32_t>(items.size());
        items.push_back(r);
        visited.push_back(0);

        int x0, y0, x1, y1;
        CellRange(r.aabb, x0, y0, x1, y1);
        const int64_t cells = static_cast<int64_t>(x1 - x0 + 1) * (y1 - y0 + 1);
        if (cells > kMaxCellsPerItem) {
            oversized.push_back(id);        // legend boxes and the like
            return;
        }
        for (int cy = y0; cy <= y1; ++cy) {
            for (int cx = x0; cx <= x1; ++cx) {
                buckets[Key(cx, cy)].push_back(id);
            }
        }
    }

    // Calls fn(const OrientedRect&) once per item whose cells the box touches.
    template <typename F>
    void ForEachNear(const Rect2Dd& box, F&& fn) const {
        ++epoch;
        for (uint32_t id : oversized) {
            visited[id] = epoch;
            fn(items[id]);
        }
        int x0, y0, x1, y1;
        CellRange(box, x0, y0, x1, y1);
        const int64_t cells = static_cast<int64_t>(x1 - x0 + 1) * (y1 - y0 + 1);
        if (cells > kMaxCellsPerItem) {          // absurd query: fall back to all
            for (uint32_t id = 0; id < items.size(); ++id) {
                if (visited[id] == epoch) continue;
                visited[id] = epoch;
                fn(items[id]);
            }
            return;
        }
        for (int cy = y0; cy <= y1; ++cy) {
            for (int cx = x0; cx <= x1; ++cx) {
                auto it = buckets.find(Key(cx, cy));
                if (it == buckets.end()) continue;
                for (uint32_t id : it->second) {
                    if (visited[id] == epoch) continue;
                    visited[id] = epoch;
                    fn(items[id]);
                }
            }
        }
    }

    bool Empty() const { return items.empty(); }

private:
    static constexpr double kCell = 64.0;
    static constexpr int64_t kMaxCellsPerItem = 256;

    static uint64_t Key(int cx, int cy) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
                static_cast<uint64_t>(static_cast<uint32_t>(cy));
    }

    static void CellRange(const Rect2Dd& r, int& x0, int& y0, int& x1, int& y1) {
        x0 = static_cast<int>(std::floor(r.Left()   / kCell));
        y0 = static_cast<int>(std::floor(r.Top()    / kCell));
        x1 = static_cast<int>(std::floor(r.Right()  / kCell));
        y1 = static_cast<int>(std::floor(r.Bottom() / kCell));
    }

    std::vector<OrientedRect> items;
    std::unordered_map<uint64_t, std::vector<uint32_t>> buckets;
    std::vector<uint32_t> oversized;
    mutable std::vector<uint32_t> visited;      // per-query dedup stamps
    mutable uint32_t epoch = 0;
};

// =============================================================================
// CANDIDATE GENERATION (unchanged geometry, now rotation-aware)
// =============================================================================

// A concrete position being considered for one label. baseCost encodes the
// static preference (side order, slide distance); the dynamic score adds
// overlap and out-of-bounds penalties on top.
struct Candidate {
    Rect2Dd rect;              // axis-aligned footprint of the (possibly rotated) label
    LabelSide side;
    double baseCost;
};

// Approximate area of shape n rect. Exact for rectangles and for rects fully
// inside / fully outside a circle; partial circle overlaps use the bounding
// box intersection scaled by the circle/square area ratio, which is accurate
// enough for scoring.
double ShapeOverlapArea(const LabelShape& s, const Rect2Dd& rect) {
    double area = OverlapArea(s.BoundingRect(), rect);
    if (area <= 0.0) return 0.0;
    if (s.type == LabelShapeType::Circle) {
        const double r2 = s.radius * s.radius;
        // Closest point of the rect to the circle centre: outside the radius
        // means the rect only clips the empty corners of the bounding box.
        const double cx = std::clamp(s.center.x, rect.Left(), rect.Right());
        const double cy = std::clamp(s.center.y, rect.Top(), rect.Bottom());
        const double dx = cx - s.center.x, dy = cy - s.center.y;
        if (dx * dx + dy * dy >= r2) return 0.0;
        // Farthest corner inside the radius means the rect is fully contained.
        const double fx = std::max(std::abs(rect.Left() - s.center.x), std::abs(rect.Right() - s.center.x));
        const double fy = std::max(std::abs(rect.Top() - s.center.y), std::abs(rect.Bottom() - s.center.y));
        if (fx * fx + fy * fy <= r2) return rect.width * rect.height;
        area *= M_PI / 4.0;
    }
    return area;
}

// Candidates outside the shape on one side. Besides the centred position the
// label may slide along that side so neighbouring labels can sit next to each
// other; positions are emitted preferring small slides, biased away from the
// diagram centre first.
void AddOutsideCandidates(std::vector<Candidate>& out, const LabelShape& s,
                          const Size2Dd& text, LabelSide side,
                          double margin, double sideCost, double slideBias) {
    const Rect2Dd bb = s.BoundingRect();
    const double halfW = bb.width * 0.5;
    const double halfH = bb.height * 0.5;
    const bool horizontal = (side == LabelSide::Top || side == LabelSide::Bottom);
    const double slideMax = horizontal ? halfW : halfH;

    static const double fractions[] = {0.0, 0.35, 0.7, 1.0};
    const double sign = (slideBias < 0.0) ? -1.0 : 1.0;

    for (double f : fractions) {
        for (int flip = 0; flip < (f == 0.0 ? 1 : 2); ++flip) {
            const double offset = f * slideMax * (flip == 0 ? sign : -sign);
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
    const int col = static_cast<int>(anchor) % 3 - 1;   // -1 left, 0 centre, 1 right
    const int row = static_cast<int>(anchor) / 3 - 1;   // -1 top,  0 centre, 1 bottom
    if (s.type == LabelShapeType::Rectangle) {
        const Rect2Dd bb = s.BoundingRect();
        const double xIn = margin * 2.0;
        const double yIn = margin;
        const double x = (col < 0) ? bb.Left() + xIn
                       : (col > 0) ? bb.Right() - xIn - text.width
                                   : s.center.x - text.width * 0.5;
        const double y = (row < 0) ? bb.Top() + yIn
                       : (row > 0) ? bb.Bottom() - yIn - text.height
                                   : s.center.y - text.height * 0.5;
        return Rect2Dd(x, y, text.width, text.height);
    }
    const double f = (col != 0 && row != 0) ? 0.5 * M_SQRT1_2 : 0.5;
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
        const double rad = deg * (M_PI / 180.0);
        const double dx = std::sin(rad);
        const double dy = -std::cos(rad);
        Point2Dd p = s.center;
        if (s.type == LabelShapeType::Circle) {
            p.x += dx * s.radius;
            p.y += dy * s.radius;
        } else {
            // Ray-to-border intersection of the rectangle.
            const double tx = (std::abs(dx) > 1e-9) ? (s.size.width * 0.5) / std::abs(dx) : 1e18;
            const double ty = (std::abs(dy) > 1e-9) ? (s.size.height * 0.5) / std::abs(dy) : 1e18;
            const double t = std::min(tx, ty);
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

// Closest point on a shape's outline to `towards`; the anchor a leader line
// starts from.
Point2Dd ClosestPointOnShape(const LabelShape& s, const Point2Dd& towards) {
    if (s.type == LabelShapeType::Circle) {
        const double dx = towards.x - s.center.x, dy = towards.y - s.center.y;
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d < 1e-9 || s.radius <= 0.0) return s.center;
        return Point2Dd(s.center.x + dx / d * s.radius, s.center.y + dy / d * s.radius);
    }
    const Rect2Dd bb = s.BoundingRect();
    return Point2Dd(std::clamp(towards.x, bb.Left(), bb.Right()),
                    std::clamp(towards.y, bb.Top(), bb.Bottom()));
}

} // namespace

// =============================================================================
// SESSION
// =============================================================================

struct LabelPlacementSession::Impl {
    LabelPlacementOptions options;
    std::vector<LabelShape> shapes;
    SpatialGrid shapeGrid;
    SpatialGrid claimedGrid;         // obstacles + placed labels
    std::vector<Rect2Dd> claimedRects;
    Point2Dd centroidSum{0.0, 0.0};

    double Separation() const {
        return std::max(options.labelMargin, options.minLabelSeparation);
    }

    Point2Dd Centroid() const {
        if (!shapes.empty()) {
            return Point2Dd(centroidSum.x / static_cast<double>(shapes.size()),
                            centroidSum.y / static_cast<double>(shapes.size()));
        }
        if (options.bounds.width > 0.0 && options.bounds.height > 0.0) {
            return Point2Dd(options.bounds.x + options.bounds.width * 0.5,
                            options.bounds.y + options.bounds.height * 0.5);
        }
        return Point2Dd(0.0, 0.0);
    }

    void Claim(const OrientedRect& r) {
        claimedGrid.Insert(r);
        claimedRects.push_back(r.aabb);
    }

    // Seeds the claimed world with the caller's static keep-out rectangles.
    void AddObstacles() {
        for (const Rect2Dd& r : options.obstacles) Claim(MakeOriented(r));
    }

    // Total penalty for putting a label at `cand`.
    double Score(const OrientedRect& quad, const Rect2Dd& rect, LabelSide side,
                 double baseCost, const LabelShape& own, size_t ownShapeIndex,
                 int containerIndex, bool tolerateOverflow) const {
        const double labelArea = std::max(1.0, rect.width * rect.height);
        double cost = baseCost;

        if (side == LabelSide::Border || (side == LabelSide::Inside && tolerateOverflow)) {
            // Straddling the own shape's edge is the intent - no own-shape penalty.
        } else if (side == LabelSide::Inside) {
            // Penalise the part of the label sticking out of its own shape.
            cost += 200.0 * (labelArea - ShapeOverlapArea(own, rect)) / labelArea;
        } else {
            cost += 400.0 * ShapeOverlapArea(own, rect) / labelArea;
        }

        if (options.avoidOtherShapes && !shapeGrid.Empty()) {
            shapeGrid.ForEachNear(rect, [&](const OrientedRect& entry) {
                const size_t j = entry.payload;
                if (j == ownShapeIndex || shapes[j].isContainer) return;
                cost += 60.0 * ShapeOverlapArea(shapes[j], rect) / labelArea;
            });
        }

        if (options.bounds.width > 0.0 && options.bounds.height > 0.0) {
            cost += 300.0 * (labelArea - OverlapArea(options.bounds, rect)) / labelArea;
        }

        if (containerIndex >= 0 && static_cast<size_t>(containerIndex) < shapes.size()) {
            // Keep the label within its container shape (e.g. the parent circle
            // of a hierarchical packing), like a second, shaped bounds.
            cost += 300.0 * (labelArea - ShapeOverlapArea(shapes[containerIndex], rect))
                    / labelArea;
        }

        const double sep = Separation();
        claimedGrid.ForEachNear(Expand(quad.aabb, sep), [&](const OrientedRect& claimed) {
            const double o = OverlapArea(Expand(claimed, sep), quad);
            if (o > 0.0) cost += 150.0 + 500.0 * o / labelArea;
        });
        return cost;
    }

    // Overlap of a placed quad with everything already claimed.
    double ClaimedOverlap(const OrientedRect& quad) const {
        const double sep = Separation();
        double total = 0.0;
        claimedGrid.ForEachNear(Expand(quad.aabb, sep), [&](const OrientedRect& claimed) {
            total += OverlapArea(Expand(claimed, sep), quad);
        });
        return total;
    }

    // Slide a label along its free axis (x for top/bottom/inside labels, y for
    // left/right) in one direction until it clears everything claimed. Returns
    // the shifted rect and the distance moved, or a negative distance when no
    // clear position was found.
    std::pair<Rect2Dd, double> SlideClear(Rect2Dd r, double rotation, bool horizontal,
                                          double sign) const {
        const double sep = Separation();
        double total = 0.0;
        for (int iter = 0; iter < 16; ++iter) {
            OrientedRect quad = MakeOriented(r, rotation);
            double shift = 0.0;
            claimedGrid.ForEachNear(Expand(quad.aabb, sep), [&](const OrientedRect& claimed) {
                const OrientedRect blocked = Expand(claimed, sep);
                // Step past the blocker using the region actually in the way.
                // Its bounding box would do for an upright blocker but is
                // useless for a rotated one - the bounding box of a long
                // diagonal band is most of the plot, and stepping past *that*
                // throws the label off the chart.
                const Rect2Dd hit = (blocked.rotationDegrees == 0.0 && rotation == 0.0)
                    ? blocked.aabb
                    : OverlapRegion(blocked, quad);
                if (hit.width <= 0.0 && hit.height <= 0.0) return;
                const double delta = horizontal
                    ? ((sign > 0.0) ? hit.Right() - quad.aabb.Left() : quad.aabb.Right() - hit.Left())
                    : ((sign > 0.0) ? hit.Bottom() - quad.aabb.Top() : quad.aabb.Bottom() - hit.Top());
                if (delta <= 0.0) return;
                shift = std::max(shift, delta + 0.5);
            });
            if (shift <= 0.0) return {r, total};
            if (horizontal) r.x += sign * shift; else r.y += sign * shift;
            total += shift;
        }
        return {r, -1.0};
    }
};

LabelPlacementSession::LabelPlacementSession(const LabelPlacementOptions& options)
    : impl(std::make_unique<Impl>()) {
    impl->options = options;
    impl->AddObstacles();
}

// Defined out of line so the Impl definition above is visible.
LabelPlacementSession::~LabelPlacementSession() = default;

size_t LabelPlacementSession::AddShape(const LabelShape& shape) {
    const size_t index = impl->shapes.size();
    impl->shapes.push_back(shape);
    impl->centroidSum.x += shape.center.x;
    impl->centroidSum.y += shape.center.y;
    impl->shapeGrid.Insert(MakeOriented(shape.BoundingRect(), 0.0,
                                        static_cast<uint32_t>(index)));
    return index;
}

size_t LabelPlacementSession::AddShapes(const std::vector<LabelShape>& shapes) {
    const size_t base = impl->shapes.size();
    for (const LabelShape& s : shapes) AddShape(s);
    return base;
}

void LabelPlacementSession::AddObstacleRect(const Rect2Dd& rect) {
    impl->Claim(MakeOriented(rect));
}

void LabelPlacementSession::AddObstacleRects(const std::vector<Rect2Dd>& rects) {
    for (const Rect2Dd& r : rects) AddObstacleRect(r);
}

void LabelPlacementSession::AddObstaclePolyline(const std::vector<Point2Dd>& points,
                                                double halfWidth) {
    if (points.size() < 2) {
        if (points.size() == 1) {
            AddObstacleRect(Rect2Dd(points[0].x - halfWidth, points[0].y - halfWidth,
                                    halfWidth * 2.0, halfWidth * 2.0));
        }
        return;
    }
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const Point2Dd& a = points[i];
        const Point2Dd& b = points[i + 1];
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-9) continue;
        // One tight quad per segment: a diagonal run costs a thin band rather
        // than the bounding box of the whole line.
        OrientedRect seg;
        seg.center = Point2Dd((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
        seg.size = Size2Dd(len, halfWidth * 2.0);
        seg.rotationDegrees = std::atan2(dy, dx) * (180.0 / M_PI);
        seg.aabb = AabbOf(seg);
        impl->Claim(seg);
    }
}

const std::vector<Rect2Dd>& LabelPlacementSession::ClaimedRects() const {
    return impl->claimedRects;
}

size_t LabelPlacementSession::ShapeCount() const { return impl->shapes.size(); }

void LabelPlacementSession::Reset() {
    impl->shapes.clear();
    impl->shapeGrid.Clear();
    impl->claimedGrid.Clear();
    impl->claimedRects.clear();
    impl->centroidSum = Point2Dd(0.0, 0.0);
    impl->AddObstacles();
}

std::vector<PlacedShapeLabel> LabelPlacementSession::Place(
        const std::vector<ShapeLabel>& labels) {

    std::vector<PlacedShapeLabel> results(labels.size());
    if (labels.empty()) return results;

    const LabelPlacementOptions& options = impl->options;
    const Point2Dd centroid = impl->Centroid();

    // Most important first, input order as the tie-break: all-equal priorities
    // reproduce the historical input-order behaviour exactly.
    std::vector<size_t> order(labels.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return labels[a].priority > labels[b].priority;
    });

    for (size_t idx : order) {
        const ShapeLabel& label = labels[idx];
        PlacedShapeLabel& result = results[idx];
        result.rotationDegrees = label.rotationDegrees;

        // Resolve the shape this label hangs off: a real one, or a synthetic
        // zero-radius point for anchor-based labels.
        LabelShape pointShape;
        const LabelShape* own = nullptr;
        size_t ownIndex = static_cast<size_t>(-1);
        if (label.usePointAnchor) {
            pointShape.type = LabelShapeType::Circle;
            pointShape.center = label.anchorPoint;
            pointShape.radius = 0.0;
            own = &pointShape;
        } else if (label.shapeIndex < impl->shapes.size()) {
            ownIndex = label.shapeIndex;
            own = &impl->shapes[ownIndex];
        } else {
            result.bounds = Rect2Dd(options.bounds.x, options.bounds.y,
                                    label.textSize.width, label.textSize.height);
            result.fitted = false;
            continue;
        }

        // A rotated label reserves its rotated footprint; collisions still use
        // the exact rotated quad, so tilted and upright labels pack together.
        const Size2Dd footprint = RotatedExtents(label.textSize, label.rotationDegrees);
        const double dx = own->center.x - centroid.x;
        const double dy = own->center.y - centroid.y;

        std::vector<Candidate> candidates;
        const bool wantInside = own->keepLabelInside || label.preferredSide == LabelSide::Inside;

        // The four outside sides, most promising first: the preferred side if
        // one was given, otherwise the vertical side facing away from the
        // diagram centre (wide, short labels read best above/below), then the
        // outward horizontal side, then the two remaining sides.
        auto addOutsideSides = [&](double sideCost) {
            const LabelSide vertical = (dy <= 0.0) ? LabelSide::Top : LabelSide::Bottom;
            const LabelSide horizontal = (dx <= 0.0) ? LabelSide::Left : LabelSide::Right;
            std::vector<LabelSide> sides;
            if (label.preferredSide != LabelSide::Auto &&
                label.preferredSide != LabelSide::Border &&
                label.preferredSide != LabelSide::Inside) {
                sides.push_back(label.preferredSide);
            }
            for (LabelSide side : {vertical, horizontal, OppositeSide(vertical), OppositeSide(horizontal)}) {
                if (std::find(sides.begin(), sides.end(), side) == sides.end()) {
                    sides.push_back(side);
                }
            }
            for (LabelSide side : sides) {
                const double bias = (side == LabelSide::Top || side == LabelSide::Bottom) ? dx : dy;
                AddOutsideCandidates(candidates, *own, footprint, side,
                                     options.shapeMargin, sideCost, bias);
                sideCost += 8.0;
            }
        };

        if (wantInside) {
            AddInsideCandidates(candidates, *own, footprint, options.shapeMargin,
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
            AddBorderCandidates(candidates, *own, footprint, borderAngles);
            addOutsideSides(borderAngles.empty() ? 0.0 : borderAngles.size() + 6.0);
        }

        const Candidate* best = nullptr;
        double bestScore = 0.0;
        for (const Candidate& c : candidates) {
            const OrientedRect quad = MakeOriented(c.rect, label.rotationDegrees);
            const double score = impl->Score(quad, c.rect, c.side, c.baseCost, *own,
                                             ownIndex, label.containerShape,
                                             label.tolerateShapeOverflow);
            if (!best || score < bestScore) {
                best = &c;
                bestScore = score;
            }
        }
        if (!best) {                      // no candidate at all (empty anchor list)
            result.bounds = Rect2Dd(own->center.x - footprint.width * 0.5,
                                    own->center.y - footprint.height * 0.5,
                                    footprint.width, footprint.height);
            result.fitted = false;
            continue;
        }

        Rect2Dd rect = ClampToBounds(best->rect, options);
        result.side = best->side;
        result.fitted = true;

        // The scoring already steers labels apart; if the winner still touches
        // an earlier label, slide it along its free axis until the labels sit
        // next to each other separated by the separation margin.
        const bool slideHorizontal = (best->side != LabelSide::Left && best->side != LabelSide::Right);
        const auto posDir = impl->SlideClear(rect, label.rotationDegrees, slideHorizontal, 1.0);
        const auto negDir = impl->SlideClear(rect, label.rotationDegrees, slideHorizontal, -1.0);
        const bool posOk = posDir.second >= 0.0 && InsideBounds(posDir.first, options);
        const bool negOk = negDir.second >= 0.0 && InsideBounds(negDir.first, options);
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

        const OrientedRect finalQuad = MakeOriented(rect, label.rotationDegrees);
        // Two ways a label can end up drawn badly, and both count as "did not
        // fit": it overlaps something already claimed, or it had to hang
        // outside the bounds to avoid doing so. Callers that allow suppression
        // want it dropped either way.
        const bool collides = impl->ClaimedOverlap(finalQuad) > 0.0;
        const bool escaped = !InsideBounds(rect, options);
        if (escaped) result.fitted = false;
        const bool suppress = (collides || escaped) &&
            (label.allowSuppress || options.declutter == LabelDeclutterPolicy::SuppressColliding);

        result.bounds = rect;
        if (suppress) {
            // A dropped label claims nothing, so the space stays available to
            // whatever comes next.
            result.suppressed = true;
            result.fitted = false;
            continue;
        }

        if (label.wantLeaderLine) {
            const Point2Dd labelCentre(rect.x + rect.width * 0.5, rect.y + rect.height * 0.5);
            const Point2Dd anchor = ClosestPointOnShape(*own, labelCentre);
            const double gapX = std::max({anchor.x - rect.Right(), rect.Left() - anchor.x, 0.0});
            const double gapY = std::max({anchor.y - rect.Bottom(), rect.Top() - anchor.y, 0.0});
            // Only worth a connector once the label has actually walked away
            // from its shape; adjacent labels read fine without one.
            if (std::sqrt(gapX * gapX + gapY * gapY) > options.shapeMargin * 1.5) {
                result.hasLeader = true;
                result.leaderFrom = anchor;
            }
        }

        impl->Claim(finalQuad);
    }

    return results;
}

// =============================================================================
// ONE-SHOT WRAPPER
// =============================================================================

std::vector<PlacedShapeLabel> PlaceShapeLabels(
    const std::vector<LabelShape>& shapes,
    const std::vector<ShapeLabel>& labels,
    const LabelPlacementOptions& options) {

    std::vector<PlacedShapeLabel> results(labels.size());
    if (shapes.empty()) return results;

    LabelPlacementSession session(options);
    session.AddShapes(shapes);
    return session.Place(labels);
}

// =============================================================================
// AXIS TICK DECLUTTER
// =============================================================================

std::vector<bool> DeclutterAxisTicks(const std::vector<double>& positions,
                                     const std::vector<double>& extents,
                                     double minGap,
                                     TickDeclutterPolicy policy,
                                     const std::vector<int>* priorities) {
    const size_t n = positions.size();
    std::vector<bool> keep(n, false);
    if (n == 0) return keep;
    if (n == 1) { keep[0] = true; return keep; }

    auto extentAt = [&](size_t i) {
        return (i < extents.size()) ? std::max(0.0, extents[i]) : 0.0;
    };

    // Work in axis order regardless of how the caller stored the ticks.
    std::vector<size_t> order(n);
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return positions[a] < positions[b];
    });

    auto fits = [&](size_t a, size_t b) {          // a before b along the axis
        const double gap = std::abs(positions[b] - positions[a])
                         - (extentAt(a) + extentAt(b)) * 0.5;
        return gap >= minGap;
    };

    switch (policy) {
        case TickDeclutterPolicy::KeepEveryNth: {
            // Smallest stride that keeps every retained neighbour pair clear,
            // so the surviving labels stay evenly spaced.
            for (size_t stride = 1; stride <= n; ++stride) {
                bool ok = true;
                for (size_t i = stride; i < n && ok; i += stride) {
                    if (!fits(order[i - stride], order[i])) ok = false;
                }
                if (ok) {
                    for (size_t i = 0; i < n; i += stride) keep[order[i]] = true;
                    return keep;
                }
            }
            keep[order[0]] = true;                 // nothing fits: keep one
            return keep;
        }

        case TickDeclutterPolicy::Greedy: {
            size_t last = order[0];
            keep[last] = true;
            for (size_t i = 1; i < n; ++i) {
                if (fits(last, order[i])) {
                    keep[order[i]] = true;
                    last = order[i];
                }
            }
            return keep;
        }

        case TickDeclutterPolicy::PriorityGreedy: {
            std::vector<size_t> byPriority = order;
            if (priorities) {
                std::stable_sort(byPriority.begin(), byPriority.end(),
                                 [&](size_t a, size_t b) {
                    const int pa = (a < priorities->size()) ? (*priorities)[a] : 0;
                    const int pb = (b < priorities->size()) ? (*priorities)[b] : 0;
                    return pa > pb;
                });
            }
            std::vector<size_t> kept;
            for (size_t candidate : byPriority) {
                bool clear = true;
                for (size_t k : kept) {
                    const size_t a = (positions[k] <= positions[candidate]) ? k : candidate;
                    const size_t b = (a == k) ? candidate : k;
                    if (!fits(a, b)) { clear = false; break; }
                }
                if (clear) {
                    kept.push_back(candidate);
                    keep[candidate] = true;
                }
            }
            return keep;
        }
    }
    return keep;
}

} // namespace UltraCanvas

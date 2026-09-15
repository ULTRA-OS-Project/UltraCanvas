// core/UltraCanvasBezierPath.cpp
// The Bézier editing model: see the header.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework

#include "UltraCanvasBezierPath.h"
#include "DataFormats/UltraCanvasVectorPathOps.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace UltraCanvas {

namespace {
    constexpr double kEps = 1e-9;

    double Dist(const Point2Dd& a, const Point2Dd& b) {
        return std::hypot(a.x - b.x, a.y - b.y);
    }

    Point2Dd Lerp(const Point2Dd& a, const Point2Dd& b, double t) {
        return Point2Dd(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
    }

    Point2Dd CubicAt(const Point2Dd& p0, const Point2Dd& p1, const Point2Dd& p2, const Point2Dd& p3, double t) {
        const double u = 1 - t;
        const double b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
        return Point2Dd(b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x,
                        b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y);
    }

    Point2Dd CubicTangent(const Point2Dd& p0, const Point2Dd& p1, const Point2Dd& p2, const Point2Dd& p3, double t) {
        const double u = 1 - t;
        return Point2Dd(3 * u * u * (p1.x - p0.x) + 6 * u * t * (p2.x - p1.x) + 3 * t * t * (p3.x - p2.x),
                        3 * u * u * (p1.y - p0.y) + 6 * u * t * (p2.y - p1.y) + 3 * t * t * (p3.y - p2.y));
    }

    // How many straight pieces a cubic needs so the chords stay within
    // `tolerance` of the curve (from the control-polygon bound).
    int CubicSteps(const Point2Dd& p0, const Point2Dd& p1, const Point2Dd& p2, const Point2Dd& p3, double tolerance) {
        const double dd = std::max(Dist(p0, p1) + Dist(p1, p2) + Dist(p2, p3), 1e-6);
        const double tol = std::max(tolerance, 1e-3);
        int n = static_cast<int>(std::ceil(std::sqrt(dd / tol * 0.75)));
        return std::clamp(n, 1, 200);
    }

    void UnionPoint(Rect2Dd& r, bool& any, const Point2Dd& p) {
        if (!any) { r = Rect2Dd(p.x, p.y, 0, 0); any = true; return; }
        const double x0 = std::min(r.x, p.x), y0 = std::min(r.y, p.y);
        const double x1 = std::max(r.x + r.width, p.x), y1 = std::max(r.y + r.height, p.y);
        r = Rect2Dd(x0, y0, x1 - x0, y1 - y0);
    }

    // Distance from p to the segment ab, and where along it (0..1).
    double PointSegmentDistance(const Point2Dd& p, const Point2Dd& a, const Point2Dd& b, double& t) {
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double len2 = dx * dx + dy * dy;
        t = len2 > kEps ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        return Dist(p, Point2Dd(a.x + dx * t, a.y + dy * t));
    }

    BezierNodeType InferType(const BezierNode& n) {
        if (!n.handleInActive || !n.handleOutActive) return BezierNodeType::Corner;
        const Point2Dd a(n.handleIn.x - n.anchor.x, n.handleIn.y - n.anchor.y);
        const Point2Dd b(n.handleOut.x - n.anchor.x, n.handleOut.y - n.anchor.y);
        const double la = std::hypot(a.x, a.y), lb = std::hypot(b.x, b.y);
        if (la < kEps || lb < kEps) return BezierNodeType::Corner;
        const double cosAngle = (a.x * b.x + a.y * b.y) / (la * lb);
        if (cosAngle > -0.9995) return BezierNodeType::Corner;      // not opposite
        return std::fabs(la - lb) < 1e-3 * std::max(la, lb) + 1e-6 ? BezierNodeType::Symmetric
                                                                    : BezierNodeType::Smooth;
    }
}

// ===========================================================================
// SUBPATH
// ===========================================================================

int UltraCanvasBezierSubpath::SegmentCount() const {
    const int n = static_cast<int>(nodes.size());
    if (n < 2) return 0;
    return closed ? n : n - 1;
}

bool UltraCanvasBezierSubpath::SegmentIsLine(int segment) const {
    if (segment < 0 || segment >= SegmentCount()) return true;
    const BezierNode& a = nodes[segment];
    const BezierNode& b = nodes[(segment + 1) % nodes.size()];
    return !a.handleOutActive && !b.handleInActive;
}

void UltraCanvasBezierSubpath::SegmentControls(int segment, Point2Dd& p0, Point2Dd& p1, Point2Dd& p2, Point2Dd& p3) const {
    const BezierNode& a = nodes[segment];
    const BezierNode& b = nodes[(segment + 1) % nodes.size()];
    p0 = a.anchor;
    p3 = b.anchor;
    p1 = a.handleOutActive ? a.handleOut : a.anchor;
    p2 = b.handleInActive ? b.handleIn : b.anchor;
}

Point2Dd UltraCanvasBezierSubpath::EvaluateSegment(int segment, double t) const {
    if (segment < 0 || segment >= SegmentCount()) return nodes.empty() ? Point2Dd() : nodes.front().anchor;
    Point2Dd p0, p1, p2, p3;
    SegmentControls(segment, p0, p1, p2, p3);
    return CubicAt(p0, p1, p2, p3, t);
}

Point2Dd UltraCanvasBezierSubpath::TangentAt(int segment, double t) const {
    if (segment < 0 || segment >= SegmentCount()) return Point2Dd(1, 0);
    Point2Dd p0, p1, p2, p3;
    SegmentControls(segment, p0, p1, p2, p3);
    Point2Dd d = CubicTangent(p0, p1, p2, p3, t);
    if (std::hypot(d.x, d.y) < kEps) d = Point2Dd(p3.x - p0.x, p3.y - p0.y);
    return d;
}

std::vector<Point2Dd> UltraCanvasBezierSubpath::Flatten(double tolerance) const {
    std::vector<Point2Dd> out;
    if (nodes.empty()) return out;
    out.push_back(nodes.front().anchor);
    for (int s = 0; s < SegmentCount(); ++s) {
        Point2Dd p0, p1, p2, p3;
        SegmentControls(s, p0, p1, p2, p3);
        if (SegmentIsLine(s)) { out.push_back(p3); continue; }
        const int n = CubicSteps(p0, p1, p2, p3, tolerance);
        for (int i = 1; i <= n; ++i) out.push_back(CubicAt(p0, p1, p2, p3, static_cast<double>(i) / n));
    }
    return out;
}

double UltraCanvasBezierSubpath::Length(double tolerance) const {
    const auto pts = Flatten(tolerance);
    double len = 0;
    for (size_t i = 1; i < pts.size(); ++i) len += Dist(pts[i - 1], pts[i]);
    return len;
}

Rect2Dd UltraCanvasBezierSubpath::Bounds(bool includeHandles) const {
    Rect2Dd r;
    bool any = false;
    for (const auto& p : Flatten(0.1)) UnionPoint(r, any, p);
    if (includeHandles) {
        for (const auto& n : nodes) {
            if (n.handleInActive) UnionPoint(r, any, n.handleIn);
            if (n.handleOutActive) UnionPoint(r, any, n.handleOut);
        }
    }
    return any ? r : Rect2Dd(0, 0, 0, 0);
}

int UltraCanvasBezierSubpath::InsertNodeAt(int segment, double t) {
    if (segment < 0 || segment >= SegmentCount()) return -1;
    t = std::clamp(t, 0.0, 1.0);
    const int ia = segment;
    const int ib = (segment + 1) % static_cast<int>(nodes.size());
    BezierNode fresh;
    if (SegmentIsLine(segment)) {
        fresh.anchor = Lerp(nodes[ia].anchor, nodes[ib].anchor, t);
        fresh.handleIn = fresh.handleOut = fresh.anchor;
        fresh.type = BezierNodeType::Corner;
    } else {
        Point2Dd p0, p1, p2, p3;
        SegmentControls(segment, p0, p1, p2, p3);
        // de Casteljau: the two halves' control points.
        const Point2Dd q0 = Lerp(p0, p1, t), q1 = Lerp(p1, p2, t), q2 = Lerp(p2, p3, t);
        const Point2Dd r0 = Lerp(q0, q1, t), r1 = Lerp(q1, q2, t);
        const Point2Dd s = Lerp(r0, r1, t);
        nodes[ia].handleOut = q0; nodes[ia].handleOutActive = true;
        nodes[ib].handleIn = q2;  nodes[ib].handleInActive = true;
        fresh.anchor = s;
        fresh.handleIn = r0;  fresh.handleInActive = true;
        fresh.handleOut = r1; fresh.handleOutActive = true;
        fresh.type = BezierNodeType::Smooth;
    }
    nodes.insert(nodes.begin() + ia + 1, fresh);
    return ia + 1;
}

bool UltraCanvasBezierSubpath::RemoveNode(int index) {
    if (index < 0 || index >= static_cast<int>(nodes.size())) return false;
    nodes.erase(nodes.begin() + index);
    return true;
}

void UltraCanvasBezierSubpath::SetNodeType(int index, BezierNodeType type) {
    if (index < 0 || index >= static_cast<int>(nodes.size())) return;
    BezierNode& n = nodes[index];
    n.type = type;
    if (type == BezierNodeType::Corner) return;
    // Both handles active, opposite each other; lengths per the type.
    Point2Dd dirOut(n.handleOut.x - n.anchor.x, n.handleOut.y - n.anchor.y);
    Point2Dd dirIn(n.handleIn.x - n.anchor.x, n.handleIn.y - n.anchor.y);
    double lenOut = n.handleOutActive ? std::hypot(dirOut.x, dirOut.y) : 0;
    double lenIn = n.handleInActive ? std::hypot(dirIn.x, dirIn.y) : 0;
    Point2Dd dir;
    if (lenOut > kEps) dir = Point2Dd(dirOut.x / lenOut, dirOut.y / lenOut);
    else if (lenIn > kEps) dir = Point2Dd(-dirIn.x / lenIn, -dirIn.y / lenIn);
    else {
        // No handles yet: take the direction between the neighbours.
        const int n0 = static_cast<int>(nodes.size());
        const Point2Dd& prev = nodes[(index - 1 + n0) % n0].anchor;
        const Point2Dd& next = nodes[(index + 1) % n0].anchor;
        dir = Point2Dd(next.x - prev.x, next.y - prev.y);
        const double l = std::hypot(dir.x, dir.y);
        dir = l > kEps ? Point2Dd(dir.x / l, dir.y / l) : Point2Dd(1, 0);
        lenOut = lenIn = std::max(1.0, Dist(prev, next) / 6.0);
    }
    if (lenOut <= kEps) lenOut = lenIn;
    if (lenIn <= kEps) lenIn = lenOut;
    if (type == BezierNodeType::Symmetric) lenIn = lenOut = (lenIn + lenOut) / 2.0;
    n.handleOut = Point2Dd(n.anchor.x + dir.x * lenOut, n.anchor.y + dir.y * lenOut);
    n.handleIn = Point2Dd(n.anchor.x - dir.x * lenIn, n.anchor.y - dir.y * lenIn);
    n.handleOutActive = n.handleInActive = true;
}

void UltraCanvasBezierSubpath::MoveAnchor(int index, const Point2Dd& to) {
    if (index < 0 || index >= static_cast<int>(nodes.size())) return;
    BezierNode& n = nodes[index];
    const double dx = to.x - n.anchor.x, dy = to.y - n.anchor.y;
    n.anchor = to;
    n.handleIn.x += dx; n.handleIn.y += dy;
    n.handleOut.x += dx; n.handleOut.y += dy;
}

void UltraCanvasBezierSubpath::MoveHandle(int index, bool outgoing, const Point2Dd& to) {
    if (index < 0 || index >= static_cast<int>(nodes.size())) return;
    BezierNode& n = nodes[index];
    Point2Dd& moved = outgoing ? n.handleOut : n.handleIn;
    Point2Dd& other = outgoing ? n.handleIn : n.handleOut;
    bool& movedActive = outgoing ? n.handleOutActive : n.handleInActive;
    bool& otherActive = outgoing ? n.handleInActive : n.handleOutActive;
    moved = to;
    movedActive = true;
    if (n.type == BezierNodeType::Corner || !otherActive) return;
    const Point2Dd dir(moved.x - n.anchor.x, moved.y - n.anchor.y);
    const double len = std::hypot(dir.x, dir.y);
    if (len < kEps) return;
    const double otherLen = n.type == BezierNodeType::Symmetric
                            ? len : Dist(other, n.anchor);
    other = Point2Dd(n.anchor.x - dir.x / len * otherLen, n.anchor.y - dir.y / len * otherLen);
}

void UltraCanvasBezierSubpath::DragSegment(int segment, double t, const Point2Dd& delta) {
    if (segment < 0 || segment >= SegmentCount()) return;
    const int ia = segment;
    const int ib = (segment + 1) % static_cast<int>(nodes.size());
    if (SegmentIsLine(segment)) {
        // A straight segment moves as a whole.
        MoveAnchor(ia, Point2Dd(nodes[ia].anchor.x + delta.x, nodes[ia].anchor.y + delta.y));
        MoveAnchor(ib, Point2Dd(nodes[ib].anchor.x + delta.x, nodes[ib].anchor.y + delta.y));
        return;
    }
    // Distribute the displacement over the two handles by their Bernstein
    // weights at t, so the curve point at t moves by exactly delta.
    t = std::clamp(t, 0.01, 0.99);
    const double u = 1 - t;
    const double w1 = 3 * u * u * t, w2 = 3 * u * t * t;
    const double sum = w1 * w1 + w2 * w2;
    const double k1 = w1 / sum, k2 = w2 / sum;
    BezierNode& a = nodes[ia];
    BezierNode& b = nodes[ib];
    if (!a.handleOutActive) { a.handleOut = a.anchor; a.handleOutActive = true; }
    if (!b.handleInActive) { b.handleIn = b.anchor; b.handleInActive = true; }
    MoveHandle(ia, true, Point2Dd(a.handleOut.x + delta.x * k1, a.handleOut.y + delta.y * k1));
    MoveHandle(ib, false, Point2Dd(b.handleIn.x + delta.x * k2, b.handleIn.y + delta.y * k2));
}

void UltraCanvasBezierSubpath::Reverse() {
    std::reverse(nodes.begin(), nodes.end());
    for (auto& n : nodes) {
        std::swap(n.handleIn, n.handleOut);
        std::swap(n.handleInActive, n.handleOutActive);
    }
}

void UltraCanvasBezierSubpath::Translate(double dx, double dy) {
    for (auto& n : nodes) {
        n.anchor.x += dx; n.anchor.y += dy;
        n.handleIn.x += dx; n.handleIn.y += dy;
        n.handleOut.x += dx; n.handleOut.y += dy;
    }
}

std::optional<UltraCanvasBezierSubpath::Hit> UltraCanvasBezierSubpath::HitTestOutline(const Point2Dd& p, double maxDistance) const {
    std::optional<Hit> best;
    for (int s = 0; s < SegmentCount(); ++s) {
        Point2Dd p0, p1, p2, p3;
        SegmentControls(s, p0, p1, p2, p3);
        const int n = SegmentIsLine(s) ? 1 : CubicSteps(p0, p1, p2, p3, 0.1);
        Point2Dd prev = p0;
        for (int i = 1; i <= n; ++i) {
            const double tb = static_cast<double>(i) / n;
            const Point2Dd cur = CubicAt(p0, p1, p2, p3, tb);
            double u;
            const double d = PointSegmentDistance(p, prev, cur, u);
            if (d <= maxDistance && (!best || d < best->distance)) {
                Hit h;
                h.segment = s;
                h.t = (i - 1 + u) / n;
                h.distance = d;
                h.point = Lerp(prev, cur, u);
                best = h;
            }
            prev = cur;
        }
    }
    return best;
}

bool UltraCanvasBezierSubpath::ContainsPoint(const Point2Dd& p, bool evenOdd) const {
    const auto pts = Flatten(0.1);
    if (pts.size() < 3) return false;
    int winding = 0;
    bool odd = false;
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
        const Point2Dd& a = pts[j];
        const Point2Dd& b = pts[i];
        if ((b.y > p.y) != (a.y > p.y)) {
            const double x = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (p.x < x) {
                odd = !odd;
                winding += (b.y > a.y) ? 1 : -1;
            }
        }
    }
    return evenOdd ? odd : winding != 0;
}

void UltraCanvasBezierSubpath::BuildPath(IRenderContext* ctx) const {
    if (!ctx || nodes.empty()) return;
    ctx->MoveTo(nodes.front().anchor.x, nodes.front().anchor.y);
    for (int s = 0; s < SegmentCount(); ++s) {
        Point2Dd p0, p1, p2, p3;
        SegmentControls(s, p0, p1, p2, p3);
        if (SegmentIsLine(s)) ctx->LineTo(p3.x, p3.y);
        else ctx->BezierCurveTo(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y);
    }
    if (closed) ctx->ClosePath();
}

// ===========================================================================
// PATH
// ===========================================================================

int UltraCanvasBezierPath::NodeCount() const {
    int n = 0;
    for (const auto& s : subpaths) n += static_cast<int>(s.nodes.size());
    return n;
}

Rect2Dd UltraCanvasBezierPath::Bounds(bool includeHandles) const {
    Rect2Dd r;
    bool any = false;
    for (const auto& s : subpaths) {
        if (s.nodes.empty()) continue;
        const Rect2Dd b = s.Bounds(includeHandles);
        UnionPoint(r, any, Point2Dd(b.x, b.y));
        UnionPoint(r, any, Point2Dd(b.x + b.width, b.y + b.height));
    }
    return any ? r : Rect2Dd(0, 0, 0, 0);
}

void UltraCanvasBezierPath::Transform(const VectorStorage::Matrix3x3& m) {
    for (auto& s : subpaths)
        for (auto& n : s.nodes) {
            n.anchor = m.Transform(n.anchor);
            n.handleIn = m.Transform(n.handleIn);
            n.handleOut = m.Transform(n.handleOut);
        }
}

void UltraCanvasBezierPath::Translate(double dx, double dy) {
    for (auto& s : subpaths) s.Translate(dx, dy);
}

UltraCanvasBezierPath UltraCanvasBezierPath::FromPathData(const VectorStorage::PathData& data) {
    using VectorConverter::PathOps::FlatSeg;
    UltraCanvasBezierPath path;
    const auto segs = VectorConverter::PathOps::NormalizePath(data);
    UltraCanvasBezierSubpath* cur = nullptr;
    auto finish = [&]() {
        if (!cur) return;
        // A closed subpath whose last node sits on the first is the same
        // node: fold the closing segment's incoming handle into node 0.
        if (cur->closed && cur->nodes.size() >= 2 &&
            Dist(cur->nodes.front().anchor, cur->nodes.back().anchor) < 1e-6) {
            BezierNode& last = cur->nodes.back();
            BezierNode& first = cur->nodes.front();
            first.handleIn = last.handleIn;
            first.handleInActive = last.handleInActive;
            cur->nodes.pop_back();
        }
        for (auto& n : cur->nodes) n.type = InferType(n);
        if (cur->nodes.empty()) path.subpaths.pop_back();
        cur = nullptr;
    };
    for (const auto& seg : segs) {
        switch (seg.kind) {
            case FlatSeg::Move: {
                finish();
                path.subpaths.emplace_back();
                cur = &path.subpaths.back();
                BezierNode n;
                n.anchor = n.handleIn = n.handleOut = seg.p[0];
                cur->nodes.push_back(n);
                break;
            }
            case FlatSeg::Line: {
                if (!cur) { path.subpaths.emplace_back(); cur = &path.subpaths.back(); BezierNode n0; n0.anchor = seg.p[0]; cur->nodes.push_back(n0); break; }
                BezierNode n;
                n.anchor = n.handleIn = n.handleOut = seg.p[0];
                cur->nodes.push_back(n);
                break;
            }
            case FlatSeg::Cubic: {
                if (!cur) { path.subpaths.emplace_back(); cur = &path.subpaths.back(); BezierNode n0; n0.anchor = seg.p[0]; cur->nodes.push_back(n0); }
                BezierNode& prev = cur->nodes.back();
                prev.handleOut = seg.p[0];
                prev.handleOutActive = Dist(seg.p[0], prev.anchor) > 1e-9;
                BezierNode n;
                n.anchor = seg.p[2];
                n.handleIn = seg.p[1];
                n.handleInActive = Dist(seg.p[1], n.anchor) > 1e-9;
                n.handleOut = n.anchor;
                cur->nodes.push_back(n);
                break;
            }
        }
        if (seg.closeAfter && cur) {
            cur->closed = true;
            finish();
        }
    }
    finish();
    return path;
}

VectorStorage::PathData UltraCanvasBezierPath::ToPathData() const {
    using namespace VectorStorage;
    PathData data;
    auto cmd = [&](PathCommandType type, std::initializer_list<double> params) {
        PathCommand c;
        c.Type = type;
        c.Relative = false;
        for (double v : params) c.Parameters.push_back(static_cast<float>(v));
        data.commands.push_back(c);
    };
    for (const auto& s : subpaths) {
        if (s.nodes.empty()) continue;
        cmd(PathCommandType::MoveTo, {s.nodes.front().anchor.x, s.nodes.front().anchor.y});
        const int count = s.SegmentCount();
        for (int i = 0; i < count; ++i) {
            // The closing segment of a closed subpath is what Z draws when
            // it is straight; only a curved one needs writing out.
            if (s.closed && i == count - 1 && s.SegmentIsLine(i)) break;
            Point2Dd p0, p1, p2, p3;
            s.SegmentControls(i, p0, p1, p2, p3);
            if (s.SegmentIsLine(i)) cmd(PathCommandType::LineTo, {p3.x, p3.y});
            else cmd(PathCommandType::CurveTo, {p1.x, p1.y, p2.x, p2.y, p3.x, p3.y});
        }
        if (s.closed) cmd(PathCommandType::ClosePath, {});
    }
    return data;
}

std::optional<UltraCanvasBezierPath> UltraCanvasBezierPath::FromSVGPathData(const std::string& d) {
    VectorStorage::PathData data = VectorStorage::ParsePathString(d);
    if (data.commands.empty()) return std::nullopt;
    return FromPathData(data);
}

std::string UltraCanvasBezierPath::ToSVGPathData() const {
    return VectorStorage::SerializePathData(ToPathData());
}

std::vector<Point2Dd> SimplifyPolyline(const std::vector<Point2Dd>& points, double tolerance) {
    if (points.size() < 3 || tolerance <= 0) return points;
    std::vector<bool> keep(points.size(), false);
    keep.front() = keep.back() = true;
    std::vector<std::pair<size_t, size_t>> stack{{0, points.size() - 1}};
    while (!stack.empty()) {
        auto [a, b] = stack.back();
        stack.pop_back();
        double worst = 0;
        size_t idx = a;
        for (size_t i = a + 1; i < b; ++i) {
            double t;
            const double dd = PointSegmentDistance(points[i], points[a], points[b], t);
            if (dd > worst) { worst = dd; idx = i; }
        }
        if (worst > tolerance) {
            keep[idx] = true;
            stack.push_back({a, idx});
            stack.push_back({idx, b});
        }
    }
    std::vector<Point2Dd> out;
    for (size_t i = 0; i < points.size(); ++i) if (keep[i]) out.push_back(points[i]);
    return out;
}

UltraCanvasBezierPath UltraCanvasBezierPath::FromPolyline(const std::vector<Point2Dd>& points, bool closed,
                                                          double tolerance, double smoothing) {
    UltraCanvasBezierPath path;
    std::vector<Point2Dd> pts = SimplifyPolyline(points, tolerance);
    // Drop duplicate consecutive points.
    pts.erase(std::unique(pts.begin(), pts.end(), [](const Point2Dd& a, const Point2Dd& b) {
        return Dist(a, b) < 1e-9;
    }), pts.end());
    if (closed && pts.size() > 2 && Dist(pts.front(), pts.back()) < 1e-9) pts.pop_back();
    if (pts.empty()) return path;
    UltraCanvasBezierSubpath sp;
    sp.closed = closed && pts.size() >= 3;
    const int n = static_cast<int>(pts.size());
    for (int i = 0; i < n; ++i) {
        BezierNode node;
        node.anchor = pts[i];
        node.handleIn = node.handleOut = pts[i];
        if (n >= 2 && smoothing > 0) {
            const bool hasPrev = sp.closed || i > 0;
            const bool hasNext = sp.closed || i < n - 1;
            const Point2Dd& prev = pts[(i - 1 + n) % n];
            const Point2Dd& next = pts[(i + 1) % n];
            // Catmull-Rom tangent; at an open end, the chord to the neighbour.
            Point2Dd tangent = hasPrev && hasNext ? Point2Dd(next.x - prev.x, next.y - prev.y)
                             : hasNext ? Point2Dd(next.x - pts[i].x, next.y - pts[i].y)
                             : Point2Dd(pts[i].x - prev.x, pts[i].y - prev.y);
            if (hasNext) {
                node.handleOut = Point2Dd(pts[i].x + tangent.x * smoothing, pts[i].y + tangent.y * smoothing);
                node.handleOutActive = true;
            }
            if (hasPrev) {
                node.handleIn = Point2Dd(pts[i].x - tangent.x * smoothing, pts[i].y - tangent.y * smoothing);
                node.handleInActive = true;
            }
            node.type = node.handleInActive && node.handleOutActive ? BezierNodeType::Smooth : BezierNodeType::Corner;
        }
        sp.nodes.push_back(node);
    }
    path.subpaths.push_back(sp);
    return path;
}

void UltraCanvasBezierPath::BuildPath(IRenderContext* ctx) const {
    for (const auto& s : subpaths) s.BuildPath(ctx);
}

std::optional<UltraCanvasBezierPath::Hit> UltraCanvasBezierPath::HitTestOutline(const Point2Dd& p, double maxDistance) const {
    std::optional<Hit> best;
    for (size_t i = 0; i < subpaths.size(); ++i) {
        auto h = subpaths[i].HitTestOutline(p, maxDistance);
        if (h && (!best || h->distance < best->distance)) {
            Hit hit;
            hit.subpath = static_cast<int>(i);
            hit.segment = h->segment;
            hit.t = h->t;
            hit.distance = h->distance;
            hit.point = h->point;
            best = hit;
        }
    }
    return best;
}

bool UltraCanvasBezierPath::ContainsPoint(const Point2Dd& p, bool evenOdd) const {
    // Subpaths combine under the fill rule: even-odd toggles, non-zero sums.
    int count = 0;
    for (const auto& s : subpaths) if (s.ContainsPoint(p, evenOdd)) ++count;
    return evenOdd ? (count % 2) == 1 : count > 0;
}

std::optional<std::pair<int, int>> UltraCanvasBezierPath::HitTestNode(const Point2Dd& p, double maxDistance) const {
    std::optional<std::pair<int, int>> best;
    double bestDist = maxDistance;
    for (size_t i = 0; i < subpaths.size(); ++i)
        for (size_t j = 0; j < subpaths[i].nodes.size(); ++j) {
            const double d = Dist(p, subpaths[i].nodes[j].anchor);
            if (d <= bestDist) { bestDist = d; best = {static_cast<int>(i), static_cast<int>(j)}; }
        }
    return best;
}

} // namespace UltraCanvas

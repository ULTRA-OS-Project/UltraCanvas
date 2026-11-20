// UltraCanvas/Plugins/Vector/UltraCanvasVectorFillRule.cpp
// A non-zero fill for a format that only fills even-odd - see the header.
// Version: 1.0.0
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework

#include "UltraCanvasVectorFillRule.h"

#include <algorithm>
#include <cmath>

namespace UltraCanvas {
namespace VectorConverter {

using PathOps::FlatSeg;

namespace {

struct Loop {
    size_t begin = 0, end = 0;            // its segments in the input
    std::vector<Point2Dd> polygon;        // flattened, implicitly closed
    double area = 0;                      // signed (shoelace)
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
};

// The loop flattened: each cubic sampled at 16 points, which places the
// containment and crossing tests well inside any outline's detail.
void Flatten(const std::vector<FlatSeg>& segs, Loop& loop) {
    Point2Dd cur{0, 0};
    for (size_t i = loop.begin; i < loop.end; ++i) {
        const FlatSeg& s = segs[i];
        if (s.kind == FlatSeg::Cubic) {
            for (int k = 1; k <= 16; ++k) {
                const double t = k / 16.0, u = 1 - t;
                loop.polygon.emplace_back(
                        u * u * u * cur.x + 3 * u * u * t * s.p[0].x + 3 * u * t * t * s.p[1].x + t * t * t * s.p[2].x,
                        u * u * u * cur.y + 3 * u * u * t * s.p[0].y + 3 * u * t * t * s.p[1].y + t * t * t * s.p[2].y);
            }
            cur = s.p[2];
        } else {
            loop.polygon.push_back(s.p[0]);
            cur = s.p[0];
        }
    }
    const auto& v = loop.polygon;
    if (v.empty()) return;
    loop.minX = loop.maxX = v[0].x;
    loop.minY = loop.maxY = v[0].y;
    for (size_t i = 0, j = v.size() - 1; i < v.size(); j = i++) {
        loop.area += (v[j].x * v[i].y - v[i].x * v[j].y) / 2;
        loop.minX = std::min(loop.minX, v[i].x); loop.maxX = std::max(loop.maxX, v[i].x);
        loop.minY = std::min(loop.minY, v[i].y); loop.maxY = std::max(loop.maxY, v[i].y);
    }
}

bool Inside(const std::vector<Point2Dd>& v, const Point2Dd& p) {
    bool in = false;
    for (size_t i = 0, j = v.size() - 1; i < v.size(); j = i++)
        if (((v[i].y > p.y) != (v[j].y > p.y)) &&
            (p.x < (v[j].x - v[i].x) * (p.y - v[i].y) / (v[j].y - v[i].y) + v[i].x))
            in = !in;
    return in;
}

double Cross(const Point2Dd& o, const Point2Dd& a, const Point2Dd& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// Whether two loops' edges cross (properly: touching does not count).
bool Crosses(const Loop& a, const Loop& b) {
    if (a.maxX < b.minX || b.maxX < a.minX || a.maxY < b.minY || b.maxY < a.minY) return false;
    const auto& u = a.polygon;
    const auto& v = b.polygon;
    for (size_t i = 0, ip = u.size() - 1; i < u.size(); ip = i++) {
        const double ex0 = std::min(u[ip].x, u[i].x), ex1 = std::max(u[ip].x, u[i].x);
        const double ey0 = std::min(u[ip].y, u[i].y), ey1 = std::max(u[ip].y, u[i].y);
        if (ex1 < b.minX || ex0 > b.maxX || ey1 < b.minY || ey0 > b.maxY) continue;
        for (size_t j = 0, jp = v.size() - 1; j < v.size(); jp = j++) {
            const double d1 = Cross(u[ip], u[i], v[jp]), d2 = Cross(u[ip], u[i], v[j]);
            const double d3 = Cross(v[jp], v[j], u[ip]), d4 = Cross(v[jp], v[j], u[i]);
            if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0)))
                return true;
        }
    }
    return false;
}

}   // namespace

std::vector<FlatSeg> NonZeroAsEvenOdd(const std::vector<FlatSeg>& segs, bool* exact) {
    if (exact) *exact = true;
    std::vector<Loop> loops;
    for (size_t i = 0; i < segs.size(); ++i) {
        if (segs[i].kind == FlatSeg::Move || loops.empty()) {
            if (!loops.empty()) loops.back().end = i;
            loops.emplace_back();
            loops.back().begin = i;
        }
    }
    if (loops.size() < 2) return segs;   // one subpath is its own boundary
    loops.back().end = segs.size();
    for (Loop& l : loops) Flatten(segs, l);

    // Crossing subpaths make the result an approximation. The check is
    // quadratic in edges, so it is skipped for a path too large to afford
    // it - which is then simply not reported.
    size_t edges = 0;
    for (const Loop& l : loops) edges += l.polygon.size();
    if (exact && edges <= 20000) {
        for (size_t a = 0; a < loops.size() && *exact; ++a)
            for (size_t b = a + 1; b < loops.size(); ++b)
                if (loops[a].polygon.size() >= 3 && loops[b].polygon.size() >= 3 && Crosses(loops[a], loops[b])) {
                    *exact = false;
                    break;
                }
    }

    // A loop's winding is the sum of the turns of the loops around it; the
    // fill state just inside it against just outside it decides whether it
    // is a boundary. A loop with no area winds no way and bounds nothing.
    std::vector<int> sign(loops.size());
    for (size_t i = 0; i < loops.size(); ++i)
        sign[i] = loops[i].polygon.size() < 3 || std::fabs(loops[i].area) < 1e-12 ? 0 : (loops[i].area > 0 ? 1 : -1);
    std::vector<bool> keep(loops.size(), false);
    bool all = true;
    for (size_t i = 0; i < loops.size(); ++i) {
        if (sign[i] == 0) { all = false; continue; }
        int outside = 0;
        for (size_t j = 0; j < loops.size(); ++j) {
            if (j == i || sign[j] == 0 || std::fabs(loops[j].area) <= std::fabs(loops[i].area)) continue;
            if (Inside(loops[j].polygon, loops[i].polygon.front())) outside += sign[j];
        }
        const int inside = outside + sign[i];
        keep[i] = (inside != 0) != (outside != 0);
        all = all && keep[i];
    }
    if (all) return segs;
    std::vector<FlatSeg> out;
    for (size_t i = 0; i < loops.size(); ++i)
        if (keep[i]) out.insert(out.end(), segs.begin() + static_cast<std::ptrdiff_t>(loops[i].begin),
                                segs.begin() + static_cast<std::ptrdiff_t>(loops[i].end));
    return out;
}

} // namespace VectorConverter
} // namespace UltraCanvas

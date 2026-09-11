// core/DataFormats/UltraCanvasPolygonTriangulation.cpp
// Ear clipping, hole bridging and Newell projection. Declared in
// include/DataFormats/UltraCanvasPolygonTriangulation.h.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "DataFormats/UltraCanvasPolygonTriangulation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace UltraCanvas {
namespace ModelStorage {

namespace {

// Which side of ab does c fall on? Positive is left, for a counter-clockwise
// polygon that means convex.
double Cross(const Vec2d& a, const Vec2d& b, const Vec2d& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// Barycentric containment, with the boundary counted as inside. Ear clipping
// has to reject an ear that merely touches another vertex, or it produces
// zero-area triangles and leaves the polygon unclippable.
bool PointInTriangle(const Vec2d& p, const Vec2d& a, const Vec2d& b, const Vec2d& c) {
    const double d1 = Cross(a, b, p);
    const double d2 = Cross(b, c, p);
    const double d3 = Cross(c, a, p);
    const bool negative = (d1 < 0.0) || (d2 < 0.0) || (d3 < 0.0);
    const bool positive = (d1 > 0.0) || (d2 > 0.0) || (d3 > 0.0);
    return !(negative && positive);
}

} // namespace

double PolygonArea2D(const std::vector<Vec2d>& polygon) {
    if (polygon.size() < 3) return 0.0;
    double twice = 0.0;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
        twice += (polygon[j].x * polygon[i].y) - (polygon[i].x * polygon[j].y);
    return twice * 0.5;
}

bool TriangulatePolygon2D(const std::vector<Vec2d>& polygon,
                          std::vector<uint32_t>& outTriangles) {
    const size_t n = polygon.size();
    if (n < 3) return false;
    if (n == 3) {
        outTriangles.push_back(0);
        outTriangles.push_back(1);
        outTriangles.push_back(2);
        return true;
    }

    // Work counter-clockwise; the output is flipped back at the end if the
    // input was clockwise, so the caller's winding is preserved.
    const bool counterClockwise = PolygonArea2D(polygon) > 0.0;

    std::vector<uint32_t> remaining(n);
    for (size_t i = 0; i < n; ++i)
        remaining[i] = static_cast<uint32_t>(counterClockwise ? i : n - 1 - i);

    const size_t produced = outTriangles.size();
    // Each successful clip removes one vertex, so 2n iterations is generous
    // even with the skips a difficult polygon forces.
    size_t guard = 2 * n;

    while (remaining.size() > 3 && guard-- > 0) {
        bool clipped = false;
        const size_t count = remaining.size();

        for (size_t i = 0; i < count; ++i) {
            const uint32_t ia = remaining[(i + count - 1) % count];
            const uint32_t ib = remaining[i];
            const uint32_t ic = remaining[(i + 1) % count];
            const Vec2d& a = polygon[ia];
            const Vec2d& b = polygon[ib];
            const Vec2d& c = polygon[ic];

            // Reflex or collinear: not an ear. Collinear is excluded because
            // clipping it emits a zero-area triangle and can strand the rest.
            if (Cross(a, b, c) <= 0.0) continue;

            // An ear may not contain any other vertex of the polygon — this is
            // the whole difference from a fan, and the reason a concave face
            // comes out right.
            //
            // A vertex that merely *coincides* with one of the ear's corners
            // does not count. Hole bridging deliberately duplicates two
            // vertices to cut into and back out of a hole, and treating those
            // duplicates as blockers makes a bridged polygon unclippable —
            // every ear looks occupied, and the fallback below then emits
            // overlapping triangles.
            bool contains = false;
            for (size_t k = 0; k < count && !contains; ++k) {
                const uint32_t id = remaining[k];
                if (id == ia || id == ib || id == ic) continue;
                const Vec2d& p = polygon[id];
                if ((p - a).Length() <= 0.0 || (p - b).Length() <= 0.0 ||
                    (p - c).Length() <= 0.0) continue;
                if (PointInTriangle(p, a, b, c)) contains = true;
            }
            if (contains) continue;

            outTriangles.push_back(ia);
            outTriangles.push_back(ib);
            outTriangles.push_back(ic);
            remaining.erase(remaining.begin() + static_cast<long>(i));
            clipped = true;
            break;
        }

        if (!clipped) {
            // No ear found: the polygon self-intersects, or every remaining
            // corner is collinear. Clip the least-bad corner so progress
            // continues rather than looping, and report the shortfall.
            size_t best = 0;
            double bestArea = -std::numeric_limits<double>::max();
            for (size_t i = 0; i < count; ++i) {
                const Vec2d& a = polygon[remaining[(i + count - 1) % count]];
                const Vec2d& b = polygon[remaining[i]];
                const Vec2d& c = polygon[remaining[(i + 1) % count]];
                const double area = Cross(a, b, c);
                if (area > bestArea) { bestArea = area; best = i; }
            }
            if (bestArea <= 0.0) break;   // nothing left with positive area
            outTriangles.push_back(remaining[(best + count - 1) % count]);
            outTriangles.push_back(remaining[best]);
            outTriangles.push_back(remaining[(best + 1) % count]);
            remaining.erase(remaining.begin() + static_cast<long>(best));
        }
    }

    bool complete = false;
    if (remaining.size() == 3) {
        outTriangles.push_back(remaining[0]);
        outTriangles.push_back(remaining[1]);
        outTriangles.push_back(remaining[2]);
        complete = true;
    }

    if (!counterClockwise) {
        // Restore the caller's winding: the vertices were visited in reverse,
        // so each emitted triangle is reversed back.
        for (size_t i = produced; i + 2 < outTriangles.size(); i += 3)
            std::swap(outTriangles[i], outTriangles[i + 2]);
    }
    return complete;
}

namespace {

// The bridging cut of Eberly / the classic "polygon with holes" method: take
// the hole's rightmost vertex M, shoot a ray from it to +x, find the outer
// edge it first crosses, and pick as the bridge partner either that edge's
// endpoint or — when the cut would clip a reflex vertex of the outer boundary
// — the nearest reflex vertex inside the triangle M, intersection, endpoint.
size_t FindBridgeVertex(const std::vector<Vec2d>& outer, const Vec2d& holePoint) {
    double bestX = -std::numeric_limits<double>::max();
    size_t bestEdge = outer.size();

    for (size_t i = 0, j = outer.size() - 1; i < outer.size(); j = i++) {
        const Vec2d& a = outer[j];
        const Vec2d& b = outer[i];
        // Only edges that span the hole point's y can be crossed by the ray.
        if ((a.y > holePoint.y) == (b.y > holePoint.y)) continue;
        const double t = (holePoint.y - a.y) / (b.y - a.y);
        const double x = a.x + t * (b.x - a.x);
        if (x < holePoint.x) continue;              // the ray goes to +x only
        if (x > bestX) { bestX = x; bestEdge = j; }
    }
    if (bestEdge >= outer.size()) return outer.size();

    // Of the crossed edge's two endpoints, the one further along +x is the
    // candidate the standard construction uses.
    const size_t next = (bestEdge + 1) % outer.size();
    size_t candidate = outer[bestEdge].x > outer[next].x ? bestEdge : next;

    // Any outer vertex inside the triangle (holePoint, (bestX, y), candidate)
    // would be cut off by the bridge, so the closest such reflex vertex
    // becomes the partner instead.
    const Vec2d intersection(bestX, holePoint.y);
    const Vec2d& c = outer[candidate];
    double bestAngle = std::numeric_limits<double>::max();
    size_t replacement = candidate;
    for (size_t i = 0; i < outer.size(); ++i) {
        if (i == candidate) continue;
        const Vec2d& p = outer[i];
        if (!PointInTriangle(p, holePoint, intersection, c)) continue;
        const Vec2d d = p - holePoint;
        const double length = d.Length();
        if (length <= 0.0) continue;
        // Prefer the smallest angle off the ray; ties break on distance.
        const double angle = std::acos(std::max(-1.0, std::min(1.0, d.x / length)));
        if (angle < bestAngle) { bestAngle = angle; replacement = i; }
    }
    return replacement;
}

} // namespace

bool TriangulatePolygonWithHoles2D(const std::vector<Vec2d>& outer,
                                   const std::vector<std::vector<Vec2d>>& holes,
                                   std::vector<Vec2d>& outVertices,
                                   std::vector<uint32_t>& outSources,
                                   std::vector<uint32_t>& outTriangles) {
    outVertices.clear();
    outSources.clear();
    if (outer.size() < 3) return false;

    // The outer boundary must run counter-clockwise and each hole clockwise
    // for the bridge to produce a single simple polygon.
    std::vector<Vec2d> merged(outer);
    std::vector<uint32_t> sources(outer.size());
    for (uint32_t i = 0; i < outer.size(); ++i) sources[i] = i;
    if (PolygonArea2D(outer) < 0.0) {
        std::reverse(merged.begin(), merged.end());
        std::reverse(sources.begin(), sources.end());
    }

    uint32_t sourceBase = static_cast<uint32_t>(outer.size());

    // Cut the holes in rightmost-first order: a hole bridged later may attach
    // to a vertex an earlier bridge introduced, which is legal and is why the
    // order matters.
    std::vector<size_t> order(holes.size());
    for (size_t i = 0; i < holes.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&holes](size_t a, size_t b) {
        auto rightmost = [](const std::vector<Vec2d>& h) {
            double x = -std::numeric_limits<double>::max();
            for (const Vec2d& p : h) x = std::max(x, p.x);
            return x;
        };
        return rightmost(holes[a]) > rightmost(holes[b]);
    });

    std::vector<uint32_t> holeBase(holes.size(), 0);
    {
        uint32_t base = sourceBase;
        for (size_t i = 0; i < holes.size(); ++i) {
            holeBase[i] = base;
            base += static_cast<uint32_t>(holes[i].size());
        }
    }

    bool bridgedAll = true;
    for (size_t oi = 0; oi < order.size(); ++oi) {
        const size_t h = order[oi];
        const std::vector<Vec2d>& hole = holes[h];
        if (hole.size() < 3) continue;

        std::vector<Vec2d> ring(hole);
        std::vector<uint32_t> ringSources(hole.size());
        for (uint32_t i = 0; i < hole.size(); ++i) ringSources[i] = holeBase[h] + i;
        if (PolygonArea2D(ring) > 0.0) {   // holes run opposite to the outer loop
            std::reverse(ring.begin(), ring.end());
            std::reverse(ringSources.begin(), ringSources.end());
        }

        // Enter the hole at its rightmost vertex, which is the one guaranteed
        // to see the outer boundary along +x.
        size_t entry = 0;
        for (size_t i = 1; i < ring.size(); ++i)
            if (ring[i].x > ring[entry].x) entry = i;

        const size_t bridge = FindBridgeVertex(merged, ring[entry]);
        if (bridge >= merged.size()) { bridgedAll = false; continue; }

        // Splice: ... outer[bridge], hole[entry..entry], outer[bridge] ...
        std::vector<Vec2d> spliced;
        std::vector<uint32_t> splicedSources;
        spliced.reserve(merged.size() + ring.size() + 2);
        splicedSources.reserve(merged.size() + ring.size() + 2);
        for (size_t i = 0; i <= bridge; ++i) {
            spliced.push_back(merged[i]);
            splicedSources.push_back(sources[i]);
        }
        for (size_t k = 0; k <= ring.size(); ++k) {
            const size_t idx = (entry + k) % ring.size();
            spliced.push_back(ring[idx]);
            splicedSources.push_back(ringSources[idx]);
        }
        // Duplicate the bridge vertex to come back out of the hole.
        spliced.push_back(merged[bridge]);
        splicedSources.push_back(sources[bridge]);
        for (size_t i = bridge + 1; i < merged.size(); ++i) {
            spliced.push_back(merged[i]);
            splicedSources.push_back(sources[i]);
        }

        merged.swap(spliced);
        sources.swap(splicedSources);
    }

    outVertices = std::move(merged);
    outSources = std::move(sources);
    const bool complete = TriangulatePolygon2D(outVertices, outTriangles);
    return complete && bridgedAll;
}

Vec3d NewellNormal(const std::vector<Vec3d>& polygon) {
    Vec3d n;
    if (polygon.size() < 3) return n;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Vec3d& a = polygon[j];
        const Vec3d& b = polygon[i];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n;
}

bool TriangulatePolygon3D(const std::vector<Vec3d>& polygon,
                          std::vector<uint32_t>& outTriangles) {
    if (polygon.size() < 3) return false;
    if (polygon.size() == 3) {
        outTriangles.push_back(0);
        outTriangles.push_back(1);
        outTriangles.push_back(2);
        return true;
    }

    const Vec3d normal = NewellNormal(polygon).Normalized();
    if (normal.Length() < 0.5) {
        // Every vertex collinear, or the polygon has no area. Nothing sound to
        // emit; a fan here would produce degenerate triangles.
        return false;
    }

    // Any two axes orthogonal to the normal give a projection that preserves
    // winding; picking the one least aligned with it keeps the basis stable.
    Vec3d axisX{1.0, 0.0, 0.0};
    if (std::fabs(normal.x) > 0.9) axisX = Vec3d{0.0, 1.0, 0.0};
    axisX = (axisX - normal * normal.Dot(axisX)).Normalized();
    const Vec3d axisY = normal.Cross(axisX);

    std::vector<Vec2d> flat;
    flat.reserve(polygon.size());
    for (const Vec3d& p : polygon)
        flat.emplace_back(p.Dot(axisX), p.Dot(axisY));

    return TriangulatePolygon2D(flat, outTriangles);
}

} // namespace ModelStorage
} // namespace UltraCanvas

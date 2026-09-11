// include/DataFormats/UltraCanvasPolygonTriangulation.h
// Turning a polygon into triangles, correctly — including concave ones and
// ones with holes.
//
// Two callers need this and used to have half of it each:
//
//   MeshPrimitive::Triangulate() fanned every n-gon about its first vertex.
//   That is right for a convex face and wrong for a concave one, where the fan
//   emits triangles that lie outside the polygon: a hand-authored OBJ or PLY
//   L-shaped face came out with its notch filled in.
//
//   A trimmed B-rep face is a region of parameter space bounded by one outer
//   loop and any number of inner ones. Meshing it *is* triangulating a polygon
//   with holes; there is no fan that does it at all.
//
// The algorithm is ear clipping (O(n^2), which is the right trade for the face
// sizes 3D files actually contain — the alternative, a sweep-line monotone
// decomposition, is O(n log n) and several times the code), with holes bridged
// into the outer boundary first by the standard visible-vertex cut. Degenerate
// input degrades rather than fails: a polygon that cannot be fully clipped
// emits the triangles it managed and reports the shortfall.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_POLYGON_TRIANGULATION_H
#define ULTRACANVAS_POLYGON_TRIANGULATION_H

#include "DataFormats/UltraCanvasModelMath.h"

#include <cstdint>
#include <vector>

namespace UltraCanvas {
namespace ModelStorage {

// Signed area of a closed 2D polygon; positive when counter-clockwise.
    double PolygonArea2D(const std::vector<Vec2d>& polygon);

// Triangulate a simple polygon. `outTriangles` receives indices into
// `polygon`, three per triangle, appended to whatever is already there.
// Winding of the output follows the input's.
//
// Returns true when the polygon was fully consumed. False means the input was
// self-intersecting or degenerate and the output is the best partial cover
// found — which is still better than nothing for a mesh.
    bool TriangulatePolygon2D(const std::vector<Vec2d>& polygon,
                              std::vector<uint32_t>& outTriangles);

// Triangulate an outer boundary with holes. The holes are bridged into the
// outer loop, so the result is a triangulation of the material region only.
//
// `outVertices` receives the merged vertex list — outer boundary first, then
// each hole, with bridge vertices duplicated as the cut requires — and
// `outTriangles` indexes into it. Callers that need to map results back to
// their own data use `outSources`, which is parallel to `outVertices` and
// holds the index each merged vertex came from: [0, outer.size()) for the
// outer boundary, and offsets past it for the holes in the order given.
    bool TriangulatePolygonWithHoles2D(const std::vector<Vec2d>& outer,
                                       const std::vector<std::vector<Vec2d>>& holes,
                                       std::vector<Vec2d>& outVertices,
                                       std::vector<uint32_t>& outSources,
                                       std::vector<uint32_t>& outTriangles);

// Triangulate a polygon that lies in (or near) a plane in space. The polygon
// is projected onto its own best-fit plane — by Newell's method, which is
// stable for the near-degenerate faces real files contain — and triangulated
// there. `outTriangles` indexes into `polygon`.
//
// This is what an n-gon in a mesh needs: it has no parameter space of its own,
// only a normal that has to be inferred.
    bool TriangulatePolygon3D(const std::vector<Vec3d>& polygon,
                              std::vector<uint32_t>& outTriangles);

// Newell's normal: the area-weighted normal of a closed 3D polygon, correct
// even when the vertices are not exactly coplanar. Zero-length for a
// degenerate polygon.
    Vec3d NewellNormal(const std::vector<Vec3d>& polygon);

} // namespace ModelStorage
} // namespace UltraCanvas

#endif // ULTRACANVAS_POLYGON_TRIANGULATION_H

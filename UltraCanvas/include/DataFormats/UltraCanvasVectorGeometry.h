// UltraCanvas/include/DataFormats/UltraCanvasVectorGeometry.h
// Polygon geometry over the vector model's path data: boolean operations
// (add, subtract, intersect, exclude) and offsetting (outset / inset
// with round, mitre or bevel joins). Both work on paths flattened to
// polygons, resolve self-intersections, and hand back closed line paths
// with a consistent winding (outer rings one way, holes the other), so a
// non-zero fill draws them as they are.
//
// The booleans are a planar-map clipper: every edge of both inputs is
// split at every crossing, each piece is classified by the winding
// numbers of the two inputs on its two sides, the pieces that separate
// "in the result" from "out" are kept and linked into rings. Offsetting
// unions (or subtracts) a strip along every edge and a join at every
// vertex with the polygon, through the same clipper.
//
// Used by the editing layer's CombineShapes, the renderer's contour
// effect and the XAR writer. Core rather than plugin-owned for the same
// reason as the model itself.
// Version: 1.0.0
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework
#pragma once

#include "DataFormats/UltraCanvasVectorStorage.h"
#include <utility>
#include <vector>

namespace UltraCanvas {
    namespace VectorStorage {

        // A closed ring of points (the last edge runs back to the first
        // point) and a set of them: outer boundaries and holes together,
        // told apart by the fill rule.
        using PolygonRing = std::vector<Point2Dd>;
        using PolygonSet = std::vector<PolygonRing>;

        enum class PathBooleanOp {
            Union,       // in A or in B
            Subtract,    // in A and not in B
            Intersect,   // in A and in B
            Exclude      // in exactly one of them
        };

        // Every subpath of the path flattened and closed (an open subpath
        // is closed by its chord). Degenerate rings are dropped.
        PolygonSet FlattenToPolygons(const PathData& path);
        // The rings as closed line subpaths (Closed set).
        PathData PolygonsToPath(const PolygonSet& polygons);
        // Sum of the rings' signed areas: the enclosed area for a set with
        // consistent winding (holes count negative).
        double PolygonSetArea(const PolygonSet& polygons);
        // Winding number of `point` over the set, and the fill rule's
        // reading of it.
        int WindingNumber(const PolygonSet& polygons, const Point2Dd& point);
        bool PolygonSetContains(const PolygonSet& polygons, FillRule rule, const Point2Dd& point);

        // The boolean of two sets, each read with its own fill rule. The
        // result's rings are wound consistently (the interior on the same
        // side of every edge) and cross nowhere. An empty `b` with Union
        // normalises `a` alone: self-intersections resolved, winding made
        // consistent.
        PolygonSet PolygonBoolean(const PolygonSet& a, FillRule ruleA,
                                  const PolygonSet& b, FillRule ruleB, PathBooleanOp op);

        // The set grown (distance > 0) or shrunk (distance < 0) by
        // `distance`, joins at convex corners as the stroke join says
        // (Round, Miter within `miterLimit`, else Bevel). The input is
        // normalised first, so any winding will do.
        PolygonSet OffsetPolygons(const PolygonSet& polygons, FillRule rule, double distance,
                                  StrokeLineJoin join = StrokeLineJoin::Round, double miterLimit = 4.0);

        // The same over path data: flatten, operate, and hand back a
        // closed line path.
        PathData PathBoolean(const PathData& a, const PathData& b, PathBooleanOp op,
                             FillRule ruleA = FillRule::NonZero, FillRule ruleB = FillRule::NonZero);
        PathData OffsetPath(const PathData& path, double distance,
                            StrokeLineJoin join = StrokeLineJoin::Round, double miterLimit = 4.0,
                            FillRule rule = FillRule::NonZero);
        // `a` cut by `b`: the part inside `b` and the part outside it.
        std::pair<PathData, PathData> SlicePath(const PathData& a, const PathData& b,
                                                FillRule ruleA = FillRule::NonZero,
                                                FillRule ruleB = FillRule::NonZero);

    } // namespace VectorStorage
} // namespace UltraCanvas

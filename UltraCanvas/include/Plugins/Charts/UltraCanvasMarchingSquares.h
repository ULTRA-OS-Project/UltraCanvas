// include/Plugins/Charts/UltraCanvasMarchingSquares.h
// Isoline and isoband extraction from a ContourGrid.
//
// All output is expressed in *fractional grid coordinates*: x is a column
// position in [0, cols-1] and y is a row position in [0, rows-1]. Converting
// those to screen space is the renderer's job, which keeps this module free of
// any layout, row-order or projection concerns and makes it directly testable.
//
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "Plugins/Charts/UltraCanvasContourGrid.h"
#include <vector>

namespace UltraCanvas {

// =============================================================================
// OUTPUT TYPES
// =============================================================================

// A stitched contour line at one level. `closed` marks a loop (a peak or a
// basin); open lines are those that run off the edge of the grid.
    struct ContourPolyline {
        std::vector<Point2Dd> points;   // fractional grid coordinates
        double level = 0.0;
        bool closed = false;

        double Length() const;
    };

// One raw marching-squares segment, tagged with the cell that produced it.
// The 3D surface renderer draws these interleaved with their cells so the
// painter's-algorithm depth ordering also occludes the isolines correctly.
    struct ContourSegment {
        Point2Dd a, b;                  // fractional grid coordinates
        int cellCol = 0;
        int cellRow = 0;
        double level = 0.0;
    };

// =============================================================================
// EXTRACTION
// =============================================================================

// Raw per-cell segments at one level, in no particular order.
    std::vector<ContourSegment> ExtractContourSegments(const ContourGrid& grid, double level);

// Segments stitched into ordered polylines. Segment endpoints are keyed by the
// lattice edge they cross rather than by their floating-point position, so the
// stitching is exact — no epsilon matching and no dangling fragments.
    std::vector<ContourPolyline> ExtractContourPolylines(const ContourGrid& grid, double level);

// Convenience: extract every level in one call. Polylines carry their level.
    std::vector<ContourPolyline> ExtractContourPolylines(const ContourGrid& grid,
                                                         const std::vector<double>& levels);

// =============================================================================
// BAND POLYGONS
// =============================================================================

// The part of cell (c, r) whose value lies within [lo, hi], as a polygon in
// fractional grid coordinates. Computed by clipping the cell quad against the
// two thresholds with Sutherland-Hodgman, interpolating along the edges exactly
// as marching squares does — so band polygons and isolines agree.
//
// Returns an empty polygon when the cell lies entirely outside the band or
// touches a NaN node. Pass -infinity / +infinity for an open-ended band.
    std::vector<Point2Dd> ClipCellToBand(const ContourGrid& grid, int c, int r,
                                         double lo, double hi);

// =============================================================================
// POST-PROCESSING
// =============================================================================

// Chaikin corner cutting: each iteration replaces every segment with its 1/4
// and 3/4 points, converging on a quadratic B-spline. Closed polylines stay
// closed. 1-2 iterations is usually enough to take the staircase off a
// contour extracted from a coarse grid.
    void SmoothContourPolyline(ContourPolyline& poly, int iterations);

// Drop polylines shorter than `minLength` (in grid-cell units). Removes the
// speckle of tiny contours that noisy fields produce around a level.
    void CullShortPolylines(std::vector<ContourPolyline>& polys, double minLength);

} // namespace UltraCanvas

// include/Plugins/Charts/UltraCanvasContourGrid.h
// Scalar-field container, contour level generation, grid smoothing/resampling
// and scattered-point gridding (KDE / binning / IDW / nearest).
//
// Deliberately free of any UI or rendering dependency so the whole contour
// pipeline can be unit tested without a window. The rendering elements
// (UltraCanvasContourChart, UltraCanvasContourSurface3D) build on top of this.
//
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework
#pragma once

#include <vector>
#include <cstddef>
#include <cmath>
#include <limits>

namespace UltraCanvas {

// =============================================================================
// SCALAR GRID
// =============================================================================

// A regular lattice of scalar samples. Values live at the *nodes* of the
// lattice, stored row-major: values[row * cols + col].
//
// The node lattice spans [xMin, xMax] x [yMin, yMax] in data space, with node 0
// exactly on the minimum and node cols-1 / rows-1 exactly on the maximum. Row 0
// is simply "the first data row"; whether that ends up at the top or the bottom
// of the screen is the renderer's decision (see HeatmapRowOrder).
//
// NaN marks a missing sample. Cells touching a NaN node are skipped by the
// contour extractor, which produces clean holes in the field.
    struct ContourGrid {
        std::vector<double> values;
        int cols = 0;
        int rows = 0;

        double xMin = 0.0, xMax = 1.0;
        double yMin = 0.0, yMax = 1.0;

        // A grid needs at least a 2x2 node lattice to contain one cell.
        bool Valid() const {
            return cols >= 2 && rows >= 2 &&
                   values.size() == static_cast<size_t>(cols) * static_cast<size_t>(rows);
        }

        int CellCols() const { return cols > 0 ? cols - 1 : 0; }
        int CellRows() const { return rows > 0 ? rows - 1 : 0; }

        void Resize(int c, int r, double fill = 0.0) {
            cols = c > 0 ? c : 0;
            rows = r > 0 ? r : 0;
            values.assign(static_cast<size_t>(cols) * static_cast<size_t>(rows), fill);
        }

        void SetExtents(double x0, double x1, double y0, double y1) {
            xMin = x0; xMax = x1; yMin = y0; yMax = y1;
        }

        double At(int c, int r) const {
            if (c < 0 || c >= cols || r < 0 || r >= rows) {
                return std::numeric_limits<double>::quiet_NaN();
            }
            return values[static_cast<size_t>(r) * cols + c];
        }

        void Set(int c, int r, double v) {
            if (c < 0 || c >= cols || r < 0 || r >= rows) return;
            values[static_cast<size_t>(r) * cols + c] = v;
        }

        // Data-space coordinate of a node index (accepts fractional indices).
        double XAt(double c) const {
            return (cols < 2) ? xMin : xMin + (xMax - xMin) * c / (cols - 1);
        }
        double YAt(double r) const {
            return (rows < 2) ? yMin : yMin + (yMax - yMin) * r / (rows - 1);
        }

        // Inverse of XAt / YAt: data space -> fractional node index.
        double ColAtX(double x) const {
            if (cols < 2 || xMax == xMin) return 0.0;
            return (x - xMin) / (xMax - xMin) * (cols - 1);
        }
        double RowAtY(double y) const {
            if (rows < 2 || yMax == yMin) return 0.0;
            return (y - yMin) / (yMax - yMin) * (rows - 1);
        }

        // Bilinear sample at a fractional node position. Returns NaN when any
        // contributing node is NaN or the position falls outside the lattice.
        double SampleBilinear(double c, double r) const;

        // Min/max over the finite samples. Returns false when the grid holds no
        // finite value at all.
        bool ValueRange(double& lo, double& hi) const;

        // Sorted copy of the finite samples (used for quantile levels).
        std::vector<double> FiniteSorted() const;
    };

// =============================================================================
// LEVEL GENERATION
// =============================================================================

    enum class ContourLevelMode {
        Count,      // N evenly spaced levels strictly inside the value range
        Step,       // every `step` units, anchored on `origin`
        Explicit,   // caller-supplied list
        Quantile    // levels at data quantiles (equal point count per band)
    };

// Round `raw` up to the nearest "nice" number: 1, 2, 2.5, 5 or 10 x 10^n.
// Keeps auto-generated level labels readable (0.20 rather than 0.1978).
    double NiceContourStep(double raw);

// Build the isoline level list. `explicitLevels` is used only in Explicit mode.
// The result is sorted ascending and de-duplicated; levels equal to the exact
// data minimum or maximum are dropped because they carry no isoline.
    std::vector<double> GenerateContourLevels(const ContourGrid& grid,
                                              ContourLevelMode mode,
                                              int count,
                                              double step,
                                              double origin,
                                              const std::vector<double>& explicitLevels,
                                              bool niceNumbers);

// =============================================================================
// GRID PROCESSING
// =============================================================================

// Separable Gaussian blur, sigma expressed in cells. NaN nodes are excluded
// from the weighted average (and stay NaN), so holes do not bleed into the
// field. sigma <= 0 is a no-op.
    void SmoothContourGrid(ContourGrid& grid, double sigmaCells);

// Bilinear resample onto a new node count, preserving the data extents. Useful
// for smoothing the look of a coarse input before contouring.
    ContourGrid ResampleContourGrid(const ContourGrid& grid, int newCols, int newRows);

// =============================================================================
// SCATTERED-POINT GRIDDING
// =============================================================================

    struct ContourPoint {
        double x = 0.0;
        double y = 0.0;
        double w = 1.0;   // KDE/Binning: point weight. IDW/Nearest: the z value.

        ContourPoint() = default;
        ContourPoint(double px, double py, double pw = 1.0) : x(px), y(py), w(pw) {}
    };

    enum class ContourGridding {
        KDE,        // Gaussian kernel density of the points (weights = mass)
        Binning,    // raw 2D histogram of the points (weights = mass)
        IDW,        // inverse-distance weighting of the point z values
        Nearest     // nearest-neighbour fill from the point z values
    };

    struct ContourGriddingOptions {
        ContourGridding method = ContourGridding::KDE;
        int cols = 96;
        int rows = 96;

        // KDE bandwidth in data-space units. <= 0 selects Scott's rule from the
        // point count and spread.
        double bandwidth = 0.0;

        // Binning: sigma (in cells) of an optional post-blur. IDW: distance
        // exponent. Ignored by the other methods.
        double smoothing = 1.0;
        double idwPower = 2.0;

        // Explicit output extents. When false the extents are taken from the
        // point bounding box, padded by `autoPad` (fraction of the span).
        bool useExplicitExtents = false;
        double xMin = 0.0, xMax = 1.0, yMin = 0.0, yMax = 1.0;
        double autoPad = 0.08;
    };

// Turn an unordered point cloud into a grid. For KDE/Binning the result is a
// density field (points act as mass); for IDW/Nearest the point `w` is treated
// as a measured z value and interpolated.
    ContourGrid BuildContourGridFromPoints(const std::vector<ContourPoint>& points,
                                           const ContourGriddingOptions& options);

// Scott's rule bandwidth for a 2D kernel density estimate.
    double ScottBandwidth(const std::vector<ContourPoint>& points);

} // namespace UltraCanvas

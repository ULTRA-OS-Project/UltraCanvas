// Plugins/Charts/UltraCanvasContourGrid.cpp
// Scalar-field container, level generation, smoothing and point gridding.
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasContourGrid.h"

#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// =============================================================================
// GRID SAMPLING
// =============================================================================

    double ContourGrid::SampleBilinear(double c, double r) const {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        if (cols < 1 || rows < 1) return nan;
        if (!(c >= 0.0) || !(r >= 0.0)) return nan;          // also rejects NaN input
        if (c > cols - 1 || r > rows - 1) return nan;

        int c0 = static_cast<int>(std::floor(c));
        int r0 = static_cast<int>(std::floor(r));
        // Clamp so the +1 neighbour stays inside on the far edge.
        c0 = std::min(c0, std::max(0, cols - 2));
        r0 = std::min(r0, std::max(0, rows - 2));
        int c1 = std::min(c0 + 1, cols - 1);
        int r1 = std::min(r0 + 1, rows - 1);

        double tx = c - c0;
        double ty = r - r0;

        double v00 = At(c0, r0), v10 = At(c1, r0);
        double v01 = At(c0, r1), v11 = At(c1, r1);
        if (std::isnan(v00) || std::isnan(v10) || std::isnan(v01) || std::isnan(v11)) {
            return nan;
        }

        double a = v00 + (v10 - v00) * tx;
        double b = v01 + (v11 - v01) * tx;
        return a + (b - a) * ty;
    }

    bool ContourGrid::ValueRange(double& lo, double& hi) const {
        lo = std::numeric_limits<double>::infinity();
        hi = -std::numeric_limits<double>::infinity();
        for (double v : values) {
            if (!std::isfinite(v)) continue;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        if (!std::isfinite(lo) || !std::isfinite(hi)) {
            lo = 0.0;
            hi = 1.0;
            return false;
        }
        return true;
    }

    std::vector<double> ContourGrid::FiniteSorted() const {
        std::vector<double> out;
        out.reserve(values.size());
        for (double v : values) {
            if (std::isfinite(v)) out.push_back(v);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

// =============================================================================
// LEVEL GENERATION
// =============================================================================

    double NiceContourStep(double raw) {
        if (!(raw > 0.0) || !std::isfinite(raw)) return 1.0;
        double exponent = std::floor(std::log10(raw));
        double pow10 = std::pow(10.0, exponent);
        double mantissa = raw / pow10;             // [1, 10)

        double nice;
        if (mantissa <= 1.0)      nice = 1.0;
        else if (mantissa <= 2.0) nice = 2.0;
        else if (mantissa <= 2.5) nice = 2.5;
        else if (mantissa <= 5.0) nice = 5.0;
        else                      nice = 10.0;

        return nice * pow10;
    }

    namespace {

        void SortUniqueLevels(std::vector<double>& levels, double lo, double hi) {
            std::sort(levels.begin(), levels.end());
            // Drop non-finite entries and anything outside the open range: a level
            // sitting exactly on the data minimum or maximum yields no isoline.
            const double eps = (hi > lo) ? (hi - lo) * 1e-9 : 0.0;
            std::vector<double> kept;
            kept.reserve(levels.size());
            for (double v : levels) {
                if (!std::isfinite(v)) continue;
                if (v <= lo + eps || v >= hi - eps) continue;
                if (!kept.empty() && std::fabs(v - kept.back()) <= eps) continue;
                kept.push_back(v);
            }
            levels.swap(kept);
        }

    } // namespace

    std::vector<double> GenerateContourLevels(const ContourGrid& grid,
                                              ContourLevelMode mode,
                                              int count,
                                              double step,
                                              double origin,
                                              const std::vector<double>& explicitLevels,
                                              bool niceNumbers) {
        std::vector<double> levels;

        double lo = 0.0, hi = 1.0;
        grid.ValueRange(lo, hi);
        if (!(hi > lo)) return levels;

        switch (mode) {
            case ContourLevelMode::Explicit:
                levels = explicitLevels;
                break;

            case ContourLevelMode::Step: {
                double s = step;
                if (!(s > 0.0) || !std::isfinite(s)) return levels;
                // Guard against a step so small it would produce a runaway list.
                if ((hi - lo) / s > 4096.0) return levels;
                double first = std::ceil((lo - origin) / s) * s + origin;
                for (double v = first; v < hi; v += s) {
                    levels.push_back(v);
                }
                break;
            }

            case ContourLevelMode::Quantile: {
                std::vector<double> sorted = grid.FiniteSorted();
                if (sorted.empty() || count < 1) return levels;
                // `count` levels cut the data into count+1 equally populated bands.
                for (int i = 1; i <= count; ++i) {
                    double q = static_cast<double>(i) / (count + 1);
                    double pos = q * (sorted.size() - 1);
                    size_t idx = static_cast<size_t>(pos);
                    double frac = pos - idx;
                    double a = sorted[idx];
                    double b = sorted[std::min(idx + 1, sorted.size() - 1)];
                    levels.push_back(a + (b - a) * frac);
                }
                break;
            }

            case ContourLevelMode::Count:
            default: {
                if (count < 1) return levels;
                if (niceNumbers) {
                    double s = NiceContourStep((hi - lo) / (count + 1));
                    if (!(s > 0.0)) return levels;
                    double first = std::ceil(lo / s) * s;
                    for (double v = first; v < hi; v += s) {
                        levels.push_back(v);
                    }
                } else {
                    for (int i = 1; i <= count; ++i) {
                        levels.push_back(lo + (hi - lo) * i / (count + 1));
                    }
                }
                break;
            }
        }

        SortUniqueLevels(levels, lo, hi);
        return levels;
    }

// =============================================================================
// GRID PROCESSING
// =============================================================================

    void SmoothContourGrid(ContourGrid& grid, double sigmaCells) {
        if (!(sigmaCells > 0.0) || grid.cols < 1 || grid.rows < 1) return;

        int radius = std::max(1, static_cast<int>(std::ceil(sigmaCells * 3.0)));
        // A very wide kernel on a small grid costs a lot and changes nothing.
        radius = std::min(radius, std::max(grid.cols, grid.rows));

        std::vector<double> kernel(2 * radius + 1);
        double twoSigmaSq = 2.0 * sigmaCells * sigmaCells;
        for (int i = -radius; i <= radius; ++i) {
            kernel[i + radius] = std::exp(-(i * i) / twoSigmaSq);
        }

        const int cols = grid.cols, rows = grid.rows;
        std::vector<double> tmp(grid.values.size(), std::numeric_limits<double>::quiet_NaN());

        // Horizontal pass. NaN nodes contribute nothing and stay NaN, so a hole
        // in the field does not bleed into its neighbourhood.
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                if (std::isnan(grid.At(c, r))) continue;
                double sum = 0.0, wsum = 0.0;
                for (int k = -radius; k <= radius; ++k) {
                    int cc = c + k;
                    if (cc < 0 || cc >= cols) continue;
                    double v = grid.values[static_cast<size_t>(r) * cols + cc];
                    if (std::isnan(v)) continue;
                    double w = kernel[k + radius];
                    sum += v * w;
                    wsum += w;
                }
                if (wsum > 0.0) tmp[static_cast<size_t>(r) * cols + c] = sum / wsum;
            }
        }

        // Vertical pass.
        std::vector<double> out(grid.values.size(), std::numeric_limits<double>::quiet_NaN());
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                if (std::isnan(tmp[static_cast<size_t>(r) * cols + c])) continue;
                double sum = 0.0, wsum = 0.0;
                for (int k = -radius; k <= radius; ++k) {
                    int rr = r + k;
                    if (rr < 0 || rr >= rows) continue;
                    double v = tmp[static_cast<size_t>(rr) * cols + c];
                    if (std::isnan(v)) continue;
                    double w = kernel[k + radius];
                    sum += v * w;
                    wsum += w;
                }
                if (wsum > 0.0) out[static_cast<size_t>(r) * cols + c] = sum / wsum;
            }
        }

        grid.values.swap(out);
    }

    ContourGrid ResampleContourGrid(const ContourGrid& grid, int newCols, int newRows) {
        ContourGrid out;
        if (newCols < 2 || newRows < 2 || !grid.Valid()) return out;

        out.Resize(newCols, newRows);
        out.SetExtents(grid.xMin, grid.xMax, grid.yMin, grid.yMax);

        for (int r = 0; r < newRows; ++r) {
            double srcR = static_cast<double>(r) / (newRows - 1) * (grid.rows - 1);
            for (int c = 0; c < newCols; ++c) {
                double srcC = static_cast<double>(c) / (newCols - 1) * (grid.cols - 1);
                out.Set(c, r, grid.SampleBilinear(srcC, srcR));
            }
        }
        return out;
    }

// =============================================================================
// POINT GRIDDING
// =============================================================================

    double ScottBandwidth(const std::vector<ContourPoint>& points) {
        size_t n = points.size();
        if (n < 2) return 0.0;

        double sumX = 0.0, sumY = 0.0;
        for (const auto& p : points) { sumX += p.x; sumY += p.y; }
        double meanX = sumX / n, meanY = sumY / n;

        double varX = 0.0, varY = 0.0;
        for (const auto& p : points) {
            varX += (p.x - meanX) * (p.x - meanX);
            varY += (p.y - meanY) * (p.y - meanY);
        }
        varX /= (n - 1);
        varY /= (n - 1);

        // Scott's rule for a d-dimensional KDE: h = sigma * n^(-1/(d+4)), d = 2.
        double sigma = std::sqrt(std::max(0.0, (varX + varY) * 0.5));
        if (!(sigma > 0.0)) return 0.0;
        return sigma * std::pow(static_cast<double>(n), -1.0 / 6.0);
    }

    namespace {

        // Bilinearly splat one weighted point into the accumulation grid, so a
        // point that falls between nodes contributes to all four of them. This
        // makes the binned density independent of the grid phase.
        void SplatPoint(std::vector<double>& acc, int cols, int rows,
                        double c, double r, double weight) {
            if (c < 0.0 || r < 0.0 || c > cols - 1 || r > rows - 1) return;

            int c0 = std::min(static_cast<int>(std::floor(c)), std::max(0, cols - 2));
            int r0 = std::min(static_cast<int>(std::floor(r)), std::max(0, rows - 2));
            int c1 = std::min(c0 + 1, cols - 1);
            int r1 = std::min(r0 + 1, rows - 1);
            double tx = c - c0, ty = r - r0;

            acc[static_cast<size_t>(r0) * cols + c0] += weight * (1 - tx) * (1 - ty);
            acc[static_cast<size_t>(r0) * cols + c1] += weight * tx       * (1 - ty);
            acc[static_cast<size_t>(r1) * cols + c0] += weight * (1 - tx) * ty;
            acc[static_cast<size_t>(r1) * cols + c1] += weight * tx       * ty;
        }

    } // namespace

    ContourGrid BuildContourGridFromPoints(const std::vector<ContourPoint>& points,
                                           const ContourGriddingOptions& options) {
        ContourGrid grid;
        int cols = std::max(2, options.cols);
        int rows = std::max(2, options.rows);
        grid.Resize(cols, rows, 0.0);

        if (points.empty()) return grid;

        // ---- extents ----
        double x0, x1, y0, y1;
        if (options.useExplicitExtents) {
            x0 = options.xMin; x1 = options.xMax;
            y0 = options.yMin; y1 = options.yMax;
        } else {
            x0 = y0 = std::numeric_limits<double>::infinity();
            x1 = y1 = -std::numeric_limits<double>::infinity();
            for (const auto& p : points) {
                if (!std::isfinite(p.x) || !std::isfinite(p.y)) continue;
                x0 = std::min(x0, p.x); x1 = std::max(x1, p.x);
                y0 = std::min(y0, p.y); y1 = std::max(y1, p.y);
            }
            if (!std::isfinite(x0) || !std::isfinite(y0)) { x0 = y0 = 0.0; x1 = y1 = 1.0; }
            double padX = (x1 - x0) * options.autoPad;
            double padY = (y1 - y0) * options.autoPad;
            if (padX <= 0.0) padX = 0.5;
            if (padY <= 0.0) padY = 0.5;
            x0 -= padX; x1 += padX;
            y0 -= padY; y1 += padY;
        }
        if (!(x1 > x0)) { x1 = x0 + 1.0; }
        if (!(y1 > y0)) { y1 = y0 + 1.0; }
        grid.SetExtents(x0, x1, y0, y1);

        switch (options.method) {
            case ContourGridding::KDE:
            case ContourGridding::Binning: {
                // Splat the point mass onto the lattice, then blur. A Gaussian
                // blur of a splatted histogram is the standard fast equivalent
                // of evaluating a Gaussian kernel at every node, and is O(n +
                // cols*rows*radius) instead of O(n * cols * rows).
                std::vector<double> acc(static_cast<size_t>(cols) * rows, 0.0);
                for (const auto& p : points) {
                    if (!std::isfinite(p.x) || !std::isfinite(p.y)) continue;
                    SplatPoint(acc, cols, rows, grid.ColAtX(p.x), grid.RowAtY(p.y), p.w);
                }
                grid.values.swap(acc);

                double sigmaCells;
                if (options.method == ContourGridding::KDE) {
                    double h = (options.bandwidth > 0.0) ? options.bandwidth
                                                         : ScottBandwidth(points);
                    double cellW = (x1 - x0) / (cols - 1);
                    double cellH = (y1 - y0) / (rows - 1);
                    double cellSize = 0.5 * (cellW + cellH);
                    sigmaCells = (cellSize > 0.0 && h > 0.0) ? (h / cellSize) : 1.0;
                    // Below ~0.6 cells a Gaussian degenerates into the splat
                    // itself and the result looks like raw noise.
                    sigmaCells = std::max(0.6, sigmaCells);
                } else {
                    sigmaCells = std::max(0.0, options.smoothing);
                }
                SmoothContourGrid(grid, sigmaCells);

                // Normalise to a probability-like density: total mass 1 spread
                // over the plotted area, so level values are comparable between
                // datasets of different sizes.
                double totalWeight = 0.0;
                for (const auto& p : points) totalWeight += p.w;
                double cellArea = ((x1 - x0) / (cols - 1)) * ((y1 - y0) / (rows - 1));
                if (totalWeight > 0.0 && cellArea > 0.0) {
                    double inv = 1.0 / (totalWeight * cellArea);
                    for (auto& v : grid.values) v *= inv;
                }
                break;
            }

            case ContourGridding::IDW: {
                double power = (options.idwPower > 0.0) ? options.idwPower : 2.0;
                // A node closer than this to a sample simply takes its value.
                double cellW = (x1 - x0) / (cols - 1);
                double cellH = (y1 - y0) / (rows - 1);
                double snapSq = 1e-6 * (cellW * cellW + cellH * cellH);

                for (int r = 0; r < rows; ++r) {
                    double gy = grid.YAt(r);
                    for (int c = 0; c < cols; ++c) {
                        double gx = grid.XAt(c);
                        double num = 0.0, den = 0.0;
                        double snapped = std::numeric_limits<double>::quiet_NaN();
                        for (const auto& p : points) {
                            double dx = gx - p.x, dy = gy - p.y;
                            double d2 = dx * dx + dy * dy;
                            if (d2 <= snapSq) { snapped = p.w; break; }
                            double w = 1.0 / std::pow(d2, power * 0.5);
                            num += w * p.w;
                            den += w;
                        }
                        grid.Set(c, r, std::isnan(snapped)
                                       ? (den > 0.0 ? num / den
                                                    : std::numeric_limits<double>::quiet_NaN())
                                       : snapped);
                    }
                }
                break;
            }

            case ContourGridding::Nearest:
            default: {
                for (int r = 0; r < rows; ++r) {
                    double gy = grid.YAt(r);
                    for (int c = 0; c < cols; ++c) {
                        double gx = grid.XAt(c);
                        double best = std::numeric_limits<double>::infinity();
                        double value = std::numeric_limits<double>::quiet_NaN();
                        for (const auto& p : points) {
                            double dx = gx - p.x, dy = gy - p.y;
                            double d2 = dx * dx + dy * dy;
                            if (d2 < best) { best = d2; value = p.w; }
                        }
                        grid.Set(c, r, value);
                    }
                }
                break;
            }
        }

        return grid;
    }

} // namespace UltraCanvas

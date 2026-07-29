// Plugins/Charts/UltraCanvasMarchingSquares.cpp
// Isoline / isoband extraction from a ContourGrid.
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasMarchingSquares.h"

#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// =============================================================================
// POLYLINE HELPERS
// =============================================================================

    double ContourPolyline::Length() const {
        if (points.size() < 2) return 0.0;
        double total = 0.0;
        for (size_t i = 1; i < points.size(); ++i) {
            double dx = points[i].x - points[i - 1].x;
            double dy = points[i].y - points[i - 1].y;
            total += std::sqrt(dx * dx + dy * dy);
        }
        if (closed) {
            double dx = points.front().x - points.back().x;
            double dy = points.front().y - points.back().y;
            total += std::sqrt(dx * dx + dy * dy);
        }
        return total;
    }

// =============================================================================
// LATTICE EDGE ADDRESSING
// =============================================================================
//
// Every marching-squares crossing sits on a lattice edge, and each edge is
// shared by at most two cells. Identifying crossings by edge index rather than
// by position makes the stitching exact: two cells computing the same crossing
// arrive at the same integer, so no floating-point tolerance is involved.
//
//   H(c, r): horizontal edge from node (c, r) to (c+1, r)
//            c in [0, cols-2], r in [0, rows-1]
//   V(c, r): vertical edge from node (c, r) to (c, r+1)
//            c in [0, cols-1], r in [0, rows-2]

    namespace {

        struct EdgeSpace {
            int cols = 0, rows = 0;
            int hCount = 0;             // number of horizontal edges

            explicit EdgeSpace(const ContourGrid& g)
                    : cols(g.cols), rows(g.rows), hCount((g.cols - 1) * g.rows) {}

            int Total() const { return hCount + cols * (rows - 1); }
            int H(int c, int r) const { return r * (cols - 1) + c; }
            int V(int c, int r) const { return hCount + r * cols + c; }

            // Position of the crossing on edge `id`, in fractional grid coords.
            Point2Dd Position(int id, const ContourGrid& g, double level) const {
                if (id < hCount) {
                    int c = id % (cols - 1);
                    int r = id / (cols - 1);
                    double v0 = g.At(c, r), v1 = g.At(c + 1, r);
                    double denom = v1 - v0;
                    double t = (denom != 0.0) ? (level - v0) / denom : 0.5;
                    t = std::clamp(t, 0.0, 1.0);
                    return Point2Dd(c + t, static_cast<double>(r));
                }
                int rem = id - hCount;
                int c = rem % cols;
                int r = rem / cols;
                double v0 = g.At(c, r), v1 = g.At(c, r + 1);
                double denom = v1 - v0;
                double t = (denom != 0.0) ? (level - v0) / denom : 0.5;
                t = std::clamp(t, 0.0, 1.0);
                return Point2Dd(static_cast<double>(c), r + t);
            }
        };

        // Which pair(s) of cell edges the contour crosses, per marching-squares
        // case. Edge slots are named for their side of the cell.
        enum CellEdge { EdgeBottom = 0, EdgeRight = 1, EdgeTop = 2, EdgeLeft = 3 };

        // Emit the 0, 1 or 2 segments of one cell as pairs of CellEdge slots.
        // Returns the number of segments written into `out` (max 2 pairs).
        int CellSegments(int caseIndex, double centre, double level, int out[4]) {
            switch (caseIndex) {
                case 0: case 15:
                    return 0;

                // One corner in (or three, which is the same contour mirrored).
                case 1: case 14:
                    out[0] = EdgeLeft;   out[1] = EdgeBottom; return 1;
                case 2: case 13:
                    out[0] = EdgeBottom; out[1] = EdgeRight;  return 1;
                case 4: case 11:
                    out[0] = EdgeRight;  out[1] = EdgeTop;    return 1;
                case 8: case 7:
                    out[0] = EdgeTop;    out[1] = EdgeLeft;   return 1;

                // Two adjacent corners: the contour runs straight across.
                case 3: case 12:
                    out[0] = EdgeLeft;   out[1] = EdgeRight;  return 1;
                case 6: case 9:
                    out[0] = EdgeBottom; out[1] = EdgeTop;    return 1;

                // Saddles. Corners 0 and 2 are above (case 5) or corners 1 and 3
                // are (case 10); the cell centre decides which way the two arcs
                // connect. Getting this wrong is the classic marching-squares
                // artefact - contours that cross each other at a saddle.
                case 5: {
                    if (centre >= level) {
                        // Above-region joins through the middle: isolate the two
                        // below corners (1 and 3) instead.
                        out[0] = EdgeLeft;   out[1] = EdgeTop;
                        out[2] = EdgeBottom; out[3] = EdgeRight;
                    } else {
                        out[0] = EdgeLeft;   out[1] = EdgeBottom;
                        out[2] = EdgeRight;  out[3] = EdgeTop;
                    }
                    return 2;
                }
                case 10: {
                    if (centre >= level) {
                        out[0] = EdgeLeft;   out[1] = EdgeBottom;
                        out[2] = EdgeRight;  out[3] = EdgeTop;
                    } else {
                        out[0] = EdgeLeft;   out[1] = EdgeTop;
                        out[2] = EdgeBottom; out[3] = EdgeRight;
                    }
                    return 2;
                }
                default:
                    return 0;
            }
        }

        // Map a CellEdge slot of cell (c, r) onto its global lattice edge id.
        int SlotToEdge(const EdgeSpace& es, int c, int r, int slot) {
            switch (slot) {
                case EdgeBottom: return es.H(c, r);
                case EdgeRight:  return es.V(c + 1, r);
                case EdgeTop:    return es.H(c, r + 1);
                case EdgeLeft:   return es.V(c, r);
                default:         return -1;
            }
        }

        // Gather one cell's corner values and marching-squares case. Returns
        // false when the cell touches a missing sample.
        bool CellCase(const ContourGrid& g, int c, int r, double level,
                      int& caseIndex, double& centre) {
            double v0 = g.At(c, r);
            double v1 = g.At(c + 1, r);
            double v2 = g.At(c + 1, r + 1);
            double v3 = g.At(c, r + 1);
            if (std::isnan(v0) || std::isnan(v1) || std::isnan(v2) || std::isnan(v3)) {
                return false;
            }
            caseIndex = (v0 >= level ? 1 : 0) | (v1 >= level ? 2 : 0) |
                        (v2 >= level ? 4 : 0) | (v3 >= level ? 8 : 0);
            centre = (v0 + v1 + v2 + v3) * 0.25;
            return true;
        }

    } // namespace

// =============================================================================
// SEGMENT EXTRACTION
// =============================================================================

    std::vector<ContourSegment> ExtractContourSegments(const ContourGrid& grid, double level) {
        std::vector<ContourSegment> segments;
        if (!grid.Valid() || !std::isfinite(level)) return segments;

        EdgeSpace es(grid);
        for (int r = 0; r < grid.rows - 1; ++r) {
            for (int c = 0; c < grid.cols - 1; ++c) {
                int caseIndex = 0;
                double centre = 0.0;
                if (!CellCase(grid, c, r, level, caseIndex, centre)) continue;

                int slots[4];
                int count = CellSegments(caseIndex, centre, level, slots);
                for (int s = 0; s < count; ++s) {
                    int e1 = SlotToEdge(es, c, r, slots[2 * s]);
                    int e2 = SlotToEdge(es, c, r, slots[2 * s + 1]);
                    ContourSegment seg;
                    seg.a = es.Position(e1, grid, level);
                    seg.b = es.Position(e2, grid, level);
                    seg.cellCol = c;
                    seg.cellRow = r;
                    seg.level = level;
                    segments.push_back(seg);
                }
            }
        }
        return segments;
    }

// =============================================================================
// POLYLINE STITCHING
// =============================================================================

    std::vector<ContourPolyline> ExtractContourPolylines(const ContourGrid& grid, double level) {
        std::vector<ContourPolyline> result;
        if (!grid.Valid() || !std::isfinite(level)) return result;

        EdgeSpace es(grid);
        const int totalEdges = es.Total();
        if (totalEdges <= 0) return result;

        // Two link slots per edge: an edge is shared by at most two cells, so a
        // crossing joins at most two segments.
        std::vector<int> links(static_cast<size_t>(totalEdges) * 2, -1);

        auto addLink = [&links](int from, int to) {
            if (links[static_cast<size_t>(from) * 2] < 0) {
                links[static_cast<size_t>(from) * 2] = to;
            } else if (links[static_cast<size_t>(from) * 2 + 1] < 0) {
                links[static_cast<size_t>(from) * 2 + 1] = to;
            }
        };

        for (int r = 0; r < grid.rows - 1; ++r) {
            for (int c = 0; c < grid.cols - 1; ++c) {
                int caseIndex = 0;
                double centre = 0.0;
                if (!CellCase(grid, c, r, level, caseIndex, centre)) continue;

                int slots[4];
                int count = CellSegments(caseIndex, centre, level, slots);
                for (int s = 0; s < count; ++s) {
                    int e1 = SlotToEdge(es, c, r, slots[2 * s]);
                    int e2 = SlotToEdge(es, c, r, slots[2 * s + 1]);
                    if (e1 < 0 || e2 < 0 || e1 == e2) continue;
                    addLink(e1, e2);
                    addLink(e2, e1);
                }
            }
        }

        std::vector<char> visited(static_cast<size_t>(totalEdges), 0);

        auto degree = [&links](int e) {
            return (links[static_cast<size_t>(e) * 2] >= 0 ? 1 : 0) +
                   (links[static_cast<size_t>(e) * 2 + 1] >= 0 ? 1 : 0);
        };

        auto walk = [&](int start) {
            ContourPolyline poly;
            poly.level = level;
            int prev = -1;
            int cur = start;
            while (cur >= 0 && !visited[static_cast<size_t>(cur)]) {
                visited[static_cast<size_t>(cur)] = 1;
                poly.points.push_back(es.Position(cur, grid, level));

                int n0 = links[static_cast<size_t>(cur) * 2];
                int n1 = links[static_cast<size_t>(cur) * 2 + 1];
                int next = (n0 >= 0 && n0 != prev) ? n0
                                                   : ((n1 >= 0 && n1 != prev) ? n1 : -1);
                prev = cur;
                cur = next;
            }
            // Returning to the first edge means the contour closed on itself.
            if (cur == start && poly.points.size() > 2) {
                poly.closed = true;
            }
            return poly;
        };

        // Open contours first, starting from their free ends (degree 1), so a
        // line that runs off the grid is walked end-to-end rather than from
        // somewhere in its middle.
        for (int e = 0; e < totalEdges; ++e) {
            if (visited[static_cast<size_t>(e)] || degree(e) != 1) continue;
            ContourPolyline poly = walk(e);
            if (poly.points.size() >= 2) result.push_back(std::move(poly));
        }

        // Whatever is left with links forms closed loops.
        for (int e = 0; e < totalEdges; ++e) {
            if (visited[static_cast<size_t>(e)] || degree(e) == 0) continue;
            ContourPolyline poly = walk(e);
            if (poly.points.size() >= 2) result.push_back(std::move(poly));
        }

        return result;
    }

    std::vector<ContourPolyline> ExtractContourPolylines(const ContourGrid& grid,
                                                         const std::vector<double>& levels) {
        std::vector<ContourPolyline> all;
        for (double level : levels) {
            std::vector<ContourPolyline> polys = ExtractContourPolylines(grid, level);
            all.insert(all.end(), std::make_move_iterator(polys.begin()),
                       std::make_move_iterator(polys.end()));
        }
        return all;
    }

// =============================================================================
// BAND POLYGONS
// =============================================================================

    namespace {

        struct ValuedVertex {
            Point2Dd p;
            double v = 0.0;
        };

        // Sutherland-Hodgman clip of a valued polygon against a value threshold.
        std::vector<ValuedVertex> ClipHalfSpace(const std::vector<ValuedVertex>& in,
                                                double threshold, bool keepAbove) {
            std::vector<ValuedVertex> out;
            const size_t n = in.size();
            if (n == 0) return out;
            out.reserve(n + 2);

            for (size_t i = 0; i < n; ++i) {
                const ValuedVertex& cur = in[i];
                const ValuedVertex& nxt = in[(i + 1) % n];
                bool curIn = keepAbove ? (cur.v >= threshold) : (cur.v <= threshold);
                bool nxtIn = keepAbove ? (nxt.v >= threshold) : (nxt.v <= threshold);

                if (curIn) out.push_back(cur);
                if (curIn != nxtIn) {
                    double denom = nxt.v - cur.v;
                    double f = (denom != 0.0) ? (threshold - cur.v) / denom : 0.0;
                    f = std::clamp(f, 0.0, 1.0);
                    ValuedVertex crossing;
                    crossing.p = Point2Dd(cur.p.x + (nxt.p.x - cur.p.x) * f,
                                          cur.p.y + (nxt.p.y - cur.p.y) * f);
                    crossing.v = threshold;
                    out.push_back(crossing);
                }
            }
            return out;
        }

    } // namespace

    std::vector<Point2Dd> ClipCellToBand(const ContourGrid& grid, int c, int r,
                                         double lo, double hi) {
        std::vector<Point2Dd> result;
        if (c < 0 || r < 0 || c >= grid.cols - 1 || r >= grid.rows - 1) return result;

        double v0 = grid.At(c, r);
        double v1 = grid.At(c + 1, r);
        double v2 = grid.At(c + 1, r + 1);
        double v3 = grid.At(c, r + 1);
        if (std::isnan(v0) || std::isnan(v1) || std::isnan(v2) || std::isnan(v3)) {
            return result;
        }

        std::vector<ValuedVertex> poly = {
                {Point2Dd(c,     r),     v0},
                {Point2Dd(c + 1, r),     v1},
                {Point2Dd(c + 1, r + 1), v2},
                {Point2Dd(c,     r + 1), v3},
        };

        if (std::isfinite(lo)) poly = ClipHalfSpace(poly, lo, true);
        if (poly.empty()) return result;
        if (std::isfinite(hi)) poly = ClipHalfSpace(poly, hi, false);
        if (poly.size() < 3) return result;

        result.reserve(poly.size());
        for (const auto& v : poly) result.push_back(v.p);
        return result;
    }

// =============================================================================
// POST-PROCESSING
// =============================================================================

    void SmoothContourPolyline(ContourPolyline& poly, int iterations) {
        for (int it = 0; it < iterations; ++it) {
            const size_t n = poly.points.size();
            if (n < 3) return;

            std::vector<Point2Dd> out;
            out.reserve(n * 2);

            auto cut = [](const Point2Dd& a, const Point2Dd& b, double f) {
                return Point2Dd(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f);
            };

            if (poly.closed) {
                for (size_t i = 0; i < n; ++i) {
                    const Point2Dd& a = poly.points[i];
                    const Point2Dd& b = poly.points[(i + 1) % n];
                    out.push_back(cut(a, b, 0.25));
                    out.push_back(cut(a, b, 0.75));
                }
            } else {
                // Endpoints are pinned so an open contour keeps touching the
                // grid boundary it ran off.
                out.push_back(poly.points.front());
                for (size_t i = 0; i + 1 < n; ++i) {
                    const Point2Dd& a = poly.points[i];
                    const Point2Dd& b = poly.points[i + 1];
                    out.push_back(cut(a, b, 0.25));
                    out.push_back(cut(a, b, 0.75));
                }
                out.push_back(poly.points.back());
            }

            poly.points.swap(out);
        }
    }

    void CullShortPolylines(std::vector<ContourPolyline>& polys, double minLength) {
        if (!(minLength > 0.0)) return;
        polys.erase(std::remove_if(polys.begin(), polys.end(),
                                   [minLength](const ContourPolyline& p) {
                                       return p.Length() < minLength;
                                   }),
                    polys.end());
    }

} // namespace UltraCanvas

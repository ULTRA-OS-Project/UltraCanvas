// UltraCanvas/core/DataFormats/UltraCanvasVectorGeometry.cpp
// Polygon booleans and offsetting over the vector model's path data.
// See the header for the approach.
// Version: 1.0.0
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework

#include "DataFormats/UltraCanvasVectorGeometry.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <unordered_map>

namespace UltraCanvas {
    namespace VectorStorage {

        namespace {

            // Coordinates are snapped to this grid (points) so that the
            // pieces of split edges meet exactly.
            constexpr double kGrid = 1e-4;
            constexpr double kEps = 1e-9;

            struct Key {
                int64_t x, y;
                bool operator==(const Key& o) const { return x == o.x && y == o.y; }
                bool operator<(const Key& o) const { return x < o.x || (x == o.x && y < o.y); }
            };
            struct KeyHash {
                size_t operator()(const Key& k) const {
                    return std::hash<int64_t>()(k.x * 1000003LL) ^ std::hash<int64_t>()(k.y);
                }
            };
            Key Snap(const Point2Dd& p) {
                return {static_cast<int64_t>(std::llround(p.x / kGrid)), static_cast<int64_t>(std::llround(p.y / kGrid))};
            }
            Point2Dd Unsnap(const Key& k) { return Point2Dd(k.x * kGrid, k.y * kGrid); }

            double Cross(const Point2Dd& a, const Point2Dd& b) { return a.x * b.y - a.y * b.x; }
            double Dot(const Point2Dd& a, const Point2Dd& b) { return a.x * b.x + a.y * b.y; }
            Point2Dd Sub(const Point2Dd& a, const Point2Dd& b) { return Point2Dd(a.x - b.x, a.y - b.y); }
            Point2Dd Add(const Point2Dd& a, const Point2Dd& b) { return Point2Dd(a.x + b.x, a.y + b.y); }
            Point2Dd Mul(const Point2Dd& a, double k) { return Point2Dd(a.x * k, a.y * k); }
            double Len(const Point2Dd& a) { return std::hypot(a.x, a.y); }

            double RingArea(const PolygonRing& r) {
                double a = 0;
                for (size_t i = 0, n = r.size(); i < n; ++i) {
                    const Point2Dd& p = r[i];
                    const Point2Dd& q = r[(i + 1) % n];
                    a += p.x * q.y - q.x * p.y;
                }
                return a / 2.0;
            }

            // Drops repeated and collinear points; returns false for a
            // ring with no area left.
            bool CleanRing(PolygonRing& r) {
                PolygonRing out;
                for (const auto& p : r) {
                    if (out.empty() || Len(Sub(p, out.back())) > kGrid / 2) out.push_back(p);
                }
                while (out.size() > 1 && Len(Sub(out.front(), out.back())) <= kGrid / 2) out.pop_back();
                if (out.size() < 3) return false;
                PolygonRing cleaned;
                const size_t n = out.size();
                for (size_t i = 0; i < n; ++i) {
                    const Point2Dd& prev = out[(i + n - 1) % n];
                    const Point2Dd& cur = out[i];
                    const Point2Dd& next = out[(i + 1) % n];
                    const Point2Dd a = Sub(cur, prev), b = Sub(next, cur);
                    if (std::fabs(Cross(a, b)) < 1e-12 && Dot(a, b) > 0) continue;   // straight through
                    cleaned.push_back(cur);
                }
                r = cleaned;
                return r.size() >= 3 && std::fabs(RingArea(r)) > 1e-12;
            }

            int WindingOfRing(const PolygonRing& r, const Point2Dd& p) {
                int wn = 0;
                for (size_t i = 0, n = r.size(); i < n; ++i) {
                    const Point2Dd& a = r[i];
                    const Point2Dd& b = r[(i + 1) % n];
                    if (a.y <= p.y) {
                        if (b.y > p.y && Cross(Sub(b, a), Sub(p, a)) > 0) ++wn;
                    } else if (b.y <= p.y && Cross(Sub(b, a), Sub(p, a)) < 0) {
                        --wn;
                    }
                }
                return wn;
            }

            bool Inside(int wn, FillRule rule) {
                return rule == FillRule::EvenOdd ? (wn & 1) != 0 : wn != 0;
            }

            bool ResultInside(bool inA, bool inB, PathBooleanOp op) {
                switch (op) {
                    case PathBooleanOp::Union: return inA || inB;
                    case PathBooleanOp::Subtract: return inA && !inB;
                    case PathBooleanOp::Intersect: return inA && inB;
                    case PathBooleanOp::Exclude: return inA != inB;
                }
                return false;
            }

            struct Edge {
                Point2Dd a, b;
                double minx, maxx, miny, maxy;
            };
            Edge MakeEdge(const Point2Dd& a, const Point2Dd& b) {
                return {a, b, std::min(a.x, b.x), std::max(a.x, b.x), std::min(a.y, b.y), std::max(a.y, b.y)};
            }

            // Parameters along e where f crosses or overlaps it (both
            // exclusive of e's own ends).
            void SplitParams(const Edge& e, const Edge& f, std::vector<double>& ts) {
                const Point2Dd d = Sub(e.b, e.a), g = Sub(f.b, f.a);
                const double denom = Cross(d, g);
                const double lenD2 = Dot(d, d);
                if (lenD2 < kEps) return;
                if (std::fabs(denom) < 1e-12 * std::sqrt(lenD2 * std::max(Dot(g, g), kEps))) {
                    // Parallel: overlapping only if collinear.
                    if (std::fabs(Cross(d, Sub(f.a, e.a))) > 1e-9 * std::sqrt(lenD2)) return;
                    const double ta = Dot(Sub(f.a, e.a), d) / lenD2;
                    const double tb = Dot(Sub(f.b, e.a), d) / lenD2;
                    if (ta > 1e-9 && ta < 1 - 1e-9) ts.push_back(ta);
                    if (tb > 1e-9 && tb < 1 - 1e-9) ts.push_back(tb);
                    return;
                }
                const Point2Dd w = Sub(f.a, e.a);
                const double t = Cross(w, g) / denom;
                const double u = Cross(w, d) / denom;
                if (u < -1e-9 || u > 1 + 1e-9) return;
                if (t > 1e-9 && t < 1 - 1e-9) ts.push_back(t);
            }

            // The planar map of both sets: unique undirected pieces that
            // cross nowhere.
            struct Piece { Point2Dd a, b; };

            std::vector<Piece> PlanarPieces(const PolygonSet& a, const PolygonSet& b) {
                std::vector<Edge> edges;
                auto addSet = [&](const PolygonSet& set) {
                    for (const auto& ring : set) {
                        const size_t n = ring.size();
                        if (n < 2) continue;
                        for (size_t i = 0; i < n; ++i) {
                            const Point2Dd p = Unsnap(Snap(ring[i]));
                            const Point2Dd q = Unsnap(Snap(ring[(i + 1) % n]));
                            if (Len(Sub(p, q)) > kGrid / 2) edges.push_back(MakeEdge(p, q));
                        }
                    }
                };
                addSet(a);
                addSet(b);

                std::vector<size_t> order(edges.size());
                for (size_t i = 0; i < order.size(); ++i) order[i] = i;
                std::sort(order.begin(), order.end(), [&](size_t i, size_t j) { return edges[i].minx < edges[j].minx; });
                std::vector<std::vector<double>> params(edges.size());
                for (size_t oi = 0; oi < order.size(); ++oi) {
                    const size_t i = order[oi];
                    const Edge& e = edges[i];
                    for (size_t oj = oi + 1; oj < order.size(); ++oj) {
                        const size_t j = order[oj];
                        const Edge& f = edges[j];
                        if (f.minx > e.maxx + kGrid) break;
                        if (f.maxy < e.miny - kGrid || f.miny > e.maxy + kGrid) continue;
                        SplitParams(e, f, params[i]);
                        SplitParams(f, e, params[j]);
                    }
                }

                std::map<std::pair<Key, Key>, char> unique;
                std::vector<Piece> pieces;
                for (size_t i = 0; i < edges.size(); ++i) {
                    auto& ts = params[i];
                    ts.push_back(0.0);
                    ts.push_back(1.0);
                    std::sort(ts.begin(), ts.end());
                    const Edge& e = edges[i];
                    Key prev = Snap(e.a);
                    for (double t : ts) {
                        const Point2Dd p(e.a.x + (e.b.x - e.a.x) * t, e.a.y + (e.b.y - e.a.y) * t);
                        const Key k = Snap(p);
                        if (k == prev) continue;
                        const std::pair<Key, Key> id = prev < k ? std::make_pair(prev, k) : std::make_pair(k, prev);
                        if (!unique.count(id)) {
                            unique[id] = 1;
                            pieces.push_back({Unsnap(prev), Unsnap(k)});
                        }
                        prev = k;
                    }
                }
                return pieces;
            }

            // Links directed pieces (interior on the left of each, left =
            // the side (-dy, dx) points to) into rings, taking the
            // leftmost turn at every junction so each ring bounds one face.
            PolygonSet LinkRings(const std::vector<Piece>& directed) {
                std::unordered_map<Key, std::vector<size_t>, KeyHash> outgoing;
                for (size_t i = 0; i < directed.size(); ++i) outgoing[Snap(directed[i].a)].push_back(i);
                std::vector<char> used(directed.size(), 0);
                PolygonSet rings;
                for (size_t start = 0; start < directed.size(); ++start) {
                    if (used[start]) continue;
                    PolygonRing ring;
                    size_t cur = start;
                    const Key startKey = Snap(directed[start].a);
                    while (true) {
                        used[cur] = 1;
                        ring.push_back(directed[cur].a);
                        const Key at = Snap(directed[cur].b);
                        const Point2Dd din = Sub(directed[cur].b, directed[cur].a);
                        auto it = outgoing.find(at);
                        if (it == outgoing.end()) break;
                        double bestAngle = -10;
                        size_t best = SIZE_MAX;
                        for (size_t cand : it->second) {
                            if (used[cand]) continue;
                            const Point2Dd dout = Sub(directed[cand].b, directed[cand].a);
                            double ang = std::atan2(Cross(din, dout), Dot(din, dout));
                            if (ang > M_PI - 1e-9) ang = -M_PI;   // straight back: last resort
                            if (ang > bestAngle) { bestAngle = ang; best = cand; }
                        }
                        if (best == SIZE_MAX) break;
                        if (at == startKey && best != SIZE_MAX) {
                            // Back home: continue only if the leftmost
                            // choice does not close the ring here (a face
                            // touching itself at the start).
                            bool closes = true;
                            for (size_t cand : it->second) if (!used[cand]) { closes = false; break; }
                            if (closes) break;
                        }
                        cur = best;
                    }
                    if (CleanRing(ring)) rings.push_back(ring);
                }
                return rings;
            }

            PolygonSet Normalise(const PolygonSet& set, FillRule rule) {
                return PolygonBoolean(set, rule, PolygonSet(), FillRule::NonZero, PathBooleanOp::Union);
            }

            // Rings whose signed area is positive under the (-dy, dx) =
            // interior convention, as the clipper hands them back for
            // outer boundaries.
            void OrientPositive(PolygonRing& r) {
                if (RingArea(r) < 0) std::reverse(r.begin(), r.end());
            }

            // The strips and joins along a normalised set's boundary, on
            // the side `sign` (+1 outward, -1 inward), each a positively
            // wound simple polygon.
            PolygonSet BoundaryStrips(const PolygonSet& set, double distance, int sign,
                                      StrokeLineJoin join, double miterLimit) {
                PolygonSet pieces;
                const double d = std::fabs(distance);
                for (const auto& ring : set) {
                    const size_t n = ring.size();
                    if (n < 3) continue;
                    // The interior is on the left of each edge, so the
                    // outward normal is the right-hand one (dy, -dx).
                    auto normal = [&](size_t i) {
                        const Point2Dd e = Sub(ring[(i + 1) % n], ring[i]);
                        const double l = Len(e);
                        if (l < kEps) return Point2Dd(0, 0);
                        return Mul(Point2Dd(e.y / l, -e.x / l), sign);
                    };
                    for (size_t i = 0; i < n; ++i) {
                        const Point2Dd& p = ring[i];
                        const Point2Dd& q = ring[(i + 1) % n];
                        const Point2Dd nrm = normal(i);
                        if (Len(nrm) < 0.5) continue;
                        PolygonRing quad = {p, q, Add(q, Mul(nrm, d)), Add(p, Mul(nrm, d))};
                        OrientPositive(quad);
                        pieces.push_back(quad);
                        // The join at q, between this edge's normal and
                        // the next one's, on the side the strips leave a
                        // wedge open (a convex corner for the offset side).
                        const Point2Dd n2 = normal((i + 1) % n);
                        if (Len(n2) < 0.5) continue;
                        const double turn = Cross(nrm, n2);
                        // A gap opens where the boundary turns towards the
                        // interior (a convex corner for an outset, a reflex
                        // one for an inset): the normals turn the same way
                        // as the edges, positively under this convention.
                        if (turn * sign <= 1e-12) continue;
                        const double cosA = std::max(-1.0, std::min(1.0, Dot(nrm, n2)));
                        const double angle = std::acos(cosA);
                        if (angle < 1e-6) continue;
                        PolygonRing wedge;
                        wedge.push_back(q);
                        const Point2Dd a0 = Add(q, Mul(nrm, d));
                        const Point2Dd a1 = Add(q, Mul(n2, d));
                        if (join == StrokeLineJoin::Round) {
                            const int steps = std::max(1, static_cast<int>(std::ceil(angle / (M_PI / 12))));
                            const double from = std::atan2(nrm.y, nrm.x);
                            double delta = std::atan2(n2.y, n2.x) - from;
                            while (delta > M_PI) delta -= 2 * M_PI;
                            while (delta < -M_PI) delta += 2 * M_PI;
                            for (int s = 0; s <= steps; ++s) {
                                const double t = from + delta * s / steps;
                                wedge.push_back(Add(q, Point2Dd(std::cos(t) * d, std::sin(t) * d)));
                            }
                        } else {
                            wedge.push_back(a0);
                            bool mitred = false;
                            if (join == StrokeLineJoin::Miter) {
                                const double half = angle / 2;
                                const double miterLen = 1.0 / std::max(1e-9, std::cos(half));   // in units of d
                                if (miterLen <= miterLimit) {
                                    Point2Dd bis = Add(nrm, n2);
                                    const double bl = Len(bis);
                                    if (bl > kEps) {
                                        bis = Mul(bis, 1.0 / bl);
                                        wedge.push_back(Add(q, Mul(bis, d * miterLen)));
                                        mitred = true;
                                    }
                                }
                            }
                            (void)mitred;
                            wedge.push_back(a1);
                        }
                        OrientPositive(wedge);
                        if (std::fabs(RingArea(wedge)) > 1e-12) pieces.push_back(wedge);
                    }
                }
                return pieces;
            }

        }   // namespace

        PolygonSet FlattenToPolygons(const PathData& path) {
            PolygonSet out;
            for (const FlatSubpath& sub : FlattenPathData(path)) {
                PolygonRing ring = sub.Points;
                if (CleanRing(ring)) out.push_back(ring);
            }
            return out;
        }

        PathData PolygonsToPath(const PolygonSet& polygons) {
            PathData pd;
            for (const auto& ring : polygons) {
                if (ring.size() < 3) continue;
                for (size_t i = 0; i < ring.size(); ++i) {
                    PathCommand c;
                    c.Type = i == 0 ? PathCommandType::MoveTo : PathCommandType::LineTo;
                    c.Parameters = {static_cast<float>(ring[i].x), static_cast<float>(ring[i].y)};
                    pd.commands.push_back(c);
                }
                PathCommand z;
                z.Type = PathCommandType::ClosePath;
                pd.commands.push_back(z);
            }
            pd.Closed = !pd.commands.empty();
            return pd;
        }

        double PolygonSetArea(const PolygonSet& polygons) {
            double a = 0;
            for (const auto& r : polygons) a += RingArea(r);
            return a;
        }

        int WindingNumber(const PolygonSet& polygons, const Point2Dd& point) {
            int wn = 0;
            for (const auto& r : polygons) wn += WindingOfRing(r, point);
            return wn;
        }

        bool PolygonSetContains(const PolygonSet& polygons, FillRule rule, const Point2Dd& point) {
            return Inside(WindingNumber(polygons, point), rule);
        }

        PolygonSet PolygonBoolean(const PolygonSet& a, FillRule ruleA,
                                  const PolygonSet& b, FillRule ruleB, PathBooleanOp op) {
            const std::vector<Piece> pieces = PlanarPieces(a, b);
            std::vector<Piece> kept;
            kept.reserve(pieces.size());
            for (const Piece& p : pieces) {
                const Point2Dd d = Sub(p.b, p.a);
                const double l = Len(d);
                if (l < kEps) continue;
                const Point2Dd left(-d.y / l, d.x / l);
                const Point2Dd mid((p.a.x + p.b.x) / 2, (p.a.y + p.b.y) / 2);
                const double eps = std::min(1e-3, l * 0.1);
                const Point2Dd pl = Add(mid, Mul(left, eps)), pr = Sub(mid, Mul(left, eps));
                const bool inL = ResultInside(Inside(WindingNumber(a, pl), ruleA), Inside(WindingNumber(b, pl), ruleB), op);
                const bool inR = ResultInside(Inside(WindingNumber(a, pr), ruleA), Inside(WindingNumber(b, pr), ruleB), op);
                if (inL == inR) continue;
                if (inL) kept.push_back(p);
                else kept.push_back({p.b, p.a});
            }
            return LinkRings(kept);
        }

        PolygonSet OffsetPolygons(const PolygonSet& polygons, FillRule rule, double distance,
                                  StrokeLineJoin join, double miterLimit) {
            PolygonSet base = Normalise(polygons, rule);
            if (std::fabs(distance) < kGrid) return base;
            const int sign = distance > 0 ? 1 : -1;
            const PolygonSet strips = BoundaryStrips(base, distance, sign, join, miterLimit);
            return PolygonBoolean(base, FillRule::NonZero, strips, FillRule::NonZero,
                                  sign > 0 ? PathBooleanOp::Union : PathBooleanOp::Subtract);
        }

        PathData PathBoolean(const PathData& a, const PathData& b, PathBooleanOp op, FillRule ruleA, FillRule ruleB) {
            return PolygonsToPath(PolygonBoolean(FlattenToPolygons(a), ruleA, FlattenToPolygons(b), ruleB, op));
        }

        PathData OffsetPath(const PathData& path, double distance, StrokeLineJoin join, double miterLimit, FillRule rule) {
            return PolygonsToPath(OffsetPolygons(FlattenToPolygons(path), rule, distance, join, miterLimit));
        }

        std::pair<PathData, PathData> SlicePath(const PathData& a, const PathData& b, FillRule ruleA, FillRule ruleB) {
            const PolygonSet pa = FlattenToPolygons(a), pb = FlattenToPolygons(b);
            return {PolygonsToPath(PolygonBoolean(pa, ruleA, pb, ruleB, PathBooleanOp::Intersect)),
                    PolygonsToPath(PolygonBoolean(pa, ruleA, pb, ruleB, PathBooleanOp::Subtract))};
        }

    } // namespace VectorStorage
} // namespace UltraCanvas

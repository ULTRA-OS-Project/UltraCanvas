// UltraCanvas/Plugins/Vector/UltraCanvasVectorPathOps.h
// Shared path normalisation for the vector format writers: reduces a
// VectorStorage::PathData (any command mix — H/V lines, quadratics, smooth
// variants, SVG arcs) and the basic shapes to absolute move/line/cubic
// segments, the common denominator every vector file format stores.
// Used by UltraCanvasXARConverter.cpp and UltraCanvasEPSConverter.cpp.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasVectorStorage.h"
#include <cmath>
#include <vector>

namespace UltraCanvas {
    namespace VectorConverter {
        namespace PathOps {

            // A path normalised to absolute move/line/cubic segments.
            struct FlatSeg {
                enum Kind { Move, Line, Cubic } kind;
                Point2Dd p[3];
                bool closeAfter = false;
            };

            inline void AppendQuad(std::vector<FlatSeg>& segs, const Point2Dd& from,
                                   const Point2Dd& q, const Point2Dd& end, int& lastDrawIdx) {
                Point2Dd c1(from.x + 2.0 / 3.0 * (q.x - from.x),
                            from.y + 2.0 / 3.0 * (q.y - from.y));
                Point2Dd c2(end.x + 2.0 / 3.0 * (q.x - end.x),
                            end.y + 2.0 / 3.0 * (q.y - end.y));
                segs.push_back({FlatSeg::Cubic, {c1, c2, end}, false});
                lastDrawIdx = static_cast<int>(segs.size()) - 1;
            }

            // SVG endpoint arc -> cubic segments (implementation of the
            // conversion in SVG 1.1 appendix F.6.5).
            inline void AppendArc(std::vector<FlatSeg>& segs, const Point2Dd& from,
                                  double rx, double ry, double rotDeg, bool largeArc,
                                  bool sweep, const Point2Dd& to, int& lastDrawIdx) {
                rx = std::fabs(rx); ry = std::fabs(ry);
                if (rx < 1e-9 || ry < 1e-9 ||
                    (std::fabs(from.x - to.x) < 1e-9 && std::fabs(from.y - to.y) < 1e-9)) {
                    segs.push_back({FlatSeg::Line, {to}, false});
                    lastDrawIdx = static_cast<int>(segs.size()) - 1;
                    return;
                }
                double phi = rotDeg * 3.14159265358979323846 / 180.0;
                double cosP = std::cos(phi), sinP = std::sin(phi);
                double dx = (from.x - to.x) / 2, dy = (from.y - to.y) / 2;
                double x1 = cosP * dx + sinP * dy;
                double y1 = -sinP * dx + cosP * dy;
                double lam = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
                if (lam > 1) { double s = std::sqrt(lam); rx *= s; ry *= s; }
                double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
                double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
                double co = std::sqrt(std::max(0.0, num / den));
                if (largeArc == sweep) co = -co;
                double cxp = co * rx * y1 / ry;
                double cyp = -co * ry * x1 / rx;
                double cx = cosP * cxp - sinP * cyp + (from.x + to.x) / 2;
                double cy = sinP * cxp + cosP * cyp + (from.y + to.y) / 2;

                auto ang = [](double ux, double uy, double vx, double vy) {
                    double dot = ux * vx + uy * vy;
                    double len = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
                    double a = std::acos(std::max(-1.0, std::min(1.0, dot / len)));
                    return (ux * vy - uy * vx < 0) ? -a : a;
                };
                double theta1 = ang(1, 0, (x1 - cxp) / rx, (y1 - cyp) / ry);
                double dTheta = ang((x1 - cxp) / rx, (y1 - cyp) / ry,
                                    (-x1 - cxp) / rx, (-y1 - cyp) / ry);
                const double twoPi = 2 * 3.14159265358979323846;
                if (!sweep && dTheta > 0) dTheta -= twoPi;
                if (sweep && dTheta < 0) dTheta += twoPi;

                int nSegs = static_cast<int>(std::ceil(std::fabs(dTheta) / (twoPi / 4)));
                if (nSegs < 1) nSegs = 1;
                double delta = dTheta / nSegs;
                double t = 4.0 / 3.0 * std::tan(delta / 4);

                auto pointAt = [&](double theta) {
                    double px = rx * std::cos(theta), py = ry * std::sin(theta);
                    return Point2Dd(cosP * px - sinP * py + cx, sinP * px + cosP * py + cy);
                };
                auto derivAt = [&](double theta) {
                    double px = -rx * std::sin(theta), py = ry * std::cos(theta);
                    return Point2Dd(cosP * px - sinP * py, sinP * px + cosP * py);
                };

                double theta = theta1;
                Point2Dd p0 = from;
                for (int i = 0; i < nSegs; ++i) {
                    double theta2 = theta + delta;
                    Point2Dd p3 = pointAt(theta2);
                    Point2Dd d0 = derivAt(theta);
                    Point2Dd d3 = derivAt(theta2);
                    Point2Dd c1(p0.x + t * d0.x, p0.y + t * d0.y);
                    Point2Dd c2(p3.x - t * d3.x, p3.y - t * d3.y);
                    segs.push_back({FlatSeg::Cubic, {c1, c2, p3}, false});
                    lastDrawIdx = static_cast<int>(segs.size()) - 1;
                    p0 = p3;
                    theta = theta2;
                }
            }

            inline std::vector<FlatSeg> NormalizePath(const VectorStorage::PathData& pd) {
                using VectorStorage::PathCommandType;
                std::vector<FlatSeg> segs;
                Point2Dd cur(0, 0), start(0, 0);
                Point2Dd prevCubicCtrl(0, 0), prevQuadCtrl(0, 0);
                bool hadCubic = false, hadQuad = false;
                int lastDrawIdx = -1;

                auto abs2 = [&](double x, double y, bool rel) {
                    return rel ? Point2Dd(cur.x + x, cur.y + y) : Point2Dd(x, y);
                };

                for (const auto& cmd : pd.commands) {
                    const auto& p = cmd.Parameters;
                    bool resetCtrls = true;
                    switch (cmd.Type) {
                        case PathCommandType::MoveTo: {
                            if (p.size() < 2) break;
                            cur = abs2(p[0], p[1], cmd.Relative);
                            start = cur;
                            segs.push_back({FlatSeg::Move, {cur}, false});
                            // Extra pairs are implicit LineTo per SVG.
                            for (size_t i = 2; i + 1 < p.size(); i += 2) {
                                cur = abs2(p[i], p[i + 1], cmd.Relative);
                                segs.push_back({FlatSeg::Line, {cur}, false});
                                lastDrawIdx = static_cast<int>(segs.size()) - 1;
                            }
                            break;
                        }
                        case PathCommandType::LineTo: {
                            for (size_t i = 0; i + 1 < p.size(); i += 2) {
                                cur = abs2(p[i], p[i + 1], cmd.Relative);
                                segs.push_back({FlatSeg::Line, {cur}, false});
                                lastDrawIdx = static_cast<int>(segs.size()) - 1;
                            }
                            break;
                        }
                        case PathCommandType::HorizontalLineTo: {
                            for (double v : p) {
                                cur = Point2Dd(cmd.Relative ? cur.x + v : v, cur.y);
                                segs.push_back({FlatSeg::Line, {cur}, false});
                                lastDrawIdx = static_cast<int>(segs.size()) - 1;
                            }
                            break;
                        }
                        case PathCommandType::VerticalLineTo: {
                            for (double v : p) {
                                cur = Point2Dd(cur.x, cmd.Relative ? cur.y + v : v);
                                segs.push_back({FlatSeg::Line, {cur}, false});
                                lastDrawIdx = static_cast<int>(segs.size()) - 1;
                            }
                            break;
                        }
                        case PathCommandType::CurveTo: {
                            for (size_t i = 0; i + 5 < p.size(); i += 6) {
                                Point2Dd c1 = abs2(p[i], p[i + 1], cmd.Relative);
                                Point2Dd c2 = abs2(p[i + 2], p[i + 3], cmd.Relative);
                                Point2Dd end = abs2(p[i + 4], p[i + 5], cmd.Relative);
                                segs.push_back({FlatSeg::Cubic, {c1, c2, end}, false});
                                lastDrawIdx = static_cast<int>(segs.size()) - 1;
                                prevCubicCtrl = c2;
                                hadCubic = true;
                                cur = end;
                            }
                            resetCtrls = false;
                            break;
                        }
                        case PathCommandType::SmoothCurveTo: {
                            for (size_t i = 0; i + 3 < p.size(); i += 4) {
                                Point2Dd c1 = hadCubic
                                        ? Point2Dd(2 * cur.x - prevCubicCtrl.x,
                                                   2 * cur.y - prevCubicCtrl.y)
                                        : cur;
                                Point2Dd c2 = abs2(p[i], p[i + 1], cmd.Relative);
                                Point2Dd end = abs2(p[i + 2], p[i + 3], cmd.Relative);
                                segs.push_back({FlatSeg::Cubic, {c1, c2, end}, false});
                                lastDrawIdx = static_cast<int>(segs.size()) - 1;
                                prevCubicCtrl = c2;
                                hadCubic = true;
                                cur = end;
                            }
                            resetCtrls = false;
                            break;
                        }
                        case PathCommandType::QuadraticTo: {
                            for (size_t i = 0; i + 3 < p.size(); i += 4) {
                                Point2Dd q = abs2(p[i], p[i + 1], cmd.Relative);
                                Point2Dd end = abs2(p[i + 2], p[i + 3], cmd.Relative);
                                AppendQuad(segs, cur, q, end, lastDrawIdx);
                                prevQuadCtrl = q;
                                hadQuad = true;
                                cur = end;
                            }
                            resetCtrls = false;
                            break;
                        }
                        case PathCommandType::SmoothQuadraticTo: {
                            for (size_t i = 0; i + 1 < p.size(); i += 2) {
                                Point2Dd q = hadQuad
                                        ? Point2Dd(2 * cur.x - prevQuadCtrl.x,
                                                   2 * cur.y - prevQuadCtrl.y)
                                        : cur;
                                Point2Dd end = abs2(p[i], p[i + 1], cmd.Relative);
                                AppendQuad(segs, cur, q, end, lastDrawIdx);
                                prevQuadCtrl = q;
                                hadQuad = true;
                                cur = end;
                            }
                            resetCtrls = false;
                            break;
                        }
                        case PathCommandType::ArcTo: {
                            for (size_t i = 0; i + 6 < p.size(); i += 7) {
                                Point2Dd end = abs2(p[i + 5], p[i + 6], cmd.Relative);
                                AppendArc(segs, cur, p[i], p[i + 1], p[i + 2],
                                          p[i + 3] != 0, p[i + 4] != 0, end, lastDrawIdx);
                                cur = end;
                            }
                            break;
                        }
                        case PathCommandType::ClosePath: {
                            if (lastDrawIdx >= 0) segs[lastDrawIdx].closeAfter = true;
                            cur = start;
                            break;
                        }
                    }
                    if (resetCtrls) { hadCubic = false; hadQuad = false; }
                }

                if (pd.Closed && lastDrawIdx >= 0 && !segs[lastDrawIdx].closeAfter) {
                    segs[lastDrawIdx].closeAfter = true;
                }
                return segs;
            }

            inline std::vector<FlatSeg> RectSegs(const Rect2Dd& r) {
                std::vector<FlatSeg> segs;
                segs.push_back({FlatSeg::Move, {Point2Dd(r.x, r.y)}, false});
                segs.push_back({FlatSeg::Line, {Point2Dd(r.x + r.width, r.y)}, false});
                segs.push_back({FlatSeg::Line, {Point2Dd(r.x + r.width, r.y + r.height)}, false});
                segs.push_back({FlatSeg::Line, {Point2Dd(r.x, r.y + r.height)}, false});
                segs.push_back({FlatSeg::Line, {Point2Dd(r.x, r.y)}, true});
                return segs;
            }

            inline std::vector<FlatSeg> RoundedRectSegs(const Rect2Dd& r,
                                                        double rx, double ry) {
                const double k = 0.5522847498307936;
                double x0 = r.x, y0 = r.y, x1 = r.x + r.width, y1 = r.y + r.height;
                std::vector<FlatSeg> segs;
                segs.push_back({FlatSeg::Move, {Point2Dd(x0 + rx, y0)}, false});
                segs.push_back({FlatSeg::Line, {Point2Dd(x1 - rx, y0)}, false});
                segs.push_back({FlatSeg::Cubic, {Point2Dd(x1 - rx + k * rx, y0),
                                                 Point2Dd(x1, y0 + ry - k * ry),
                                                 Point2Dd(x1, y0 + ry)}, false});
                segs.push_back({FlatSeg::Line, {Point2Dd(x1, y1 - ry)}, false});
                segs.push_back({FlatSeg::Cubic, {Point2Dd(x1, y1 - ry + k * ry),
                                                 Point2Dd(x1 - rx + k * rx, y1),
                                                 Point2Dd(x1 - rx, y1)}, false});
                segs.push_back({FlatSeg::Line, {Point2Dd(x0 + rx, y1)}, false});
                segs.push_back({FlatSeg::Cubic, {Point2Dd(x0 + rx - k * rx, y1),
                                                 Point2Dd(x0, y1 - ry + k * ry),
                                                 Point2Dd(x0, y1 - ry)}, false});
                segs.push_back({FlatSeg::Line, {Point2Dd(x0, y0 + ry)}, false});
                segs.push_back({FlatSeg::Cubic, {Point2Dd(x0, y0 + ry - k * ry),
                                                 Point2Dd(x0 + rx - k * rx, y0),
                                                 Point2Dd(x0 + rx, y0)}, true});
                return segs;
            }

            inline std::vector<FlatSeg> EllipseSegs(const Point2Dd& c,
                                                    double rx, double ry) {
                const double k = 0.5522847498307936;
                std::vector<FlatSeg> segs;
                segs.push_back({FlatSeg::Move, {Point2Dd(c.x + rx, c.y)}, false});
                segs.push_back({FlatSeg::Cubic, {Point2Dd(c.x + rx, c.y + k * ry),
                                                 Point2Dd(c.x + k * rx, c.y + ry),
                                                 Point2Dd(c.x, c.y + ry)}, false});
                segs.push_back({FlatSeg::Cubic, {Point2Dd(c.x - k * rx, c.y + ry),
                                                 Point2Dd(c.x - rx, c.y + k * ry),
                                                 Point2Dd(c.x - rx, c.y)}, false});
                segs.push_back({FlatSeg::Cubic, {Point2Dd(c.x - rx, c.y - k * ry),
                                                 Point2Dd(c.x - k * rx, c.y - ry),
                                                 Point2Dd(c.x, c.y - ry)}, false});
                segs.push_back({FlatSeg::Cubic, {Point2Dd(c.x + k * rx, c.y - ry),
                                                 Point2Dd(c.x + rx, c.y - k * ry),
                                                 Point2Dd(c.x + rx, c.y)}, true});
                return segs;
            }

        } // namespace PathOps
    } // namespace VectorConverter
} // namespace UltraCanvas

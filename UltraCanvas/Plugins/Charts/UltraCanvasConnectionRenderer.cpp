// Plugins/Charts/UltraCanvasConnectionRenderer.cpp
// Reusable connection / ribbon drawing toolkit for radial chart elements.
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasConnectionRenderer.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

    namespace {
        // A curve long enough to matter gets more samples; short ones stay cheap.
        int SegmentsForSpan(double spanLength) {
            int n = static_cast<int>(std::ceil(spanLength / 6.0));
            return std::max(8, std::min(64, n));
        }

        double Length(const Point2Dd& a, const Point2Dd& b) {
            double dx = b.x - a.x, dy = b.y - a.y;
            return std::sqrt(dx * dx + dy * dy);
        }
    }

// =============================================================================
// GEOMETRY
// =============================================================================

namespace ConnectionGeometry {

    float ResolveThickness(const ConnectionStyle& style, double maxValue) {
        if (maxValue <= 0.0 || style.flowValue <= 0.0 ||
            style.proportionalThickness <= 0.0f) {
            return std::max(0.1f, style.baseThickness);
        }
        double normalized = std::min(1.0, style.flowValue / maxValue);
        return std::max(0.1f, style.baseThickness +
                              static_cast<float>(normalized) * style.proportionalThickness);
    }

    void BezierControlPoints(const Point2Dd& start, const Point2Dd& end,
                             const Point2Dd& center, float curvature,
                             Point2Dd& cp1, Point2Dd& cp2) {
        double k = std::max(0.0f, std::min(1.0f, curvature));
        // Pull each control point from its own endpoint toward the centre.
        // k == 1 puts both controls exactly on the centre, which is the
        // classic chord-diagram look.
        cp1.x = start.x + (center.x - start.x) * k;
        cp1.y = start.y + (center.y - start.y) * k;
        cp2.x = end.x   + (center.x - end.x)   * k;
        cp2.y = end.y   + (center.y - end.y)   * k;
    }

    Point2Dd BezierPoint(const Point2Dd& p0, const Point2Dd& p1,
                         const Point2Dd& p2, const Point2Dd& p3, double t) {
        double u = 1.0 - t;
        double uu = u * u, tt = t * t;
        double uuu = uu * u, ttt = tt * t;
        return Point2Dd(
            uuu * p0.x + 3.0 * uu * t * p1.x + 3.0 * u * tt * p2.x + ttt * p3.x,
            uuu * p0.y + 3.0 * uu * t * p1.y + 3.0 * u * tt * p2.y + ttt * p3.y);
    }

    Point2Dd BezierTangent(const Point2Dd& p0, const Point2Dd& p1,
                           const Point2Dd& p2, const Point2Dd& p3, double t) {
        double u = 1.0 - t;
        return Point2Dd(
            3.0 * u * u * (p1.x - p0.x) + 6.0 * u * t * (p2.x - p1.x) + 3.0 * t * t * (p3.x - p2.x),
            3.0 * u * u * (p1.y - p0.y) + 6.0 * u * t * (p2.y - p1.y) + 3.0 * t * t * (p3.y - p2.y));
    }

    void SampleBezier(const Point2Dd& p0, const Point2Dd& p1,
                      const Point2Dd& p2, const Point2Dd& p3,
                      int segments, std::vector<Point2Dd>& out) {
        segments = std::max(1, segments);
        out.reserve(out.size() + segments + 1);
        for (int i = 0; i <= segments; ++i) {
            out.push_back(BezierPoint(p0, p1, p2, p3,
                                      static_cast<double>(i) / segments));
        }
    }

    void SampleArc(const Point2Dd& center, double radius,
                   double startAngle, double endAngle,
                   int segments, std::vector<Point2Dd>& out) {
        segments = std::max(1, segments);
        out.reserve(out.size() + segments + 1);
        for (int i = 0; i <= segments; ++i) {
            double a = startAngle + (endAngle - startAngle) *
                                    (static_cast<double>(i) / segments);
            out.emplace_back(center.x + radius * std::cos(a),
                             center.y + radius * std::sin(a));
        }
    }

    void BuildRibbonOutline(const Point2Dd& p0, const Point2Dd& cp1,
                            const Point2Dd& cp2, const Point2Dd& p3,
                            float startWidth, float endWidth,
                            int segments, std::vector<Point2Dd>& out) {
        segments = std::max(2, segments);
        out.clear();
        out.reserve(2 * (segments + 1));

        std::vector<Point2Dd> lower;
        lower.reserve(segments + 1);

        for (int i = 0; i <= segments; ++i) {
            double t = static_cast<double>(i) / segments;
            Point2Dd spine = BezierPoint(p0, cp1, cp2, p3, t);
            Point2Dd tan   = BezierTangent(p0, cp1, cp2, p3, t);

            double len = std::sqrt(tan.x * tan.x + tan.y * tan.y);
            // A degenerate tangent (coincident control points) leaves the
            // normal undefined; fall back to the chord direction.
            if (len < 1e-9) {
                tan = Point2Dd(p3.x - p0.x, p3.y - p0.y);
                len = std::sqrt(tan.x * tan.x + tan.y * tan.y);
                if (len < 1e-9) { tan = Point2Dd(1.0, 0.0); len = 1.0; }
            }
            Point2Dd normal(-tan.y / len, tan.x / len);

            double half = 0.5 * (startWidth + (endWidth - startWidth) * t);
            out.emplace_back(spine.x + normal.x * half, spine.y + normal.y * half);
            lower.emplace_back(spine.x - normal.x * half, spine.y - normal.y * half);
        }

        for (auto it = lower.rbegin(); it != lower.rend(); ++it) {
            out.push_back(*it);
        }
    }

    void BuildOrganicCurve(const Point2Dd& start, const Point2Dd& end,
                           int segments, float variation,
                           std::vector<Point2Dd>& out) {
        segments = std::max(2, segments);
        out.clear();
        out.reserve(segments + 1);

        double dx = end.x - start.x, dy = end.y - start.y;
        double len = std::sqrt(dx * dx + dy * dy);
        double px = 0.0, py = 0.0;
        if (len > 1e-9) { px = -dy / len; py = dx / len; }

        for (int i = 0; i <= segments; ++i) {
            double t = static_cast<double>(i) / segments;
            // sin(pi*t) tapers the wobble to zero at both endpoints so the
            // curve still meets its anchors exactly.
            double envelope = std::sin(t * M_PI);
            double offset = std::sin(t * M_PI * 3.0) * envelope * variation * len;
            out.emplace_back(start.x + dx * t + px * offset,
                             start.y + dy * t + py * offset);
        }
    }

    void BuildSpringCurve(const Point2Dd& start, const Point2Dd& end,
                          int coils, float amplitude, float phase,
                          std::vector<Point2Dd>& out) {
        coils = std::max(1, coils);
        int total = std::max(8, coils * 16);
        out.clear();
        out.reserve(total + 1);

        double dx = end.x - start.x, dy = end.y - start.y;
        double len = std::sqrt(dx * dx + dy * dy);
        double px = 0.0, py = 0.0;
        if (len > 1e-9) { px = -dy / len; py = dx / len; }

        for (int i = 0; i <= total; ++i) {
            double t = static_cast<double>(i) / total;
            double angle = t * coils * 2.0 * M_PI + phase;
            // Taper the coil so the spring starts and ends on its anchors.
            double offset = std::sin(angle) * amplitude * std::sin(t * M_PI);
            out.emplace_back(start.x + dx * t + px * offset,
                             start.y + dy * t + py * offset);
        }
    }

    void BuildArrowHead(const Point2Dd& tip, const Point2Dd& from,
                        float size, std::vector<Point2Dd>& out) {
        out.clear();
        double dx = tip.x - from.x, dy = tip.y - from.y;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-9) return;
        dx /= len; dy /= len;

        out.reserve(3);
        out.push_back(tip);
        out.emplace_back(tip.x - dx * size - dy * size * 0.5,
                         tip.y - dy * size + dx * size * 0.5);
        out.emplace_back(tip.x - dx * size + dy * size * 0.5,
                         tip.y - dy * size - dx * size * 0.5);
    }

    bool PolygonContains(const std::vector<Point2Dd>& polygon, const Point2Dd& p) {
        if (polygon.size() < 3) return false;
        bool inside = false;
        for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
            const Point2Dd& a = polygon[i];
            const Point2Dd& b = polygon[j];
            if (((a.y > p.y) != (b.y > p.y)) &&
                (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)) {
                inside = !inside;
            }
        }
        return inside;
    }

    double DistanceToPolyline(const std::vector<Point2Dd>& points, const Point2Dd& p) {
        if (points.empty()) return 1e30;
        if (points.size() == 1) return Length(points[0], p);

        double best = 1e30;
        for (size_t i = 0; i + 1 < points.size(); ++i) {
            const Point2Dd& a = points[i];
            const Point2Dd& b = points[i + 1];
            double cx = b.x - a.x, cy = b.y - a.y;
            double lenSq = cx * cx + cy * cy;
            double t = 0.0;
            if (lenSq > 1e-12) {
                t = ((p.x - a.x) * cx + (p.y - a.y) * cy) / lenSq;
                t = std::max(0.0, std::min(1.0, t));
            }
            double qx = a.x + t * cx, qy = a.y + t * cy;
            double dx = p.x - qx, dy = p.y - qy;
            best = std::min(best, std::sqrt(dx * dx + dy * dy));
        }
        return best;
    }

} // namespace ConnectionGeometry

// =============================================================================
// RENDERER
// =============================================================================

    void ConnectionRenderer::FillPolygon(const std::vector<Point2Dd>& pts, const Color& color) {
        if (!ctx || pts.size() < 3 || color.a == 0) return;
        ctx->SetFillPaint(color);
        ctx->FillLinePath(pts);
    }

    void ConnectionRenderer::Draw(const Point2Dd& start, const Point2Dd& end,
                                  const ConnectionStyle& style, double maxValue) {
        if (!ctx || !style.isVisible) return;

        float thickness = ConnectionGeometry::ResolveThickness(style, maxValue);

        ctx->PushState();
        if (style.opacity < 1.0f) {
            ctx->SetAlpha(std::max(0.0f, style.opacity));
        }

        switch (style.drawStyle) {
            case ConnectionDrawStyle::StraightLine:
                DrawStraight(start, end, style, thickness); break;
            case ConnectionDrawStyle::CurvedLine:
                DrawCurved(start, end, style, thickness); break;
            case ConnectionDrawStyle::ThickRibbon:
                DrawRibbon(start, end, style, thickness, false); break;
            case ConnectionDrawStyle::GradientRibbon:
                DrawRibbon(start, end, style, thickness, true); break;
            case ConnectionDrawStyle::DashedLine:
                DrawDashed(start, end, style, thickness); break;
            case ConnectionDrawStyle::ArrowLine:
                DrawArrow(start, end, style, thickness, false); break;
            case ConnectionDrawStyle::DoubleArrow:
                DrawArrow(start, end, style, thickness, true); break;
            case ConnectionDrawStyle::SpringCurve:
                DrawSpring(start, end, style, thickness); break;
            case ConnectionDrawStyle::MultiStrand:
                DrawMultiStrand(start, end, style, maxValue); break;
            case ConnectionDrawStyle::OrganicCurve:
                DrawOrganic(start, end, style, thickness); break;
        }

        if (style.capStyle != ConnectionCapStyle::None) {
            DrawCaps(start, end, style, thickness);
        }

        ctx->PopState();
    }

    void ConnectionRenderer::DrawStraight(const Point2Dd& a, const Point2Dd& b,
                                          const ConnectionStyle& s, float thickness) {
        ctx->SetStrokePaint(s.color);
        ctx->SetStrokeWidth(thickness);
        ctx->DrawLine(a, b);
    }

    void ConnectionRenderer::DrawCurved(const Point2Dd& a, const Point2Dd& b,
                                        const ConnectionStyle& s, float thickness) {
        Point2Dd cp1, cp2;
        ConnectionGeometry::BezierControlPoints(a, b, center, s.curvature, cp1, cp2);
        ctx->SetStrokePaint(s.color);
        ctx->SetStrokeWidth(thickness);
        ctx->DrawBezierCurve(a, cp1, cp2, b);
    }

    void ConnectionRenderer::DrawRibbon(const Point2Dd& a, const Point2Dd& b,
                                        const ConnectionStyle& s, float thickness,
                                        bool gradient) {
        Point2Dd cp1, cp2;
        ConnectionGeometry::BezierControlPoints(a, b, center, s.curvature, cp1, cp2);

        int segments = SegmentsForSpan(Length(a, b));

        if (!gradient && !s.useGradient) {
            std::vector<Point2Dd> outline;
            ConnectionGeometry::BuildRibbonOutline(a, cp1, cp2, b, thickness, thickness,
                                                   segments, outline);
            FillPolygon(outline, s.color);
            if (s.borderWidth > 0.0f && s.borderColor.a > 0) {
                ctx->SetStrokePaint(s.borderColor);
                ctx->SetStrokeWidth(s.borderWidth);
                ctx->DrawLinePath(outline, true);
            }
            return;
        }

        // Gradient ribbon: emit one filled quad per step, each with its own
        // interpolated colour. IRenderContext has no path-space gradient fill,
        // so per-segment solid fills are the portable equivalent.
        std::vector<Point2Dd> spine, upper, lower;
        spine.reserve(segments + 1);
        upper.reserve(segments + 1);
        lower.reserve(segments + 1);

        for (int i = 0; i <= segments; ++i) {
            double t = static_cast<double>(i) / segments;
            Point2Dd p   = ConnectionGeometry::BezierPoint(a, cp1, cp2, b, t);
            Point2Dd tan = ConnectionGeometry::BezierTangent(a, cp1, cp2, b, t);
            double len = std::sqrt(tan.x * tan.x + tan.y * tan.y);
            if (len < 1e-9) { tan = Point2Dd(1.0, 0.0); len = 1.0; }
            Point2Dd n(-tan.y / len, tan.x / len);
            double half = thickness * 0.5;
            upper.emplace_back(p.x + n.x * half, p.y + n.y * half);
            lower.emplace_back(p.x - n.x * half, p.y - n.y * half);
        }

        for (int i = 0; i < segments; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(segments);
            Color c = s.startColor.Blend(s.endColor, t);
            std::vector<Point2Dd> quad = {
                upper[i], upper[i + 1], lower[i + 1], lower[i]
            };
            FillPolygon(quad, c);
        }

        if (s.borderWidth > 0.0f && s.borderColor.a > 0) {
            ctx->SetStrokePaint(s.borderColor);
            ctx->SetStrokeWidth(s.borderWidth);
            ctx->DrawLinePath(upper, false);
            ctx->DrawLinePath(lower, false);
        }
    }

    void ConnectionRenderer::DrawDashed(const Point2Dd& a, const Point2Dd& b,
                                        const ConnectionStyle& s, float thickness) {
        ctx->SetStrokePaint(s.color);
        ctx->SetStrokeWidth(thickness);

        double total = Length(a, b);
        double dash = std::max(1.0f, s.dashLength);
        double gap  = std::max(0.5f, s.gapLength);
        if (total < 1e-6) return;

        double pos = 0.0;
        while (pos < total) {
            double end = std::min(total, pos + dash);
            double t0 = pos / total, t1 = end / total;
            ctx->DrawLine(Point2Dd(a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0),
                          Point2Dd(a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1));
            pos = end + gap;
        }
    }

    void ConnectionRenderer::DrawArrow(const Point2Dd& a, const Point2Dd& b,
                                       const ConnectionStyle& s, float thickness,
                                       bool bothEnds) {
        DrawCurved(a, b, s, thickness);

        Point2Dd cp1, cp2;
        ConnectionGeometry::BezierControlPoints(a, b, center, s.curvature, cp1, cp2);

        float size = std::max(6.0f, thickness * 3.0f);
        std::vector<Point2Dd> head;

        // Aim each head along the curve's tangent at that end, not along the
        // straight chord, so it sits flush with a strongly bowed curve.
        Point2Dd tanEnd = ConnectionGeometry::BezierTangent(a, cp1, cp2, b, 1.0);
        ConnectionGeometry::BuildArrowHead(b, Point2Dd(b.x - tanEnd.x, b.y - tanEnd.y),
                                           size, head);
        FillPolygon(head, s.color);

        if (bothEnds) {
            Point2Dd tanStart = ConnectionGeometry::BezierTangent(a, cp1, cp2, b, 0.0);
            ConnectionGeometry::BuildArrowHead(a, Point2Dd(a.x + tanStart.x, a.y + tanStart.y),
                                               size, head);
            FillPolygon(head, s.color);
        }
    }

    void ConnectionRenderer::DrawSpring(const Point2Dd& a, const Point2Dd& b,
                                        const ConnectionStyle& s, float thickness) {
        std::vector<Point2Dd> pts;
        ConnectionGeometry::BuildSpringCurve(a, b, s.springCoils, s.springAmplitude,
                                             0.0f, pts);
        ctx->SetStrokePaint(s.color);
        ctx->SetStrokeWidth(thickness);
        ctx->DrawLinePath(pts, false);
    }

    void ConnectionRenderer::DrawMultiStrand(const Point2Dd& a, const Point2Dd& b,
                                             const ConnectionStyle& s, double maxValue) {
        int count = std::max(1, s.strandCount);
        float thickness = ConnectionGeometry::ResolveThickness(s, maxValue) /
                          static_cast<float>(count);

        double dx = b.x - a.x, dy = b.y - a.y;
        double len = std::sqrt(dx * dx + dy * dy);
        double px = 0.0, py = 0.0;
        if (len > 1e-9) { px = -dy / len; py = dx / len; }

        for (int i = 0; i < count; ++i) {
            double offset = (i - (count - 1) / 2.0) * s.strandSpacing;
            Point2Dd sa(a.x + px * offset, a.y + py * offset);
            Point2Dd sb(b.x + px * offset, b.y + py * offset);
            DrawCurved(sa, sb, s, std::max(0.5f, thickness));
        }
    }

    void ConnectionRenderer::DrawOrganic(const Point2Dd& a, const Point2Dd& b,
                                         const ConnectionStyle& s, float thickness) {
        std::vector<Point2Dd> pts;
        ConnectionGeometry::BuildOrganicCurve(a, b, SegmentsForSpan(Length(a, b)),
                                              s.organicVariation, pts);
        ctx->SetStrokePaint(s.color);
        ctx->SetStrokeWidth(thickness);
        ctx->DrawLinePath(pts, false);
    }

    void ConnectionRenderer::DrawCaps(const Point2Dd& a, const Point2Dd& b,
                                      const ConnectionStyle& s, float thickness) {
        float size = std::max(3.0f, thickness * 2.0f);
        float half = size * 0.5f;

        switch (s.capStyle) {
            case ConnectionCapStyle::CircleDot:
                ctx->SetFillPaint(s.color);
                ctx->FillCircle(a, half);
                ctx->FillCircle(b, half);
                break;

            case ConnectionCapStyle::SquareDot:
                ctx->SetFillPaint(s.color);
                ctx->FillRectangle(Rect2Dd(a.x - half, a.y - half, size, size));
                ctx->FillRectangle(Rect2Dd(b.x - half, b.y - half, size, size));
                break;

            case ConnectionCapStyle::DiamondDot: {
                for (const Point2Dd& p : {a, b}) {
                    std::vector<Point2Dd> diamond = {
                        Point2Dd(p.x, p.y - half), Point2Dd(p.x + half, p.y),
                        Point2Dd(p.x, p.y + half), Point2Dd(p.x - half, p.y)
                    };
                    FillPolygon(diamond, s.color);
                }
                break;
            }

            case ConnectionCapStyle::ArrowHead: {
                std::vector<Point2Dd> head;
                ConnectionGeometry::BuildArrowHead(b, a, size, head);
                FillPolygon(head, s.color);
                break;
            }

            case ConnectionCapStyle::None:
            default:
                break;
        }
    }

    void ConnectionRenderer::BuildArcRibbonOutline(
        const Point2Dd& circleCenter, double radius,
        double sourceStartAngle, double sourceEndAngle,
        double targetStartAngle, double targetEndAngle,
        float curvature, std::vector<Point2Dd>& out) {

        out.clear();

        auto arcPoint = [&](double angle) {
            return Point2Dd(circleCenter.x + radius * std::cos(angle),
                            circleCenter.y + radius * std::sin(angle));
        };

        int srcSteps = std::max(2, static_cast<int>(
            std::ceil(std::fabs(sourceEndAngle - sourceStartAngle) / (M_PI / 90.0))));
        int tgtSteps = std::max(2, static_cast<int>(
            std::ceil(std::fabs(targetEndAngle - targetStartAngle) / (M_PI / 90.0))));

        // Source arc.
        ConnectionGeometry::SampleArc(circleCenter, radius,
                                      sourceStartAngle, sourceEndAngle, srcSteps, out);

        // Bow across the disc to the target arc's leading edge.
        {
            Point2Dd from = arcPoint(sourceEndAngle);
            Point2Dd to   = arcPoint(targetStartAngle);
            Point2Dd cp1, cp2;
            ConnectionGeometry::BezierControlPoints(from, to, circleCenter, curvature, cp1, cp2);
            std::vector<Point2Dd> curve;
            ConnectionGeometry::SampleBezier(from, cp1, cp2, to, 28, curve);
            // Drop the duplicated first sample; it is already the arc's last point.
            out.insert(out.end(), curve.begin() + 1, curve.end());
        }

        // Target arc.
        {
            std::vector<Point2Dd> arc;
            ConnectionGeometry::SampleArc(circleCenter, radius,
                                          targetStartAngle, targetEndAngle, tgtSteps, arc);
            out.insert(out.end(), arc.begin() + 1, arc.end());
        }

        // Bow back to where we started.
        {
            Point2Dd from = arcPoint(targetEndAngle);
            Point2Dd to   = arcPoint(sourceStartAngle);
            Point2Dd cp1, cp2;
            ConnectionGeometry::BezierControlPoints(from, to, circleCenter, curvature, cp1, cp2);
            std::vector<Point2Dd> curve;
            ConnectionGeometry::SampleBezier(from, cp1, cp2, to, 28, curve);
            out.insert(out.end(), curve.begin() + 1, curve.end());
        }
    }

    void ConnectionRenderer::DrawArcRibbon(
        const Point2Dd& circleCenter, double radius,
        double sourceStartAngle, double sourceEndAngle,
        double targetStartAngle, double targetEndAngle,
        float curvature, const Color& fill,
        const Color& borderColor, float borderWidth) {

        if (!ctx) return;

        std::vector<Point2Dd> outline;
        BuildArcRibbonOutline(circleCenter, radius,
                              sourceStartAngle, sourceEndAngle,
                              targetStartAngle, targetEndAngle,
                              curvature, outline);
        if (outline.size() < 3) return;

        FillPolygon(outline, fill);

        if (borderWidth > 0.0f && borderColor.a > 0) {
            ctx->SetStrokePaint(borderColor);
            ctx->SetStrokeWidth(borderWidth);
            ctx->DrawLinePath(outline, true);
        }
    }

} // namespace UltraCanvas

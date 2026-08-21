// Plugins/Charts/Engine/UltraCanvasChartHighlights.cpp
// Geometry for the chart engine's highlight layer: the covariance ellipse
// behind the 50%/95%-around-grouped-dots convention, convex hulls, centroid
// padding and Chaikin smoothing for cluster blobs. Pure math - no UI
// dependency - so Tests/ChartEngineTest exercises it directly.
//
// Version: 1.0.0
// Last Modified: 2026-08-20
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/UltraCanvasChartHighlights.h"

#include <algorithm>
#include <cmath>

namespace UltraCanvas {

ChartEllipseSpec ComputeConfidenceEllipse(const std::vector<Point2Dd>& points,
                                          double confidence) {
    ChartEllipseSpec spec;
    if (points.size() < 3) return spec;

    double mx = 0.0, my = 0.0;
    for (const Point2Dd& p : points) {
        mx += p.x;
        my += p.y;
    }
    const double n = static_cast<double>(points.size());
    mx /= n;
    my /= n;

    double sxx = 0.0, syy = 0.0, sxy = 0.0;
    for (const Point2Dd& p : points) {
        const double dx = p.x - mx, dy = p.y - my;
        sxx += dx * dx;
        syy += dy * dy;
        sxy += dx * dy;
    }
    sxx /= n - 1.0;
    syy /= n - 1.0;
    sxy /= n - 1.0;

    // Eigen decomposition of the 2x2 covariance matrix.
    const double trace = (sxx + syy) / 2.0;
    const double det = std::sqrt(std::max(
        0.0, ((sxx - syy) / 2.0) * ((sxx - syy) / 2.0) + sxy * sxy));
    const double l1 = std::max(0.0, trace + det);   // major variance
    const double l2 = std::max(0.0, trace - det);   // minor variance

    // Chi-square quantile with 2 degrees of freedom: r^2 = -2 ln(1 - p).
    const double p = std::clamp(confidence, 0.001, 0.999);
    const double k = std::sqrt(-2.0 * std::log(1.0 - p));

    spec.center = Point2Dd(mx, my);
    spec.rx = k * std::sqrt(l1);
    spec.ry = k * std::sqrt(l2);
    spec.rotationRadians = 0.5 * std::atan2(2.0 * sxy, sxx - syy);
    spec.valid = spec.rx > 0.0;
    return spec;
}

static double Cross(const Point2Dd& o, const Point2Dd& a, const Point2Dd& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

std::vector<Point2Dd> ComputeConvexHull(std::vector<Point2Dd> points) {
    std::sort(points.begin(), points.end(),
              [](const Point2Dd& a, const Point2Dd& b) {
                  return a.x < b.x || (a.x == b.x && a.y < b.y);
              });
    points.erase(std::unique(points.begin(), points.end(),
                             [](const Point2Dd& a, const Point2Dd& b) {
                                 return a.x == b.x && a.y == b.y;
                             }),
                 points.end());
    const size_t n = points.size();
    if (n < 3) return points;

    // Monotone chain, counter-clockwise.
    std::vector<Point2Dd> hull(2 * n);
    size_t k = 0;
    for (size_t i = 0; i < n; ++i) {                       // lower
        while (k >= 2 && Cross(hull[k - 2], hull[k - 1], points[i]) <= 0.0) --k;
        hull[k++] = points[i];
    }
    for (size_t i = n - 1, t = k + 1; i-- > 0;) {          // upper
        while (k >= t && Cross(hull[k - 2], hull[k - 1], points[i]) <= 0.0) --k;
        hull[k++] = points[i];
    }
    hull.resize(k - 1);                                    // last == first
    return hull;
}

std::vector<Point2Dd> ExpandPolygon(const std::vector<Point2Dd>& polygon,
                                    double padding) {
    if (polygon.size() < 3 || padding == 0.0) return polygon;

    double cx = 0.0, cy = 0.0;
    for (const Point2Dd& p : polygon) {
        cx += p.x;
        cy += p.y;
    }
    cx /= polygon.size();
    cy /= polygon.size();

    std::vector<Point2Dd> expanded;
    expanded.reserve(polygon.size());
    for (const Point2Dd& p : polygon) {
        const double dx = p.x - cx, dy = p.y - cy;
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-9) {
            expanded.push_back(p);
        } else {
            const double f = (len + padding) / len;
            expanded.emplace_back(cx + dx * f, cy + dy * f);
        }
    }
    return expanded;
}

std::vector<Point2Dd> SmoothPolygonChaikin(const std::vector<Point2Dd>& polygon,
                                           int iterations) {
    std::vector<Point2Dd> result = polygon;
    if (polygon.size() < 3) return result;
    for (int it = 0; it < iterations; ++it) {
        std::vector<Point2Dd> next;
        next.reserve(result.size() * 2);
        for (size_t i = 0; i < result.size(); ++i) {
            const Point2Dd& a = result[i];
            const Point2Dd& b = result[(i + 1) % result.size()];
            next.emplace_back(0.75 * a.x + 0.25 * b.x, 0.75 * a.y + 0.25 * b.y);
            next.emplace_back(0.25 * a.x + 0.75 * b.x, 0.25 * a.y + 0.75 * b.y);
        }
        result = std::move(next);
    }
    return result;
}

} // namespace UltraCanvas

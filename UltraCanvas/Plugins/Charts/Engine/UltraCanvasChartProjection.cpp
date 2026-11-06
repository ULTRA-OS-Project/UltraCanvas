// Plugins/Charts/Engine/UltraCanvasChartProjection.cpp
// Chart space projections
// Version: 1.1.0 - the ternary projection
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/UltraCanvasChartProjection.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// =============================================================================
// VERTICAL
// =============================================================================

Point2Dd ChartVerticalProjection::ToScreen(const ChartNormalizedPoint& p) const {
    return Point2Dd(plotArea.x + p.u * plotArea.width,
                    plotArea.y + plotArea.height - p.v * plotArea.height);
}

ChartNormalizedPoint ChartVerticalProjection::ToNormalized(const Point2Dd& screen) const {
    const double w = (plotArea.width  != 0.0) ? plotArea.width  : 1.0;
    const double h = (plotArea.height != 0.0) ? plotArea.height : 1.0;
    return ChartNormalizedPoint((screen.x - plotArea.x) / w,
                                (plotArea.y + plotArea.height - screen.y) / h);
}

// =============================================================================
// HORIZONTAL
// =============================================================================

Point2Dd ChartHorizontalProjection::ToScreen(const ChartNormalizedPoint& p) const {
    return Point2Dd(plotArea.x + p.v * plotArea.width,
                    plotArea.y + p.u * plotArea.height);
}

ChartNormalizedPoint ChartHorizontalProjection::ToNormalized(const Point2Dd& screen) const {
    const double w = (plotArea.width  != 0.0) ? plotArea.width  : 1.0;
    const double h = (plotArea.height != 0.0) ? plotArea.height : 1.0;
    return ChartNormalizedPoint((screen.y - plotArea.y) / h,
                                (screen.x - plotArea.x) / w);
}

// =============================================================================
// POLAR
// =============================================================================

void ChartPolarProjection::SetAngles(double startAngleDegrees, double sweepDegrees,
                                     bool cw) {
    startAngle = startAngleDegrees;
    sweep = sweepDegrees;
    clockwise = cw;
}

void ChartPolarProjection::SetInnerRadiusFraction(double fraction) {
    innerFraction = std::clamp(fraction, 0.0, 0.95);
}

Point2Dd ChartPolarProjection::Center() const {
    return Point2Dd(plotArea.x + plotArea.width * 0.5,
                    plotArea.y + plotArea.height * 0.5);
}

double ChartPolarProjection::OuterRadius() const {
    return std::min(plotArea.width, plotArea.height) * 0.5;
}

Point2Dd ChartPolarProjection::ToScreen(const ChartNormalizedPoint& p) const {
    const double outer = OuterRadius();
    const double inner = outer * innerFraction;
    const double radius = inner + p.v * (outer - inner);
    const double degrees = startAngle + (clockwise ? 1.0 : -1.0) * p.u * sweep;
    const double radians = degrees * (M_PI / 180.0);
    const Point2Dd c = Center();
    return Point2Dd(c.x + std::cos(radians) * radius,
                    c.y + std::sin(radians) * radius);
}

ChartNormalizedPoint ChartPolarProjection::ToNormalized(const Point2Dd& screen) const {
    const Point2Dd c = Center();
    const double dx = screen.x - c.x;
    const double dy = screen.y - c.y;
    const double radius = std::sqrt(dx * dx + dy * dy);

    const double outer = OuterRadius();
    const double inner = outer * innerFraction;
    const double span = (outer - inner);
    const double v = (std::abs(span) < 1e-12) ? 0.0 : (radius - inner) / span;

    double degrees = std::atan2(dy, dx) * (180.0 / M_PI);
    double delta = (degrees - startAngle) * (clockwise ? 1.0 : -1.0);
    // Bring the angle into [0, 360) before dividing, so a point just before the
    // start angle reads as almost a full turn rather than a negative one.
    while (delta < 0.0) delta += 360.0;
    while (delta >= 360.0) delta -= 360.0;
    const double u = (std::abs(sweep) < 1e-12) ? 0.0 : delta / sweep;

    return ChartNormalizedPoint(u, v);
}

// =============================================================================
// TERNARY
// =============================================================================

double ChartTernaryProjection::SideLength() const {
    // The widest equilateral triangle inside the plot area: limited by the
    // width, or by the height through the triangle's height (side * sqrt(3)/2).
    return std::max(0.0, std::min(plotArea.width, plotArea.height * 2.0 / std::sqrt(3.0)));
}

Point2Dd ChartTernaryProjection::Corner(int index) const {
    const double side = SideLength();
    const double height = side * std::sqrt(3.0) * 0.5;
    const double cx = plotArea.x + plotArea.width * 0.5;
    // Centred in the plot area both ways.
    const double top = plotArea.y + (plotArea.height - height) * 0.5;
    const double bottom = top + height;
    switch (index) {
        case 0:  return Point2Dd(cx, apexDown ? bottom : top);
        case 1:  return Point2Dd(cx - side * 0.5, apexDown ? top : bottom);
        default: return Point2Dd(cx + side * 0.5, apexDown ? top : bottom);
    }
}

Point2Dd ChartTernaryProjection::ToScreen(const ChartNormalizedPoint& p) const {
    const Point2Dd a = Corner(0), b = Corner(1), c = Corner(2);
    return Point2Dd(a.x + p.u * (b.x - a.x) + p.v * (c.x - a.x),
                    a.y + p.u * (b.y - a.y) + p.v * (c.y - a.y));
}

ChartNormalizedPoint ChartTernaryProjection::ToNormalized(const Point2Dd& screen) const {
    // Solve screen - A = u (B - A) + v (C - A) for u and v.
    const Point2Dd a = Corner(0), b = Corner(1), c = Corner(2);
    const double e1x = b.x - a.x, e1y = b.y - a.y;
    const double e2x = c.x - a.x, e2y = c.y - a.y;
    const double det = e1x * e2y - e1y * e2x;
    if (std::abs(det) < 1e-12) return ChartNormalizedPoint(0.0, 0.0);
    const double px = screen.x - a.x, py = screen.y - a.y;
    return ChartNormalizedPoint((px * e2y - py * e2x) / det, (e1x * py - e1y * px) / det);
}

// =============================================================================
// FACTORY
// =============================================================================

std::unique_ptr<IChartProjection> CreateChartProjection(ChartProjectionKind kind) {
    switch (kind) {
        case ChartProjectionKind::Horizontal:
            return std::make_unique<ChartHorizontalProjection>();
        case ChartProjectionKind::Polar:
            return std::make_unique<ChartPolarProjection>();
        case ChartProjectionKind::Ternary:
            return std::make_unique<ChartTernaryProjection>();
        case ChartProjectionKind::Space3D:
            // Arrives with the camera work; the vertical projection keeps a
            // caller that asks early rendering something sensible.
            return std::make_unique<ChartVerticalProjection>();
        case ChartProjectionKind::Vertical:
        default:
            return std::make_unique<ChartVerticalProjection>();
    }
}

} // namespace UltraCanvas

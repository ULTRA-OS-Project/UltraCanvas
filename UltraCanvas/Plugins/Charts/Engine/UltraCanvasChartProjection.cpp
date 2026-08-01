// Plugins/Charts/Engine/UltraCanvasChartProjection.cpp
// Chart space projections
// Version: 1.0.0
// Last Modified: 2026-08-01
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
// FACTORY
// =============================================================================

std::unique_ptr<IChartProjection> CreateChartProjection(ChartProjectionKind kind) {
    switch (kind) {
        case ChartProjectionKind::Horizontal:
            return std::make_unique<ChartHorizontalProjection>();
        case ChartProjectionKind::Polar:
            return std::make_unique<ChartPolarProjection>();
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

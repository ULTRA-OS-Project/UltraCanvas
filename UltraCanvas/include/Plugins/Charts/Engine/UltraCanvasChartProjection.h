// include/Plugins/Charts/Engine/UltraCanvasChartProjection.h
// Chart space projections: vertical, horizontal and polar.
//
// A projection maps normalised chart space - (u, v) both on [0,1], u along the
// domain, v along the value - onto screen coordinates. Series drawing code is
// written once against normalised space and works in every projection, so a
// horizontal bar chart is the vertical one with a different projection rather
// than a second class.
//
// Free of UI dependencies; the 3-D projection arrives with the camera work in a
// later phase.
//
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartTypes.h"
#include <memory>

namespace UltraCanvas {

enum class ChartProjectionKind { Vertical, Horizontal, Polar, Space3D };

// A point in normalised chart space. Both components run 0..1 across the plot;
// values outside that are legal and land outside the plot area.
struct ChartNormalizedPoint {
    double u = 0.0;    // along the domain / category / axis-index direction
    double v = 0.0;    // along the value direction

    ChartNormalizedPoint() = default;
    ChartNormalizedPoint(double uu, double vv) : u(uu), v(vv) {}
};

class IChartProjection {
public:
    virtual ~IChartProjection() = default;

    virtual ChartProjectionKind Kind() const = 0;

    void SetPlotArea(const Rect2Dd& area) { plotArea = area; }
    const Rect2Dd& PlotArea() const { return plotArea; }

    virtual Point2Dd ToScreen(const ChartNormalizedPoint& p) const = 0;
    virtual ChartNormalizedPoint ToNormalized(const Point2Dd& screen) const = 0;

    // Painter's-algorithm sort key. Flat for the 2-D projections.
    virtual double DepthOf(const ChartNormalizedPoint&) const { return 0.0; }

protected:
    Rect2Dd plotArea{0.0, 0.0, 1.0, 1.0};
};

// Domain left-to-right, value bottom-to-top. Line, bar, area, scatter, and
// parallel coordinates with vertical axes.
class ChartVerticalProjection : public IChartProjection {
public:
    ChartProjectionKind Kind() const override { return ChartProjectionKind::Vertical; }
    Point2Dd ToScreen(const ChartNormalizedPoint& p) const override;
    ChartNormalizedPoint ToNormalized(const Point2Dd& screen) const override;
};

// The same content transposed: domain top-to-bottom, value left-to-right.
// Horizontal bars, dumbbell rows, population pyramids, parallel coordinates
// with the axes as rows.
class ChartHorizontalProjection : public IChartProjection {
public:
    ChartProjectionKind Kind() const override { return ChartProjectionKind::Horizontal; }
    Point2Dd ToScreen(const ChartNormalizedPoint& p) const override;
    ChartNormalizedPoint ToNormalized(const Point2Dd& screen) const override;
};

// Domain around the circle, value from the inner radius outward. Radar, polar,
// sunburst, radial bar, pie and the circular progress charts share this instead
// of each carrying their own angle maths.
class ChartPolarProjection : public IChartProjection {
public:
    ChartProjectionKind Kind() const override { return ChartProjectionKind::Polar; }

    // startAngle in degrees, 0 = 3 o'clock, negative = anticlockwise from there.
    // The radar convention (-90, clockwise) is the default.
    void SetAngles(double startAngleDegrees, double sweepDegrees, bool clockwise);
    // Fraction of the outer radius left empty in the middle (donuts, radial bars).
    void SetInnerRadiusFraction(double fraction);

    double StartAngle() const { return startAngle; }
    double Sweep() const { return sweep; }
    bool IsClockwise() const { return clockwise; }
    Point2Dd Center() const;
    double OuterRadius() const;

    Point2Dd ToScreen(const ChartNormalizedPoint& p) const override;
    ChartNormalizedPoint ToNormalized(const Point2Dd& screen) const override;

private:
    double startAngle = -90.0;
    double sweep = 360.0;
    bool clockwise = true;
    double innerFraction = 0.0;
};

std::unique_ptr<IChartProjection> CreateChartProjection(ChartProjectionKind kind);

} // namespace UltraCanvas

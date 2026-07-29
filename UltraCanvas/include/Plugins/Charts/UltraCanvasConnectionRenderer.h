// include/Plugins/Charts/UltraCanvasConnectionRenderer.h
// Reusable connection / ribbon drawing toolkit for radial chart elements.
// Provides the curve geometry (bezier, organic, spring, variable-width ribbon
// outlines) plus a renderer that draws them through IRenderContext.
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include <vector>

// X11 defines `None` as a macro that clobbers our enumerators.
#ifdef None
#undef None
#endif

namespace UltraCanvas {

// =============================================================================
// CONFIGURATION ENUMS
// =============================================================================

    enum class ConnectionDrawStyle {
        StraightLine,      // Simple straight line
        CurvedLine,        // Cubic bezier pulled toward the centre
        ThickRibbon,       // Filled ribbon with proportional thickness
        DashedLine,        // Dashed straight line
        ArrowLine,         // Curve with an arrow head at the target
        DoubleArrow,       // Curve with arrow heads at both ends
        SpringCurve,       // Coiled spring
        GradientRibbon,    // Ribbon filled with a start->end colour ramp
        MultiStrand,       // Several thin parallel curves
        OrganicCurve       // Natural, slightly irregular curve
    };

    enum class ConnectionCapStyle {
        None,              // No end decoration
        CircleDot,
        SquareDot,
        DiamondDot,
        ArrowHead          // Arrow head at the target end only
    };

// =============================================================================
// CONNECTION STYLE
// =============================================================================

    // Purely visual description of one connection. Endpoint identity (which
    // ring/cell/category a connection joins) belongs to the owning chart, so
    // this struct stays reusable across chart types.
    struct ConnectionStyle {
        ConnectionDrawStyle drawStyle = ConnectionDrawStyle::CurvedLine;
        ConnectionCapStyle  capStyle  = ConnectionCapStyle::None;

        Color color      = Color(128, 128, 128, 255);
        Color startColor = Color(128, 128, 128, 255);
        Color endColor   = Color(128, 128, 128, 255);
        bool  useGradient = false;

        // Final thickness = baseThickness + (flowValue / maxValue) * proportionalThickness
        float  baseThickness         = 2.0f;
        float  proportionalThickness = 0.0f;
        double flowValue             = 0.0;

        float opacity   = 1.0f;
        // 0.0 = straight line, 1.0 = control points fully pulled onto the centre.
        float curvature = 0.7f;

        // MultiStrand
        int   strandCount   = 1;
        float strandSpacing = 3.0f;

        // DashedLine
        float dashLength = 8.0f;
        float gapLength  = 4.0f;

        // OrganicCurve: perpendicular wobble as a fraction of the span length.
        float organicVariation = 0.08f;

        // SpringCurve
        int   springCoils     = 8;
        float springAmplitude = 15.0f;

        // Ribbon outline stroke. Width 0 or a transparent colour disables it.
        Color borderColor = Color(0, 0, 0, 0);
        float borderWidth = 0.0f;

        bool isVisible = true;
    };

// =============================================================================
// GEOMETRY HELPERS
// =============================================================================

    namespace ConnectionGeometry {

        // Thickness including the value-proportional component. `maxValue <= 0`
        // (or a zero flow) yields the base thickness alone.
        float ResolveThickness(const ConnectionStyle& style, double maxValue);

        // Control points for a cubic bezier bowed toward `center`. `curvature`
        // 0 keeps the curve straight, 1 pulls the controls onto the centre.
        void BezierControlPoints(const Point2Dd& start, const Point2Dd& end,
                                 const Point2Dd& center, float curvature,
                                 Point2Dd& cp1, Point2Dd& cp2);

        Point2Dd BezierPoint(const Point2Dd& p0, const Point2Dd& p1,
                             const Point2Dd& p2, const Point2Dd& p3, double t);

        Point2Dd BezierTangent(const Point2Dd& p0, const Point2Dd& p1,
                               const Point2Dd& p2, const Point2Dd& p3, double t);

        // Appends `segments + 1` samples of the curve to `out` (it is not
        // cleared first, so curves can be chained into one outline).
        void SampleBezier(const Point2Dd& p0, const Point2Dd& p1,
                          const Point2Dd& p2, const Point2Dd& p3,
                          int segments, std::vector<Point2Dd>& out);

        // Appends `segments + 1` points along a circular arc.
        void SampleArc(const Point2Dd& center, double radius,
                       double startAngle, double endAngle,
                       int segments, std::vector<Point2Dd>& out);

        // Closed outline of a ribbon that follows a bezier spine, widening
        // linearly from `startWidth` to `endWidth`.
        void BuildRibbonOutline(const Point2Dd& p0, const Point2Dd& cp1,
                                const Point2Dd& cp2, const Point2Dd& p3,
                                float startWidth, float endWidth,
                                int segments, std::vector<Point2Dd>& out);

        void BuildOrganicCurve(const Point2Dd& start, const Point2Dd& end,
                               int segments, float variation,
                               std::vector<Point2Dd>& out);

        void BuildSpringCurve(const Point2Dd& start, const Point2Dd& end,
                              int coils, float amplitude, float phase,
                              std::vector<Point2Dd>& out);

        // Triangle for an arrow head whose tip sits at `tip`, pointing away
        // from `from`.
        void BuildArrowHead(const Point2Dd& tip, const Point2Dd& from,
                            float size, std::vector<Point2Dd>& out);

        // Even-odd point-in-polygon test, used for ribbon hit testing.
        bool PolygonContains(const std::vector<Point2Dd>& polygon, const Point2Dd& p);

        double DistanceToPolyline(const std::vector<Point2Dd>& points, const Point2Dd& p);

    } // namespace ConnectionGeometry

// =============================================================================
// RENDERER
// =============================================================================

    // Stateless apart from the context and the radial centre the curves bow
    // toward. Construct one per render pass and reuse it for every connection.
    class ConnectionRenderer {
    public:
        ConnectionRenderer(IRenderContext* context, const Point2Dd& radialCenter)
            : ctx(context), center(radialCenter) {}

        void SetCenter(const Point2Dd& c) { center = c; }

        // `maxValue` normalises proportional thickness across a set of
        // connections; pass the largest flowValue in the set (0 to disable).
        void Draw(const Point2Dd& start, const Point2Dd& end,
                  const ConnectionStyle& style, double maxValue = 0.0);

        // Filled ribbon joining two arcs of the same circle — the chord-diagram
        // primitive. Both ends keep their own angular width, so asymmetric
        // flows read correctly.
        void DrawArcRibbon(const Point2Dd& circleCenter, double radius,
                           double sourceStartAngle, double sourceEndAngle,
                           double targetStartAngle, double targetEndAngle,
                           float curvature, const Color& fill,
                           const Color& borderColor, float borderWidth);

        // Outline of the same shape, for hit testing and caching.
        static void BuildArcRibbonOutline(const Point2Dd& circleCenter, double radius,
                                          double sourceStartAngle, double sourceEndAngle,
                                          double targetStartAngle, double targetEndAngle,
                                          float curvature, std::vector<Point2Dd>& out);

    private:
        IRenderContext* ctx = nullptr;
        Point2Dd center;

        void DrawStraight(const Point2Dd& a, const Point2Dd& b, const ConnectionStyle& s, float thickness);
        void DrawCurved(const Point2Dd& a, const Point2Dd& b, const ConnectionStyle& s, float thickness);
        void DrawRibbon(const Point2Dd& a, const Point2Dd& b, const ConnectionStyle& s, float thickness, bool gradient);
        void DrawDashed(const Point2Dd& a, const Point2Dd& b, const ConnectionStyle& s, float thickness);
        void DrawArrow(const Point2Dd& a, const Point2Dd& b, const ConnectionStyle& s, float thickness, bool bothEnds);
        void DrawSpring(const Point2Dd& a, const Point2Dd& b, const ConnectionStyle& s, float thickness);
        void DrawMultiStrand(const Point2Dd& a, const Point2Dd& b, const ConnectionStyle& s, double maxValue);
        void DrawOrganic(const Point2Dd& a, const Point2Dd& b, const ConnectionStyle& s, float thickness);
        void DrawCaps(const Point2Dd& a, const Point2Dd& b, const ConnectionStyle& s, float thickness);
        void FillPolygon(const std::vector<Point2Dd>& pts, const Color& color);
    };

} // namespace UltraCanvas

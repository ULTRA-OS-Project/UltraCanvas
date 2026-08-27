// include/Plugins/Charts/UltraCanvasScatterPlot3D.h
// 3D scatter plot element: renders an (x, y, z) point cloud inside a
// perspective axes box with orbit/zoom interaction, optional ground grid,
// depth cueing and an optional 3D correlation (best-fit) line computed as the
// principal axis of the point cloud.
//
// The renderer is pure software (painter's algorithm over IRenderContext), so
// it works in every build regardless of ULTRACANVAS_ENABLE_GL. Points and the
// correlation-line segments are depth sorted together, so the line threads
// through the cloud with correct occlusion and no depth buffer.
//
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include "UltraCanvasSmoothScroll.h"
#include "Models/STL/UltraCanvas3DTypes.h"
#include <functional>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// 3D SCATTER PLOT ELEMENT
// =============================================================================

    class UltraCanvasScatterPlot3DElement : public UltraCanvasChartElementBase {
    public:
        using ValueFormatter = std::function<std::string(double)>;

        enum class PointShape3D {
            Circle, Square, Diamond
        };

    private:
        // ---- point appearance ----
        Color pointColor = Color(0, 102, 204, 255);
        double pointSize = 5.0;             // screen radius at the camera's orbit distance
        PointShape3D pointShape = PointShape3D::Circle;
        bool depthFade = true;              // farther points fade towards the background
        double depthFadeStrength = 0.55;    // 0 = off, 1 = far points fully faded

        // ---- correlation line ----
        bool showCorrelationLine = false;
        Color correlationLineColor = Color(220, 60, 60, 255);
        float correlationLineWidth = 2.5f;

        // ---- camera ----
        double yaw = -0.62;
        double pitch = 0.42;
        double distance = 4.6;      // fits the [-1,1] data box at the default FOV
        // The wheel dollies the camera: the distance eases towards its target
        // instead of stepping, so a spin reads as one continuous move (the dolly
        // is multiplicative, which is what UltraCanvasSmoothZoom animates).
        UltraCanvasSmoothZoom dollyAnim;
        double fieldOfView = 0.62;  // radians
        bool enableOrbit = true;

        // ---- axes / grid ----
        bool showAxes3D = true;
        bool showGroundGrid = true;
        Color groundGridColor = Color(210, 210, 210, 160);
        std::string axisTitleX, axisTitleY, axisTitleZ;
        int xTickCount = 5;
        int yTickCount = 5;
        int zTickCount = 4;
        ValueFormatter xTickFormatter, yTickFormatter, zTickFormatter;
        Color axisColor = Color(150, 150, 150, 255);
        Color axisLabelColor = Color(70, 70, 70, 255);
        float axisLabelFontSize = 10.0f;
        bool rotateAxisLabels = true;
        Color titleColor = Color(0, 0, 0, 255);

        // ---- interaction state ----
        bool dragging = false;
        int lastMouseX = 0, lastMouseY = 0;

        // ---- cached per-frame projection (for tooltips) ----
        struct ProjectedPoint {
            Point2Dd screen;
            double radius = 0.0;
            size_t index = 0;
        };
        std::vector<ProjectedPoint> projectedPoints;

        // ---- cached correlation line (data space) ----
        mutable bool fitValid = false;
        mutable bool fitExists = false;
        mutable Vec3 fitCentroid, fitDirection;
        mutable double fitTMin = 0.0, fitTMax = 0.0;

    public:
        UltraCanvasScatterPlot3DElement(const std::string& id, int x, int y, int width, int height);

        // =====================================================================
        // POINT APPEARANCE
        // =====================================================================

        void SetPointColor(const Color& color);
        void SetPointSize(double size);
        void SetPointShape(PointShape3D shape);
        // Depth cueing: points farther from the camera shrink with perspective
        // and, when enabled, fade towards the background colour.
        void SetDepthFade(bool enabled, double strength = 0.55);

        // =====================================================================
        // CORRELATION LINE
        // =====================================================================

        // The correlation line is the principal axis of the point cloud
        // (orthogonal least-squares fit), spanning the data extent.
        void SetShowCorrelationLine(bool show);
        void SetCorrelationLineColor(const Color& color);
        void SetCorrelationLineWidth(float width);
        // Fit in data space. Returns false with fewer than 2 points or a
        // degenerate (zero-variance) cloud.
        bool GetCorrelationLine(Vec3& centroid, Vec3& direction) const;

        // =====================================================================
        // CAMERA
        // =====================================================================

        void SetCamera(double yawRadians, double pitchRadians, double cameraDistance);
        void SetFieldOfView(double radians);
        void SetEnableOrbit(bool on);
        double GetYaw() const { return yaw; }
        double GetPitch() const { return pitch; }
        double GetDistance() const { return distance; }
        void ViewIsometric();
        void ViewTop();
        void ViewFront();

        // =====================================================================
        // AXES / GRID
        // =====================================================================

        void SetShowAxes3D(bool on);
        void SetShowGroundGrid(bool on, const Color& c = Color(210, 210, 210, 160));
        void SetAxisTitles(const std::string& x, const std::string& y, const std::string& z);
        void SetTickCounts(int xTicks, int yTicks, int zTicks);
        void SetXTickFormatter(ValueFormatter fn);
        void SetYTickFormatter(ValueFormatter fn);
        void SetZTickFormatter(ValueFormatter fn);
        void SetAxisColors(const Color& line, const Color& label);
        void SetAxisLabelFontSize(float size);
        void SetRotateAxisLabels(bool on);
        void SetTitleColor(const Color& c);

        // =====================================================================
        // OVERRIDES
        // =====================================================================

        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

    protected:
        // 3D bounds include z; the base class helper only covers x/y.
        ChartDataBounds CalculateDataBounds() override;
        std::string GenerateTooltipContent(const ChartDataPoint& point, size_t index) override;

    private:
        // ---- projection ----
        struct Camera {
            Vec3 eye, right, up, forward;
            double focal = 1.0;
            double cx = 0.0, cy = 0.0;
        };
        Camera BuildCamera(const ChartPlotArea& area) const;
        // Data coordinates -> world cube [-1,1]^3 (data z becomes world up).
        Vec3 WorldFromData(double x, double y, double z) const;
        // Returns false when the point falls behind the camera.
        bool Project(const Camera& cam, const Vec3& p, Point2Dd& out, double& depth) const;

        // ---- correlation fit ----
        void RebuildFit() const;

        // ---- rendering stages ----
        ChartPlotArea ComputePlotArea3D(IRenderContext* ctx);
        void RenderGroundGrid(IRenderContext* ctx, const Camera& cam);
        void RenderAxes3D(IRenderContext* ctx, const Camera& cam);
        void RenderAxisLine(IRenderContext* ctx, const Camera& cam,
                            const Vec3& from, const Vec3& to,
                            const std::vector<std::string>& tickTexts,
                            const std::string& title);
        void RenderCloud(IRenderContext* ctx, const Camera& cam);
        void DrawPointSprite(IRenderContext* ctx, const Point2Dd& at,
                             double radius, const Color& color);

        std::string FormatTick(double v, const ValueFormatter& fn) const;
    };

// =============================================================================
// FACTORY
// =============================================================================

    inline std::shared_ptr<UltraCanvasScatterPlot3DElement> CreateScatterPlot3DElement(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasScatterPlot3DElement>(id, x, y, width, height);
    }

} // namespace UltraCanvas

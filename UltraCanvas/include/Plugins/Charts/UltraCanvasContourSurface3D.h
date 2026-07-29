// include/Plugins/Charts/UltraCanvasContourSurface3D.h
// 3D contour surface element: renders a scalar grid as a shaded height-field
// coloured by contour band, with optional wireframe, isolines drawn onto the
// surface, perspective axes and a band legend.
//
// The renderer is pure software (painter's algorithm over IRenderContext), so
// it works in every build regardless of ULTRACANVAS_ENABLE_GL. A height field
// sorts almost perfectly by depth, which is exactly the case a painter's
// algorithm handles well, and the low polygon counts typical of contour
// surfaces keep it cheap. Isolines are emitted per cell immediately after their
// quad, so they are occluded correctly without a depth buffer.
//
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include "Plugins/Charts/UltraCanvasColormap.h"
#include "Plugins/Charts/UltraCanvasContourGrid.h"
#include "Plugins/Charts/UltraCanvasMarchingSquares.h"
#include "Models/STL/UltraCanvas3DTypes.h"
#include <functional>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// ENUMS
// =============================================================================

// How the surface is coloured.
    enum class SurfaceColorMode {
        Banded,      // one flat colour per contour band (the Excel look)
        Smooth       // continuous colour-map value per quad
    };

// Legend placement for the band list.
    enum class SurfaceLegendMode {
        NoLegend,
        Horizontal,  // a row of swatches under the plot
        Vertical     // a column of swatches on the right
    };

// =============================================================================
// 3D CONTOUR SURFACE ELEMENT
// =============================================================================

    class UltraCanvasContourSurface3DElement : public UltraCanvasChartElementBase {
    public:
        using ValueFormatter = std::function<std::string(double)>;

    private:
        // ---- data ----
        ContourGrid field;
        bool fieldValid = false;
        double valueMin = 0.0, valueMax = 1.0;

        // ---- levels / colour ----
        ContourLevelMode levelMode = ContourLevelMode::Count;
        int levelCount = 8;
        double levelStep = 1.0;
        double levelOrigin = 0.0;
        std::vector<double> explicitLevels;
        bool niceLevels = true;
        mutable std::vector<double> levels;
        mutable bool levelsValid = false;

        HeatmapColormap colormap = HeatmapColormap::Viridis;
        std::vector<Color> customColormap;
        bool reverseColormap = false;
        SurfaceColorMode colorMode = SurfaceColorMode::Banded;

        // ---- surface appearance ----
        bool showWireframe = false;
        Color wireframeColor = Color(255, 255, 255, 60);
        float wireframeWidth = 0.6f;
        bool showSurfaceIsolines = false;
        Color isolineColor = Color(255, 255, 255, 190);
        float isolineWidth = 1.0f;
        double zExaggeration = 1.0;
        double heightSpan = 0.62;      // world height of the full value range

        // ---- lighting ----
        Vec3 lightDirection{-0.45f, 0.78f, 0.44f};
        double ambient = 0.42;
        bool lightingEnabled = true;

        // ---- camera ----
        double yaw = -0.62;
        double pitch = 0.42;
        double distance = 4.6;       // fits the [-1,1] ground box at the default FOV
        double fieldOfView = 0.62;     // radians
        bool enableOrbit = true;

        // ---- axes ----
        bool showAxes3D = true;
        std::string axisTitleX, axisTitleY, axisTitleZ;
        std::vector<std::string> categoryLabelsX;
        std::vector<std::string> categoryLabelsY;
        int xTickCount = 5;
        int yTickCount = 5;
        int zTickCount = 4;
        ValueFormatter xTickFormatter, yTickFormatter, zTickFormatter;
        Color axisColor = Color(150, 150, 150, 255);
        Color axisLabelColor = Color(70, 70, 70, 255);
        float axisLabelFontSize = 10.0f;
        bool rotateAxisLabels = true;

        // ---- legend / theme ----
        SurfaceLegendMode legendMode = SurfaceLegendMode::Horizontal;
        std::string legendTitle;
        int legendDecimals = 0;
        int legendSwatchSize = 12;
        Color titleColor = Color(0, 0, 0, 255);

        // ---- interaction state ----
        bool dragging = false;
        int lastMouseX = 0, lastMouseY = 0;

        // ---- cached per-frame geometry ----
        struct SurfaceQuad {
            Point2Dd screen[4];
            double depth = 0.0;
            Color color;
            int cellCol = 0;
            int cellRow = 0;
            bool visible = false;
        };
        std::vector<SurfaceQuad> quadBuffer;
        mutable std::vector<ContourSegment> surfaceSegments;   // sorted by cell
        // Prefix offsets into surfaceSegments, one entry per cell plus a tail,
        // so a quad's isolines are a contiguous range rather than a full scan.
        mutable std::vector<size_t> segmentCellStart;
        mutable bool segmentsValid = false;

    public:
        UltraCanvasContourSurface3DElement(const std::string& id, int x, int y, int width, int height);

        // =====================================================================
        // DATA
        // =====================================================================

        void SetData(const std::vector<double>& flat, int cols, int rows);
        void SetData(const std::vector<std::vector<double>>& matrix);
        void SetFunction(const std::function<double(double, double)>& fn,
                         int cols, int rows,
                         double xMin, double xMax, double yMin, double yMax);
        void SetDataRange(double xMin, double xMax, double yMin, double yMax);
        void SetValueRange(double lo, double hi);   // manual z scaling
        void SetAutoValueRange();
        void ClearData();

        const ContourGrid& GetField() const { return field; }
        double GetValueMin() const { return valueMin; }
        double GetValueMax() const { return valueMax; }

        // =====================================================================
        // LEVELS / COLOUR
        // =====================================================================

        void SetLevelCount(int count);
        void SetLevelStep(double step, double origin = 0.0);
        void SetLevels(const std::vector<double>& values);
        void SetNiceLevels(bool on);
        const std::vector<double>& GetLevels() const;

        void SetColormap(HeatmapColormap c);
        void SetCustomColormap(const std::vector<Color>& colors);
        void SetReverseColormap(bool on);
        void SetSurfaceColorMode(SurfaceColorMode mode);

        // =====================================================================
        // SURFACE APPEARANCE
        // =====================================================================

        void SetShowWireframe(bool on, const Color& c = Color(255, 255, 255, 60), float width = 0.6f);
        void SetShowSurfaceIsolines(bool on, const Color& c = Color(255, 255, 255, 190), float width = 1.0f);
        void SetZExaggeration(double factor);
        void SetHeightSpan(double span);
        void SetLighting(bool enabled, double ambientLevel = 0.42);
        void SetLightDirection(const Vec3& dir);

        // =====================================================================
        // CAMERA
        // =====================================================================

        void SetCamera(double yawRadians, double pitchRadians, double cameraDistance);
        void SetFieldOfView(double radians);
        void SetEnableOrbit(bool on);
        double GetYaw() const { return yaw; }
        double GetPitch() const { return pitch; }
        double GetDistance() const { return distance; }
        // Handy presets: isometric, straight down, straight on.
        void ViewIsometric();
        void ViewTop();
        void ViewFront();

        // =====================================================================
        // AXES
        // =====================================================================

        void SetShowAxes3D(bool on);
        void SetAxisTitles(const std::string& x, const std::string& y, const std::string& z);
        void SetCategoryLabelsX(const std::vector<std::string>& labels);
        void SetCategoryLabelsY(const std::vector<std::string>& labels);
        void SetTickCounts(int xTicks, int yTicks, int zTicks);
        void SetXTickFormatter(ValueFormatter fn);
        void SetYTickFormatter(ValueFormatter fn);
        void SetZTickFormatter(ValueFormatter fn);
        void SetAxisColors(const Color& line, const Color& label);
        void SetAxisLabelFontSize(float size);
        void SetRotateAxisLabels(bool on);

        // =====================================================================
        // LEGEND / THEME
        // =====================================================================

        void SetLegendMode(SurfaceLegendMode mode);
        void SetLegendTitle(const std::string& title);
        void SetLegendDecimals(int decimals);
        void SetTitleColor(const Color& c);

        // =====================================================================
        // OVERRIDES
        // =====================================================================

        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        // ---- pipeline ----
        void RecomputeValueRange();
        void RebuildLevels() const;
        void RebuildSegments() const;
        void InvalidateDerived();

        // ---- projection ----
        struct Camera {
            Vec3 eye, right, up, forward;
            double focal = 1.0;
            double cx = 0.0, cy = 0.0;
        };
        Camera BuildCamera(const ChartPlotArea& area) const;
        // World position of a fractional grid coordinate (uses the height field).
        Vec3 WorldAt(double col, double row, double value) const;
        Vec3 WorldAtNode(int c, int r) const;
        // Returns false when the point falls behind the camera.
        bool Project(const Camera& cam, const Vec3& p, Point2Dd& out, double& depth) const;

        // ---- colour ----
        double NormalizeValue(double v) const;
        Color ColorForValue(double v) const;
        Color ShadeColor(const Color& base, const Vec3& normal) const;
        size_t BandCount() const;
        void BandRange(size_t band, double& lo, double& hi) const;
        Color BandColor(size_t band) const;
        std::string BandIntervalText(size_t band) const;

        // ---- rendering stages ----
        ChartPlotArea ComputeSurfaceArea(IRenderContext* ctx);
        void RenderSurface(IRenderContext* ctx, const Camera& cam);
        void RenderAxes3D(IRenderContext* ctx, const Camera& cam);
        void RenderAxisLine(IRenderContext* ctx, const Camera& cam,
                            const Vec3& from, const Vec3& to,
                            const std::vector<std::string>& tickTexts,
                            const std::string& title, bool outwardLeft);
        void RenderLegend(IRenderContext* ctx, const ChartPlotArea& area);

        std::string FormatTick(double v, const ValueFormatter& fn) const;

        double legendReservedH = 0.0;
        double legendReservedW = 0.0;
    };

// =============================================================================
// FACTORY
// =============================================================================

    inline std::shared_ptr<UltraCanvasContourSurface3DElement> CreateContourSurface3DElement(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasContourSurface3DElement>(id, x, y, width, height);
    }

} // namespace UltraCanvas

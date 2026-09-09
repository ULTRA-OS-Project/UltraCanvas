// include/Plugins/Charts/UltraCanvasContourSurfaceGL.h
// GL-backed 3D contour surface element (roadmap item T6).
//
// When built with ULTRACANVAS_ENABLE_GL the surface is rendered on a
// UltraCanvasGLSurface with real depth testing - the same element pattern as
// UltraCanvasSTLElement. The mesh (per-quad flat colours for the banded look,
// per-node colours and normals for the smooth look), the wireframe and the
// on-surface isolines are drawn by OpenGL; the chart title, the 3D axes and
// the colour-bar legend are drawn on top by the normal 2D renderer, using a
// CPU projection that matches the GL camera exactly.
//
// Without ULTRACANVAS_ENABLE_GL the class simply derives from the software
// UltraCanvasContourSurface3DElement, so application code compiles and runs
// unchanged in every build - it just renders through the painter's-algorithm
// path instead.
//
// Version: 1.1.0
// Last Modified: 2026-08-20
// V1.1.0: legend: migrated to the shared ChartLegend ColorBar mode.
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/UltraCanvasChartLegend.h"
#include "UltraCanvasSmoothScroll.h"
#include "Plugins/Charts/UltraCanvasContourSurface3D.h"

#ifdef ULTRACANVAS_ENABLE_GL
#include "UltraCanvasGLSurface.h"
#endif

namespace UltraCanvas {

#ifdef ULTRACANVAS_ENABLE_GL

// =============================================================================
// GL-BACKED 3D CONTOUR SURFACE
// =============================================================================

    class UltraCanvasContourSurfaceGLElement : public UltraCanvasGLSurface {
    public:
        using ValueFormatter = std::function<std::string(double)>;

        UltraCanvasContourSurfaceGLElement(const std::string& id,
                                           float x, float y, float width, float height);
        ~UltraCanvasContourSurfaceGLElement() override;

        // ===== DATA (mirrors UltraCanvasContourSurface3DElement) =====
        void SetData(const std::vector<double>& flat, int cols, int rows);
        void SetData(const std::vector<std::vector<double>>& matrix);
        void SetFunction(const std::function<double(double, double)>& fn,
                         int cols, int rows,
                         double xMin, double xMax, double yMin, double yMax);
        void SetDataRange(double xMin, double xMax, double yMin, double yMax);
        void SetValueRange(double lo, double hi);
        void SetAutoValueRange();
        void ClearData();
        const ContourGrid& GetField() const { return field; }
        double GetValueMin() const { return valueMin; }
        double GetValueMax() const { return valueMax; }

        // ===== LEVELS / COLOUR =====
        void SetLevelCount(int count);
        void SetLevelStep(double step, double origin = 0.0);
        void SetLevels(const std::vector<double>& values);
        void SetNiceLevels(bool on);
        const std::vector<double>& GetLevels() const;

        void SetColormap(HeatmapColormap c);
        void SetCustomColormap(const std::vector<Color>& colors);
        void SetReverseColormap(bool on);
        void SetSurfaceColorMode(SurfaceColorMode mode);

        // ===== SURFACE APPEARANCE =====
        void SetShowWireframe(bool on, const Color& c = Color(255, 255, 255, 60),
                              float width = 0.6f);
        void SetShowSurfaceIsolines(bool on, const Color& c = Color(255, 255, 255, 190),
                                    float width = 1.0f);
        void SetZExaggeration(double factor);
        void SetHeightSpan(double span);
        void SetLighting(bool enabled, double ambientLevel = 0.42);
        void SetLightDirection(const Vec3& dir);

        // ===== CAMERA =====
        void SetCamera(double yawRadians, double pitchRadians, double cameraDistance);
        void SetFieldOfView(double radians);
        void SetEnableOrbit(bool on) { enableOrbit = on; }
        void SetAutoRotate(bool on);
        double GetYaw() const { return yaw; }
        double GetPitch() const { return pitch; }
        double GetDistance() const { return distance; }
        void ViewIsometric();
        void ViewTop();
        void ViewFront();

        // ===== AXES =====
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

        // ===== LEGEND / THEME =====
        void SetLegendMode(SurfaceLegendMode mode);
        void SetLegendTitle(const std::string& title);
        void SetLegendDecimals(int decimals);
        void SetChartTitle(const std::string& title);
        void SetTitleColor(const Color& c);
        void SetBackgroundColor(const Color& c);

        // ===== OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

    protected:
        void OnGLInit() override;
        void OnGLRender(const RenderSurfaceInfo& info) override;
        void OnGLCleanup() override;

    private:
        // ---- data / configuration ----
        ContourGrid field;
        bool fieldValid = false;
        double valueMin = 0.0, valueMax = 1.0;

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

        bool showWireframe = false;
        Color wireframeColor = Color(255, 255, 255, 60);
        float wireframeWidth = 0.6f;
        bool showSurfaceIsolines = false;
        Color isolineColor = Color(255, 255, 255, 190);
        float isolineWidth = 1.0f;
        double zExaggeration = 1.0;
        double heightSpan = 0.62;

        Vec3 lightDirection{-0.45f, 0.78f, 0.44f};
        double ambient = 0.42;
        bool lightingEnabled = true;

        double yaw = -0.62;
        double pitch = 0.42;
        double distance = 4.6;
        // The wheel dollies the camera: the distance eases towards its target
        // instead of stepping, so a spin reads as one continuous move (the dolly
        // is multiplicative, which is what UltraCanvasSmoothZoom animates).
        UltraCanvasSmoothZoom dollyAnim;
        double fieldOfView = 0.62;
        bool enableOrbit = true;
        bool autoRotate = false;

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

        SurfaceLegendMode legendMode = SurfaceLegendMode::Horizontal;
        std::string legendTitle;
        int legendDecimals = 0;
        ChartLegend legend;                 // shared component, ColorBar mode
        std::string chartTitle;
        Color titleColor = Color(235, 235, 240, 255);
        Color backgroundColor = Color(18, 19, 24, 255);

        // ---- interaction state ----
        bool dragging = false;
        int lastMouseX = 0, lastMouseY = 0;

        // ---- GL objects (unsigned handles keep GL headers out of this file) ----
        unsigned int program = 0;
        unsigned int vao = 0;
        unsigned int vbo = 0;
        unsigned int ebo = 0;
        unsigned int wireEbo = 0;
        unsigned int lineVao = 0;
        unsigned int lineVbo = 0;
        int triangleIndexCount = 0;
        int wireIndexCount = 0;
        int isolineVertexCount = 0;
        bool glReady = false;
        bool meshDirty = true;

        int uMVP = -1;
        int uLightDir = -1;
        int uAmbient = -1;
        int uLightingOn = -1;
        int uOverrideOn = -1;
        int uOverrideColor = -1;

        // ---- pipeline ----
        void RecomputeValueRange();
        void RebuildLevels() const;
        void InvalidateMesh();
        void UploadMesh();

        // ---- geometry / colour ----
        Vec3 WorldAt(double col, double row, double value) const;
        Vec3 NodeNormal(int c, int r) const;
        double NormalizeValue(double v) const;
        Color ColorForValue(double v) const;
        size_t BandCount() const;
        void BandRange(size_t band, double& lo, double& hi) const;
        Color BandColor(size_t band) const;
        std::string FormatTick(double v, const ValueFormatter& fn) const;

        // ---- 2D overlay (title, axes, legend on top of the GL image) ----
        struct OverlayCamera {
            Vec3 eye, right, up, forward;
            double focal = 1.0;
            double cx = 0.0, cy = 0.0;
        };
        OverlayCamera BuildOverlayCamera() const;
        bool Project(const OverlayCamera& cam, const Vec3& p,
                     Point2Dd& out, double& depth) const;
        void RenderOverlay(IRenderContext* ctx);
        void RenderAxes3D(IRenderContext* ctx, const OverlayCamera& cam);
        void RenderAxisLine(IRenderContext* ctx, const OverlayCamera& cam,
                            const Vec3& from, const Vec3& to,
                            const std::vector<std::string>& tickTexts,
                            const std::string& title);
        // Push the chart's legend configuration into the shared component.
        void SyncLegend();
    };

#else // !ULTRACANVAS_ENABLE_GL

// =============================================================================
// NON-GL FALLBACK: the software surface with the same class name
// =============================================================================

    class UltraCanvasContourSurfaceGLElement : public UltraCanvasContourSurface3DElement {
    public:
        UltraCanvasContourSurfaceGLElement(const std::string& id,
                                           float x, float y, float width, float height)
                : UltraCanvasContourSurface3DElement(id, static_cast<int>(x), static_cast<int>(y),
                                                     static_cast<int>(width), static_cast<int>(height)) {}

        // GL-only extras become no-ops so calling code compiles everywhere.
        void SetAutoRotate(bool) {}
    };

#endif // ULTRACANVAS_ENABLE_GL

// =============================================================================
// FACTORY
// =============================================================================

    inline std::shared_ptr<UltraCanvasContourSurfaceGLElement> CreateContourSurfaceGLElement(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasContourSurfaceGLElement>(
                id, static_cast<float>(x), static_cast<float>(y),
                static_cast<float>(width), static_cast<float>(height));
    }

} // namespace UltraCanvas

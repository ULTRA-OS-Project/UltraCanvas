// include/Plugins/Charts/UltraCanvasPieChart.h
// Comprehensive pie / donut / 3D chart element for UltraCanvas
// Version: 1.0.0
// Last Modified: 2026-05-14
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartElementBase.h"
#include "UltraCanvasRenderContext.h"
#include <functional>
#include <unordered_map>

// X11 defines `None` and `True`/`False` as macros that clobber our enumerators.
#ifdef None
#undef None
#endif

namespace UltraCanvas {

    enum class LabelPosition {
        None,
        Inside,
        Outside,
        Edge,
        Auto
    };

    enum class LabelContent {
        Name,
        Value,
        Percentage,
        NameValue,
        NamePercentage,
        ValuePercentage,
        All
    };

    class UltraCanvasPieChartElement : public UltraCanvasChartElementBase {
    public:
        UltraCanvasPieChartElement(const std::string& id, int x, int y, int w, int h);
        ~UltraCanvasPieChartElement() override = default;

        // ===== Palette & borders =====
        void SetColorPalette(const std::vector<Color>& palette);
        const std::vector<Color>& GetColorPalette() const { return colorPalette; }
        void SetBorderColor(const Color& c);
        void SetBorderWidth(float w);

        // ===== Donut / ring =====
        void SetDonutMode(bool on);
        bool GetDonutMode() const { return donutMode; }
        void SetInnerRadius(float fraction);    // fraction of outer radius (0..1)
        float GetInnerRadius() const { return innerRadiusFraction; }

        // ===== Explosion =====
        void SetGlobalExplosion(float fraction);
        float GetGlobalExplosion() const { return globalExplosion; }
        void SetSliceExplosion(size_t index, float fraction);
        void ClearSliceExplosion(size_t index);
        void ClearAllSliceExplosions();

        // ===== Labels =====
        void SetLabelPosition(LabelPosition p);
        LabelPosition GetLabelPosition() const { return labelPosition; }
        void SetLabelContent(LabelContent c);
        LabelContent GetLabelContent() const { return labelContent; }
        void SetLeaderLinesEnabled(bool on);
        bool GetLeaderLinesEnabled() const { return leaderLinesEnabled; }
        void SetLeaderLineStyle(const Color& c, float w);
        void SetLabelFont(const std::string& family, float size, FontWeight weight);
        void SetLabelColor(const Color& c);
        void SetLabelBackgroundEnabled(bool on);
        void SetLabelBackgroundColor(const Color& c);
        void SetPercentageFormatter(std::function<std::string(double pct)> f);
        void SetValueFormatter(std::function<std::string(double v)> f);

        // ===== Fills =====
        void SetSliceGradient(size_t index, const std::vector<GradientStop>& stops);
        void SetAutoRadialGradients();
        void ClearSliceGradients();
        void SetSlicePattern(size_t index, std::shared_ptr<IPaintPattern> pattern);
        void ClearSlicePatterns();

        // ===== 3D =====
        void Enable3DMode(float depthHeight, float perspectiveAngleDeg);
        void Disable3DMode();
        bool Is3DEnabled() const { return enable3D; }
        void SetLightDirection(Point2Df dir);
        void SetAmbientLight(float a);
        void SetDiffuseLight(float d);

        // ===== Tooltips =====
        void SetTooltipsEnabled(bool on) { SetEnableTooltips(on); }

        // ===== Export =====
        bool QuickExport(const std::string& filename);
        bool ExportToFile(const std::string& filename, int w, int h);

        // ===== Base overrides =====
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;

    protected:
        ChartPlotArea CalculatePlotArea() override;

    private:
        // ----- Configuration -----
        std::vector<Color> colorPalette;
        Color borderColor = Colors::White;
        float borderWidth = 1.5f;

        bool donutMode = false;
        float innerRadiusFraction = 0.45f;

        float globalExplosion = 0.0f;
        std::unordered_map<size_t, float> sliceExplosionOverrides;

        LabelPosition labelPosition = LabelPosition::Auto;
        LabelContent labelContent = LabelContent::NamePercentage;
        bool leaderLinesEnabled = true;
        Color leaderLineColor = Color(150, 150, 150, 255);
        float leaderLineWidth = 1.0f;
        std::string labelFontFamily = "Arial";
        float labelFontSize = 11.0f;
        FontWeight labelFontWeight = FontWeight::Normal;
        Color labelColor = Color(50, 50, 50, 255);
        bool labelBackgroundEnabled = false;
        Color labelBackgroundColor = Color(255, 255, 255, 200);
        std::function<std::string(double)> percentFormatter;
        std::function<std::string(double)> valueFormatter;

        std::unordered_map<size_t, std::vector<GradientStop>> sliceGradients;
        bool autoGradients = false;
        std::unordered_map<size_t, std::shared_ptr<IPaintPattern>> slicePatterns;

        bool enable3D = false;
        float depthHeight = 25.0f;
        float perspectiveAngleDeg = 20.0f;
        Point2Df lightDirection = Point2Df(-0.5, -0.8);
        float ambientLight = 0.4f;
        float diffuseLight = 0.6f;

        // ----- Cached slice data -----
        struct Slice {
            size_t index;
            std::string name;
            double value;
            double percentage;
            double startAngle;
            double endAngle;
            double midAngle;
            Color baseColor;
            float explosion;
        };

        std::vector<Slice> cachedSlices;
        bool slicesValid = false;
        Point2Df cachedCenter;
        float cachedOuterRadius = 0.0f;

        // ----- Hover state -----
        size_t hoveredSliceIndex = SIZE_MAX;

        // ----- Internal helpers -----
        void RebuildSlices();
        void InvalidateSlices();
        void ComputeCenterAndRadius(const Rect2Df& plotArea);
        float GetSliceExplosion(size_t index) const;
        Color ResolveSliceColor(size_t index, const Color& dataColor) const;
        std::string FormatPercent(double pct) const;
        std::string FormatValue(double v) const;
        std::string BuildLabelText(const Slice& slice) const;

        void RenderChartImpl(IRenderContext* ctx, const Rect2Df& chartArea);
        void Render2D(IRenderContext* ctx, const Rect2Df& chartArea);
        void Render3D(IRenderContext* ctx, const Rect2Df& chartArea);

        void DrawSlice2D(IRenderContext* ctx,
                         const Point2Df& center,
                         float outerR,
                         float innerR,
                         const Slice& slice);

        void DrawSlice3DSides(IRenderContext* ctx,
                              const Point2Df& center,
                              float outerR,
                              float innerR,
                              double vScale,
                              float depth,
                              const Slice& slice);
        void DrawSlice3DTop(IRenderContext* ctx,
                            const Point2Df& center,
                            float outerR,
                            float innerR,
                            double vScale,
                            const Slice& slice);

        std::shared_ptr<IPaintPattern> CreateSliceGradient(IRenderContext* ctx,
                                                          float outerR,
                                                          const Slice& slice);

        void RenderLabels(IRenderContext* ctx,
                          const Point2Df& center,
                          float outerR,
                          double vScale);

        // Hit-testing helper: returns slice index or SIZE_MAX
        size_t HitTestSlice(const Point2Df& localPos) const;

        // Default palette
        static std::vector<Color> DefaultPalette();
    };

    std::shared_ptr<UltraCanvasPieChartElement> CreatePieChartElement(
        const std::string& id, int x, int y, int w, int h);

} // namespace UltraCanvas

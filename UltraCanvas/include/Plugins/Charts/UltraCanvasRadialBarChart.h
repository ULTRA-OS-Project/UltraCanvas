// include/Plugins/Charts/UltraCanvasRadialBarChart.h
// Radial bar / radial line ("ray") chart element for UltraCanvas
// Version: 1.0.0
// Last Modified: 2026-07-28
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartElementBase.h"
#include "UltraCanvasRenderContext.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// RADIAL BAR DATA MODEL
// =============================================================================

    struct RadialBarValue {
        std::string label;
        double value = 0.0;
        Color color = Colors::Transparent;   // Transparent = derive from series

        RadialBarValue() = default;
        RadialBarValue(const std::string& lbl, double val,
                       const Color& clr = Colors::Transparent)
            : label(lbl), value(val), color(clr) {}
    };

    // One angular sector of the chart: a named group of consecutive bars,
    // e.g. one sensor with its 24 hourly readings.
    struct RadialBarSeries {
        std::string name;
        Color color = Colors::Transparent;   // Transparent = take from palette
        std::vector<RadialBarValue> values;

        RadialBarSeries() = default;
        explicit RadialBarSeries(const std::string& n,
                                 const Color& clr = Colors::Transparent)
            : name(n), color(clr) {}
    };

// =============================================================================
// CONFIGURATION ENUMS
// =============================================================================

    enum class RadialBarStyle {
        Bars,       // Thick rays, thickness derived from the angular slot
        Lines       // Thin uniform rays (spoke / "radial line" look)
    };

    enum class RadialBarCapStyle {
        Butt,
        Round,
        Square,
        Arrow
    };

    enum class RadialBarNormalization {
        Global,     // One min/max across every series (comparable lengths)
        PerSeries   // Each series scales to its own min/max
    };

// =============================================================================
// RADIAL BAR CHART ELEMENT
// =============================================================================

    class UltraCanvasRadialBarChart : public UltraCanvasChartElementBase {
    public:
        UltraCanvasRadialBarChart(const std::string& id, int x, int y, int w, int h);
        ~UltraCanvasRadialBarChart() override = default;

        // ===== Data management =====
        void AddSeries(const RadialBarSeries& series);
        void AddSeries(const std::string& name,
                       const std::vector<std::pair<std::string, double>>& values,
                       const Color& color = Colors::Transparent);
        void ClearSeries();
        size_t GetSeriesCount() const { return seriesList.size(); }
        RadialBarSeries* GetSeries(size_t index);
        void SetSeriesColor(size_t index, const Color& color);

        // ===== Geometry =====
        void SetStartAngle(float degrees);           // default -90 (12 o'clock)
        float GetStartAngle() const { return startAngleDeg; }
        void SetSweepAngle(float degrees);           // 10..360, default 360
        float GetSweepAngle() const { return sweepAngleDeg; }
        void SetInnerRadiusFraction(float fraction); // base ring, 0.05..0.9
        float GetInnerRadiusFraction() const { return innerRadiusFraction; }
        void SetSeriesGapAngle(float degrees);       // angular gap between series
        float GetSeriesGapAngle() const { return seriesGapDeg; }
        void SetBarWidth(float pixels);              // 0 = automatic from slot
        float GetBarWidth() const { return barWidth; }

        // ===== Value scaling =====
        void SetNormalization(RadialBarNormalization mode);
        RadialBarNormalization GetNormalization() const { return normalization; }
        void SetValueRange(double minValue, double maxValue); // manual override
        void ClearValueRange();
        void SetMinBarLengthFraction(float fraction); // keep smallest bars visible

        // ===== Style =====
        void SetBarStyle(RadialBarStyle style);
        RadialBarStyle GetBarStyle() const { return barStyle; }
        void SetCapStyle(RadialBarCapStyle cap);
        RadialBarCapStyle GetCapStyle() const { return capStyle; }
        void SetGradientFade(bool on);               // fade bars toward the tip
        bool GetGradientFade() const { return gradientFade; }
        void SetColorPalette(const std::vector<Color>& palette);
        const std::vector<Color>& GetColorPalette() const { return colorPalette; }

        // ===== Guides & center =====
        void SetShowRingGuides(bool show);
        void SetRingGuideCount(int count);
        void SetRingGuideColor(const Color& c);
        void SetInnerCircleColor(const Color& fill, const Color& border);
        void SetCenterText(const std::string& text);
        void SetCenterTextColor(const Color& c);
        void SetCenterFont(const std::string& family, float size, FontWeight weight);

        // ===== Labels =====
        void SetShowSeriesLabels(bool show);         // captions around perimeter
        void SetSeriesLabelFont(const std::string& family, float size, FontWeight weight);
        void SetShowPeakLabels(bool show);           // "Max <v>" at series peak
        void SetLabelColor(const Color& c);
        void SetValueFormatter(std::function<std::string(double)> f);

        // ===== Interaction =====
        void SetHoverHighlightEnabled(bool on);
        void SetTooltipsEnabled(bool on) { SetEnableTooltips(on); }
        void SetRadialTooltipFormatter(
            std::function<std::string(const RadialBarSeries&, const RadialBarValue&)> f);

        // Callbacks receive (seriesIndex, valueIndex)
        std::function<void(size_t, size_t)> onBarClick;
        std::function<void(size_t, size_t)> onBarHover;

        // ===== Base overrides =====
        // Overridden because the chart keeps its own series data and must
        // render without an IChartDataSource attached.
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;

    protected:
        ChartPlotArea CalculatePlotArea() override;

    private:
        // ----- Data -----
        std::vector<RadialBarSeries> seriesList;

        // ----- Geometry configuration -----
        float startAngleDeg = -90.0f;
        float sweepAngleDeg = 360.0f;
        float innerRadiusFraction = 0.25f;
        float seriesGapDeg = 3.0f;
        float barWidth = 0.0f;               // 0 = automatic

        // ----- Scaling -----
        RadialBarNormalization normalization = RadialBarNormalization::Global;
        bool manualRange = false;
        double manualMin = 0.0;
        double manualMax = 1.0;
        float minBarLengthFraction = 0.06f;

        // ----- Style -----
        RadialBarStyle barStyle = RadialBarStyle::Bars;
        RadialBarCapStyle capStyle = RadialBarCapStyle::Round;
        bool gradientFade = true;
        std::vector<Color> colorPalette;

        bool showRingGuides = false;
        int ringGuideCount = 3;
        Color ringGuideColor = Color(210, 210, 210, 180);
        Color innerCircleFill = Color(248, 248, 248, 255);
        Color innerCircleBorder = Color(210, 210, 210, 255);
        std::string centerText;
        Color centerTextColor = Color(60, 60, 60, 255);
        std::string centerFontFamily = "Arial";
        float centerFontSize = 13.0f;
        FontWeight centerFontWeight = FontWeight::Bold;

        bool showSeriesLabels = true;
        std::string seriesLabelFontFamily = "Arial";
        float seriesLabelFontSize = 11.0f;
        FontWeight seriesLabelFontWeight = FontWeight::Bold;
        bool showPeakLabels = false;
        Color labelColor = Color(70, 70, 70, 255);
        std::function<std::string(double)> valueFormatter;
        std::function<std::string(const RadialBarSeries&, const RadialBarValue&)>
            radialTooltipFormatter;

        bool hoverHighlightEnabled = true;

        // ----- Cached layout -----
        struct Bar {
            size_t seriesIndex = 0;
            size_t valueIndex = 0;
            double angle = 0.0;          // radians, center of the slot
            double slotSpan = 0.0;       // radians available to this bar
            double normalized = 0.0;     // 0..1 after normalization
            Color color;
        };

        struct SeriesSector {
            size_t seriesIndex = 0;
            double startAngle = 0.0;     // radians
            double endAngle = 0.0;
            Color color;
            size_t peakBarIndex = SIZE_MAX;   // index into `bars`
        };

        std::vector<Bar> bars;
        std::vector<SeriesSector> sectors;
        bool layoutValid = false;
        Point2Dd cachedCenter;
        float cachedInnerRadius = 0.0f;
        float cachedOuterRadius = 0.0f;

        size_t hoveredBar = SIZE_MAX;

        // ----- Helpers -----
        void InvalidateLayout();
        void RebuildLayout();
        Color ResolveSeriesColor(size_t index) const;
        float BarLength(const Bar& bar) const;       // radial extent from inner ring
        float BarThickness(const Bar& bar) const;
        std::string FormatValue(double v) const;
        std::string BuildTooltipText(const Bar& bar) const;

        void DrawBar(IRenderContext* ctx, const Bar& bar, bool highlighted);
        void DrawArrowCap(IRenderContext* ctx, const Point2Dd& tip,
                          double angle, float thickness, const Color& color);
        void RenderRingGuides(IRenderContext* ctx);
        void RenderInnerCircle(IRenderContext* ctx);
        void RenderSeriesLabels(IRenderContext* ctx);
        void RenderPeakLabels(IRenderContext* ctx);
        void RenderTitle(IRenderContext* ctx);

        size_t HitTestBar(const Point2Dd& localPos) const;
        bool HandleClick(const UCEvent& event);

        static std::vector<Color> DefaultPalette();
    };

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    std::shared_ptr<UltraCanvasRadialBarChart> CreateRadialBarChart(
        const std::string& id, int x, int y, int w, int h);

    // Convenience: one series per entry, values labeled T1..Tn — the classic
    // multi-sensor / time-series ray chart.
    std::shared_ptr<UltraCanvasRadialBarChart> CreateRadialBarChartFromSeries(
        const std::string& id, int x, int y, int w, int h,
        const std::vector<std::pair<std::string, std::vector<double>>>& seriesData);

} // namespace UltraCanvas

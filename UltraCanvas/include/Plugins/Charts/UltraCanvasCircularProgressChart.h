// include/Plugins/Charts/UltraCanvasCircularProgressChart.h
// Circular progress chart element: concentric progress rings ("activity
// rings"), single progress ring and progress pie, all angle-encoded.
// Version: 1.1.0
// Last Modified: 2026-08-20
// V1.1.0: legend: migrated to the shared ChartLegend component
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartElementBase.h"
#include "UltraCanvasRenderContext.h"
#include "Plugins/Charts/UltraCanvasChartLegend.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// RING DATA MODEL
// =============================================================================

    // One ring of the chart. `value` is mapped onto the ring's arc sweep via
    // the [minValue, maxValue] range (0..100 by default, so values read as
    // percentages out of the box).
    struct ProgressRing {
        std::string label;
        double value = 0.0;
        double minValue = 0.0;
        double maxValue = 100.0;
        Color color = Colors::Transparent;   // Transparent = take from palette
        std::string iconPath;                // optional icon for the legend chip

        ProgressRing() = default;
        ProgressRing(const std::string& lbl, double val,
                     const Color& clr = Colors::Transparent)
            : label(lbl), value(val), color(clr) {}
    };

// =============================================================================
// CONFIGURATION ENUMS
// =============================================================================

    enum class CircularProgressStyle {
        ConcentricRings,   // One arc per ring on its own radius (default)
        SingleRing,        // Only the first ring, drawn at full thickness
        ProgressPie        // First ring as a filled sector over a track disc
    };

    enum class RingCapStyle {
        Butt,
        Round
    };

    enum class RingTrackMode {
        AutoTint,          // Pale tint of the ring's own colour (default)
        Explicit,          // The colour given to SetTrackColor for every ring
        Hidden             // No background track
    };

    // Content of the callout at the end of each arc.
    enum class RingTipLabel {
        NoLabel,           // 'None' avoided — X11 #defines None as a macro
        Percentage,
        Value
    };

    // Where the ring names are drawn.
    enum class RingNamePosition {
        Hidden,
        InsideStart,       // Horizontal text inside the band, at the arc start
        StackedStart       // Column of "label value" rows, one per ring, each
                           // row aligned with its ring's start point
    };

    enum class CircularLegendStyle {
        NoLegend,
        NumberedChips,     // "01" chip in the ring colour + optional icon + label
        Swatches           // Small colour swatch + label
    };

    enum class CircularLegendPosition {
        Left, Right, Top, Bottom
    };

// =============================================================================
// CIRCULAR PROGRESS CHART ELEMENT
// =============================================================================

    class UltraCanvasCircularProgressChart : public UltraCanvasChartElementBase {
    public:
        UltraCanvasCircularProgressChart(const std::string& id, int x, int y, int w, int h);
        ~UltraCanvasCircularProgressChart() override = default;

        // ===== Data management =====
        // Rings are ordered inner to outer. Returns the ring index.
        size_t AddRing(const ProgressRing& ring);
        size_t AddRing(const std::string& label, double value,
                       const Color& color = Colors::Transparent);
        void ClearRings();
        size_t GetRingCount() const { return rings.size(); }
        const ProgressRing* GetRing(size_t index) const;
        bool SetRingValue(size_t index, double value);
        bool SetRingColor(size_t index, const Color& color);
        bool SetRingRange(size_t index, double minValue, double maxValue);

        // ===== Sub-style =====
        void SetSubStyle(CircularProgressStyle style);
        CircularProgressStyle GetSubStyle() const { return subStyle; }

        // ===== Geometry =====
        void SetStartAngle(float degrees);            // default -90 (12 o'clock)
        float GetStartAngle() const { return startAngleDeg; }
        void SetClockwise(bool on);                   // default true
        bool GetClockwise() const { return clockwise; }
        void SetInnerRadiusFraction(float fraction);  // centre hole, 0..0.9
        float GetInnerRadiusFraction() const { return innerRadiusFraction; }
        void SetRingSpacing(float pixels);            // radial gap between rings
        float GetRingSpacing() const { return ringSpacing; }
        void SetRingThickness(float pixels);          // 0 = automatic from radius
        float GetRingThickness() const { return ringThickness; }

        // ===== Arcs & tracks =====
        void SetCapStyle(RingCapStyle cap);
        RingCapStyle GetCapStyle() const { return capStyle; }
        void SetTrackMode(RingTrackMode mode);
        RingTrackMode GetTrackMode() const { return trackMode; }
        void SetTrackColor(const Color& c);           // used by Explicit mode
        void SetTrackTintAlpha(uint8_t alpha);        // used by AutoTint mode
        void SetColorPalette(const std::vector<Color>& palette);
        const std::vector<Color>& GetColorPalette() const { return colorPalette; }

        // ===== Tip callouts =====
        void SetTipLabelContent(RingTipLabel content);
        RingTipLabel GetTipLabelContent() const { return tipLabelContent; }
        void SetTipLabelBubble(bool on);              // rounded background pill
        void SetTipLabelFont(const std::string& family, float size, FontWeight weight);
        void SetTipLabelColor(const Color& c);

        // ===== Ring name labels =====
        void SetRingNameLabels(RingNamePosition position);
        RingNamePosition GetRingNameLabels() const { return ringNamePosition; }
        void SetNameLabelFont(const std::string& family, float size, FontWeight weight);
        void SetNameLabelColor(const Color& c);

        // ===== Centre disc =====
        void SetShowCenterDisc(bool show);
        void SetCenterDisc(const Color& fill, const Color& border);
        void SetCenterText(const std::string& title, const std::string& subtitle = "");
        void SetCenterTextColor(const Color& c);
        void SetCenterFont(const std::string& family, float size, FontWeight weight);

        // ===== Legend =====
        void SetLegendStyle(CircularLegendStyle style,
                            CircularLegendPosition position = CircularLegendPosition::Left);
        CircularLegendStyle GetLegendStyle() const { return legendStyle; }
        CircularLegendPosition GetLegendPosition() const { return legendPosition; }
        void SetLegendFont(const std::string& family, float size);
        void SetLegendTextColor(const Color& c);

        // ===== Formatters =====
        void SetValueFormatter(std::function<std::string(double)> f);
        void SetPercentFormatter(std::function<std::string(double pct)> f); // pct 0..100

        // ===== Interaction =====
        void SetHoverHighlightEnabled(bool on);
        void SetTooltipsEnabled(bool on) { SetEnableTooltips(on); }
        void SetRingTooltipFormatter(std::function<std::string(const ProgressRing&)> f);

        // Callbacks receive the ring index.
        std::function<void(size_t)> onRingClick;
        std::function<void(size_t)> onRingHover;

        // ===== Base overrides =====
        // Overridden because the chart owns its ring data and renders without
        // an IChartDataSource attached.
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;

    protected:
        ChartPlotArea CalculatePlotArea() override;

    private:
        // ----- Data -----
        std::vector<ProgressRing> rings;

        // ----- Configuration -----
        CircularProgressStyle subStyle = CircularProgressStyle::ConcentricRings;
        float startAngleDeg = -90.0f;
        bool clockwise = true;
        float innerRadiusFraction = 0.28f;
        float ringSpacing = 4.0f;
        float ringThickness = 0.0f;          // 0 = automatic

        RingCapStyle capStyle = RingCapStyle::Round;
        RingTrackMode trackMode = RingTrackMode::AutoTint;
        Color trackColor = Color(238, 238, 242, 255);
        uint8_t trackTintAlpha = 46;
        std::vector<Color> colorPalette;

        RingTipLabel tipLabelContent = RingTipLabel::Percentage;
        bool tipLabelBubble = false;
        std::string tipFontFamily = "Arial";
        float tipFontSize = 11.0f;
        FontWeight tipFontWeight = FontWeight::Bold;
        Color tipLabelColor = Color(40, 40, 40, 255);

        RingNamePosition ringNamePosition = RingNamePosition::Hidden;
        std::string nameFontFamily = "Arial";
        float nameFontSize = 10.0f;
        FontWeight nameFontWeight = FontWeight::Normal;
        Color nameLabelColor = Color(70, 70, 70, 255);

        bool showCenterDisc = false;
        Color centerFillColor = Color(245, 245, 245, 255);
        Color centerBorderColor = Colors::Transparent;
        std::string centerTitle;
        std::string centerSubtitle;
        Color centerTextColor = Color(50, 50, 50, 255);
        std::string centerFontFamily = "Arial";
        float centerFontSize = 14.0f;
        FontWeight centerFontWeight = FontWeight::Bold;

        CircularLegendStyle legendStyle = CircularLegendStyle::NoLegend;
        CircularLegendPosition legendPosition = CircularLegendPosition::Left;
        std::string legendFontFamily = "Arial";
        float legendFontSize = 11.0f;
        Color legendTextColor = Color(70, 70, 70, 255);
        // Shared legend component; content, style and position are synced from
        // the legacy fields above on every render pass.
        ChartLegend legend;

        std::function<std::string(double)> valueFormatter;
        std::function<std::string(double)> percentFormatter;
        std::function<std::string(const ProgressRing&)> ringTooltipFormatter;

        bool hoverHighlightEnabled = true;

        // ----- Cached layout -----
        struct RingLayout {
            size_t ringIndex = 0;
            double radius = 0.0;         // mid-band radius in pixels
            float thickness = 0.0f;
            double startAngle = 0.0;     // radians, screen convention
            double endAngle = 0.0;       // radians; start <= end always
            double fraction = 0.0;       // normalized 0..1
            Color color;
        };

        std::vector<RingLayout> ringLayouts;
        bool layoutValid = false;
        Point2Dd cachedCenter;
        float cachedInnerRadius = 0.0f;
        float cachedOuterRadius = 0.0f;

        size_t hoveredRing = SIZE_MAX;

        // ----- Helpers -----
        void InvalidateLayout();
        void RebuildLayout();
        size_t VisibleRingCount() const;
        double RingFraction(const ProgressRing& ring) const;
        Color ResolveRingColor(size_t index) const;
        Color TrackColorFor(const RingLayout& layout) const;
        std::string FormatValue(double v) const;
        std::string FormatPercent(double fraction) const;   // fraction 0..1
        std::string TipLabelText(const RingLayout& layout) const;
        std::string BuildTooltipText(size_t ringIndex) const;

        // ----- Rendering -----
        void RenderTitle(IRenderContext* ctx);
        void RenderRings(IRenderContext* ctx);
        void RenderProgressPie(IRenderContext* ctx);
        void DrawRingArc(IRenderContext* ctx, const RingLayout& layout, bool highlighted);
        void RenderTipLabels(IRenderContext* ctx);
        void RenderNameLabels(IRenderContext* ctx);
        void RenderStackedStartLabels(IRenderContext* ctx);
        void RenderCenterDisc(IRenderContext* ctx);
        void UpdateLegend();
        Point2Dd ArcTipPoint(const RingLayout& layout) const;
        double ScreenAngle(const RingLayout& layout, double fractionAlong) const;

        // ----- Hit testing -----
        size_t HitTestRing(const Point2Dd& localPos) const;
        bool HandleClick(const UCEvent& event);

        static std::vector<Color> DefaultPalette();
    };

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    std::shared_ptr<UltraCanvasCircularProgressChart> CreateCircularProgressChart(
        const std::string& id, int x, int y, int w, int h);

    // Convenience: concentric rings from (label, percent) pairs.
    std::shared_ptr<UltraCanvasCircularProgressChart> CreateConcentricRingChart(
        const std::string& id, int x, int y, int w, int h,
        const std::vector<std::pair<std::string, double>>& labeledPercentages);

    // Convenience: one-value progress pie (filled sector over a track disc).
    std::shared_ptr<UltraCanvasCircularProgressChart> CreateProgressPieChart(
        const std::string& id, int x, int y, int w, int h,
        const std::string& label, double percent,
        const Color& color = Colors::Transparent);

} // namespace UltraCanvas

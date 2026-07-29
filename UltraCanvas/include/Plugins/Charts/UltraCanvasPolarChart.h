// include/Plugins/Charts/UltraCanvasPolarChart.h
// Comprehensive polar chart element: scatter, line, spline, area and column
// series plotted on a configurable angle/radius coordinate system
// Version: 1.0.0
// Last Modified: 2026-07-29
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
// POLAR DATA MODEL
// =============================================================================

    // A single observation in polar space. In Numeric angle mode `angle` is a
    // value on the angular axis (degrees by default); in Categorical mode the
    // point's position comes from its index in the series and `angle` is unused.
    struct PolarDataPoint {
        double angle = 0.0;
        double radius = 0.0;
        std::string label;
        Color color = Colors::Transparent;   // Transparent = inherit series color
        float markerSize = 0.0f;             // 0 = inherit series marker size

        PolarDataPoint() = default;
        PolarDataPoint(double angleValue, double radiusValue,
                       const std::string& lbl = "",
                       const Color& c = Colors::Transparent)
            : angle(angleValue), radius(radiusValue), label(lbl), color(c) {}
    };

    enum class PolarSeriesType {
        Line,        // Straight segments between consecutive points
        Spline,      // Catmull-Rom smoothed curve
        Area,        // Line plus a fill down to the base radius
        SplineArea,  // Spline plus a fill down to the base radius
        Scatter,     // Markers only
        Column       // Annular sectors (polar columns / Nightingale rose)
    };

    enum class PolarMarkerShape {
        NoMarker, Circle, Square, Diamond, Triangle, Cross, Star
    };

    struct PolarSeries {
        std::string name;
        PolarSeriesType type = PolarSeriesType::Line;
        std::vector<PolarDataPoint> points;

        Color color = Colors::Transparent;       // Transparent = take from palette
        Color fillColor = Colors::Transparent;   // Transparent = color + fillOpacity
        float lineWidth = 2.0f;
        float fillOpacity = 0.35f;
        bool dashed = false;

        PolarMarkerShape marker = PolarMarkerShape::Circle;
        float markerSize = 5.0f;

        // Connect the last point back to the first. Meaningful for closed
        // categorical series (radar-style outlines) and full-circle sweeps.
        bool closed = true;
        bool visible = true;
        bool showValueLabels = false;

        PolarSeries() = default;
        explicit PolarSeries(const std::string& seriesName,
                             PolarSeriesType seriesType = PolarSeriesType::Line,
                             const Color& seriesColor = Colors::Transparent)
            : name(seriesName), type(seriesType), color(seriesColor) {}
    };

// =============================================================================
// AXIS / GRID CONFIGURATION
// =============================================================================

    enum class PolarAngleMode {
        Numeric,      // Points carry their own angular value
        Categorical   // Points are distributed evenly, one slot per category
    };

    enum class PolarDirection {
        Clockwise,
        CounterClockwise
    };

    enum class PolarGridShape {
        Circular,     // Concentric circles
        Polygonal     // Spider-web polygons through the angular ticks
    };

    enum class PolarRadialScale {
        Linear,
        Logarithmic,
        SquareRoot    // Area-true scaling for rose charts
    };

    enum class PolarLabelPlacement {
        Outside,
        Inside,
        Hidden
    };

    enum class PolarLabelOrientation {
        Horizontal,   // Always upright
        Tangential,   // Runs along the circumference
        Radial        // Runs along the spoke
    };

    enum class PolarStackMode {
        NoStacking,     // Column series are grouped side by side
        Stacked,        // Column/area series accumulate outward
        PercentStacked  // Accumulate outward normalized to 100%
    };

    enum class PolarLegendPosition {
        NoLegend, Top, Bottom, Left, Right
    };

    // Where categories sit relative to the angular grid lines.
    enum class PolarCategoryPlacement {
        OnSpokes,      // Category angle lies on a grid spoke (radar look)
        BetweenSpokes  // Grid spokes fall on slot boundaries (column look)
    };

    // A shaded ring between two radial values, e.g. tolerance zones.
    struct PolarRadialBand {
        double from = 0.0;
        double to = 0.0;
        Color color = Color(200, 200, 200, 90);
        std::string label;

        PolarRadialBand() = default;
        PolarRadialBand(double f, double t, const Color& c, const std::string& lbl = "")
            : from(f), to(t), color(c), label(lbl) {}
    };

    // A shaded wedge between two angular values, e.g. a wind sector.
    struct PolarAngularBand {
        double fromAngle = 0.0;
        double toAngle = 0.0;
        Color color = Color(200, 200, 200, 90);
        std::string label;

        PolarAngularBand() = default;
        PolarAngularBand(double f, double t, const Color& c, const std::string& lbl = "")
            : fromAngle(f), toAngle(t), color(c), label(lbl) {}
    };

    // Styling for one ring of angular labels. The chart draws a primary ring and
    // an optional secondary ring (different interval / placement / colour), which
    // is how dual angle-axis layouts are produced.
    struct PolarAngleAxisStyle {
        bool visible = true;
        PolarLabelPlacement placement = PolarLabelPlacement::Outside;
        PolarLabelOrientation orientation = PolarLabelOrientation::Horizontal;
        float tickIntervalDeg = 0.0f;              // 0 = automatic
        float labelOffset = 12.0f;                 // pixels from the axis circle
        float tickLength = 5.0f;
        bool showTicks = true;
        bool showAxisLine = true;
        Color labelColor = Color(90, 90, 90, 255);
        Color axisLineColor = Color(170, 170, 170, 255);
        std::string fontFamily = "Arial";
        float fontSize = 11.0f;
    };

// =============================================================================
// POLAR CHART ELEMENT
// =============================================================================

    class UltraCanvasPolarChart : public UltraCanvasChartElementBase {
    public:
        UltraCanvasPolarChart(const std::string& id, int x, int y, int w, int h);
        ~UltraCanvasPolarChart() override = default;

        // ===== Series management =====
        void AddSeries(const PolarSeries& series);
        // Numeric convenience overload: (angle, radius) pairs.
        void AddSeries(const std::string& name, PolarSeriesType type,
                       const std::vector<std::pair<double, double>>& angleRadiusPairs,
                       const Color& color = Colors::Transparent);
        // Categorical convenience overload: one value per category.
        void AddCategorySeries(const std::string& name, PolarSeriesType type,
                               const std::vector<double>& values,
                               const Color& color = Colors::Transparent);
        void ClearSeries();
        size_t GetSeriesCount() const { return seriesList.size(); }
        PolarSeries* GetSeries(size_t index);
        const PolarSeries* GetSeries(size_t index) const;
        void SetSeriesColor(size_t index, const Color& color);
        void SetSeriesType(size_t index, PolarSeriesType type);
        void SetSeriesVisible(size_t index, bool visible);
        bool IsSeriesVisible(size_t index) const;

        // ===== Angular axis =====
        void SetAngleMode(PolarAngleMode mode);
        PolarAngleMode GetAngleMode() const { return angleMode; }
        void SetCategories(const std::vector<std::string>& categories);
        const std::vector<std::string>& GetCategories() const { return categories; }
        void SetCategoryPlacement(PolarCategoryPlacement placement);
        // Angular data domain mapped onto the sweep (numeric mode). Default 0..360.
        void SetAngleRange(double minAngle, double maxAngle);
        // Screen direction of the smallest angular value. -90 = 12 o'clock (default).
        void SetZeroAngle(float degrees);
        float GetZeroAngle() const { return zeroAngleDeg; }
        void SetDirection(PolarDirection direction);
        PolarDirection GetDirection() const { return direction; }
        // Angular extent of the plot: 360 = full circle, smaller = fan / wedge.
        void SetSweepAngle(float degrees);
        float GetSweepAngle() const { return sweepAngleDeg; }

        void SetAngleAxisStyle(const PolarAngleAxisStyle& style);
        const PolarAngleAxisStyle& GetAngleAxisStyle() const { return angleAxis; }
        void SetShowAngleLabels(bool show);
        void SetAngleTickInterval(float degrees);          // 0 = automatic
        void SetAngleLabelOrientation(PolarLabelOrientation orientation);
        void SetAngleLabelPlacement(PolarLabelPlacement placement);
        void SetAngleLabelFormatter(std::function<std::string(double)> formatter);

        // Optional second ring of angular labels (different interval or side).
        void SetSecondaryAngleAxisEnabled(bool enabled);
        bool GetSecondaryAngleAxisEnabled() const { return secondaryAngleAxisEnabled; }
        void SetSecondaryAngleAxisStyle(const PolarAngleAxisStyle& style);
        const PolarAngleAxisStyle& GetSecondaryAngleAxisStyle() const { return secondaryAngleAxis; }

        // ===== Radial axis =====
        void SetRadialScale(PolarRadialScale scale);
        PolarRadialScale GetRadialScale() const { return radialScale; }
        void SetRadialRange(double minValue, double maxValue);   // manual override
        void ClearRadialRange();                                  // back to automatic
        double GetRadialMin() const { return resolvedRadialMin; }
        double GetRadialMax() const { return resolvedRadialMax; }
        void SetRadialTickCount(int count);                       // target tick count
        void SetRadialTickInterval(double interval);              // 0 = automatic
        void SetRadialAxisIncludesZero(bool include);
        void SetRadialAxisReversed(bool reversed);                // values grow inward
        bool GetRadialAxisReversed() const { return radialReversed; }
        void SetInnerRadiusFraction(float fraction);              // donut hole, 0..0.9
        float GetInnerRadiusFraction() const { return innerRadiusFraction; }
        void SetShowRadialLabels(bool show);
        // Data-space angle along which the radial tick labels are drawn.
        void SetRadialLabelAngle(double angle);
        void SetRadialLabelFormatter(std::function<std::string(double)> formatter);
        void SetRadialLabelUnit(const std::string& unit);         // suffix, e.g. " ppm"
        void SetRadialAxisLineVisible(bool visible);
        void SetRadialLabelColor(const Color& color);

        // ===== Grid & plot background =====
        void SetGridShape(PolarGridShape shape);
        PolarGridShape GetGridShape() const { return gridShape; }
        void SetShowRadialGrid(bool show);      // concentric rings
        void SetShowAngularGrid(bool show);     // spokes
        void SetGridLineWidth(float width);
        void SetMinorRadialGridCount(int count); // extra rings between major ticks
        void SetAlternatingBands(bool enabled,
                                 const Color& colorA = Color(250, 250, 252, 255),
                                 const Color& colorB = Color(240, 242, 246, 255));
        void SetPlotBackgroundColor(const Color& color);   // disc behind the data

        void AddRadialBand(const PolarRadialBand& band);
        void AddRadialBand(double from, double to, const Color& color,
                           const std::string& label = "");
        void ClearRadialBands();
        void AddAngularBand(const PolarAngularBand& band);
        void AddAngularBand(double fromAngle, double toAngle, const Color& color,
                            const std::string& label = "");
        void ClearAngularBands();

        // ===== Series layout =====
        void SetStackMode(PolarStackMode mode);
        PolarStackMode GetStackMode() const { return stackMode; }
        void SetColumnWidthFraction(float fraction);   // 0.05..1.0 of the angular slot
        float GetColumnWidthFraction() const { return columnWidthFraction; }
        void SetColumnBorder(const Color& color, float width);
        void SetSortPointsByAngle(bool sort);          // numeric mode only
        void SetSplineTension(float tension);          // 0..1, default 0.5

        // ===== Styling =====
        void SetColorPalette(const std::vector<Color>& palette);
        const std::vector<Color>& GetColorPalette() const { return colorPalette; }
        void SetSubtitle(const std::string& subtitle);
        void SetTitleColor(const Color& color);
        void SetLabelFont(const std::string& family, float size);

        // ===== Legend =====
        void SetLegendPosition(PolarLegendPosition position);
        PolarLegendPosition GetLegendPosition() const { return legendPosition; }
        void SetLegendFont(const std::string& family, float size);
        void SetLegendTextColor(const Color& color);
        // Clicking a legend entry toggles that series on and off.
        void SetLegendToggleEnabled(bool enabled);

        // ===== Interaction =====
        void SetHoverHighlightEnabled(bool enabled);
        void SetTooltipsEnabled(bool enabled) { SetEnableTooltips(enabled); }
        void SetPolarTooltipFormatter(
            std::function<std::string(const PolarSeries&, const PolarDataPoint&)> formatter);
        // Dragging horizontally rotates the chart by changing the zero angle.
        void SetRotateOnDrag(bool enabled);

        // Callbacks receive (seriesIndex, pointIndex)
        std::function<void(size_t, size_t)> onPointClick;
        std::function<void(size_t, size_t)> onPointHover;
        std::function<void(size_t, bool)> onSeriesVisibilityChanged;

        // ===== Coordinate helpers (element-local pixels) =====
        Point2Dd PolarToScreen(double dataAngle, double radiusValue) const;
        // Inverse of PolarToScreen. Returns false when the position falls outside
        // the plotted radius band.
        bool ScreenToPolar(const Point2Dd& pos, double& dataAngle, double& radiusValue) const;

        // ===== Base overrides =====
        // Overridden because the chart owns its series data and renders without
        // an IChartDataSource attached.
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

    protected:
        ChartPlotArea CalculatePlotArea() override;

    private:
        // ----- Data -----
        std::vector<PolarSeries> seriesList;
        std::vector<std::string> categories;

        // ----- Angular axis -----
        PolarAngleMode angleMode = PolarAngleMode::Numeric;
        PolarCategoryPlacement categoryPlacement = PolarCategoryPlacement::OnSpokes;
        double angleMin = 0.0;
        double angleMax = 360.0;
        float zeroAngleDeg = -90.0f;
        float sweepAngleDeg = 360.0f;
        PolarDirection direction = PolarDirection::Clockwise;
        PolarAngleAxisStyle angleAxis;
        bool secondaryAngleAxisEnabled = false;
        PolarAngleAxisStyle secondaryAngleAxis;
        std::function<std::string(double)> angleLabelFormatter;

        // ----- Radial axis -----
        PolarRadialScale radialScale = PolarRadialScale::Linear;
        bool manualRadialRange = false;
        double manualRadialMin = 0.0;
        double manualRadialMax = 1.0;
        bool radialIncludesZero = true;
        bool radialReversed = false;
        int radialTickCount = 5;
        double radialTickInterval = 0.0;
        float innerRadiusFraction = 0.0f;
        bool showRadialLabels = true;
        bool radialAxisLineVisible = true;
        double radialLabelAngle = 0.0;
        bool radialLabelAngleSet = false;
        std::function<std::string(double)> radialLabelFormatter;
        std::string radialLabelUnit;
        Color radialLabelColor = Color(90, 90, 90, 255);

        // ----- Grid -----
        PolarGridShape gridShape = PolarGridShape::Circular;
        bool showRadialGrid = true;
        bool showAngularGrid = true;
        float gridLineWidth = 1.0f;
        int minorRadialGridCount = 0;
        bool alternatingBands = false;
        Color alternatingColorA = Color(250, 250, 252, 255);
        Color alternatingColorB = Color(240, 242, 246, 255);
        Color plotBackgroundColor = Colors::Transparent;
        std::vector<PolarRadialBand> radialBands;
        std::vector<PolarAngularBand> angularBands;

        // ----- Series layout -----
        PolarStackMode stackMode = PolarStackMode::NoStacking;
        float columnWidthFraction = 0.85f;
        Color columnBorderColor = Color(255, 255, 255, 200);
        float columnBorderWidth = 1.0f;
        bool sortPointsByAngle = false;
        float splineTension = 0.5f;

        // ----- Styling -----
        std::vector<Color> colorPalette;
        std::string subtitle;
        Color titleColor = Color(60, 70, 85, 255);
        std::string labelFontFamily = "Arial";
        float labelFontSize = 11.0f;

        // ----- Legend -----
        PolarLegendPosition legendPosition = PolarLegendPosition::Top;
        std::string legendFontFamily = "Arial";
        float legendFontSize = 11.0f;
        Color legendTextColor = Color(70, 70, 70, 255);
        bool legendToggleEnabled = true;

        // ----- Interaction -----
        bool hoverHighlightEnabled = true;
        bool rotateOnDrag = false;
        std::function<std::string(const PolarSeries&, const PolarDataPoint&)> polarTooltipFormatter;

        size_t hoveredSeries = SIZE_MAX;
        size_t hoveredPoint = SIZE_MAX;
        bool isRotating = false;
        Point2Di rotateAnchor;
        float rotateAnchorAngle = 0.0f;

        // ----- Cached layout -----
        struct AngleTick {
            double dataAngle = 0.0;   // value on the angular axis
            double screenRad = 0.0;   // radians in element-local screen space
            std::string label;
        };

        struct PlottedPoint {
            size_t seriesIndex = 0;
            size_t pointIndex = 0;
            Point2Dd pos;
            double screenRad = 0.0;
            double radiusPx = 0.0;
        };

        struct PlottedColumn {
            size_t seriesIndex = 0;
            size_t pointIndex = 0;
            double startRad = 0.0;    // screen-space radians
            double endRad = 0.0;
            double innerPx = 0.0;
            double outerPx = 0.0;
            Color color;
        };

        struct LegendEntry {
            size_t seriesIndex = 0;
            Rect2Dd bounds;
            Color color;
            std::string text;
        };

        bool layoutValid = false;
        int lastLayoutWidth = -1;
        int lastLayoutHeight = -1;
        std::string layoutTitleSnapshot;

        Point2Dd cachedCenter;
        double cachedInnerRadius = 0.0;
        double cachedOuterRadius = 0.0;
        double resolvedRadialMin = 0.0;
        double resolvedRadialMax = 1.0;

        std::vector<double> radialTicks;
        std::vector<AngleTick> angleTicks;
        // Screen-space angles of the angular grid lines. These coincide with the
        // angle ticks except in BetweenSpokes mode, where they mark slot borders.
        std::vector<double> gridSpokeRadians;
        std::vector<AngleTick> secondaryTicks;
        std::vector<PlottedPoint> plottedPoints;
        std::vector<PlottedColumn> plottedColumns;
        std::vector<LegendEntry> legendEntries;
        double legendBandSize = 0.0;

        // ----- Layout helpers -----
        void InvalidateLayout();
        void EnsureLayout();
        void RebuildLayout();
        void ComputeRadialRange();
        void ComputeRadialTicks();
        void ComputeAngleTicks();
        void BuildAngleTicksFor(const PolarAngleAxisStyle& style,
                                std::vector<AngleTick>& out) const;
        void BuildPlottedSeries();
        void BuildPlottedColumns();
        void BuildLegendEntries(IRenderContext* ctx);

        size_t CategorySlotCount() const;
        double CategoryAngle(size_t index) const;
        double AngleToScreenRadians(double dataAngle) const;
        double ScreenRadiansToAngle(double screenRad) const;
        double RadiusToPixels(double value) const;
        double PixelsToRadius(double pixels) const;
        double NormalizeRadial(double value) const;
        Color ResolveSeriesColor(size_t index) const;
        Color ResolveFillColor(const PolarSeries& series, size_t index) const;
        std::string FormatRadialLabel(double value) const;
        std::string FormatAngleLabel(double value) const;
        std::string BuildTooltipText(size_t seriesIndex, size_t pointIndex) const;
        // Effective points of a series in draw order, honouring angle sorting.
        std::vector<size_t> SeriesDrawOrder(const PolarSeries& series) const;

        // ----- Rendering helpers -----
        void RenderPlotBackground(IRenderContext* ctx);
        void RenderRadialBands(IRenderContext* ctx);
        void RenderAngularBands(IRenderContext* ctx);
        void RenderGridRings(IRenderContext* ctx);
        void RenderGridSpokes(IRenderContext* ctx);
        void RenderAngleAxis(IRenderContext* ctx, const PolarAngleAxisStyle& style,
                             const std::vector<AngleTick>& ticks);
        void RenderRadialAxis(IRenderContext* ctx);
        void RenderSeries(IRenderContext* ctx);
        void RenderColumns(IRenderContext* ctx);
        void RenderLineSeries(IRenderContext* ctx, size_t seriesIndex);
        void RenderMarkers(IRenderContext* ctx, size_t seriesIndex);
        void RenderValueLabelsForSeries(IRenderContext* ctx, size_t seriesIndex);
        void RenderLegend(IRenderContext* ctx);
        void RenderTitles(IRenderContext* ctx);

        void DrawRingPath(IRenderContext* ctx, double radiusPx, bool polygonal) const;
        void DrawAnnularSector(IRenderContext* ctx, double a0, double a1,
                               double r0, double r1, const Color& fill,
                               const Color& border, float borderWidth) const;
        void DrawMarker(IRenderContext* ctx, PolarMarkerShape shape,
                        const Point2Dd& pos, double size,
                        const Color& fill, const Color& stroke) const;
        void DrawRotatedText(IRenderContext* ctx, const std::string& text,
                             const Point2Dd& anchor, double rotationRad) const;
        std::vector<Point2Dd> BuildSplinePath(const std::vector<Point2Dd>& pts,
                                              bool closed) const;

        // ----- Hit testing -----
        bool HitTestPoint(const Point2Dd& localPos, size_t& seriesIndex,
                          size_t& pointIndex) const;
        size_t HitTestLegend(const Point2Dd& localPos) const;
        bool HandleClick(const UCEvent& event);

        static std::vector<Color> DefaultPalette();
        static double NiceTickSpacing(double roughSpacing);
    };

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    std::shared_ptr<UltraCanvasPolarChart> CreatePolarChart(
        const std::string& id, int x, int y, int w, int h);

    // Numeric polar scatter: each entry is one named series of (angle, radius).
    std::shared_ptr<UltraCanvasPolarChart> CreatePolarScatterChart(
        const std::string& id, int x, int y, int w, int h,
        const std::vector<std::pair<std::string, std::vector<std::pair<double, double>>>>& seriesData);

    // Categorical rose / stacked polar column chart.
    std::shared_ptr<UltraCanvasPolarChart> CreatePolarRoseChart(
        const std::string& id, int x, int y, int w, int h,
        const std::vector<std::string>& categories,
        const std::vector<std::pair<std::string, std::vector<double>>>& seriesData,
        PolarStackMode stackMode = PolarStackMode::Stacked);

} // namespace UltraCanvas

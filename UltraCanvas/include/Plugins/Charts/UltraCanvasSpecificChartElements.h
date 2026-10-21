// include/Plugins/Charts/UltraCanvasSpecificChartElements.h
// Specific chart element implementations inheriting from UltraCanvasChartElementBase
// Version: 1.1.0 - line dots follow SetPointRadius; line/area/scatter zoom and
//                  pan the x axis when enabled (off by default); bars rise from 0
// Version: 1.0.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartElementBase.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace UltraCanvas {

// =============================================================================
// LINE CHART ELEMENT
// =============================================================================

    class UltraCanvasLineChartElement : public UltraCanvasChartElementBase {
    private:
        // Line-specific properties
        Color lineColor = Color(0, 102, 204, 255);
        float lineWidth = 2.0f;
        bool showDataPoints = false;
        Color pointColor = Color(0, 102, 204, 255);
        bool enableSmoothing = false;

    public:
        UltraCanvasLineChartElement(const std::string &id, int x, int y, int width, int height)
                : UltraCanvasChartElementBase(id, x, y, width, height) {
            pointRadius = 4.0f;   // the line's dots (SetPointRadius) start a little larger
        }

        // Line chart specific configuration
        void SetLineColor(const Color &color) {
            lineColor = color;
            RequestRedraw();
        }

        void SetLineWidth(float width) {
            lineWidth = width;
            RequestRedraw();
        }

        void SetShowDataPoints(bool show) {
            showDataPoints = show;
            RequestRedraw();
        }

        void SetPointColor(const Color &color) {
            pointColor = color;
            RequestRedraw();
        }

        void SetSmoothingEnabled(bool enabled) {
            enableSmoothing = enabled;
            RequestRedraw();
        }

        void RenderChart(IRenderContext *ctx) override;

        bool HandleChartMouseMove(const Point2Di &mousePos) override;

    protected:
        bool SupportsXAxisZoom() const override { return true; }

    private:
        void DrawSmoothLine(IRenderContext *ctx, const std::vector<Point2Dd> &points);
    };

// =============================================================================
// BAR CHART ELEMENT
// =============================================================================

    // Which way the bars run. Vertical: categories along the bottom, values
    // up the left. Horizontal: one row per category, its name on the left,
    // values along the bottom - the form for "power by device" or any list
    // of named amounts whose names are longer than a bar is wide.
    enum class BarChartOrientation { Vertical, Horizontal };

    class UltraCanvasBarChartElement : public UltraCanvasChartElementBase {
    private:
        // Bar-specific properties
        Color barColor = Color(0, 102, 204, 255);
        Color barBorderColor = Color(0, 51, 102, 255);
        float barBorderWidth = 1.0f;
        float barSpacing = 0.1f; // 10% of bar width
        BarChartOrientation orientation = BarChartOrientation::Vertical;
        bool hasValueRange = false;
        double valueRangeMin = 0.0;
        double valueRangeMax = 0.0;

    public:
        UltraCanvasBarChartElement(const std::string &id, int x, int y, int width, int height)
                : UltraCanvasChartElementBase(id, x, y, width, height) {
            // Bars carry no value labels unless asked (SetShowValueLabels):
            // a long series would bury itself in them.
            showValueLabels = false;
        }

//    ChartType GetChartType() const override {
//        return ChartType::Bar;
//    }

        // Bar chart specific configuration
        void SetBarColor(const Color &color) {
            barColor = color;
            RequestRedraw();
        }

        void SetBarBorderColor(const Color &color) {
            barBorderColor = color;
            RequestRedraw();
        }

        void SetBarBorderWidth(double width) {
            barBorderWidth = width;
            RequestRedraw();
        }

        void SetBarSpacing(double spacing) {
            barSpacing = std::clamp(spacing, 0.0, 0.9);
            RequestRedraw();
        }

        void SetOrientation(BarChartOrientation newOrientation) {
            orientation = newOrientation;
            InvalidateCache();
            RequestRedraw();
        }
        BarChartOrientation GetOrientation() const { return orientation; }

        // A fixed value axis, from min to max, instead of one fitted to the
        // data: two charts side by side then compare, and a live chart does
        // not rescale every time its biggest value moves. A bar beyond the
        // range is cut at the plot's edge. min and max may come in either
        // order; an empty range is ignored.
        void SetValueRange(double min, double max) {
            if (max < min) std::swap(min, max);
            if (!(max > min)) return;
            hasValueRange = true;
            valueRangeMin = min;
            valueRangeMax = max;
            InvalidateCache();
            RequestRedraw();
        }
        // Back to a value axis fitted to the data.
        void ClearValueRange() {
            hasValueRange = false;
            InvalidateCache();
            RequestRedraw();
        }
        bool HasValueRange() const { return hasValueRange; }

        void RenderChart(IRenderContext *ctx) override;
        double GetXAxisLabelPosition(size_t dataIndex, size_t totalPoints) override;
        bool HandleChartMouseMove(const Point2Di &mousePos) override;

        // Where bar `index` is drawn, in element coordinates (for tests and
        // for a host that annotates a bar).
        Rect2Dd GetBarRect(size_t index);

    protected:
        // The value axis always includes 0, so bars rise from (or hang
        // below) the zero line; it is padded only away from zero and then
        // rounded out to the next whole tick. A value range replaces it.
        ChartDataBounds CalculateDataBounds() override;
        // Horizontal bars need room on the left for the category names.
        ChartPlotArea CalculatePlotArea() override;
        // Grid lines on the value ticks only: lines across the categories
        // would run through the bars.
        void RenderGrid(IRenderContext *ctx) override;
        void RenderAxisLabels(IRenderContext *ctx) override;
        // Eight: the axis is rounded out to a whole tick, and the finer the
        // step the less room that rounding leaves empty.
        int GetYAxisTickTarget() const override { return 8; }

    private:
        // Screen position of a value along the value axis: y when vertical,
        // x when horizontal.
        double ValueToScreen(double value) const;
        void DrawBarValueLabel(IRenderContext *ctx, const Rect2Dd &bar, double value);
    };

// =============================================================================
// SCATTER PLOT ELEMENT
// =============================================================================

    class UltraCanvasScatterPlotElement : public UltraCanvasChartElementBase {
    public:
        enum class PointShape {
            Circle, Square, Triangle, Diamond
        } pointShape = PointShape::Circle;

        enum class TrendLineStyle {
            Solid, Dashed, Dotted
        };

    private:
        // Scatter-specific properties
        Color pointColor = Color(0, 102, 204, 255);
        double pointSize = 6.0f;

        // Correlation / trend line properties
        bool showTrendLine = false;
        Color trendLineColor = Color(220, 60, 60, 255);
        float trendLineWidth = 2.0f;
        TrendLineStyle trendLineStyle = TrendLineStyle::Dashed;
        bool showCorrelationInfo = false;
        Color correlationInfoColor = Color(80, 80, 80, 255);
        float correlationInfoFontSize = 11.0f;

    public:
        UltraCanvasScatterPlotElement(const std::string &id, int x, int y, int width, int height)
                : UltraCanvasChartElementBase(id, x, y, width, height) {
            enableSelection = true;
        }

//    ChartType GetChartType() const override {
//        return ChartType::Scatter;
//    }

        // Scatter plot specific configuration
        void SetPointColor(const Color &color) {
            pointColor = color;
            RequestRedraw();
        }

        void SetPointSize(double size) {
            pointSize = size;
            RequestRedraw();
        }

        void SetPointShape(PointShape shape) {
            pointShape = shape;
            RequestRedraw();
        }

        // Correlation / trend line configuration
        void SetShowTrendLine(bool show) {
            showTrendLine = show;
            RequestRedraw();
        }

        bool GetShowTrendLine() const { return showTrendLine; }

        void SetTrendLineColor(const Color &color) {
            trendLineColor = color;
            RequestRedraw();
        }

        void SetTrendLineWidth(float width) {
            trendLineWidth = std::max(0.1f, width);
            RequestRedraw();
        }

        void SetTrendLineStyle(TrendLineStyle style) {
            trendLineStyle = style;
            RequestRedraw();
        }

        // Shows "y = ax + b" and the Pearson r / r-squared next to the plot.
        void SetShowCorrelationInfo(bool show) {
            showCorrelationInfo = show;
            RequestRedraw();
        }

        void SetCorrelationInfoColor(const Color &color) {
            correlationInfoColor = color;
            RequestRedraw();
        }

        void SetCorrelationInfoFontSize(float size) {
            correlationInfoFontSize = std::max(1.0f, size);
            RequestRedraw();
        }

        // Least-squares fit over the current data source. Returns false when
        // there are fewer than 2 points or the x values have no variance.
        bool ComputeLinearRegression(double &slope, double &intercept) const;

        // Pearson correlation coefficient r of the current data (0 when the
        // fit is undefined).
        double GetCorrelationCoefficient() const;

        void RenderChart(IRenderContext *ctx) override;

        bool HandleChartMouseMove(const Point2Di &mousePos) override;

    protected:
        bool SupportsXAxisZoom() const override { return true; }

    private:
        void RenderTrendLine(IRenderContext *ctx);
        void RenderCorrelationInfo(IRenderContext *ctx, double slope, double intercept);
    };


// =============================================================================
// AREA CHART ELEMENT
// =============================================================================

    class UltraCanvasAreaChartElement : public UltraCanvasChartElementBase {
    private:
        // Area-specific properties
        Color fillColor = Color(0, 102, 204, 128);  // Semi-transparent
        Color lineColor = Color(0, 102, 204, 255);
        double lineWidth = 2.0f;
        bool showDataPoints = false;
        Color pointColor = Color(0, 102, 204, 255);

        bool enableSmoothing = false;
        bool enableGradientFill = false;
        void* gradientFill = nullptr;
        Color gradientStartColor = Color(0, 102, 204, 200);
        Color gradientEndColor = Color(0, 102, 204, 50);

    public:
        UltraCanvasAreaChartElement(const std::string &id, int x, int y, int width, int height)
                : UltraCanvasChartElementBase(id, x, y, width, height) {
        }

//    ChartType GetChartType() const override {
//        return ChartType::Area;
//    }

        // Area chart specific configuration
        void SetFillColor(const Color &color) {
            fillColor = color;
            RequestRedraw();
        }

        void SetLineColor(const Color &color) {
            lineColor = color;
            RequestRedraw();
        }

        void SetLineWidth(double width) {
            lineWidth = width;
            RequestRedraw();
        }

        void SetShowDataPoints(bool show) {
            showDataPoints = show;
            RequestRedraw();
        }

        void SetPointColor(const Color &color) {
            pointColor = color;
            RequestRedraw();
        }

        void SetSmoothingEnabled(bool enabled) {
            enableSmoothing = enabled;
            RequestRedraw();
        }

        void SetFillGradientEnabled(bool enabled) {
            enableGradientFill = enabled;
            RequestRedraw();
        }

        void SetGradientColors(const Color &startColor, const Color &endColor) {
            gradientStartColor = startColor;
            gradientEndColor = endColor;
            if (enableGradientFill) {
                RequestRedraw();
            }
        }

        void RenderChart(IRenderContext *ctx) override;

        bool HandleChartMouseMove(const Point2Di &mousePos) override;

    protected:
        bool SupportsXAxisZoom() const override { return true; }
    };

// =============================================================================
// FACTORY FUNCTIONS - FOLLOW EXISTING ULTRACANVAS PATTERNS
// =============================================================================

// Line Chart Factory
    inline std::shared_ptr<UltraCanvasLineChartElement> CreateLineChartElement(
            const std::string &id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasLineChartElement>(id, x, y, width, height);
    }

// Bar Chart Factory
    inline std::shared_ptr<UltraCanvasBarChartElement> CreateBarChartElement(
            const std::string &id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasBarChartElement>(id, x, y, width, height);
    }

// Scatter Plot Factory
    inline std::shared_ptr<UltraCanvasScatterPlotElement> CreateScatterPlotElement(
            const std::string &id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasScatterPlotElement>(id, x, y, width, height);
    }

// Area Chart Factory
    inline std::shared_ptr<UltraCanvasAreaChartElement> CreateAreaChartElement(
            const std::string &id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasAreaChartElement>(id, x, y, width, height);
    }

} // namespace UltraCanvas


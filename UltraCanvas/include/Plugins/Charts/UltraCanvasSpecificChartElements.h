// include/Plugins/Charts/UltraCanvasSpecificChartElements.h
// Specific chart element implementations inheriting from UltraCanvasChartElementBase
// Version: 1.0.0
// Last Modified: 2025-09-10
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartElementBase.h"
#include <cmath>

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
        float pointRadius = 4.0f;
        bool enableSmoothing = false;

    public:
        UltraCanvasLineChartElement(const std::string &id, int x, int y, int width, int height)
                : UltraCanvasChartElementBase(id, x, y, width, height) {
            enableZoom = true;
            enablePan = true;
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

    private:
        void DrawSmoothLine(IRenderContext *ctx, const std::vector<Point2Dd> &points);
    };

// =============================================================================
// BAR CHART ELEMENT
// =============================================================================

    class UltraCanvasBarChartElement : public UltraCanvasChartElementBase {
    private:
        // Bar-specific properties
        Color barColor = Color(0, 102, 204, 255);
        Color barBorderColor = Color(0, 51, 102, 255);
        float barBorderWidth = 1.0f;
        float barSpacing = 0.1f; // 10% of bar width

    public:
        UltraCanvasBarChartElement(const std::string &id, int x, int y, int width, int height)
                : UltraCanvasChartElementBase(id, x, y, width, height) {
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

        void RenderChart(IRenderContext *ctx) override;
        double GetXAxisLabelPosition(size_t dataIndex, size_t totalPoints) override;
        bool HandleChartMouseMove(const Point2Di &mousePos) override;
    };

// =============================================================================
// SCATTER PLOT ELEMENT
// =============================================================================

    class UltraCanvasScatterPlotElement : public UltraCanvasChartElementBase {
    private:
        // Scatter-specific properties
        Color pointColor = Color(0, 102, 204, 255);
        double pointSize = 6.0f;

    public:
        enum class PointShape {
            Circle, Square, Triangle, Diamond
        } pointShape = PointShape::Circle;

        UltraCanvasScatterPlotElement(const std::string &id, int x, int y, int width, int height)
                : UltraCanvasChartElementBase(id, x, y, width, height) {
            enableZoom = true;
            enablePan = true;
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

        void RenderChart(IRenderContext *ctx) override;

        bool HandleChartMouseMove(const Point2Di &mousePos) override;
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
            enableZoom = true;
            enablePan = true;
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


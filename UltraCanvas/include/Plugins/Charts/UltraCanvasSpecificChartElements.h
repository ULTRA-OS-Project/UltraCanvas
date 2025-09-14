// UltraCanvasSpecificChartElements.h
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
    UltraCanvasLineChartElement(const std::string& id, long uid, int x, int y, int width, int height)
        : UltraCanvasChartElementBase(id, uid, x, y, width, height) {
        enableZoom = true;
        enablePan = true;
    }

    // Line chart specific configuration
    void SetLineColor(const Color& color) {
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

    void SetPointColor(const Color& color) {
        pointColor = color;
        RequestRedraw();
    }

    void SetPointRadius(float radius) {
        pointRadius = radius;
        RequestRedraw();
    }

    void SetSmoothingEnabled(bool enabled) {
        enableSmoothing = enabled;
        RequestRedraw();
    }

    void RenderChart(IRenderContext* ctx) override;

    bool HandleChartMouseMove(const Point2Di& mousePos) override;

private:
    void DrawSmoothLine(IRenderContext* ctx, const std::vector<Point2Df>& points);
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
    UltraCanvasBarChartElement(const std::string& id, long uid, int x, int y, int width, int height)
        : UltraCanvasChartElementBase(id, uid, x, y, width, height) {
    }

//    ChartType GetChartType() const override {
//        return ChartType::Bar;
//    }

    // Bar chart specific configuration
    void SetBarColor(const Color& color) {
        barColor = color;
        RequestRedraw();
    }

    void SetBarBorderColor(const Color& color) {
        barBorderColor = color;
        RequestRedraw();
    }

    void SetBarBorderWidth(float width) {
        barBorderWidth = width;
        RequestRedraw();
    }

    void SetBarSpacing(float spacing) {
        barSpacing = std::clamp(spacing, 0.0f, 0.9f);
        RequestRedraw();
    }

    void RenderChart(IRenderContext* ctx) override;

    bool HandleChartMouseMove(const Point2Di& mousePos) override;
};

// =============================================================================
// SCATTER PLOT ELEMENT
// =============================================================================

class UltraCanvasScatterPlotElement : public UltraCanvasChartElementBase {
private:
    // Scatter-specific properties
    Color pointColor = Color(0, 102, 204, 255);
    float pointSize = 6.0f;

public:
    enum class PointShape { Circle, Square, Triangle, Diamond } pointShape = PointShape::Circle;

    UltraCanvasScatterPlotElement(const std::string& id, long uid, int x, int y, int width, int height)
        : UltraCanvasChartElementBase(id, uid, x, y, width, height) {
        enableZoom = true;
        enablePan = true;
        enableSelection = true;
    }

//    ChartType GetChartType() const override {
//        return ChartType::Scatter;
//    }

    // Scatter plot specific configuration
    void SetPointColor(const Color& color) {
        pointColor = color;
        RequestRedraw();
    }

    void SetPointSize(float size) {
        pointSize = size;
        RequestRedraw();
    }

    void SetPointShape(PointShape shape) {
        pointShape = shape;
        RequestRedraw();
    }

    void RenderChart(IRenderContext* ctx) override;

    bool HandleChartMouseMove(const Point2Di& mousePos) override;
};

// =============================================================================
// PIE CHART ELEMENT
// =============================================================================

class UltraCanvasPieChartElement : public UltraCanvasChartElementBase {
private:
    // Pie-specific properties
    std::vector<Color> colorPalette = {
        Color(54, 162, 235, 255),   // Blue
        Color(255, 99, 132, 255),   // Red  
        Color(255, 205, 86, 255),   // Yellow
        Color(75, 192, 192, 255),   // Teal
        Color(153, 102, 255, 255),  // Purple
        Color(255, 159, 64, 255),   // Orange
        Color(199, 199, 199, 255),  // Grey
        Color(83, 102, 255, 255)    // Light Blue
    };
    Color borderColor = Color(255, 255, 255, 255);
    float borderWidth = 2.0f;

public:
    UltraCanvasPieChartElement(const std::string& id, long uid, int x, int y, int width, int height)
        : UltraCanvasChartElementBase(id, uid, x, y, width, height) {
    }

//    ChartType GetChartType() const override {
//        return ChartType::Pie;
//    }

    // Pie chart specific configuration
    void SetColorPalette(const std::vector<Color>& colors) {
        colorPalette = colors;
        RequestRedraw();
    }

    void SetBorderColor(const Color& color) {
        borderColor = color;
        RequestRedraw();
    }

    void SetBorderWidth(float width) {
        borderWidth = width;
        RequestRedraw();
    }

    void RenderChart(IRenderContext* ctx) override {
        if (!ctx || !dataSource || dataSource->GetPointCount() == 0) return;

        // Calculate total for percentages
        double total = 0.0;
        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);
            total += point.y;
        }

        if (total <= 0) return;

        // Calculate center and radius
        Point2Df center(cachedPlotArea.x + cachedPlotArea.width / 2, 
                       cachedPlotArea.y + cachedPlotArea.height / 2);
        float radius = std::min(cachedPlotArea.width, cachedPlotArea.height) / 2 * 0.8f;

        float currentAngle = 0.0f;
        const float fullCircle = 2.0f * M_PI;

        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);

            // Calculate slice angle
            float sliceAngle = static_cast<float>((point.y / total) * fullCircle);

            // Set color from palette
            Color sliceColor = GetColorFromPalette(i);
            ctx->SetFillColor(sliceColor);

            // Use existing FillArc function
            ctx->FillArc(center.x, center.y, radius, currentAngle, currentAngle + sliceAngle);

            // Draw slice border using existing DrawArc
            if (borderWidth > 0) {
                ctx->SetStrokeColor(borderColor);
                ctx->SetStrokeWidth(borderWidth);
                ctx->DrawArc(center.x, center.y, radius, currentAngle, currentAngle + sliceAngle);
            }

            currentAngle += sliceAngle;
        }
    }

    bool HandleChartMouseMove(const Point2Di& mousePos) override {
        if (!enableTooltips) {
            HideTooltip();
            return false;
        }

        // Calculate center and radius
        Point2Df center(cachedPlotArea.x + cachedPlotArea.width / 2, 
                       cachedPlotArea.y + cachedPlotArea.height / 2);
        float radius = std::min(cachedPlotArea.width, cachedPlotArea.height) / 2 * 0.8f;

        // Check if mouse is within pie radius
        float dx = mousePos.x - center.x;
        float dy = mousePos.y - center.y;
        float distance = std::sqrt(dx * dx + dy * dy);

        if (distance > radius) {
            HideTooltip();
            return false;
        }

        // Calculate angle from center
        float angle = std::atan2(dy, dx);
        if (angle < 0) angle += 2.0f * M_PI;

        // Find which slice the mouse is over
        double total = 0.0;
        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);
            total += point.y;
        }

        float currentAngle = 0.0f;
        const float fullCircle = 2.0f * M_PI;

        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);
            float sliceAngle = static_cast<float>((point.y / total) * fullCircle);

            if (angle >= currentAngle && angle <= currentAngle + sliceAngle) {
                ShowChartPointTooltip(mousePos, point, i);
                return true;
            }

            currentAngle += sliceAngle;
        }

        HideTooltip();
        return false;
    }

private:
    Color GetColorFromPalette(size_t index) {
        return colorPalette[index % colorPalette.size()];
    }
};

// =============================================================================
// AREA CHART ELEMENT
// =============================================================================

class UltraCanvasAreaChartElement : public UltraCanvasChartElementBase {
private:
    // Area-specific properties
    Color fillColor = Color(0, 102, 204, 128);  // Semi-transparent
    Color lineColor = Color(0, 102, 204, 255);
    float lineWidth = 2.0f;
    bool showDataPoints = false;
    Color pointColor = Color(0, 102, 204, 255);
    float pointRadius = 3.0f;
    bool enableSmoothing = false;
    bool enableGradientFill = false;
    Color gradientStartColor = Color(0, 102, 204, 200);
    Color gradientEndColor = Color(0, 102, 204, 50);

public:
    UltraCanvasAreaChartElement(const std::string& id, long uid, int x, int y, int width, int height)
        : UltraCanvasChartElementBase(id, uid, x, y, width, height) {
        enableZoom = true;
        enablePan = true;
    }

//    ChartType GetChartType() const override {
//        return ChartType::Area;
//    }

    // Area chart specific configuration
    void SetFillColor(const Color& color) {
        fillColor = color;
        RequestRedraw();
    }

    void SetLineColor(const Color& color) {
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

    void SetPointColor(const Color& color) {
        pointColor = color;
        RequestRedraw();
    }

    void SetPointRadius(float radius) {
        pointRadius = std::max(0.0f, radius);
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

    void SetGradientColors(const Color& startColor, const Color& endColor) {
        gradientStartColor = startColor;
        gradientEndColor = endColor;
        if (enableGradientFill) {
            RequestRedraw();
        }
    }

    void RenderChart(IRenderContext* ctx) override;

    bool HandleChartMouseMove(const Point2Di& mousePos) override;

private:
    std::vector<Point2Df> SmoothAreaPoints(const std::vector<Point2Df>& points) const {
        if (points.size() < 4 || !enableSmoothing) {
            return points;
        }

        std::vector<Point2Df> smoothed;
        smoothed.reserve(points.size() * 2);

        // Keep baseline start point
        smoothed.push_back(points[0]);

        // Smooth the data points (skip baseline points)
        for (size_t i = 1; i < points.size() - 1; ++i) {
            smoothed.push_back(points[i]);

            // Add interpolated point if not the last data point
            if (i < points.size() - 2) {
                Point2Df interpolated;
                interpolated.x = (points[i].x + points[i + 1].x) * 0.5f;
                interpolated.y = (points[i].y + points[i + 1].y) * 0.5f;
                smoothed.push_back(interpolated);
            }
        }

        // Keep baseline end point
        smoothed.push_back(points.back());

        return smoothed;
    }

    void RenderDataPoints(IRenderContext* ctx, const std::vector<Point2Df>& areaPoints) const {
        if (!showDataPoints || pointRadius <= 0.0f || areaPoints.size() < 3) {
            return;
        }

        ctx->SetFillColor(pointColor);
        ctx->SetStrokeColor(Color(255, 255, 255, 255));
        ctx->SetStrokeWidth(1.0f);

        // Draw points only for data points (skip baseline start/end)
        for (size_t i = 1; i < areaPoints.size() - 1; ++i) {
            ctx->FillCircle(areaPoints[i].x, areaPoints[i].y, pointRadius);
            ctx->DrawCircle(areaPoints[i].x, areaPoints[i].y, pointRadius);
        }
    }

    void RenderGradientFill(IRenderContext* ctx, const std::vector<Point2Df>& areaPoints) const {
        if (!enableGradientFill || areaPoints.size() < 3) {
            return;
        }

        // Find top and bottom of area for gradient direction
        float minY = areaPoints[0].y;
        float maxY = areaPoints[0].y;

        for (const auto& point : areaPoints) {
            minY = std::min(minY, point.y);
            maxY = std::max(maxY, point.y);
        }

        // Create vertical gradient from top to bottom
        ctx->SetFillGradient(gradientStartColor, gradientEndColor,
                             Point2Df(0, minY), Point2Df(0, maxY));
        ctx->FillPath(areaPoints);
    }
};

// =============================================================================
// FACTORY FUNCTIONS - FOLLOW EXISTING ULTRACANVAS PATTERNS
// =============================================================================

// Line Chart Factory
inline std::shared_ptr<UltraCanvasLineChartElement> CreateLineChartElement(
    const std::string& id, long uid, int x, int y, int width, int height) {
    return std::make_shared<UltraCanvasLineChartElement>(id, uid, x, y, width, height);
}

// Bar Chart Factory
inline std::shared_ptr<UltraCanvasBarChartElement> CreateBarChartElement(
    const std::string& id, long uid, int x, int y, int width, int height) {
    return std::make_shared<UltraCanvasBarChartElement>(id, uid, x, y, width, height);
}

// Scatter Plot Factory
inline std::shared_ptr<UltraCanvasScatterPlotElement> CreateScatterPlotElement(
    const std::string& id, long uid, int x, int y, int width, int height) {
    return std::make_shared<UltraCanvasScatterPlotElement>(id, uid, x, y, width, height);
}

//// Pie Chart Factory
//inline std::shared_ptr<UltraCanvasPieChartElement> CreatePieChartElement(
//    const std::string& id, long uid, int x, int y, int width, int height) {
//    return std::make_shared<UltraCanvasPieChartElement>(id, uid, x, y, width, height);
//}

// Area Chart Factory
inline std::shared_ptr<UltraCanvasAreaChartElement> CreateAreaChartElement(
    const std::string& id, long uid, int x, int y, int width, int height) {
    return std::make_shared<UltraCanvasAreaChartElement>(id, uid, x, y, width, height);
}
} // namespace UltraCanvas
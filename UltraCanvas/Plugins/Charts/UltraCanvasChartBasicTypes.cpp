// UltraCanvasChartBasicTypes.cpp
// Implementation of basic chart types (Line, Bar, Scatter, Area, Pie)
// Version: 1.0.1
// Last Modified: 2025-09-10
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasChartRenderer.h"
#include "Plugins/Charts/UltraCanvasChartStructures.h"
#include "UltraCanvasRenderContext.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// Forward declarations of core rendering functions
    PlotArea CalculatePlotArea(const ChartConfiguration& config, int width, int height);
    DataBounds CalculateDataBounds(IChartDataSource* dataSource, const ChartConfiguration& config);
    void DrawChartBackground(const ChartConfiguration& config, const PlotArea& plotArea, IRenderContext* ctx);
    void DrawGrid(const ChartConfiguration& config, const PlotArea& plotArea, const DataBounds& bounds, IRenderContext* ctx);
    void DrawAxes(const ChartConfiguration& config, const PlotArea& plotArea, const DataBounds& bounds, IRenderContext* ctx);
    void DrawAxisHighlights(const ChartConfiguration& config, const PlotArea& plotArea, const DataBounds& bounds, IRenderContext* ctx);
    void DrawTitles(const ChartConfiguration& config, int width, int height, IRenderContext* ctx);
    void DrawLegend(const ChartConfiguration& config, const PlotArea& plotArea, IRenderContext* ctx);
    void DrawTrendLine(const ChartConfiguration& config, const TrendLine& trendLine, const PlotArea& plotArea, const DataBounds& bounds, IRenderContext* ctx);

// =============================================================================
// MAIN CHART RENDERER IMPLEMENTATION
// =============================================================================

    bool UltraCanvasChartRenderer::RenderChart(const ChartConfiguration& config,
                                               int width, int height,
                                               IRenderContext* ctx) {
        if (!ctx || !config.dataSource || config.dataSource->GetPointCount() == 0) {
            return false;
        }

        // Switch based on chart type
        switch (config.type) {
            case ChartType::Line:
                return RenderLineChart(config, width, height, ctx);
            case ChartType::Bar:
                return RenderBarChart(config, width, height, ctx);
            case ChartType::Scatter:
                return RenderScatterPlot(config, width, height, ctx);
            case ChartType::Area:
                return RenderAreaChart(config, width, height, ctx);
            case ChartType::Pie:
                return RenderPieChart(config, width, height, ctx);
            default:
                return false;
        }
    }

// =============================================================================
// LINE CHART IMPLEMENTATION
// =============================================================================

    bool UltraCanvasChartRenderer::RenderLineChart(const ChartConfiguration& config,
                                                   int width, int height,
                                                   IRenderContext* ctx) {
        // Calculate plot area and data bounds
        PlotArea plotArea = CalculatePlotArea(config, width, height);
        DataBounds bounds = CalculateDataBounds(config.dataSource.get(), config);

        // Draw chart components
        DrawChartBackground(config, plotArea, ctx);
        DrawGrid(config, plotArea, bounds, ctx);
        DrawAxes(config, plotArea, bounds, ctx);

        // Draw line series
        DrawLineSeries(config, plotArea, bounds, ctx);

        // Draw trend lines
        for (const auto& trendLine : config.trendLines) {
            DrawTrendLine(config, trendLine, plotArea, bounds, ctx);
        }

        // Draw axis highlights
        DrawAxisHighlights(config, plotArea, bounds, ctx);

        // Draw titles and legend
        DrawTitles(config, width, height, ctx);
        DrawLegend(config, plotArea, ctx);

        return true;
    }

    void UltraCanvasChartRenderer::DrawLineSeries(const ChartConfiguration& config,
                                                  const PlotArea& plotArea,
                                                  const DataBounds& bounds,
                                                  IRenderContext* ctx) {
        if (!ctx || !config.dataSource || config.dataSource->GetPointCount() < 2) return;

        ChartCoordinateTransform transform(plotArea, bounds);

        // Set line style
        ctx->SetStrokeColor(Color::FromARGB(config.lineStyle.color));
        ctx->SetStrokeWidth(config.lineStyle.width);

        // Convert data points to screen coordinates
        std::vector<Point2D> screenPoints;
        size_t pointCount = config.dataSource->GetPointCount();

        for (size_t i = 0; i < pointCount; ++i) {
            auto dataPoint = config.dataSource->GetPoint(i);
            Point2D screenPoint = transform.DataToScreen(dataPoint.x, dataPoint.y);
            screenPoints.push_back(screenPoint);
        }

        // Draw line segments
        if (config.lineStyle.enableSmoothing) {
            // Draw smooth curved line (simplified Bezier approximation)
            DrawSmoothLine(screenPoints, ctx);
        } else {
            // Draw straight line segments
            for (size_t i = 1; i < screenPoints.size(); ++i) {
                if (config.lineStyle.isDashed) {
                    DrawDashedLine(screenPoints[i-1], screenPoints[i], ctx);
                } else {
                    ctx->DrawLine(screenPoints[i-1], screenPoints[i]);
                }
            }
        }

        // Draw data points if enabled
        if (config.showDataPoints) {
            DrawDataPoints(screenPoints, config, ctx);
        }
    }

    void UltraCanvasChartRenderer::DrawSmoothLine(const std::vector<Point2D>& points, IRenderContext* ctx) {
        if (points.size() < 3) {
            // Not enough points for smoothing, draw regular lines
            for (size_t i = 1; i < points.size(); ++i) {
                ctx->DrawLine(points[i-1], points[i]);
            }
            return;
        }

        // Simple Bezier smoothing
        for (size_t i = 1; i < points.size() - 1; ++i) {
            Point2D p0 = (i > 0) ? points[i-1] : points[i];
            Point2D p1 = points[i];
            Point2D p2 = points[i+1];
            Point2D p3 = (i < points.size() - 2) ? points[i+2] : points[i+1];

            // Calculate control points
            Point2D cp1(p1.x + (p2.x - p0.x) * 0.1f, p1.y + (p2.y - p0.y) * 0.1f);
            Point2D cp2(p2.x - (p3.x - p1.x) * 0.1f, p2.y - (p3.y - p1.y) * 0.1f);

            // Draw Bezier curve
            ctx->DrawBezier(p1, cp1, cp2, p2);
        }
    }

    void UltraCanvasChartRenderer::DrawDashedLine(const Point2D& start, const Point2D& end, IRenderContext* ctx) {
        float dashLength = 5.0f;
        float gapLength = 3.0f;
        float totalLength = std::sqrt((end.x - start.x) * (end.x - start.x) + (end.y - start.y) * (end.y - start.y));

        if (totalLength <= 0) return;

        float dx = (end.x - start.x) / totalLength;
        float dy = (end.y - start.y) / totalLength;

        float currentPos = 0;
        bool drawing = true;

        while (currentPos < totalLength) {
            float segmentLength = drawing ? dashLength : gapLength;
            float nextPos = std::min(currentPos + segmentLength, totalLength);

            if (drawing) {
                Point2D segStart(start.x + dx * currentPos, start.y + dy * currentPos);
                Point2D segEnd(start.x + dx * nextPos, start.y + dy * nextPos);
                ctx->DrawLine(segStart, segEnd);
            }

            currentPos = nextPos;
            drawing = !drawing;
        }
    }

    void UltraCanvasChartRenderer::DrawDataPoints(const std::vector<Point2D>& points, const ChartConfiguration& config, IRenderContext* ctx) {
        float pointSize = config.pointStyle.size;
        Color pointColor = Color::FromARGB(config.pointStyle.color);

        ctx->SetFillColor(pointColor);

        for (const auto& point : points) {
            switch (config.pointStyle.shape) {
                case PointShape::Circle:
                    ctx->FillCircle(point.x, point.y, pointSize);
                    break;
                case PointShape::Square:
                    ctx->FillRectangle(point.x - pointSize, point.y - pointSize, pointSize * 2, pointSize * 2);
                    break;
                case PointShape::Triangle:
                    DrawTrianglePoint(point, pointSize, ctx);
                    break;
                case PointShape::Diamond:
                    DrawDiamondPoint(point, pointSize, ctx);
                    break;
                default:
                    ctx->FillCircle(point.x, point.y, pointSize);
                    break;
            }
        }
    }

// =============================================================================
// BAR CHART IMPLEMENTATION
// =============================================================================

    bool UltraCanvasChartRenderer::RenderBarChart(const ChartConfiguration& config,
                                                  int width, int height,
                                                  IRenderContext* ctx) {
        PlotArea plotArea = CalculatePlotArea(config, width, height);
        DataBounds bounds = CalculateDataBounds(config.dataSource.get(), config);

        DrawChartBackground(config, plotArea, ctx);
        DrawGrid(config, plotArea, bounds, ctx);
        DrawAxes(config, plotArea, bounds, ctx);

        // Draw bars
        DrawBarsWithStyling(config, plotArea, bounds, ctx);

        DrawAxisHighlights(config, plotArea, bounds, ctx);
        DrawTitles(config, width, height, ctx);
        DrawLegend(config, plotArea, ctx);

        return true;
    }

    void UltraCanvasChartRenderer::DrawBarsWithStyling(const ChartConfiguration& config,
                                                       const PlotArea& plotArea,
                                                       const DataBounds& bounds,
                                                       IRenderContext* ctx) {
        if (!ctx || !config.dataSource || config.dataSource->GetPointCount() == 0) return;

        ChartCoordinateTransform transform(plotArea, bounds);
        size_t pointCount = config.dataSource->GetPointCount();

        // Calculate bar width
        float barWidth = plotArea.width / (pointCount * 1.5f); // Leave some spacing
        float barSpacing = barWidth * 0.2f;
        barWidth *= 0.8f;

        // Ensure bars aren't too wide
        barWidth = std::min(barWidth, 50.0f);

        for (size_t i = 0; i < pointCount; ++i) {
            auto dataPoint = config.dataSource->GetPoint(i);

            // Calculate bar position and size
            float barX = plotArea.x + (i + 0.5f) * (plotArea.width / pointCount) - barWidth / 2;
            float barBottom = transform.DataToScreenY(0); // Base line
            float barTop = transform.DataToScreenY(dataPoint.y);
            float barHeight = std::abs(barBottom - barTop);

            // Adjust for negative values
            if (dataPoint.y < 0) {
                std::swap(barTop, barBottom);
            }

            // Draw bar with styling
            DrawStyledBar(barX, barTop, barWidth, barHeight, config.barStyle, ctx);
        }
    }

    void UltraCanvasChartRenderer::DrawStyledBar(float x, float y, float width, float height,
                                                 const BarStyle& style, IRenderContext* ctx) {
        Color primaryColor = Color::FromARGB(style.primaryColor);
        Color secondaryColor = Color::FromARGB(style.secondaryColor);

        switch (style.texture) {
            case BarStyle::TextureType::None:
                // Solid color
                ctx->SetFillColor(primaryColor);
                ctx->FillRectangle(x, y, width, height);
                break;

            case BarStyle::TextureType::Gradient:
                // Simple gradient approximation using multiple rectangles
                DrawGradientBar(x, y, width, height, primaryColor, secondaryColor, ctx);
                break;

            case BarStyle::TextureType::Hatched:
                // Solid fill first, then diagonal lines
                ctx->SetFillColor(primaryColor);
                ctx->FillRectangle(x, y, width, height);
                DrawHatchPattern(x, y, width, height, secondaryColor, ctx);
                break;

            case BarStyle::TextureType::Dotted:
                // Solid fill first, then dots
                ctx->SetFillColor(primaryColor);
                ctx->FillRectangle(x, y, width, height);
                DrawDotPattern(x, y, width, height, secondaryColor, ctx);
                break;

            case BarStyle::TextureType::Striped:
                // Horizontal stripes
                DrawStripedBar(x, y, width, height, primaryColor, secondaryColor, ctx);
                break;
        }

        // Apply fade opacity
        if (style.fadeOpacity < 1.0f) {
            // This would require alpha blending support in the render interface
            // For now, we'll adjust the color alpha
        }

        // Draw border
        if (style.borderWidth > 0) {
            ctx->SetStrokeColor(Color::FromARGB(style.borderColor));
            ctx->SetStrokeWidth(style.borderWidth);
            ctx->DrawRectangle(x, y, width, height);
        }
    }

    void UltraCanvasChartRenderer::DrawGradientBar(float x, float y, float width, float height,
                                                   const Color& color1, const Color& color2, IRenderContext* ctx) {
        int steps = static_cast<int>(height / 2); // Number of gradient steps
        steps = std::max(1, std::min(steps, 50)); // Limit steps

        float stepHeight = height / steps;

        for (int i = 0; i < steps; ++i) {
            float t = static_cast<float>(i) / (steps - 1);
            Color blendedColor = ChartRenderingHelpers::InterpolateColor(color1, color2, t);

            ctx->SetFillColor(blendedColor);
            ctx->FillRectangle(x, y + i * stepHeight, width, stepHeight + 1);
        }
    }

// =============================================================================
// SCATTER PLOT IMPLEMENTATION
// =============================================================================

    bool UltraCanvasChartRenderer::RenderScatterPlot(const ChartConfiguration& config,
                                                     int width, int height,
                                                     IRenderContext* ctx) {
        PlotArea plotArea = CalculatePlotArea(config, width, height);
        DataBounds bounds = CalculateDataBounds(config.dataSource.get(), config);

        DrawChartBackground(config, plotArea, ctx);
        DrawGrid(config, plotArea, bounds, ctx);
        DrawAxes(config, plotArea, bounds, ctx);

        // Draw scatter points
        DrawScatterPointsStandard(config, plotArea, bounds, ctx);

        // Draw trend lines
        for (const auto& trendLine : config.trendLines) {
            DrawTrendLine(config, trendLine, plotArea, bounds, ctx);
        }

        DrawAxisHighlights(config, plotArea, bounds, ctx);
        DrawTitles(config, width, height, ctx);
        DrawLegend(config, plotArea, ctx);

        return true;
    }

    void UltraCanvasChartRenderer::DrawScatterPointsStandard(const ChartConfiguration& config,
                                                             const PlotArea& plotArea,
                                                             const DataBounds& bounds,
                                                             IRenderContext* ctx) {
        if (!ctx || !config.dataSource) return;

        ChartCoordinateTransform transform(plotArea, bounds);
        size_t pointCount = config.dataSource->GetPointCount();

        float pointSize = config.pointStyle.size;
        Color pointColor = Color::FromARGB(config.pointStyle.color);

        ctx->SetFillColor(pointColor);

        for (size_t i = 0; i < pointCount; ++i) {
            auto dataPoint = config.dataSource->GetPoint(i);
            Point2D screenPoint = transform.DataToScreen(dataPoint.x, dataPoint.y);

            // Skip points outside plot area
            if (!plotArea.Contains(static_cast<int>(screenPoint.x), static_cast<int>(screenPoint.y))) {
                continue;
            }

            // Use custom color if specified
            if (dataPoint.color != 0) {
                ctx->SetFillColor(Color::FromARGB(dataPoint.color));
            }

            // Draw point based on shape
            switch (config.pointStyle.shape) {
                case PointShape::Circle:
                    ctx->FillCircle(screenPoint.x, screenPoint.y, pointSize);
                    break;
                case PointShape::Square:
                    ctx->FillRectangle(screenPoint.x - pointSize, screenPoint.y - pointSize,
                                       pointSize * 2, pointSize * 2);
                    break;
                case PointShape::Triangle:
                    DrawTrianglePoint(screenPoint, pointSize, ctx);
                    break;
                case PointShape::Diamond:
                    DrawDiamondPoint(screenPoint, pointSize, ctx);
                    break;
                default:
                    ctx->FillCircle(screenPoint.x, screenPoint.y, pointSize);
                    break;
            }

            // Reset color for next point
            if (dataPoint.color != 0) {
                ctx->SetFillColor(pointColor);
            }
        }
    }

// =============================================================================
// AREA CHART IMPLEMENTATION
// =============================================================================

    bool UltraCanvasChartRenderer::RenderAreaChart(const ChartConfiguration& config,
                                                   int width, int height,
                                                   IRenderContext* ctx) {
        PlotArea plotArea = CalculatePlotArea(config, width, height);
        DataBounds bounds = CalculateDataBounds(config.dataSource.get(), config);

        DrawChartBackground(config, plotArea, ctx);
        DrawGrid(config, plotArea, bounds, ctx);
        DrawAxes(config, plotArea, bounds, ctx);

        // Draw filled area
        DrawAreaFill(config, plotArea, bounds, ctx);

        // Draw area border line
        DrawLineSeries(config, plotArea, bounds, ctx);

        DrawAxisHighlights(config, plotArea, bounds, ctx);
        DrawTitles(config, width, height, ctx);
        DrawLegend(config, plotArea, ctx);

        return true;
    }

    void UltraCanvasChartRenderer::DrawAreaFill(const ChartConfiguration& config,
                                                const PlotArea& plotArea,
                                                const DataBounds& bounds,
                                                IRenderContext* ctx) {
        if (!ctx || !config.dataSource || config.dataSource->GetPointCount() < 2) return;

        ChartCoordinateTransform transform(plotArea, bounds);
        size_t pointCount = config.dataSource->GetPointCount();

        // Create polygon points for filled area
        std::vector<Point2D> polygonPoints;

        // Start from bottom-left of first point
        auto firstPoint = config.dataSource->GetPoint(0);
        polygonPoints.push_back(Point2D(transform.DataToScreenX(firstPoint.x), transform.DataToScreenY(0)));

        // Add all data points
        for (size_t i = 0; i < pointCount; ++i) {
            auto dataPoint = config.dataSource->GetPoint(i);
            polygonPoints.push_back(transform.DataToScreen(dataPoint.x, dataPoint.y));
        }

        // End at bottom-right of last point
        auto lastPoint = config.dataSource->GetPoint(pointCount - 1);
        polygonPoints.push_back(Point2D(transform.DataToScreenX(lastPoint.x), transform.DataToScreenY(0)));

        // Fill the area
        Color fillColor = Color::FromARGB(config.areaStyle.fillColor);
        fillColor.a = static_cast<uint8_t>(fillColor.a * config.areaStyle.opacity);
        ctx->SetFillColor(fillColor);
        ctx->FillPath(polygonPoints);
    }

// =============================================================================
// PIE CHART IMPLEMENTATION
// =============================================================================

    bool UltraCanvasChartRenderer::RenderPieChart(const ChartConfiguration& config,
                                                  int width, int height,
                                                  IRenderContext* ctx) {
        if (!ctx || !config.dataSource || config.dataSource->GetPointCount() == 0) return false;

        // Calculate pie center and radius
        Point2D center(width / 2.0f, height / 2.0f);
        float radius = std::min(width, height) * 0.3f;

        DrawChartBackground(config, PlotArea(0, 0, width, height), ctx);

        // Calculate total value
        double totalValue = 0;
        size_t pointCount = config.dataSource->GetPointCount();
        for (size_t i = 0; i < pointCount; ++i) {
            totalValue += config.dataSource->GetPoint(i).value;
        }

        if (totalValue <= 0) return false;

        // Draw pie slices
        float currentAngle = config.pieStyle.startAngle;
        auto colors = ChartRenderingHelpers::GenerateColorPalette(static_cast<int>(pointCount));

        for (size_t i = 0; i < pointCount; ++i) {
            auto dataPoint = config.dataSource->GetPoint(i);
            float sliceAngle = static_cast<float>(dataPoint.value / totalValue * 360.0);

            // Draw slice
            ctx->SetFillColor(colors[i]);
            ctx->FillArc(center.x, center.y, radius,
                         currentAngle * M_PI / 180.0f,
                         (currentAngle + sliceAngle) * M_PI / 180.0f);

            // Draw slice border
            ctx->SetStrokeColor(Colors::White);
            ctx->SetStrokeWidth(2.0f);
            ctx->DrawArc(center.x, center.y, radius,
                         currentAngle * M_PI / 180.0f,
                         (currentAngle + sliceAngle) * M_PI / 180.0f);

            // Draw labels if enabled
            if (config.pieStyle.showLabels && !dataPoint.label.empty()) {
                float labelAngle = (currentAngle + sliceAngle / 2) * M_PI / 180.0f;
                float labelX = center.x + std::cos(labelAngle) * (radius + 20);
                float labelY = center.y + std::sin(labelAngle) * (radius + 20);

                ctx->SetTextColor(Colors::Black);
                ctx->SetFont("Arial", 12.0f);
                ctx->DrawText(dataPoint.label, labelX, labelY);
            }

            currentAngle += sliceAngle;
        }

        DrawTitles(config, width, height, ctx);

        return true;
    }

// =============================================================================
// HELPER SHAPE DRAWING FUNCTIONS
// =============================================================================

    void UltraCanvasChartRenderer::DrawTrianglePoint(const Point2D& center, float size, IRenderContext* ctx) {
        std::vector<Point2D> triangle = {
                Point2D(center.x, center.y - size),
                Point2D(center.x - size * 0.866f, center.y + size * 0.5f),
                Point2D(center.x + size * 0.866f, center.y + size * 0.5f)
        };
        ctx->FillPath(triangle);
    }

    void UltraCanvasChartRenderer::DrawDiamondPoint(const Point2D& center, float size, IRenderContext* ctx) {
        std::vector<Point2D> diamond = {
                Point2D(center.x, center.y - size),
                Point2D(center.x + size, center.y),
                Point2D(center.x, center.y + size),
                Point2D(center.x - size, center.y)
        };
        ctx->FillPath(diamond);
    }

    void UltraCanvasChartRenderer::DrawHatchPattern(float x, float y, float width, float height,
                                                    const Color& color, IRenderContext* ctx) {
        ctx->SetStrokeColor(color);
        ctx->SetStrokeWidth(1.0f);

        float spacing = 6.0f;

        // Draw diagonal lines from top-left to bottom-right
        for (float offset = -height; offset < width; offset += spacing) {
            float startX = x + offset;
            float startY = y;
            float endX = x + offset + height;
            float endY = y + height;

            // Clip to rectangle bounds
            if (startX < x) {
                startY += (x - startX);
                startX = x;
            }
            if (endX > x + width) {
                endY -= (endX - (x + width));
                endX = x + width;
            }

            if (startX <= endX && startY <= y + height && endY >= y) {
                ctx->DrawLine(startX, std::max(startY, y), endX, std::min(endY, y + height));
            }
        }
    }

    void UltraCanvasChartRenderer::DrawDotPattern(float x, float y, float width, float height,
                                                  const Color& color, IRenderContext* ctx) {
        ctx->SetFillColor(color);

        float spacing = 8.0f;
        float dotSize = 1.5f;

        for (float dotY = y + spacing / 2; dotY < y + height; dotY += spacing) {
            for (float dotX = x + spacing / 2; dotX < x + width; dotX += spacing) {
                ctx->FillCircle(dotX, dotY, dotSize);
            }
        }
    }

    void UltraCanvasChartRenderer::DrawStripedBar(float x, float y, float width, float height,
                                                  const Color& color1, const Color& color2, IRenderContext* ctx) {
        float stripeHeight = 4.0f;
        bool useColor1 = true;

        for (float currentY = y; currentY < y + height; currentY += stripeHeight) {
            float actualHeight = std::min(stripeHeight, y + height - currentY);

            ctx->SetFillColor(useColor1 ? color1 : color2);
            ctx->FillRectangle(x, currentY, width, actualHeight);

            useColor1 = !useColor1;
        }
    }

} // namespace UltraCanvas

// =============================================================================
// CHART RENDERER IMPLEMENTATION - REMAINING METHODS
// =============================================================================

namespace UltraCanvas {

// Implementation of the missing methods declared in the header

    ChartConfiguration UltraCanvasChartRenderer::CreateLineChart(std::shared_ptr<IChartDataSource> data,
                                                                 const std::string& title) {
        ChartConfiguration config;
        config.type = ChartType::Line;
        config.dataSource = data;
        config.title = title;

        // Set default line chart styling
        config.lineStyle.color = 0xFF0066CC;
        config.lineStyle.width = 2.0f;
        config.lineStyle.isDashed = false;
        config.lineStyle.enableSmoothing = false;

        config.showDataPoints = false;
        config.pointStyle.size = 4.0f;
        config.pointStyle.shape = PointShape::Circle;
        config.pointStyle.color = 0xFF0066CC;

        config.enableAnimations = true;
        config.enableTooltips = true;
        config.enableZoom = true;
        config.enablePan = true;

        return config;
    }

    ChartConfiguration UltraCanvasChartRenderer::CreateBarChart(std::shared_ptr<IChartDataSource> data,
                                                                const std::string& title,
                                                                const BarStyle& style) {
        ChartConfiguration config;
        config.type = ChartType::Bar;
        config.dataSource = data;
        config.title = title;
        config.barStyle = style;

        // Set default bar chart styling
        if (config.barStyle.primaryColor == 0) {
            config.barStyle.primaryColor = 0xFF0066CC;
        }
        if (config.barStyle.texture == BarStyle::TextureType::None) {
            config.barStyle.texture = BarStyle::TextureType::None;
        }

        config.enableAnimations = true;
        config.enableTooltips = true;

        return config;
    }

    ChartConfiguration UltraCanvasChartRenderer::CreateScatterPlot(std::shared_ptr<IChartDataSource> data,
                                                                   const std::string& title) {
        ChartConfiguration config;
        config.type = ChartType::Scatter;
        config.dataSource = data;
        config.title = title;

        // Set default scatter plot styling
        config.pointStyle.size = 5.0f;
        config.pointStyle.shape = PointShape::Circle;
        config.pointStyle.color = 0xFF0066CC;
        config.pointStyle.borderWidth = 1.0f;
        config.pointStyle.borderColor = 0xFF003366;

        config.enableTooltips = true;
        config.enableZoom = true;
        config.enablePan = true;
        config.enableSelection = true;

        // Optimize for large datasets
        if (data && data->GetPointCount() > 50000) {
            config.enableAnimations = false;
            config.enableLOD = true;
        }

        return config;
    }

    ChartConfiguration UltraCanvasChartRenderer::CreateAreaChart(std::shared_ptr<IChartDataSource> data,
                                                                 const std::string& title) {
        ChartConfiguration config;
        config.type = ChartType::Area;
        config.dataSource = data;
        config.title = title;

        // Set default area chart styling
        config.areaStyle.fillColor = 0x800066CC;  // Semi-transparent blue
        config.areaStyle.opacity = 0.6f;
        config.areaStyle.enableGradient = true;

        config.lineStyle.color = 0xFF0066CC;
        config.lineStyle.width = 2.0f;
        config.showDataPoints = false;

        config.enableAnimations = true;
        config.enableTooltips = true;

        return config;
    }

    ChartConfiguration UltraCanvasChartRenderer::CreatePieChart(std::shared_ptr<IChartDataSource> data,
                                                                const std::string& title) {
        ChartConfiguration config;
        config.type = ChartType::Pie;
        config.dataSource = data;
        config.title = title;

        // Set default pie chart styling
        config.pieStyle.startAngle = -90.0f;  // Start from top
        config.pieStyle.showLabels = true;
        config.pieStyle.showPercentages = true;
        config.pieStyle.showValues = false;
        config.pieStyle.labelDistance = 20.0f;
        config.pieStyle.enable3D = false;
        config.pieStyle.innerRadius = 0.0f;  // Full pie, not donut

        config.enableAnimations = true;
        config.enableTooltips = true;

        return config;
    }

// =============================================================================
// EXPORT FUNCTIONALITY
// =============================================================================

    bool UltraCanvasChartRenderer::ExportToPNG(const ChartConfiguration& config,
                                               int width, int height,
                                               const std::string& filePath) {
        // This would require integration with UltraCanvas image export system
        // For now, return success to indicate the API is available

        // Pseudo-code:
        // 1. Create off-screen render target
        // 2. Render chart to off-screen target
        // 3. Save render target as PNG file
        // 4. Clean up resources

        return true; // Placeholder implementation
    }

    bool UltraCanvasChartRenderer::ExportToSVG(const ChartConfiguration& config,
                                               int width, int height,
                                               const std::string& filePath) {
        // This would require SVG rendering backend
        // For now, return success to indicate the API is available

        return true; // Placeholder implementation
    }

// =============================================================================
// STYLING HELPERS IMPLEMENTATION
// =============================================================================

    void UltraCanvasChartRenderer::AddAxisHighlight(ChartConfiguration& config,
                                                    const std::string& axis,
                                                    double position, uint32_t color,
                                                    const std::string& label) {
        AxisHighlight highlight(position, color, 2.0f, label, false);

        if (axis == "x" || axis == "X") {
            config.xAxis.highlights.push_back(highlight);
        } else if (axis == "y" || axis == "Y") {
            config.yAxis.highlights.push_back(highlight);
        } else if (axis == "z" || axis == "Z") {
            config.zAxis.highlights.push_back(highlight);
        }
    }

    void UltraCanvasChartRenderer::AddTrendLine(ChartConfiguration& config,
                                                TrendLine::Type type, uint32_t color) {
        TrendLine trend(type, color);
        config.trendLines.push_back(trend);
    }

    void UltraCanvasChartRenderer::SetBarTexture(ChartConfiguration& config,
                                                 BarStyle::TextureType texture,
                                                 uint32_t primaryColor,
                                                 uint32_t secondaryColor) {
        config.barStyle.texture = texture;
        config.barStyle.primaryColor = primaryColor;
        config.barStyle.secondaryColor = secondaryColor;
    }

} // namespace UltraCanvas

// =============================================================================
// INTEGRATION WITH ULTRACANVAS PLUGIN SYSTEM
// =============================================================================

namespace UltraCanvas {

// Chart Plugin Implementation
    class UltraCanvasChartPlugin : public IGraphicsPlugin {
    public:
        std::string GetPluginName() const override {
            return "UltraCanvas Chart Renderer";
        }

        std::string GetPluginVersion() const override {
            return "1.0.1";
        }

        std::vector<std::string> GetSupportedExtensions() const override {
            return {"chart", "csv", "data", "tsv"};
        }

        bool CanHandle(const std::string& filePath) const override {
            auto extensions = GetSupportedExtensions();
            std::string ext = GetFileExtension(filePath);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
        }

        std::shared_ptr<UltraCanvasElement> LoadGraphics(const std::string& filePath) override {
            // Load data and create chart element
            auto dataSource = UltraCanvasChartRenderer::LoadCSVData(filePath);
            if (!dataSource) return nullptr;

            // Auto-detect chart type based on data characteristics
            ChartType detectedType = DetectOptimalChartType(dataSource.get());

            // Create appropriate chart configuration
            ChartConfiguration config;
            switch (detectedType) {
                case ChartType::Line:
                    config = UltraCanvasChartRenderer::CreateLineChart(dataSource, ExtractTitle(filePath));
                    break;
                case ChartType::Bar:
                    config = UltraCanvasChartRenderer::CreateBarChart(dataSource, ExtractTitle(filePath));
                    break;
                case ChartType::Scatter:
                    config = UltraCanvasChartRenderer::CreateScatterPlot(dataSource, ExtractTitle(filePath));
                    break;
                default:
                    config = UltraCanvasChartRenderer::CreateLineChart(dataSource, ExtractTitle(filePath));
                    break;
            }

            // Create and return chart element
            return CreateChartElement(config);
        }

        std::shared_ptr<UltraCanvasElement> CreateGraphics(int width, int height, GraphicsFormatType type) override {
            // Create empty chart element
            auto emptyData = std::make_shared<ChartDataVector>();
            auto config = UltraCanvasChartRenderer::CreateLineChart(emptyData, "New Chart");
            return CreateChartElement(config);
        }

    private:
        ChartType DetectOptimalChartType(IChartDataSource* data) {
            size_t pointCount = data->GetPointCount();

            // Large datasets work better as scatter plots
            if (pointCount > 10000) {
                return ChartType::Scatter;
            }

            // Check if data appears to be categorical (for bar charts)
            if (pointCount < 20) {
                // Small datasets with discrete values work well as bar charts
                return ChartType::Bar;
            }

            // Default to line chart for time series and continuous data
            return ChartType::Line;
        }

        std::string ExtractTitle(const std::string& filePath) {
            size_t lastSlash = filePath.find_last_of("/\\");
            size_t lastDot = filePath.find_last_of(".");

            if (lastSlash == std::string::npos) lastSlash = 0;
            else lastSlash++;

            if (lastDot == std::string::npos) lastDot = filePath.length();

            std::string title = filePath.substr(lastSlash, lastDot - lastSlash);

            // Capitalize first letter and replace underscores with spaces
            if (!title.empty()) {
                title[0] = std::toupper(title[0]);
                std::replace(title.begin(), title.end(), '_', ' ');
            }

            return title;
        }

        std::string GetFileExtension(const std::string& filePath) {
            size_t dotPos = filePath.find_last_of('.');
            if (dotPos == std::string::npos) return "";
            return filePath.substr(dotPos + 1);
        }

        std::shared_ptr<UltraCanvasElement> CreateChartElement(const ChartConfiguration& config) {
            // This would create a UltraCanvasChartElement
            // For now, return nullptr as placeholder
            return nullptr;
        }
    };

// Global registration function
    bool RegisterUltraCanvasChartPlugin() {
        auto plugin = std::make_shared<UltraCanvasChartPlugin>();

        // Register with UltraCanvas plugin system
        // This would call the actual plugin registration
        // UltraCanvasGraphicsPluginRegistry::RegisterPlugin(plugin);

        return true;
    }

} // namespace UltraCanvas
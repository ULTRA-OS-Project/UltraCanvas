// Plugins/Charts/UltraCanvasSpecificChartElements.cpp
// Specific chart element implementations inheriting from UltraCanvasChartElementBase
// Version: 1.0.0
// Last Modified: 2025-09-10
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasSpecificChartElements.h"

namespace UltraCanvas {
    void UltraCanvasLineChartElement::RenderChart(IRenderContext *ctx) {
        if (!ctx || !dataSource || dataSource->GetPointCount() < 2) return;

        ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);

        // Set line style using existing functions
        ctx->SetStrokeColor(lineColor);
        ctx->SetStrokeWidth(lineWidth);

        // Build path points
        std::vector<Point2Df> linePoints;
        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);
            auto screenPos = transform.DataToScreen(point.x, point.y);
            linePoints. push_back(screenPos);
        }

// Draw line using existing DrawPath function
        if (linePoints.size() > 1) {
            if (enableSmoothing) {
                DrawSmoothLine(ctx, linePoints);
            } else {
                ctx->DrawPath(linePoints, false);
            }
        }

// Draw data points if enabled using existing FillCircle
        if (showDataPoints) {
            ctx->SetFillColor(pointColor);
            for (const auto &point : linePoints) {
                ctx->FillCircle(point.x, point.y, pointRadius);
            }
        }
    }

    void UltraCanvasLineChartElement::DrawSmoothLine(IRenderContext *ctx, const std::vector<Point2Df> &points) {
        // Simple smoothing using bezier curves
        for (size_t i = 0; i < points.size() - 1; ++i) {
            if (i + 2 < points.size()) {
                // Create smooth curve using existing DrawBezier
                Point2Df cp1 = Point2Df(
                        points[i].x + (points[i+1].x - points[i].x) * 0.3f,
                        points[i].y + (points[i+1].y - points[i].y) * 0.3f
                );
                Point2Df cp2 = Point2Df(
                        points[i+1].x - (points[i+2].x - points[i+1].x) * 0.3f,
                        points[i+1].y - (points[i+2].y - points[i+1].y) * 0.3f
                );
                ctx->DrawBezier(points[i], cp1, cp2, points[i+1]);
            } else {
                ctx->DrawLine(points[i].x, points[i].y, points[i+1].x, points[i+1].y);
            }
        }
    }

    bool UltraCanvasLineChartElement::HandleChartMouseMove(const Point2Di &mousePos) {
        if (!enableTooltips) {
            HideTooltip();
            return false;
        }

        // Find nearest data point
        ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
        double mouseDataX = transform.ScreenToDataX(mousePos.x - GetX());
        double mouseDataY = transform.ScreenToDataY(mousePos.y - GetY());
        double pointSize = 5;
        size_t nearestIndex = SIZE_MAX;
        double nearestDistance = std::numeric_limits<double>::max();

        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);
            auto screenPos = transform.DataToScreen(point.x, point.y);

            // Check if mouse is within point radius
            double distance = std::sqrt((screenPos.x - mousePos.x) * (screenPos.x - mousePos.x) +
                                        (screenPos.y - mousePos.y) * (screenPos.y - mousePos.y));

            if (distance < pointSize * 2 && distance < nearestDistance) {
                nearestDistance = distance;
                nearestIndex = i;
            }
        }

        if (nearestIndex != SIZE_MAX) {
            auto point = dataSource->GetPoint(nearestIndex);
            ShowChartPointTooltip(mousePos, point, nearestIndex);
            return true;
        } else {
            HideTooltip();
            return false;
        }
    }


    // UltraCanvasBarChartElement
    void UltraCanvasBarChartElement::RenderChart(IRenderContext *ctx) {
        if (!ctx || !dataSource || dataSource->GetPointCount() == 0) return;

        ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
        float barWidth = cachedPlotArea.width / static_cast<float>(dataSource->GetPointCount());
        float actualBarSpacing = barWidth * barSpacing;
        float actualBarWidth = barWidth - actualBarSpacing;

        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);

            // Calculate bar dimensions
            float barX = cachedPlotArea.x + (i * barWidth) + (actualBarSpacing / 2);
            auto topPos = transform.DataToScreen(point.x, point.y);
            auto bottomPos = transform.DataToScreen(point.x, cachedDataBounds.minY);

            float barHeight = bottomPos.y - topPos.y;
            if (barHeight < 0) {
                barHeight = -barHeight;
                topPos.y = bottomPos.y;
            }

            // Use existing FillRectangle function
            ctx->SetFillColor(barColor);
            ctx->FillRectangle(barX, topPos.y, actualBarWidth, barHeight);

            // Use existing DrawRectangle for border
            if (barBorderWidth > 0) {
                ctx->SetStrokeColor(barBorderColor);
                ctx->SetStrokeWidth(barBorderWidth);
                ctx->DrawRectangle(barX, topPos.y, actualBarWidth, barHeight);
            }
        }
    }

    bool UltraCanvasBarChartElement::HandleChartMouseMove(const Point2Di &mousePos) {
        if (!enableTooltips) {
            HideTooltip();
            return false;
        }

        // Find which bar the mouse is over
        if (mousePos.x < cachedPlotArea.x || mousePos.x > cachedPlotArea.x + cachedPlotArea.width ||
            mousePos.y < cachedPlotArea.y || mousePos.y > cachedPlotArea.y + cachedPlotArea.height) {
            HideTooltip();
            return false;
        }

        float barWidth = cachedPlotArea.width / static_cast<float>(dataSource->GetPointCount());
        size_t barIndex = static_cast<size_t>((mousePos.x - cachedPlotArea.x) / barWidth);

        if (barIndex < dataSource->GetPointCount()) {
            auto point = dataSource->GetPoint(barIndex);
            ShowChartPointTooltip(mousePos, point, barIndex);
            return true;
        } else {
            HideTooltip();
            return false;
        }
        return false;
    }


    // UltraCanvasScatterPlotElement
    void UltraCanvasScatterPlotElement::RenderChart(IRenderContext *ctx) {
        if (!ctx || !dataSource || dataSource->GetPointCount() == 0) return;

        ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
        ctx->SetFillColor(pointColor);

        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);
            auto screenPos = transform.DataToScreen(point.x, point.y);

            // Determine point size (could be data-driven)
            float currentPointSize = pointSize;
//  Fixme! implement actual point with 'point.size'
//            if (point.size.has_value()) {
//                currentPointSize = static_cast<float>(point.size.value() * pointSize);
//            }
            currentPointSize = 2 * pointSize;

            // Draw point based on shape using existing functions
            switch (pointShape) {
                case PointShape::Circle:
                    ctx->FillCircle(screenPos.x, screenPos.y, currentPointSize);
                    break;
                case PointShape::Square: {
                    float halfSize = currentPointSize / 2;
                    ctx->FillRectangle(screenPos.x - halfSize, screenPos.y - halfSize,
                                       currentPointSize, currentPointSize);
                    break;
                }
                case PointShape::Triangle: {
                    std::vector<Point2Df> trianglePoints = {
                            Point2Df(screenPos.x, screenPos.y - currentPointSize),
                            Point2Df(screenPos.x - currentPointSize * 0.866f, screenPos.y + currentPointSize * 0.5f),
                            Point2Df(screenPos.x + currentPointSize * 0.866f, screenPos.y + currentPointSize * 0.5f)
                    };
                    ctx->FillPath(trianglePoints);
                    break;
                }
                case PointShape::Diamond: {
                    std::vector<Point2Df> diamondPoints = {
                            Point2Df(screenPos.x, screenPos.y - currentPointSize),
                            Point2Df(screenPos.x + currentPointSize, screenPos.y),
                            Point2Df(screenPos.x, screenPos.y + currentPointSize),
                            Point2Df(screenPos.x - currentPointSize, screenPos.y)
                    };
                    ctx->FillPath(diamondPoints);
                    break;
                }
            }
        }
    }

    bool UltraCanvasScatterPlotElement::HandleChartMouseMove(const Point2Di &mousePos) {
        if (!enableTooltips) {
            HideTooltip();
            return false;
        }

        // Find nearest point
        ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
        double mouseDataX = transform.ScreenToDataX(mousePos.x - GetX());
        double mouseDataY = transform.ScreenToDataY(mousePos.y - GetY());

        size_t nearestIndex = SIZE_MAX;
        double nearestDistance = std::numeric_limits<double>::max();

        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);
            auto screenPos = transform.DataToScreen(point.x, point.y);

            // Check if mouse is within point radius
            double distance = std::sqrt((screenPos.x - mousePos.x) * (screenPos.x - mousePos.x) +
                                        (screenPos.y - mousePos.y) * (screenPos.y - mousePos.y));

            if (distance < pointSize * 2 && distance < nearestDistance) {
                nearestDistance = distance;
                nearestIndex = i;
            }
        }

        if (nearestIndex != SIZE_MAX) {
            auto point = dataSource->GetPoint(nearestIndex);
            ShowChartPointTooltip(mousePos, point, nearestIndex);
            return true;
        } else {
            HideTooltip();
            return false;
        }
    }


    // UltraCanvasAreaChartElement
    void UltraCanvasAreaChartElement::RenderChart(IRenderContext* ctx) {
        if (!ctx || !dataSource || dataSource->GetPointCount() < 2) {
            return;
        }

        // Update cache if needed (using existing base class functionality)
        UpdateRenderingCache();

        // Use existing ChartCoordinateTransform from base class
        ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);

        // Create area path points following existing pattern
        std::vector<Point2Df> areaPoints;
        areaPoints.reserve(dataSource->GetPointCount() + 2); // +2 for baseline closure

        // Add baseline start point (bottom-left of area)
        auto firstPoint = dataSource->GetPoint(0);
        auto baselineStart = transform.DataToScreen(firstPoint.x, cachedDataBounds.minY);
        areaPoints.push_back(baselineStart);

        // Add all data points to create the top edge of the area
        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);
            auto screenPos = transform.DataToScreen(point.x, point.y);
            areaPoints.push_back(screenPos);
        }

        // Add baseline end point (bottom-right of area) to close the shape
        auto lastPoint = dataSource->GetPoint(dataSource->GetPointCount() - 1);
        auto baselineEnd = transform.DataToScreen(lastPoint.x, cachedDataBounds.minY);
        areaPoints.push_back(baselineEnd);

        // Apply smoothing if enabled
        std::vector<Point2Df> renderPoints = enableSmoothing ?
                                             SmoothAreaPoints(areaPoints) : areaPoints;

        // Fill the area using gradient or solid fill
        if (enableGradientFill) {
            RenderGradientFill(ctx, renderPoints);
        } else {
            // Standard solid fill using existing IRenderContext methods
            ctx->SetFillColor(fillColor);
            ctx->FillPath(renderPoints);
        }

        // Draw the top edge line (data line) using existing methods
        if (lineWidth > 0.0f) {
            ctx->SetStrokeColor(lineColor);
            ctx->SetStrokeWidth(lineWidth);

            // Draw line connecting all data points (skip baseline points)
            ctx->DrawPath(renderPoints);
        }

        // Render data points if enabled
        if (showDataPoints) {
            RenderDataPoints(ctx, renderPoints);
        }
    }

    bool UltraCanvasAreaChartElement::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!enableTooltips || !dataSource || dataSource->GetPointCount() == 0) {
            return false;
        }

        // Check if mouse is within chart area using existing base functionality
        if (mousePos.x < cachedPlotArea.x ||
            mousePos.x > cachedPlotArea.x + cachedPlotArea.width ||
            mousePos.y < cachedPlotArea.y ||
            mousePos.y > cachedPlotArea.y + cachedPlotArea.height) {
            HideTooltip();
            return false;
        }

        // Use existing coordinate transformation
        ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
        auto dataPos = transform.ScreenToData(mousePos.x, mousePos.y);

        // Find closest data point on X-axis (following existing chart pattern)
        size_t closestIndex = 0;
        double minDistance = std::numeric_limits<double>::max();

        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);
            double distance = std::abs(point.x - dataPos.x);
            if (distance < minDistance) {
                minDistance = distance;
                closestIndex = i;
            }
        }

        // Show tooltip for closest point using existing base class method
        if (closestIndex < dataSource->GetPointCount()) {
            auto point = dataSource->GetPoint(closestIndex);
            ShowChartPointTooltip(mousePos, point, closestIndex);
            return true;
        }

        HideTooltip();
        return false;
    }
}
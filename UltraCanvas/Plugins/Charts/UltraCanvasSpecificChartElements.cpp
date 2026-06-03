// Plugins/Charts/UltraCanvasSpecificChartElements.cpp
// Specific chart element implementations with aligned X-axis positioning
// Version: 1.1.0
// Last Modified: 2025-01-27
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasSpecificChartElements.h"
#include <cmath>
#include <algorithm>

namespace UltraCanvas {

// =============================================================================
// LINE CHART IMPLEMENTATION
// =============================================================================

    void UltraCanvasLineChartElement::RenderChart(IRenderContext* ctx) {
        if (!ctx || !dataSource || dataSource->GetPointCount() == 0) return;

        // Set line style
        ctx->SetStrokePaint(lineColor);
        ctx->SetStrokeWidth(lineWidth);

        std::vector<Point2Dd> linePoints;

        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);

            // Use the new positioning method that respects label mode
            Point2Dd screenPos = GetDataPointScreenPosition(i, point);
            linePoints.push_back(screenPos);
        }

        // Draw the line
        if (enableSmoothing && linePoints.size() > 2) {
            DrawSmoothLine(ctx, linePoints);
        } else {
            // Draw straight line segments
            for (size_t i = 1; i < linePoints.size(); ++i) {
                ctx->DrawLine({linePoints[i-1].x, linePoints[i-1].y},
                              {linePoints[i].x, linePoints[i].y});
            }
        }

        // Draw data points if enabled
        if (showDataPoints) {
            ctx->SetFillPaint(pointColor);
            for (const auto& screenPos : linePoints) {
                ctx->FillCircle(screenPos, pointRadius);
            }
        }

        if (showValueLabels) {
            RenderValueLabels(ctx, linePoints);
        }
    }

    void UltraCanvasLineChartElement::DrawSmoothLine(IRenderContext* ctx, const std::vector<Point2Dd>& points) {
        if (points.size() < 3) {
            // Not enough points for smoothing
            for (size_t i = 1; i < points.size(); ++i) {
                ctx->DrawLine(points[i-1], points[i]);
            }
            return;
        }

        // Simple Catmull-Rom spline interpolation
        for (size_t i = 0; i < points.size() - 1; ++i) {
            Point2Dd p0 = (i > 0) ? points[i-1] : points[i];
            Point2Dd p1 = points[i];
            Point2Dd p2 = points[i+1];
            Point2Dd p3 = (i < points.size() - 2) ? points[i+2] : points[i+1];

            // Draw interpolated curve segment
            int steps = 20;
            Point2Dd prevPoint = p1;

            for (int step = 1; step <= steps; ++step) {
                double t = step / static_cast<double>(steps);
                double t2 = t * t;
                double t3 = t2 * t;

                double x = 0.5f * ((2 * p1.x) +
                                  (-p0.x + p2.x) * t +
                                  (2*p0.x - 5*p1.x + 4*p2.x - p3.x) * t2 +
                                  (-p0.x + 3*p1.x - 3*p2.x + p3.x) * t3);

                double y = 0.5f * ((2 * p1.y) +
                                  (-p0.y + p2.y) * t +
                                  (2*p0.y - 5*p1.y + 4*p2.y - p3.y) * t2 +
                                  (-p0.y + 3*p1.y - 3*p2.y + p3.y) * t3);

                Point2Dd currentPoint(x, y);
                ctx->DrawLine(prevPoint, currentPoint);
                prevPoint = currentPoint;
            }
        }
    }

    bool UltraCanvasLineChartElement::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!dataSource || !enableTooltips) return false;

        // Find nearest data point
        double minDistance = 20.0f; // Threshold distance in pixels
        size_t nearestIndex = SIZE_MAX;

        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);
            Point2Dd screenPos = GetDataPointScreenPosition(i, point);

            double dx = mousePos.x - screenPos.x;
            double dy = mousePos.y - screenPos.y;
            double distance = std::sqrt(dx * dx + dy * dy);

            if (distance < minDistance) {
                minDistance = distance;
                nearestIndex = i;
            }
        }

        if (nearestIndex != SIZE_MAX) {
            auto point = dataSource->GetPoint(nearestIndex);
            ShowChartPointTooltip(mousePos, point, nearestIndex);
            return true;
        } else if (isTooltipActive) {
            HideTooltip();
        }

        return false;
    }

// =============================================================================
// BAR CHART IMPLEMENTATION
// =============================================================================

    void UltraCanvasBarChartElement::RenderChart(IRenderContext* ctx) {
        if (!ctx || !dataSource || dataSource->GetPointCount() == 0) return;

        size_t pointCount = dataSource->GetPointCount();

        for (size_t i = 0; i < pointCount; ++i) {
            auto point = dataSource->GetPoint(i);

            double barX, barY, barHeight;

            if (IsUsingIndexBasedPositioning()) {
                // When using labels, distribute bars evenly across width
                double totalWidth = cachedPlotArea.width;
                double barWidth = totalWidth / pointCount;
                double actualBarSpacing = barWidth * barSpacing;
                double actualBarWidth = barWidth - actualBarSpacing;

                barX = cachedPlotArea.x + (i * barWidth) + (actualBarSpacing / 2);

                // Calculate Y position for the bar top
                ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
                auto topPos = transform.DataToScreen(point.x, point.y);
                auto bottomPos = transform.DataToScreen(point.x, cachedDataBounds.minY);

                barY = topPos.y;
                barHeight = bottomPos.y - topPos.y;

                if (barHeight < 0) {
                    barHeight = -barHeight;
                    barY = bottomPos.y;
                }

                // Draw the bar
                ctx->SetFillPaint(barColor);
                ctx->FillRectangle(Rect2Dd(barX, barY, actualBarWidth, barHeight));

                // Draw border if enabled
                if (barBorderWidth > 0) {
                    ctx->SetStrokePaint(barBorderColor);
                    ctx->SetStrokeWidth(barBorderWidth);
                    ctx->DrawRectangle(Rect2Dd(barX, barY, actualBarWidth, barHeight));
                }
            } else {
                // Use original numeric positioning
                ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
                double barWidth = cachedPlotArea.width / static_cast<double>(pointCount);
                double actualBarSpacing = barWidth * barSpacing;
                double actualBarWidth = barWidth - actualBarSpacing;

                barX = cachedPlotArea.x + (i * barWidth) + (actualBarSpacing / 2);

                auto topPos = transform.DataToScreen(point.x, point.y);
                auto bottomPos = transform.DataToScreen(point.x, cachedDataBounds.minY);

                barHeight = bottomPos.y - topPos.y;
                if (barHeight < 0) {
                    barHeight = -barHeight;
                    topPos.y = bottomPos.y;
                }

                ctx->SetFillPaint(barColor);
                ctx->FillRectangle(Rect2Dd(barX, topPos.y, actualBarWidth, barHeight));

                if (barBorderWidth > 0) {
                    ctx->SetStrokePaint(barBorderColor);
                    ctx->SetStrokeWidth(barBorderWidth);
                    ctx->DrawRectangle(Rect2Dd(barX, topPos.y, actualBarWidth, barHeight));
                }
            }
        }
    }

    bool UltraCanvasBarChartElement::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!dataSource || !enableTooltips) return false;

        size_t pointCount = dataSource->GetPointCount();
        double barWidth = cachedPlotArea.width / static_cast<double>(pointCount);

        // Check if mouse is over any bar
        for (size_t i = 0; i < pointCount; ++i) {
            double barX = cachedPlotArea.x + (i * barWidth);

            if (mousePos.x >= barX && mousePos.x <= barX + barWidth) {
                auto point = dataSource->GetPoint(i);
                ShowChartPointTooltip(mousePos, point, i);
                return true;
            }
        }

        if (isTooltipActive) {
            HideTooltip();
        }

        return false;
    }

    double UltraCanvasBarChartElement::GetXAxisLabelPosition(size_t dataIndex, size_t totalPoints) {
        // For bar charts, center the label under each bar
        double barWidth = cachedPlotArea.width / totalPoints;
        return cachedPlotArea.x + (dataIndex * barWidth) + (barWidth / 2);
    }

// =============================================================================
// SCATTER PLOT IMPLEMENTATION
// =============================================================================

    void UltraCanvasScatterPlotElement::RenderChart(IRenderContext* ctx) {
        if (!ctx || !dataSource || dataSource->GetPointCount() == 0) return;

        ctx->SetFillPaint(pointColor);
        ctx->SetStrokePaint(pointColor);
        ctx->SetStrokeWidth(1.5f);

        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);

            // Use the new positioning method
            Point2Dd screenPos = GetDataPointScreenPosition(i, point);

            // Draw point based on shape
            switch (pointShape) {
                case PointShape::Circle:
                    ctx->FillCircle(screenPos, pointSize);
                    break;

                case PointShape::Square: {
                    double halfSize = pointSize;
                    ctx->FillRectangle(Rect2Dd(screenPos.x - halfSize, screenPos.y - halfSize,
                                       halfSize * 2, halfSize * 2));
                    break;
                }

                case PointShape::Triangle: {
                    std::vector<Point2Dd> triangle = {
                            Point2Dd(screenPos.x, screenPos.y - pointSize),
                            Point2Dd(screenPos.x - pointSize, screenPos.y + pointSize),
                            Point2Dd(screenPos.x + pointSize, screenPos.y + pointSize)
                    };
                    ctx->FillLinePath(triangle);
                    break;
                }

                case PointShape::Diamond: {
                    std::vector<Point2Dd> diamond = {
                            Point2Dd(screenPos.x, screenPos.y - pointSize),
                            Point2Dd(screenPos.x + pointSize, screenPos.y),
                            Point2Dd(screenPos.x, screenPos.y + pointSize),
                            Point2Dd(screenPos.x - pointSize, screenPos.y)
                    };
                    ctx->FillLinePath(diamond);
                    break;
                }
            }
        }
    }

    bool UltraCanvasScatterPlotElement::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!dataSource || !enableTooltips) return false;

        // Find nearest point
        double minDistance = pointSize + 5.0f; // Threshold based on point size
        size_t nearestIndex = SIZE_MAX;

        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);
            Point2Dd screenPos = GetDataPointScreenPosition(i, point);

            double dx = mousePos.x - screenPos.x;
            double dy = mousePos.y - screenPos.y;
            double distance = std::sqrt(dx * dx + dy * dy);

            if (distance < minDistance) {
                minDistance = distance;
                nearestIndex = i;
            }
        }

        if (nearestIndex != SIZE_MAX) {
            auto point = dataSource->GetPoint(nearestIndex);
            ShowChartPointTooltip(mousePos, point, nearestIndex);
            return true;
        } else if (isTooltipActive) {
            HideTooltip();
        }

        return false;
    }

// =============================================================================
// AREA CHART IMPLEMENTATION
// =============================================================================

    std::vector<Point2Dd> CalculateSmoothPath(const std::vector<Point2Dd>& points) {
        std::vector<Point2Dd> newpoints;
        if (points.size() < 3) {
            newpoints = points;
            return newpoints;
        }

        newpoints.push_back(points[0]);

        // Simple Catmull-Rom spline interpolation
        for (size_t i = 0; i < points.size() - 1; ++i) {
            Point2Dd p0 = (i > 0) ? points[i-1] : points[i];
            Point2Dd p1 = points[i];
            Point2Dd p2 = points[i+1];
            Point2Dd p3 = (i < points.size() - 2) ? points[i+2] : points[i+1];

            // Draw interpolated curve segment
            int steps = 20;
//            Point2Dd prevPoint = p1;

            for (int step = 1; step <= steps; ++step) {
                double t = step / static_cast<double>(steps);
                double t2 = t * t;
                double t3 = t2 * t;

                double x = 0.5f * ((2 * p1.x) +
                                  (-p0.x + p2.x) * t +
                                  (2*p0.x - 5*p1.x + 4*p2.x - p3.x) * t2 +
                                  (-p0.x + 3*p1.x - 3*p2.x + p3.x) * t3);

                double y = 0.5f * ((2 * p1.y) +
                                  (-p0.y + p2.y) * t +
                                  (2*p0.y - 5*p1.y + 4*p2.y - p3.y) * t2 +
                                  (-p0.y + 3*p1.y - 3*p2.y + p3.y) * t3);

                newpoints.emplace_back(x, y);
            }
        }
        return newpoints;
    }


    void UltraCanvasAreaChartElement::RenderChart(IRenderContext* ctx) {
        if (!ctx || !dataSource || dataSource->GetPointCount() == 0) return;

        std::vector<Point2Dd> areaPoints;
        std::vector<Point2Dd> smoothedAreaPoints;

        // Build the area polygon
        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);
            Point2Dd screenPos = GetDataPointScreenPosition(i, point);
            areaPoints.push_back(screenPos);
        }

        // Add bottom points to close the area
        ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
        auto bottomY = transform.DataToScreen(0, cachedDataBounds.minY).y;

        if (enableSmoothing) {
            smoothedAreaPoints = CalculateSmoothPath(areaPoints);
        } else {
            smoothedAreaPoints = areaPoints;
        }
        // Add bottom-right corner
        smoothedAreaPoints.push_back(Point2Dd(areaPoints.back().x, bottomY));

        // Add bottom-left corner
        smoothedAreaPoints.push_back(Point2Dd(areaPoints.front().x, bottomY));

        // Fill the area
        if (enableGradientFill) {
            // Create gradient
            double minY = areaPoints[0].y;
            double maxY = areaPoints[0].y;

            for (const auto& point : areaPoints) {
                minY = std::min(minY, point.y);
                maxY = std::max(maxY, point.y);
            }

            auto gradient = ctx->CreateLinearGradientPattern(
                    0, minY, 0, maxY,
                    {GradientStop(0, gradientStartColor),
                     GradientStop(1, gradientEndColor)}
            );
            ctx->SetFillPaint(gradient);
        } else {
            ctx->SetFillPaint(fillColor);
        }

        ctx->FillLinePath(smoothedAreaPoints);

        // Draw the line on top
        ctx->SetStrokePaint(lineColor);
        ctx->SetStrokeWidth(lineWidth);

        for (size_t i = 1; i < smoothedAreaPoints.size() - 2; ++i) {
            ctx->DrawLine(smoothedAreaPoints[i-1],
                          smoothedAreaPoints[i]);
        }

        // Draw data points if enabled
        if (showDataPoints) {
            ctx->SetFillPaint(pointColor);
            for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
                ctx->FillCircle(areaPoints[i], pointRadius);
            }
        }

        if (showValueLabels) {
            RenderValueLabels(ctx, areaPoints);
        }
    }

    bool UltraCanvasAreaChartElement::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!dataSource || !enableTooltips) return false;

        // Find nearest data point along the x-axis
        double minXDistance = 30.0f; // Threshold distance in pixels for X
        double maxYDistance = 50.0f; // Threshold distance in pixels for Y
        size_t nearestIndex = SIZE_MAX;

        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);
            Point2Dd screenPos = GetDataPointScreenPosition(i, point);

            double dx = std::abs(mousePos.x - screenPos.x);
            double dy = std::abs(mousePos.y - screenPos.y);

            // Check if mouse is close enough in X direction and within reasonable Y range
            if (dx < minXDistance && dy < maxYDistance) {
                minXDistance = dx;
                nearestIndex = i;
            }
        }

        if (nearestIndex != SIZE_MAX) {
            auto point = dataSource->GetPoint(nearestIndex);
            ShowChartPointTooltip(mousePos, point, nearestIndex);
            return true;
        } else if (isTooltipActive) {
            HideTooltip();
        }

        return false;
    }

} // namespace UltraCanvas
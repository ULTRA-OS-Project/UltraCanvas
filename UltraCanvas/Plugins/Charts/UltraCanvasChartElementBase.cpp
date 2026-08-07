// Plugins/Charts/UltraCanvasChartElementBase.cpp
// Base class for all chart elements with common functionality
// Version: 1.1.0
// Last Modified: 2025-01-27
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

    void UltraCanvasChartElementBase::SetDataSource(std::shared_ptr<IChartDataSource> data) {
        dataSource = data;
        InvalidateCache();
        StartAnimation();
        RequestRedraw();
    }

    void UltraCanvasChartElementBase::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        // Check if we have data
        if (!dataSource || dataSource->GetPointCount() == 0) {
            DrawEmptyState(ctx);
            return;
        }

        // Update cache if needed
        UpdateRenderingCache();

        // Apply animation if active
        if (animationEnabled && !animationComplete) {
            UpdateAnimation();
        }

        // Draw common background
        RenderCommonBackground(ctx);

        // Call derived class to render specific chart type
        RenderChart(ctx);

        // Draw selection indicators if any using existing drawing functions
        if (enableSelection) {
            DrawSelectionIndicators(ctx);
        }

    }

    bool UltraCanvasChartElementBase::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                return HandleMouseDown(event);
            case UCEventType::MouseUp:
                return HandleMouseUp(event);
            case UCEventType::MouseMove:
                return HandleMouseMove(event);
            case UCEventType::MouseWheel:
                return HandleMouseWheel(event);
            default:
                return false;
        }
    }

    ChartDataBounds UltraCanvasChartElementBase::CalculateDataBounds() {
        ChartDataBounds bounds;
        if (!dataSource || dataSource->GetPointCount() == 0) {
            return bounds;
        }

        // Initialize with first point
        auto firstPoint = dataSource->GetPoint(0);
        bounds.minX = bounds.maxX = firstPoint.x;
        bounds.minY = bounds.maxY = firstPoint.y;

        // Find min/max values
        for (size_t i = 1; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);
            bounds.minX = std::min(bounds.minX, point.x);
            bounds.maxX = std::max(bounds.maxX, point.x);
            bounds.minY = std::min(bounds.minY, point.y);
            bounds.maxY = std::max(bounds.maxY, point.y);
        }

        // Add padding
        double rangeX = bounds.maxX - bounds.minX;
        double rangeY = bounds.maxY - bounds.minY;
        if (rangeX > 0) {
            bounds.minX -= rangeX * 0.05;
            bounds.maxX += rangeX * 0.05;
        }
        if (rangeY > 0) {
            bounds.minY -= rangeY * 0.05;
            bounds.maxY += rangeY * 0.05;
        }

        return bounds;
    }

    void UltraCanvasChartElementBase::RenderCommonBackground(IRenderContext* ctx) {
        if (!ctx) return;

        // Draw overall background using existing functions
        if (showBackground) {
            ctx->DrawFilledRectangle(GetLocalBounds(), backgroundColor);

            // Draw plot area background using existing functions
            ctx->SetFillPaint(plotAreaColor);
            ctx->FillRectangle(cachedPlotArea.ToRect2D());

            // Draw plot area border using existing functions
            ctx->SetStrokePaint(Color(180, 180, 180, 255));
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawRectangle(cachedPlotArea.ToRect2D());
        }

        // Draw grid if enabled using existing functions
        if (showGrid) {
            RenderGrid(ctx);
        }

        if (showAxes) {
            // Draw axes using existing functions
            RenderAxes(ctx);
        }

        // Draw title using existing functions
        if (!chartTitle.empty()) {
            ctx->SetTextPaint(Color(0, 0, 0, 255));
            ctx->SetFontSize(16.0f);

            // Calculate center position (simplified)
            double titleX = static_cast<double>(GetWidth()) / 2 - chartTitle.length() * 5;
            ctx->DrawText(chartTitle, Point2Dd(titleX, 0));
        }
    }

    void UltraCanvasChartElementBase::RenderGrid(IRenderContext* ctx) {
        ctx->SetStrokePaint(gridColor);
        ctx->SetStrokeWidth(1.0f);

        // Vertical grid lines using existing DrawLine
        int numVerticalLines = 10;
        for (int i = 1; i < numVerticalLines; ++i) {
            double x = cachedPlotArea.x + (i * cachedPlotArea.width / numVerticalLines);
            ctx->DrawLine({x, cachedPlotArea.y}, { x, cachedPlotArea.y + cachedPlotArea.height});
        }

        // Horizontal grid lines using existing DrawLine
        int numHorizontalLines = 8;
        for (int i = 1; i < numHorizontalLines; ++i) {
            double y = cachedPlotArea.y + (i * cachedPlotArea.height / numHorizontalLines);
            ctx->DrawLine({cachedPlotArea.x, y}, { cachedPlotArea.x + cachedPlotArea.width, y});
        }
    }

    void UltraCanvasChartElementBase::RenderAxes(IRenderContext* ctx) {
        // Set axis style using existing functions
        ctx->SetStrokePaint(Color(0, 0, 0, 255));
        ctx->SetStrokeWidth(1.0f);

        // Draw X-axis using existing DrawLine
        ctx->DrawLine({cachedPlotArea.x, cachedPlotArea.y + cachedPlotArea.height}, {
                      cachedPlotArea.x + cachedPlotArea.width, cachedPlotArea.y + cachedPlotArea.height});

        // Draw Y-axis using existing DrawLine
        ctx->DrawLine({cachedPlotArea.x, cachedPlotArea.y}, {
                      cachedPlotArea.x, cachedPlotArea.y + cachedPlotArea.height});

        // Draw basic tick marks and labels
        RenderAxisLabels(ctx);
    }

//    void UltraCanvasChartElementBase::RenderAxisLabels(IRenderContext* ctx) {
//        if (!ctx) return;
//
//        ctx->SetStrokePaint(Color(0, 0, 0, 255));
//        ctx->SetTextPaint(Color(0, 0, 0, 255));
//        ctx->SetFontSize(10.0f);
//
//        // Use new method for X-axis labels that supports label mode
//        RenderXAxisLabelsWithMode(ctx);
//
//        // Y-axis labels
//        int numYTicks = 6, txtW, txtH;
//        for (int i = 0; i <= numYTicks; ++i) {
//            float y = cachedPlotArea.y + cachedPlotArea.height - (i * cachedPlotArea.height / numYTicks);
//            float tickX = cachedPlotArea.x;
//
//            // Draw tick mark using existing DrawLine
//            ctx->DrawLine({tickX - 5, y}, { tickX, y});
//
//            // Draw label using existing DrawText
//            double labelValue = cachedDataBounds.minY + (i * (cachedDataBounds.maxY - cachedDataBounds.minY) / numYTicks);
//            std::string label = FormatAxisLabel(labelValue);
//            ctx->GetTextLineDimensions(label, txtW, txtH);
//            ctx->DrawText(label, tickX - txtW - 8, y - (txtH / 2));
//        }
//    }

    double UltraCanvasChartElementBase::GetXAxisLabelPosition(size_t dataIndex, size_t totalPoints) {
        // Default implementation for line, scatter, area charts
        if (totalPoints == 1) {
            return cachedPlotArea.x + cachedPlotArea.width / 2;
        } else {
            return cachedPlotArea.x + (dataIndex * cachedPlotArea.width / (totalPoints - 1));
        }
    }

    void UltraCanvasChartElementBase::RenderAxisLabels(IRenderContext* ctx) {
        if (!ctx || !dataSource) return;

        size_t dataPointCount = dataSource->GetPointCount();
        if (dataPointCount == 0) return;

        ctx->SetStrokePaint(Color(0, 0, 0, 255));
        ctx->SetTextPaint(Color(0, 0, 0, 255));
        ctx->SetFontSize(10.0f);

        if (useIndexBasedPositioning) {
            // When using index-based positioning (DataLabel mode),
            // draw labels at evenly spaced positions matching the data points

            // Determine number of labels to show based on available space
            int maxLabels = 12;
            int labelStep = std::max(1, static_cast<int>(dataPointCount) / maxLabels);

            for (size_t i = 0; i < dataPointCount; i += labelStep) {
                auto point = dataSource->GetPoint(i);

                // Calculate X position - must match GetDataPointScreenPosition logic
                double x = GetXAxisLabelPosition(i, dataPointCount);

                double tickY = cachedPlotArea.y + cachedPlotArea.height;

                // Draw tick mark
                ctx->DrawLine({x, tickY}, { x, tickY + 5});

                // Draw label (use label if available, otherwise fall back to x value)
                std::string label = point.label;
                if (label.empty()) {
                    label = FormatAxisLabel(point.x);
                }

                // Handle rotation if enabled
                if (rotateXAxisLabels) {
                    ctx->PushState();
                    ctx->Translate(x, tickY + 8);
                    ctx->Rotate(xAxisLabelRotation * M_PI / 180.0f);
                    ctx->DrawText(label, {0, 0});
                    ctx->PopState();
                } else {
                    Size2Di txtSize = ctx->GetTextLineDimensions(label);
                    ctx->DrawText(label, {x - txtSize.width / 2, tickY + 8});
                }
            }
        } else {
            // Use numeric values with actual data coordinates
            int numXTicks = 8;
            for (int i = 0; i <= numXTicks; ++i) {
                double x = cachedPlotArea.x + (i * cachedPlotArea.width / numXTicks);
                double tickY = cachedPlotArea.y + cachedPlotArea.height;

                // Draw tick mark
                ctx->DrawLine({x, tickY}, { x, tickY + 5});

                // Draw label
                double labelValue = cachedDataBounds.minX + (i * (cachedDataBounds.maxX - cachedDataBounds.minX) / numXTicks);
                std::string label = FormatAxisLabel(labelValue);

                if (rotateXAxisLabels) {
                    ctx->PushState();
                    ctx->Translate(x, tickY + 8);
                    ctx->Rotate(xAxisLabelRotation * M_PI / 180.0f);
                    ctx->DrawText(label, {0, 0});
                    ctx->PopState();
                } else {
                    ctx->DrawText(label, Point2Dd (x - 4, tickY + 8));
                }
            }
        }

        int numYTicks = 6;
        for (int i = 0; i <= numYTicks; ++i) {
            double y = cachedPlotArea.y + cachedPlotArea.height - (i * cachedPlotArea.height / numYTicks);
            double tickX = cachedPlotArea.x;

            // Draw tick mark using existing DrawLine
            ctx->DrawLine({tickX - 5, y}, { tickX, y});

            // Draw label using existing DrawText
            double labelValue = cachedDataBounds.minY + (i * (cachedDataBounds.maxY - cachedDataBounds.minY) / numYTicks);
            std::string label = FormatAxisLabel(labelValue);
            Size2Di txtSize = ctx->GetTextLineDimensions(label);
            ctx->DrawText(label, Point2Dd (tickX - txtSize.width - 8, y - (static_cast<double>(txtSize.height) / 2)));
        }
    }

    void UltraCanvasChartElementBase::RenderValueLabels(IRenderContext* ctx, const std::vector<Point2Dd>& screenPositions) {
        if (!dataSource) return;

        ctx->SetTextPaint(valueLabelColor);
        ctx->SetFontSize(valueLabelFontSize);

        // Labels are placed greedily in point order: each is drawn only if its
        // bounds do not overlap a label already drawn, so dense series degrade
        // to a readable subset instead of painting every value over its
        // neighbors.
        std::vector<Rect2Dd> placedLabels;
        const double labelPadding = 2.0;

        for (size_t i = 0; i < dataSource->GetPointCount() && i < screenPositions.size(); ++i) {
            auto point = dataSource->GetPoint(i);
            Point2Dd labelPos = CalculateValueLabelPosition(screenPositions[i], i, dataSource->GetPointCount());

            std::string valueText = FormatAxisLabel(point.y);
            Size2Di txtSize = ctx->GetTextLineDimensions(valueText);

            double rotation = 0.0;
            if (valueLabelAutoRotate && dataSource->GetPointCount() > 10) {
                rotation = -45.0;
            } else if (valueLabelRotation != 0.0f) {
                rotation = valueLabelRotation;
            }

            Rect2Dd bounds;
            if (rotation != 0.0) {
                // Axis-aligned bounds of the text rect rotated around labelPos.
                double rad = rotation * M_PI / 180.0;
                double c = std::cos(rad), s = std::sin(rad);
                double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
                const double cx[3] = {static_cast<double>(txtSize.width), 0.0,
                                      static_cast<double>(txtSize.width)};
                const double cy[3] = {0.0, static_cast<double>(txtSize.height),
                                      static_cast<double>(txtSize.height)};
                for (int k = 0; k < 3; ++k) {
                    double rx = cx[k] * c - cy[k] * s;
                    double ry = cx[k] * s + cy[k] * c;
                    minX = std::min(minX, rx); maxX = std::max(maxX, rx);
                    minY = std::min(minY, ry); maxY = std::max(maxY, ry);
                }
                bounds = Rect2Dd(labelPos.x + minX - labelPadding, labelPos.y + minY - labelPadding,
                                 maxX - minX + 2 * labelPadding, maxY - minY + 2 * labelPadding);
            } else {
                bounds = Rect2Dd(labelPos.x - txtSize.width / 2.0 - labelPadding, labelPos.y - labelPadding,
                                 txtSize.width + 2 * labelPadding, txtSize.height + 2 * labelPadding);
            }

            bool overlaps = false;
            for (const auto& placed : placedLabels) {
                if (bounds.Intersects(placed)) { overlaps = true; break; }
            }
            if (overlaps) continue;
            placedLabels.push_back(bounds);

            if (rotation != 0.0) {
                ctx->PushState();
                ctx->Translate(labelPos.x, labelPos.y);
                ctx->Rotate(rotation * M_PI / 180.0);
                ctx->DrawText(valueText, {0, 0});
                ctx->PopState();
            } else {
                // No rotation - center the text
                ctx->DrawText(valueText, {labelPos.x - txtSize.width/2, labelPos.y});
            }
        }
    }

    Point2Dd UltraCanvasChartElementBase::CalculateValueLabelPosition(const Point2Dd& pointPos, size_t index, size_t totalPoints) {
        Point2Dd labelPos = pointPos;

        switch (valueLabelPosition) {
            case ValueLabelPosition::LabelAbove:
                labelPos.y -= (pointRadius + valueLabelOffset);
                break;
            case ValueLabelPosition::LabelBelow:
                labelPos.y += (pointRadius + valueLabelOffset);
                break;
            case ValueLabelPosition::LabelLeft:
                labelPos.x -= (pointRadius + valueLabelOffset);
                break;
            case ValueLabelPosition::LabelRight:
                labelPos.x += (pointRadius + valueLabelOffset);
                break;
            case ValueLabelPosition::LabelAuto:
                // Auto position - alternate above/below for crowded charts
                if (totalPoints > 10 && index % 2 == 1) {
                    labelPos.y += (pointRadius + valueLabelOffset);
                } else {
                    labelPos.y -= (pointRadius + valueLabelOffset);
                }
                break;
        }

        return labelPos;
    }

    std::string UltraCanvasChartElementBase::FormatAxisLabel(double value) {
        // Simple number formatting
        if (std::abs(value) >= 1e6) {
            return std::to_string(static_cast<int>(value / 1e6)) + "M";
        } else if (std::abs(value) >= 1e3) {
            return std::to_string(static_cast<int>(value / 1e3)) + "K";
        } else if (std::abs(value - std::round(value)) < 0.01) {
            return std::to_string(static_cast<int>(std::round(value)));
        } else {
            return std::to_string(value).substr(0, 6);
        }
    }

    void UltraCanvasChartElementBase::DrawSelectionIndicators(IRenderContext* ctx) {
        if (hoveredPointIndex == SIZE_MAX || !dataSource) return;

        auto point = dataSource->GetPoint(hoveredPointIndex);
        ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);

        auto screenPos = transform.DataToScreen(point.x, point.y);
        float indicatorSize = 8.0f;

        // Use existing IRenderContext drawing functions
        ctx->SetStrokePaint(Color(255, 0, 0, 255)); // Red selection indicator
        ctx->SetStrokeWidth(2.0f);
        ctx->DrawCircle(screenPos, indicatorSize);
    }

    void UltraCanvasChartElementBase::DrawEmptyState(IRenderContext* ctx) {
        // Use existing IRenderContext functions
        ctx->SetFillPaint(Color(240, 240, 240, 255));
        ctx->FillRectangle(GetLocalBounds());

        ctx->SetTextPaint(Color(128, 128, 128, 255));
        ctx->SetFontSize(14.0f);

        std::string emptyText = "No data to display";
        // Calculate center position (simplified)
        double textX = static_cast<double>(GetWidth()) / 2 - 60;
        double textY = static_cast<double>(GetHeight()) / 2;

        ctx->DrawText(emptyText, {textX, textY});
    }

    bool UltraCanvasChartElementBase::HandleMouseMove(const UCEvent& event) {
        Point2Di mousePos(event.pointer.x, event.pointer.y);
        lastMousePos = mousePos;

        // Call derived class for chart-specific handling
        bool handled = HandleChartMouseMove(mousePos);

        // Handle pan if enabled
        if (isDragging && enablePan) {
            // Implement panning logic
            handled = true;
        }

        return handled;
    }

    bool UltraCanvasChartElementBase::HandleMouseDown(const UCEvent& event) {
        if (event.button == UCMouseButton::Left) { // Left mouse button
            isDragging = true;
            lastMousePos = Point2Di(event.pointer.x, event.pointer.y);
            return true;
        }
        return false;
    }

    bool UltraCanvasChartElementBase::HandleMouseUp(const UCEvent& event) {
        if (event.button == UCMouseButton::Left) {
            isDragging = false;
            return true;
        }
        return false;
    }

    bool UltraCanvasChartElementBase::HandleMouseWheel(const UCEvent& event) {
        if (enableZoom) {
            float zoomDelta = event.wheelDelta > 0 ? 1.1f : 0.9f;
            zoomLevel *= zoomDelta;
            zoomLevel = std::clamp(zoomLevel, 0.1f, 10.0f);
            InvalidateCache();
            RequestRedraw();
            return true;
        }
        return false;
    }

    void UltraCanvasChartElementBase::HideTooltip() {
        if (isTooltipActive) {
            UltraCanvasTooltipManager::HideTooltip();
            isTooltipActive = false;
            hoveredPointIndex = SIZE_MAX;
        }
    }

} // namespace UltraCanvas
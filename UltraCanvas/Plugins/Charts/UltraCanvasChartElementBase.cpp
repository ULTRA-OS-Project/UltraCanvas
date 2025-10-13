// Plugins/Charts/UltraCanvasChartElementBase.cpp
// Base class for all chart elements with common functionality
// Version: 1.1.0
// Last Modified: 2025-01-27
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include <cmath>

namespace UltraCanvas {

    void UltraCanvasChartElementBase::SetDataSource(std::shared_ptr<IChartDataSource> data) {
        dataSource = data;
        InvalidateCache();
        StartAnimation();
        RequestRedraw();
    }

    void UltraCanvasChartElementBase::Render() {
        // Get render interface
        IRenderContext* ctx = GetRenderContext();
        if (!ctx) return;

        ctx->PushState();
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

        // Set clipping to element bounds using existing functions
        ctx->SetClipRect(GetActualBounds());

        // Draw common background
        RenderCommonBackground(ctx);

        // Call derived class to render specific chart type
        RenderChart(ctx);

        // Draw selection indicators if any using existing drawing functions
        if (enableSelection) {
            DrawSelectionIndicators(ctx);
        }

        // Clear clipping using existing functions
        //ctx->ClearClipRect();
        ctx->PopState();
    }

    bool UltraCanvasChartElementBase::OnEvent(const UCEvent& event) {
        if (!IsActive() || !IsVisible()) return false;

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
        ctx->DrawFilledRectangle(GetBounds(), backgroundColor);

        // Draw plot area background using existing functions
        ctx->SetFillPaint(plotAreaColor);
        ctx->FillRectangle(cachedPlotArea.x, cachedPlotArea.y, cachedPlotArea.width, cachedPlotArea.height);

        // Draw plot area border using existing functions
        ctx->SetStrokePaint(Color(180, 180, 180, 255));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(cachedPlotArea.x, cachedPlotArea.y, cachedPlotArea.width, cachedPlotArea.height);

        // Draw grid if enabled using existing functions
        if (showGrid) {
            RenderGrid(ctx);
        }

        // Draw axes using existing functions
        RenderAxes(ctx);

        // Draw title using existing functions
        if (!chartTitle.empty()) {
            ctx->SetTextPaint(Color(0, 0, 0, 255));
            ctx->SetFontSize(16.0f);

            // Calculate center position (simplified)
            int titleX = GetX() + GetWidth() / 2 - static_cast<int>(chartTitle.length()) * 5;
            ctx->DrawText(chartTitle, titleX, GetY());
        }
    }

    void UltraCanvasChartElementBase::RenderGrid(IRenderContext* ctx) {
        if (!ctx) return;

        ctx->SetStrokePaint(gridColor);
        ctx->SetStrokeWidth(1.0f);

        // Vertical grid lines using existing DrawLine
        int numVerticalLines = 10;
        for (int i = 1; i < numVerticalLines; ++i) {
            float x = cachedPlotArea.x + (i * cachedPlotArea.width / numVerticalLines);
            ctx->DrawLine(x, cachedPlotArea.y, x, cachedPlotArea.y + cachedPlotArea.height);
        }

        // Horizontal grid lines using existing DrawLine
        int numHorizontalLines = 8;
        for (int i = 1; i < numHorizontalLines; ++i) {
            float y = cachedPlotArea.y + (i * cachedPlotArea.height / numHorizontalLines);
            ctx->DrawLine(cachedPlotArea.x, y, cachedPlotArea.x + cachedPlotArea.width, y);
        }
    }

    void UltraCanvasChartElementBase::RenderAxes(IRenderContext* ctx) {
        if (!ctx) return;

        // Set axis style using existing functions
        ctx->SetStrokePaint(Color(0, 0, 0, 255));
        ctx->SetStrokeWidth(1.0f);

        // Draw X-axis using existing DrawLine
        ctx->DrawLine(cachedPlotArea.x, cachedPlotArea.y + cachedPlotArea.height,
                      cachedPlotArea.x + cachedPlotArea.width, cachedPlotArea.y + cachedPlotArea.height);

        // Draw Y-axis using existing DrawLine
        ctx->DrawLine(cachedPlotArea.x, cachedPlotArea.y,
                      cachedPlotArea.x, cachedPlotArea.y + cachedPlotArea.height);

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
//            ctx->DrawLine(tickX - 5, y, tickX, y);
//
//            // Draw label using existing DrawText
//            double labelValue = cachedDataBounds.minY + (i * (cachedDataBounds.maxY - cachedDataBounds.minY) / numYTicks);
//            std::string label = FormatAxisLabel(labelValue);
//            ctx->GetTextDimension(label, txtW, txtH);
//            ctx->DrawText(label, tickX - txtW - 8, y - (txtH / 2));
//        }
//    }

    float UltraCanvasChartElementBase::GetXAxisLabelPosition(size_t dataIndex, size_t totalPoints) {
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
                float x = GetXAxisLabelPosition(i, dataPointCount);

                float tickY = cachedPlotArea.y + cachedPlotArea.height;

                // Draw tick mark
                ctx->DrawLine(x, tickY, x, tickY + 5);

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
                    ctx->DrawText(label, 0, 0);
                    ctx->PopState();
                } else {
                    int txtW, txtH;
                    ctx->GetTextDimension(label, txtW, txtH);
                    ctx->DrawText(label, x - txtW / 2, tickY + 8);
                }
            }
        } else {
            // Use numeric values with actual data coordinates
            int numXTicks = 8;
            for (int i = 0; i <= numXTicks; ++i) {
                float x = cachedPlotArea.x + (i * cachedPlotArea.width / numXTicks);
                float tickY = cachedPlotArea.y + cachedPlotArea.height;

                // Draw tick mark
                ctx->DrawLine(x, tickY, x, tickY + 5);

                // Draw label
                double labelValue = cachedDataBounds.minX + (i * (cachedDataBounds.maxX - cachedDataBounds.minX) / numXTicks);
                std::string label = FormatAxisLabel(labelValue);

                if (rotateXAxisLabels) {
                    ctx->PushState();
                    ctx->Translate(x, tickY + 8);
                    ctx->Rotate(xAxisLabelRotation * M_PI / 180.0f);
                    ctx->DrawText(label, 0, 0);
                    ctx->PopState();
                } else {
                    ctx->DrawText(label, x - 4, tickY + 8);
                }
            }
        }

        int numYTicks = 6, txtW, txtH;
        for (int i = 0; i <= numYTicks; ++i) {
            float y = cachedPlotArea.y + cachedPlotArea.height - (i * cachedPlotArea.height / numYTicks);
            float tickX = cachedPlotArea.x;

            // Draw tick mark using existing DrawLine
            ctx->DrawLine(tickX - 5, y, tickX, y);

            // Draw label using existing DrawText
            double labelValue = cachedDataBounds.minY + (i * (cachedDataBounds.maxY - cachedDataBounds.minY) / numYTicks);
            std::string label = FormatAxisLabel(labelValue);
            ctx->GetTextDimension(label, txtW, txtH);
            ctx->DrawText(label, tickX - txtW - 8, y - (txtH / 2));
        }
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
        ctx->DrawCircle(screenPos.x, screenPos.y, indicatorSize);
    }

    void UltraCanvasChartElementBase::DrawEmptyState(IRenderContext* ctx) {
        // Use existing IRenderContext functions
        ctx->SetFillPaint(Color(240, 240, 240, 255));
        ctx->FillRectangle(GetX(), GetY(), GetWidth(), GetHeight());

        ctx->SetTextPaint(Color(128, 128, 128, 255));
        ctx->SetFontSize(14.0f);

        std::string emptyText = "No data to display";
        // Calculate center position (simplified)
        int textX = GetX() + GetWidth() / 2 - 60;
        int textY = GetY() + GetHeight() / 2;

        ctx->DrawText(emptyText, textX, textY);
    }

    bool UltraCanvasChartElementBase::HandleMouseMove(const UCEvent& event) {
        Point2Di mousePos(event.x, event.y);
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
            lastMousePos = Point2Di(event.x, event.y);
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
// Plugins/Charts/UltraCanvasChartElementBase.cpp
// Base class for all chart elements with common functionality
// Version: 1.0.0
// Last Modified: 2025-09-10
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasChartElementBase.h"

namespace UltraCanvas {

    void UltraCanvasChartElementBase::SetDataSource(std::shared_ptr<IChartDataSource> data) {
        dataSource = data;
        InvalidateCache();
        StartAnimation();
        RequestRedraw();
    }

    void UltraCanvasChartElementBase::Render() {
        // Get render interface
        IRenderContext *ctx = GetRenderContext();
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
        DrawCommonBackground(ctx);

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

    bool UltraCanvasChartElementBase::OnEvent(const UCEvent &event) {
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
            case UCEventType::KeyDown:
                return HandleKeyDown(event);
            default:
                return false;
        }
    }

    void UltraCanvasChartElementBase::UpdateRenderingCache() {
        if (cacheValid) return;

        if (dataSource && dataSource->GetPointCount() > 0) {
            cachedPlotArea = CalculatePlotArea();
            cachedDataBounds = CalculateDataBounds();
            cacheValid = true;
        }
    }

    void UltraCanvasChartElementBase::UpdateAnimation() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - animationStartTime);
        float progress = std::min(1.0f, elapsed.count() / (animationDuration * 1000.0f));

        if (progress >= 1.0f) {
            animationComplete = true;
        }

        // Animation logic here - could modify alpha, scale, or other visual properties
        if (!animationComplete) {
            RequestRedraw();
        }
    }

    ChartDataBounds UltraCanvasChartElementBase::CalculateDataBounds() {
        if (!dataSource || dataSource->GetPointCount() == 0) {
            return ChartDataBounds();
        }

        ChartDataBounds bounds;
        bool first = true;

        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);

            if (first) {
                bounds.minX = bounds.maxX = point.x;
                bounds.minY = bounds.maxY = point.y;
                first = false;
            } else {
                bounds.minX = std::min(bounds.minX, point.x);
                bounds.maxX = std::max(bounds.maxX, point.x);
                bounds.minY = std::min(bounds.minY, point.y);
                bounds.maxY = std::max(bounds.maxY, point.y);
            }

//            // Include OHLC values if present
//            if (point.open.has_value()) {
//                bounds.minY = std::min(bounds.minY, point.open.value());
//                bounds.maxY = std::max(bounds.maxY, point.open.value());
//            }
//            if (point.high.has_value()) {
//                bounds.maxY = std::max(bounds.maxY, point.high.value());
//            }
//            if (point.low.has_value()) {
//                bounds.minY = std::min(bounds.minY, point.low.value());
//            }
//            if (point.close.has_value()) {
//                bounds.minY = std::min(bounds.minY, point.close.value());
//                bounds.maxY = std::max(bounds.maxY, point.close.value());
//            }
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

    void UltraCanvasChartElementBase::DrawCommonBackground(IRenderContext *ctx) {
        if (!ctx) return;

        // Draw overall background using existing functions
        ctx->SetFillColor(backgroundColor);
        ctx->FillRectangle(GetX(), GetY(), GetWidth(), GetHeight());

        // Draw plot area background using existing functions
        ctx->SetFillColor(plotAreaColor);
        ctx->FillRectangle(cachedPlotArea.x, cachedPlotArea.y, cachedPlotArea.width, cachedPlotArea.height);

        // Draw plot area border using existing functions
        ctx->SetStrokeColor(Color(180, 180, 180, 255));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(cachedPlotArea.x, cachedPlotArea.y, cachedPlotArea.width, cachedPlotArea.height);

        // Draw grid if enabled using existing functions
        if (showGrid) {
            DrawGrid(ctx);
        }

        // Draw axes using existing functions
        DrawAxes(ctx);

        // Draw title using existing functions
        if (!chartTitle.empty()) {
            ctx->SetTextColor(Color(0, 0, 0, 255));
            ctx->SetFont("Arial", 16.0f);

            // Calculate center position (simplified)
            int titleX = GetX() + GetWidth() / 2 - static_cast<int>(chartTitle.length()) * 5;
            ctx->DrawText(chartTitle, titleX, GetY());
        }
    }

    void UltraCanvasChartElementBase::DrawGrid(IRenderContext *ctx) {
        if (!ctx) return;

        ctx->SetStrokeColor(gridColor);
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

    void UltraCanvasChartElementBase::DrawAxes(IRenderContext *ctx) {
        if (!ctx) return;

        // Set axis style using existing functions
        ctx->SetStrokeColor(Color(0, 0, 0, 255));
        ctx->SetStrokeWidth(1.0f);

        // Draw X-axis using existing DrawLine
        ctx->DrawLine(cachedPlotArea.x, cachedPlotArea.y + cachedPlotArea.height,
                      cachedPlotArea.x + cachedPlotArea.width, cachedPlotArea.y + cachedPlotArea.height);

        // Draw Y-axis using existing DrawLine
        ctx->DrawLine(cachedPlotArea.x, cachedPlotArea.y,
                      cachedPlotArea.x, cachedPlotArea.y + cachedPlotArea.height);

        // Draw basic tick marks and labels
        DrawAxisLabels(ctx);
    }

    void UltraCanvasChartElementBase::DrawAxisLabels(IRenderContext *ctx) {
        if (!ctx) return;

        ctx->SetTextColor(Color(0, 0, 0, 255));
        ctx->SetFont("Arial", 10.0f);

        // X-axis labels
        int numXTicks = 8;
        for (int i = 0; i <= numXTicks; ++i) {
            float x = cachedPlotArea.x + (i * cachedPlotArea.width / numXTicks);
            float tickY = cachedPlotArea.y + cachedPlotArea.height;

            // Draw tick mark using existing DrawLine
            ctx->DrawLine(x, tickY, x, tickY + 5);

            // Draw label using existing DrawText
            double labelValue = cachedDataBounds.minX + (i * (cachedDataBounds.maxX - cachedDataBounds.minX) / numXTicks);
            std::string label = FormatAxisLabel(labelValue);
            ctx->DrawText(label, x - 4, tickY + 3);
        }

        // Y-axis labels
        int numYTicks = 6, txtW, txtH;
        for (int i = 0; i <= numYTicks; ++i) {
            float y = cachedPlotArea.y + cachedPlotArea.height - (i * cachedPlotArea.height / numYTicks);
            float tickX = cachedPlotArea.x;

            // Draw tick mark using existing DrawLine
            ctx->DrawLine(tickX - 5, y, tickX, y);

            // Draw label using existing DrawText
            double labelValue = cachedDataBounds.minY + (i * (cachedDataBounds.maxY - cachedDataBounds.minY) / numYTicks);
            std::string label = FormatAxisLabel(labelValue);
            ctx->MeasureText(label, txtW, txtH);
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

    void UltraCanvasChartElementBase::DrawSelectionIndicators(IRenderContext *ctx) {
        if (hoveredPointIndex == SIZE_MAX || !dataSource) return;

        auto point = dataSource->GetPoint(hoveredPointIndex);
        ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);

        auto screenPos = transform.DataToScreen(point.x, point.y);
        float indicatorSize = 8.0f;

        // Use existing IRenderContext drawing functions
        ctx->SetStrokeColor(Color(255, 0, 0, 255)); // Red selection indicator
        ctx->SetStrokeWidth(2.0f);
        ctx->DrawCircle(screenPos.x, screenPos.y, indicatorSize);
    }

    void UltraCanvasChartElementBase::DrawEmptyState(IRenderContext *ctx) {
        // Use existing IRenderContext functions
        ctx->SetFillColor(Color(240, 240, 240, 255));
        ctx->FillRectangle(GetX(), GetY(), GetWidth(), GetHeight());

        ctx->SetTextColor(Color(128, 128, 128, 255));
        ctx->SetFont("Arial", 14.0f);

        std::string emptyText = "No data to display";
        // Calculate center position (simplified)
        int textX = GetX() + GetWidth() / 2 - 60;
        int textY = GetY() + GetHeight() / 2;

        ctx->DrawText(emptyText, textX, textY);
    }

    bool UltraCanvasChartElementBase::HandleMouseMove(const UCEvent &event) {
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

    bool UltraCanvasChartElementBase::HandleMouseDown(const UCEvent &event) {
        if (event.button == UCMouseButton::Left) { // Left mouse button
            isDragging = true;
            lastMousePos = Point2Di(event.x, event.y);
            return true;
        }
        return false;
    }

    bool UltraCanvasChartElementBase::HandleMouseUp(const UCEvent &event) {
        if (event.button == UCMouseButton::Left) {
            isDragging = false;
            return true;
        }
        return false;
    }

    bool UltraCanvasChartElementBase::HandleMouseWheel(const UCEvent &event) {
        if (enableZoom) {
            float zoomDelta = event.wheelDelta > 0 ? 1.1f : 0.9f;
            SetZoom(zoomLevel * zoomDelta);
            return true;
        }
        return false;
    }

    bool UltraCanvasChartElementBase::HandleKeyDown(const UCEvent &event) {
        // Handle keyboard shortcuts
        switch (event.virtualKey) {
            case UCKeys::R:
                ZoomToFit();
                return true;
            case UCKeys::Plus:
                ZoomIn();
                return true;
            case UCKeys::Minus:
                ZoomOut();
                return true;
            default:
                return false;
        }
    }

    void UltraCanvasChartElementBase::HideTooltip() {
        if (isTooltipActive) {
//            UltraCanvasTooltipManager::OnElementLeave(this);
            isTooltipActive = false;
            hoveredPointIndex = SIZE_MAX;
        }
    }

}
// UltraCanvasChartElement.h
// Chart element integration with UltraCanvas UI system
// Version: 1.0.1
// Last Modified: 2025-09-10
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasChartRenderer.h"
#include "UltraCanvasChartStructures.h"
#include <memory>
#include <chrono>

namespace UltraCanvas {

// =============================================================================
// CHART ELEMENT CLASS
// =============================================================================

    class UltraCanvasChartElement : public UltraCanvasElement {
    private:
        ChartConfiguration chartConfig;

        // Interactive state
        bool isDragging = false;
        bool isZooming = false;
        Point2Di lastMousePos;
        float zoomLevel = 1.0f;
        Point2Di panOffset;

        // Animation state
        bool animationEnabled = true;
        std::chrono::steady_clock::time_point animationStartTime;
        float animationDuration = 1.0f;
        bool animationComplete = false;

        // Cached rendering data
        PlotArea cachedPlotArea;
        DataBounds cachedDataBounds;
        bool cacheValid = false;

        // Tooltip state
        bool showTooltip = false;
        Point2Di tooltipPosition;
        std::string tooltipText;
        size_t hoveredPointIndex = SIZE_MAX;

    public:
        UltraCanvasChartElement(const std::string& id, long uid, int x, int y, int width, int height)
                : UltraCanvasElement(id, uid, x, y, width, height) {

            // Initialize with default configuration
            auto emptyData = std::make_shared<ChartDataVector>();
            chartConfig = UltraCanvasChartRenderer::CreateLineChart(emptyData, "Chart");

            // Enable interactive features by default
            SetMouseControls(1); // Enable mouse events
            SetActive(true);
            SetVisible(true);
        }

        // =============================================================================
        // CHART CONFIGURATION
        // =============================================================================

        void SetChartConfiguration(const ChartConfiguration& config) {
            chartConfig = config;
            cacheValid = false;
            InvalidateCache();

            // Reset animation
            if (chartConfig.enableAnimations) {
                StartAnimation();
            }

            Invalidate(); // Trigger redraw
        }

        const ChartConfiguration& GetChartConfiguration() const {
            return chartConfig;
        }

        void SetChartData(std::shared_ptr<IChartDataSource> data) {
            chartConfig.dataSource = data;
            cacheValid = false;
            InvalidateCache();

            if (chartConfig.enableAnimations) {
                StartAnimation();
            }

            Invalidate();
        }

        std::shared_ptr<IChartDataSource> GetChartData() const {
            return chartConfig.dataSource;
        }

        void SetChartType(ChartType type) {
            chartConfig.type = type;
            cacheValid = false;
            InvalidateCache();
            Invalidate();
        }

        ChartType GetChartType() const {
            return chartConfig.type;
        }

        // =============================================================================
        // STYLING METHODS
        // =============================================================================

        void SetTitle(const std::string& title) {
            chartConfig.title = title;
            Invalidate();
        }

        const std::string& GetTitle() const {
            return chartConfig.title;
        }

        void SetSubtitle(const std::string& subtitle) {
            chartConfig.subtitle = subtitle;
            Invalidate();
        }

        void AddAxisHighlight(const std::string& axis, double position, uint32_t color, const std::string& label = "") {
            UltraCanvasChartRenderer::AddAxisHighlight(chartConfig, axis, position, color, label);
            Invalidate();
        }

        void AddTrendLine(TrendLine::Type type, uint32_t color) {
            UltraCanvasChartRenderer::AddTrendLine(chartConfig, type, color);
            Invalidate();
        }

        void SetBarTexture(BarStyle::TextureType texture, uint32_t primaryColor, uint32_t secondaryColor = 0) {
            UltraCanvasChartRenderer::SetBarTexture(chartConfig, texture, primaryColor, secondaryColor);
            Invalidate();
        }

        // =============================================================================
        // INTERACTIVE FEATURES
        // =============================================================================

        void EnableZoom(bool enable) {
            chartConfig.enableZoom = enable;
        }

        void EnablePan(bool enable) {
            chartConfig.enablePan = enable;
        }

        void EnableTooltips(bool enable) {
            chartConfig.enableTooltips = enable;
        }

        void EnableAnimations(bool enable) {
            chartConfig.enableAnimations = enable;
            animationEnabled = enable;
        }

        void SetZoom(float zoom) {
            zoomLevel = std::clamp(zoom, 0.1f, 10.0f);
            cacheValid = false;
            RequestRedraw();
        }

        float GetZoom() const {
            return zoomLevel;
        }

        void ZoomIn() {
            SetZoom(zoomLevel * 1.2f);
        }

        void ZoomOut() {
            SetZoom(zoomLevel / 1.2f);
        }

        void ZoomToFit() {
            SetZoom(1.0f);
            panOffset = Point2Di(0, 0);
        }

        void SetPan(const Point2Di& offset) {
            panOffset = offset;
            cacheValid = false;
            RequestRedraw();
        }

        const Point2Di& GetPan() const {
            return panOffset;
        }

        // =============================================================================
        // RENDERING OVERRIDE
        // =============================================================================

        void Render(CanvasContext* canvasCtx) override {
            if (!canvasCtx || !chartConfig.dataSource || chartConfig.dataSource->GetPointCount() == 0) {
                // Draw empty state
                DrawEmptyState(canvasCtx);
                return;
            }

            // Get render interface
            IRenderContext* ctx = canvasCtx->GetRenderInterface();
            if (!ctx) return;

            // Update cache if needed
            UpdateRenderingCache();

            // Apply animation if active
            if (animationEnabled && chartConfig.enableAnimations && !animationComplete) {
                UpdateAnimation();
            }

            // Set clipping to element bounds
            ctx->SetClipRect(GetX(), GetY(), GetWidth(), GetHeight());

            // Render chart
            bool success = UltraCanvasChartRenderer::RenderChart(chartConfig, GetWidth(), GetHeight(), ctx);

            // Draw tooltip if active
            if (showTooltip && chartConfig.enableTooltips) {
                DrawTooltip(ctx);
            }

            // Draw selection indicators if any
            if (chartConfig.enableSelection) {
                DrawSelectionIndicators(ctx);
            }

            // Clear clipping
            ctx->ClearClipRect();

            if (!success) {
                DrawErrorState(ctx);
            }
        }

        // =============================================================================
        // EVENT HANDLING
        // =============================================================================

        bool HandleEvent(const UCEvent& event) override {
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

    private:
        // =============================================================================
        // MOUSE EVENT HANDLERS
        // =============================================================================

        bool HandleMouseDown(const UCEvent& event) {
            if (!Contains(event.x, event.y)) return false;

            lastMousePos = Point2Di(event.x, event.y);

            if (event.button == 1) { // Left mouse button
                if (chartConfig.enablePan) {
                    isDragging = true;
                    SetCapture(true);
                    return true;
                }
            }

            return false;
        }

        bool HandleMouseUp(const UCEvent& event) {
            if (isDragging) {
                isDragging = false;
                SetCapture(false);
                return true;
            }

            return false;
        }

        bool HandleMouseMove(const UCEvent& event) {
            Point2Di currentPos(event.x, event.y);

            if (isDragging && chartConfig.enablePan) {
                // Update pan offset
                Point2Di delta = Point2Di(currentPos.x - lastMousePos.x, currentPos.y - lastMousePos.y);
                panOffset = Point2Di(panOffset.x + delta.x, panOffset.y + delta.y);

                lastMousePos = currentPos;
                cacheValid = false;
                RequestRedraw();
                return true;
            }

            // Handle tooltip
            if (chartConfig.enableTooltips && Contains(event.x, event.y)) {
                UpdateTooltip(currentPos);
            } else {
                showTooltip = false;
                RequestRedraw();
            }

            lastMousePos = currentPos;
            return false;
        }

        bool HandleMouseWheel(const UCEvent& event) {
            if (!Contains(event.x, event.y) || !chartConfig.enableZoom) return false;

            float zoomFactor = (event.delta > 0) ? 1.1f : 0.9f;
            SetZoom(zoomLevel * zoomFactor);

            return true;
        }

        bool HandleKeyDown(const UCEvent& event) {
            switch (event.key) {
                case 'R':
                case 'r':
                    // Reset zoom and pan
                    ZoomToFit();
                    return true;

                case '+':
                case '=':
                    // Zoom in
                    ZoomIn();
                    return true;

                case '-':
                case '_':
                    // Zoom out
                    ZoomOut();
                    return true;

                default:
                    return false;
            }
        }

        // =============================================================================
        // HELPER METHODS
        // =============================================================================

        void UpdateRenderingCache() {
            if (cacheValid) return;

            cachedPlotArea = CalculatePlotArea(chartConfig, GetWidth(), GetHeight());
            cachedDataBounds = CalculateDataBounds(chartConfig.dataSource.get(), chartConfig);

            // Apply zoom and pan transformations
            if (zoomLevel != 1.0f || panOffset.x != 0 || panOffset.y != 0) {
                ApplyViewTransform();
            }

            cacheValid = true;
        }

        void ApplyViewTransform() {
            // Apply zoom to data bounds
            double centerX = (cachedDataBounds.minX + cachedDataBounds.maxX) / 2.0;
            double centerY = (cachedDataBounds.minY + cachedDataBounds.maxY) / 2.0;
            double rangeX = cachedDataBounds.GetXRange() / zoomLevel;
            double rangeY = cachedDataBounds.GetYRange() / zoomLevel;

            cachedDataBounds.minX = centerX - rangeX / 2.0;
            cachedDataBounds.maxX = centerX + rangeX / 2.0;
            cachedDataBounds.minY = centerY - rangeY / 2.0;
            cachedDataBounds.maxY = centerY + rangeY / 2.0;

            // Apply pan offset (convert screen space to data space)
            ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
            double panDataX = panOffset.x / cachedPlotArea.width * cachedDataBounds.GetXRange();
            double panDataY = -panOffset.y / cachedPlotArea.height * cachedDataBounds.GetYRange(); // Flip Y

            cachedDataBounds.minX -= panDataX;
            cachedDataBounds.maxX -= panDataX;
            cachedDataBounds.minY -= panDataY;
            cachedDataBounds.maxY -= panDataY;
        }

        void InvalidateCache() {
            cacheValid = false;
        }

        void StartAnimation() {
            animationStartTime = std::chrono::steady_clock::now();
            animationDuration = chartConfig.animationDuration;
            animationComplete = false;
        }

        void UpdateAnimation() {
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - animationStartTime);
            float progress = static_cast<float>(elapsed.count()) / (animationDuration * 1000.0f);

            if (progress >= 1.0f) {
                animationComplete = true;
                progress = 1.0f;
            }

            // Apply animation effects (fade in, grow, etc.)
            // This could modify alpha, scale, or other visual properties
            // For now, just trigger invalidation to keep animation running
            if (!animationComplete) {
                RequestRedraw();
            }
        }

        void UpdateTooltip(const Point2Di& mousePos) {
            if (!chartConfig.dataSource || chartConfig.dataSource->GetPointCount() == 0) {
                showTooltip = false;
                return;
            }

            // Find nearest data point
            ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
            double mouseDataX = transform.ScreenToDataX(mousePos.x - GetX());
            double mouseDataY = transform.ScreenToDataY(mousePos.y - GetY());

            size_t nearestIndex = SIZE_MAX;
            double nearestDistance = std::numeric_limits<double>::max();

            for (size_t i = 0; i < chartConfig.dataSource->GetPointCount(); ++i) {
                auto point = chartConfig.dataSource->GetPoint(i);
                double distance = std::sqrt((point.x - mouseDataX) * (point.x - mouseDataX) +
                                            (point.y - mouseDataY) * (point.y - mouseDataY));

                if (distance < nearestDistance) {
                    nearestDistance = distance;
                    nearestIndex = i;
                }
            }

            // Show tooltip if point is close enough
            if (nearestIndex != SIZE_MAX && nearestDistance < cachedDataBounds.GetXRange() * 0.05) {
                auto point = chartConfig.dataSource->GetPoint(nearestIndex);

                tooltipText = "X: " + ChartRenderingHelpers::FormatAxisLabel(point.x) +
                              "\nY: " + ChartRenderingHelpers::FormatAxisLabel(point.y);

                if (!point.label.empty()) {
                    tooltipText = point.label + "\n" + tooltipText;
                }

                tooltipPosition = mousePos;
                hoveredPointIndex = nearestIndex;
                showTooltip = true;
                RequestRedraw();
            } else {
                showTooltip = false;
                hoveredPointIndex = SIZE_MAX;
                RequestRedraw();
            }
        }

        void DrawTooltip(IRenderContext* ctx) {
            if (!showTooltip || tooltipText.empty()) return;

            // Measure tooltip text
            auto textSize = ChartRenderingHelpers::MeasureText(ctx, tooltipText, "Arial", 11.0f);

            // Calculate tooltip box
            float padding = 8.0f;
            float boxWidth = textSize.x + padding * 2;
            float boxHeight = textSize.y + padding * 2;

            // Position tooltip (avoid going off-screen)
            float tooltipX = tooltipPosition.x + 10;
            float tooltipY = tooltipPosition.y - boxHeight - 10;

            if (tooltipX + boxWidth > GetX() + GetWidth()) {
                tooltipX = tooltipPosition.x - boxWidth - 10;
            }
            if (tooltipY < GetY()) {
                tooltipY = tooltipPosition.y + 10;
            }

            // Draw tooltip background
            ctx->SetFillColor(Color(255, 255, 224, 240)); // Light yellow with alpha
            ctx->FillRoundedRectangle(tooltipX, tooltipY, boxWidth, boxHeight, 4.0f);

            // Draw tooltip border
            ctx->SetStrokeColor(Color(128, 128, 128));
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawRoundedRectangle(tooltipX, tooltipY, boxWidth, boxHeight, 4.0f);

            // Draw tooltip text
            ctx->SetTextColor(Color(0, 0, 0));
            ctx->SetFont("Arial", 11.0f);
            ctx->DrawText(tooltipText, tooltipX + padding, tooltipY + padding + textSize.y);
        }

        void DrawSelectionIndicators(IRenderContext* ctx) {
            if (hoveredPointIndex == SIZE_MAX || !chartConfig.dataSource) return;

            auto point = chartConfig.dataSource->GetPoint(hoveredPointIndex);
            ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
            Point2Di screenPos = transform.DataToScreen(point.x, point.y);

            // Draw highlight circle around hovered point
            ctx->SetStrokeColor(Color(255, 165, 0)); // Orange
            ctx->SetStrokeWidth(3.0f);
            ctx->DrawCircle(screenPos.x, screenPos.y, 8.0f);
        }

        void DrawEmptyState(CanvasContext* canvasCtx) {
            IRenderContext* ctx = canvasCtx->GetRenderInterface();
            if (!ctx) return;

            // Draw background
            ctx->SetFillColor(Color(248, 248, 248));
            ctx->FillRectangle(GetX(), GetY(), GetWidth(), GetHeight());

            // Draw border
            ctx->SetStrokeColor(Color(200, 200, 200));
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawRectangle(GetX(), GetY(), GetWidth(), GetHeight());

            // Draw message
            std::string message = "No data to display";
            ctx->SetTextColor(Color(128, 128, 128));
            ctx->SetFont("Arial", 14.0f);

            auto textSize = ChartRenderingHelpers::MeasureText(ctx, message, "Arial", 14.0f);
            float textX = GetX() + GetWidth() / 2.0f - textSize.x / 2.0f;
            float textY = GetY() + GetHeight() / 2.0f;

            ctx->DrawText(message, textX, textY);
        }

        void DrawErrorState(IRenderContext* ctx) {
            // Draw error message
            std::string errorMsg = "Error rendering chart";
            ctx->SetTextColor(Color(255, 0, 0));
            ctx->SetFont("Arial", 12.0f);

            auto textSize = ChartRenderingHelpers::MeasureText(ctx, errorMsg, "Arial", 12.0f);
            float textX = GetX() + 10;
            float textY = GetY() + GetHeight() - 20;

            ctx->DrawText(errorMsg, textX, textY);
        }
    };

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    std::unique_ptr<UltraCanvasChartElement> CreateChartElement(const std::string& id, long uid,
                                                                int x, int y, int width, int height) {
        return std::make_unique<UltraCanvasChartElement>(id, uid, x, y, width, height);
    }

    std::unique_ptr<UltraCanvasChartElement> CreateChartElementWithData(const std::string& id, long uid,
                                                                        int x, int y, int width, int height,
                                                                        std::shared_ptr<IChartDataSource> data,
                                                                        ChartType type = ChartType::Line) {
        auto element = std::make_unique<UltraCanvasChartElement>(id, uid, x, y, width, height);

        ChartConfiguration config;
        switch (type) {
            case ChartType::Line:
                config = UltraCanvasChartRenderer::CreateLineChart(data, "Chart");
                break;
            case ChartType::Bar:
                config = UltraCanvasChartRenderer::CreateBarChart(data, "Chart");
                break;
            case ChartType::Scatter:
                config = UltraCanvasChartRenderer::CreateScatterPlot(data, "Chart");
                break;
            case ChartType::Area:
                config = UltraCanvasChartRenderer::CreateAreaChart(data, "Chart");
                break;
            case ChartType::Pie:
                config = UltraCanvasChartRenderer::CreatePieChart(data, "Chart");
                break;
            default:
                config = UltraCanvasChartRenderer::CreateLineChart(data, "Chart");
                break;
        }

        element->SetChartConfiguration(config);
        return element;
    }

} // namespace UltraCanvas

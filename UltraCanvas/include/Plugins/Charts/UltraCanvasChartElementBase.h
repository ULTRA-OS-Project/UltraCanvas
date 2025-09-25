// include/Plugins/Charts/UltraCanvasChartElementBase.h
// Base class for all chart elements with common functionality
// Version: 1.0.0
// Last Modified: 2025-09-10
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasTooltipManager.h"
#include "UltraCanvasRenderContext.h"
#include "Plugins/Charts/UltraCanvasChartDataStructures.h"
#include <memory>
#include <chrono>
#include <functional>

namespace UltraCanvas {


// =============================================================================
// BASE CHART ELEMENT CLASS
// =============================================================================

class UltraCanvasChartElementBase : public UltraCanvasUIElement {
public:
    protected:
        // Common chart data
        std::shared_ptr<IChartDataSource> dataSource;
        std::string chartTitle;

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
        ChartPlotArea cachedPlotArea;
        ChartDataBounds cachedDataBounds;
        bool cacheValid = false;

        // Enhanced tooltip configuration
//    TooltipContentType tooltipContentType = TooltipContentType::Basic;
        std::string seriesName = "";
        std::string financialSymbol = "";
        std::string statisticalMetric = "";
        std::function<std::string(const ChartDataPoint &, size_t)> customTooltipGenerator;

        // Tooltip tracking
        size_t hoveredPointIndex = SIZE_MAX;
        bool isTooltipActive = false;

        // Chart styling
        Color backgroundColor = Color(255, 255, 255, 255);
        Color plotAreaColor = Color(250, 250, 250, 255);
        bool showGrid = true;
        Color gridColor = Color(220, 220, 220, 255);

        // Interactive features
        bool enableTooltips = true;
        bool enableZoom = false;
        bool enablePan = false;
        bool enableSelection = false;

    public:
        UltraCanvasChartElementBase(const std::string &id, long uid, int x, int y, int width, int height) :
                UltraCanvasUIElement(id, uid, x, y, width, height) {};
//    UltraCanvasChartElementBase(const std::string& id, long uid, int x, int y, int width, int height)
//            : UltraCanvasUIElement(id, uid, x, y, width, height) {
//
//        // Enable interactive features by default
//
//        // Set default tooltip style
//        //UltraCanvasTooltipManager::SetStyle(ChartTooltipStyles::CreateMinimalStyle());
//
//        // Initialize animation time
//        //animationStartTime = std::chrono::steady_clock::now();
//    }

        virtual ~UltraCanvasChartElementBase() = default;

        // =============================================================================
        // PURE VIRTUAL METHODS - MUST BE IMPLEMENTED BY DERIVED CLASSES
        // =============================================================================

        // Pure virtual render method - each chart type implements its own rendering
        virtual void RenderChart(IRenderContext *ctx) = 0;

        // Pure virtual method to get chart type
//    virtual ChartType GetChartType() const = 0;

        // Pure virtual method to handle chart-specific mouse interactions
        virtual bool HandleChartMouseMove(const Point2Di &mousePos) = 0;

        // =============================================================================
        // DATA MANAGEMENT (COMMON)
        // =============================================================================

        void SetDataSource(std::shared_ptr<IChartDataSource> data);

        std::shared_ptr<IChartDataSource> GetDataSource() const { return dataSource; }

        void SetChartTitle(const std::string &title) {
            chartTitle = title;
            RequestRedraw();
        }

        const std::string &GetChartTitle() const {
            return chartTitle;
        }

        // =============================================================================
        // TOOLTIP CONFIGURATION METHODS (COMMON)
        // =============================================================================

        void SetSeriesName(const std::string &name) {
            seriesName = name;
        }

        void SetCustomTooltipGenerator(std::function<std::string(const ChartDataPoint &, size_t)> generator) {
            customTooltipGenerator = generator;
//        tooltipContentType = TooltipContentType::Custom;
        }

        // =============================================================================
        // STYLING METHODS (COMMON)
        // =============================================================================

        void SetBackgroundColor(const Color &color) {
            backgroundColor = color;
            RequestRedraw();
        }

        void SetPlotAreaColor(const Color &color) {
            plotAreaColor = color;
            RequestRedraw();
        }

        void SetGridEnabled(bool enabled) {
            showGrid = enabled;
            RequestRedraw();
        }

        void SetGridColor(const Color &color) {
            gridColor = color;
            RequestRedraw();
        }

        // =============================================================================
        // INTERACTIVE FEATURES (COMMON)
        // =============================================================================

        void SetEnableZoom(bool enable) {
            enableZoom = enable;
        }

        void SetEnablePan(bool enable) {
            enablePan = enable;
        }

        void SetEnableTooltips(bool enable) {
            enableTooltips = enable;
        }

        void SetEnableAnimations(bool enable) {
            animationEnabled = enable;
        }

        void SetEnableSelection(bool enable) {
            enableSelection = enable;
        }

        void SetZoom(float zoom) {
            zoomLevel = std::clamp(zoom, 0.1f, 10.0f);
            InvalidateCache();
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
            InvalidateCache();
            RequestRedraw();
        }

        void SetPan(const Point2Di &offset) {
            panOffset = offset;
            InvalidateCache();
            RequestRedraw();
        }

        const Point2Di &GetPan() const {
            return panOffset;
        }

        // =============================================================================
        // MAIN RENDERING OVERRIDE - CALLS DERIVED CLASS RenderChart()
        // =============================================================================

        void Render() override;

        // =============================================================================
        // EVENT HANDLING - CALLS DERIVED CLASS METHODS
        // =============================================================================

        bool OnEvent(const UCEvent &event) override;

    protected:
        // =============================================================================
        // COMMON HELPER METHODS
        // =============================================================================

        void InvalidateCache() { cacheValid = false; }

        void StartAnimation() {
            animationComplete = false;
            animationStartTime = std::chrono::steady_clock::now();
        }

        virtual void UpdateRenderingCache();

        void UpdateAnimation();

        virtual ChartPlotArea CalculatePlotArea();
        virtual ChartDataBounds CalculateDataBounds();

        virtual void RenderCommonBackground(IRenderContext *ctx);
        virtual void RenderGrid(IRenderContext *ctx);
        virtual void RenderAxes(IRenderContext *ctx);
        virtual void RenderAxisLabels(IRenderContext *ctx);

        std::string FormatAxisLabel(double value);

        void DrawSelectionIndicators(IRenderContext *ctx);

        void DrawEmptyState(IRenderContext *ctx);

        // =============================================================================
        // MOUSE EVENT HANDLING (COMMON)
        // =============================================================================

        bool HandleMouseMove(const UCEvent &event);

        bool HandleMouseDown(const UCEvent &event);

        bool HandleMouseUp(const UCEvent &event);

        bool HandleMouseWheel(const UCEvent &event);

        bool HandleKeyDown(const UCEvent &event);

        // =============================================================================
        // TOOLTIP INTEGRATION WITH EXISTING SYSTEM
        // =============================================================================

        void ShowChartPointTooltip(const Point2Di &mousePos, const ChartDataPoint &point, size_t index) {
            std::string tooltipContent = GenerateTooltipContent(point, index);
            UltraCanvasTooltipManager::UpdateAndShowTooltip(this->window, tooltipContent, mousePos);
            isTooltipActive = true;
            hoveredPointIndex = index;
        }

//
        void HideTooltip();

        virtual std::string GenerateTooltipContent(const ChartDataPoint &point, size_t index) {

            if (customTooltipGenerator) {
                return customTooltipGenerator(point, index);
            }

            std::string content;

            if (!seriesName.empty()) {
                content += seriesName + "\n";
            }

            // Add X value
            if (!point.label.empty()) {
                content += "X: " + point.label + "\n";
            } else {
                content += "X: " + FormatAxisLabel(point.x) + "\n";
            }

            // Add Y value
            content += "Y: " + FormatAxisLabel(point.y);

            // Add additional data if available
//            if (point.z.has_value()) {
//                content += "\nZ: " + FormatAxisLabel(point.z.value());
//            }
//
//            if (point.size.has_value()) {
//                content += "\nSize: " + FormatAxisLabel(point.size.value());
//            }

            return content;
        }
};

// Generic Chart Factory with Data
    template<typename ChartElementType>
    std::shared_ptr<ChartElementType> CreateChartElementWithData(
            const std::string& id, long uid, int x, int y, int width, int height,
            std::shared_ptr<IChartDataSource> data, const std::string& title = "") {

        auto element = std::make_shared<ChartElementType>(id, uid, x, y, width, height);
        element->SetDataSource(data);
        if (!title.empty()) {
            element->SetTitle(title);
        }
        return element;
    }

} // namespace UltraCanvas

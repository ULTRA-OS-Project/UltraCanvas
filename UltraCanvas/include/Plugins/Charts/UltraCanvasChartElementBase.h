// include/Plugins/Charts/UltraCanvasChartElementBase.h
// Base class for all chart elements with common functionality
// Version: 1.1.0
// Last Modified: 2025-01-27
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
// X-AXIS LABEL MODE ENUM
// =============================================================================

    enum class XAxisLabelMode {
        NumericValue,    // Use the 'x' numeric value (default)
        DataLabel       // Use the 'label' string property
    };

// =============================================================================
// BASE CHART ELEMENT CLASS
// =============================================================================

    class UltraCanvasChartElementBase : public UltraCanvasUIElement {
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
        std::string seriesName = "";
        std::string financialSymbol = "";
        std::string statisticalMetric = "";
        std::function<std::string(const ChartDataPoint&, size_t)> customTooltipGenerator;

        // Tooltip tracking
        size_t hoveredPointIndex = SIZE_MAX;
        bool isTooltipActive = false;

        // Chart styling
        Color backgroundColor = Color(255, 255, 255, 255);
        Color plotAreaColor = Color(250, 250, 250, 255);
        bool showGrid = true;
        bool showAxes = true;
        Color gridColor = Color(220, 220, 220, 255);

        // X-axis label configuration
        XAxisLabelMode xAxisLabelMode = XAxisLabelMode::NumericValue;
        bool rotateXAxisLabels = false;
        float xAxisLabelRotation = 0.0f; // Rotation angle in degrees
        bool useIndexBasedPositioning = false; // When true, use index-based positioning for data points

        // Interactive features
        bool enableTooltips = true;
        bool enableZoom = false;
        bool enablePan = false;
        bool enableSelection = false;

        float pointRadius = 3.0f;

        // Value label properties
        bool showValueLabels = true;
        Color valueLabelColor = Color(0, 0, 0, 255);
        float valueLabelFontSize = 10.0f;
        int valueLabelOffset = 20;  // Pixels above the point
        bool valueLabelAutoRotate = false;  // Auto-rotate to avoid overlap
        float valueLabelRotation = 0.0f;    // Manual rotation angle in degrees
        enum class ValueLabelPosition {
            LabelAbove,
            LabelBelow,
            LabelLeft,
            LabelRight,
            LabelAuto  // Automatically choose best position
        };
        ValueLabelPosition valueLabelPosition = ValueLabelPosition::LabelAbove;

    public:
        UltraCanvasChartElementBase(const std::string& id, long uid, int x, int y, int width, int height) :
                UltraCanvasUIElement(id, uid, x, y, width, height) {};

        virtual ~UltraCanvasChartElementBase() = default;

        // =============================================================================
        // PURE VIRTUAL METHODS - MUST BE IMPLEMENTED BY DERIVED CLASSES
        // =============================================================================

        // Pure virtual render method - each chart type implements its own rendering
        virtual void RenderChart(IRenderContext* ctx) = 0;

        // Pure virtual method to handle chart-specific mouse interactions
        virtual bool HandleChartMouseMove(const Point2Di& mousePos) = 0;

        // =============================================================================
        // DATA MANAGEMENT (COMMON)
        // =============================================================================

        void SetDataSource(std::shared_ptr<IChartDataSource> data);

        std::shared_ptr<IChartDataSource> GetDataSource() const { return dataSource; }

        void SetChartTitle(const std::string& title) {
            chartTitle = title;
            RequestRedraw();
        }

        const std::string& GetChartTitle() const {
            return chartTitle;
        }

        // =============================================================================
        // X-AXIS LABEL CONFIGURATION
        // =============================================================================

        void SetXAxisLabelMode(XAxisLabelMode mode) {
            xAxisLabelMode = mode;
            useIndexBasedPositioning = (mode == XAxisLabelMode::DataLabel);
            cacheValid = false;
            RequestRedraw();
        }

        XAxisLabelMode GetXAxisLabelMode() const {
            return xAxisLabelMode;
        }

        void SetRotateXAxisLabels(bool rotate, float angle = 45.0f) {
            rotateXAxisLabels = rotate;
            xAxisLabelRotation = angle;
            RequestRedraw();
        }

        bool GetRotateXAxisLabels() const {
            return rotateXAxisLabels;
        }

        float GetXAxisLabelRotation() const {
            return xAxisLabelRotation;
        }

        // =============================================================================
        // TOOLTIP CONFIGURATION METHODS (COMMON)
        // =============================================================================

        void SetSeriesName(const std::string& name) {
            seriesName = name;
        }

        const std::string& GetSeriesName() const {
            return seriesName;
        }

        void SetEnableTooltips(bool enable) {
            enableTooltips = enable;
            if (!enable && isTooltipActive) {
                HideTooltip();
            }
        }

        bool GetEnableTooltips() const {
            return enableTooltips;
        }

        void SetCustomTooltipGenerator(std::function<std::string(const ChartDataPoint&, size_t)> generator) {
            customTooltipGenerator = generator;
        }

        // =============================================================================
        // VISUAL CONFIGURATION (COMMON)
        // =============================================================================

        void SetBackgroundColor(const Color& color) {
            backgroundColor = color;
            RequestRedraw();
        }

        void SetPlotAreaColor(const Color& color) {
            plotAreaColor = color;
            RequestRedraw();
        }

        void SetGridColor(const Color& color) {
            gridColor = color;
            RequestRedraw();
        }

        void SetShowGrid(bool show) {
            showGrid = show;
            RequestRedraw();
        }

        void SetShowAxes(bool show) {
            showAxes = show;
            RequestRedraw();
        }

        void SetShowValueLabels(bool show) {
            showValueLabels = show;
            RequestRedraw();
        }

        void SetPointRadius(float radius) {
            pointRadius = std::max(0.0f, radius);
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

        void SetEnableSelection(bool enable) {
            enableSelection = enable;
        }

        bool GetEnableZoom() const { return enableZoom; }
        bool GetEnablePan() const { return enablePan; }
        bool GetEnableSelection() const { return enableSelection; }

        void SetTitle(const std::string& title) {
            chartTitle = title;
            RequestRedraw();
        }

        // =============================================================================
        // RENDERING OVERRIDE FROM UIELEM
        // =============================================================================

        void Render(IRenderContext* ctx) override;

        // =============================================================================
        // EVENT HANDLING OVERRIDE
        // =============================================================================

        bool OnEvent(const UCEvent& event);

        // =============================================================================
        // PROTECTED RENDERING HELPERS (COMMON)
        // =============================================================================

    protected:
        virtual void UpdateRenderingCache() {
            if (!cacheValid) {
                // Calculate plot area based on margin needs
                cachedPlotArea = CalculatePlotArea();
                // Calculate data bounds from current data source
                cachedDataBounds = CalculateDataBounds();
                cacheValid = true;
            }
        }

        virtual ChartPlotArea CalculatePlotArea() {
            // Default implementation with margins for axes and labels
            int marginLeft = 60;
            int marginRight = 20;
            int marginTop = 40;
            int marginBottom = 50;

            return ChartPlotArea(
                    GetX() + marginLeft,
                    GetY() + marginTop,
                    GetWidth() - marginLeft - marginRight,
                    GetHeight() - marginTop - marginBottom
            );
        }

        virtual ChartDataBounds CalculateDataBounds();

        void InvalidateCache() {
            cacheValid = false;
        }

        void StartAnimation() {
            animationStartTime = std::chrono::steady_clock::now();
            animationComplete = false;
        }

        void UpdateAnimation() {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration<float>(now - animationStartTime).count();
            if (elapsed >= animationDuration) {
                animationComplete = true;
            }
        }

        float GetAnimationProgress() const {
            if (animationComplete) return 1.0f;
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration<float>(now - animationStartTime).count();
            return std::min(1.0f, elapsed / animationDuration);
        }

        virtual void RenderCommonBackground(IRenderContext* ctx);
        virtual void RenderGrid(IRenderContext* ctx);
        virtual void RenderAxes(IRenderContext* ctx);
        virtual void RenderAxisLabels(IRenderContext* ctx);
//        virtual void RenderXAxisLabelsWithMode(IRenderContext* ctx); // New method for X-axis label handling

        void RenderValueLabels(IRenderContext *ctx, const std::vector<Point2Df> &screenPositions);
        Point2Df CalculateValueLabelPosition(const Point2Df &pointPos, size_t index, size_t totalPoints);

        virtual float GetXAxisLabelPosition(size_t dataIndex, size_t totalPoints);

        std::string FormatAxisLabel(double value);
        void DrawSelectionIndicators(IRenderContext* ctx);
        void DrawEmptyState(IRenderContext* ctx);

        bool HandleMouseMove(const UCEvent& event);
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);
        bool HandleMouseWheel(const UCEvent& event);

        // Helper method to get screen position for a data point
        Point2Df GetDataPointScreenPosition(size_t index, const ChartDataPoint& point) {
            if (useIndexBasedPositioning && dataSource) {
                // Use index-based positioning (for categorical data with labels)
                size_t totalPoints = dataSource->GetPointCount();
                if (totalPoints <= 1) {
                    // Single point - center it
                    float x = cachedPlotArea.x + cachedPlotArea.width / 2;
                    ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
                    return Point2Df(x, transform.DataToScreen(point.x, point.y).y);
                } else {
                    // Multiple points - distribute evenly
                    float x = cachedPlotArea.x + (index * cachedPlotArea.width / (totalPoints - 1));
                    ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
                    return Point2Df(x, transform.DataToScreen(point.x, point.y).y);
                }
            } else {
                // Use actual x coordinate positioning (for numeric data)
                ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
                return transform.DataToScreen(point.x, point.y);
            }
        }

        bool IsUsingIndexBasedPositioning() const { return useIndexBasedPositioning; }
        // =============================================================================
        // TOOLTIP INTEGRATION WITH EXISTING SYSTEM
        // =============================================================================

        void ShowChartPointTooltip(const Point2Di& mousePos, const ChartDataPoint& point, size_t index) {
            std::string tooltipContent = GenerateTooltipContent(point, index);
            auto windowMousePos = ConvertContainerToWindowCoordinates(mousePos);
            UltraCanvasTooltipManager::UpdateAndShowTooltip(this->window, tooltipContent, windowMousePos);
            isTooltipActive = true;
            hoveredPointIndex = index;
        }

        void HideTooltip();

        virtual std::string GenerateTooltipContent(const ChartDataPoint& point, size_t index) {
            if (customTooltipGenerator) {
                return customTooltipGenerator(point, index);
            }

            std::string content;

            if (!seriesName.empty()) {
                content += seriesName + "\n";
            }

            // Add X value or label based on mode
            if (xAxisLabelMode == XAxisLabelMode::DataLabel && !point.label.empty()) {
                content += "X: " + point.label + "\n";
            } else {
                content += "X: " + FormatAxisLabel(point.x) + "\n";
            }

            // Add Y value
            content += "Y: " + FormatAxisLabel(point.y);

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
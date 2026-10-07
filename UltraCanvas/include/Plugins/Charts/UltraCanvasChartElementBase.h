// include/Plugins/Charts/UltraCanvasChartElementBase.h
// Base class for all chart elements with common functionality
// Version: 1.2.0 - x-axis zoom and pan that work (charts opt in); the wheel and
//                  drags are left to the parent when nothing zooms; plot area
//                  recomputed on every resize; the element's own background
// Version: 1.1.0
// Last Modified: 2026-10-07
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
        Point2Di lastMousePos;

        // Free for a subclass that zooms or pans its own way (the tree map
        // does); the base class's x-axis zoom below does not use them.
        bool isZooming = false;
        float zoomLevel = 1.0f;
        Point2Di panOffset;

        // The visible part of the x axis, as fractions of the whole range
        // (0..1 = everything). Only the wheel and drags of a chart that returns
        // true from SupportsXAxisZoom() move it, with SetEnableZoom /
        // SetEnablePan on. In numeric mode it narrows cachedDataBounds' x
        // range; in DataLabel mode it spreads the evenly spaced points.
        double xViewStart = 0.0;
        double xViewEnd = 1.0;
        static constexpr double kMaxXAxisZoom = 50.0;   // narrowest view: 1/50 of the range
        bool isPanning = false;
        int panStartPointerX = 0;
        double panStartViewStart = 0.0;

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

        // Chart styling. The background is the element's own
        // (UltraCanvasUIElement::backgroundColor), white unless set.
        Color plotAreaColor = Color(250, 250, 250, 255);
        bool showBackground = true;
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
        UltraCanvasChartElementBase(const std::string& id, int x, int y, int width, int height) :
                UltraCanvasUIElement(id, x, y, width, height) {
            backgroundColor = Color(255, 255, 255, 255);
        }

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

        // The element's background: the same colour UltraCanvasUIElement's
        // SetBackgroundColor / GetBackgroundColor set and read. This overload
        // only adds the repaint.
        void SetBackgroundColor(const Color& color) {
            UltraCanvasUIElement::SetBackgroundColor(color);
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

        // Wheel zoom of the x axis around the pointer (wheel up zooms in, down
        // out, up to 50x). The line, area and scatter charts implement it; on
        // other charts the flag is kept but the wheel goes to the parent. Off
        // by default. Even when on, a wheel turn that changes nothing - outside
        // the plot, or zooming out of the whole range - is left to the parent,
        // so a scrolling container still scrolls. Turning it off resets the view.
        void SetEnableZoom(bool enable) {
            enableZoom = enable;
            if (!enable) ResetZoom();
        }

        // Dragging a zoomed x axis sideways with the left button. Off by
        // default; only does something while a chart that zooms is zoomed in.
        void SetEnablePan(bool enable) {
            enablePan = enable;
            if (!enable) EndPan();
        }

        // Back to the whole x range.
        void ResetZoom() {
            if (!IsZoomed()) return;
            xViewStart = 0.0;
            xViewEnd = 1.0;
            InvalidateCache();
            RequestRedraw();
        }

        bool IsZoomed() const { return xViewEnd - xViewStart < 1.0 - 1e-9; }

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

        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

        // A new size (from SetBounds or from the layout's Arrange) makes the
        // plot area be worked out again on the next paint.
        using UltraCanvasUIElement::SetBounds;
        void SetBounds(const Rect2Df& b) override;
        void Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) override;

        // =============================================================================
        // EVENT HANDLING OVERRIDE
        // =============================================================================

        bool OnEvent(const UCEvent& event) override;

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
                ApplyXAxisView(cachedDataBounds);
                cacheValid = true;
            }
        }

        // Charts that place their points through GetDataPointScreenPosition
        // (numeric x through cachedDataBounds, or evenly spaced by index)
        // return true, and then the wheel and drags zoom and pan the x axis.
        virtual bool SupportsXAxisZoom() const { return false; }

        // Narrows the x range of `bounds` to the visible part of the axis.
        void ApplyXAxisView(ChartDataBounds& bounds) const {
            if (!IsZoomed()) return;
            double fullMin = bounds.minX;
            double range = bounds.maxX - bounds.minX;
            bounds.minX = fullMin + xViewStart * range;
            bounds.maxX = fullMin + xViewEnd * range;
        }

        // Moves the visible part of the x axis, kept inside the whole range.
        void SetXAxisView(double start, double span);

        // Ends a pan drag and lets go of the mouse it captured.
        void EndPan();

        // Screen x of the index-th of totalPoints evenly spaced points
        // (DataLabel mode), through the visible part of the axis.
        double IndexToScreenX(size_t index, size_t totalPoints) const {
            if (totalPoints <= 1) return cachedPlotArea.x + cachedPlotArea.width / 2;
            double t = static_cast<double>(index) / static_cast<double>(totalPoints - 1);
            return cachedPlotArea.x + (t - xViewStart) / (xViewEnd - xViewStart) * cachedPlotArea.width;
        }

        // False for a point scrolled out of the plot by the zoom, so it is
        // neither hovered nor drawn outside the plot.
        bool IsScreenXInView(double screenX) const {
            return !IsZoomed() ||
                   (screenX >= cachedPlotArea.x - 0.5 && screenX <= cachedPlotArea.GetRight() + 0.5);
        }

        // While zoomed: clips drawing to the plot's columns (value labels
        // above and below the plot stay visible). Returns whether it pushed a
        // state the caller pops.
        bool PushXAxisViewClip(IRenderContext* ctx) {
            if (!IsZoomed()) return false;
            ctx->PushState();
            ctx->ClipRect(Rect2Dd(cachedPlotArea.x, 0, cachedPlotArea.width, GetHeight()));
            return true;
        }

        virtual ChartPlotArea CalculatePlotArea() {
            // Default implementation with margins for axes and labels
            int marginLeft = 60;
            int marginRight = 20;
            int marginTop = 40;
            int marginBottom = 50;

            return ChartPlotArea(
                    marginLeft,
                    marginTop,
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

        void RenderValueLabels(IRenderContext *ctx, const std::vector<Point2Dd> &screenPositions);
        Point2Dd CalculateValueLabelPosition(const Point2Dd &pointPos, size_t index, size_t totalPoints);

        virtual double GetXAxisLabelPosition(size_t dataIndex, size_t totalPoints);

        std::string FormatAxisLabel(double value);
        void DrawSelectionIndicators(IRenderContext* ctx);
        void DrawEmptyState(IRenderContext* ctx);

        bool HandleMouseMove(const UCEvent& event);
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);
        bool HandleMouseWheel(const UCEvent& event);

        // Helper method to get screen position for a data point
        Point2Dd GetDataPointScreenPosition(size_t index, const ChartDataPoint& point) {
            if (useIndexBasedPositioning && dataSource) {
                // Use index-based positioning (for categorical data with labels):
                // one point is centred, several are spread evenly over the
                // visible part of the axis
                ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
                return Point2Dd(IndexToScreenX(index, dataSource->GetPointCount()),
                                transform.DataToScreen(point.x, point.y).y);
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
            auto windowMousePos = MapFromLocal(mousePos, nullptr);
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
            const std::string& id, int x, int y, int width, int height,
            std::shared_ptr<IChartDataSource> data, const std::string& title = "") {

        auto element = std::make_shared<ChartElementType>(id, x, y, width, height);
        element->SetDataSource(data);
        if (!title.empty()) {
            element->SetTitle(title);
        }
        return element;
    }

} // namespace UltraCanvas
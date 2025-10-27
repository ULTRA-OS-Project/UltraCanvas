// include/Plugins/Charts/UltraCanvasWaterfallChart.h
// Waterfall chart element with cumulative flow visualization and customizable styling
// Version: 1.0.0
// Last Modified: 2025-09-20
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartElementBase.h"
#include "UltraCanvasChartDataStructures.h"
#include <memory>
#include <vector>
#include <string>

namespace UltraCanvas {

// =============================================================================
// WATERFALL CHART DATA STRUCTURES
// =============================================================================

    struct WaterfallChartDataPoint {
        std::string label;          // Category/step name
        double value;               // Change amount (positive or negative)
        double cumulativeValue;     // Running total (calculated automatically)
        bool isSubtotal;           // True for intermediate subtotal bars
        bool isTotal;              // True for final total bar
        Color customColor;         // Optional custom color override
        std::string category;      // Optional grouping category

        WaterfallChartDataPoint(const std::string& label, double value, bool isSubtotal = false, bool isTotal = false)
                : label(label), value(value), cumulativeValue(0.0), isSubtotal(isSubtotal), isTotal(isTotal),
                  customColor(Color(0, 0, 0, 0)), category("") {}
    };

    class WaterfallChartDataVector : public IChartDataSource {
    private:
        std::vector<WaterfallChartDataPoint> waterfallData;
        double startingValue = 0.0;
        bool isNeedCalculateCumulative = true;
    public:
        // IChartDataSource implementation
        size_t GetPointCount() const override { return waterfallData.size(); }

        ChartDataPoint GetPoint(size_t index) override {
            if (index >= waterfallData.size()) return ChartDataPoint(0, 0);
            if (isNeedCalculateCumulative) {
                RecalculateCumulativeValues();
            }
            const auto& point = waterfallData[index];
            return ChartDataPoint(static_cast<double>(index), point.cumulativeValue, point.value);
        }

        // Waterfall-specific methods
//        void SetStartingValue(double startValue) {
//            startingValue = startValue;
//            RecalculateCumulativeValues();
//        }

        void AddWaterfallPoint(const WaterfallChartDataPoint& point) {
            waterfallData.push_back(point);
            RecalculateCumulativeValues();
        }

        void AddWaterfallPoint(const std::string& label, double value, bool isSubtotal = false, bool isTotal = false) {
            waterfallData.emplace_back(label, value, isSubtotal, isTotal);
            RecalculateCumulativeValues();
        }

        void ClearData() {
            waterfallData.clear();
//            startingValue = 0.0;
        }

        const WaterfallChartDataPoint& GetWaterfallPoint(size_t index) const {
            static WaterfallChartDataPoint dummy("", 0);
            return (index < waterfallData.size()) ? waterfallData[index] : dummy;
        }

        void LoadFromArray(const std::vector<std::pair<std::string, double>>& data) {
            ClearData();
            for (const auto& pair : data) {
                AddWaterfallPoint(pair.first, pair.second);
            }
        }

//        double GetStartingValue() const { return startingValue; }
        double GetFinalValue() const {
            return waterfallData.empty() ? 0.0 : waterfallData.back().cumulativeValue;
        }

        bool SupportsStreaming() const override { return false; }
        void LoadFromCSV(const std::string& filePath) override {}
        void LoadFromArray(const std::vector<ChartDataPoint>& data) override {}

    private:
        void RecalculateCumulativeValues() {
            double cumulative = 0.0;
            for (auto& point : waterfallData) {
                if (point.isTotal) {
                    point.cumulativeValue = cumulative + point.value;
                } else if (point.isSubtotal) {
                    point.cumulativeValue = cumulative;
                } else {
                    cumulative += point.value;
                    point.cumulativeValue = cumulative;
                }
            }
        }
    };

// =============================================================================
// WATERFALL CHART ELEMENT
// =============================================================================

    class UltraCanvasWaterfallChartElement : public UltraCanvasChartElementBase {
    public:
        enum class ConnectionStyle {
            NoneStyle,           // No connecting lines
            Dotted,         // Dotted connecting lines
            Solid,          // Solid connecting lines
            Dashed          // Dashed connecting lines
        };

        enum class BarStyle {
            Standard,       // Standard rectangular bars
            Rounded,        // Rounded corner bars
            Gradient        // Gradient fill bars
        };

    private:
        // Waterfall-specific styling
        Color positiveBarColor = Color(46, 125, 50, 255);      // Green for positive values
        Color negativeBarColor = Color(211, 47, 47, 255);      // Red for negative values
        Color subtotalBarColor = Color(158, 158, 158, 255);    // Gray for subtotals
        Color totalBarColor = Color(25, 118, 210, 255);        // Blue for totals
        Color startingBarColor = Color(96, 125, 139, 255);     // Blue-gray for starting value

        Color connectionLineColor = Color(117, 117, 117, 255);  // Gray for connecting lines
        float connectionLineWidth = 1.5f;
        ConnectionStyle connectionStyle = ConnectionStyle::Dotted;

        Color barBorderColor = Color(0, 0, 0, 255);
        float barBorderWidth = 1.0f;
        float barSpacing = 0.2f;        // Space between bars as ratio of bar width
        BarStyle barStyle = BarStyle::Standard;

        // Labels and text
        bool showValueLabels = true;
        bool showCumulativeLabels = false;
        Color labelTextColor = Color(33, 33, 33, 255);
        float labelFontSize = 10.0f;

        bool showStartingBar = true;
        bool enableConnectorLines = true;

        // Cached rendering data
        struct WaterfallRenderData {
            float barWidth;
            float barSpacing;
            std::vector<float> barX;
            std::vector<float> barY;
            std::vector<float> barHeight;
            std::vector<Color> barColors;
            bool isValid = false;
        } renderCache;

    public:
        UltraCanvasWaterfallChartElement(const std::string& id, long uid, int x, int y, int width, int height)
                : UltraCanvasChartElementBase(id, uid, x, y, width, height) {
            enableTooltips = true;
            enableZoom = false;  // Waterfall charts typically don't zoom
            enablePan = false;
        }

        // =============================================================================
        // CONFIGURATION METHODS
        // =============================================================================

        void SetPositiveBarColor(const Color& color) {
            positiveBarColor = color;
            renderCache.isValid = false;
            RequestRedraw();
        }

        void SetNegativeBarColor(const Color& color) {
            negativeBarColor = color;
            renderCache.isValid = false;
            RequestRedraw();
        }

        void SetSubtotalBarColor(const Color& color) {
            subtotalBarColor = color;
            renderCache.isValid = false;
            RequestRedraw();
        }

        void SetTotalBarColor(const Color& color) {
            totalBarColor = color;
            renderCache.isValid = false;
            RequestRedraw();
        }

        void SetStartingBarColor(const Color& color) {
            startingBarColor = color;
            renderCache.isValid = false;
            RequestRedraw();
        }

        void SetConnectionLineStyle(ConnectionStyle style, const Color& color = Color(117, 117, 117, 255), float width = 1.5f) {
            connectionStyle = style;
            connectionLineColor = color;
            connectionLineWidth = width;
            RequestRedraw();
        }

        void SetBarStyle(BarStyle style) {
            barStyle = style;
            RequestRedraw();
        }

        void SetBarBorder(const Color& color, float width) {
            barBorderColor = color;
            barBorderWidth = width;
            RequestRedraw();
        }

        void SetBarSpacing(float spacing) {
            barSpacing = std::clamp(spacing, 0.0f, 0.8f);
            renderCache.isValid = false;
            RequestRedraw();
        }

        void SetShowValueLabels(bool show) {
            showValueLabels = show;
            RequestRedraw();
        }

        void SetShowCumulativeLabels(bool show) {
            showCumulativeLabels = show;
            RequestRedraw();
        }

        void SetLabelStyle(const Color& textColor, float fontSize) {
            labelTextColor = textColor;
            labelFontSize = fontSize;
            RequestRedraw();
        }

        void SetShowStartingBar(bool show) {
            showStartingBar = show;
            renderCache.isValid = false;
            RequestRedraw();
        }

        void SetEnableConnectorLines(bool enable) {
            enableConnectorLines = enable;
            RequestRedraw();
        }

        void SetWaterfallDataSource(std::shared_ptr<WaterfallChartDataVector> ds) {
            dataSource = ds;
            renderCache.isValid = false;
            cacheValid = false;
            RequestRedraw();
        }

        std::shared_ptr<WaterfallChartDataVector> GetWaterfallDataSource() const {
            return std::dynamic_pointer_cast<WaterfallChartDataVector>(dataSource);
        }

        // =============================================================================
        // CHART RENDERING IMPLEMENTATION
        // =============================================================================

        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;

    protected:
        ChartDataBounds CalculateDataBounds() override;

    private:
        // =============================================================================
        // RENDERING HELPER METHODS
        // =============================================================================

        void UpdateRenderingCache() override;
        void CalculateBarDimensions();

        void RenderAxisLabels(UltraCanvas::IRenderContext *ctx) override;
        void DrawCommonBackground(IRenderContext* ctx);
        void DrawWaterfallBars(IRenderContext* ctx);
        void DrawConnectionLines(IRenderContext* ctx);
        void DrawValueLabels(IRenderContext* ctx);
        void DrawStartingBar(IRenderContext* ctx);

        void DrawSingleBar(IRenderContext* ctx, float x, float y, float width, float height,
                           const Color& fillColor, bool hasBorder = true);
        void DrawConnectionLine(IRenderContext* ctx, float x1, float y1, float x2, float y2);

        Color GetBarColor(const WaterfallChartDataPoint& point) const;
        std::string FormatValue(double value) const;

        size_t GetBarIndexAtPosition(const Point2Di& mousePos) const;
        std::string GenerateWaterfallTooltip(size_t index) const;
    };

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    inline std::shared_ptr<UltraCanvasWaterfallChartElement> CreateWaterfallChartElement(
            const std::string& id, long uid, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasWaterfallChartElement>(id, uid, x, y, width, height);
    }

    inline std::shared_ptr<UltraCanvasWaterfallChartElement> CreateWaterfallChartWithData(
            const std::string& id, long uid, int x, int y, int width, int height,
            std::shared_ptr<WaterfallChartDataVector> data, const std::string& title = "") {

        auto chart = CreateWaterfallChartElement(id, uid, x, y, width, height);
        chart->SetWaterfallDataSource(data);
        if (!title.empty()) {
            chart->SetChartTitle(title);
        }
        return chart;
    }

} // namespace UltraCanvas
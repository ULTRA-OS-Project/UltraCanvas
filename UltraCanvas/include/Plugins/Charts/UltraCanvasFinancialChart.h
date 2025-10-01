// include/Plugins/Charts/UltraCanvasFinancialChart.h
// Financial chart element with OHLC candlestick and volume rendering
// Version: 1.3.0
// Last Modified: 2025-09-15
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include "Plugins/Charts/UltraCanvasChartDataStructures.h"
#include <memory>
#include <vector>
#include <algorithm>

namespace UltraCanvas {

// =============================================================================
// FINANCIAL DATA STRUCTURES
// =============================================================================

    struct FinancialChartDataPoint : public ChartDataPoint {
        double open;
        double high;
        double low;
        double close;
        double volume;
        std::string date;

        FinancialChartDataPoint(double time, double openPrice, double highPrice,
                                double lowPrice, double closePrice, double vol = 0.0,
                                const std::string& dateStr = "")
                : ChartDataPoint(time, closePrice, 0.0, dateStr, vol),
                  open(openPrice), high(highPrice), low(lowPrice),
                  close(closePrice), volume(vol), date(dateStr) {}
    };

    class FinancialChartDataVector : public IChartDataSource {
    private:
        std::vector<FinancialChartDataPoint> financialData;

    public:
        size_t GetPointCount() const override { return financialData.size(); }

        ChartDataPoint GetPoint(size_t index) override {
            return financialData[index];
        }

        FinancialChartDataPoint GetFinancialPoint(size_t index) {
            return financialData[index];
        }

        bool SupportsStreaming() const override { return false; }

        void LoadFromCSV(const std::string& filePath) override;
        void LoadFromArray(const std::vector<ChartDataPoint>& data) override;
        void LoadFinancialData(const std::vector<FinancialChartDataPoint>& data);
        void AddFinancialPoint(const FinancialChartDataPoint& point);
        void Clear() { financialData.clear(); }

    protected:
        FinancialChartDataPoint ParseFinancialCSVLine(const std::string& line);
    };

// =============================================================================
// FINANCIAL CHART ELEMENT
// =============================================================================

    class UltraCanvasFinancialChartElement : public UltraCanvasChartElementBase {
    public:
        enum class CandleDisplayStyle {
            Candlestick,
            OHLCBars,
            HeikinAshi
        };

    private:
        // Financial chart styling
        Color bullishCandleColor = Color(0, 150, 0, 255);    // Green for rising prices
        Color bearishCandleColor = Color(200, 0, 0, 255);    // Red for falling prices
        Color wickLineColor = Color(80, 80, 80, 255);        // Gray for wicks
        Color volumeBarColor = Color(100, 100, 100, 128);    // Semi-transparent volume

        // Enhanced volume chart styling
        Color volumeChartBackgroundColor = Color(245, 245, 245, 255);  // Light gray background
        Color volumeChartBorderColor = Color(180, 180, 180, 255);      // Border for separation
        Color volumeAxisLabelColor = Color(80, 80, 80, 255);           // Volume axis labels
        Color volumeGridColor = Color(210, 210, 210, 255);             // Volume grid lines

        // Chart configuration
        CandleDisplayStyle candleStyle = CandleDisplayStyle::Candlestick;
        bool showVolumePanel = true;
        bool showMovingAverage = false;
        float candleWidthRatio = 0.8f;
        float volumePanelHeightRatio = 0.25f;

        // Enhanced volume configuration
        bool showVolumeAxisLabels = true;
        bool showVolumeBorder = true;
        int volumeSeparationHeight = 15;  // Gap between price and volume charts

        // Volume display options
        bool showVolumeMovingAverage = false;
        int volumeMovingAveragePeriod = 20;
        Color volumeMovingAverageColor = Color(80, 80, 200, 255);

        // Moving average settings
        int movingAveragePeriod = 20;
        Color movingAverageLineColor = Color(0, 0, 200, 255);

        // Cached rendering areas - enhanced for separate charts
        ChartPlotArea priceRenderArea;
        ChartPlotArea volumeRenderArea;
        ChartPlotArea volumeAxisArea;  // Separate area for volume Y-axis labels

    public:
        UltraCanvasFinancialChartElement(const std::string& id, long uid, int x, int y, int width, int height);

        // =============================================================================
        // FINANCIAL CHART CONFIGURATION
        // =============================================================================

        void SetCandleDisplayStyle(CandleDisplayStyle style) {
            candleStyle = style;
            RequestRedraw();
        }

        void SetBullishCandleColor(const Color& color) {
            bullishCandleColor = color;
            RequestRedraw();
        }

        void SetBearishCandleColor(const Color& color) {
            bearishCandleColor = color;
            RequestRedraw();
        }

        void SetMovingAverageColor(const Color& color) {
            movingAverageLineColor = color;
            RequestRedraw();
        }

        void SetWickLineColor(const Color& color) {
            wickLineColor = color;
            RequestRedraw();
        }

        void SetVolumeBarColor(const Color& color) {
            volumeBarColor = color;
            RequestRedraw();
        }

        // Enhanced volume chart styling methods
        void SetVolumeChartBackgroundColor(const Color& color) {
            volumeChartBackgroundColor = color;
            RequestRedraw();
        }

        void SetVolumeChartBorderColor(const Color& color) {
            volumeChartBorderColor = color;
            RequestRedraw();
        }

        void SetVolumeAxisLabelColor(const Color& color) {
            volumeAxisLabelColor = color;
            RequestRedraw();
        }

        void SetVolumeGridColor(const Color& color) {
            volumeGridColor = color;
            RequestRedraw();
        }

        void SetShowVolumePanel(bool show) {
            showVolumePanel = show;
            cacheValid = false;
            RequestRedraw();
        }

        void SetShowVolumeAxisLabels(bool show) {
            showVolumeAxisLabels = show;
            RequestRedraw();
        }

        void SetShowVolumeBorder(bool show) {
            showVolumeBorder = show;
            RequestRedraw();
        }

        void SetVolumeSeparationHeight(int height) {
            volumeSeparationHeight = std::max(5, std::min(30, height));
            cacheValid = false;
            RequestRedraw();
        }

        void SetShowVolumeMovingAverage(bool show, int period = 20) {
            showVolumeMovingAverage = show;
            volumeMovingAveragePeriod = period;
            RequestRedraw();
        }

        void SetVolumeMovingAverageColor(const Color& color) {
            volumeMovingAverageColor = color;
            RequestRedraw();
        }

        void SetShowMovingAverage(bool show, int period = 20) {
            showMovingAverage = show;
            movingAveragePeriod = period;
            RequestRedraw();
        }

        void SetCandleWidthRatio(float ratio) {
            candleWidthRatio = std::max(0.1f, std::min(1.0f, ratio));
            RequestRedraw();
        }

        void SetVolumePanelHeightRatio(float ratio) {
            volumePanelHeightRatio = std::max(0.1f, std::min(0.5f, ratio));
            cacheValid = false;
            RequestRedraw();
        }

        void SetFinancialDataSource(std::shared_ptr<FinancialChartDataVector> ds) {
            dataSource = ds;
        }

        std::shared_ptr<FinancialChartDataVector> GetFinancialDataSource() const {
            return std::dynamic_pointer_cast<FinancialChartDataVector>(dataSource);
        }

        // =============================================================================
        // CHART RENDERING IMPLEMENTATION
        // =============================================================================

        void RenderChart(IRenderContext* ctx) override;

        bool HandleChartMouseMove(const Point2Di& mousePos) override;

    private:
        // =============================================================================
        // FINANCIAL RENDERING HELPERS
        // =============================================================================
        void UpdateRenderingCache() override;
        void CalculateRenderingAreas();

        void RenderGrid(IRenderContext* ctx) override;
        void RenderCommonBackground(IRenderContext* ctx) override;

        // Enhanced separate chart rendering methods
        void RenderPriceChart(IRenderContext* ctx);
        void RenderVolumeChart(IRenderContext* ctx);
        void DrawPriceChartBackground(IRenderContext* ctx);
        void DrawPriceChartBorder(IRenderContext* ctx);
        void DrawPriceChartGrid(IRenderContext* ctx);
        void DrawPriceChartAxes(IRenderContext* ctx);
        void DrawVolumeChartBackground(IRenderContext* ctx);
        void DrawVolumeChartBorder(IRenderContext* ctx);
        void DrawVolumeGrid(IRenderContext* ctx);
        void DrawVolumeChartAxes(IRenderContext* ctx);
        void DrawVolumeAxisLabels(IRenderContext* ctx);
        void RenderVolumeMovingAverage(IRenderContext* ctx);

        void RenderFinancialData(IRenderContext* ctx);
        void RenderVolumeData(IRenderContext* ctx);
        void RenderMovingAverageData(IRenderContext* ctx);

        void DrawCandlestickCandle(IRenderContext* ctx, const FinancialChartDataPoint& point,
                                   float x, float candleWidth, double minPrice, double priceRange);

        void DrawOHLCBar(IRenderContext* ctx, const FinancialChartDataPoint& point,
                         float x, float candleWidth, double minPrice, double priceRange);

        void DrawHeikinAshiCandle(IRenderContext* ctx, const FinancialChartDataPoint& point,
                                  float x, float candleWidth, double minPrice, double priceRange) {
            // Simplified Heikin-Ashi - render as regular candlestick for now
            // Full implementation would require previous candle calculations
            DrawCandlestickCandle(ctx, point, x, candleWidth * 0.9f, minPrice, priceRange);
        }

        FinancialChartDataPoint GetFinancialPointAtPosition(const Point2Di& mousePos) const;
        std::string GenerateFinancialTooltip(const FinancialChartDataPoint& point);
        std::string FormatVolumeValue(double volume);
    };

// =============================================================================
// FINANCIAL CHART FACTORY FUNCTION
// =============================================================================

    inline std::shared_ptr<UltraCanvasFinancialChartElement> CreateFinancialChartElement(
            const std::string& id, long uid, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasFinancialChartElement>(id, uid, x, y, width, height);
    }

    // Helper factory for financial chart with data
    inline std::shared_ptr<UltraCanvasFinancialChartElement> CreateFinancialChartWithData(
            const std::string& id, long uid, int x, int y, int width, int height,
            std::shared_ptr<FinancialChartDataVector> data, const std::string& title = "") {

        auto chart = CreateFinancialChartElement(id, uid, x, y, width, height);
        chart->SetFinancialDataSource(data);
        if (!title.empty()) {
            chart->SetChartTitle(title);
        }
        return chart;
    }

} // namespace UltraCanvas
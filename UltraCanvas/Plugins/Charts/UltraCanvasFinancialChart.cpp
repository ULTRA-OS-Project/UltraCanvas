// Plugins/Charts/UltraCanvasFinancialChart.cpp
// Financial chart element with OHLC candlestick and enhanced separate volume chart rendering
// Version: 1.3.0
// Last Modified: 2025-09-15
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasFinancialChart.h"
#include "UltraCanvasTooltipManager.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <limits>
#include <cmath>
#include <iomanip>

namespace UltraCanvas {

// =============================================================================
// FINANCIAL DATA VECTOR IMPLEMENTATION
// =============================================================================

    void FinancialChartDataVector::LoadFromCSV(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) return;

        std::string line;
        bool firstLine = true;

        while (std::getline(file, line)) {
            if (firstLine) {
                firstLine = false;
                continue; // Skip header
            }

            auto point = ParseFinancialCSVLine(line);
            if (point.close > 0) { // Basic validation
                financialData.push_back(point);
            }
        }
    }

    void FinancialChartDataVector::LoadFromArray(const std::vector<ChartDataPoint>& data) {
        financialData.clear();
        for (const auto& point : data) {
            financialData.emplace_back(point.x, point.y, point.y, point.y, point.y, point.value);
        }
    }

    void FinancialChartDataVector::LoadFinancialData(const std::vector<FinancialChartDataPoint>& data) {
        financialData = data;
    }

    void FinancialChartDataVector::AddFinancialPoint(const FinancialChartDataPoint& point) {
        financialData.push_back(point);
    }

    FinancialChartDataPoint FinancialChartDataVector::ParseFinancialCSVLine(const std::string& line) {
        std::stringstream ss(line);
        std::string cell;
        std::vector<std::string> values;

        while (std::getline(ss, cell, ',')) {
            // Trim whitespace
            cell.erase(0, cell.find_first_not_of(" \t"));
            cell.erase(cell.find_last_not_of(" \t") + 1);
            values.push_back(cell);
        }

        // Expected format: Date,Open,High,Low,Close,Volume
        if (values.size() >= 5) {
            std::string date = values[0];
            double open = std::stod(values[1]);
            double high = std::stod(values[2]);
            double low = std::stod(values[3]);
            double close = std::stod(values[4]);
            double volume = values.size() > 5 ? std::stod(values[5]) : 0.0;

            // Use index as time for now (could parse date later)
            static double timeIndex = 0.0;
            timeIndex += 1.0;

            return FinancialChartDataPoint(timeIndex, open, high, low, close, volume, date);
        }

        return FinancialChartDataPoint(0, 0, 0, 0, 0, 0, "");
    }

// =============================================================================
// FINANCIAL CHART ELEMENT IMPLEMENTATION
// =============================================================================

    UltraCanvasFinancialChartElement::UltraCanvasFinancialChartElement(const std::string &id, long uid, int x, int y,
                                                                       int width, int height)
            : UltraCanvasChartElementBase(id, uid, x, y, width, height) {
        enableZoom = true;
        enablePan = true;
        enableTooltips = true;
    }

    void UltraCanvasFinancialChartElement::RenderCommonBackground(IRenderContext *ctx) {
        if (!ctx) return;

        // Draw overall background using existing functions
        ctx->SetFillPaint(backgroundColor);
        ctx->FillRectangle(GetX(), GetY(), GetWidth(), GetHeight());

        // Draw title using existing functions
        if (!chartTitle.empty()) {
            ctx->SetTextPaint(Color(0, 0, 0, 255));
            ctx->SetFontSize(16.0f);

            // Calculate center position (simplified)
            int titleX = GetX() + GetWidth() / 2 - static_cast<int>(chartTitle.length()) * 5;
            ctx->DrawText(chartTitle, titleX, GetY());
        }
    }

    void UltraCanvasFinancialChartElement::RenderChart(IRenderContext *ctx) {
        if (!ctx || !dataSource || dataSource->GetPointCount() == 0) {
            return;
        }

        // Update rendering areas if cache invalid
        if (!cacheValid) {
            CalculateRenderingAreas();
            cacheValid = true;
        }

        // Render price chart as first independent chart
        RenderPriceChart(ctx);

        // Render volume chart as second independent chart (if enabled)
        if (showVolumePanel) {
            RenderVolumeChart(ctx);
        }
    }

    void UltraCanvasFinancialChartElement::RenderPriceChart(IRenderContext *ctx) {
        // Draw price chart background
        DrawPriceChartBackground(ctx);

        // Draw price chart border
        DrawPriceChartBorder(ctx);

        // Draw price chart grid
        if (showGrid) {
            DrawPriceChartGrid(ctx);
        }

        // Draw price chart axes with correct price scaling
        DrawPriceChartAxes(ctx);

        // Render financial data (candlesticks/bars)
        RenderFinancialData(ctx);

        // Render moving average if enabled
        if (showMovingAverage) {
            RenderMovingAverageData(ctx);
        }
    }

    void UltraCanvasFinancialChartElement::RenderVolumeChart(IRenderContext *ctx) {
        // Draw volume chart background
        DrawVolumeChartBackground(ctx);

        // Draw volume chart border
        if (showVolumeBorder) {
            DrawVolumeChartBorder(ctx);
        }

        // Draw volume grid
        if (showGrid) {
            DrawVolumeGrid(ctx);
        }

        // Draw volume chart axes with correct volume scaling
        DrawVolumeChartAxes(ctx);

        // Render volume data
        RenderVolumeData(ctx);

        // Render volume moving average if enabled
        if (showVolumeMovingAverage) {
            RenderVolumeMovingAverage(ctx);
        }
    }

    void UltraCanvasFinancialChartElement::DrawPriceChartBackground(IRenderContext *ctx) {
        ctx->SetFillPaint(plotAreaColor);
        ctx->FillRectangle(priceRenderArea.x, priceRenderArea.y,
                           priceRenderArea.width, priceRenderArea.height);
    }

    void UltraCanvasFinancialChartElement::DrawPriceChartBorder(IRenderContext *ctx) {
        ctx->SetStrokePaint(Color(150, 150, 150, 255));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(priceRenderArea.x, priceRenderArea.y,
                           priceRenderArea.width, priceRenderArea.height);
    }

    void UltraCanvasFinancialChartElement::DrawPriceChartGrid(IRenderContext *ctx) {
        ctx->SetStrokePaint(gridColor);
        ctx->SetStrokeWidth(1.0f);

        // Vertical grid lines
        int numVerticalLines = 8;
        for (int i = 1; i < numVerticalLines; ++i) {
            float x = priceRenderArea.x + (i * priceRenderArea.width / numVerticalLines);
            ctx->DrawLine(x, priceRenderArea.y, x, priceRenderArea.GetBottom());
        }

        // Horizontal grid lines for price area
        int numHorizontalLines = 6;
        for (int i = 1; i < numHorizontalLines; ++i) {
            float y = priceRenderArea.y + (i * priceRenderArea.height / numHorizontalLines);
            ctx->DrawLine(priceRenderArea.x, y, priceRenderArea.GetRight(), y);
        }
    }

    void UltraCanvasFinancialChartElement::DrawPriceChartAxes(IRenderContext *ctx) {
        auto ds = GetFinancialDataSource();
        if (!ds) return;

        size_t pointCount = ds->GetPointCount();
        if (pointCount == 0) return;

        // Calculate price bounds for Y-axis labels
        double minPrice = std::numeric_limits<double>::max();
        double maxPrice = std::numeric_limits<double>::lowest();

        for (size_t i = 0; i < pointCount; ++i) {
            auto financialPoint = ds->GetFinancialPoint(i);
            minPrice = std::min(minPrice, financialPoint.low);
            maxPrice = std::max(maxPrice, financialPoint.high);
        }

        // Draw price chart axes
        ctx->SetStrokePaint(Color(100, 100, 100, 255));
        ctx->SetStrokeWidth(2.0f);

        // Draw X-axis (bottom of price chart)
        ctx->DrawLine(priceRenderArea.x, priceRenderArea.GetBottom(),
                      priceRenderArea.GetRight(), priceRenderArea.GetBottom());

        // Draw Y-axis (left of price chart)
        ctx->DrawLine(priceRenderArea.x, priceRenderArea.y,
                      priceRenderArea.x, priceRenderArea.GetBottom());

        // Draw price Y-axis labels
        ctx->SetStrokeWidth(1.0f);
        ctx->SetTextPaint(Color(80, 80, 80, 255));
        ctx->SetStrokePaint(Color(80, 80, 80, 255));
        ctx->SetFontSize(10.0f);

        int numPriceLabels = 6;
        for (int i = 0; i <= numPriceLabels; ++i) {
            double priceValue = minPrice + (maxPrice - minPrice) * i / numPriceLabels;
            float y = priceRenderArea.GetBottom() - (i * priceRenderArea.height / numPriceLabels);

            // Draw tick mark
            ctx->DrawLine(priceRenderArea.x - 5, y, priceRenderArea.x, y);

            // Format and draw price label
            std::ostringstream priceLabel;
            priceLabel << std::fixed << std::setprecision(2) << "$" << priceValue;

            int textWidth, textHeight;
            ctx->MeasureText(priceLabel.str(), textWidth, textHeight);
            ctx->DrawText(priceLabel.str(), priceRenderArea.x - textWidth - 8, y - textHeight/2);
        }

        // Draw shared X-axis labels (dates) for price chart
        int numXLabels = std::min(static_cast<int>(pointCount), 6);
        for (int i = 0; i <= numXLabels; ++i) {
            size_t dataIndex = (pointCount - 1) * i / numXLabels;
            if (dataIndex >= pointCount) dataIndex = pointCount - 1;

            FinancialChartDataPoint financialPoint = ds->GetFinancialPoint(dataIndex);
            float x = priceRenderArea.x + (dataIndex + 0.5f) * (priceRenderArea.width / pointCount);

            // Draw tick mark
            ctx->DrawLine(x, priceRenderArea.GetBottom(), x, priceRenderArea.GetBottom() + 8);

            // Draw date label
            std::string dateLabel = financialPoint.date;
            if (dateLabel.length() > 10) {
                dateLabel = dateLabel.substr(0, 10);
            }

            int textWidth, textHeight;
            ctx->MeasureText(dateLabel, textWidth, textHeight);
            ctx->DrawText(dateLabel, x - textWidth/2, priceRenderArea.GetBottom() + 12);
        }
    }

    void UltraCanvasFinancialChartElement::DrawVolumeChartBackground(IRenderContext *ctx) {
        ctx->SetFillPaint(volumeChartBackgroundColor);
        ctx->FillRectangle(volumeRenderArea.x, volumeRenderArea.y,
                           volumeRenderArea.width, volumeRenderArea.height);
    }

    void UltraCanvasFinancialChartElement::DrawVolumeChartBorder(IRenderContext *ctx) {
        ctx->SetStrokePaint(volumeChartBorderColor);
        ctx->SetStrokeWidth(1.5f);
        ctx->DrawRectangle(volumeRenderArea.x, volumeRenderArea.y,
                           volumeRenderArea.width, volumeRenderArea.height);
    }

    void UltraCanvasFinancialChartElement::DrawVolumeGrid(IRenderContext *ctx) {
        ctx->SetStrokePaint(volumeGridColor);
        ctx->SetStrokeWidth(1.0f);

        // Vertical grid lines (align with price chart)
        int numVerticalLines = 8;
        for (int i = 1; i < numVerticalLines; ++i) {
            float x = volumeRenderArea.x + (i * volumeRenderArea.width / numVerticalLines);
            ctx->DrawLine(x, volumeRenderArea.y, x, volumeRenderArea.GetBottom());
        }

        // Horizontal grid lines for volume area
        int numVolumeLines = 4;
        for (int i = 1; i < numVolumeLines; ++i) {
            float y = volumeRenderArea.y + (i * volumeRenderArea.height / numVolumeLines);
            ctx->DrawLine(volumeRenderArea.x, y, volumeRenderArea.GetRight(), y);
        }
    }

    void UltraCanvasFinancialChartElement::DrawVolumeChartAxes(IRenderContext *ctx) {
        auto ds = GetFinancialDataSource();
        if (!ds) return;

        size_t pointCount = ds->GetPointCount();
        if (pointCount == 0) return;

        // Calculate volume bounds for Y-axis labels
        double maxVolume = 0;
        for (size_t i = 0; i < pointCount; ++i) {
            FinancialChartDataPoint financialPoint = ds->GetFinancialPoint(i);
            maxVolume = std::max(maxVolume, financialPoint.volume);
        }

        // Draw volume chart axes
        ctx->SetStrokePaint(Color(100, 100, 100, 255));
        ctx->SetStrokeWidth(2.0f);

        // Draw X-axis (bottom of volume chart)
        ctx->DrawLine(volumeRenderArea.x, volumeRenderArea.GetBottom(),
                      volumeRenderArea.GetRight(), volumeRenderArea.GetBottom());

        // Draw Y-axis (left of volume chart)
        ctx->DrawLine(volumeRenderArea.x, volumeRenderArea.y,
                      volumeRenderArea.x, volumeRenderArea.GetBottom());

        // Draw volume Y-axis labels
        ctx->SetStrokeWidth(1.0f);
        ctx->SetStrokePaint(volumeAxisLabelColor);
        ctx->SetFontSize(10.0f);

        int numVolumeLabels = 4;
        for (int i = 0; i <= numVolumeLabels; ++i) {
            double volumeValue = (maxVolume * i) / numVolumeLabels;
            float y = volumeRenderArea.GetBottom() - (i * volumeRenderArea.height / numVolumeLabels);

            // Draw tick mark
            ctx->DrawLine(volumeRenderArea.x - 5, y, volumeRenderArea.x, y);

            // Format and draw volume label
            std::string label = FormatVolumeValue(volumeValue);

            int textWidth, textHeight;
            ctx->MeasureText(label, textWidth, textHeight);
            ctx->DrawText(label, volumeRenderArea.x - textWidth - 8, y - textHeight/2);
        }

        // Draw shared X-axis labels (dates) for volume chart - same as price chart
        int numXLabels = std::min(static_cast<int>(pointCount), 6);
        for (int i = 0; i <= numXLabels; ++i) {
            size_t dataIndex = (pointCount - 1) * i / numXLabels;
            if (dataIndex >= pointCount) dataIndex = pointCount - 1;

            FinancialChartDataPoint financialPoint = ds->GetFinancialPoint(dataIndex);
            float x = volumeRenderArea.x + (dataIndex + 0.5f) * (volumeRenderArea.width / pointCount);

            // Draw tick mark
            ctx->DrawLine(x, volumeRenderArea.GetBottom(), x, volumeRenderArea.GetBottom() + 8);

            // Draw date label
            std::string dateLabel = financialPoint.date;
            if (dateLabel.length() > 10) {
                dateLabel = dateLabel.substr(0, 10);
            }

            int textWidth, textHeight;
            ctx->MeasureText(dateLabel, textWidth, textHeight);
            ctx->DrawText(dateLabel, x - textWidth/2, volumeRenderArea.GetBottom() + 12);
        }
    }

    void UltraCanvasFinancialChartElement::DrawVolumeAxisLabels(IRenderContext *ctx) {
        auto ds = GetFinancialDataSource();
        if (!ds) return;

        size_t pointCount = ds->GetPointCount();
        if (pointCount == 0) return;

        // Calculate volume bounds
        double maxVolume = 0;
        for (size_t i = 0; i < pointCount; ++i) {
            FinancialChartDataPoint financialPoint = ds->GetFinancialPoint(i);
            maxVolume = std::max(maxVolume, financialPoint.volume);
        }

        if (maxVolume == 0) return;

        ctx->SetTextPaint(volumeAxisLabelColor);
        ctx->SetFontSize(10.0f);

        // Draw volume scale labels
        int numLabels = 4;
        for (int i = 0; i <= numLabels; ++i) {
            double volumeValue = (maxVolume * i) / numLabels;
            float y = volumeRenderArea.GetBottom() - (i * volumeRenderArea.height / numLabels);

            std::string label = FormatVolumeValue(volumeValue);

            // Measure text to position it properly
            int textWidth, textHeight;
            ctx->MeasureText(label, textWidth, textHeight);

            // Draw label to the left of the volume area
            ctx->DrawText(label, volumeRenderArea.x - textWidth - 5, y - textHeight/2);
        }

        // Draw "Volume" label vertically on the left
        ctx->SetFontSize(12.0f);
        int labelWidth, labelHeight;
        ctx->MeasureText("Volume", labelWidth, labelHeight);
        ctx->DrawText("Volume", volumeRenderArea.x - 40, volumeRenderArea.GetCenter().y - labelHeight/2);
    }

    std::string UltraCanvasFinancialChartElement::FormatVolumeValue(double volume) {
        if (volume >= 1000000) {
            return std::to_string(static_cast<int>(volume / 1000000)) + "M";
        } else if (volume >= 1000) {
            return std::to_string(static_cast<int>(volume / 1000)) + "K";
        } else {
            return std::to_string(static_cast<int>(volume));
        }
    }

    bool UltraCanvasFinancialChartElement::HandleChartMouseMove(const Point2Di &mousePos) {
        if (!enableTooltips) return false;

        // Check if mouse is in price or volume area
        bool inPriceArea = priceRenderArea.Contains(mousePos.x, mousePos.y);
        bool inVolumeArea = showVolumePanel && volumeRenderArea.Contains(mousePos.x, mousePos.y);

        if (inPriceArea || inVolumeArea) {
            auto financialPoint = GetFinancialPointAtPosition(mousePos);
            std::string tooltipContent = GenerateFinancialTooltip(financialPoint);

            UltraCanvasTooltipManager::UpdateAndShowTooltip(window, tooltipContent, mousePos);
            return true;
        } else {
            UltraCanvasTooltipManager::HideTooltip();
            return false;
        }
    }

    void UltraCanvasFinancialChartElement::UpdateRenderingCache() {
        UltraCanvasChartElementBase::UpdateRenderingCache();
        CalculateRenderingAreas();
    }

    void UltraCanvasFinancialChartElement::CalculateRenderingAreas() {
        int padding = 60;
        int titleHeight = chartTitle.empty() ? 0 : 30;
        int xAxisHeight = 40;  // Space for X-axis labels and ticks
        int volumeAxisWidth = 0;  // Space for volume Y-axis labels

        if (showVolumePanel) {
            // Calculate total available space for both charts
            int totalAvailableHeight = GetHeight() - padding - titleHeight; // Top and bottom padding

            // Reserve space for main chart X-axis
            int availableChartHeight = totalAvailableHeight - xAxisHeight;

            // Calculate heights for price and volume sections
            int priceChartHeight = static_cast<int>(availableChartHeight * (1.0f - volumePanelHeightRatio));
            int volumeChartHeight = availableChartHeight - priceChartHeight - volumeSeparationHeight;

            // Price chart area (top section)
            priceRenderArea = ChartPlotArea(
                    GetX() + padding + volumeAxisWidth,  // Leave space for volume Y-axis
                    GetY() + titleHeight,
                    GetWidth() - padding * 2 - volumeAxisWidth,
                    priceChartHeight
            );

            // Calculate Y position for volume chart (below main chart X-axis)
            int volumeStartY = priceRenderArea.GetBottom() + xAxisHeight + volumeSeparationHeight;

            // Volume chart area (bottom section, below X-axis)
            volumeRenderArea = ChartPlotArea(
                    GetX() + padding + volumeAxisWidth,  // Align with price chart
                    volumeStartY,
                    GetWidth() - padding * 2 - volumeAxisWidth,
                    volumeChartHeight
            );

            // Volume axis area (for labels)
            volumeAxisArea = ChartPlotArea(
                    GetX() + padding,
                    volumeStartY,
                    volumeAxisWidth,
                    volumeChartHeight
            );
        } else {
            // Use full area for price chart only, but leave space for X-axis
            int priceChartHeight = GetHeight() - padding - titleHeight - xAxisHeight;

            priceRenderArea = ChartPlotArea(
                    GetX() + padding,
                    GetY() + titleHeight,
                    GetWidth() - padding * 2,
                    priceChartHeight
            );
        }
    }

    void UltraCanvasFinancialChartElement::RenderGrid(IRenderContext *ctx) {
        ctx->SetStrokePaint(gridColor);
        ctx->SetStrokeWidth(1.0f);

        // Vertical grid lines (only for price chart)
        int numVerticalLines = 10;
        for (int i = 1; i < numVerticalLines; ++i) {
            float x = priceRenderArea.x + (i * priceRenderArea.width / numVerticalLines);
            ctx->DrawLine(x, priceRenderArea.y, x, priceRenderArea.GetBottom());
        }

        // Horizontal grid lines for price area
        int numHorizontalLines = 8;
        for (int i = 1; i < numHorizontalLines; ++i) {
            float y = priceRenderArea.y + (i * priceRenderArea.height / numHorizontalLines);
            ctx->DrawLine(priceRenderArea.x, y, priceRenderArea.GetRight(), y);
        }
    }

    void UltraCanvasFinancialChartElement::RenderFinancialData(IRenderContext *ctx) {
        size_t pointCount = dataSource->GetPointCount();
        auto ds = GetFinancialDataSource();
        if (!ds || pointCount == 0) return;

        float candleSpacing = priceRenderArea.width / static_cast<float>(pointCount);
        float actualCandleWidth = candleSpacing * candleWidthRatio;

        // Calculate price bounds
        double minPrice = std::numeric_limits<double>::max();
        double maxPrice = std::numeric_limits<double>::lowest();

        for (size_t i = 0; i < pointCount; ++i) {
            auto financialPoint = ds->GetFinancialPoint(i);

            minPrice = std::min(minPrice, financialPoint.low);
            maxPrice = std::max(maxPrice, financialPoint.high);
        }

        double priceRange = maxPrice - minPrice;
        if (priceRange == 0) priceRange = 1.0; // Avoid division by zero

        // Render each candle
        for (size_t i = 0; i < pointCount; ++i) {
            auto financialPoint = ds->GetFinancialPoint(i);

            float x = priceRenderArea.x + (i + 0.5f) * candleSpacing;

            switch (candleStyle) {
                case CandleDisplayStyle::Candlestick:
                    DrawCandlestickCandle(ctx, financialPoint, x, actualCandleWidth,
                                          minPrice, priceRange);
                    break;
                case CandleDisplayStyle::OHLCBars:
                    DrawOHLCBar(ctx, financialPoint, x, actualCandleWidth,
                                minPrice, priceRange);
                    break;
                case CandleDisplayStyle::HeikinAshi:
                    DrawHeikinAshiCandle(ctx, financialPoint, x, actualCandleWidth,
                                         minPrice, priceRange);
                    break;
            }
        }
    }

    void UltraCanvasFinancialChartElement::RenderVolumeData(IRenderContext *ctx) {
        auto ds = GetFinancialDataSource();
        if (!ds || !showVolumePanel) return;

        size_t pointCount = ds->GetPointCount();
        if (pointCount == 0) return;

        // Calculate volume bounds
        double maxVolume = 0;
        for (size_t i = 0; i < pointCount; ++i) {
            FinancialChartDataPoint financialPoint = ds->GetFinancialPoint(i);
            maxVolume = std::max(maxVolume, financialPoint.volume);
        }

        if (maxVolume == 0) return;

        float barSpacing = volumeRenderArea.width / static_cast<float>(pointCount);
        float actualBarWidth = barSpacing * candleWidthRatio;

        ctx->SetFillPaint(volumeBarColor);

        for (size_t i = 0; i < pointCount; ++i) {
            FinancialChartDataPoint financialPoint = ds->GetFinancialPoint(i);

            if (financialPoint.volume > 0) {
                float x = volumeRenderArea.x + (i + 0.5f) * barSpacing;
                float barHeight = (financialPoint.volume / maxVolume) * volumeRenderArea.height;
                float y = volumeRenderArea.GetBottom() - barHeight;

                ctx->FillRectangle(x - actualBarWidth/2, y, actualBarWidth, barHeight);
            }
        }
    }

    void UltraCanvasFinancialChartElement::RenderVolumeMovingAverage(IRenderContext *ctx) {
        auto ds = GetFinancialDataSource();
        if (!ds || !showVolumeMovingAverage || volumeMovingAveragePeriod <= 0) return;

        size_t pointCount = ds->GetPointCount();
        if (pointCount < static_cast<size_t>(volumeMovingAveragePeriod)) return;

        // Calculate volume bounds
        double maxVolume = 0;
        for (size_t i = 0; i < pointCount; ++i) {
            FinancialChartDataPoint financialPoint = ds->GetFinancialPoint(i);
            maxVolume = std::max(maxVolume, financialPoint.volume);
        }

        if (maxVolume == 0) return;

        ctx->SetStrokePaint(volumeMovingAverageColor);
        ctx->SetStrokeWidth(2.0f);

        float barSpacing = volumeRenderArea.width / static_cast<float>(pointCount);

        // Calculate and draw volume moving average
        for (size_t i = volumeMovingAveragePeriod - 1; i < pointCount - 1; ++i) {
            // Calculate moving average for current point
            double volumeSum = 0;
            for (int j = 0; j < volumeMovingAveragePeriod; ++j) {
                FinancialChartDataPoint point = ds->GetFinancialPoint(i - j);
                volumeSum += point.volume;
            }
            double volumeAvg1 = volumeSum / volumeMovingAveragePeriod;

            // Calculate moving average for next point
            volumeSum = 0;
            for (int j = 0; j < volumeMovingAveragePeriod; ++j) {
                FinancialChartDataPoint point = ds->GetFinancialPoint(i + 1 - j);
                volumeSum += point.volume;
            }
            double volumeAvg2 = volumeSum / volumeMovingAveragePeriod;

            // Draw line segment
            float x1 = volumeRenderArea.x + (i + 0.5f) * barSpacing;
            float y1 = volumeRenderArea.GetBottom() - (volumeAvg1 / maxVolume) * volumeRenderArea.height;

            float x2 = volumeRenderArea.x + (i + 1 + 0.5f) * barSpacing;
            float y2 = volumeRenderArea.GetBottom() - (volumeAvg2 / maxVolume) * volumeRenderArea.height;

            ctx->DrawLine(x1, y1, x2, y2);
        }
    }

    void UltraCanvasFinancialChartElement::RenderMovingAverageData(IRenderContext *ctx) {
        auto ds = GetFinancialDataSource();
        if (!ds || !showMovingAverage || movingAveragePeriod <= 0) return;

        size_t pointCount = ds->GetPointCount();
        if (pointCount < static_cast<size_t>(movingAveragePeriod)) return;

        // Calculate price bounds for mapping
        double minPrice = std::numeric_limits<double>::max();
        double maxPrice = std::numeric_limits<double>::lowest();

        for (size_t i = 0; i < pointCount; ++i) {
            FinancialChartDataPoint financialPoint = ds->GetFinancialPoint(i);
            minPrice = std::min(minPrice, financialPoint.low);
            maxPrice = std::max(maxPrice, financialPoint.high);
        }

        double priceRange = maxPrice - minPrice;
        if (priceRange == 0) return;

        ctx->SetStrokePaint(movingAverageLineColor);
        ctx->SetStrokeWidth(2.0f);

        float candleSpacing = priceRenderArea.width / static_cast<float>(pointCount);

        // Calculate and draw moving average
        for (size_t i = movingAveragePeriod - 1; i < pointCount - 1; ++i) {
            // Calculate moving average for current point
            double priceSum = 0;
            for (int j = 0; j < movingAveragePeriod; ++j) {
                FinancialChartDataPoint point = ds->GetFinancialPoint(i - j);
                priceSum += point.close;
            }
            double priceAvg1 = priceSum / movingAveragePeriod;

            // Calculate moving average for next point
            priceSum = 0;
            for (int j = 0; j < movingAveragePeriod; ++j) {
                FinancialChartDataPoint point = ds->GetFinancialPoint(i + 1 - j);
                priceSum += point.close;
            }
            double priceAvg2 = priceSum / movingAveragePeriod;

            // Draw line segment
            float x1 = priceRenderArea.x + (i + 0.5f) * candleSpacing;
            float y1 = priceRenderArea.GetBottom() - ((priceAvg1 - minPrice) / priceRange) * priceRenderArea.height;

            float x2 = priceRenderArea.x + (i + 1 + 0.5f) * candleSpacing;
            float y2 = priceRenderArea.GetBottom() - ((priceAvg2 - minPrice) / priceRange) * priceRenderArea.height;

            ctx->DrawLine(x1, y1, x2, y2);
        }
    }

    void UltraCanvasFinancialChartElement::DrawCandlestickCandle(IRenderContext *ctx,
                                                                 const FinancialChartDataPoint &point,
                                                                 float x, float candleWidth,
                                                                 double minPrice, double priceRange) {
        // Calculate positions
        float highY = priceRenderArea.GetBottom() - ((point.high - minPrice) / priceRange) * priceRenderArea.height;
        float lowY = priceRenderArea.GetBottom() - ((point.low - minPrice) / priceRange) * priceRenderArea.height;
        float openY = priceRenderArea.GetBottom() - ((point.open - minPrice) / priceRange) * priceRenderArea.height;
        float closeY = priceRenderArea.GetBottom() - ((point.close - minPrice) / priceRange) * priceRenderArea.height;

        // Draw wick (high-low line)
        ctx->SetStrokePaint(wickLineColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(x, highY, x, lowY);

        // Draw candle body
        bool isBullish = point.close > point.open;
        Color candleColor = isBullish ? bullishCandleColor : bearishCandleColor;

        ctx->SetStrokePaint(candleColor);
        ctx->SetFillPaint(candleColor);
        ctx->SetStrokeWidth(1.0f);

        float bodyTop = std::min(openY, closeY);
        float bodyHeight = std::abs(closeY - openY);

        if (bodyHeight < 1.0f) bodyHeight = 1.0f; // Minimum visible height

        if (isBullish) {
            ctx->FillRectangle(x - candleWidth/2, bodyTop, candleWidth, bodyHeight);
        } else {
            ctx->DrawRectangle(x - candleWidth/2, bodyTop, candleWidth, bodyHeight);
        }
    }

    void UltraCanvasFinancialChartElement::DrawOHLCBar(IRenderContext *ctx,
                                                       const FinancialChartDataPoint &point,
                                                       float x, float candleWidth,
                                                       double minPrice, double priceRange) {
        // Calculate positions
        float highY = priceRenderArea.GetBottom() - ((point.high - minPrice) / priceRange) * priceRenderArea.height;
        float lowY = priceRenderArea.GetBottom() - ((point.low - minPrice) / priceRange) * priceRenderArea.height;
        float openY = priceRenderArea.GetBottom() - ((point.open - minPrice) / priceRange) * priceRenderArea.height;
        float closeY = priceRenderArea.GetBottom() - ((point.close - minPrice) / priceRange) * priceRenderArea.height;

        bool isBullish = point.close > point.open;
        Color barColor = isBullish ? bullishCandleColor : bearishCandleColor;

        ctx->SetStrokePaint(barColor);
        ctx->SetStrokeWidth(2.0f);

        // Draw main vertical line (high-low)
        ctx->DrawLine(x, highY, x, lowY);

        // Draw open tick (left side)
        ctx->DrawLine(x - candleWidth/4, openY, x, openY);

        // Draw close tick (right side)
        ctx->DrawLine(x, closeY, x + candleWidth/4, closeY);
    }

    FinancialChartDataPoint UltraCanvasFinancialChartElement::GetFinancialPointAtPosition(const Point2Di &mousePos) const {
        auto ds = GetFinancialDataSource();
        if (!ds || ds->GetPointCount() == 0) {
            return FinancialChartDataPoint(0, 0, 0, 0, 0, 0, "");
        }

        // Determine which chart area the mouse is in
        bool inPriceArea = priceRenderArea.Contains(mousePos.x, mousePos.y);
        bool inVolumeArea = showVolumePanel && volumeRenderArea.Contains(mousePos.x, mousePos.y);

        if (!inPriceArea && !inVolumeArea) {
            return FinancialChartDataPoint(0, 0, 0, 0, 0, 0, "");
        }

        // Calculate which data point based on X position
        // Use price area for X calculation since both charts are aligned
        float relativeX = mousePos.x - priceRenderArea.x;
        float spacing = priceRenderArea.width / static_cast<float>(ds->GetPointCount());
        size_t index = static_cast<size_t>(relativeX / spacing);

        if (index >= ds->GetPointCount()) {
            index = ds->GetPointCount() - 1;
        }

        return ds->GetFinancialPoint(index);
    }

    std::string UltraCanvasFinancialChartElement::GenerateFinancialTooltip(const FinancialChartDataPoint &point) {
        std::ostringstream tooltip;
        tooltip << std::fixed << std::setprecision(2);

        tooltip << "Date: " << point.date << "\n";
        tooltip << "Open: $" << point.open << "\n";
        tooltip << "High: $" << point.high << "\n";
        tooltip << "Low: $" << point.low << "\n";
        tooltip << "Close: $" << point.close << "\n";
        tooltip << "Volume: " << FormatVolumeValue(point.volume);

        return tooltip.str();
    }

//    std::string UltraCanvasFinancialChartElement::FormatVolumeValue(double volume) {
//        if (volume >= 1000000) {
//            return std::to_string(static_cast<int>(volume / 1000000)) + "M";
//        } else if (volume >= 1000) {
//            return std::to_string(static_cast<int>(volume / 1000)) + "K";
//        } else {
//            return std::to_string(static_cast<int>(volume));
//        }
//    }

} // namespace UltraCanvas
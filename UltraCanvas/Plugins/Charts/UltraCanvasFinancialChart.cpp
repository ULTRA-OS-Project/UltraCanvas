// Plugins/Charts/UltraCanvasFinancialChart.cpp
// Financial chart element with OHLC candlestick and volume rendering
// Version: 1.0.0
// Last Modified: 2025-09-14
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasFinancialChart.h"
#include "UltraCanvasTooltipManager.h"
#include <sstream>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace UltraCanvas {

// =============================================================================
// FINANCIAL DATA VECTOR IMPLEMENTATION
// =============================================================================

    void FinancialChartDataVector::LoadFromCSV(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open financial CSV file: " + filePath);
        }

        financialData.clear();
        std::string line;
        bool skipHeader = false;

        // Check for header line
        if (std::getline(file, line)) {
            // Look for common financial CSV headers
            if (line.find("Date") != std::string::npos ||
                line.find("Open") != std::string::npos ||
                line.find("OHLC") != std::string::npos) {
                skipHeader = true;
            } else {
                // Process first line as data
                financialData.push_back(ParseFinancialCSVLine(line));
            }
        }

        while (std::getline(file, line)) {
            if (!line.empty()) {
                financialData.push_back(ParseFinancialCSVLine(line));
            }
        }
    }

    void FinancialChartDataVector::LoadFromArray(const std::vector<ChartDataPoint>& data) {
        financialData.clear();
        for (size_t i = 0; i < data.size(); ++i) {
            const auto& point = data[i];
            // Convert basic ChartDataPoint to FinancialChartDataPoint
            // Assume close price in y, volume in z
            financialData.emplace_back(point.x, point.y, point.y, point.y, point.y, point.z, point.label);
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

    void UltraCanvasFinancialChartElement::RenderChart(IRenderContext *ctx) {
        if (!ctx || !dataSource || dataSource->GetPointCount() == 0) {
            return;
        }

        // Update rendering areas if cache invalid
//        if (!cacheValid) {
//            CalculateRenderingAreas();
//            cacheValid = true;
//        }

        // Draw chart background
//        ctx->SetFillColor(plotAreaColor);
//        ctx->FillRectangle(priceRenderArea.x, priceRenderArea.y,
//                           priceRenderArea.width, priceRenderArea.height);

        if (showVolumePanel) {
            ctx->FillRectangle(volumeRenderArea.x, volumeRenderArea.y,
                               volumeRenderArea.width, volumeRenderArea.height);
        }

        // Draw grid if enabled
//        if (showGrid) {
//            DrawGrid(ctx);
//        }

        // Render financial data
        RenderFinancialData(ctx);

        // Render volume if enabled
        if (showVolumePanel) {
            RenderVolumeData(ctx);
        }

        // Render moving average if enabled
        if (showMovingAverage) {
            RenderMovingAverageData(ctx);
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
        int padding = 40;
        int titleHeight = chartTitle.empty() ? 0 : 30;

        if (showVolumePanel) {
            // Split into price and volume areas
            int totalHeight = GetHeight() - padding - titleHeight;
            int priceHeight = static_cast<int>(totalHeight * (1.0f - volumePanelHeightRatio));
            int volumeHeight = totalHeight - priceHeight - 10; // 10px gap

            priceRenderArea = ChartPlotArea(
                    GetX() + padding + 20,
                    GetY() + titleHeight,
                    GetWidth() - padding * 2 - 20,
                    priceHeight
            );

            volumeRenderArea = ChartPlotArea(
                    GetX() + padding + 20,
                    priceRenderArea.GetBottom(),
                    GetWidth() - padding * 2 - 20,
                    volumeHeight
            );
        } else {
            // Use full area for price chart
            priceRenderArea = ChartPlotArea(
                    GetX() + padding,
                    GetY() + titleHeight,
                    GetWidth() - padding * 2,
                    GetHeight() - padding * 2 - titleHeight
            );
        }
    }

    void UltraCanvasFinancialChartElement::DrawGrid(IRenderContext *ctx) {
        ctx->SetStrokeColor(gridColor);
        ctx->SetStrokeWidth(1.0f);

        // Vertical grid lines
        int numVerticalLines = 10;
        for (int i = 1; i < numVerticalLines; ++i) {
            float x = priceRenderArea.x + (i * priceRenderArea.width / numVerticalLines);
            ctx->DrawLine(x, priceRenderArea.y, x, priceRenderArea.GetBottom());

            if (showVolumePanel) {
                ctx->DrawLine(x, volumeRenderArea.y, x, volumeRenderArea.GetBottom());
            }
        }

        // Horizontal grid lines for price area
        int numHorizontalLines = 8;
        for (int i = 1; i < numHorizontalLines; ++i) {
            float y = priceRenderArea.y + (i * priceRenderArea.height / numHorizontalLines);
            ctx->DrawLine(priceRenderArea.x, y, priceRenderArea.GetRight(), y);
        }

        // Horizontal grid lines for volume area
        if (showVolumePanel) {
            int numVolumeLines = 4;
            for (int i = 1; i < numVolumeLines; ++i) {
                float y = volumeRenderArea.y + (i * volumeRenderArea.height / numVolumeLines);
                ctx->DrawLine(volumeRenderArea.x, y, volumeRenderArea.GetRight(), y);
            }
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

        ctx->SetFillColor(volumeBarColor);

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

        ctx->SetStrokeColor(movingAverageLineColor);
        ctx->SetStrokeWidth(2.0f);

        float candleSpacing = priceRenderArea.width / static_cast<float>(pointCount);
        Point2Df prevPoint;
        bool firstPoint = true;

        for (size_t i = movingAveragePeriod - 1; i < pointCount; ++i) {
            // Calculate moving average
            double sum = 0;
            for (int j = 0; j < movingAveragePeriod; ++j) {
                auto financialPoint = ds->GetFinancialPoint(i - j);
                sum += financialPoint.close;
            }
            double average = sum / movingAveragePeriod;

            // Map to screen coordinates
            float x = priceRenderArea.x + (i + 0.5f) * candleSpacing;
            float y = priceRenderArea.y + priceRenderArea.height -
                      ((average - minPrice) / priceRange) * priceRenderArea.height;

            Point2Df currentPoint(x, y);

            if (!firstPoint) {
                ctx->DrawLine(prevPoint.x, prevPoint.y, currentPoint.x, currentPoint.y);
            }

            prevPoint = currentPoint;
            firstPoint = false;
        }
    }

    void
    UltraCanvasFinancialChartElement::DrawCandlestickCandle(IRenderContext *ctx, const FinancialChartDataPoint &point,
                                                            float x, float candleWidth, double minPrice,
                                                            double priceRange) {
        // Map prices to screen coordinates
        auto mapPriceToY = [this, minPrice, priceRange](double price) {
            return priceRenderArea.y + priceRenderArea.height -
                   ((price - minPrice) / priceRange) * priceRenderArea.height;
        };

        float highY = mapPriceToY(point.high);
        float lowY = mapPriceToY(point.low);
        float openY = mapPriceToY(point.open);
        float closeY = mapPriceToY(point.close);

        // Draw wick (high-low line)
        ctx->SetStrokeColor(wickLineColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(x, highY, x, lowY);

        // Determine candle color and body coordinates
        bool isBullish = point.close >= point.open;
        Color candleColor = isBullish ? bullishCandleColor : bearishCandleColor;

        float bodyTop = isBullish ? closeY : openY;
        float bodyBottom = isBullish ? openY : closeY;
        float bodyHeight = bodyBottom - bodyTop;

        // Draw candle body
        ctx->SetFillColor(candleColor);
        ctx->FillRectangle(x - candleWidth/2, bodyTop, candleWidth, bodyHeight);

        // Draw candle border
        ctx->SetStrokeColor(Color(0, 0, 0, 255));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(x - candleWidth/2, bodyTop, candleWidth, bodyHeight);
    }

    void
    UltraCanvasFinancialChartElement::DrawOHLCBar(IRenderContext *ctx, const FinancialChartDataPoint &point, float x,
                                                  float candleWidth, double minPrice, double priceRange) {
        // Map prices to screen coordinates
        auto mapPriceToY = [this, minPrice, priceRange](double price) {
            return priceRenderArea.y + priceRenderArea.height -
                   ((price - minPrice) / priceRange) * priceRenderArea.height;
        };

        float highY = mapPriceToY(point.high);
        float lowY = mapPriceToY(point.low);
        float openY = mapPriceToY(point.open);
        float closeY = mapPriceToY(point.close);

        bool isBullish = point.close >= point.open;
        Color ohlcColor = isBullish ? bullishCandleColor : bearishCandleColor;

        ctx->SetStrokeColor(ohlcColor);
        ctx->SetStrokeWidth(2.0f);

        // Draw main vertical line (high to low)
        ctx->DrawLine(x, highY, x, lowY);

        // Draw open tick (left side)
        float tickLength = candleWidth * 0.3f;
        ctx->DrawLine(x - tickLength, openY, x, openY);

        // Draw close tick (right side)
        ctx->DrawLine(x, closeY, x + tickLength, closeY);
    }

    FinancialChartDataPoint
    UltraCanvasFinancialChartElement::GetFinancialPointAtPosition(const Point2Di &mousePos) const {
        auto ds = GetFinancialDataSource();
        if (!ds || ds->GetPointCount() == 0) {
            return FinancialChartDataPoint(0, 0, 0, 0, 0, 0, "");
        }

        // Calculate which candle the mouse is over
        float relativeX = (mousePos.x - priceRenderArea.x) / priceRenderArea.width;
        size_t index = static_cast<size_t>(relativeX * ds->GetPointCount());
        index = std::min(index, ds->GetPointCount() - 1);

        return ds->GetFinancialPoint(index);
    }

    std::string UltraCanvasFinancialChartElement::GenerateFinancialTooltip(const FinancialChartDataPoint &point) {
        std::string tooltip;

        if (!point.label.empty()) {
            tooltip += "Date: " + point.label + "\n";
        }

        tooltip += "Open: $" + FormatAxisLabel(point.open) + "\n";
        tooltip += "High: $" + FormatAxisLabel(point.high) + "\n";
        tooltip += "Low: $" + FormatAxisLabel(point.low) + "\n";
        tooltip += "Close: $" + FormatAxisLabel(point.close);

        if (showVolumePanel && point.volume > 0) {
            tooltip += "\nVolume: " + FormatAxisLabel(point.volume);
        }

        // Add price change
        if (point.open > 0) {
            double change = point.close - point.open;
            double changePercent = (change / point.open) * 100.0;
            tooltip += "\nChange: $" + FormatAxisLabel(change);
            tooltip += " (" + FormatAxisLabel(changePercent) + "%)";
        }

        return tooltip;
    }

} // namespace UltraCanvas
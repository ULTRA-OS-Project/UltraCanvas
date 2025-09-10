// UltraCanvasFinancialCharts.cpp
// Complete financial chart implementation with candlesticks and technical indicators
// Version: 1.0.1
// Last Modified: 2025-09-10
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasChartSpecialized.h"
#include "Plugins/Charts/UltraCanvasChartStructures.h"
#include "UltraCanvasRenderContext.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace UltraCanvas {

// =============================================================================
// FINANCIAL CHART RENDERER IMPLEMENTATION
// =============================================================================

    void FinancialChartRenderer::LoadCandlestickData(const std::vector<CandlestickData>& data) {
        candleData = data;

        // Sort by timestamp to ensure proper ordering
        std::sort(candleData.begin(), candleData.end(),
                  [](const CandlestickData& a, const CandlestickData& b) {
                      return a.timestamp < b.timestamp;
                  });

        // Clear existing indicators since data changed
        indicators.clear();
    }

    void FinancialChartRenderer::AddTechnicalIndicator(TechnicalIndicator::Type type,
                                                       const std::vector<double>& params,
                                                       uint32_t color,
                                                       const std::string& name) {
        if (candleData.empty()) return;

        TechnicalIndicator indicator(type, color, name);

        switch (type) {
            case TechnicalIndicator::Type::SMA:
                if (!params.empty()) {
                    CalculateSMA(indicator, static_cast<int>(params[0]));
                }
                break;

            case TechnicalIndicator::Type::EMA:
                if (!params.empty()) {
                    CalculateEMA(indicator, static_cast<int>(params[0]));
                }
                break;

            case TechnicalIndicator::Type::Bollinger:
                if (params.size() >= 2) {
                    CalculateBollingerBands(indicator, static_cast<int>(params[0]), params[1]);
                }
                break;

            case TechnicalIndicator::Type::RSI:
                if (!params.empty()) {
                    CalculateRSI(indicator, static_cast<int>(params[0]));
                }
                break;

            case TechnicalIndicator::Type::MACD:
                if (params.size() >= 3) {
                    CalculateMACD(indicator, static_cast<int>(params[0]),
                                  static_cast<int>(params[1]), static_cast<int>(params[2]));
                }
                break;

            case TechnicalIndicator::Type::Stochastic:
                if (params.size() >= 2) {
                    CalculateStochastic(indicator, static_cast<int>(params[0]), static_cast<int>(params[1]));
                }
                break;
        }

        indicators.push_back(indicator);
    }

    void FinancialChartRenderer::RenderFinancialChart(const ChartConfiguration& config,
                                                      const PlotArea& plotArea,
                                                      IRenderContext* ctx) {
        if (!ctx || candleData.empty()) return;

        // Calculate layout for multiple panels
        FinancialLayout layout = CalculateFinancialLayout(plotArea);

        // Calculate time and price bounds
        DataBounds timeBounds = CalculateTimeBounds();
        DataBounds priceBounds = CalculatePriceBounds();

        // Draw main price panel
        DrawPricePanel(layout.pricePanel, timeBounds, priceBounds, config, ctx);

        // Draw volume panel if enabled
        if (config.showVolume && layout.volumePanel.height > 0) {
            DrawVolumePanel(layout.volumePanel, timeBounds, ctx);
        }

        // Draw indicator panels
        DrawIndicatorPanels(layout.indicatorPanels, timeBounds, ctx);

        // Draw time axis (shared across all panels)
        DrawTimeAxis(layout, timeBounds, ctx);
    }

// =============================================================================
// LAYOUT CALCULATION
// =============================================================================

    FinancialChartRenderer::FinancialLayout FinancialChartRenderer::CalculateFinancialLayout(const PlotArea& plotArea) {
        FinancialLayout layout;

        // Reserve space for time axis
        int timeAxisHeight = 40;
        int panelSpacing = 5;
        int availableHeight = plotArea.height - timeAxisHeight;

        // Calculate panel count and heights
        int panelCount = 1; // Price panel
        if (HasVolumeData()) panelCount++;

        // Count indicator panels needed
        int indicatorPanelCount = 0;
        for (const auto& indicator : indicators) {
            if (RequiresSeparatePanel(indicator.type)) {
                indicatorPanelCount++;
            }
        }
        panelCount += indicatorPanelCount;

        // Allocate heights
        int priceHeight = static_cast<int>(availableHeight * 0.6f); // 60% for price
        int volumeHeight = HasVolumeData() ? static_cast<int>(availableHeight * 0.15f) : 0; // 15% for volume
        int indicatorHeight = indicatorPanelCount > 0 ?
                              (availableHeight - priceHeight - volumeHeight) / indicatorPanelCount : 0;

        // Create price panel
        layout.pricePanel = PlotArea(plotArea.x, plotArea.y, plotArea.width, priceHeight);

        int currentY = layout.pricePanel.GetBottom() + panelSpacing;

        // Create volume panel
        if (HasVolumeData()) {
            layout.volumePanel = PlotArea(plotArea.x, currentY, plotArea.width, volumeHeight);
            currentY = layout.volumePanel.GetBottom() + panelSpacing;
        }

        // Create indicator panels
        for (const auto& indicator : indicators) {
            if (RequiresSeparatePanel(indicator.type)) {
                PlotArea indicatorPanel(plotArea.x, currentY, plotArea.width, indicatorHeight);
                layout.indicatorPanels.push_back({indicatorPanel, indicator.type, indicator.name});
                currentY = indicatorPanel.GetBottom() + panelSpacing;
            }
        }

        // Time axis at bottom
        layout.timeAxis = PlotArea(plotArea.x, plotArea.GetBottom() - timeAxisHeight,
                                   plotArea.width, timeAxisHeight);

        return layout;
    }

    bool FinancialChartRenderer::HasVolumeData() const {
        return !candleData.empty() && candleData[0].volume > 0;
    }

    bool FinancialChartRenderer::RequiresSeparatePanel(TechnicalIndicator::Type type) const {
        return type == TechnicalIndicator::Type::RSI ||
               type == TechnicalIndicator::Type::MACD ||
               type == TechnicalIndicator::Type::Stochastic;
    }

// =============================================================================
// PRICE PANEL RENDERING
// =============================================================================

    void FinancialChartRenderer::DrawPricePanel(const PlotArea& panel,
                                                const DataBounds& timeBounds,
                                                const DataBounds& priceBounds,
                                                const ChartConfiguration& config,
                                                IRenderContext* ctx) {
        // Draw panel background
        ctx->SetFillColor(Color::FromARGB(config.plotAreaColor));
        ctx->FillRectangle(panel.ToRect2D());

        // Draw price grid
        DrawPriceGrid(panel, priceBounds, ctx);

        // Draw candlesticks
        DrawCandlesticks(panel, timeBounds, priceBounds, ctx);

        // Draw overlaid technical indicators (SMA, EMA, Bollinger)
        for (const auto& indicator : indicators) {
            if (!RequiresSeparatePanel(indicator.type)) {
                DrawTechnicalIndicator(indicator, panel, timeBounds, priceBounds, ctx);
            }
        }

        // Draw price axis
        DrawPriceAxis(panel, priceBounds, ctx);
    }

    void FinancialChartRenderer::DrawCandlesticks(const PlotArea& panel,
                                                  const DataBounds& timeBounds,
                                                  const DataBounds& priceBounds,
                                                  IRenderContext* ctx) {
        if (candleData.empty()) return;

        ChartCoordinateTransform transform(panel,
                                           DataBounds(timeBounds.minX, timeBounds.maxX, priceBounds.minY, priceBounds.maxY));

        // Calculate candle width
        double timeRange = timeBounds.maxX - timeBounds.minX;
        float candleWidth = (panel.width / static_cast<float>(candleData.size())) * 0.8f;
        candleWidth = std::min(candleWidth, 20.0f); // Maximum candle width
        candleWidth = std::max(candleWidth, 1.0f);  // Minimum candle width

        for (const auto& candle : candleData) {
            // Calculate screen coordinates
            float x = transform.DataToScreenX(candle.timestamp);
            float openY = transform.DataToScreenY(candle.open);
            float highY = transform.DataToScreenY(candle.high);
            float lowY = transform.DataToScreenY(candle.low);
            float closeY = transform.DataToScreenY(candle.close);

            // Determine candle color
            bool isUp = candle.close >= candle.open;
            Color candleColor = isUp ? Color(0, 200, 81) : Color(255, 68, 68); // Green up, red down
            Color wickColor = Color(117, 117, 117); // Gray wicks

            // Draw high-low line (wick)
            ctx->SetStrokeColor(wickColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine(x, highY, x, lowY);

            // Draw open-close rectangle (body)
            float bodyTop = std::min(openY, closeY);
            float bodyBottom = std::max(openY, closeY);
            float bodyHeight = bodyBottom - bodyTop;

            // Ensure minimum visible height for doji candles
            if (bodyHeight < 1.0f) {
                bodyHeight = 1.0f;
                bodyTop = (openY + closeY) / 2.0f - 0.5f;
            }

            ctx->SetFillColor(candleColor);
            ctx->FillRectangle(x - candleWidth / 2, bodyTop, candleWidth, bodyHeight);

            // Draw candle border for definition
            ctx->SetStrokeColor(candleColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawRectangle(x - candleWidth / 2, bodyTop, candleWidth, bodyHeight);
        }
    }

    void FinancialChartRenderer::DrawPriceGrid(const PlotArea& panel,
                                               const DataBounds& priceBounds,
                                               IRenderContext* ctx) {
        ctx->SetStrokeColor(Color(230, 230, 230));
        ctx->SetStrokeWidth(1.0f);

        // Calculate price levels for grid lines
        auto priceLevels = ChartRenderingHelpers::CalculateAxisTicks(priceBounds.minY, priceBounds.maxY, 6);

        ChartCoordinateTransform transform(panel, DataBounds(0, 1, priceBounds.minY, priceBounds.maxY));

        for (double price : priceLevels) {
            float y = transform.DataToScreenY(price);
            ctx->DrawLine(static_cast<float>(panel.x), y,
                          static_cast<float>(panel.GetRight()), y);
        }
    }

    void FinancialChartRenderer::DrawPriceAxis(const PlotArea& panel,
                                               const DataBounds& priceBounds,
                                               IRenderContext* ctx) {
        // Draw price labels on the right side
        ctx->SetTextColor(Color(80, 80, 80));
        ctx->SetFont("Arial", 10.0f);

        auto priceLevels = ChartRenderingHelpers::CalculateAxisTicks(priceBounds.minY, priceBounds.maxY, 6);
        ChartCoordinateTransform transform(panel, DataBounds(0, 1, priceBounds.minY, priceBounds.maxY));

        for (double price : priceLevels) {
            float y = transform.DataToScreenY(price);
            std::string priceText = "$" + ChartRenderingHelpers::FormatAxisLabel(price, 2);

            auto textSize = ChartRenderingHelpers::MeasureText(ctx, priceText, "Arial", 10.0f);
            ctx->DrawText(priceText, panel.GetRight() + 5, y + textSize.y / 2);
        }
    }

// =============================================================================
// VOLUME PANEL RENDERING
// =============================================================================

    void FinancialChartRenderer::DrawVolumePanel(const PlotArea& panel,
                                                 const DataBounds& timeBounds,
                                                 IRenderContext* ctx) {
        if (candleData.empty()) return;

        // Find volume bounds
        double maxVolume = 0;
        for (const auto& candle : candleData) {
            maxVolume = std::max(maxVolume, candle.volume);
        }

        DataBounds volumeBounds(timeBounds.minX, timeBounds.maxX, 0, maxVolume);
        ChartCoordinateTransform transform(panel, volumeBounds);

        // Draw volume background
        ctx->SetFillColor(Color(248, 248, 248));
        ctx->FillRectangle(panel.ToRect2D());

        // Draw volume bars
        float barWidth = (panel.width / static_cast<float>(candleData.size())) * 0.6f;
        barWidth = std::max(barWidth, 1.0f);

        for (const auto& candle : candleData) {
            float x = transform.DataToScreenX(candle.timestamp);
            float volumeHeight = transform.DataToScreenY(candle.volume);
            float barHeight = panel.GetBottom() - volumeHeight;

            // Color volume bars based on price direction
            bool isUp = candle.close >= candle.open;
            Color volumeColor = isUp ? Color(0, 200, 81, 128) : Color(255, 68, 68, 128);

            ctx->SetFillColor(volumeColor);
            ctx->FillRectangle(x - barWidth / 2, volumeHeight, barWidth, barHeight);
        }

        // Draw volume axis
        DrawVolumeAxis(panel, DataBounds(0, 1, 0, maxVolume), ctx);
    }

    void FinancialChartRenderer::DrawVolumeAxis(const PlotArea& panel,
                                                const DataBounds& volumeBounds,
                                                IRenderContext* ctx) {
        ctx->SetTextColor(Color(80, 80, 80));
        ctx->SetFont("Arial", 9.0f);

        auto volumeLevels = ChartRenderingHelpers::CalculateAxisTicks(0, volumeBounds.maxY, 3);
        ChartCoordinateTransform transform(panel, volumeBounds);

        for (double volume : volumeLevels) {
            if (volume <= 0) continue;

            float y = transform.DataToScreenY(volume);
            std::string volumeText = FormatVolume(volume);

            auto textSize = ChartRenderingHelpers::MeasureText(ctx, volumeText, "Arial", 9.0f);
            ctx->DrawText(volumeText, panel.GetRight() + 5, y + textSize.y / 2);
        }
    }

    std::string FinancialChartRenderer::FormatVolume(double volume) {
        if (volume >= 1000000) {
            return ChartRenderingHelpers::FormatAxisLabel(volume / 1000000, 1) + "M";
        } else if (volume >= 1000) {
            return ChartRenderingHelpers::FormatAxisLabel(volume / 1000, 1) + "K";
        } else {
            return ChartRenderingHelpers::FormatAxisLabel(volume, 0);
        }
    }

// =============================================================================
// TECHNICAL INDICATOR CALCULATIONS
// =============================================================================

    void FinancialChartRenderer::CalculateSMA(TechnicalIndicator& indicator, int period) {
        indicator.values.clear();
        indicator.values.reserve(candleData.size());

        for (size_t i = 0; i < candleData.size(); ++i) {
            if (i < static_cast<size_t>(period - 1)) {
                indicator.values.push_back(0.0); // Not enough data
            } else {
                double sum = 0.0;
                for (int j = 0; j < period; ++j) {
                    sum += candleData[i - j].close;
                }
                indicator.values.push_back(sum / period);
            }
        }
    }

    void FinancialChartRenderer::CalculateEMA(TechnicalIndicator& indicator, int period) {
        indicator.values.clear();
        indicator.values.reserve(candleData.size());

        if (candleData.empty()) return;

        double multiplier = 2.0 / (period + 1);
        double ema = candleData[0].close; // Start with first close price

        for (size_t i = 0; i < candleData.size(); ++i) {
            if (i == 0) {
                indicator.values.push_back(ema);
            } else {
                ema = (candleData[i].close * multiplier) + (ema * (1 - multiplier));
                indicator.values.push_back(ema);
            }
        }
    }

    void FinancialChartRenderer::CalculateBollingerBands(TechnicalIndicator& indicator,
                                                         int period, double stdDevMultiplier) {
        // First calculate SMA (middle band)
        CalculateSMA(indicator, period);

        indicator.upperBand.clear();
        indicator.lowerBand.clear();
        indicator.upperBand.reserve(candleData.size());
        indicator.lowerBand.reserve(candleData.size());

        for (size_t i = 0; i < candleData.size(); ++i) {
            if (i < static_cast<size_t>(period - 1)) {
                indicator.upperBand.push_back(0.0);
                indicator.lowerBand.push_back(0.0);
            } else {
                // Calculate standard deviation
                double mean = indicator.values[i];
                double variance = 0.0;

                for (int j = 0; j < period; ++j) {
                    double diff = candleData[i - j].close - mean;
                    variance += diff * diff;
                }

                double stdDev = std::sqrt(variance / period);
                indicator.upperBand.push_back(mean + stdDevMultiplier * stdDev);
                indicator.lowerBand.push_back(mean - stdDevMultiplier * stdDev);
            }
        }
    }

    void FinancialChartRenderer::CalculateRSI(TechnicalIndicator& indicator, int period) {
        indicator.values.clear();
        indicator.values.reserve(candleData.size());

        if (candleData.size() < 2) return;

        std::vector<double> gains, losses;
        gains.reserve(candleData.size());
        losses.reserve(candleData.size());

        // Calculate price changes
        for (size_t i = 1; i < candleData.size(); ++i) {
            double change = candleData[i].close - candleData[i-1].close;
            gains.push_back(change > 0 ? change : 0);
            losses.push_back(change < 0 ? -change : 0);
        }

        // Calculate RSI
        for (size_t i = 0; i < gains.size(); ++i) {
            if (i < static_cast<size_t>(period - 1)) {
                indicator.values.push_back(0.0);
            } else {
                double avgGain = 0, avgLoss = 0;
                for (int j = 0; j < period; ++j) {
                    avgGain += gains[i - j];
                    avgLoss += losses[i - j];
                }
                avgGain /= period;
                avgLoss /= period;

                double rs = avgLoss == 0 ? 100 : avgGain / avgLoss;
                double rsi = 100 - (100 / (1 + rs));
                indicator.values.push_back(rsi);
            }
        }

        // Add leading zero for alignment with candleData
        indicator.values.insert(indicator.values.begin(), 0.0);
    }

    void FinancialChartRenderer::CalculateMACD(TechnicalIndicator& indicator,
                                               int fastPeriod, int slowPeriod, int signalPeriod) {
        // Calculate fast and slow EMAs
        TechnicalIndicator fastEMA(TechnicalIndicator::Type::EMA, 0, "FastEMA");
        TechnicalIndicator slowEMA(TechnicalIndicator::Type::EMA, 0, "SlowEMA");

        CalculateEMA(fastEMA, fastPeriod);
        CalculateEMA(slowEMA, slowPeriod);

        // Calculate MACD line (fast EMA - slow EMA)
        indicator.values.clear();
        indicator.values.reserve(candleData.size());

        for (size_t i = 0; i < candleData.size(); ++i) {
            double macdValue = fastEMA.values[i] - slowEMA.values[i];
            indicator.values.push_back(macdValue);
        }

        // Calculate signal line (EMA of MACD)
        indicator.upperBand.clear(); // Reuse for signal line
        indicator.upperBand.reserve(candleData.size());

        if (!indicator.values.empty()) {
            double multiplier = 2.0 / (signalPeriod + 1);
            double signal = indicator.values[0];

            for (size_t i = 0; i < indicator.values.size(); ++i) {
                if (i == 0) {
                    indicator.upperBand.push_back(signal);
                } else {
                    signal = (indicator.values[i] * multiplier) + (signal * (1 - multiplier));
                    indicator.upperBand.push_back(signal);
                }
            }
        }

        // Calculate histogram (MACD - Signal)
        indicator.lowerBand.clear(); // Reuse for histogram
        indicator.lowerBand.reserve(candleData.size());

        for (size_t i = 0; i < indicator.values.size(); ++i) {
            double histogram = indicator.values[i] - indicator.upperBand[i];
            indicator.lowerBand.push_back(histogram);
        }
    }

    void FinancialChartRenderer::CalculateStochastic(TechnicalIndicator& indicator,
                                                     int kPeriod, int dPeriod) {
        indicator.values.clear(); // %K line
        indicator.upperBand.clear(); // %D line
        indicator.values.reserve(candleData.size());
        indicator.upperBand.reserve(candleData.size());

        // Calculate %K
        for (size_t i = 0; i < candleData.size(); ++i) {
            if (i < static_cast<size_t>(kPeriod - 1)) {
                indicator.values.push_back(0.0);
            } else {
                // Find highest high and lowest low in period
                double highestHigh = candleData[i].high;
                double lowestLow = candleData[i].low;

                for (int j = 1; j < kPeriod; ++j) {
                    highestHigh = std::max(highestHigh, candleData[i - j].high);
                    lowestLow = std::min(lowestLow, candleData[i - j].low);
                }

                double k = highestHigh == lowestLow ? 50.0 :
                           ((candleData[i].close - lowestLow) / (highestHigh - lowestLow)) * 100.0;
                indicator.values.push_back(k);
            }
        }

        // Calculate %D (SMA of %K)
        for (size_t i = 0; i < indicator.values.size(); ++i) {
            if (i < static_cast<size_t>(dPeriod - 1)) {
                indicator.upperBand.push_back(0.0);
            } else {
                double sum = 0.0;
                for (int j = 0; j < dPeriod; ++j) {
                    sum += indicator.values[i - j];
                }
                indicator.upperBand.push_back(sum / dPeriod);
            }
        }
    }

// =============================================================================
// TECHNICAL INDICATOR RENDERING
// =============================================================================

    void FinancialChartRenderer::DrawTechnicalIndicator(const TechnicalIndicator& indicator,
                                                        const PlotArea& panel,
                                                        const DataBounds& timeBounds,
                                                        const DataBounds& priceBounds,
                                                        IRenderContext* ctx) {
        if (indicator.values.empty() || candleData.empty()) return;

        ctx->SetStrokeColor(Color::FromARGB(indicator.color));
        ctx->SetStrokeWidth(2.0f);

        switch (indicator.type) {
            case TechnicalIndicator::Type::SMA:
            case TechnicalIndicator::Type::EMA:
                DrawMovingAverageLine(indicator, panel, timeBounds, priceBounds, ctx);
                break;

            case TechnicalIndicator::Type::Bollinger:
                DrawBollingerBands(indicator, panel, timeBounds, priceBounds, ctx);
                break;

            default:
                // Other indicators are drawn in separate panels
                break;
        }
    }

    void FinancialChartRenderer::DrawMovingAverageLine(const TechnicalIndicator& indicator,
                                                       const PlotArea& panel,
                                                       const DataBounds& timeBounds,
                                                       const DataBounds& priceBounds,
                                                       IRenderContext* ctx) {
        ChartCoordinateTransform transform(panel,
                                           DataBounds(timeBounds.minX, timeBounds.maxX, priceBounds.minY, priceBounds.maxY));

        Point2D prevPoint;
        bool firstPoint = true;

        for (size_t i = 0; i < std::min(indicator.values.size(), candleData.size()); ++i) {
            if (indicator.values[i] <= 0) continue; // Skip invalid values

            Point2D currentPoint = transform.DataToScreen(candleData[i].timestamp, indicator.values[i]);

            if (!firstPoint) {
                ctx->DrawLine(prevPoint, currentPoint);
            }

            prevPoint = currentPoint;
            firstPoint = false;
        }
    }

    void FinancialChartRenderer::DrawBollingerBands(const TechnicalIndicator& indicator,
                                                    const PlotArea& panel,
                                                    const DataBounds& timeBounds,
                                                    const DataBounds& priceBounds,
                                                    IRenderContext* ctx) {
        if (indicator.upperBand.empty() || indicator.lowerBand.empty()) return;

        ChartCoordinateTransform transform(panel,
                                           DataBounds(timeBounds.minX, timeBounds.maxX, priceBounds.minY, priceBounds.maxY));

        // Draw middle line (SMA)
        ctx->SetStrokeColor(Color::FromARGB(indicator.color));
        ctx->SetStrokeWidth(2.0f);
        DrawMovingAverageLine(indicator, panel, timeBounds, priceBounds, ctx);

        // Draw upper and lower bands
        Color bandColor = Color::FromARGB(indicator.color);
        bandColor.a = 128; // Semi-transparent
        ctx->SetStrokeColor(bandColor);
        ctx->SetStrokeWidth(1.0f);

        // Upper band
        Point2D prevUpper;
        bool firstUpper = true;

        for (size_t i = 0; i < std::min(indicator.upperBand.size(), candleData.size()); ++i) {
            if (indicator.upperBand[i] <= 0) continue;

            Point2D currentUpper = transform.DataToScreen(candleData[i].timestamp, indicator.upperBand[i]);

            if (!firstUpper) {
                ctx->DrawLine(prevUpper, currentUpper);
            }

            prevUpper = currentUpper;
            firstUpper = false;
        }

        // Lower band
        Point2D prevLower;
        bool firstLower = true;

        for (size_t i = 0; i < std::min(indicator.lowerBand.size(), candleData.size()); ++i) {
            if (indicator.lowerBand[i] <= 0) continue;

            Point2D currentLower = transform.DataToScreen(candleData[i].timestamp, indicator.lowerBand[i]);

            if (!firstLower) {
                ctx->DrawLine(prevLower, currentLower);
            }

            prevLower = currentLower;
            firstLower = false;
        }

        // Fill area between bands
        ctx->SetFillColor(Color(bandColor.r, bandColor.g, bandColor.b, 32)); // Very transparent
        DrawBandFill(indicator, panel, timeBounds, priceBounds, ctx);
    }

    void FinancialChartRenderer::DrawBandFill(const TechnicalIndicator& indicator,
                                              const PlotArea& panel,
                                              const DataBounds& timeBounds,
                                              const DataBounds& priceBounds,
                                              IRenderContext* ctx) {
        if (indicator.upperBand.empty() || indicator.lowerBand.empty()) return;

        ChartCoordinateTransform transform(panel,
                                           DataBounds(timeBounds.minX, timeBounds.maxX, priceBounds.minY, priceBounds.maxY));

        std::vector<Point2D> fillPoints;

        // Add upper band points
        for (size_t i = 0; i < std::min(indicator.upperBand.size(), candleData.size()); ++i) {
            if (indicator.upperBand[i] > 0) {
                fillPoints.push_back(transform.DataToScreen(candleData[i].timestamp, indicator.upperBand[i]));
            }
        }

        // Add lower band points in reverse order
        for (int i = static_cast<int>(std::min(indicator.lowerBand.size(), candleData.size())) - 1; i >= 0; --i) {
            if (indicator.lowerBand[i] > 0) {
                fillPoints.push_back(transform.DataToScreen(candleData[i].timestamp, indicator.lowerBand[i]));
            }
        }

        if (fillPoints.size() > 4) {
            ctx->FillPath(fillPoints);
        }
    }

// =============================================================================
// INDICATOR PANEL RENDERING
// =============================================================================

    void FinancialChartRenderer::DrawIndicatorPanels(const std::vector<IndicatorPanel>& panels,
                                                     const DataBounds& timeBounds,
                                                     IRenderContext* ctx) {
        for (const auto& panel : panels) {
            DrawIndicatorPanel(panel, timeBounds, ctx);
        }
    }

    void FinancialChartRenderer::DrawIndicatorPanel(const IndicatorPanel& panel,
                                                    const DataBounds& timeBounds,
                                                    IRenderContext* ctx) {
        // Find the indicator for this panel
        auto it = std::find_if(indicators.begin(), indicators.end(),
                               [&panel](const TechnicalIndicator& indicator) {
                                   return indicator.type == panel.indicatorType;
                               });

        if (it == indicators.end()) return;

        const TechnicalIndicator& indicator = *it;

        // Draw panel background
        ctx->SetFillColor(Color(250, 250, 250));
        ctx->FillRectangle(panel.area.ToRect2D());

        // Draw panel border
        ctx->SetStrokeColor(Color(200, 200, 200));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(panel.area.ToRect2D());

        switch (panel.indicatorType) {
            case TechnicalIndicator::Type::RSI:
                DrawRSIPanel(indicator, panel.area, timeBounds, ctx);
                break;

            case TechnicalIndicator::Type::MACD:
                DrawMACDPanel(indicator, panel.area, timeBounds, ctx);
                break;

            case TechnicalIndicator::Type::Stochastic:
                DrawStochasticPanel(indicator, panel.area, timeBounds, ctx);
                break;

            default:
                break;
        }

        // Draw panel title
        ctx->SetTextColor(Color(60, 60, 60));
        ctx->SetFont("Arial", 11.0f);
        ctx->DrawText(panel.title, panel.area.x + 5, panel.area.y + 15);
    }

    void FinancialChartRenderer::DrawRSIPanel(const TechnicalIndicator& indicator,
                                              const PlotArea& panel,
                                              const DataBounds& timeBounds,
                                              IRenderContext* ctx) {
        // RSI oscillates between 0 and 100
        DataBounds rsiBounds(timeBounds.minX, timeBounds.maxX, 0, 100);
        ChartCoordinateTransform transform(panel, rsiBounds);

        // Draw reference lines at 30 and 70
        ctx->SetStrokeColor(Color(180, 180, 180));
        ctx->SetStrokeWidth(1.0f);

        float oversoldY = transform.DataToScreenY(30);
        float overboughtY = transform.DataToScreenY(70);
        float midlineY = transform.DataToScreenY(50);

        ctx->DrawLine(panel.x, oversoldY, panel.GetRight(), oversoldY);
        ctx->DrawLine(panel.x, overboughtY, panel.GetRight(), overboughtY);
        ctx->DrawLine(panel.x, midlineY, panel.GetRight(), midlineY);

        // Draw RSI line
        ctx->SetStrokeColor(Color::FromARGB(indicator.color));
        ctx->SetStrokeWidth(2.0f);

        Point2D prevPoint;
        bool firstPoint = true;

        for (size_t i = 0; i < std::min(indicator.values.size(), candleData.size()); ++i) {
            if (indicator.values[i] <= 0) continue;

            Point2D currentPoint = transform.DataToScreen(candleData[i].timestamp, indicator.values[i]);

            if (!firstPoint) {
                ctx->DrawLine(prevPoint, currentPoint);
            }

            prevPoint = currentPoint;
            firstPoint = false;
        }

        // Draw axis labels
        ctx->SetTextColor(Color(80, 80, 80));
        ctx->SetFont("Arial", 9.0f);

        auto textSize30 = ChartRenderingHelpers::MeasureText(ctx, "30", "Arial", 9.0f);
        auto textSize70 = ChartRenderingHelpers::MeasureText(ctx, "70", "Arial", 9.0f);

        ctx->DrawText("30", panel.GetRight() + 5, oversoldY + textSize30.y / 2);
        ctx->DrawText("70", panel.GetRight() + 5, overboughtY + textSize70.y / 2);
    }

    void FinancialChartRenderer::DrawMACDPanel(const TechnicalIndicator& indicator,
                                               const PlotArea& panel,
                                               const DataBounds& timeBounds,
                                               IRenderContext* ctx) {
        if (indicator.values.empty()) return;

        // Find MACD value range for scaling
        double minMACD = *std::min_element(indicator.values.begin(), indicator.values.end());
        double maxMACD = *std::max_element(indicator.values.begin(), indicator.values.end());

        // Include histogram in range calculation
        if (!indicator.lowerBand.empty()) {
            double minHist = *std::min_element(indicator.lowerBand.begin(), indicator.lowerBand.end());
            double maxHist = *std::max_element(indicator.lowerBand.begin(), indicator.lowerBand.end());
            minMACD = std::min(minMACD, minHist);
            maxMACD = std::max(maxMACD, maxHist);
        }

        DataBounds macdBounds(timeBounds.minX, timeBounds.maxX, minMACD, maxMACD);
        ChartCoordinateTransform transform(panel, macdBounds);

        // Draw zero line
        ctx->SetStrokeColor(Color(150, 150, 150));
        ctx->SetStrokeWidth(1.0f);
        float zeroY = transform.DataToScreenY(0);
        ctx->DrawLine(panel.x, zeroY, panel.GetRight(), zeroY);

        // Draw histogram (MACD - Signal)
        if (!indicator.lowerBand.empty()) {
            for (size_t i = 0; i < std::min(indicator.lowerBand.size(), candleData.size()); ++i) {
                float x = transform.DataToScreenX(candleData[i].timestamp);
                float histogramValue = static_cast<float>(indicator.lowerBand[i]);
                float histogramY = transform.DataToScreenY(histogramValue);

                Color histColor = histogramValue >= 0 ? Color(0, 150, 0, 128) : Color(150, 0, 0, 128);
                ctx->SetFillColor(histColor);

                float barHeight = std::abs(histogramY - zeroY);
                float barTop = std::min(histogramY, zeroY);

                ctx->FillRectangle(x - 1, barTop, 2, barHeight);
            }
        }

        // Draw MACD line
        ctx->SetStrokeColor(Color::FromARGB(indicator.color));
        ctx->SetStrokeWidth(2.0f);

        Point2D prevMACD;
        bool firstMACD = true;

        for (size_t i = 0; i < std::min(indicator.values.size(), candleData.size()); ++i) {
            Point2D currentMACD = transform.DataToScreen(candleData[i].timestamp, indicator.values[i]);

            if (!firstMACD) {
                ctx->DrawLine(prevMACD, currentMACD);
            }

            prevMACD = currentMACD;
            firstMACD = false;
        }

        // Draw Signal line
        if (!indicator.upperBand.empty()) {
            ctx->SetStrokeColor(Color(255, 140, 0)); // Orange
            ctx->SetStrokeWidth(1.5f);

            Point2D prevSignal;
            bool firstSignal = true;

            for (size_t i = 0; i < std::min(indicator.upperBand.size(), candleData.size()); ++i) {
                Point2D currentSignal = transform.DataToScreen(candleData[i].timestamp, indicator.upperBand[i]);

                if (!firstSignal) {
                    ctx->DrawLine(prevSignal, currentSignal);
                }

                prevSignal = currentSignal;
                firstSignal = false;
            }
        }
    }

    void FinancialChartRenderer::DrawStochasticPanel(const TechnicalIndicator& indicator,
                                                     const PlotArea& panel,
                                                     const DataBounds& timeBounds,
                                                     IRenderContext* ctx) {
        // Stochastic oscillates between 0 and 100
        DataBounds stochBounds(timeBounds.minX, timeBounds.maxX, 0, 100);
        ChartCoordinateTransform transform(panel, stochBounds);

        // Draw reference lines at 20 and 80
        ctx->SetStrokeColor(Color(180, 180, 180));
        ctx->SetStrokeWidth(1.0f);

        float oversoldY = transform.DataToScreenY(20);
        float overboughtY = transform.DataToScreenY(80);
        float midlineY = transform.DataToScreenY(50);

        ctx->DrawLine(panel.x, oversoldY, panel.GetRight(), oversoldY);
        ctx->DrawLine(panel.x, overboughtY, panel.GetRight(), overboughtY);
        ctx->DrawLine(panel.x, midlineY, panel.GetRight(), midlineY);

        // Draw %K line
        ctx->SetStrokeColor(Color::FromARGB(indicator.color));
        ctx->SetStrokeWidth(2.0f);

        Point2D prevK;
        bool firstK = true;

        for (size_t i = 0; i < std::min(indicator.values.size(), candleData.size()); ++i) {
            if (indicator.values[i] <= 0) continue;

            Point2D currentK = transform.DataToScreen(candleData[i].timestamp, indicator.values[i]);

            if (!firstK) {
                ctx->DrawLine(prevK, currentK);
            }

            prevK = currentK;
            firstK = false;
        }

        // Draw %D line
        if (!indicator.upperBand.empty()) {
            ctx->SetStrokeColor(Color(255, 140, 0)); // Orange
            ctx->SetStrokeWidth(1.5f);

            Point2D prevD;
            bool firstD = true;

            for (size_t i = 0; i < std::min(indicator.upperBand.size(), candleData.size()); ++i) {
                if (indicator.upperBand[i] <= 0) continue;

                Point2D currentD = transform.DataToScreen(candleData[i].timestamp, indicator.upperBand[i]);

                if (!firstD) {
                    ctx->DrawLine(prevD, currentD);
                }

                prevD = currentD;
                firstD = false;
            }
        }
    }

// =============================================================================
// TIME AXIS RENDERING
// =============================================================================

    void FinancialChartRenderer::DrawTimeAxis(const FinancialLayout& layout,
                                              const DataBounds& timeBounds,
                                              IRenderContext* ctx) {
        if (candleData.empty()) return;

        // Draw time axis background
        ctx->SetFillColor(Color(240, 240, 240));
        ctx->FillRectangle(layout.timeAxis.ToRect2D());

        // Calculate time labels
        auto timeTicks = CalculateTimeTicks(timeBounds);

        ctx->SetTextColor(Color(80, 80, 80));
        ctx->SetFont("Arial", 10.0f);

        // Use price panel width for coordinate transformation
        ChartCoordinateTransform transform(layout.pricePanel,
                                           DataBounds(timeBounds.minX, timeBounds.maxX, 0, 1));

        for (const auto& tick : timeTicks) {
            float x = transform.DataToScreenX(tick.timestamp);

            // Draw tick mark
            ctx->SetStrokeColor(Color(120, 120, 120));
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine(x, layout.timeAxis.y, x, layout.timeAxis.y + 5);

            // Draw label
            auto textSize = ChartRenderingHelpers::MeasureText(ctx, tick.label, "Arial", 10.0f);
            ctx->DrawText(tick.label, x - textSize.x / 2, layout.timeAxis.y + 20);
        }
    }

    std::vector<FinancialChartRenderer::TimeTick> FinancialChartRenderer::CalculateTimeTicks(const DataBounds& timeBounds) {
        std::vector<TimeTick> ticks;

        if (candleData.empty()) return ticks;

        // Simple implementation: show every Nth candle based on data size
        size_t step = std::max(1UL, candleData.size() / 8); // Show ~8 labels

        for (size_t i = 0; i < candleData.size(); i += step) {
            TimeTick tick;
            tick.timestamp = candleData[i].timestamp;
            tick.label = FormatTimestamp(candleData[i].timestamp);
            ticks.push_back(tick);
        }

        return ticks;
    }

    std::string FinancialChartRenderer::FormatTimestamp(double timestamp) {
        // Convert Unix timestamp to readable date
        // This is a simplified implementation
        time_t timeT = static_cast<time_t>(timestamp);
        struct tm* timeInfo = localtime(&timeT);

        char buffer[20];
        strftime(buffer, sizeof(buffer), "%m/%d", timeInfo);
        return std::string(buffer);
    }

// =============================================================================
// DATA BOUNDS CALCULATION
// =============================================================================

    DataBounds FinancialChartRenderer::CalculateTimeBounds() {
        if (candleData.empty()) return DataBounds();

        double minTime = candleData.front().timestamp;
        double maxTime = candleData.back().timestamp;

        return DataBounds(minTime, maxTime, 0, 1);
    }

    DataBounds FinancialChartRenderer::CalculatePriceBounds() {
        if (candleData.empty()) return DataBounds();

        double minPrice = candleData[0].low;
        double maxPrice = candleData[0].high;

        for (const auto& candle : candleData) {
            minPrice = std::min(minPrice, candle.low);
            maxPrice = std::max(maxPrice, candle.high);
        }

        // Add 5% margin for better visualization
        double margin = (maxPrice - minPrice) * 0.05;
        return DataBounds(0, 1, minPrice - margin, maxPrice + margin);
    }

} // namespace UltraCanvas

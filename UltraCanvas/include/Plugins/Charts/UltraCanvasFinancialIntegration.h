// UltraCanvasFinancialIntegration.h
// Financial chart element integration with UltraCanvas UI system
// Version: 1.0.1
// Last Modified: 2025-09-10
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartElement.h"
#include "Plugins/Charts/UltraCanvasChartSpecialized.h"
#include <memory>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// FINANCIAL CHART ELEMENT
// =============================================================================

    class UltraCanvasFinancialChartElement : public UltraCanvasElement {
    private:
        FinancialChartRenderer financialRenderer;
        ChartConfiguration chartConfig;

        // Financial-specific settings
        bool showVolume = true;
        bool showIndicatorPanels = true;
        std::vector<std::string> enabledIndicators;

        // Interactive state
        bool isDragging = false;
        Point2D lastMousePos;
        float timeZoomLevel = 1.0f;
        double timeOffset = 0.0;

        // Tooltip state for financial data
        bool showCandleTooltip = false;
        Point2D tooltipPosition;
        std::string candleTooltipText;
        size_t hoveredCandleIndex = SIZE_MAX;

    public:
        UltraCanvasFinancialChartElement(const std::string& id, long uid, int x, int y, int width, int height)
                : UltraCanvasElement(id, uid, x, y, width, height) {

            // Initialize with default financial chart configuration
            chartConfig.type = ChartType::CandleStick;
            chartConfig.title = "Financial Chart";

            // Financial chart specific settings
            chartConfig.showVolume = true;
            chartConfig.enableTooltips = true;
            chartConfig.enableZoom = true;
            chartConfig.enablePan = true;

            SetMouseControls(1);
            SetActive(true);
            SetVisible(true);
        }

        // =============================================================================
        // FINANCIAL DATA MANAGEMENT
        // =============================================================================

        void LoadStockData(const std::vector<FinancialChartRenderer::CandlestickData>& data) {
            financialRenderer.LoadCandlestickData(data);
            Invalidate();
        }

        void LoadStockDataFromCSV(const std::string& filePath) {
            auto stockData = LoadFinancialCSV(filePath);
            if (!stockData.empty()) {
                LoadStockData(stockData);
            }
        }

        void AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type type,
                                   const std::vector<double>& params,
                                   uint32_t color,
                                   const std::string& name) {
            financialRenderer.AddTechnicalIndicator(type, params, color, name);
            enabledIndicators.push_back(name);
            Invalidate();
        }

        void RemoveTechnicalIndicator(const std::string& name) {
            // Remove from enabled list
            auto it = std::find(enabledIndicators.begin(), enabledIndicators.end(), name);
            if (it != enabledIndicators.end()) {
                enabledIndicators.erase(it);
            }

            // Reload all indicators except the removed one
            // This is a simplified approach - in production you'd want a more efficient removal
            RefreshIndicators();
            Invalidate();
        }

        // =============================================================================
        // PROFESSIONAL INDICATOR SETUPS
        // =============================================================================

        void AddStandardTradingIndicators() {
            // Moving Averages
            AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type::SMA,
                                  {20}, 0xFF0066CC, "SMA-20");
            AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type::SMA,
                                  {50}, 0xFFCC6600, "SMA-50");
            AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type::EMA,
                                  {12}, 0xFF00AA00, "EMA-12");

            // Bollinger Bands
            AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type::Bollinger,
                                  {20, 2.0}, 0xFFAA00AA, "Bollinger (20,2)");

            // Oscillators
            AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type::RSI,
                                  {14}, 0xFF00CCCC, "RSI-14");
            AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type::MACD,
                                  {12, 26, 9}, 0xFFFF6600, "MACD (12,26,9)");
        }

        void AddDayTradingSetup() {
            // Short-term indicators for day trading
            AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type::EMA,
                                  {9}, 0xFF0066CC, "EMA-9");
            AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type::EMA,
                                  {21}, 0xFFCC6600, "EMA-21");
            AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type::RSI,
                                  {9}, 0xFF00CCCC, "RSI-9");
            AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type::Stochastic,
                                  {14, 3}, 0xFFFF6600, "Stochastic (14,3)");
        }

        void AddSwingTradingSetup() {
            // Medium-term indicators for swing trading
            AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type::SMA,
                                  {20}, 0xFF0066CC, "SMA-20");
            AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type::SMA,
                                  {50}, 0xFFCC6600, "SMA-50");
            AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type::SMA,
                                  {200}, 0xFF666666, "SMA-200");
            AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type::RSI,
                                  {14}, 0xFF00CCCC, "RSI-14");
            AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type::MACD,
                                  {12, 26, 9}, 0xFFFF6600, "MACD");
        }

        void AddScalpingSetup() {
            // Very short-term indicators for scalping
            AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type::EMA,
                                  {5}, 0xFF0066CC, "EMA-5");
            AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type::EMA,
                                  {13}, 0xFFCC6600, "EMA-13");
            AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type::RSI,
                                  {7}, 0xFF00CCCC, "RSI-7");
            AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type::Stochastic,
                                  {5, 3}, 0xFFFF6600, "Fast Stochastic");
        }

        // =============================================================================
        // DISPLAY SETTINGS
        // =============================================================================

        void ShowVolume(bool show) {
            showVolume = show;
            chartConfig.showVolume = show;
            Invalidate();
        }

        void ShowIndicatorPanels(bool show) {
            showIndicatorPanels = show;
            Invalidate();
        }

        void SetCandlestickStyle(uint32_t upColor, uint32_t downColor, uint32_t wickColor) {
            chartConfig.candlestickStyle.upColor = upColor;
            chartConfig.candlestickStyle.downColor = downColor;
            chartConfig.candlestickStyle.wickColor = wickColor;
            Invalidate();
        }

        void SetVolumeColors(uint32_t upColor, uint32_t downColor) {
            chartConfig.volumeUpColor = upColor;
            chartConfig.volumeDownColor = downColor;
            Invalidate();
        }

        void SetTimeframe(const std::string& timeframe) {
            chartConfig.subtitle = "Timeframe: " + timeframe;
            Invalidate();
        }

        // =============================================================================
        // MARKET ANALYSIS FEATURES
        // =============================================================================

        void AddSupportResistanceLevel(double price, const std::string& label, uint32_t color = 0xFF00AA00) {
            UltraCanvasChartRenderer::AddAxisHighlight(chartConfig, "y", price, color, label);
            Invalidate();
        }

        void AddFibonacciRetracement(double high, double low) {
            // Standard Fibonacci levels
            double range = high - low;

            AddSupportResistanceLevel(high, "100.0% (" + FormatPrice(high) + ")", 0xFF666666);
            AddSupportResistanceLevel(high - range * 0.236, "76.4% (" + FormatPrice(high - range * 0.236) + ")", 0xFFFFAA00);
            AddSupportResistanceLevel(high - range * 0.382, "61.8% (" + FormatPrice(high - range * 0.382) + ")", 0xFFFF6600);
            AddSupportResistanceLevel(high - range * 0.5, "50.0% (" + FormatPrice(high - range * 0.5) + ")", 0xFFFF0000);
            AddSupportResistanceLevel(high - range * 0.618, "38.2% (" + FormatPrice(high - range * 0.618) + ")", 0xFFFF6600);
            AddSupportResistanceLevel(high - range * 0.764, "23.6% (" + FormatPrice(high - range * 0.764) + ")", 0xFFFFAA00);
            AddSupportResistanceLevel(low, "0.0% (" + FormatPrice(low) + ")", 0xFF666666);

            Invalidate();
        }

        void AddTrendLine(double startPrice, double endPrice, uint32_t color = 0xFF0066CC) {
            TrendLine trend(TrendLine::Type::Linear, color);
            trend.label = "Trend Line";
            chartConfig.trendLines.push_back(trend);
            Invalidate();
        }

        // =============================================================================
        // FINANCIAL ANALYSIS
        // =============================================================================

        struct MarketSummary {
            double currentPrice;
            double dayChange;
            double dayChangePercent;
            double dayHigh;
            double dayLow;
            double volume;
            std::string trend; // "Bullish", "Bearish", "Neutral"
        };

        MarketSummary GetMarketSummary() const {
            MarketSummary summary = {};

            if (financialRenderer.GetCandleData().empty()) return summary;

            const auto& candles = financialRenderer.GetCandleData();
            const auto& lastCandle = candles.back();

            summary.currentPrice = lastCandle.close;
            summary.dayHigh = lastCandle.high;
            summary.dayLow = lastCandle.low;
            summary.volume = lastCandle.volume;

            if (candles.size() > 1) {
                const auto& prevCandle = candles[candles.size() - 2];
                summary.dayChange = lastCandle.close - prevCandle.close;
                summary.dayChangePercent = (summary.dayChange / prevCandle.close) * 100.0;

                // Simple trend determination
                if (summary.dayChangePercent > 1.0) {
                    summary.trend = "Bullish";
                } else if (summary.dayChangePercent < -1.0) {
                    summary.trend = "Bearish";
                } else {
                    summary.trend = "Neutral";
                }
            }

            return summary;
        }

        // =============================================================================
        // RENDERING OVERRIDE
        // =============================================================================

        void Render(CanvasContext* canvasCtx) override {
            if (!canvasCtx) return;

            IRenderContext* ctx = canvasCtx->GetRenderInterface();
            if (!ctx) return;

            // Set clipping to element bounds
            ctx->SetClipRect(GetX(), GetY(), GetWidth(), GetHeight());

            // Calculate plot area
            PlotArea plotArea(GetX() + 10, GetY() + 10, GetWidth() - 20, GetHeight() - 20);

            // Render financial chart
            financialRenderer.RenderFinancialChart(chartConfig, plotArea, ctx);

            // Draw financial-specific overlays
            if (showCandleTooltip) {
                DrawFinancialTooltip(ctx);
            }

            // Draw market summary if enabled
            DrawMarketSummary(ctx);

            // Clear clipping
            ctx->ClearClipRect();
        }

        // =============================================================================
        // EVENT HANDLING
        // =============================================================================

        bool HandleEvent(const UCEvent& event) override {
            if (!IsActive() || !IsVisible()) return false;

            switch (event.type) {
                case UCEventType::MouseMove:
                    return HandleFinancialMouseMove(event);
                case UCEventType::MouseWheel:
                    return HandleFinancialMouseWheel(event);
                case UCEventType::KeyDown:
                    return HandleFinancialKeyDown(event);
                default:
                    return UltraCanvasElement::HandleEvent(event);
            }
        }

    private:
        // =============================================================================
        // FINANCIAL EVENT HANDLERS
        // =============================================================================

        bool HandleFinancialMouseMove(const UCEvent& event) {
            if (!Contains(event.x, event.y)) {
                showCandleTooltip = false;
                hoveredCandleIndex = SIZE_MAX;
                Invalidate();
                return false;
            }

            Point2D mousePos(static_cast<float>(event.x), static_cast<float>(event.y));
            UpdateFinancialTooltip(mousePos);

            return true;
        }

        bool HandleFinancialMouseWheel(const UCEvent& event) {
            if (!Contains(event.x, event.y) || !chartConfig.enableZoom) return false;

            // Time-based zooming for financial charts
            float zoomFactor = (event.delta > 0) ? 1.1f : 0.9f;
            timeZoomLevel *= zoomFactor;
            timeZoomLevel = std::clamp(timeZoomLevel, 0.1f, 10.0f);

            Invalidate();
            return true;
        }

        bool HandleFinancialKeyDown(const UCEvent& event) {
            switch (event.key) {
                case 'V':
                case 'v':
                    // Toggle volume display
                    ShowVolume(!showVolume);
                    return true;

                case 'I':
                case 'i':
                    // Toggle indicator panels
                    ShowIndicatorPanels(!showIndicatorPanels);
                    return true;

                case 'F':
                case 'f':
                    // Zoom to fit all data
                    timeZoomLevel = 1.0f;
                    timeOffset = 0.0;
                    Invalidate();
                    return true;

                case '1':
                    // Add standard trading setup
                    ClearIndicators();
                    AddStandardTradingIndicators();
                    return true;

                case '2':
                    // Add day trading setup
                    ClearIndicators();
                    AddDayTradingSetup();
                    return true;

                case '3':
                    // Add swing trading setup
                    ClearIndicators();
                    AddSwingTradingSetup();
                    return true;

                case '4':
                    // Add scalping setup
                    ClearIndicators();
                    AddScalpingSetup();
                    return true;

                default:
                    return false;
            }
        }

        // =============================================================================
        // FINANCIAL TOOLTIP SYSTEM
        // =============================================================================

        void UpdateFinancialTooltip(const Point2D& mousePos) {
            const auto& candleData = financialRenderer.GetCandleData();
            if (candleData.empty()) {
                showCandleTooltip = false;
                return;
            }

            // Calculate which candle the mouse is over
            PlotArea plotArea(GetX() + 10, GetY() + 10, GetWidth() - 20, GetHeight() - 20);

            // Simple approximation - in production you'd want precise coordinate transformation
            float relativeX = (mousePos.x - plotArea.x) / plotArea.width;
            size_t candleIndex = static_cast<size_t>(relativeX * candleData.size());

            if (candleIndex < candleData.size()) {
                const auto& candle = candleData[candleIndex];

                // Format financial tooltip
                candleTooltipText = FormatFinancialTooltip(candle, candleIndex);
                tooltipPosition = mousePos;
                hoveredCandleIndex = candleIndex;
                showCandleTooltip = true;
            } else {
                showCandleTooltip = false;
                hoveredCandleIndex = SIZE_MAX;
            }

            Invalidate();
        }

        std::string FormatFinancialTooltip(const FinancialChartRenderer::CandlestickData& candle, size_t index) {
            std::ostringstream oss;

            // Format timestamp
            time_t timeT = static_cast<time_t>(candle.timestamp);
            struct tm* timeInfo = localtime(&timeT);
            char dateBuffer[50];
            strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d %H:%M", timeInfo);

            oss << "Date: " << dateBuffer << "\n";
            oss << "Open: $" << std::fixed << std::setprecision(2) << candle.open << "\n";
            oss << "High: $" << candle.high << "\n";
            oss << "Low: $" << candle.low << "\n";
            oss << "Close: $" << candle.close << "\n";

            if (candle.volume > 0) {
                oss << "Volume: " << FormatVolume(candle.volume) << "\n";
            }

            // Calculate change from previous candle
            const auto& candleData = financialRenderer.GetCandleData();
            if (index > 0 && index < candleData.size()) {
                double change = candle.close - candleData[index - 1].close;
                double changePercent = (change / candleData[index - 1].close) * 100.0;

                oss << "Change: ";
                if (change >= 0) oss << "+";
                oss << std::fixed << std::setprecision(2) << change << " (";
                if (changePercent >= 0) oss << "+";
                oss << std::setprecision(1) << changePercent << "%)";
            }

            return oss.str();
        }

        void DrawFinancialTooltip(IRenderContext* ctx) {
            if (!showCandleTooltip || candleTooltipText.empty()) return;

            // Measure tooltip text (handle multi-line)
            std::vector<std::string> lines;
            std::istringstream iss(candleTooltipText);
            std::string line;
            float maxWidth = 0;
            float totalHeight = 0;
            float lineHeight = 14.0f;

            while (std::getline(iss, line)) {
                lines.push_back(line);
                auto textSize = ChartRenderingHelpers::MeasureText(ctx, line, "Arial", 11.0f);
                maxWidth = std::max(maxWidth, textSize.x);
                totalHeight += lineHeight;
            }

            // Calculate tooltip box
            float padding = 10.0f;
            float boxWidth = maxWidth + padding * 2;
            float boxHeight = totalHeight + padding * 2;

            // Position tooltip (avoid going off-screen)
            float tooltipX = tooltipPosition.x + 15;
            float tooltipY = tooltipPosition.y - boxHeight - 10;

            if (tooltipX + boxWidth > GetX() + GetWidth()) {
                tooltipX = tooltipPosition.x - boxWidth - 15;
            }
            if (tooltipY < GetY()) {
                tooltipY = tooltipPosition.y + 15;
            }

            // Draw tooltip background with financial styling
            ctx->SetFillColor(Color(250, 250, 250, 245)); // Nearly opaque white
            ctx->FillRoundedRectangle(tooltipX, tooltipY, boxWidth, boxHeight, 6.0f);

            // Draw tooltip border
            ctx->SetStrokeColor(Color(180, 180, 180));
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawRoundedRectangle(tooltipX, tooltipY, boxWidth, boxHeight, 6.0f);

            // Draw tooltip content
            ctx->SetFont("Arial", 11.0f);
            float currentY = tooltipY + padding + lineHeight;

            for (const auto& textLine : lines) {
                // Color-code certain lines
                if (textLine.find("Change:") != std::string::npos) {
                    if (textLine.find("+") != std::string::npos) {
                        ctx->SetTextColor(Color(0, 150, 0)); // Green for positive
                    } else if (textLine.find("-") != std::string::npos) {
                        ctx->SetTextColor(Color(200, 0, 0)); // Red for negative
                    } else {
                        ctx->SetTextColor(Color(100, 100, 100)); // Gray for neutral
                    }
                } else {
                    ctx->SetTextColor(Color(50, 50, 50)); // Dark gray for other text
                }

                ctx->DrawText(textLine, tooltipX + padding, currentY);
                currentY += lineHeight;
            }
        }

        // =============================================================================
        // MARKET SUMMARY DISPLAY
        // =============================================================================

        void DrawMarketSummary(IRenderContext* ctx) {
            auto summary = GetMarketSummary();
            if (summary.currentPrice == 0) return;

            // Draw market summary in top-left corner
            float summaryX = GetX() + 15;
            float summaryY = GetY() + 15;

            ctx->SetFont("Arial", 12.0f);

            // Current price with color coding
            std::string priceText = "Price: $" + FormatPrice(summary.currentPrice);
            if (summary.dayChangePercent > 0) {
                ctx->SetTextColor(Color(0, 150, 0)); // Green
            } else if (summary.dayChangePercent < 0) {
                ctx->SetTextColor(Color(200, 0, 0)); // Red
            } else {
                ctx->SetTextColor(Color(100, 100, 100)); // Gray
            }

            ctx->DrawText(priceText, summaryX, summaryY);

            // Change information
            summaryY += 18;
            std::string changeText = "Change: ";
            if (summary.dayChange >= 0) changeText += "+";
            changeText += FormatPrice(summary.dayChange) + " (" +
                          FormatPercent(summary.dayChangePercent) + ")";

            ctx->DrawText(changeText, summaryX, summaryY);

            // Volume
            summaryY += 18;
            ctx->SetTextColor(Color(80, 80, 80));
            std::string volumeText = "Volume: " + FormatVolume(summary.volume);
            ctx->DrawText(volumeText, summaryX, summaryY);

            // Trend indicator
            summaryY += 18;
            ctx->SetTextColor(Color(60, 60, 60));
            std::string trendText = "Trend: " + summary.trend;
            ctx->DrawText(trendText, summaryX, summaryY);
        }

        // =============================================================================
        // UTILITY FUNCTIONS
        // =============================================================================

        std::vector<FinancialChartRenderer::CandlestickData> LoadFinancialCSV(const std::string& filePath) {
            std::vector<FinancialChartRenderer::CandlestickData> data;
            std::ifstream file(filePath);

            if (!file.is_open()) return data;

            std::string line;
            bool firstLine = true;

            while (std::getline(file, line)) {
                if (firstLine) {
                    // Skip header if it looks like one
                    if (line.find("Date") != std::string::npos ||
                        line.find("timestamp") != std::string::npos) {
                        firstLine = false;
                        continue;
                    }
                    firstLine = false;
                }

                // Parse CSV line: timestamp,open,high,low,close,volume
                std::istringstream iss(line);
                std::string cell;
                std::vector<std::string> values;

                while (std::getline(iss, cell, ',')) {
                    values.push_back(cell);
                }

                if (values.size() >= 5) {
                    try {
                        double timestamp = std::stod(values[0]);
                        double open = std::stod(values[1]);
                        double high = std::stod(values[2]);
                        double low = std::stod(values[3]);
                        double close = std::stod(values[4]);
                        double volume = values.size() > 5 ? std::stod(values[5]) : 0.0;

                        data.emplace_back(timestamp, open, high, low, close, volume);
                    } catch (const std::exception&) {
                        // Skip invalid lines
                        continue;
                    }
                }
            }

            return data;
        }

        void RefreshIndicators() {
            // Clear and reload all indicators
            // This is a simplified implementation
            auto currentIndicators = enabledIndicators;
            financialRenderer.ClearIndicators();

            for (const auto& indicatorName : currentIndicators) {
                // Re-add indicator based on name pattern
                // In production, you'd store the full configuration
                if (indicatorName.find("SMA-20") != std::string::npos) {
                    AddTechnicalIndicator(FinancialChartRenderer::TechnicalIndicator::Type::SMA,
                                          {20}, 0xFF0066CC, "SMA-20");
                }
                // Add other indicator patterns...
            }
        }

        void ClearIndicators() {
            financialRenderer.ClearIndicators();
            enabledIndicators.clear();
            Invalidate();
        }

        std::string FormatPrice(double price) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << price;
            return oss.str();
        }

        std::string FormatPercent(double percent) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << percent << "%";
            return oss.str();
        }

        std::string FormatVolume(double volume) {
            if (volume >= 1000000000) {
                return FormatPrice(volume / 1000000000) + "B";
            } else if (volume >= 1000000) {
                return FormatPrice(volume / 1000000) + "M";
            } else if (volume >= 1000) {
                return FormatPrice(volume / 1000) + "K";
            } else {
                return std::to_string(static_cast<int>(volume));
            }
        }
    };

// =============================================================================
// FACTORY FUNCTIONS FOR FINANCIAL CHARTS
// =============================================================================

    std::unique_ptr<UltraCanvasFinancialChartElement> CreateFinancialChartElement(
            const std::string& id, long uid, int x, int y, int width, int height) {
        return std::make_unique<UltraCanvasFinancialChartElement>(id, uid, x, y, width, height);
    }

    std::unique_ptr<UltraCanvasFinancialChartElement> CreateStockChartFromCSV(
            const std::string& id, long uid, int x, int y, int width, int height,
            const std::string& csvFilePath, const std::string& symbol = "") {

        auto element = std::make_unique<UltraCanvasFinancialChartElement>(id, uid, x, y, width, height);

        element->LoadStockDataFromCSV(csvFilePath);

        if (!symbol.empty()) {
            element->SetTitle(symbol + " Stock Chart");
        }

        // Add standard trading indicators by default
        element->AddStandardTradingIndicators();

        return element;
    }

    std::unique_ptr<UltraCanvasFinancialChartElement> CreateProfessionalTradingChart(
            const std::string& id, long uid, int x, int y, int width, int height,
            const std::vector<FinancialChartRenderer::CandlestickData>& data,
            const std::string& symbol = "") {

        auto element = std::make_unique<UltraCanvasFinancialChartElement>(id, uid, x, y, width, height);

        element->LoadStockData(data);

        if (!symbol.empty()) {
            element->SetTitle(symbol + " Professional Analysis");
            element->SetSubtitle("Real-time Technical Analysis");
        }

        // Professional trading setup
        element->SetCandlestickStyle(0xFF00C851, 0xFFFF4444, 0xFF757575); // Material colors
        element->SetVolumeColors(0x8000C851, 0x80FF4444);

        // Add comprehensive indicator suite
        element->AddStandardTradingIndicators();

        return element;
    }

// =============================================================================
// INTEGRATION WITH EXISTING CHART SYSTEM
// =============================================================================

    namespace FinancialChartIntegration {

// Convert regular chart data to financial format
        std::vector<FinancialChartRenderer::CandlestickData> ConvertToFinancialData(
                std::shared_ptr<IChartDataSource> data) {

            std::vector<FinancialChartRenderer::CandlestickData> financialData;

            if (!data || data->GetPointCount() < 4) return financialData;

            // Assume data points represent: timestamp, open, high, low, close
            for (size_t i = 0; i + 3 < data->GetPointCount(); i += 4) {
                auto timestamp = data->GetPoint(i);
                auto open = data->GetPoint(i + 1);
                auto high = data->GetPoint(i + 2);
                auto low = data->GetPoint(i + 3);
                auto close = data->GetPoint(i + 4);

                financialData.emplace_back(timestamp.x, open.y, high.y, low.y, close.y, 0.0);
            }

            return financialData;
        }

// Register financial chart plugin
        class UltraCanvasFinancialChartPlugin : public IGraphicsPlugin {
        public:
            std::string GetPluginName() const override {
                return "UltraCanvas Financial Chart Plugin";
            }

            std::string GetPluginVersion() const override {
                return "1.0.1";
            }

            std::vector<std::string> GetSupportedExtensions() const override {
                return {"csv", "stock", "financial", "ohlc"};
            }

            bool CanHandle(const std::string& filePath) const override {
                auto extensions = GetSupportedExtensions();
                std::string ext = GetFileExtension(filePath);
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end()) {
                    // Additional check for CSV files - look for financial data patterns
                    if (ext == "csv") {
                        return ContainsFinancialData(filePath);
                    }
                    return true;
                }
                return false;
            }

            std::shared_ptr<UltraCanvasElement> LoadGraphics(const std::string& filePath) override {
                std::string symbol = ExtractSymbolFromFilename(filePath);
                auto element = CreateStockChartFromCSV("financial_chart",
                                                       GenerateUID(), 0, 0, 800, 600, filePath, symbol);

                return std::static_pointer_cast<UltraCanvasElement>(element);
            }

        private:
            bool ContainsFinancialData(const std::string& filePath) {
                std::ifstream file(filePath);
                if (!file.is_open()) return false;

                std::string firstLine;
                if (std::getline(file, firstLine)) {
                    // Look for common financial CSV headers
                    std::transform(firstLine.begin(), firstLine.end(), firstLine.begin(), ::tolower);
                    return firstLine.find("open") != std::string::npos &&
                           firstLine.find("high") != std::string::npos &&
                           firstLine.find("low") != std::string::npos &&
                           firstLine.find("close") != std::string::npos;
                }
                return false;
            }

            std::string ExtractSymbolFromFilename(const std::string& filePath) {
                size_t lastSlash = filePath.find_last_of("/\\");
                size_t lastDot = filePath.find_last_of(".");

                if (lastSlash == std::string::npos) lastSlash = 0;
                else lastSlash++;

                if (lastDot == std::string::npos) lastDot = filePath.length();

                std::string filename = filePath.substr(lastSlash, lastDot - lastSlash);

                // Look for stock symbols (usually 3-5 uppercase letters)
                std::regex symbolRegex("[A-Z]{2,5}");
                std::smatch match;
                if (std::regex_search(filename, match, symbolRegex)) {
                    return match.str();
                }

                return filename;
            }

            std::string GetFileExtension(const std::string& filePath) {
                size_t dotPos = filePath.find_last_of('.');
                if (dotPos == std::string::npos) return "";
                return filePath.substr(dotPos + 1);
            }

            long GenerateUID() {
                static long counter = 10000;
                return ++counter;
            }
        };

        bool RegisterFinancialChartPlugin() {
            auto plugin = std::make_shared<UltraCanvasFinancialChartPlugin>();
            // UltraCanvasGraphicsPluginRegistry::RegisterPlugin(plugin);
            return true;
        }

    } // namespace FinancialChartIntegration

} // namespace UltraCanvas

// Apps/UltraCanvasFinancialChartExample.cpp
// Example usage of UltraCanvasFinancialChart element
// Version: 1.0.0
// Last Modified: 2025-09-14
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasFinancialChart.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasLabel.h"
#include <random>
#include <iomanip>

using namespace UltraCanvas;

class FinancialChartExampleApp : public UltraCanvasApplication {
private:
    std::shared_ptr<UltraCanvasWindow> window;
    std::shared_ptr<UltraCanvasFinancialChartElement> stockChart;
    std::shared_ptr<UltraCanvasLabel> instructionLabel;
    std::shared_ptr<FinancialChartDataVector> stockData;

public:
    bool Initialize() override {
        UltraCanvasApplication::Initialize();
        // Create main window
        WindowConfig config;
        config.title = "UltraCanvas Financial Chart Example";
        config.width = 1500;
        config.height = 800;

        window = std::make_shared<UltraCanvasWindow>(config);
        if (!window) {
            return false;
        }
        RegisterGlobalEventHandler([this](const UCEvent& ev) {
            OnAppKeyPress(ev);
            return false;
        });
        // Generate sample financial data
        CreateSampleStockData();

        // Create financial chart
        CreateFinancialChart();

        // Create instruction label
        CreateInstructionLabels();

        // Add elements to window
        window->AddChild(stockChart);
        window->AddChild(instructionLabel);
        window->Show();

        return true;
    }

    void CreateSampleStockData() {
        stockData = std::make_shared<FinancialChartDataVector>();

        // Generate realistic stock data with trend and volatility
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> priceDist(-2.0, 3.0);  // Price movement
        std::uniform_real_distribution<> volDist(0.5, 2.0);     // Volume multiplier

        std::vector<FinancialChartDataPoint> stockPoints;
        double basePrice = 100.0;  // Starting stock price
        double baseVolume = 1000000; // Base volume

        // Generate 60 trading days of data
        for (int day = 1; day <= 60; ++day) {
            // Calculate daily price movement
            double priceMovement = priceDist(gen);
            basePrice += priceMovement;
            basePrice = std::max(basePrice, 50.0); // Don't go below $50

            // Generate OHLC with realistic intraday movement
            double open = basePrice;
            double close = basePrice + priceMovement;

            // High and low based on volatility
            double volatility = std::abs(priceMovement) + 1.0;
            double high = std::max(open, close) + (volatility * 0.5);
            double low = std::min(open, close) - (volatility * 0.3);

            // Volume varies with price movement (higher volume on big moves)
            double volume = baseVolume * (1.0 + std::abs(priceMovement) * 0.2) * volDist(gen);

            // Generate date string
            std::string dateStr = "2024-" +
                                  std::to_string((day - 1) / 30 + 1) + "-" +
                                  std::to_string((day - 1) % 30 + 1);

            stockPoints.emplace_back(day, open, high, low, close, volume, dateStr);

            basePrice = close; // Update base price for next day
        }

        stockData->LoadFinancialData(stockPoints);
    }

    void CreateFinancialChart() {
        // Create financial chart with custom styling
        stockChart = CreateFinancialChartElement("stockChart", 1001, 50, 50, 1000, 600);

        stockChart->SetBullishCandleColor(Color(0, 180, 0, 255));
        stockChart->SetBearishCandleColor(Color(220, 0, 0, 255));
        stockChart->SetShowVolumePanel(true);
        // Configure the chart
        stockChart->SetFinancialDataSource(stockData);
        stockChart->SetChartTitle("ACME Corp (ACME) - 60 Day Chart");

        // Set candlestick style
        stockChart->SetCandleDisplayStyle(UltraCanvasFinancialChartElement::CandleDisplayStyle::Candlestick);

        // Configure display options
        stockChart->SetShowVolumePanel(true);
        stockChart->SetVolumePanelHeightRatio(0.25f);  // 25% of chart height for volume
        stockChart->SetCandleWidthRatio(0.8f);         // 80% candle width

        // Enable moving average
        stockChart->SetShowMovingAverage(true, 20); // 20-day moving average
        stockChart->SetMovingAverageColor(Color(0, 0, 200, 255)); // Blue MA line

        // Configure interactivity
        stockChart->SetEnableTooltips(true);
        stockChart->SetEnableZoom(true);
        stockChart->SetEnablePan(true);

        // Set custom colors
        stockChart->SetWickLineColor(Color(100, 100, 100, 255));
        stockChart->SetVolumeBarColor(Color(150, 150, 150, 100));
    }

    void CreateInstructionLabels() {
        std::string instructions =
                "Financial Chart Controls:\n"
                "• Mouse over candles to see OHLC data and volume\n"
                "• Drag to pan the chart view\n"
                "• Mouse wheel to zoom in/out\n"
                "• S - Switch between Candlestick/OHLC/Heikin-Ashi\n"
                "        styles\n"
                "• V - Toggle volume display\n"
                "• M - Toggle moving average (20-day)\n"
                "• R - Reset zoom and pan\n"
                "• G - Toggle grid display\n\n"
                "Chart Features:\n"
                "• Green candles: Close > Open (bullish)\n"
                "• Red candles: Close < Open (bearish)\n"
                "• Blue line: 20-day moving average\n"
                "• Bottom panel: Volume bars";


        instructionLabel = CreateLabel(
                "instructions", 2000, 1070, 50, 400, 330, instructions
        );

        instructionLabel->SetFont("Arial", 12.0f, FontWeight::Normal);
        instructionLabel->SetTextColor(Color(60, 60, 60, 255));
        instructionLabel->SetAlignment(LabelAlignment::Left, LabelVerticalAlignment::Top);
        instructionLabel->SetBackgroundColor(Color(250, 250, 250, 200));
        instructionLabel->SetPadding(10.0f, 10.0f, 10.0f, 10.0f);
    }

    void OnAppKeyPress(const UCEvent& event) {
        if (event.type != UCEventType::KeyDown) return;
        switch (event.virtualKey) {
            case UCKeys::S: {
                // Cycle through candle styles
                static int styleIndex = 0;
                const UltraCanvasFinancialChartElement::CandleDisplayStyle styles[] = {
                        UltraCanvasFinancialChartElement::CandleDisplayStyle::Candlestick,
                        UltraCanvasFinancialChartElement::CandleDisplayStyle::OHLCBars,
                        UltraCanvasFinancialChartElement::CandleDisplayStyle::HeikinAshi
                };
                const std::string styleNames[] = { "Candlestick", "OHLC", "Heikin-Ashi" };

                styleIndex = (styleIndex + 1) % 3;
                stockChart->SetCandleDisplayStyle(styles[styleIndex]);
                std::cout << "Switched to " << styleNames[styleIndex] << " style" << std::endl;
                break;
            }

            case UCKeys::V: {
                // Toggle volume display
                static bool volumeVisible = true;
                volumeVisible = !volumeVisible;
                stockChart->SetShowVolumePanel(volumeVisible);
                std::cout << "Volume display: " << (volumeVisible ? "enabled" : "disabled") << std::endl;
                break;
            }

            case UCKeys::M: {
                // Toggle moving average
                static bool maVisible = true;
                maVisible = !maVisible;
                stockChart->SetShowMovingAverage(maVisible, 20);
                std::cout << "Moving average: " << (maVisible ? "enabled" : "disabled") << std::endl;
                break;
            }

            case UCKeys::R: {
                // Reset zoom and pan (would need to implement in base class)
                std::cout << "Chart view reset" << std::endl;
                break;
            }

            case UCKeys::G: {
                // Toggle grid (would access through base class method)
                std::cout << "Grid display toggled" << std::endl;
                static bool gridEnabled = true;
                gridEnabled = !gridEnabled;
                stockChart->SetGridEnabled(gridEnabled);
                break;
            }
        }
    }

    void RunInEventLoop() override {
        // Could add real-time data updates here
        UltraCanvasApplication::RunInEventLoop();

        static int updateCounter = 0;
        updateCounter++;

        // Every 5 seconds (at 60fps), add a new data point
        if (updateCounter % 300 == 0) {
            AddNewTradingDay();
        }
    }

    void AddNewTradingDay() {
        // Simulate adding a new trading day
        if (!stockData || stockData->GetPointCount() == 0) return;

        // Get the last data point
        auto lastPoint = stockData->GetFinancialPoint(stockData->GetPointCount() - 1);

        // Generate new price movement
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> movement(-3.0, 3.0);

        double priceChange = movement(gen);
        double newOpen = lastPoint.close;
        double newClose = newOpen + priceChange;
        double newHigh = std::max(newOpen, newClose) + std::abs(priceChange) * 0.3;
        double newLow = std::min(newOpen, newClose) - std::abs(priceChange) * 0.2;

        std::uniform_real_distribution<> volumeDist(500000, 2000000);
        double newVolume = volumeDist(gen);

        // Generate next date
        int nextDay = static_cast<int>(stockData->GetPointCount()) + 1;
        std::string newDate = "2024-" +
                              std::to_string((nextDay - 1) / 30 + 1) + "-" +
                              std::to_string((nextDay - 1) % 30 + 1);

        // Add new point
        FinancialChartDataPoint newPoint(nextDay, newOpen, newHigh, newLow, newClose, newVolume, newDate);
        stockData->AddFinancialPoint(newPoint);

        // Refresh chart
        stockChart->RequestRedraw();

        std::cout << "Added new trading day: " << newDate
                  << " Close: $" << std::fixed << std::setprecision(2) << newClose << std::endl;
    }
};

// Application entry point
int main() {
    FinancialChartExampleApp app;

    if (!app.Initialize()) {
        std::cerr << "Failed to initialize Financial Chart Example" << std::endl;
        return -1;
    }

    std::cout << "Financial Chart Example initialized successfully!" << std::endl;
    std::cout << "Use keyboard controls to interact with the chart." << std::endl;

    app.Run();

    return 0;
}
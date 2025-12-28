// Apps/DemoApp/UltraCanvasFinancialChartExamples.cpp
// Financial chart component example creators with interactive controls
// Version: 1.0.0
// Last Modified: 2025-01-14
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/UltraCanvasFinancialChart.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include <random>
#include <sstream>
#include <iomanip>

namespace UltraCanvas {
    void CreateFinancialChartControlPanel(
            std::shared_ptr<UltraCanvasContainer> container,
            std::shared_ptr<UltraCanvasFinancialChartElement> chart,
            int x, int y);

    std::shared_ptr<FinancialChartDataVector> GenerateSampleStockData(
            const std::string& symbol, int days);

    std::shared_ptr<FinancialChartDataVector> GenerateSampleCryptoData(
            const std::string& pair, int days);

    std::shared_ptr<FinancialChartDataVector> GenerateSampleForexData(
            const std::string& pair, int days);

// ===== FINANCIAL CHART CONTROLS STATE =====
    static struct FinancialChartControls {
        UltraCanvasFinancialChartElement::CandleDisplayStyle candleStyle =
                UltraCanvasFinancialChartElement::CandleDisplayStyle::Candlestick;
        bool showVolumePanel = true;
        bool showMovingAverage = false;
        int movingAveragePeriod = 20;
        float candleWidthRatio = 0.8f;
        float volumePanelHeightRatio = 0.25f;
        bool showGrid = true;
        bool enableTooltips = true;
        bool enableZoom = true;
        bool enablePan = true;
    } financialChartControls;

// ===== FINANCIAL CHART EXAMPLES =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateFinancialChartExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("FinancialChartContainer", 1000, 0, 0, 1000, 780);
        container->SetPadding(0,0,10,0);

        // Create title label
        auto titleLabel = std::make_shared<UltraCanvasLabel>("TitleLabel", 1001, 20, 10, 960, 30);
        titleLabel->SetText("Financial Chart Components - Stock Market Data Visualization");
        titleLabel->SetFontSize(18);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetAlignment(TextAlignment::Center);
        titleLabel->SetBackgroundColor(Color(240, 240, 240, 255));
        container->AddChild(titleLabel);

        // ===== CREATE SAMPLE STOCK DATA =====
        auto stockData = GenerateSampleStockData("ULTR", 90);  // 90 days of stock data
        auto cryptoData = GenerateSampleCryptoData("BTC/USD", 60);  // 60 days of crypto data
        auto forexData = GenerateSampleForexData("EUR/USD", 30);  // 30 days of forex data

        // ===== MAIN FINANCIAL CHART (Stock Market) =====
        auto stockChart = CreateFinancialChartElement("StockChart", 1002, 20, 50, 980, 400);
        stockChart->SetFinancialDataSource(stockData);
        stockChart->SetChartTitle("FCHI - Fantasy Chart Inc. (90 Day Chart)");
        stockChart->SetCandleDisplayStyle(financialChartControls.candleStyle);
        stockChart->SetShowVolumePanel(true);
        stockChart->SetVolumePanelHeightRatio(0.25f);
        stockChart->SetShowMovingAverage(true, 20);
        stockChart->SetMovingAverageColor(Color(0, 100, 200, 255));
        stockChart->SetBullishCandleColor(Color(0, 180, 0, 255));
        stockChart->SetBearishCandleColor(Color(220, 0, 0, 255));
        stockChart->SetEnableTooltips(true);
        stockChart->SetEnableZoom(true);
        stockChart->SetEnablePan(true);
        container->AddChild(stockChart);

        // ===== CRYPTOCURRENCY CHART =====
//        auto cryptoChart = CreateFinancialChartElement("CryptoChart", 1003, 720, 50, 260, 200);
//        cryptoChart->SetFinancialDataSource(cryptoData);
//        cryptoChart->SetChartTitle("BTC/USD - Bitcoin");
//        cryptoChart->SetCandleDisplayStyle(UltraCanvasFinancialChartElement::CandleDisplayStyle::Candlestick);
//        cryptoChart->SetShowVolumePanel(false);  // Hide volume for smaller chart
//        cryptoChart->SetBullishCandleColor(Color(255, 153, 0, 255));  // Bitcoin orange
//        cryptoChart->SetBearishCandleColor(Color(100, 100, 100, 255));
//        container->AddChild(cryptoChart);

        // ===== FOREX CHART =====
//        auto forexChart = CreateFinancialChartElement("ForexChart", 1004, 720, 260, 260, 190);
//        forexChart->SetFinancialDataSource(forexData);
//        forexChart->SetChartTitle("EUR/USD - Forex");
//        forexChart->SetCandleDisplayStyle(UltraCanvasFinancialChartElement::CandleDisplayStyle::OHLCBars);
//        forexChart->SetShowVolumePanel(false);
//        forexChart->SetBullishCandleColor(Color(0, 50, 150, 255));
//        forexChart->SetBearishCandleColor(Color(150, 50, 0, 255));
//        container->AddChild(forexChart);

        // ===== CONTROL PANEL =====
        CreateFinancialChartControlPanel(container, stockChart, 20, 470);

        // ===== INFORMATION PANEL =====
        auto infoPanel = std::make_shared<UltraCanvasContainer>("InfoPanel", 1020, 720, 470, 260, 280);
        infoPanel->SetBackgroundColor(Color(250, 250, 250, 255));
        infoPanel->SetBorders(1, Color(200, 200, 200, 255));

        // Add info labels
        auto infoTitle = std::make_shared<UltraCanvasLabel>("InfoTitle", 1021, 10, 10, 240, 25);
        infoTitle->SetText("Financial Chart Features:");
        infoTitle->SetFontWeight(FontWeight::Bold);
        infoPanel->AddChild(infoTitle);

        std::vector<std::string> features = {
                "• Candlestick, OHLC & Heikin-Ashi styles",
                "• Volume panel with bars",
                "• Moving averages (configurable period)",
                "• Interactive tooltips with OHLC data",
                "• Zoom and pan navigation",
                "• Real-time data updates support",
                "• Custom color schemes",
                "• Multiple timeframes",
                "• Technical indicators (planned)",
                "• Export to PNG/PDF (planned)"
        };

        int yPos = 40;
        for (size_t i = 0; i < features.size(); i++) {
            auto featureLabel = std::make_shared<UltraCanvasLabel>(
                    "Feature" + std::to_string(i), 1030 + i, 10, yPos, 240, 20
            );
            featureLabel->SetText(features[i]);
            featureLabel->SetFontSize(11);
            infoPanel->AddChild(featureLabel);
            yPos += 22;
        }

        container->AddChild(infoPanel);

        return container;
    }

// ===== CONTROL PANEL CREATION =====
    void CreateFinancialChartControlPanel(
            std::shared_ptr<UltraCanvasContainer> container,
            std::shared_ptr<UltraCanvasFinancialChartElement> chart,
            int x, int y) {

        // Create control panel container
        auto controlPanel = std::make_shared<UltraCanvasContainer>("ControlPanel", 1050, x, y, 680, 280);
        controlPanel->SetBackgroundColor(Color(245, 245, 245, 255));
        controlPanel->SetBorders(1, Color(200, 200, 200, 255));

        // Panel title
        auto panelTitle = std::make_shared<UltraCanvasLabel>("PanelTitle", 1051, 10, 10, 660, 25);
        panelTitle->SetText("Chart Controls");
        panelTitle->SetFontWeight(FontWeight::Bold);
        panelTitle->SetAlignment(TextAlignment::Center);
        controlPanel->AddChild(panelTitle);

        // ===== STYLE SELECTION DROPDOWN =====
        auto styleLabel = std::make_shared<UltraCanvasLabel>("StyleLabel", 1052, 20, 45, 100, 25);
        styleLabel->SetText("Chart Style:");
        controlPanel->AddChild(styleLabel);

        auto styleDropdown = std::make_shared<UltraCanvasDropdown>("StyleDropdown", 1053, 130, 45, 150, 30);
        styleDropdown->AddItem("Candlestick");
        styleDropdown->AddItem("OHLC Bars");
        styleDropdown->AddItem("Heikin-Ashi");
        styleDropdown->SetSelectedIndex(0);
        styleDropdown->onSelectionChanged = [chart](int index, const DropdownItem&) {
            switch (index) {
                case 0:
                    chart->SetCandleDisplayStyle(UltraCanvasFinancialChartElement::CandleDisplayStyle::Candlestick);
                    break;
                case 1:
                    chart->SetCandleDisplayStyle(UltraCanvasFinancialChartElement::CandleDisplayStyle::OHLCBars);
                    break;
                case 2:
                    chart->SetCandleDisplayStyle(UltraCanvasFinancialChartElement::CandleDisplayStyle::HeikinAshi);
                    break;
            }
            chart->RequestRedraw();
        };
        controlPanel->AddChild(styleDropdown);

        // ===== VOLUME PANEL TOGGLE =====
        auto volumeCheckbox = std::make_shared<UltraCanvasCheckbox>("VolumeCheckbox", 1054, 300, 45, 150, 30);
        volumeCheckbox->SetText("Show Volume");
        volumeCheckbox->SetChecked(true);
        volumeCheckbox->onStateChanged = [chart](CheckboxState oldState, CheckboxState newState) {
            chart->SetShowVolumePanel(newState == CheckboxState::Checked);
            chart->RequestRedraw();
        };
        controlPanel->AddChild(volumeCheckbox);

        // ===== MOVING AVERAGE TOGGLE =====
        auto maCheckbox = std::make_shared<UltraCanvasCheckbox>("MACheckbox", 1055, 470, 45, 180, 30);
        maCheckbox->SetText("Moving Average (20)");
        maCheckbox->SetChecked(true);
        maCheckbox->onStateChanged = [chart](CheckboxState oldState, CheckboxState newState) {
            chart->SetShowMovingAverage(newState == CheckboxState::Checked, 20);
            chart->RequestRedraw();
        };
        controlPanel->AddChild(maCheckbox);

        // ===== CANDLE WIDTH SLIDER =====
        auto widthLabel = std::make_shared<UltraCanvasLabel>("WidthLabel", 1056, 20, 85, 100, 25);
        widthLabel->SetText("Candle Width:");
        controlPanel->AddChild(widthLabel);

        auto widthSlider = std::make_shared<UltraCanvasSlider>("WidthSlider", 1057, 130, 85, 200, 30);
        widthSlider->SetRange(0.3f, 1.0f);
        widthSlider->SetValue(0.8f);
        widthSlider->onValueChanged = [chart, widthLabel](float value) {
            chart->SetCandleWidthRatio(value);
            chart->RequestRedraw();
            std::ostringstream oss;
            oss << "Candle Width: " << std::fixed << std::setprecision(1) << (value * 100) << "%";
            widthLabel->SetText(oss.str());
        };
        controlPanel->AddChild(widthSlider);

        // ===== GRID TOGGLE =====
        auto gridCheckbox = std::make_shared<UltraCanvasCheckbox>("GridCheckbox", 1058, 350, 85, 100, 30);
        gridCheckbox->SetText("Show Grid");
        gridCheckbox->SetChecked(true);
        gridCheckbox->onStateChanged = [chart](CheckboxState oldState, CheckboxState newState) {
            chart->SetShowGrid(newState == CheckboxState::Checked);
            chart->RequestRedraw();
        };
        controlPanel->AddChild(gridCheckbox);

        // ===== TOOLTIPS TOGGLE =====
        auto tooltipsCheckbox = std::make_shared<UltraCanvasCheckbox>("TooltipsCheckbox", 1059, 470, 85, 150, 30);
        tooltipsCheckbox->SetText("Show Tooltips");
        tooltipsCheckbox->SetChecked(true);
        tooltipsCheckbox->onStateChanged = [chart](CheckboxState oldState, CheckboxState newState) {
            chart->SetEnableTooltips(newState == CheckboxState::Checked);
            chart->RequestRedraw();
        };
        controlPanel->AddChild(tooltipsCheckbox);

        // ===== TIME PERIOD BUTTONS =====
        auto periodLabel = std::make_shared<UltraCanvasLabel>("PeriodLabel", 1060, 20, 125, 100, 25);
        periodLabel->SetText("Time Period:");
        controlPanel->AddChild(periodLabel);

        std::vector<std::pair<std::string, int>> periods = {
                {"1W", 7}, {"1M", 30}, {"3M", 90}, {"6M", 180}, {"1Y", 365}
        };

        int btnX = 130;
        for (size_t i = 0; i < periods.size(); i++) {
            auto periodBtn = std::make_shared<UltraCanvasButton>(
                    "Period" + periods[i].first, 1070 + i, btnX, 125, 60, 30
            );
            periodBtn->SetText(periods[i].first);

            // Capture by value to avoid reference issues
            auto period = periods[i];
            periodBtn->SetOnClick([chart, period]() {
                // In real app, this would filter the data to show only the specified period
                std::cout << "Switching to " << period.first << " view (" << period.second << " days)" << std::endl;
                chart->RequestRedraw();
            });
            controlPanel->AddChild(periodBtn);
            btnX += 70;
        }

        // ===== ACTION BUTTONS =====
        auto resetBtn = std::make_shared<UltraCanvasButton>("ResetBtn", 1080, 20, 165, 100, 35);
        resetBtn->SetText("Reset View");
        resetBtn->SetOnClick([chart]() {
//            chart->ResetZoomAndPan();
            chart->RequestRedraw();
        });
        controlPanel->AddChild(resetBtn);

        auto exportBtn = std::make_shared<UltraCanvasButton>("ExportBtn", 1081, 130, 165, 100, 35);
        exportBtn->SetText("Export PNG");
        exportBtn->SetOnClick([chart]() {
            std::cout << "Exporting chart to PNG..." << std::endl;
            // In real app, this would export the chart to a PNG file
        });
        controlPanel->AddChild(exportBtn);

        auto dataBtn = std::make_shared<UltraCanvasButton>("DataBtn", 1082, 240, 165, 100, 35);
        dataBtn->SetText("Load CSV");
        dataBtn->SetOnClick([chart]() {
            std::cout << "Loading data from CSV..." << std::endl;
            // In real app, this would open a file dialog to load CSV data
        });
        controlPanel->AddChild(dataBtn);

        // ===== COLOR CUSTOMIZATION =====
        auto colorLabel = std::make_shared<UltraCanvasLabel>("ColorLabel", 1090, 20, 210, 100, 25);
        colorLabel->SetText("Color Scheme:");
        controlPanel->AddChild(colorLabel);

        auto greenRedBtn = std::make_shared<UltraCanvasButton>("GreenRedBtn", 1091, 130, 210, 80, 30);
        greenRedBtn->SetText("Green/Red");
        greenRedBtn->SetOnClick([chart]() {
            chart->SetBullishCandleColor(Color(0, 180, 0, 255));
            chart->SetBearishCandleColor(Color(220, 0, 0, 255));
            chart->RequestRedraw();
        });
        controlPanel->AddChild(greenRedBtn);

        auto blueOrangeBtn = std::make_shared<UltraCanvasButton>("BlueOrangeBtn", 1092, 220, 210, 90, 30);
        blueOrangeBtn->SetText("Blue/Orange");
        blueOrangeBtn->SetOnClick([chart]() {
            chart->SetBullishCandleColor(Color(0, 120, 200, 255));
            chart->SetBearishCandleColor(Color(255, 140, 0, 255));
            chart->RequestRedraw();
        });
        controlPanel->AddChild(blueOrangeBtn);

        auto monochrome = std::make_shared<UltraCanvasButton>("MonochromeBtn", 1093, 320, 210, 90, 30);
        monochrome->SetText("Monochrome");
        monochrome->SetOnClick([chart]() {
            chart->SetBullishCandleColor(Color(255, 255, 255, 255));
            chart->SetBearishCandleColor(Color(80, 80, 80, 255));
            chart->RequestRedraw();
        });
        controlPanel->AddChild(monochrome);

        // ===== REAL-TIME UPDATE SIMULATION =====
        auto updateLabel = std::make_shared<UltraCanvasLabel>("UpdateLabel", 1094, 20, 250, 150, 25);
        updateLabel->SetText("Live Updates:");
        controlPanel->AddChild(updateLabel);

        auto liveToggle = std::make_shared<UltraCanvasCheckbox>("LiveToggle", 1095, 130, 250, 150, 25);
        liveToggle->SetText("Simulate Live Data");
        liveToggle->SetChecked(false);
        liveToggle->onStateChanged = [chart](CheckboxState oldState, CheckboxState newState) {
            if (newState == CheckboxState::Checked) {
                std::cout << "Starting live data simulation..." << std::endl;
                // In real app, this would start a timer to update data
            } else {
                std::cout << "Stopping live data simulation..." << std::endl;
            }
        };
        controlPanel->AddChild(liveToggle);

        container->AddChild(controlPanel);
    }

// ===== DATA GENERATION FUNCTIONS =====
    std::shared_ptr<FinancialChartDataVector> GenerateSampleStockData(
            const std::string& symbol, int days) {

        auto data = std::make_shared<FinancialChartDataVector>();

        // Random number generation for realistic stock data
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> volatility(-2.0, 2.0);
        std::uniform_real_distribution<> volumeDist(1000000, 5000000);

        double basePrice = 100.0;
        double trendFactor = 0.15;  // Slight upward trend

        for (int day = 0; day < days; day++) {
            // Calculate base price with trend
            basePrice += (trendFactor + volatility(gen));

            // Generate OHLC data with realistic relationships
            double open = basePrice + volatility(gen);
            double close = open + volatility(gen) * 1.5;
            double high = std::max(open, close) + std::abs(volatility(gen)) * 0.5;
            double low = std::min(open, close) - std::abs(volatility(gen)) * 0.5;
            double volume = volumeDist(gen);

            // Generate date string
            std::ostringstream dateStr;
            dateStr << "2024-" << std::setfill('0') << std::setw(2)
                    << (day / 30) + 1 << "-" << std::setw(2) << (day % 30) + 1;

            FinancialChartDataPoint point(day, open, high, low, close, volume, dateStr.str());
            data->AddFinancialPoint(point);
        }

        return data;
    }

    std::shared_ptr<FinancialChartDataVector> GenerateSampleCryptoData(
            const std::string& pair, int days) {

        auto data = std::make_shared<FinancialChartDataVector>();

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> volatility(-5.0, 5.0);  // Higher volatility for crypto
        std::uniform_real_distribution<> volumeDist(10000000, 100000000);

        double basePrice = 50000.0;  // Bitcoin-like price

        for (int day = 0; day < days; day++) {
            basePrice += volatility(gen) * 100;  // Larger price movements

            double open = basePrice + volatility(gen) * 50;
            double close = open + volatility(gen) * 100;
            double high = std::max(open, close) + std::abs(volatility(gen)) * 50;
            double low = std::min(open, close) - std::abs(volatility(gen)) * 50;
            double volume = volumeDist(gen);

            std::ostringstream dateStr;
            dateStr << "2024-" << std::setfill('0') << std::setw(2)
                    << (day / 30) + 9 << "-" << std::setw(2) << (day % 30) + 1;

            FinancialChartDataPoint point(day, open, high, low, close, volume, dateStr.str());
            data->AddFinancialPoint(point);
        }

        return data;
    }

    std::shared_ptr<FinancialChartDataVector> GenerateSampleForexData(
            const std::string& pair, int days) {

        auto data = std::make_shared<FinancialChartDataVector>();

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> volatility(-0.005, 0.005);  // Small movements for forex
        std::uniform_real_distribution<> volumeDist(100000000, 500000000);

        double basePrice = 1.0850;  // EUR/USD-like price

        for (int day = 0; day < days; day++) {
            basePrice += volatility(gen);

            double open = basePrice + volatility(gen);
            double close = open + volatility(gen);
            double high = std::max(open, close) + std::abs(volatility(gen)) * 0.5;
            double low = std::min(open, close) - std::abs(volatility(gen)) * 0.5;
            double volume = volumeDist(gen);

            std::ostringstream dateStr;
            dateStr << "2024-" << std::setfill('0') << std::setw(2)
                    << 11 << "-" << std::setw(2) << (day % 30) + 1;

            FinancialChartDataPoint point(day, open, high, low, close, volume, dateStr.str());
            data->AddFinancialPoint(point);
        }

        return data;
    }

} // namespace UltraCanvas
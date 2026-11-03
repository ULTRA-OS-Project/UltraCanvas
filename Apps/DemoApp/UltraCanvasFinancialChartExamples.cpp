// Apps/DemoApp/UltraCanvasFinancialChartExamples.cpp
// Financial chart component example creators with interactive controls
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the chart in a
//                  card as wide as the page, the controls and the feature list
//                  as cards beside each other, the controls in rows that wrap
//                  and every label sized to its text
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Charts/UltraCanvasFinancialChart.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include <random>
#include <sstream>
#include <iomanip>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {
    void CreateFinancialChartControlPanel(
            std::shared_ptr<UltraCanvasContainer> container,
            std::shared_ptr<UltraCanvasFinancialChartElement> chart);

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
        auto container = DemoKit::CreatePage("FinancialChartContainer",
                "Candlestick Chart Components - Stock Market Data Visualization",
                "Stock market OHLC and candlestick charts");

        // ===== CREATE SAMPLE STOCK DATA =====
        auto stockData = GenerateSampleStockData("ULTR", 90);  // 90 days of stock data
        auto cryptoData = GenerateSampleCryptoData("BTC/USD", 60);  // 60 days of crypto data
        auto forexData = GenerateSampleForexData("EUR/USD", 30);  // 30 days of forex data

        // ===== MAIN FINANCIAL CHART (Stock Market) =====
        auto chartCard = DemoKit::AddCard(container, "StockChartCard");
        auto stockChart = CreateFinancialChartElement("StockChart", 0, 0, 980, 400);
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
        DemoKit::AddStretched(chartCard, stockChart);

        // ===== CRYPTOCURRENCY CHART =====
//        auto cryptoChart = CreateFinancialChartElement("CryptoChart", 720, 50, 260, 200);
//        cryptoChart->SetFinancialDataSource(cryptoData);
//        cryptoChart->SetChartTitle("BTC/USD - Bitcoin");
//        cryptoChart->SetCandleDisplayStyle(UltraCanvasFinancialChartElement::CandleDisplayStyle::Candlestick);
//        cryptoChart->SetShowVolumePanel(false);  // Hide volume for smaller chart
//        cryptoChart->SetBullishCandleColor(Color(255, 153, 0, 255));  // Bitcoin orange
//        cryptoChart->SetBearishCandleColor(Color(100, 100, 100, 255));
//        container->AddChild(cryptoChart);

        // ===== FOREX CHART =====
//        auto forexChart = CreateFinancialChartElement("ForexChart", 720, 260, 260, 190);
//        forexChart->SetFinancialDataSource(forexData);
//        forexChart->SetChartTitle("EUR/USD - Forex");
//        forexChart->SetCandleDisplayStyle(UltraCanvasFinancialChartElement::CandleDisplayStyle::OHLCBars);
//        forexChart->SetShowVolumePanel(false);
//        forexChart->SetBullishCandleColor(Color(0, 50, 150, 255));
//        forexChart->SetBearishCandleColor(Color(150, 50, 0, 255));
//        container->AddChild(forexChart);

        // ===== CONTROL PANEL + INFORMATION PANEL, side by side =====
        auto panels = DemoKit::AddRow(container, "FinancialChartPanels");
        CreateFinancialChartControlPanel(panels, stockChart);

        DemoKit::AddNotes(panels, "InfoPanel", "Financial Chart Features", {
                "Candlestick, OHLC & Heikin-Ashi styles",
                "Volume panel with bars",
                "Moving averages (configurable period)",
                "Interactive tooltips with OHLC data",
                "Zoom and pan navigation",
                "Real-time data updates support",
                "Custom color schemes",
                "Multiple timeframes",
                "Technical indicators (planned)",
                "Export to PNG/PDF (planned)"});

        return container;
    }

// ===== CONTROL PANEL CREATION =====
    void CreateFinancialChartControlPanel(
            std::shared_ptr<UltraCanvasContainer> container,
            std::shared_ptr<UltraCanvasFinancialChartElement> chart) {

        // The control panel: a card in the given row, one row of controls
        // per line of the old panel; each row wraps when the card is narrow.
        auto controlPanel = DemoKit::AddCard(container, "ControlPanel", "Chart Controls", "", 560);
        auto addControlRow = [&controlPanel](const std::string& id) {
            auto row = DemoKit::AddRow(controlPanel, id, 10);
            row->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
            return row;
        };
        auto addRowLabel = [](const std::shared_ptr<UltraCanvasContainer>& row,
                              const std::string& id, const std::string& text) {
            auto label = DemoKit::CreateText(id, text);
            label->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
            DemoKit::Add(row, label);
            return label;
        };

        // ===== STYLE SELECTION DROPDOWN =====
        auto styleRow = addControlRow("StyleRow");
        addRowLabel(styleRow, "StyleLabel", "Chart Style:");

        auto styleDropdown = std::make_shared<UltraCanvasDropdown>("StyleDropdown", 0, 0, 150, 30);
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
        DemoKit::AddFixed(styleRow, styleDropdown);

        // ===== VOLUME PANEL TOGGLE =====
        auto volumeCheckbox = std::make_shared<UltraCanvasCheckbox>("VolumeCheckbox", 0, 0, 150, 30);
        volumeCheckbox->SetText("Show Volume");
        volumeCheckbox->SetChecked(true);
        volumeCheckbox->onStateChanged = [chart](CheckedState oldState, CheckedState newState) {
            chart->SetShowVolumePanel(newState == CheckedState::Checked);
            chart->RequestRedraw();
        };
        DemoKit::AddFixed(styleRow, volumeCheckbox);

        // ===== MOVING AVERAGE TOGGLE =====
        auto maCheckbox = std::make_shared<UltraCanvasCheckbox>("MACheckbox", 0, 0, 180, 30);
        maCheckbox->SetText("Moving Average (20)");
        maCheckbox->SetChecked(true);
        maCheckbox->onStateChanged = [chart](CheckedState oldState, CheckedState newState) {
            chart->SetShowMovingAverage(newState == CheckedState::Checked, 20);
            chart->RequestRedraw();
        };
        DemoKit::AddFixed(styleRow, maCheckbox);

        // ===== CANDLE WIDTH SLIDER =====
        // The label doubles as the readout ("Candle Width: 80.0%").
        auto widthRow = addControlRow("WidthRow");
        auto widthLabel = addRowLabel(widthRow, "WidthLabel", "Candle Width:");

        auto widthSlider = std::make_shared<UltraCanvasSlider>("WidthSlider", 0, 0, 200, 30);
        widthSlider->SetRange(0.3f, 1.0f);
        widthSlider->SetValue(0.8f);
        widthSlider->onValueChanged = [chart, widthLabel](float value) {
            chart->SetCandleWidthRatio(value);
            chart->RequestRedraw();
            std::ostringstream oss;
            oss << "Candle Width: " << std::fixed << std::setprecision(1) << (value * 100) << "%";
            widthLabel->SetText(oss.str());
        };
        DemoKit::AddFixed(widthRow, widthSlider);

        // ===== GRID TOGGLE =====
        auto gridCheckbox = std::make_shared<UltraCanvasCheckbox>("GridCheckbox", 0, 0, 100, 30);
        gridCheckbox->SetText("Show Grid");
        gridCheckbox->SetChecked(true);
        gridCheckbox->onStateChanged = [chart](CheckedState oldState, CheckedState newState) {
            chart->SetShowGrid(newState == CheckedState::Checked);
            chart->RequestRedraw();
        };
        DemoKit::AddFixed(widthRow, gridCheckbox);

        // ===== TOOLTIPS TOGGLE =====
        auto tooltipsCheckbox = std::make_shared<UltraCanvasCheckbox>("TooltipsCheckbox", 0, 0, 150, 30);
        tooltipsCheckbox->SetText("Show Tooltips");
        tooltipsCheckbox->SetChecked(true);
        tooltipsCheckbox->onStateChanged = [chart](CheckedState oldState, CheckedState newState) {
            chart->SetEnableTooltips(newState == CheckedState::Checked);
            chart->RequestRedraw();
        };
        DemoKit::AddFixed(widthRow, tooltipsCheckbox);

        // ===== TIME PERIOD BUTTONS =====
        /*
        auto periodLabel = std::make_shared<UltraCanvasLabel>("PeriodLabel", 20, 125, 100, 25);
        periodLabel->SetText("Time Period:");
        controlPanel->AddChild(periodLabel);

        std::vector<std::pair<std::string, int>> periods = {
                {"1W", 7}, {"1M", 30}, {"3M", 90}, {"6M", 180}, {"1Y", 365}
        };

        int btnX = 130;
        for (size_t i = 0; i < periods.size(); i++) {
            auto periodBtn = std::make_shared<UltraCanvasButton>(
                    "Period" + periods[i].first,  btnX, 125, 60, 30
            );
            periodBtn->SetText(periods[i].first);

            // Capture by value to avoid reference issues
            auto period = periods[i];
            periodBtn->SetOnClick([chart, period]() {
                // In real app, this would filter the data to show only the specified period
                debugOutput << "Switching to " << period.first << " view (" << period.second << " days)" << std::endl;
                chart->RequestRedraw();
            });
            controlPanel->AddChild(periodBtn);
            btnX += 70;
        }
*/
        /*
        // ===== ACTION BUTTONS =====
        auto resetBtn = std::make_shared<UltraCanvasButton>("ResetBtn", 150, 165, 120, 35);
        resetBtn->SetText("Reset View");
        resetBtn->SetOnClick([chart]() {
//            chart->ResetZoomAndPan();
            chart->RequestRedraw();
        });
        controlPanel->AddChild(resetBtn);

        auto exportBtn = std::make_shared<UltraCanvasButton>("ExportBtn", 280, 165, 120, 35);
        exportBtn->SetText("Export PNG");
        exportBtn->SetOnClick([chart]() {
            debugOutput << "Exporting chart to PNG..." << std::endl;
            // In real app, this would export the chart to a PNG file
        });
        controlPanel->AddChild(exportBtn);

        auto dataBtn = std::make_shared<UltraCanvasButton>("DataBtn", 410, 165, 120, 35);
        dataBtn->SetText("Load CSV");
        dataBtn->SetOnClick([chart]() {
            debugOutput << "Loading data from CSV..." << std::endl;
            // In real app, this would open a file dialog to load CSV data
        });
        controlPanel->AddChild(dataBtn);
*/
        // ===== COLOR CUSTOMIZATION =====
        auto colorRow = addControlRow("ColorRow");
        addRowLabel(colorRow, "ColorLabel", "Color Scheme:");

        auto greenRedBtn = std::make_shared<UltraCanvasButton>("GreenRedBtn", 0, 0, 120, 30);
        greenRedBtn->SetText("Green/Red");
        greenRedBtn->SetOnClick([chart]() {
            chart->SetBullishCandleColor(Color(0, 180, 0, 255));
            chart->SetBearishCandleColor(Color(220, 0, 0, 255));
            chart->RequestRedraw();
        });
        DemoKit::Add(colorRow, greenRedBtn);

        auto blueOrangeBtn = std::make_shared<UltraCanvasButton>("BlueOrangeBtn", 0, 0, 120, 30);
        blueOrangeBtn->SetText("Blue/Orange");
        blueOrangeBtn->SetOnClick([chart]() {
            chart->SetBullishCandleColor(Color(0, 120, 200, 255));
            chart->SetBearishCandleColor(Color(255, 140, 0, 255));
            chart->RequestRedraw();
        });
        DemoKit::Add(colorRow, blueOrangeBtn);

        auto monochrome = std::make_shared<UltraCanvasButton>("MonochromeBtn", 0, 0, 120, 30);
        monochrome->SetText("Monochrome");
        monochrome->SetOnClick([chart]() {
            chart->SetBullishCandleColor(Color(255, 255, 255, 255));
            chart->SetBearishCandleColor(Color(80, 80, 80, 255));
            chart->RequestRedraw();
        });
        DemoKit::Add(colorRow, monochrome);

        // ===== REAL-TIME UPDATE SIMULATION =====
        /*
        auto updateLabel = std::make_shared<UltraCanvasLabel>("UpdateLabel", 20, 250, 150, 25);
        updateLabel->SetText("Live Updates:");
        controlPanel->AddChild(updateLabel);

        auto liveToggle = std::make_shared<UltraCanvasCheckbox>("LiveToggle", 130, 250, 150, 25);
        liveToggle->SetText("Simulate Live Data");
        liveToggle->SetChecked(false);
        liveToggle->onStateChanged = [chart](CheckboxState oldState, CheckboxState newState) {
            if (newState == CheckboxState::Checked) {
                debugOutput << "Starting live data simulation..." << std::endl;
                // In real app, this would start a timer to update data
            } else {
                debugOutput << "Stopping live data simulation..." << std::endl;
            }
        };
        controlPanel->AddChild(liveToggle);
*/
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
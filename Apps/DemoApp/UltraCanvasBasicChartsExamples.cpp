// Apps/DemoApp/UltraCanvasBasicChartsExamples.cpp
// Implementation of all chart component example creators with interactive controls
// Version: 1.1.0
// Last Modified: 2025-01-07
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/UltraCanvasSpecificChartElements.h"
#include "Plugins/Charts/UltraCanvasScatterPlot3D.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSwitch.h"
#include <sstream>
#include <random>
#include <map>

namespace UltraCanvas {

// ===== BASIC CHART ELEMENTS =====
static struct ChartControls {
    bool showPoints = true;
    bool smoothingEnabled = true;
    bool valueLabelsEnabled = true;
    int currentShape = 0;
} chartControl;

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateLineChartsExamples() {
        // Sample data sources
        std::shared_ptr<ChartDataVector> salesData;
        std::shared_ptr<ChartDataVector> revenueData;
        std::shared_ptr<ChartDataVector> randomData;

        // Create revenue data
        revenueData = std::make_shared<ChartDataVector>();
        std::vector<ChartDataPoint> revenue = {
                ChartDataPoint(1, 85000, 0, "Q1 2023", 85000),
                ChartDataPoint(2, 92000, 0, "Q2 2023", 92000),
                ChartDataPoint(3, 78000, 0, "Q3 2023", 78000),
                ChartDataPoint(4, 105000, 0, "Q4 2023", 105000),
                ChartDataPoint(5, 98000, 0, "Q1 2024", 98000),
                ChartDataPoint(6, 112000, 0, "Q2 2024", 112000),
                ChartDataPoint(7, 125000, 0, "Q3 2024", 125000),
                ChartDataPoint(8, 138000, 0, "Q4 2024", 138000)
        };
        revenueData->LoadFromArray(revenue);

        // Create sales data for line chart (monthly sales over 12 months)
        salesData = std::make_shared<ChartDataVector>();
        std::vector<ChartDataPoint> sales = {
                ChartDataPoint(1, 45000, 0, "Jan", 45000),
                ChartDataPoint(2, 52000, 0, "Feb", 52000),
                ChartDataPoint(3, 48000, 0, "Mar", 48000),
                ChartDataPoint(4, 61000, 0, "Apr", 61000),
                ChartDataPoint(5, 55000, 0, "May", 55000),
                ChartDataPoint(6, 67000, 0, "Jun", 67000),
                ChartDataPoint(7, 71000, 0, "Jul", 71000),
                ChartDataPoint(8, 69000, 0, "Aug", 69000),
                ChartDataPoint(9, 58000, 0, "Sep", 58000),
                ChartDataPoint(10, 63000, 0, "Oct", 63000),
                ChartDataPoint(11, 72000, 0, "Nov", 72000),
                ChartDataPoint(12, 78000, 0, "Dec", 78000)
        };
        salesData->LoadFromArray(sales);

        // Generate random data
        randomData = std::make_shared<ChartDataVector>();
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> valueDist(20000, 100000);
        std::vector<ChartDataPoint> randomPoints;
        for (int i = 1; i <= 10; ++i) {
            double value = valueDist(gen);
            randomPoints.emplace_back(i, value, 0, "Point " + std::to_string(i), value);
        }
        randomData->LoadFromArray(randomPoints);

        auto container = std::make_shared<UltraCanvasContainer>("LineChartExamples", 0, 0, 950, 700);

        // Add description label
        auto descLabel = std::make_shared<UltraCanvasLabel>("LineChartDescription", 50, 20, 700, 60);
        descLabel->SetText("Line Chart Example - Visualizes trends over time with smooth lines and data points.\n"
                           "Perfect for showing continuous data changes like sales, temperature, or stock prices.\n"
                           "Features: Interactive zooming, panning, tooltips, and customizable appearance.");
        descLabel->SetFontSize(12);
        descLabel->SetTextColor(Color(50, 50, 50, 255));
        container->AddChild(descLabel);

        // Create line chart
        std::shared_ptr<UltraCanvasLineChartElement> lineChart =
                CreateLineChartElement("salesLineChart", 50, 100, 600, 400);

        lineChart->SetDataSource(salesData);
        lineChart->SetChartTitle("Monthly Sales Trend");
        lineChart->SetLineColor(Color(0, 102, 204, 255));       // Blue line
        lineChart->SetLineWidth(3.0f);
        lineChart->SetShowDataPoints(chartControl.showPoints);
        lineChart->SetPointColor(Color(255, 99, 71, 255));      // Tomato red points
        lineChart->SetPointRadius(5.0f);
        lineChart->SetSmoothingEnabled(chartControl.smoothingEnabled);
        lineChart->SetEnableTooltips(true);
        lineChart->SetEnableZoom(true);
        lineChart->SetEnablePan(true);
        lineChart->SetShowValueLabels(chartControl.valueLabelsEnabled);
        lineChart->SetXAxisLabelMode(XAxisLabelMode::DataLabel);

        container->AddChild(lineChart);

        // Button group positioning
        int buttonY = 520;
        int buttonX = 50;
        int buttonWidth = 140;
        int buttonHeight = 35;
        int buttonSpacing = 10;

        // Load Revenue button
        auto btnLoadRevenue = std::make_shared<UltraCanvasButton>("btnLoadRevenue",
                                                                  buttonX, buttonY, buttonWidth, buttonHeight);
        btnLoadRevenue->SetText("Load Revenue");
        btnLoadRevenue->onClick = [lineChart, revenueData]() {
            lineChart->SetDataSource(revenueData);
            lineChart->SetChartTitle("Quarterly Revenue");
            //lineChart->Invalidate();
        };
        container->AddChild(btnLoadRevenue);

        // Load Sales button
        buttonX += buttonWidth + buttonSpacing;
        auto btnLoadSales = std::make_shared<UltraCanvasButton>("btnLoadSales",
                                                                buttonX, buttonY, buttonWidth, buttonHeight);
        btnLoadSales->SetText("Load Sales");
        btnLoadSales->onClick = [lineChart, salesData]() {
            lineChart->SetDataSource(salesData);
            lineChart->SetChartTitle("Monthly Sales Trend");
            //lineChart->Invalidate();
        };
        container->AddChild(btnLoadSales);

        // Load Random button
        buttonX += buttonWidth + buttonSpacing;
        auto btnLoadRandom = std::make_shared<UltraCanvasButton>("btnLoadRandom",
                                                                 buttonX, buttonY, buttonWidth, buttonHeight);
        btnLoadRandom->SetText("Load Random");
        btnLoadRandom->onClick = [lineChart]() {
            // Generate new random data
            auto newRandomData = std::make_shared<ChartDataVector>();
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<> valueDist(15000, 90000);
            std::vector<ChartDataPoint> randomPoints;
            for (int i = 1; i <= 12; ++i) {
                double value = valueDist(gen);
                randomPoints.emplace_back(i, value, 0, "Pt" + std::to_string(i), value);
            }
            newRandomData->LoadFromArray(randomPoints);
            lineChart->SetDataSource(newRandomData);
            lineChart->SetChartTitle("Random Data");
            //lineChart->Invalidate();
        };
        container->AddChild(btnLoadRandom);

        // Data Points switch
        buttonX += buttonWidth + buttonSpacing;
        auto swPoints = UltraCanvasSwitch::Create("swTogglePoints",
                                                  buttonX, buttonY + 7, "Points", chartControl.showPoints);
        swPoints->onStateChanged = [lineChart](CheckedState, CheckedState newState) {
            chartControl.showPoints = (newState == CheckedState::Checked);
            lineChart->SetShowDataPoints(chartControl.showPoints);
        };
        container->AddChild(swPoints);

        // Smoothing switch
        buttonX += buttonWidth + buttonSpacing;
        auto swSmoothing = UltraCanvasSwitch::Create("swToggleSmoothing",
                                                     buttonX, buttonY + 7, "Smooth", chartControl.smoothingEnabled);
        swSmoothing->onStateChanged = [lineChart](CheckedState, CheckedState newState) {
            chartControl.smoothingEnabled = (newState == CheckedState::Checked);
            lineChart->SetSmoothingEnabled(chartControl.smoothingEnabled);
        };
        container->AddChild(swSmoothing);

        // Value Labels switch
        buttonX += buttonWidth + buttonSpacing;
        auto swValueLabels = UltraCanvasSwitch::Create("swToggleValueLabels",
                                                       buttonX, buttonY + 7, "Labels", chartControl.valueLabelsEnabled);
        swValueLabels->onStateChanged = [lineChart](CheckedState, CheckedState newState) {
            chartControl.valueLabelsEnabled = (newState == CheckedState::Checked);
            lineChart->SetShowValueLabels(chartControl.valueLabelsEnabled);
        };
        container->AddChild(swValueLabels);

        return container;
    }

    // CreateBarChartsExamples lives in UltraCanvasBarChartExamples.cpp - the
    // bar chart page is a chart-engine showcase now.

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateScatterPlotChartsExamples() {
        // Sample data sources
        std::shared_ptr<ChartDataVector> correlationData;

        // Create correlation data for scatter plot (marketing spend vs sales)
        correlationData = std::make_shared<ChartDataVector>();
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> xDist(1000, 10000);  // Marketing spend
        std::uniform_real_distribution<> noise(-5000, 5000);  // Random noise

        std::vector<ChartDataPoint> correlation;
        for (int i = 0; i < 50; ++i) {
            double marketingSpend = xDist(gen);
            double sales = marketingSpend * 3.2 + 15000 + noise(gen);  // Correlation with noise
            correlation.emplace_back(marketingSpend, sales, 0,
                                     "Point " + std::to_string(i + 1), sales);
        }
        correlationData->LoadFromArray(correlation);

        auto container = std::make_shared<UltraCanvasContainer>("ScatterPlotExamples", 0, 0, 1180, 700);

        // Add description label
        auto descLabel = std::make_shared<UltraCanvasLabel>("ScatterPlotDescription", 30, 20, 1120, 60);
        descLabel->SetText("Scatter Plot Example - Shows relationships between two continuous variables.\n"
                           "The 2D plot fits a least-squares correlation line with r / r-squared readout; "
                           "the 3D plot shows an (x, y, z) cloud with its principal-axis correlation line.\n"
                           "Features: Point shapes, per-point colors, tooltips - drag the 3D plot to orbit, wheel to zoom.");
        descLabel->SetFontSize(12);
        descLabel->SetTextColor(Color(50, 50, 50, 255));
        container->AddChild(descLabel);

        // Create scatter plot for correlation analysis
        std::shared_ptr<UltraCanvasScatterPlotElement> scatterPlot =
                CreateScatterPlotElement("correlationScatter", 30, 100, 540, 420);

        // Configure Scatter Plot
        scatterPlot->SetDataSource(correlationData);
        scatterPlot->SetChartTitle("Marketing Spend vs Sales");
        scatterPlot->SetPointColor(Color(255, 140, 0, 255));    // Dark orange
        scatterPlot->SetPointSize(8.0f);
        scatterPlot->SetPointShape(UltraCanvasScatterPlotElement::PointShape::Circle);
        scatterPlot->SetEnableTooltips(true);
        scatterPlot->SetEnableZoom(true);
        scatterPlot->SetEnablePan(true);
        scatterPlot->SetEnableSelection(true);
        scatterPlot->SetShowTrendLine(true);
        scatterPlot->SetShowCorrelationInfo(true);

        container->AddChild(scatterPlot);

        // 3D scatter plot: a correlated cloud along a spatial diagonal with a
        // few outliers, plus the fitted principal-axis correlation line.
        auto scatter3DData = std::make_shared<ChartDataVector>();
        {
            std::uniform_real_distribution<> tDist(-100.0, 100.0);
            std::uniform_real_distribution<> jitter(-8.0, 8.0);
            std::uniform_real_distribution<> outlierJitter(-55.0, 55.0);
            std::vector<ChartDataPoint> cloud;
            for (int i = 0; i < 90; ++i) {
                bool outlier = (i % 9) == 8;
                auto& spread = outlier ? outlierJitter : jitter;
                double t = tDist(gen);
                ChartDataPoint p(t * 0.9 + spread(gen),
                                 t * 0.7 + spread(gen),
                                 t * 1.1 + spread(gen),
                                 outlier ? "Outlier " + std::to_string(i + 1)
                                         : "Inlier " + std::to_string(i + 1));
                p.color = outlier ? Color(220, 60, 60, 255) : Color(0, 102, 204, 255);
                cloud.push_back(p);
            }
            scatter3DData->LoadFromArray(cloud);
        }

        auto scatterPlot3D = CreateScatterPlot3DElement("scatter3D", 600, 100, 540, 420);
        scatterPlot3D->SetDataSource(scatter3DData);
        scatterPlot3D->SetChartTitle("3D Correlation Cloud");
        scatterPlot3D->SetPointSize(4.5);
        scatterPlot3D->SetAxisTitles("X", "Y", "Z");
        scatterPlot3D->SetShowCorrelationLine(true);
        container->AddChild(scatterPlot3D);

        // Buttons under the charts
        int buttonY = 540;
        int buttonWidth = 220;
        int buttonHeight = 35;

        // Cycle Scatter Plot Shapes button
        auto btnCycleShapes = std::make_shared<UltraCanvasButton>("btnCycleShapes",
                                                                  30, buttonY, buttonWidth, buttonHeight);
        btnCycleShapes->SetText("Cycle Scatter Shapes");

        // Track current shape
        std::vector<UltraCanvasScatterPlotElement::PointShape> shapes = {
                UltraCanvasScatterPlotElement::PointShape::Circle,
                UltraCanvasScatterPlotElement::PointShape::Square,
                UltraCanvasScatterPlotElement::PointShape::Triangle,
                UltraCanvasScatterPlotElement::PointShape::Diamond
        };

        btnCycleShapes->SetOnClick([scatterPlot, shapes]()  {
            chartControl.currentShape = (chartControl.currentShape + 1) % shapes.size();
            scatterPlot->SetPointShape(shapes[chartControl.currentShape]);
            //scatterPlot->Invalidate();
        });
        container->AddChild(btnCycleShapes);

        // Toggle the 2D correlation line
        auto swTrend = UltraCanvasSwitch::Create("swToggleTrend",
                                                 30 + buttonWidth + 20, buttonY + 7,
                                                 "Correlation Line", scatterPlot->GetShowTrendLine());
        swTrend->onStateChanged = [scatterPlot](CheckedState, CheckedState newState) {
            bool show = (newState == CheckedState::Checked);
            scatterPlot->SetShowTrendLine(show);
            scatterPlot->SetShowCorrelationInfo(show);
        };
        container->AddChild(swTrend);

        // Reset the 3D camera
        auto btnResetView = std::make_shared<UltraCanvasButton>("btnResetView3D",
                                                                600, buttonY, buttonWidth, buttonHeight);
        btnResetView->SetText("Reset 3D View");
        btnResetView->SetOnClick([scatterPlot3D]() {
            scatterPlot3D->ViewIsometric();
        });
        container->AddChild(btnResetView);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateAreaChartsExamples() {
        // Sample data sources
        std::shared_ptr<ChartDataVector> salesData;
        std::shared_ptr<ChartDataVector> revenueData;

        revenueData = std::make_shared<ChartDataVector>();
        std::vector<ChartDataPoint> revenue = {
                ChartDataPoint(1, 85000, 0, "Q1 2023", 85000),
                ChartDataPoint(2, 92000, 0, "Q2 2023", 92000),
                ChartDataPoint(3, 78000, 0, "Q3 2023", 78000),
                ChartDataPoint(4, 105000, 0, "Q4 2023", 105000),
                ChartDataPoint(5, 98000, 0, "Q1 2024", 98000),
                ChartDataPoint(6, 112000, 0, "Q2 2024", 112000),
                ChartDataPoint(7, 125000, 0, "Q3 2024", 125000),
                ChartDataPoint(8, 138000, 0, "Q4 2024", 138000)
        };
        revenueData->LoadFromArray(revenue);

        // Create sales data
        salesData = std::make_shared<ChartDataVector>();
        std::vector<ChartDataPoint> sales = {
                ChartDataPoint(1, 45000, 0, "Jan", 45000),
                ChartDataPoint(2, 52000, 0, "Feb", 52000),
                ChartDataPoint(3, 48000, 0, "Mar", 48000),
                ChartDataPoint(4, 61000, 0, "Apr", 61000),
                ChartDataPoint(5, 55000, 0, "May", 55000),
                ChartDataPoint(6, 67000, 0, "Jun", 67000),
                ChartDataPoint(7, 71000, 0, "Jul", 71000),
                ChartDataPoint(8, 69000, 0, "Aug", 69000),
                ChartDataPoint(9, 58000, 0, "Sep", 58000),
                ChartDataPoint(10, 63000, 0, "Oct", 63000),
                ChartDataPoint(11, 72000, 0, "Nov", 72000),
                ChartDataPoint(12, 78000, 0, "Dec", 78000)
        };
        salesData->LoadFromArray(sales);

        auto container = std::make_shared<UltraCanvasContainer>("AreaChartExamples", 0, 0, 900, 700);

        // Add description label
        auto descLabel = std::make_shared<UltraCanvasLabel>("AreaChartDescription", 50, 20, 800, 60);
        descLabel->SetText("Area Chart Example - Emphasizes magnitude of change over time with filled areas.\n"
                           "Perfect for showing cumulative values, trends, and volume data like revenue or resource usage.\n"
                           "Features: Gradient fills, transparency, smooth curves, data points, zoom, and pan capabilities.");
        descLabel->SetFontSize(12);
        descLabel->SetTextColor(Color(50, 50, 50, 255));
        container->AddChild(descLabel);

        // Create area chart
        std::shared_ptr<UltraCanvasAreaChartElement> areaChart =
                CreateAreaChartElement("revenueAreaChart", 50, 100, 600, 400);

        // Configure Area Chart
        areaChart->SetDataSource(revenueData);
        areaChart->SetChartTitle("Quarterly Revenue Growth");
        areaChart->SetFillColor(Color(0, 150, 136, 120));        // Teal with transparency
        areaChart->SetLineColor(Color(0, 150, 136, 255));        // Solid teal line
        areaChart->SetLineWidth(3.0f);
        areaChart->SetShowDataPoints(chartControl.showPoints);
        areaChart->SetPointColor(Color(255, 87, 34, 255));       // Deep orange points
        areaChart->SetPointRadius(4.0f);
        areaChart->SetFillGradientEnabled(true);
        areaChart->SetGradientColors(
                Color(0, 150, 136, 180),    // Teal top
                Color(0, 150, 136, 40)      // Faded teal bottom
        );
        areaChart->SetSmoothingEnabled(chartControl.smoothingEnabled);
        areaChart->SetShowValueLabels(chartControl.valueLabelsEnabled);
        areaChart->SetEnableTooltips(true);
        areaChart->SetEnableZoom(true);
        areaChart->SetEnablePan(true);
        areaChart->SetXAxisLabelMode(XAxisLabelMode::DataLabel);

        container->AddChild(areaChart);

        // Button group positioning
        int buttonY = 520;
        int buttonX = 50;
        int buttonWidth = 140;
        int buttonHeight = 35;
        int buttonSpacing = 10;

        // Load Revenue button
        auto btnLoadRevenue = std::make_shared<UltraCanvasButton>("btnLoadRevenue",
                                                                  buttonX, buttonY, buttonWidth, buttonHeight);
        btnLoadRevenue->SetText("Load Revenue");
        btnLoadRevenue->SetOnClick([areaChart, revenueData]() {
            areaChart->SetDataSource(revenueData);
            areaChart->SetChartTitle("Quarterly Revenue Growth");
            //areaChart->Invalidate();
        });
        container->AddChild(btnLoadRevenue);

        // Load Sales button
        buttonX += buttonWidth + buttonSpacing;
        auto btnLoadSales = std::make_shared<UltraCanvasButton>("btnLoadSales",
                                                                buttonX, buttonY, buttonWidth, buttonHeight);
        btnLoadSales->SetText("Load Sales");
        btnLoadSales->SetOnClick([areaChart, salesData]() {
            areaChart->SetDataSource(salesData);
            areaChart->SetChartTitle("Monthly Sales Volume");
            //areaChart->Invalidate();
        });
        container->AddChild(btnLoadSales);

        // Load Random button
        buttonX += buttonWidth + buttonSpacing;
        auto btnLoadRandom = std::make_shared<UltraCanvasButton>("btnLoadRandom",
                                                                 buttonX, buttonY, buttonWidth, buttonHeight);
        btnLoadRandom->SetText("Load Random");
        btnLoadRandom->SetOnClick([areaChart]() {
            // Generate new random data
            auto newRandomData = std::make_shared<ChartDataVector>();
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<> valueDist(30000, 120000);
            std::vector<ChartDataPoint> randomPoints;
            for (int i = 1; i <= 10; ++i) {
                double value = valueDist(gen);
                randomPoints.emplace_back(i, value, 0, "Period " + std::to_string(i), value);
            }
            newRandomData->LoadFromArray(randomPoints);
            areaChart->SetDataSource(newRandomData);
            areaChart->SetChartTitle("Random Data Volume");
            //areaChart->Invalidate();
        });
        container->AddChild(btnLoadRandom);

        // Data Points switch
        buttonX += buttonWidth + buttonSpacing;
        auto swPoints = UltraCanvasSwitch::Create("swTogglePoints",
                                                  buttonX, buttonY + 7, "Points", chartControl.showPoints);
        swPoints->onStateChanged = [areaChart](CheckedState, CheckedState newState) {
            chartControl.showPoints = (newState == CheckedState::Checked);
            areaChart->SetShowDataPoints(chartControl.showPoints);
        };
        container->AddChild(swPoints);

        // Smoothing switch
        buttonX += buttonWidth + buttonSpacing;
        auto swSmoothing = UltraCanvasSwitch::Create("swToggleSmoothing",
                                                     buttonX, buttonY + 7, "Smooth", chartControl.smoothingEnabled);
        swSmoothing->onStateChanged = [areaChart](CheckedState, CheckedState newState) {
            chartControl.smoothingEnabled = (newState == CheckedState::Checked);
            areaChart->SetSmoothingEnabled(chartControl.smoothingEnabled);
        };
        container->AddChild(swSmoothing);

        // Value Labels switch
        buttonX += buttonWidth + buttonSpacing;
        auto swValueLabels = UltraCanvasSwitch::Create("swToggleValueLabels",
                                                       buttonX, buttonY + 7, "Labels", chartControl.valueLabelsEnabled);
        swValueLabels->onStateChanged = [areaChart](CheckedState, CheckedState newState) {
            chartControl.valueLabelsEnabled = (newState == CheckedState::Checked);
            areaChart->SetShowValueLabels(chartControl.valueLabelsEnabled);
        };
        container->AddChild(swValueLabels);

        return container;
    }

} // namespace UltraCanvas
// Apps/DemoApp/UltraCanvasBasicChartsExamples.cpp
// Implementation of all chart component example creators with interactive controls
// Version: 1.1.0
// Last Modified: 2025-01-07
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/UltraCanvasSpecificChartElements.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
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

        auto container = std::make_shared<UltraCanvasContainer>("LineChartExamples", 100, 0, 0, 950, 700);

        // Add description label
        auto descLabel = std::make_shared<UltraCanvasLabel>("LineChartDescription", 2001, 50, 20, 700, 60);
        descLabel->SetText("Line Chart Example - Visualizes trends over time with smooth lines and data points.\n"
                           "Perfect for showing continuous data changes like sales, temperature, or stock prices.\n"
                           "Features: Interactive zooming, panning, tooltips, and customizable appearance.");
        descLabel->SetFontSize(12);
        descLabel->SetTextColor(Color(50, 50, 50, 255));
        container->AddChild(descLabel);

        // Create line chart
        std::shared_ptr<UltraCanvasLineChartElement> lineChart =
                CreateLineChartElement("salesLineChart", 1001, 50, 100, 600, 400);

        lineChart->SetDataSource(salesData);
        lineChart->SetChartTitle("Monthly Sales Trend");
        lineChart->SetLineColor(Color(0, 102, 204, 255));       // Blue line
        lineChart->SetLineWidth(3.0f);
        lineChart->SetShowDataPoints(true);
        lineChart->SetPointColor(Color(255, 99, 71, 255));      // Tomato red points
        lineChart->SetPointRadius(5.0f);
        lineChart->SetSmoothingEnabled(true);
        lineChart->SetEnableTooltips(true);
        lineChart->SetEnableZoom(true);
        lineChart->SetEnablePan(true);
        lineChart->SetShowValueLabels(chartControl.valueLabelsEnabled);
        lineChart->SetXAxisLabelMode(XAxisLabelMode::DataLabel);

        container->AddChild(lineChart);

        // Button group positioning
        int buttonY = 520;
        int buttonX = 50;
        int buttonWidth = 120;
        int buttonHeight = 35;
        int buttonSpacing = 10;

        // Load Revenue button
        auto btnLoadRevenue = std::make_shared<UltraCanvasButton>("btnLoadRevenue", 3001,
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
        auto btnLoadSales = std::make_shared<UltraCanvasButton>("btnLoadSales", 3002,
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
        auto btnLoadRandom = std::make_shared<UltraCanvasButton>("btnLoadRandom", 3003,
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

        // Toggle Data Points button
        buttonX += buttonWidth + buttonSpacing;
        auto btnTogglePoints = std::make_shared<UltraCanvasButton>("btnTogglePoints", 3004,
                                                                   buttonX, buttonY, buttonWidth, buttonHeight);
        btnTogglePoints->SetText("Toggle Points");
        btnTogglePoints->onClick = [lineChart]()  {
            chartControl.showPoints = !chartControl.showPoints;
            lineChart->SetShowDataPoints(chartControl.showPoints);
            //lineChart->Invalidate();
        };
        container->AddChild(btnTogglePoints);

        // Toggle Smoothing button
        buttonX += buttonWidth + buttonSpacing;
        auto btnToggleSmoothing = std::make_shared<UltraCanvasButton>("btnToggleSmoothing", 3005,
                                                                      buttonX, buttonY, buttonWidth, buttonHeight);
        btnToggleSmoothing->SetText("Toggle Smooth");
        btnToggleSmoothing->onClick = [lineChart]()  {
            chartControl.smoothingEnabled = !chartControl.smoothingEnabled;
            lineChart->SetSmoothingEnabled(chartControl.smoothingEnabled);
            //lineChart->Invalidate();
        };
        container->AddChild(btnToggleSmoothing);

        buttonX += buttonWidth + buttonSpacing;

        auto btnToggleValueLabels = std::make_shared<UltraCanvasButton>("btnToggleValueLabels", 3005,
                                                                      buttonX, buttonY, buttonWidth, buttonHeight);
        btnToggleValueLabels->SetText("Toggle Labels");
        btnToggleValueLabels->onClick = [lineChart]()  {
            chartControl.valueLabelsEnabled = !chartControl.valueLabelsEnabled;
            lineChart->SetShowValueLabels(chartControl.valueLabelsEnabled);
            //lineChart->Invalidate();
        };
        container->AddChild(btnToggleValueLabels);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBarChartsExamples() {
        // Sample data sources
        std::shared_ptr<ChartDataVector> salesData;
        std::shared_ptr<ChartDataVector> performanceData;
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

        // Create performance data for bar chart (quarterly performance)
        performanceData = std::make_shared<ChartDataVector>();
        std::vector<ChartDataPoint> performance = {
                ChartDataPoint(1, 85, 0, "Q1 2024", 85),
                ChartDataPoint(2, 92, 0, "Q2 2024", 92),
                ChartDataPoint(3, 78, 0, "Q3 2024", 78),
                ChartDataPoint(4, 95, 0, "Q4 2024", 95),
                ChartDataPoint(5, 88, 0, "Q1 2025", 88)
        };
        performanceData->LoadFromArray(performance);

        auto container = std::make_shared<UltraCanvasContainer>("BarChartExamples", 100, 0, 0, 800, 700);

        // Add description label
        auto descLabel = std::make_shared<UltraCanvasLabel>("BarChartDescription", 2002, 50, 20, 700, 60);
        descLabel->SetText("Bar Chart Example - Displays categorical data with rectangular bars.\n"
                           "Ideal for comparing discrete values across categories like quarterly performance or regional sales.\n"
                           "Features: Customizable colors, borders, spacing, and interactive tooltips.");
        descLabel->SetFontSize(12);
        descLabel->SetTextColor(Color(50, 50, 50, 255));
        container->AddChild(descLabel);

        // Create bar chart
        std::shared_ptr<UltraCanvasBarChartElement> barChart =
                CreateBarChartElement("performanceBarChart", 1002, 50, 100, 600, 400);

        // Configure Bar Chart
        barChart->SetDataSource(performanceData);
        barChart->SetChartTitle("Quarterly Performance");
        barChart->SetBarColor(Color(60, 179, 113, 255));        // Medium sea green
        barChart->SetBarBorderColor(Color(34, 139, 34, 255));   // Forest green border
        barChart->SetBarBorderWidth(2.0f);
        barChart->SetBarSpacing(0.2f);                          // 20% spacing between bars
        barChart->SetEnableTooltips(true);
        barChart->SetXAxisLabelMode(XAxisLabelMode::DataLabel);

        container->AddChild(barChart);

        // Button group positioning
        int buttonY = 520;
        int buttonX = 50;
        int buttonWidth = 120;
        int buttonHeight = 35;
        int buttonSpacing = 10;

        // Load Revenue button
        auto btnLoadRevenue = std::make_shared<UltraCanvasButton>("btnLoadRevenue", 3011,
                                                                  buttonX, buttonY, buttonWidth, buttonHeight);
        btnLoadRevenue->SetText("Load Revenue");
        btnLoadRevenue->onClick = [barChart, revenueData]() {
            barChart->SetDataSource(revenueData);
            barChart->SetChartTitle("Quarterly Revenue");
            //barChart->Invalidate();
        };
        container->AddChild(btnLoadRevenue);

        // Load Sales button
        buttonX += buttonWidth + buttonSpacing;
        auto btnLoadSales = std::make_shared<UltraCanvasButton>("btnLoadSales", 3012,
                                                                buttonX, buttonY, buttonWidth, buttonHeight);
        btnLoadSales->SetText("Load Sales");
        btnLoadSales->SetOnClick([barChart, salesData]() {
            barChart->SetDataSource(salesData);
            barChart->SetChartTitle("Monthly Sales");
            //barChart->Invalidate();
        });
        container->AddChild(btnLoadSales);

        // Load Random button
        buttonX += buttonWidth + buttonSpacing;
        auto btnLoadRandom = std::make_shared<UltraCanvasButton>("btnLoadRandom", 3013,
                                                                 buttonX, buttonY, buttonWidth, buttonHeight);
        btnLoadRandom->SetText("Load Random");
        btnLoadRandom->SetOnClick([barChart]() {
            // Generate new random data
            auto newRandomData = std::make_shared<ChartDataVector>();
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<> valueDist(50, 100);
            std::vector<ChartDataPoint> randomPoints;
            std::vector<std::string> categories = {"Cat A", "Cat B", "Cat C", "Cat D", "Cat E", "Cat F"};
            for (size_t i = 0; i < categories.size(); ++i) {
                double value = valueDist(gen);
                randomPoints.emplace_back(i + 1, value, 0, categories[i], value);
            }
            newRandomData->LoadFromArray(randomPoints);
            barChart->SetDataSource(newRandomData);
            barChart->SetChartTitle("Random Categories");
            //barChart->Invalidate();
        });
        container->AddChild(btnLoadRandom);

        return container;
    }

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

        auto container = std::make_shared<UltraCanvasContainer>("ScatterPlotExamples", 100, 0, 0, 800, 700);

        // Add description label
        auto descLabel = std::make_shared<UltraCanvasLabel>("ScatterPlotDescription", 2003, 50, 20, 700, 60);
        descLabel->SetText("Scatter Plot Example - Shows relationships between two continuous variables.\n"
                           "Excellent for identifying correlations, clusters, and outliers in datasets.\n"
                           "Features: Multiple point shapes, selection capability, zoom, pan, and interactive tooltips.");
        descLabel->SetFontSize(12);
        descLabel->SetTextColor(Color(50, 50, 50, 255));
        container->AddChild(descLabel);

        // Create scatter plot for correlation analysis
        std::shared_ptr<UltraCanvasScatterPlotElement> scatterPlot =
                CreateScatterPlotElement("correlationScatter", 1003, 50, 100, 600, 400);

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

        container->AddChild(scatterPlot);

        // Button for cycling scatter plot shapes
        int buttonY = 520;
        int buttonX = 50;
        int buttonWidth = 180;
        int buttonHeight = 35;

        // Cycle Scatter Plot Shapes button
        auto btnCycleShapes = std::make_shared<UltraCanvasButton>("btnCycleShapes", 3021,
                                                                  buttonX, buttonY, buttonWidth, buttonHeight);
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

        auto container = std::make_shared<UltraCanvasContainer>("AreaChartExamples", 100, 0, 0, 900, 700);

        // Add description label
        auto descLabel = std::make_shared<UltraCanvasLabel>("AreaChartDescription", 2004, 50, 20, 800, 60);
        descLabel->SetText("Area Chart Example - Emphasizes magnitude of change over time with filled areas.\n"
                           "Perfect for showing cumulative values, trends, and volume data like revenue or resource usage.\n"
                           "Features: Gradient fills, transparency, smooth curves, data points, zoom, and pan capabilities.");
        descLabel->SetFontSize(12);
        descLabel->SetTextColor(Color(50, 50, 50, 255));
        container->AddChild(descLabel);

        // Create area chart
        std::shared_ptr<UltraCanvasAreaChartElement> areaChart =
                CreateAreaChartElement("revenueAreaChart", 1004, 50, 100, 600, 400);

        // Configure Area Chart
        areaChart->SetDataSource(revenueData);
        areaChart->SetChartTitle("Quarterly Revenue Growth");
        areaChart->SetFillColor(Color(0, 150, 136, 120));        // Teal with transparency
        areaChart->SetLineColor(Color(0, 150, 136, 255));        // Solid teal line
        areaChart->SetLineWidth(3.0f);
        areaChart->SetShowDataPoints(true);
        areaChart->SetPointColor(Color(255, 87, 34, 255));       // Deep orange points
        areaChart->SetPointRadius(4.0f);
        areaChart->SetFillGradientEnabled(true);
        areaChart->SetGradientColors(
                Color(0, 150, 136, 180),    // Teal top
                Color(0, 150, 136, 40)      // Faded teal bottom
        );
        areaChart->SetSmoothingEnabled(chartControl.smoothingEnabled);
        areaChart->SetEnableTooltips(true);
        areaChart->SetEnableZoom(true);
        areaChart->SetEnablePan(true);
        areaChart->SetXAxisLabelMode(XAxisLabelMode::DataLabel);

        container->AddChild(areaChart);

        // Button group positioning
        int buttonY = 520;
        int buttonX = 50;
        int buttonWidth = 120;
        int buttonHeight = 35;
        int buttonSpacing = 10;

        // Load Revenue button
        auto btnLoadRevenue = std::make_shared<UltraCanvasButton>("btnLoadRevenue", 3031,
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
        auto btnLoadSales = std::make_shared<UltraCanvasButton>("btnLoadSales", 3032,
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
        auto btnLoadRandom = std::make_shared<UltraCanvasButton>("btnLoadRandom", 3033,
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

        // Toggle Data Points button
        buttonX += buttonWidth + buttonSpacing;
        auto btnTogglePoints = std::make_shared<UltraCanvasButton>("btnTogglePoints", 3034,
                                                                   buttonX, buttonY, buttonWidth, buttonHeight);
        btnTogglePoints->SetText("Toggle Points");
        btnTogglePoints->SetOnClick([areaChart]() {
            chartControl.showPoints = !chartControl.showPoints;
            areaChart->SetShowDataPoints(chartControl.showPoints);
            //areaChart->Invalidate();
        });
        container->AddChild(btnTogglePoints);

        // Toggle Smoothing button
        buttonX += buttonWidth + buttonSpacing;
        auto btnToggleSmoothing = std::make_shared<UltraCanvasButton>("btnToggleSmoothing", 3035,
                                                                      buttonX, buttonY, buttonWidth, buttonHeight);
        btnToggleSmoothing->SetText("Toggle Smooth");
        btnToggleSmoothing->SetOnClick([areaChart]() {
            chartControl.smoothingEnabled = !chartControl.smoothingEnabled;
            areaChart->SetSmoothingEnabled(chartControl.smoothingEnabled);
            //areaChart->Invalidate();
        });
        container->AddChild(btnToggleSmoothing);

        buttonX += buttonWidth + buttonSpacing;
        auto btnToggleValueLabels = std::make_shared<UltraCanvasButton>("btnToggleValueLabels", 3005,
                                                                        buttonX, buttonY, buttonWidth, buttonHeight);
        btnToggleValueLabels->SetText("Toggle Labels");
        btnToggleValueLabels->onClick = [areaChart]()  {
            chartControl.valueLabelsEnabled = !chartControl.valueLabelsEnabled;
            areaChart->SetShowValueLabels(chartControl.valueLabelsEnabled);
            //lineChart->Invalidate();
        };
        container->AddChild(btnToggleValueLabels);

        return container;
    }

} // namespace UltraCanvas
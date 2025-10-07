// Apps/DemoApp/UltraCanvasButtonDemo.cpp
// Implementation of all component example creators
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
//#include "UltraCanvasButton3Sections.h"
#include "Plugins/Charts/UltraCanvasSpecificChartElements.h"
#include <sstream>
#include <random>
#include <map>

namespace UltraCanvas {

// ===== BASIC UI ELEMENTS =====


std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateLineChartsExamples() {
    std::shared_ptr<UltraCanvasLineChartElement> lineChart;

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

        performanceData = std::make_shared<ChartDataVector>();
        std::vector<ChartDataPoint> performance = {
                ChartDataPoint(1, 85, 0, "Q1 2024", 85),
                ChartDataPoint(2, 92, 0, "Q2 2024", 92),
                ChartDataPoint(3, 78, 0, "Q3 2024", 78),
                ChartDataPoint(4, 95, 0, "Q4 2024", 95),
                ChartDataPoint(5, 88, 0, "Q1 2025", 88)
        };
        performanceData->LoadFromArray(performance);

        auto container = std::make_shared<UltraCanvasContainer>("LineChartExamples", 100, 0, 0, 1000, 700);

        lineChart = CreateLineChartElement("salesLineChart", 1001, 50, 50, 500, 300);


        lineChart->SetDataSource(salesData);
        //lineChart->SetChartTitle("Monthly Sales Trend");
        lineChart->SetLineColor(Color(0, 102, 204, 255));       // Blue line
        lineChart->SetLineWidth(3.0f);
        lineChart->SetShowDataPoints(true);
        lineChart->SetPointColor(Color(255, 99, 71, 255));      // Tomato red points
        lineChart->SetPointRadius(5.0f);
        lineChart->SetSmoothingEnabled(true);
        lineChart->SetEnableTooltips(true);
        lineChart->SetEnableZoom(true);
        lineChart->SetEnablePan(true);

        container->AddChild(lineChart);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBarChartsExamples() {
            std::shared_ptr<UltraCanvasBarChartElement> barChart;

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


            auto container = std::make_shared<UltraCanvasContainer>("LineChartExamples", 100, 0, 0, 1000, 700);

            barChart = CreateBarChartElement("performanceBarChart", 1002, 50, 50, 500, 300);

            // Configure Bar Chart
            barChart->SetDataSource(performanceData);
            barChart->SetChartTitle("Quarterly Performance");
            barChart->SetBarColor(Color(60, 179, 113, 255));        // Medium sea green
            barChart->SetBarBorderColor(Color(34, 139, 34, 255));   // Forest green border
            barChart->SetBarBorderWidth(2.0f);
            barChart->SetBarSpacing(0.2f);                          // 20% spacing between bars
            barChart->SetEnableTooltips(true);


            container->AddChild(barChart);

            return container;
    }


    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateScatterPlotChartsExamples() {
        std::shared_ptr<UltraCanvasScatterPlotElement> scatterPlot;

        // Sample data sources
        std::shared_ptr<ChartDataVector> salesData;
        std::shared_ptr<ChartDataVector> performanceData;
        std::shared_ptr<ChartDataVector> correlationData;
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

        auto container = std::make_shared<UltraCanvasContainer>("LineChartExamples", 100, 0, 0, 1000, 700);

        // Create scatter plot for correlation analysis
        scatterPlot = CreateScatterPlotElement("correlationScatter", 1003, 50, 50, 500, 300);


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

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateAreaChartsExamples() {
        std::shared_ptr<UltraCanvasAreaChartElement> areaChart;

        // Sample data sources
        std::shared_ptr<ChartDataVector> salesData;
        std::shared_ptr<ChartDataVector> performanceData;
        std::shared_ptr<ChartDataVector> correlationData;
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

        // Instruction labels - replacing direct text drawing
        auto container = std::make_shared<UltraCanvasContainer>("LineChartExamples", 100, 0, 0, 1000, 700);

        areaChart = CreateAreaChartElement("revenueAreaChart", 1004, 50, 50, 500, 300);


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

        areaChart->SetSmoothingEnabled(true);
        areaChart->SetEnableTooltips(true);
        areaChart->SetEnableZoom(true);
        areaChart->SetEnablePan(true);

        container->AddChild(areaChart);

        return container;
    }
} // namespace UltraCanvas
// Apps/UltraCanvasChartExample.cpp
// Example demonstrating usage of UltraCanvasLineChartElement, UltraCanvasBarChartElement, and UltraCanvasScatterPlotElement
// Version: 1.0.2
// Last Modified: 2025-09-14
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasSpecificChartElements.h"
#include "Plugins/Charts/UltraCanvasChartDataStructures.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasApplication.h"
#include <memory>
#include <vector>
#include <random>

using namespace UltraCanvas;

class ChartExampleApplication {
private:
    UltraCanvasApplication *ucApp;
    std::shared_ptr<UltraCanvasWindow> mainWindow;
    std::shared_ptr<UltraCanvasLineChartElement> lineChart;
    std::shared_ptr<UltraCanvasBarChartElement> barChart;
    std::shared_ptr<UltraCanvasScatterPlotElement> scatterPlot;
    std::shared_ptr<UltraCanvasAreaChartElement> areaChart;

    // Sample data sources
    std::shared_ptr<ChartDataVector> salesData;
    std::shared_ptr<ChartDataVector> performanceData;
    std::shared_ptr<ChartDataVector> correlationData;
    std::shared_ptr<ChartDataVector> revenueData;

    // Instruction labels - replacing direct text drawing
    std::shared_ptr<UltraCanvasLabel> instructionLabel;
public:
    ChartExampleApplication() {
        ucApp = new UltraCanvasApplication();
        ucApp->Initialize();
        InitializeWindow();
        CreateSampleData();
        CreateChartElements();
        ConfigureCharts();
        CreateInstructionLabels();
        LayoutCharts();
    }

    void InitializeWindow() {
        WindowConfig config;
        config.title = "UltraCanvas Charts Demo";
        config.width = 1200;
        config.height = 900;
        config.resizable = true;
        config.type = WindowType::Standard;
        config.x = 100;  // Add explicit position
        config.y = 100;

        mainWindow = std::make_shared<UltraCanvasWindow>(config);
        //mainWindow->SetBackgroundColor(Color(240, 240, 240, 255));
    }

    void CreateSampleData() {
        // CREATE REVENUE DATA FOR AREA CHART (ADD THIS NEW SECTION)
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
    }

    void CreateInstructionLabels() {
        std::string instructions =  "Chart Controls:\n"
//                "R - Reset all chart views\n"
                "S - Toggle line smoothing\n"
                "P - Toggle data points\n"
                "C - Cycle scatter plot shapes\n"
                "G - Enable/disable gradient\n"
                "Mouse: Drag to pan, wheel to zoom";

        instructionLabel = CreateLabel(
                "instruction_1",
                2000, 50, 720, 400, 20 * 6, instructions
        );
        instructionLabel->SetFont("Arial", 13.0f, FontWeight::Normal);
        instructionLabel->SetTextColor(Color(40, 40, 40, 255));

        instructionLabel->SetAlignment(TextAlignment::Left, TextVerticalAlignment::Middle);
        instructionLabel->SetBackgroundColor(Color(255, 255, 255, 200));
        instructionLabel->SetPadding(4.0f, 4.0f, 2.0f, 2.0f);
    }

    void CreateChartElements() {
        // Create line chart for sales trend
        lineChart = CreateLineChartElement("salesLineChart", 1001, 50, 50, 500, 300);
        
        // Create bar chart for performance metrics
        barChart = CreateBarChartElement("performanceBarChart", 1002, 600, 50, 500, 300);
        
        // Create scatter plot for correlation analysis
        scatterPlot = CreateScatterPlotElement("correlationScatter", 1003, 50, 400, 500, 300);

        areaChart = CreateAreaChartElement("revenueAreaChart", 1004, 600, 400, 500, 300);
    }

    void ConfigureCharts() {
        // Configure Line Chart
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

        // Configure Bar Chart
        barChart->SetDataSource(performanceData);
        barChart->SetChartTitle("Quarterly Performance");
        barChart->SetBarColor(Color(60, 179, 113, 255));        // Medium sea green
        barChart->SetBarBorderColor(Color(34, 139, 34, 255));   // Forest green border
        barChart->SetBarBorderWidth(2.0f);
        barChart->SetBarSpacing(0.2f);                          // 20% spacing between bars
        barChart->SetEnableTooltips(true);

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
    }

    void LayoutCharts() {
        // Add charts to main window
        mainWindow->AddChild(lineChart);
        mainWindow->AddChild(barChart);
        mainWindow->AddChild(scatterPlot);
        mainWindow->AddChild(areaChart);

        mainWindow->AddChild(instructionLabel);
    }

    void Run() {
        // Main application loop
        mainWindow->Show();
        ucApp->RegisterGlobalEventHandler([this](const UCEvent& event)-> bool {
            if (event.type == UCEventType::KeyDown) {
                HandleKeyboardInput(event);
            }
            return false;
        });
        ucApp->Run();
    }

private:
    void HandleKeyboardInput(const UCEvent& event) {
        switch (event.virtualKey) {
            case UCKeys::R:
                // Reset all chart zoom/pan
//                lineChart->ResetView();
//                barChart->ResetView();
//                scatterPlot->ResetView();
                break;
                
            case UCKeys::S:
                // Toggle smoothing on line chart
                static bool smoothingEnabled = true;
                smoothingEnabled = !smoothingEnabled;
                lineChart->SetSmoothingEnabled(smoothingEnabled);
                areaChart->SetSmoothingEnabled(smoothingEnabled);
                break;

            case UCKeys::G: {
                static bool gradientEnabled = true;
                gradientEnabled = !gradientEnabled;
                areaChart->SetFillGradientEnabled(gradientEnabled);
                std::cout << "Area chart gradient " << (gradientEnabled ? "enabled" : "disabled") << std::endl;
                break;
            }
            case UCKeys::U: {
                UpdateRevenueData();
                std::cout << "Revenue data updated with new values" << std::endl;
                break;
            }
            case UCKeys::P:
                // Toggle data points on line chart
                static bool pointsVisible = true;
                pointsVisible = !pointsVisible;
                lineChart->SetShowDataPoints(pointsVisible);
                areaChart->SetShowDataPoints(pointsVisible);
                break;
                
            case UCKeys::C:
                // Cycle through scatter plot point shapes
                static int shapeIndex = 0;
                const UltraCanvasScatterPlotElement::PointShape shapes[] = {
                    UltraCanvasScatterPlotElement::PointShape::Circle,
                    UltraCanvasScatterPlotElement::PointShape::Square,
                    UltraCanvasScatterPlotElement::PointShape::Triangle,
                    UltraCanvasScatterPlotElement::PointShape::Diamond
                };
                shapeIndex = (shapeIndex + 1) % 4;
                scatterPlot->SetPointShape(shapes[shapeIndex]);
                break;
        }
    }

//    void UpdateCharts() {
//        // Update any dynamic data or animations
//        // In this example, we could simulate real-time data updates
//
//        static int updateCounter = 0;
//        updateCounter++;
//
//        // Every 300 frames (about 5 seconds at 60fps), add some variance to demonstrate updates
//        if (updateCounter % 300 == 0) {
//            UpdateSalesDataWithRandomVariance();
//        }
//    }

    void UpdateRevenueData() {
        // Generate new revenue data with some variation
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> variance(-10000, 15000);

        std::vector<ChartDataPoint> newRevenue;
        double baseValue = 85000;

        for (int quarter = 1; quarter <= 8; ++quarter) {
            baseValue += 5000 + variance(gen); // Growth trend with noise
            baseValue = std::max(baseValue, 50000.0); // Don't go below 50k

            std::string label = (quarter <= 4) ?
                                ("Q" + std::to_string(quarter) + " 2023") :
                                ("Q" + std::to_string(quarter - 4) + " 2024");

            newRevenue.emplace_back(quarter, baseValue, 0, label, baseValue);
        }

        auto newRevenueData = std::make_shared<ChartDataVector>();
        newRevenueData->LoadFromArray(newRevenue);
        areaChart->SetDataSource(newRevenueData);
        revenueData = newRevenueData;
    }

    void UpdateSalesDataWithRandomVariance() {
        // Add small random variance to simulate real-time updates
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> variance(-2000, 2000);
        
        // Create updated sales data
        std::vector<ChartDataPoint> updatedSales;
        for (size_t i = 0; i < salesData->GetPointCount(); ++i) {
            auto originalPoint = salesData->GetPoint(i);
            double newValue = originalPoint.y + variance(gen);
            updatedSales.emplace_back(originalPoint.x, newValue, 0, 
                                    originalPoint.label, newValue);
        }
        
        // Update the data source
        auto newSalesData = std::make_shared<ChartDataVector>();
        newSalesData->LoadFromArray(updatedSales);
        lineChart->SetDataSource(newSalesData);
        salesData = newSalesData;
    }

//    void RenderCharts() {
//        // Clear the background
//        auto renderContext = mainWindow->GetRenderContext();
//        renderContext->Clear(Color(240, 240, 240, 255));
//
//        // Render all chart elements
//        lineChart->Render();
//        barChart->Render();
//        scatterPlot->Render();
//
//        // Draw instructions
//        DrawInstructions(renderContext);
//    }

//    void DrawInstructions(IRenderContext* ctx) {
//        ctx->SetTextColor(Color(0, 0, 0, 255));
//        ctx->SetFont("Arial", 12.0f);
//
//        std::vector<std::string> instructions = {
//            "Chart Controls:",
//            "R - Reset all chart views",
//            "S - Toggle line smoothing",
//            "P - Toggle data points",
//            "C - Cycle scatter plot shapes",
//            "Mouse: Drag to pan, wheel to zoom"
//        };
//
//        int y = 750;
//        for (const auto& instruction : instructions) {
//            ctx->DrawText(instruction, 50, y);
//            y += 15;
//        }
//    }
};

// =============================================================================
// MAIN APPLICATION ENTRY POINT
// =============================================================================

int main() {
    try {
        // Initialize UltraCanvas framework
        // Create and run the chart example application
        ChartExampleApplication app;
        app.Run();
        
        // Cleanup

        return 0;
    }
    catch (const std::exception& e) {
        // Handle any initialization or runtime errors
        std::cerr << "Application error: " << e.what() << std::endl;
        return -1;
    }
}

// =============================================================================
// ADDITIONAL HELPER FUNCTIONS FOR ADVANCED USAGE
// =============================================================================

namespace ChartExampleHelpers {
    
    // Helper function to create a line chart with custom styling
    std::shared_ptr<UltraCanvasLineChartElement> CreateStyledLineChart(
        const std::string& id, long uid, int x, int y, int width, int height,
        const Color& lineColor, float lineWidth, bool showPoints = false) {
        
        auto chart = CreateLineChartElement(id, uid, x, y, width, height);
        chart->SetLineColor(lineColor);
        chart->SetLineWidth(lineWidth);
        chart->SetShowDataPoints(showPoints);
        
        if (showPoints) {
            chart->SetPointColor(lineColor);
            chart->SetPointRadius(4.0f);
        }
        
        return chart;
    }
    
    // Helper function to create a themed bar chart
    std::shared_ptr<UltraCanvasBarChartElement> CreateThemedBarChart(
        const std::string& id, long uid, int x, int y, int width, int height,
        const Color& barColor, const Color& borderColor) {
        
        auto chart = CreateBarChartElement(id, uid, x, y, width, height);
        chart->SetBarColor(barColor);
        chart->SetBarBorderColor(borderColor);
        chart->SetBarBorderWidth(1.5f);
        chart->SetBarSpacing(0.15f);
        
        return chart;
    }
    
    // Helper function to create a customized scatter plot
    std::shared_ptr<UltraCanvasScatterPlotElement> CreateCustomScatterPlot(
        const std::string& id, long uid, int x, int y, int width, int height,
        const Color& pointColor, float pointSize,
        UltraCanvasScatterPlotElement::PointShape shape) {
        
        auto chart = CreateScatterPlotElement(id, uid, x, y, width, height);
        chart->SetPointColor(pointColor);
        chart->SetPointSize(pointSize);
        chart->SetPointShape(shape);
        chart->SetEnableZoom(true);
        chart->SetEnablePan(true);
        chart->SetEnableSelection(true);
        
        return chart;
    }
}
// WaterfallChart_ShortDemo.cpp
// Short waterfall chart demo for integration with UltraCanvas Demo App
// Version: 1.0.0
// Last Modified: 2025-09-29
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasWaterfallChart.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDemo.h"
#include <memory>
#include <random>

namespace UltraCanvas {

// =============================================================================
// SHORT DEMO: CREATE WATERFALL CHART EXAMPLES
// =============================================================================

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateWaterfallChartExamples() {
        // Main container
        auto container = std::make_shared<UltraCanvasContainer>(
                "WaterfallChartDemo", 3000, 0, 0, 1000, 780
        );

        // Title label
        auto titleLabel = std::make_shared<UltraCanvasLabel>(
                "WaterfallTitle", 3001, 20, 10, 960, 30
        );
        titleLabel->SetText("Waterfall Chart Examples");
        titleLabel->SetFontSize(16);
        titleLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(titleLabel);

        // =============================================================================
        // EXAMPLE 1: REVENUE FLOW ANALYSIS
        // =============================================================================

        auto revenueData = std::make_shared<WaterfallChartDataVector>();
        revenueData->AddWaterfallPoint("Start", 500.0);
        revenueData->AddWaterfallPoint("Q1", 150.0);
        revenueData->AddWaterfallPoint("Q2", 120.0);
        revenueData->AddWaterfallPoint("Q3", 180.0);
        revenueData->AddWaterfallPoint("Q4", 90.0);
        revenueData->AddWaterfallPoint("Return", -25.0);
        revenueData->AddWaterfallPoint("Discount", -40.0);
        revenueData->AddWaterfallPoint("Total", 0.0, true, false);

        auto revenueChart = CreateWaterfallChartWithData(
                "revenue_chart", 3010, 10, 50, 500, 300,
                revenueData, "Revenue Flow ($K)"
        );

        // Style revenue chart
        revenueChart->SetPositiveBarColor(Color(76, 175, 80, 255));    // Green
        revenueChart->SetNegativeBarColor(Color(244, 67, 54, 255));    // Red
        revenueChart->SetTotalBarColor(Color(33, 150, 243, 255));      // Blue
        revenueChart->SetBarStyle(UltraCanvasWaterfallChartElement::BarStyle::Standard);
        revenueChart->SetConnectionLineStyle(
                UltraCanvasWaterfallChartElement::ConnectionStyle::Solid
        );
        revenueChart->SetShowValueLabels(true);

        container->AddChild(revenueChart);

        // =============================================================================
        // EXAMPLE 2: CASH FLOW WITH SUBTOTALS
        // =============================================================================

        auto cashFlowData = std::make_shared<WaterfallChartDataVector>();
        cashFlowData->AddWaterfallPoint("Start", 100.0);
        cashFlowData->AddWaterfallPoint("Revenue", 1200.0);
        cashFlowData->AddWaterfallPoint("Costs", -800.0);
        cashFlowData->AddWaterfallPoint("Q1", 0.0, true, false);  // Subtotal
        cashFlowData->AddWaterfallPoint("Marketing", -150.0);
        cashFlowData->AddWaterfallPoint("R&D", -120.0);
        cashFlowData->AddWaterfallPoint("Q2", 0.0, true, false);  // Subtotal
        cashFlowData->AddWaterfallPoint("Equipment", -80.0);
        cashFlowData->AddWaterfallPoint("Final", 0.0, false, true);

        auto cashFlowChart = CreateWaterfallChartWithData(
                "cashflow_chart", 3020, 480, 50, 540, 300,
                cashFlowData, "Cash Flow ($K)"
        );

        // Style cash flow chart
        cashFlowChart->SetPositiveBarColor(Color(46, 125, 50, 255));
        cashFlowChart->SetNegativeBarColor(Color(211, 47, 47, 255));
        cashFlowChart->SetSubtotalBarColor(Color(255, 193, 7, 255));  // Amber
        cashFlowChart->SetTotalBarColor(Color(63, 81, 181, 255));
        cashFlowChart->SetBarStyle(UltraCanvasWaterfallChartElement::BarStyle::Rounded);
        cashFlowChart->SetConnectionLineStyle(
                UltraCanvasWaterfallChartElement::ConnectionStyle::Solid,
                Color(117, 117, 117, 255), 2.0f
        );

        container->AddChild(cashFlowChart);

        // =============================================================================
        // EXAMPLE 3: PERFORMANCE IMPACT (WITH GRADIENT STYLE)
        // =============================================================================

        auto performanceData = std::make_shared<WaterfallChartDataVector>();
        performanceData->AddWaterfallPoint("Start", 100.0);
        performanceData->AddWaterfallPoint("Training", 25.0);
        performanceData->AddWaterfallPoint("Tools", 15.0);
        performanceData->AddWaterfallPoint("Process", 12.0);
        performanceData->AddWaterfallPoint("Turnover", -18.0);
        performanceData->AddWaterfallPoint("Downtime", -8.0);
        performanceData->AddWaterfallPoint("Final", 0.0, false, true);

        auto performanceChart = CreateWaterfallChartWithData(
                "performance_chart", 3030, 10, 340, 520, 300,
                performanceData, "Performance Impact"
        );

        // Style performance chart with gradient
        performanceChart->SetPositiveBarColor(Color(104, 159, 56, 255));
        performanceChart->SetNegativeBarColor(Color(229, 115, 115, 255));
        performanceChart->SetTotalBarColor(Color(121, 85, 72, 255));
        performanceChart->SetBarStyle(UltraCanvasWaterfallChartElement::BarStyle::Gradient);
        performanceChart->SetConnectionLineStyle(
                UltraCanvasWaterfallChartElement::ConnectionStyle::Solid,
                Color(69, 90, 100, 255), 1.5f
        );
        performanceChart->SetShowCumulativeLabels(false);

        container->AddChild(performanceChart);

        // =============================================================================
        // INTERACTIVE CONTROLS
        // =============================================================================

        // Style toggle button
        auto btnToggleStyle = std::make_shared<UltraCanvasButton>(
                "btnToggleStyle", 3040, 520, 380, 200, 40
        );
        btnToggleStyle->SetText("Toggle Bar Style");

        static int styleIndex = 0;
        btnToggleStyle->onClick = [revenueChart, cashFlowChart, performanceChart]() {
            styleIndex = (styleIndex + 1) % 3;

            UltraCanvasWaterfallChartElement::BarStyle newStyle;
            switch (styleIndex) {
                case 0: newStyle = UltraCanvasWaterfallChartElement::BarStyle::Standard; break;
                case 1: newStyle = UltraCanvasWaterfallChartElement::BarStyle::Rounded; break;
                case 2: newStyle = UltraCanvasWaterfallChartElement::BarStyle::Gradient; break;
            }

            revenueChart->SetBarStyle(newStyle);
            cashFlowChart->SetBarStyle(newStyle);
            performanceChart->SetBarStyle(newStyle);
        };

        container->AddChild(btnToggleStyle);

        // Random data button
        auto btnRandomData = std::make_shared<UltraCanvasButton>(
                "btnRandomData", 3041, 520, 430, 200, 40
        );
        btnRandomData->SetText("Generate Random Data");

        btnRandomData->onClick = [revenueData]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<> dist(50.0, 200.0);

            // Update revenue data
            revenueData->ClearData();
            revenueData->AddWaterfallPoint("Start", 400.0 + dist(gen));
            revenueData->AddWaterfallPoint("Q1", dist(gen));
            revenueData->AddWaterfallPoint("Q2", dist(gen));
            revenueData->AddWaterfallPoint("Q3", dist(gen));
            revenueData->AddWaterfallPoint("Q4", dist(gen));
            revenueData->AddWaterfallPoint("Returns", -dist(gen) * 0.3);
            revenueData->AddWaterfallPoint("Discounts", -dist(gen) * 0.4);
            revenueData->AddWaterfallPoint("Total", 0.0, false, true);
        };

        container->AddChild(btnRandomData);

        // Info label
        auto infoLabel = std::make_shared<UltraCanvasLabel>(
                "WaterfallInfo", 3050, 520, 490, 460, 190
        );
        infoLabel->SetText(
                "Waterfall Chart Features:\n\n"
                "• Revenue flow visualization\n"
                "• Cash flow with subtotals\n"
                "• Performance impact tracking\n"
                "• Positive/negative coloring\n"
                "• Multiple bar styles\n"
                "• Connection lines\n"
                "• Interactive tooltips\n"
                "• Value labels\n\n"
                "Hover over bars for details!"
        );
        infoLabel->SetFontSize(11);
        infoLabel->SetTextColor(Color(60, 60, 60, 255));
        infoLabel->SetBackgroundColor(Color(245, 245, 245, 255));
        infoLabel->SetBorders(1.0f, Color(200, 200, 200, 255));
        infoLabel->SetPadding(10.0f);

        container->AddChild(infoLabel);

        return container;
    }

} // namespace UltraCanvas
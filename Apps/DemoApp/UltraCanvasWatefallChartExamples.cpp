// WaterfallChart_ShortDemo.cpp
// Short waterfall chart demo for integration with UltraCanvas Demo App
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per chart
//                  in a wrapping row, the buttons and the feature list in a
//                  card beside them, everything sized to its text
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasWaterfallChart.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include <memory>
#include <random>

namespace UltraCanvas {

// =============================================================================
// SHORT DEMO: CREATE WATERFALL CHART EXAMPLES
// =============================================================================

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateWaterfallChartExamples() {
        auto container = DemoKit::CreatePage("WaterfallChartDemo", Tr("Waterfall Chart Examples"), "");

        // The charts side by side while they fit, each card as wide as its
        // chart at least.
        auto chartRow = DemoKit::AddRow(container, "WaterfallCharts");

        // =============================================================================
        // EXAMPLE 1: REVENUE FLOW ANALYSIS
        // =============================================================================

        auto revenueData = std::make_shared<WaterfallChartDataVector>();
        revenueData->AddWaterfallPoint(Tr("Start"), 500.0);
        revenueData->AddWaterfallPoint(Tr("Q1"), 150.0);
        revenueData->AddWaterfallPoint(Tr("Q2"), 120.0);
        revenueData->AddWaterfallPoint(Tr("Q3"), 180.0);
        revenueData->AddWaterfallPoint(Tr("Q4"), 90.0);
        revenueData->AddWaterfallPoint(Tr("Return"), -25.0);
        revenueData->AddWaterfallPoint(Tr("Discount"), -40.0);
        revenueData->AddWaterfallPoint(Tr("Total"), 0.0, true, false);

        auto revenueChart = CreateWaterfallChartWithData(
                "revenue_chart", 0, 0, 500, 300,
                revenueData, Tr("Revenue Flow ($K)")
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

        auto revenueCard = DemoKit::AddCard(chartRow, "WaterfallRevenueCard", "", "",
                                            500 + 2 * DemoKit::kCardPadding);
        DemoKit::AddFixed(revenueCard, revenueChart);

        // =============================================================================
        // EXAMPLE 2: CASH FLOW WITH SUBTOTALS
        // =============================================================================

        auto cashFlowData = std::make_shared<WaterfallChartDataVector>();
        cashFlowData->AddWaterfallPoint(Tr("Start"), 100.0);
        cashFlowData->AddWaterfallPoint(Tr("Revenue"), 1200.0);
        cashFlowData->AddWaterfallPoint(Tr("Costs"), -800.0);
        cashFlowData->AddWaterfallPoint(Tr("Q1"), 0.0, true, false);  // Subtotal
        cashFlowData->AddWaterfallPoint(Tr("Marketing"), -150.0);
        cashFlowData->AddWaterfallPoint(Tr("R&D"), -120.0);
        cashFlowData->AddWaterfallPoint(Tr("Q2"), 0.0, true, false);  // Subtotal
        cashFlowData->AddWaterfallPoint(Tr("Equipment"), -80.0);
        cashFlowData->AddWaterfallPoint(Tr("Final"), 0.0, false, true);

        auto cashFlowChart = CreateWaterfallChartWithData(
                "cashflow_chart", 0, 0, 540, 300,
                cashFlowData, Tr("Cash Flow ($K)")
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

        auto cashFlowCard = DemoKit::AddCard(chartRow, "WaterfallCashFlowCard", "", "",
                                             540 + 2 * DemoKit::kCardPadding);
        DemoKit::AddFixed(cashFlowCard, cashFlowChart);

        // =============================================================================
        // EXAMPLE 3: PERFORMANCE IMPACT (WITH GRADIENT STYLE)
        // =============================================================================

        auto performanceData = std::make_shared<WaterfallChartDataVector>();
        performanceData->AddWaterfallPoint(Tr("Start"), 100.0);
        performanceData->AddWaterfallPoint(Tr("Training"), 25.0);
        performanceData->AddWaterfallPoint(Tr("Tools"), 15.0);
        performanceData->AddWaterfallPoint(Tr("Process"), 12.0);
        performanceData->AddWaterfallPoint(Tr("Turnover"), -18.0);
        performanceData->AddWaterfallPoint(Tr("Downtime"), -8.0);
        performanceData->AddWaterfallPoint(Tr("Final"), 0.0, false, true);

        auto performanceChart = CreateWaterfallChartWithData(
                "performance_chart", 0, 0, 520, 300,
                performanceData, Tr("Performance Impact")
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

        auto performanceCard = DemoKit::AddCard(chartRow, "WaterfallPerformanceCard", "", "",
                                                520 + 2 * DemoKit::kCardPadding);
        DemoKit::AddFixed(performanceCard, performanceChart);

        // =============================================================================
        // INTERACTIVE CONTROLS
        // =============================================================================

        // Buttons as wide as their text, at least the 200 px they had.
        CSSLayout::BoxConstraints buttonLimits;
        buttonLimits.minWidth = CSSLayout::Dimension::Px(200);
        auto controlsCard = DemoKit::AddCard(chartRow, "WaterfallControlsCard");
        auto buttonRow = DemoKit::AddRow(controlsCard, "WaterfallButtons", 10);

        // Style toggle button
        auto btnToggleStyle = std::make_shared<UltraCanvasButton>(
                "btnToggleStyle", 0, 0, 0, 40, Tr("Cycle Bar Style")
        );
        btnToggleStyle->boxConstraints = buttonLimits;

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

        DemoKit::Add(buttonRow, btnToggleStyle);

        // Random data button
        auto btnRandomData = std::make_shared<UltraCanvasButton>(
                "btnRandomData", 0, 0, 0, 40, Tr("Generate Random Data")
        );
        btnRandomData->boxConstraints = buttonLimits;

        btnRandomData->onClick = [revenueData]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<> dist(50.0, 200.0);

            // Update revenue data
            revenueData->ClearData();
            revenueData->AddWaterfallPoint(Tr("Start"), 400.0 + dist(gen));
            revenueData->AddWaterfallPoint(Tr("Q1"), dist(gen));
            revenueData->AddWaterfallPoint(Tr("Q2"), dist(gen));
            revenueData->AddWaterfallPoint(Tr("Q3"), dist(gen));
            revenueData->AddWaterfallPoint(Tr("Q4"), dist(gen));
            revenueData->AddWaterfallPoint(Tr("Returns"), -dist(gen) * 0.3);
            revenueData->AddWaterfallPoint(Tr("Discounts"), -dist(gen) * 0.4);
            revenueData->AddWaterfallPoint(Tr("Total"), 0.0, false, true);
        };

        DemoKit::Add(buttonRow, btnRandomData);

        // Info
        DemoKit::AddHeading(controlsCard, "WaterfallInfoTitle", Tr("Waterfall Chart Features:"));
        DemoKit::Add(controlsCard, DemoKit::CreateBullets("WaterfallInfo", {
                Tr("Revenue flow visualization"),
                Tr("Cash flow with subtotals"),
                Tr("Performance impact tracking"),
                Tr("Positive/negative coloring"),
                Tr("Multiple bar styles"),
                Tr("Connection lines"),
                Tr("Interactive tooltips"),
                Tr("Value labels")}));
        DemoKit::AddCaption(controlsCard, "WaterfallInfoHint", Tr("Hover over bars for details!"));

        return container;
    }

} // namespace UltraCanvas
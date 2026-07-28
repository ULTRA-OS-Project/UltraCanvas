// Apps/DemoApp/UltraCanvasMekkoChartExamples.cpp
// Mekko (Marimekko) chart demo for the UltraCanvas Demo App
// Version: 1.0.0
// Last Modified: 2026-07-28
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasMekkoChart.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDemo.h"
#include <memory>
#include <random>

namespace UltraCanvas {

// =============================================================================
// DEMO: CREATE MEKKO CHART EXAMPLES
// =============================================================================

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMekkoChartExamples() {
        auto container = std::make_shared<UltraCanvasContainer>(
                "MekkoChartDemo", 0, 0, 1040, 780
        );

        auto titleLabel = std::make_shared<UltraCanvasLabel>(
                "MekkoTitle", 20, 10, 960, 30
        );
        titleLabel->SetText("Mekko (Marimekko) Chart Examples");
        titleLabel->SetFontSize(16);
        titleLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(titleLabel);

        // =============================================================================
        // EXAMPLE 1: MARKET SHARE BY SECTOR (CLASSIC MARIMEKKO)
        // =============================================================================

        auto marketData = std::make_shared<MekkoChartDataVector>();
        marketData->AddSeries("USA", Color(25, 60, 130, 255));
        marketData->AddSeries("China", Color(235, 75, 105, 255));
        marketData->AddSeries("Europe", Color(65, 145, 225, 255));
        marketData->AddSeries("Japan", Color(250, 190, 40, 255));

        marketData->AddColumn("Technology",     {88, 44, 30, 21});
        marketData->AddColumn("Transportation", {52, 38, 34, 15});
        marketData->AddColumn("Energy",         {35, 42, 18, 10});
        marketData->AddColumn("Education",      {40, 30, 26, 16});
        marketData->AddColumn("Finance",        {95, 55, 48, 22});
        marketData->AddColumn("Tourism",        {41, 25, 51, 18});

        auto marketChart = CreateMekkoChartWithData(
                "mekko_market_chart", 10, 50, 520, 320,
                marketData, "Market Share by Sector ($B)"
        );
        marketChart->SetMekkoMode(UltraCanvasMekkoChartElement::MekkoMode::Marimekko);
        marketChart->SetColumnHeaderMode(UltraCanvasMekkoChartElement::ColumnHeaderMode::WidthPercent);
        marketChart->SetSegmentLabelMode(UltraCanvasMekkoChartElement::SegmentLabelMode::Value);
        marketChart->SetLegendPosition(UltraCanvasMekkoChartElement::LegendPosition::Right);
        marketChart->SetLegendWidth(90);
        marketChart->SetValueUnits("$", "B");
        container->AddChild(marketChart);

        // =============================================================================
        // EXAMPLE 2: PRODUCT vs REGION SALES MIX (PERCENT LABELS, SORTED)
        // =============================================================================

        auto salesData = std::make_shared<MekkoChartDataVector>();
        // No explicit colors: the chart assigns its default palette automatically
        salesData->AddSeries("USA");
        salesData->AddSeries("China");
        salesData->AddSeries("UK");
        salesData->AddSeries("Germany");
        salesData->AddSeries("France");
        salesData->AddSeries("India");

        salesData->AddColumn("Product A", {97, 14, 63, 97, 26, 49});
        salesData->AddColumn("Product B", {97, 87, 67, 66, 29, 84});
        salesData->AddColumn("Product C", {45, 54, 18, 24, 84, 36});
        salesData->AddColumn("Product D", {61, 32, 66, 47, 23, 50});

        auto salesChart = CreateMekkoChartWithData(
                "mekko_sales_chart", 540, 50, 490, 320,
                salesData, "Sales Mix: Product vs Region"
        );
        salesChart->SetMekkoMode(UltraCanvasMekkoChartElement::MekkoMode::Marimekko);
        salesChart->SetColumnHeaderMode(UltraCanvasMekkoChartElement::ColumnHeaderMode::TotalAndPercent);
        salesChart->SetSegmentLabelMode(UltraCanvasMekkoChartElement::SegmentLabelMode::Percent);
        salesChart->SetColumnSortMode(UltraCanvasMekkoChartElement::ColumnSortMode::TotalDescending);
        salesChart->SetLegendPosition(UltraCanvasMekkoChartElement::LegendPosition::Right);
        salesChart->SetLegendWidth(90);
        salesChart->SetShowCumulativeXAxis(true);
        container->AddChild(salesChart);

        // =============================================================================
        // EXAMPLE 3: CLOUD PROVIDER REVENUE (BAR-MEKKO, ABSOLUTE HEIGHTS)
        // =============================================================================

        auto cloudData = std::make_shared<MekkoChartDataVector>();
        cloudData->AddSeries("Compute", Color(45, 165, 130, 255));
        cloudData->AddSeries("Storage", Color(65, 145, 225, 255));
        cloudData->AddSeries("AI Services", Color(155, 100, 220, 255));
        cloudData->AddSeries("Other", Color(90, 110, 130, 255));

        cloudData->AddColumn("Provider 1", {36, 22, 18, 14});
        cloudData->AddColumn("Provider 2", {28, 17, 21, 9});
        cloudData->AddColumn("Provider 3", {19, 12, 15, 6});
        cloudData->AddColumn("Provider 4", {9, 6, 4, 3});
        cloudData->AddColumn("Provider 5", {5, 4, 3, 2});

        auto cloudChart = CreateMekkoChartWithData(
                "mekko_cloud_chart", 10, 390, 520, 320,
                cloudData, "Cloud Revenue by Provider ($B)"
        );
        cloudChart->SetMekkoMode(UltraCanvasMekkoChartElement::MekkoMode::BarMekko);
        cloudChart->SetColumnHeaderMode(UltraCanvasMekkoChartElement::ColumnHeaderMode::Total);
        cloudChart->SetSegmentLabelMode(UltraCanvasMekkoChartElement::SegmentLabelMode::Value);
        cloudChart->SetColumnSortMode(UltraCanvasMekkoChartElement::ColumnSortMode::TotalDescending);
        cloudChart->SetLegendPosition(UltraCanvasMekkoChartElement::LegendPosition::Bottom);
        cloudChart->SetValueUnits("$", "B");
        container->AddChild(cloudChart);

        // =============================================================================
        // INTERACTIVE CONTROLS
        // =============================================================================

        auto btnToggleMode = std::make_shared<UltraCanvasButton>(
                "btnMekkoToggleMode", 560, 390, 220, 36
        );
        btnToggleMode->SetText("Toggle Marimekko / Bar-Mekko");
        btnToggleMode->onClick = [marketChart, salesChart]() {
            auto newMode = (marketChart->GetMekkoMode() == UltraCanvasMekkoChartElement::MekkoMode::Marimekko)
                           ? UltraCanvasMekkoChartElement::MekkoMode::BarMekko
                           : UltraCanvasMekkoChartElement::MekkoMode::Marimekko;
            marketChart->SetMekkoMode(newMode);
            salesChart->SetMekkoMode(newMode);
        };
        container->AddChild(btnToggleMode);

        auto btnToggleLabels = std::make_shared<UltraCanvasButton>(
                "btnMekkoToggleLabels", 560, 434, 220, 36
        );
        btnToggleLabels->SetText("Cycle Segment Labels");
        btnToggleLabels->onClick = [marketChart, salesChart, cloudChart]() {
            static int labelIndex = 0;
            labelIndex = (labelIndex + 1) % 4;
            UltraCanvasMekkoChartElement::SegmentLabelMode mode;
            switch (labelIndex) {
                case 0: mode = UltraCanvasMekkoChartElement::SegmentLabelMode::Value; break;
                case 1: mode = UltraCanvasMekkoChartElement::SegmentLabelMode::Percent; break;
                case 2: mode = UltraCanvasMekkoChartElement::SegmentLabelMode::ValueAndPercent; break;
                default: mode = UltraCanvasMekkoChartElement::SegmentLabelMode::NoLabels; break;
            }
            marketChart->SetSegmentLabelMode(mode);
            salesChart->SetSegmentLabelMode(mode);
            cloudChart->SetSegmentLabelMode(mode);
        };
        container->AddChild(btnToggleLabels);

        auto btnToggleSort = std::make_shared<UltraCanvasButton>(
                "btnMekkoToggleSort", 560, 478, 220, 36
        );
        btnToggleSort->SetText("Cycle Column Sorting");
        btnToggleSort->onClick = [marketChart]() {
            static int sortIndex = 0;
            sortIndex = (sortIndex + 1) % 4;
            UltraCanvasMekkoChartElement::ColumnSortMode mode;
            switch (sortIndex) {
                case 0: mode = UltraCanvasMekkoChartElement::ColumnSortMode::DataOrder; break;
                case 1: mode = UltraCanvasMekkoChartElement::ColumnSortMode::TotalDescending; break;
                case 2: mode = UltraCanvasMekkoChartElement::ColumnSortMode::TotalAscending; break;
                default: mode = UltraCanvasMekkoChartElement::ColumnSortMode::Alphabetical; break;
            }
            marketChart->SetColumnSortMode(mode);
        };
        container->AddChild(btnToggleSort);

        auto btnRandomData = std::make_shared<UltraCanvasButton>(
                "btnMekkoRandomData", 560, 522, 220, 36
        );
        btnRandomData->SetText("Generate Random Data");
        btnRandomData->onClick = [marketData, marketChart]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<> dist(10.0, 100.0);

            marketData->ClearData();
            marketData->AddSeries("USA", Color(25, 60, 130, 255));
            marketData->AddSeries("China", Color(235, 75, 105, 255));
            marketData->AddSeries("Europe", Color(65, 145, 225, 255));
            marketData->AddSeries("Japan", Color(250, 190, 40, 255));
            const char* sectors[] = {"Technology", "Transportation", "Energy",
                                     "Education", "Finance", "Tourism"};
            for (const char* sector : sectors) {
                marketData->AddColumn(sector, {dist(gen), dist(gen), dist(gen), dist(gen)});
            }
            // Data changed behind the element's back: force a layout rebuild
            marketChart->SetMekkoDataSource(marketData);
        };
        container->AddChild(btnRandomData);

        // Segment click feedback
        auto clickLabel = std::make_shared<UltraCanvasLabel>(
                "MekkoClickLabel", 560, 566, 460, 24
        );
        clickLabel->SetText("Click a segment to inspect it...");
        clickLabel->SetFontSize(11);
        clickLabel->SetTextColor(Color(90, 90, 90, 255));
        container->AddChild(clickLabel);

        auto onSegmentClick = [clickLabel](std::shared_ptr<MekkoChartDataVector> data) {
            return [clickLabel, data](size_t columnIndex, size_t seriesIndex) {
                const auto& column = data->GetColumn(columnIndex);
                double value = data->GetValue(columnIndex, seriesIndex);
                clickLabel->SetText("Selected: " + column.label + " / " +
                                    data->GetSeries(seriesIndex).name + " = " +
                                    std::to_string(static_cast<int>(value)));
            };
        };
        marketChart->SetOnSegmentClick(onSegmentClick(marketData));
        salesChart->SetOnSegmentClick(onSegmentClick(salesData));
        cloudChart->SetOnSegmentClick(onSegmentClick(cloudData));

        // Info label
        auto infoLabel = std::make_shared<UltraCanvasLabel>(
                "MekkoInfo", 560, 600, 460, 110
        );
        infoLabel->SetText(
                "Mekko Chart Features:\n"
                "• Column width proportional to column total, 100% stacked height\n"
                "• Marimekko (percent) and Bar-Mekko (absolute) modes\n"
                "• Column sorting, headers, cumulative % axis, legends\n"
                "• Value/percent segment labels with auto-contrast text\n"
                "• Interactive tooltips, hover highlight and click callbacks\n\n"
                "Hover over segments and legend entries!"
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

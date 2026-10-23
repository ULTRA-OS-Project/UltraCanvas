// Apps/DemoApp/UltraCanvasMekkoChartExamples.cpp
// Mekko (Marimekko) chart demo for the UltraCanvas Demo App
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  chart, the controls and features in a card beside the
//                  Bar-Mekko chart, buttons as wide as their text
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasMekkoChart.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include <memory>
#include <random>

namespace UltraCanvas {

namespace {

    // A card in a row asks for its chart's width (plus the card's padding and
    // border) before it wraps under its neighbour.
    constexpr float kChartCardChrome = 2 * DemoKit::kCardPadding + 2;

    // A button as wide as its text, at least the 220 px it had.
    std::shared_ptr<UltraCanvasButton> MakeMekkoButton(const std::string& id, const std::string& text) {
        auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, 36, text);
        CSSLayout::BoxConstraints limits;
        limits.minWidth = CSSLayout::Dimension::Px(220);
        button->boxConstraints = limits;
        return button;
    }

} // namespace

// =============================================================================
// DEMO: CREATE MEKKO CHART EXAMPLES
// =============================================================================

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMekkoChartExamples() {
        auto container = DemoKit::CreatePage("MekkoChartDemo", "Mekko (Marimekko) Chart Examples", "");
        auto topRow = DemoKit::AddRow(container, "MekkoTopRow");
        auto bottomRow = DemoKit::AddRow(container, "MekkoBottomRow");

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

        auto marketCard = DemoKit::AddCard(topRow, "MekkoMarketCard", "Classic Marimekko",
                "Column width is the sector's total, each column stacked to 100%",
                520 + kChartCardChrome);
        auto marketChart = CreateMekkoChartWithData(
                "mekko_market_chart", 0, 0, 520, 320,
                marketData, "Market Share by Sector ($B)"
        );
        marketChart->SetMekkoMode(UltraCanvasMekkoChartElement::MekkoMode::Marimekko);
        marketChart->SetColumnHeaderMode(UltraCanvasMekkoChartElement::ColumnHeaderMode::WidthPercent);
        marketChart->SetSegmentLabelMode(UltraCanvasMekkoChartElement::SegmentLabelMode::Value);
        marketChart->SetLegendPosition(UltraCanvasMekkoChartElement::LegendPosition::Right);
        marketChart->SetLegendWidth(90);
        marketChart->SetValueUnits("$", "B");
        DemoKit::AddFixed(marketCard, marketChart);

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

        auto salesCard = DemoKit::AddCard(topRow, "MekkoSalesCard", "Percent labels, sorted columns",
                "Default palette, columns sorted by total, cumulative % axis",
                490 + kChartCardChrome);
        auto salesChart = CreateMekkoChartWithData(
                "mekko_sales_chart", 0, 0, 490, 320,
                salesData, "Sales Mix: Product vs Region"
        );
        salesChart->SetMekkoMode(UltraCanvasMekkoChartElement::MekkoMode::Marimekko);
        salesChart->SetColumnHeaderMode(UltraCanvasMekkoChartElement::ColumnHeaderMode::TotalAndPercent);
        salesChart->SetSegmentLabelMode(UltraCanvasMekkoChartElement::SegmentLabelMode::Percent);
        salesChart->SetColumnSortMode(UltraCanvasMekkoChartElement::ColumnSortMode::TotalDescending);
        salesChart->SetLegendPosition(UltraCanvasMekkoChartElement::LegendPosition::Right);
        salesChart->SetLegendWidth(90);
        salesChart->SetShowCumulativeXAxis(true);
        DemoKit::AddFixed(salesCard, salesChart);

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

        auto cloudCard = DemoKit::AddCard(bottomRow, "MekkoCloudCard", "Bar-Mekko",
                "Absolute column heights, the legend below the chart",
                520 + kChartCardChrome);
        auto cloudChart = CreateMekkoChartWithData(
                "mekko_cloud_chart", 0, 0, 520, 320,
                cloudData, "Cloud Revenue by Provider ($B)"
        );
        cloudChart->SetMekkoMode(UltraCanvasMekkoChartElement::MekkoMode::BarMekko);
        cloudChart->SetColumnHeaderMode(UltraCanvasMekkoChartElement::ColumnHeaderMode::Total);
        cloudChart->SetSegmentLabelMode(UltraCanvasMekkoChartElement::SegmentLabelMode::Value);
        cloudChart->SetColumnSortMode(UltraCanvasMekkoChartElement::ColumnSortMode::TotalDescending);
        cloudChart->SetLegendPosition(UltraCanvasMekkoChartElement::LegendPosition::Bottom);
        cloudChart->SetValueUnits("$", "B");
        DemoKit::AddFixed(cloudCard, cloudChart);

        // =============================================================================
        // INTERACTIVE CONTROLS
        // =============================================================================

        auto controlsCard = DemoKit::AddCard(bottomRow, "MekkoControlsCard", "Interactive controls");

        // Mode switch for the two Marimekko charts (the cloud chart stays Bar-Mekko)
        auto swMode = UltraCanvasSwitch::CreateWithSideLabels(
                "swMekkoMode", 0, 0, "Marimekko", "Bar-Mekko", false
        );
        swMode->onStateChanged = [marketChart, salesChart](CheckedState, CheckedState newState) {
            auto newMode = (newState == CheckedState::Checked)
                           ? UltraCanvasMekkoChartElement::MekkoMode::BarMekko
                           : UltraCanvasMekkoChartElement::MekkoMode::Marimekko;
            marketChart->SetMekkoMode(newMode);
            salesChart->SetMekkoMode(newMode);
        };
        DemoKit::AddFixed(controlsCard, swMode);

        auto btnToggleLabels = MakeMekkoButton("btnMekkoToggleLabels", "Cycle Segment Labels");
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
        DemoKit::AddFixed(controlsCard, btnToggleLabels);

        auto btnToggleSort = MakeMekkoButton("btnMekkoToggleSort", "Cycle Column Sorting");
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
        DemoKit::AddFixed(controlsCard, btnToggleSort);

        auto btnRandomData = MakeMekkoButton("btnMekkoRandomData", "Generate Random Data");
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
        DemoKit::AddFixed(controlsCard, btnRandomData);

        // Segment click feedback
        auto clickLabel = DemoKit::AddText(controlsCard, "MekkoClickLabel", "Click a segment to inspect it...");
        clickLabel->SetFontSize(11);
        clickLabel->SetTextColor(Color(90, 90, 90, 255));

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

        // Info
        DemoKit::AddHeading(controlsCard, "MekkoInfoTitle", "Mekko Chart Features:");
        DemoKit::Add(controlsCard, DemoKit::CreateBullets("MekkoInfo", {
                "Column width proportional to column total, 100% stacked height",
                "Marimekko (percent) and Bar-Mekko (absolute) modes",
                "Column sorting, headers, cumulative % axis, legends",
                "Value/percent segment labels with auto-contrast text",
                "Interactive tooltips, hover highlight and click callbacks"}));
        DemoKit::AddCaption(controlsCard, "MekkoInfoHint", "Hover over segments and legend entries!");

        return container;
    }

} // namespace UltraCanvas

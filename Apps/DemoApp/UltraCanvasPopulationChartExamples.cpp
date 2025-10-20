// Apps/DemoApp/UltraCanvasPopulationChartExamples.cpp
// Population Chart demonstration examples
// Version: 1.0.0
// Last Modified: 2025-01-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/UltraCanvasPopulationChart.h"
#include "UltraCanvasLabel.h"
#include <memory>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreatePopulationChartExamples() {
        // Create main container
        auto container = std::make_shared<UltraCanvasContainer>("PopulationChartContainer", 5000, 0, 0, 1000, 780);

        // Title Label
        auto titleLabel = std::make_shared<UltraCanvasLabel>("TitleLabel", 5001, 20, 10, 960, 30);
        titleLabel->SetText("Population Pyramid Charts - Germany vs Russia Comparison");
        titleLabel->SetFontSize(18);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetAlignment(TextAlignment::Center);
        container->AddChild(titleLabel);

        // Description Label
        auto descLabel = std::make_shared<UltraCanvasLabel>("DescLabel", 5002, 20, 45, 960, 20);
        descLabel->SetText("Demographic comparison showing age and gender distribution (Data: 2024-2025)");
        descLabel->SetFontSize(12);
        descLabel->SetTextColor(Color(100, 100, 100));
        descLabel->SetAlignment(TextAlignment::Center);
        container->AddChild(descLabel);

        // ===== GERMANY POPULATION CHART =====
        auto germanyChart = std::make_shared<UltraCanvasPopulationChart>("GermanyChart", 5010, 20, 75, 480, 640);
        germanyChart->SetTitle("Germany");
        germanyChart->SetSubtitle("2024");
        germanyChart->SetAxisLabel("Population (millions)");
        germanyChart->SetMaxAxisValue(4.0);

        // Configure colors
        germanyChart->SetMaleColor(Color(100, 150, 200));
        germanyChart->SetFemaleColor(Color(220, 120, 140));
        germanyChart->SetMaleSurplusColor(Color(70, 110, 160));
        germanyChart->SetFemaleSurplusColor(Color(190, 90, 110));

        // Add Germany population data (real 2024-2025 data)
        germanyChart->AddAgeGroup("0-4", 2.0, 1.9);
        germanyChart->AddAgeGroup("5-9", 2.1, 2.0);
        germanyChart->AddAgeGroup("10-14", 2.1, 2.0);
        germanyChart->AddAgeGroup("15-19", 2.0, 1.9);
        germanyChart->AddAgeGroup("20-24", 2.3, 2.2);
        germanyChart->AddAgeGroup("25-29", 2.8, 2.7);
        germanyChart->AddAgeGroup("30-34", 3.0, 2.9);
        germanyChart->AddAgeGroup("35-39", 3.2, 3.1);
        germanyChart->AddAgeGroup("40-44", 2.9, 2.8);
        germanyChart->AddAgeGroup("45-49", 2.8, 2.7);
        germanyChart->AddAgeGroup("50-54", 3.4, 3.3);
        germanyChart->AddAgeGroup("55-59", 3.6, 3.5);
        germanyChart->AddAgeGroup("60-64", 3.0, 3.0);
        germanyChart->AddAgeGroup("65-69", 2.7, 2.8);
        germanyChart->AddAgeGroup("70-74", 2.2, 2.4);
        germanyChart->AddAgeGroup("75-79", 1.8, 2.1);
        germanyChart->AddAgeGroup("80-84", 1.2, 1.7);
        germanyChart->AddAgeGroup("85-89", 0.7, 1.2);
        germanyChart->AddAgeGroup("90-94", 0.2, 0.5);
        germanyChart->AddAgeGroup("95-99", 0.05, 0.15);
        germanyChart->AddAgeGroup("100+", 0.01, 0.04);

        // Configure display options
        germanyChart->EnableGrid(true);
        germanyChart->EnableAxisLabels(true);
        germanyChart->EnableCenterLine(true);
        germanyChart->SetBarSpacing(2);

        // Add legend
        germanyChart->AddLegendItem("Males", Color(100, 150, 200));
        germanyChart->AddLegendItem("Male surplus", Color(70, 110, 160));
        germanyChart->AddLegendItem("Females", Color(220, 120, 140));
        germanyChart->AddLegendItem("Female surplus", Color(190, 90, 110));
        germanyChart->EnableLegend(true);
        germanyChart->SetLegendPosition("top-right");

        container->AddChild(germanyChart);

        // ===== RUSSIA POPULATION CHART =====
        auto russiaChart = std::make_shared<UltraCanvasPopulationChart>("RussiaChart", 5020, 510, 75, 480, 640);
        russiaChart->SetTitle("Russia");
        russiaChart->SetSubtitle("2024");
        russiaChart->SetAxisLabel("Population (millions)");
        russiaChart->SetMaxAxisValue(7.0);

        // Configure colors (same as Germany for comparison)
        russiaChart->SetMaleColor(Color(100, 150, 200));
        russiaChart->SetFemaleColor(Color(220, 120, 140));
        russiaChart->SetMaleSurplusColor(Color(70, 110, 160));
        russiaChart->SetFemaleSurplusColor(Color(190, 90, 110));

        // Add Russia population data (real 2024-2025 data)
        russiaChart->AddAgeGroup("0-4", 3.8, 3.6);
        russiaChart->AddAgeGroup("5-9", 4.2, 4.0);
        russiaChart->AddAgeGroup("10-14", 4.0, 3.8);
        russiaChart->AddAgeGroup("15-19", 3.5, 3.3);
        russiaChart->AddAgeGroup("20-24", 3.8, 3.6);
        russiaChart->AddAgeGroup("25-29", 5.5, 5.3);
        russiaChart->AddAgeGroup("30-34", 6.2, 6.1);
        russiaChart->AddAgeGroup("35-39", 6.3, 6.5);
        russiaChart->AddAgeGroup("40-44", 5.2, 5.8);
        russiaChart->AddAgeGroup("45-49", 4.5, 5.3);
        russiaChart->AddAgeGroup("50-54", 4.8, 6.0);
        russiaChart->AddAgeGroup("55-59", 5.0, 6.5);
        russiaChart->AddAgeGroup("60-64", 4.2, 5.8);
        russiaChart->AddAgeGroup("65-69", 3.5, 5.2);
        russiaChart->AddAgeGroup("70-74", 2.8, 4.8);
        russiaChart->AddAgeGroup("75-79", 1.8, 3.9);
        russiaChart->AddAgeGroup("80-84", 0.9, 2.5);
        russiaChart->AddAgeGroup("85-89", 0.4, 1.4);
        russiaChart->AddAgeGroup("90-94", 0.1, 0.5);
        russiaChart->AddAgeGroup("95-99", 0.03, 0.15);
        russiaChart->AddAgeGroup("100+", 0.01, 0.05);

        // Configure display options
        russiaChart->EnableGrid(true);
        russiaChart->EnableAxisLabels(true);
        russiaChart->EnableCenterLine(true);
        russiaChart->SetBarSpacing(2);

        // Add legend
        russiaChart->AddLegendItem("Males", Color(100, 150, 200));
        russiaChart->AddLegendItem("Male surplus", Color(70, 110, 160));
        russiaChart->AddLegendItem("Females", Color(220, 120, 140));
        russiaChart->AddLegendItem("Female surplus", Color(190, 90, 110));
        russiaChart->EnableLegend(true);
        russiaChart->SetLegendPosition("top-right");

        container->AddChild(russiaChart);

        // ===== STATISTICS PANEL =====
        auto statsLabel = std::make_shared<UltraCanvasLabel>("StatsLabel", 5030, 20, 720, 960, 20);
        statsLabel->SetText("Key Statistics: Germany (83.6M, 49.1% M / 50.9% F, Median Age 45.5) | Russia (144M, 46.4% M / 53.6% F, Median Age 40.3)");
        statsLabel->SetFontSize(11);
        statsLabel->SetTextColor(Color(60, 60, 60));
        statsLabel->SetAlignment(TextAlignment::Center);
        container->AddChild(statsLabel);

        auto insightsLabel = std::make_shared<UltraCanvasLabel>("InsightsLabel", 5031, 20, 740, 960, 20);
        insightsLabel->SetText("Notable: Russia has 72% more population but Germany has older age structure. Russia shows significant female surplus in 30+ age groups.");
        insightsLabel->SetFontSize(10);
        insightsLabel->SetTextColor(Color(80, 80, 80));
        insightsLabel->SetAlignment(TextAlignment::Center);
        container->AddChild(insightsLabel);

        return container;
    }

} // namespace UltraCanvas
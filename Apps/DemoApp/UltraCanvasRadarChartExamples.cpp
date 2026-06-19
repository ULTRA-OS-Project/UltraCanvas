// Apps/DemoApp/UltraCanvasRadarChartExamples.cpp
// Comprehensive radar chart demonstration
// Version: 2.0.0
// Last Modified: 2026-06-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/UltraCanvasRadarChartElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <vector>

namespace UltraCanvas {

// ===== MAIN RADAR CHART EXAMPLES CREATOR =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateRadarChartExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("RadarChartContainer", 0, 0, 1200, 810);

        // === TITLE ===
        auto titleLabel = std::make_shared<UltraCanvasLabel>("RadarTitleLabel", 20, 10, 1160, 35);
        titleLabel->SetText("Radar Chart Examples - Multi-Axis, Multi-Series Visualization");
        titleLabel->SetFontSize(18);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetAlignment(TextAlignment::Center);
        titleLabel->SetBackgroundColor(Color(240, 240, 250, 255));
        container->AddChild(titleLabel);

        // === DESCRIPTION ===
        auto descLabel = std::make_shared<UltraCanvasLabel>("RadarDescLabel", 20, 55, 1160, 40);
        descLabel->SetText(
                "Radar (spider) charts plot multiple quantitative axes on a circular grid. "
                "Each series is drawn as a filled polygon. Hover over a vertex to see its value.");
        descLabel->SetFontSize(11);
        descLabel->SetWrap(TextWrap::WrapWord);
        container->AddChild(descLabel);

        // ===== EXAMPLE 1: PERFORMANCE METRICS (top left) =====
        auto perfLabel = std::make_shared<UltraCanvasLabel>("RadarPerfLabel", 40, 110, 540, 25);
        perfLabel->SetText("Performance Metrics: Current vs Target");
        perfLabel->SetFontSize(13);
        perfLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(perfLabel);

        auto performanceRadar = CreateRadarChartElement("performanceRadar", 40, 140, 540, 320);
        performanceRadar->SetGridDisplay(true, Color(220, 220, 220, 255), 1.0f);
        performanceRadar->SetAxisLabels(true, Color(60, 60, 60, 255), "Arial", 11.0f);
        performanceRadar->SetLegendDisplay(true);
        performanceRadar->SetAnimationEnabled(true, 1200);

        performanceRadar->AddAxis("Speed", 0, 100);
        performanceRadar->AddAxis("Quality", 0, 100);
        performanceRadar->AddAxis("Efficiency", 0, 100);
        performanceRadar->AddAxis("Innovation", 0, 100);
        performanceRadar->AddAxis("Collaboration", 0, 100);
        performanceRadar->AddAxis("Leadership", 0, 100);

        performanceRadar->AddSeries("Current", {85, 92, 78, 65, 88, 72},
                                    Color(70, 130, 180, 100), Color(70, 130, 180, 255));
        performanceRadar->AddSeries("Target", {95, 95, 90, 85, 90, 85},
                                    Color(255, 140, 0, 80), Color(255, 140, 0, 255));
        performanceRadar->SetSeriesProperties(0, Color(70, 130, 180, 100), Color(70, 130, 180, 255), 2.5f, 0.3f);
        performanceRadar->SetSeriesProperties(1, Color(255, 140, 0, 80), Color(255, 140, 0, 255), 2.0f, 0.2f);
        container->AddChild(performanceRadar);

        // ===== EXAMPLE 2: SKILLS ASSESSMENT (top right) =====
        auto skillsLabel = std::make_shared<UltraCanvasLabel>("RadarSkillsLabel", 620, 110, 540, 25);
        skillsLabel->SetText("Skills Assessment (0-10 scale)");
        skillsLabel->SetFontSize(13);
        skillsLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(skillsLabel);

        auto skillsRadar = CreateRadarChartElement("skillsRadar", 620, 140, 540, 320);
        skillsRadar->SetGridDisplay(true, Color(200, 240, 200, 255), 1.5f);
        skillsRadar->SetAxisLabels(true, Color(40, 80, 40, 255), "Arial", 12.0f);
        skillsRadar->SetRadarOrientation(-90.0f, true); // Start at top
        skillsRadar->SetLegendDisplay(true);

        skillsRadar->AddAxis("Programming", 0, 10);
        skillsRadar->AddAxis("Design", 0, 10);
        skillsRadar->AddAxis("Testing", 0, 10);
        skillsRadar->AddAxis("Documentation", 0, 10);
        skillsRadar->AddAxis("Communication", 0, 10);

        for (size_t i = 0; i < skillsRadar->GetAxisCount(); ++i) {
            skillsRadar->SetAxisProperties(i, Color(100, 150, 100, 255), true, 5);
        }

        skillsRadar->AddSeries("Current Skills", {8.5, 6.5, 7.8, 5.5, 8.2},
                               Color(50, 205, 50, 120), Color(34, 139, 34, 255));
        skillsRadar->SetSeriesProperties(0, Color(50, 205, 50, 120), Color(34, 139, 34, 255), 3.0f, 0.4f);
        container->AddChild(skillsRadar);

        // ===== EXAMPLE 3: MULTI-SERIES PRODUCT COMPARISON (bottom) =====
        auto productLabel = std::make_shared<UltraCanvasLabel>("RadarProductLabel", 320, 480, 560, 25);
        productLabel->SetText("Product Comparison (8 axes, 3 series)");
        productLabel->SetFontSize(13);
        productLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(productLabel);

        auto multiSeriesRadar = CreateRadarChartElement("multiSeriesRadar", 320, 510, 560, 290);
        multiSeriesRadar->SetGridDisplay(true, Color(240, 240, 240, 255), 1.0f);
        multiSeriesRadar->SetAxisLabels(true, Color(50, 50, 50, 255), "Arial", 10.0f);
        multiSeriesRadar->SetLegendDisplay(true);
        multiSeriesRadar->SetAnimationEnabled(true, 1500);

        multiSeriesRadar->AddAxis("Performance", 0, 100);
        multiSeriesRadar->AddAxis("Features", 0, 100);
        multiSeriesRadar->AddAxis("Usability", 0, 100);
        multiSeriesRadar->AddAxis("Price", 0, 100);
        multiSeriesRadar->AddAxis("Support", 0, 100);
        multiSeriesRadar->AddAxis("Reliability", 0, 100);
        multiSeriesRadar->AddAxis("Security", 0, 100);
        multiSeriesRadar->AddAxis("Scalability", 0, 100);

        multiSeriesRadar->AddSeries("Product A", {85, 90, 78, 60, 85, 92, 88, 82},
                                    Color(220, 20, 60, 80), Color(220, 20, 60, 255));
        multiSeriesRadar->AddSeries("Product B", {75, 85, 92, 80, 70, 88, 85, 90},
                                    Color(30, 144, 255, 80), Color(30, 144, 255, 255));
        multiSeriesRadar->AddSeries("Product C", {90, 75, 85, 70, 95, 80, 90, 85},
                                    Color(50, 205, 50, 80), Color(50, 205, 50, 255));
        multiSeriesRadar->SetSeriesProperties(0, Color(220, 20, 60, 80), Color(220, 20, 60, 255), 2.5f, 0.25f);
        multiSeriesRadar->SetSeriesProperties(1, Color(30, 144, 255, 80), Color(30, 144, 255, 255), 2.5f, 0.25f);
        multiSeriesRadar->SetSeriesProperties(2, Color(50, 205, 50, 80), Color(50, 205, 50, 255), 2.5f, 0.25f);
        container->AddChild(multiSeriesRadar);

        return container;
    }

} // namespace UltraCanvas

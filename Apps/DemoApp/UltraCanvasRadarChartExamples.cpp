// Apps/DemoApp/UltraCanvasRadarChartExamples.cpp
// Comprehensive radar (spider) chart demonstration with interactive controls
// Version: 2.0.0
// Last Modified: 2026-06-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/UltraCanvasRadarChartElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include <vector>

namespace UltraCanvas {

// ===== RADAR CHART CONTROLS STATE =====
    static struct RadarChartControls {
        bool showGrid = true;
        bool showLegend = true;
        bool showValueLabels = false;
        bool animationEnabled = true;
    } radarChartControls;

    using RadarChartPtr = std::shared_ptr<UltraCanvasRadarChartElement>;

// ===== CONTROL PANEL CREATION =====
    static void CreateRadarChartControlPanel(
            std::shared_ptr<UltraCanvasContainer> container,
            std::vector<RadarChartPtr> charts,
            int x, int y) {

        int yOffset = y;
        int controlHeight = 25;
        int spacing = 8;

        // === DISPLAY OPTIONS ===
        auto displayLabel = std::make_shared<UltraCanvasLabel>("RadarDisplayLabel", x, yOffset, 200, 20);
        displayLabel->SetText("Display Options:");
        displayLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(displayLabel);
        yOffset += 25;

        auto gridCheckbox = std::make_shared<UltraCanvasCheckbox>("RadarGrid", x, yOffset, 180, controlHeight);
        gridCheckbox->SetText("Show Grid");
        gridCheckbox->SetChecked(radarChartControls.showGrid);
        gridCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            radarChartControls.showGrid = (newState == CheckedState::Checked);
            for (auto& c : charts) c->SetGridDisplay(radarChartControls.showGrid);
        };
        container->AddChild(gridCheckbox);
        yOffset += controlHeight + spacing;

        auto legendCheckbox = std::make_shared<UltraCanvasCheckbox>("RadarLegend", x, yOffset, 180, controlHeight);
        legendCheckbox->SetText("Show Legend");
        legendCheckbox->SetChecked(radarChartControls.showLegend);
        legendCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            radarChartControls.showLegend = (newState == CheckedState::Checked);
            for (auto& c : charts) c->SetLegendDisplay(radarChartControls.showLegend);
        };
        container->AddChild(legendCheckbox);
        yOffset += controlHeight + spacing;

        auto valueLabelsCheckbox = std::make_shared<UltraCanvasCheckbox>("RadarValueLabels", x, yOffset, 200, controlHeight);
        valueLabelsCheckbox->SetText("Show Value Labels");
        valueLabelsCheckbox->SetChecked(radarChartControls.showValueLabels);
        valueLabelsCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            radarChartControls.showValueLabels = (newState == CheckedState::Checked);
            for (auto& c : charts) c->SetValueLabels(radarChartControls.showValueLabels);
        };
        container->AddChild(valueLabelsCheckbox);
        yOffset += controlHeight + spacing * 2;

        // === ANIMATION ===
        auto animLabel = std::make_shared<UltraCanvasLabel>("RadarAnimLabel", x, yOffset, 200, 20);
        animLabel->SetText("Animation:");
        animLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(animLabel);
        yOffset += 25;

        auto animCheckbox = std::make_shared<UltraCanvasCheckbox>("RadarAnim", x, yOffset, 200, controlHeight);
        animCheckbox->SetText("Animate Growth");
        animCheckbox->SetChecked(radarChartControls.animationEnabled);
        animCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            radarChartControls.animationEnabled = (newState == CheckedState::Checked);
            for (auto& c : charts) c->SetAnimationEnabled(radarChartControls.animationEnabled);
        };
        container->AddChild(animCheckbox);
        yOffset += controlHeight + spacing;

        auto restartButton = std::make_shared<UltraCanvasButton>("RadarRestart", x, yOffset, 180, controlHeight + 5);
        restartButton->SetText("Restart Animations");
        restartButton->onClick = [charts]() {
            for (auto& c : charts) c->StartAnimation();
        };
        container->AddChild(restartButton);
    }

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
        auto descLabel = std::make_shared<UltraCanvasLabel>("RadarDescLabel", 240, 55, 940, 60);
        descLabel->SetText(
                "Radar (spider) charts plot several quantitative axes on a shared circular grid, drawing each "
                "data series as a filled polygon. They excel at comparing multiple entities across the same set "
                "of metrics. Use the controls on the left to toggle the grid, legend, value labels and the "
                "grow-out animation. Hover any vertex to see its exact value.");
        descLabel->SetFontSize(11);
        descLabel->SetWrap(TextWrap::WrapWord);
        container->AddChild(descLabel);

        // ===== EXAMPLE 1: PERFORMANCE METRICS (top left) =====
        auto perfLabel = std::make_shared<UltraCanvasLabel>("RadarPerfLabel", 240, 130, 460, 25);
        perfLabel->SetText("Performance Metrics: Current vs Target");
        perfLabel->SetFontSize(13);
        perfLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(perfLabel);

        auto performanceRadar = CreateRadarChartElement("performanceRadar", 240, 160, 460, 290);
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
        auto skillsLabel = std::make_shared<UltraCanvasLabel>("RadarSkillsLabel", 720, 130, 460, 25);
        skillsLabel->SetText("Skills Assessment (0-10 scale)");
        skillsLabel->SetFontSize(13);
        skillsLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(skillsLabel);

        auto skillsRadar = CreateRadarChartElement("skillsRadar", 720, 160, 460, 290);
        skillsRadar->SetGridDisplay(true, Color(200, 240, 200, 255), 1.5f);
        skillsRadar->SetAxisLabels(true, Color(40, 80, 40, 255), "Arial", 12.0f);
        skillsRadar->SetRadarOrientation(-90.0f, true); // Start at the top, clockwise
        skillsRadar->SetLegendDisplay(true);
        skillsRadar->SetAnimationEnabled(true, 1000);

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

        // ===== EXAMPLE 3: MULTI-SERIES PRODUCT COMPARISON (bottom left) =====
        auto productLabel = std::make_shared<UltraCanvasLabel>("RadarProductLabel", 240, 470, 460, 25);
        productLabel->SetText("Product Comparison (8 axes, 3 series)");
        productLabel->SetFontSize(13);
        productLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(productLabel);

        auto multiSeriesRadar = CreateRadarChartElement("multiSeriesRadar", 240, 500, 460, 290);
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

        // ===== EXAMPLE 4: TEAM COMPARISON WITH VALUE LABELS (bottom right) =====
        auto teamLabel = std::make_shared<UltraCanvasLabel>("RadarTeamLabel", 720, 470, 460, 25);
        teamLabel->SetText("Team Comparison (value labels enabled)");
        teamLabel->SetFontSize(13);
        teamLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(teamLabel);

        auto teamRadar = CreateRadarChartElement("teamRadar", 720, 500, 460, 290);
        teamRadar->SetGridDisplay(true, Color(225, 225, 235, 255), 1.0f);
        teamRadar->SetAxisLabels(true, Color(50, 50, 70, 255), "Arial", 10.0f);
        teamRadar->SetValueLabels(true, Color(40, 40, 60, 255), 10.0f);
        teamRadar->SetLegendDisplay(true);
        teamRadar->SetAnimationEnabled(true, 1300);

        teamRadar->AddAxis("Planning", 0, 100);
        teamRadar->AddAxis("Execution", 0, 100);
        teamRadar->AddAxis("Communication", 0, 100);
        teamRadar->AddAxis("Quality", 0, 100);
        teamRadar->AddAxis("Velocity", 0, 100);

        teamRadar->AddSeries("Team Alpha", {78, 88, 70, 92, 65},
                             Color(138, 43, 226, 90), Color(138, 43, 226, 255));
        teamRadar->AddSeries("Team Beta", {85, 72, 90, 80, 88},
                             Color(255, 99, 71, 80), Color(255, 99, 71, 255));
        teamRadar->SetSeriesProperties(0, Color(138, 43, 226, 90), Color(138, 43, 226, 255), 2.5f, 0.28f);
        teamRadar->SetSeriesProperties(1, Color(255, 99, 71, 80), Color(255, 99, 71, 255), 2.5f, 0.22f);
        container->AddChild(teamRadar);

        // === CONTROL PANEL (left column) ===
        CreateRadarChartControlPanel(container,
                                     {performanceRadar, skillsRadar, multiSeriesRadar, teamRadar},
                                     20, 130);

        return container;
    }

} // namespace UltraCanvas

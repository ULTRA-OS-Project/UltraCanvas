// Apps/DemoApp/UltraCanvasRadarChartExamples.cpp
// Comprehensive radar (spider) chart demonstration with interactive controls
// Version: 2.1.0 - the shared page look (UltraCanvasDemoPageKit): the controls in a
//                  card above the charts, one card per chart, checkboxes sized to their text
// Version: 2.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
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
        bool animationEnabled = false;
    } radarChartControls;

    using RadarChartPtr = std::shared_ptr<UltraCanvasRadarChartElement>;

// ===== CONTROL PANEL CREATION =====
    // Two groups side by side (they wrap under each other when narrow); the
    // checkboxes and the button are as wide as their text.
    static void CreateRadarChartControlPanel(
            std::shared_ptr<UltraCanvasContainer> container,
            std::vector<RadarChartPtr> charts) {

        auto groups = DemoKit::AddRow(container, "RadarControlGroups", 32);

        // === DISPLAY OPTIONS ===
        auto displayGroup = DemoKit::CreateColumn("RadarDisplayGroup", 8);
        DemoKit::Add(groups, displayGroup);
        auto displayLabel = DemoKit::AddText(displayGroup, "RadarDisplayLabel", "Display Options:");
        displayLabel->SetFontWeight(FontWeight::Bold);

        auto gridCheckbox = std::make_shared<UltraCanvasCheckbox>("RadarGrid", "Show Grid");
        gridCheckbox->SetChecked(radarChartControls.showGrid);
        gridCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            radarChartControls.showGrid = (newState == CheckedState::Checked);
            for (auto& c : charts) c->SetGridDisplay(radarChartControls.showGrid);
        };
        DemoKit::Add(displayGroup, gridCheckbox);

        auto legendCheckbox = std::make_shared<UltraCanvasCheckbox>("RadarLegend", "Show Legend");
        legendCheckbox->SetChecked(radarChartControls.showLegend);
        legendCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            radarChartControls.showLegend = (newState == CheckedState::Checked);
            for (auto& c : charts) c->SetLegendDisplay(radarChartControls.showLegend);
        };
        DemoKit::Add(displayGroup, legendCheckbox);

        auto valueLabelsCheckbox = std::make_shared<UltraCanvasCheckbox>("RadarValueLabels", "Show Value Labels");
        valueLabelsCheckbox->SetChecked(radarChartControls.showValueLabels);
        valueLabelsCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            radarChartControls.showValueLabels = (newState == CheckedState::Checked);
            for (auto& c : charts) c->SetValueLabels(radarChartControls.showValueLabels);
        };
        DemoKit::Add(displayGroup, valueLabelsCheckbox);

        // === ANIMATION ===
        auto animGroup = DemoKit::CreateColumn("RadarAnimGroup", 8);
        DemoKit::Add(groups, animGroup);
        auto animLabel = DemoKit::AddText(animGroup, "RadarAnimLabel", "Animation:");
        animLabel->SetFontWeight(FontWeight::Bold);

        auto animCheckbox = std::make_shared<UltraCanvasCheckbox>("RadarAnim", "Animate Growth");
        animCheckbox->SetChecked(radarChartControls.animationEnabled);
        animCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            radarChartControls.animationEnabled = (newState == CheckedState::Checked);
            for (auto& c : charts) c->SetAnimationEnabled(radarChartControls.animationEnabled);
        };
        DemoKit::Add(animGroup, animCheckbox);

        // In a row so it is as wide as its text, not stretched across.
        auto buttonRow = DemoKit::AddRow(animGroup, "RadarRestartRow");
        auto restartButton = std::make_shared<UltraCanvasButton>("RadarRestart", "Restart Animations");
        restartButton->onClick = [charts]() {
            for (auto& c : charts) c->StartAnimation();
        };
        DemoKit::Add(buttonRow, restartButton);
    }

// ===== MAIN RADAR CHART EXAMPLES CREATOR =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateRadarChartExamples() {
        auto page = DemoKit::CreatePage("RadarChartContainer",
                "Radar Chart Examples - Multi-Axis, Multi-Series Visualization",
                "Radar (spider) charts plot several quantitative axes on a shared circular grid, drawing each "
                "data series as a filled polygon. They excel at comparing multiple entities across the same set "
                "of metrics. Use the controls above the charts to toggle the grid, legend, value labels and the "
                "grow-out animation. Hover any vertex to see its exact value.");

        auto controlsCard = DemoKit::AddCard(page, "RadarControls");
        // A card holds a 460 px chart without squeezing it.
        constexpr float chartCardWidth = 460 + 2 * DemoKit::kCardPadding + 2;
        auto charts = DemoKit::AddRow(page, "RadarCharts");

        // ===== EXAMPLE 1: PERFORMANCE METRICS (top left) =====
        auto perfCard = DemoKit::AddCard(charts, "RadarPerfLabel", "Performance Metrics: Current vs Target", "", chartCardWidth);

        auto performanceRadar = CreateRadarChartElement("performanceRadar", 0, 0, 460, 290);
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
        DemoKit::AddFixed(perfCard, performanceRadar);

        // ===== EXAMPLE 2: SKILLS ASSESSMENT (top right) =====
        auto skillsCard = DemoKit::AddCard(charts, "RadarSkillsLabel", "Skills Assessment (0-10 scale)", "", chartCardWidth);

        auto skillsRadar = CreateRadarChartElement("skillsRadar", 0, 0, 460, 290);
        skillsRadar->SetGridDisplay(true, Color(200, 240, 200, 255), 1.5f);
        skillsRadar->SetAxisLabels(true, Color(40, 80, 40, 255), "Arial", 12.0f);
        skillsRadar->SetRadarOrientation(-90.0f, true); // Start at the top, clockwise
        skillsRadar->SetLegendDisplay(true);
        //skillsRadar->SetAnimationEnabled(true, 1000);

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
        DemoKit::AddFixed(skillsCard, skillsRadar);

        // ===== EXAMPLE 3: MULTI-SERIES PRODUCT COMPARISON (bottom left) =====
        auto productCard = DemoKit::AddCard(charts, "RadarProductLabel", "Product Comparison (8 axes, 3 series)", "", chartCardWidth);

        auto multiSeriesRadar = CreateRadarChartElement("multiSeriesRadar", 0, 0, 460, 290);
        multiSeriesRadar->SetGridDisplay(true, Color(240, 240, 240, 255), 1.0f);
        multiSeriesRadar->SetAxisLabels(true, Color(50, 50, 50, 255), "Arial", 10.0f);
        multiSeriesRadar->SetLegendDisplay(true);
        //multiSeriesRadar->SetAnimationEnabled(true, 1500);

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
        DemoKit::AddFixed(productCard, multiSeriesRadar);

        // ===== EXAMPLE 4: TEAM COMPARISON WITH VALUE LABELS (bottom right) =====
        auto teamCard = DemoKit::AddCard(charts, "RadarTeamLabel", "Team Comparison (value labels enabled)", "", chartCardWidth);

        auto teamRadar = CreateRadarChartElement("teamRadar", 0, 0, 460, 290);
        teamRadar->SetGridDisplay(true, Color(225, 225, 235, 255), 1.0f);
        teamRadar->SetAxisLabels(true, Color(50, 50, 70, 255), "Arial", 10.0f);
        teamRadar->SetValueLabels(true, Color(40, 40, 60, 255), 10.0f);
        teamRadar->SetLegendDisplay(true);
        //teamRadar->SetAnimationEnabled(true, 1300);

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
        DemoKit::AddFixed(teamCard, teamRadar);

        // === CONTROL PANEL (above the charts) ===
        CreateRadarChartControlPanel(controlsCard,
                                     {performanceRadar, skillsRadar, multiSeriesRadar, teamRadar});

        return page;
    }

} // namespace UltraCanvas

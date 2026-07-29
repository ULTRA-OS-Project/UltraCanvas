// Apps/DemoApp/UltraCanvasChordChartExamples.cpp
// Chord diagram demonstration: relationships and flows between categories
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/UltraCanvasChordChart.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include <cstdio>

namespace UltraCanvas {

// ===== SAMPLE DATA BUILDERS =====

    // Bilateral merchandise trade between world regions, in billions USD.
    // Rows are the source, columns the destination, so the matrix is
    // deliberately asymmetric: East Asia ships far more to North America
    // than it receives back.
    static void BuildGlobalTradeData(std::shared_ptr<UltraCanvasChordChart> chart) {
        std::vector<std::string> regions = {
            "North America", "Europe", "East Asia", "Southeast Asia",
            "South America", "Middle East", "Africa", "Oceania"
        };
        std::vector<Color> colors = {
            Color( 70, 130, 180, 255),   // Steel blue
            Color( 34, 139,  34, 255),   // Forest green
            Color(220,  20,  60, 255),   // Crimson
            Color(255, 140,   0, 255),   // Dark orange
            Color(138,  43, 226, 255),   // Blue violet
            Color(255, 193,   7, 255),   // Amber
            Color(255,  69,   0, 255),   // Red orange
            Color(  0, 172, 193, 255)    // Cyan
        };
        std::vector<std::vector<double>> flows = {
            {  0, 420, 650, 280, 180, 150, 120,  95},
            {380,   0, 580, 320, 160, 250, 210,  85},
            {720, 520,   0, 450, 190, 280, 230, 140},
            {240, 290, 380,   0,  90, 180, 150, 120},
            {160, 140, 170,  70,   0,  55,  60,  35},
            {200, 280, 260, 130,  45,   0,  95,  40},
            {110, 190, 180,  85,  50,  80,   0,  25},
            { 80,  70, 120,  95,  30,  35,  20,   0}
        };
        chart->ImportFromMatrix(flows, regions, colors);
    }

    // Net migration corridors. The strong asymmetry is the point: several
    // corridors run almost entirely one way, which a chord diagram shows as
    // a ribbon that is wide at one end and narrow at the other.
    static void BuildMigrationData(std::shared_ptr<UltraCanvasChordChart> chart) {
        std::vector<std::string> regions = {
            "Africa", "Asia", "Europe", "Latin America", "North America", "Oceania"
        };
        std::vector<Color> colors = {
            Color(255,  87,  34, 255), Color(255, 193,   7, 255),
            Color( 63,  81, 181, 255), Color( 76, 175,  80, 255),
            Color(  0, 150, 136, 255), Color(156,  39, 176, 255)
        };
        std::vector<std::vector<double>> flows = {
            {  0,  95, 640,   8, 210,  12},
            {310,   0, 720,  25, 880, 190},
            { 55,  70,   0,  40, 260,  85},
            {  5,  15, 180,   0, 940,   8},
            { 20,  45, 130,  95,   0,  30},
            {  6,  35,  60,   4,  40,   0}
        };
        chart->ImportFromMatrix(flows, regions, colors);
    }

    // Handovers between project phases, including the feedback edges that
    // make a workflow cyclical (Testing back to Development, and so on).
    static void BuildProjectPhaseData(std::shared_ptr<UltraCanvasChordChart> chart) {
        std::vector<std::string> phases = {
            "Planning", "Design", "Development", "Testing", "Deployment", "Maintenance"
        };
        std::vector<Color> colors = {
            Color(233,  30,  99, 255), Color(  3, 169, 244, 255),
            Color( 76, 175,  80, 255), Color(255, 152,   0, 255),
            Color(156,  39, 176, 255), Color(121,  85,  72, 255)
        };
        std::vector<std::vector<double>> flows = {
            {  0, 100,  20,   0,   0,   0},
            { 30,   0, 150,   0,   0,   0},
            {  0,  25,   0, 120,  15,   0},
            {  0,   0,  40,   0,  80,   0},
            {  0,   0,  10,  20,   0,  60},
            {  0,   0,  25,  10,   5,   0}
        };
        chart->ImportFromMatrix(flows, phases, colors);
    }

    // Cross-team ticket handovers inside one engineering organisation.
    static void BuildTeamCollaborationData(std::shared_ptr<UltraCanvasChordChart> chart) {
        std::vector<std::string> teams = {
            "Frontend", "Backend", "Platform", "QA", "Design", "Support"
        };
        std::vector<std::vector<double>> flows = {
            {  0,  85,  30, 110,  70,  25},
            { 95,   0, 120,  90,  15,  40},
            { 25, 130,   0,  45,   5,  35},
            { 80,  75,  30,   0,  20,  95},
            { 90,  10,   5,  15,   0,  10},
            { 40,  55,  25, 100,  15,   0}
        };
        // No explicit colours: the chart falls back to its built-in palette.
        chart->ImportFromMatrix(flows, teams);
    }

// ===== CONTROL PANEL =====

    static void CreateChordControlPanel(
        std::shared_ptr<UltraCanvasContainer> container,
        std::vector<std::shared_ptr<UltraCanvasChordChart>> charts,
        int x, int y)
    {
        int yOffset = y;
        const int controlHeight = 25;
        const int spacing = 8;

        auto ribbonHeader = std::make_shared<UltraCanvasLabel>("CcRibbonHeader", x, yOffset, 200, 20);
        ribbonHeader->SetText("Ribbon Opacity:");
        ribbonHeader->SetFontWeight(FontWeight::Bold);
        container->AddChild(ribbonHeader);
        yOffset += 25;

        auto opacitySlider = std::make_shared<UltraCanvasSlider>("CcOpacity", x, yOffset, 180, controlHeight);
        opacitySlider->SetRange(0.1f, 1.0f);
        opacitySlider->SetStep(0.05f);
        opacitySlider->SetValue(0.7f);
        opacitySlider->onValueChanged = [charts](float value) {
            for (auto& c : charts) c->SetRibbonOpacity(value);
        };
        container->AddChild(opacitySlider);
        yOffset += controlHeight + spacing;

        auto curveHeader = std::make_shared<UltraCanvasLabel>("CcCurveHeader", x, yOffset, 200, 20);
        curveHeader->SetText("Ribbon Curvature:");
        curveHeader->SetFontWeight(FontWeight::Bold);
        container->AddChild(curveHeader);
        yOffset += 25;

        // 1.0 pulls both bezier controls onto the centre (classic chord look);
        // lower values let the ribbons bulge outward.
        auto curveSlider = std::make_shared<UltraCanvasSlider>("CcCurvature", x, yOffset, 180, controlHeight);
        curveSlider->SetRange(0.0f, 1.0f);
        curveSlider->SetStep(0.05f);
        curveSlider->SetValue(1.0f);
        curveSlider->onValueChanged = [charts](float value) {
            for (auto& c : charts) c->SetRibbonCurvature(value);
        };
        container->AddChild(curveSlider);
        yOffset += controlHeight + spacing;

        auto padHeader = std::make_shared<UltraCanvasLabel>("CcPadHeader", x, yOffset, 200, 20);
        padHeader->SetText("Category Gap (degrees):");
        padHeader->SetFontWeight(FontWeight::Bold);
        container->AddChild(padHeader);
        yOffset += 25;

        auto padSlider = std::make_shared<UltraCanvasSlider>("CcPadding", x, yOffset, 180, controlHeight);
        padSlider->SetRange(0.0f, 10.0f);
        padSlider->SetStep(0.5f);
        padSlider->SetValue(2.0f);
        padSlider->onValueChanged = [charts](float value) {
            for (auto& c : charts) c->SetCategoryPadding(value);
        };
        container->AddChild(padSlider);
        yOffset += controlHeight + spacing;

        auto ringHeader = std::make_shared<UltraCanvasLabel>("CcRingHeader", x, yOffset, 200, 20);
        ringHeader->SetText("Ring Thickness:");
        ringHeader->SetFontWeight(FontWeight::Bold);
        container->AddChild(ringHeader);
        yOffset += 25;

        auto ringSlider = std::make_shared<UltraCanvasSlider>("CcRingThickness", x, yOffset, 180, controlHeight);
        ringSlider->SetRange(6.0f, 40.0f);
        ringSlider->SetStep(1.0f);
        ringSlider->SetValue(20.0f);
        ringSlider->onValueChanged = [charts](float value) {
            for (auto& c : charts) c->SetRingThickness(value);
        };
        container->AddChild(ringSlider);
        yOffset += controlHeight + spacing * 2;

        auto colorHeader = std::make_shared<UltraCanvasLabel>("CcColorHeader", x, yOffset, 200, 20);
        colorHeader->SetText("Ribbon Colour:");
        colorHeader->SetFontWeight(FontWeight::Bold);
        container->AddChild(colorHeader);
        yOffset += 25;

        auto colorDropdown = std::make_shared<UltraCanvasDropdown>("CcColorMode", x, yOffset, 180, controlHeight);
        colorDropdown->AddItem("Larger Endpoint");
        colorDropdown->AddItem("Source");
        colorDropdown->AddItem("Target");
        colorDropdown->AddItem("Blend");
        colorDropdown->SetSelectedIndex(0);
        colorDropdown->onSelectionChanged = [charts](int index, const DropdownItem&) {
            ChordRibbonColorMode mode;
            switch (index) {
                case 1:  mode = ChordRibbonColorMode::Source; break;
                case 2:  mode = ChordRibbonColorMode::Target; break;
                case 3:  mode = ChordRibbonColorMode::Blend;  break;
                default: mode = ChordRibbonColorMode::Larger; break;
            }
            for (auto& c : charts) c->SetRibbonColorMode(mode);
        };
        container->AddChild(colorDropdown);
        yOffset += controlHeight + spacing;

        auto labelHeader = std::make_shared<UltraCanvasLabel>("CcLabelHeader", x, yOffset, 200, 20);
        labelHeader->SetText("Labels:");
        labelHeader->SetFontWeight(FontWeight::Bold);
        container->AddChild(labelHeader);
        yOffset += 25;

        auto labelDropdown = std::make_shared<UltraCanvasDropdown>("CcLabelMode", x, yOffset, 180, controlHeight);
        labelDropdown->AddItem("Outside (horizontal)");
        labelDropdown->AddItem("Radial");
        labelDropdown->AddItem("None");
        labelDropdown->SetSelectedIndex(0);
        labelDropdown->onSelectionChanged = [charts](int index, const DropdownItem&) {
            ChordLabelPlacement placement;
            switch (index) {
                case 1:  placement = ChordLabelPlacement::Radial; break;
                case 2:  placement = ChordLabelPlacement::None;   break;
                default: placement = ChordLabelPlacement::Outside; break;
            }
            for (auto& c : charts) c->SetLabelPlacement(placement);
        };
        container->AddChild(labelDropdown);
        yOffset += controlHeight + spacing * 2;

        auto filterHeader = std::make_shared<UltraCanvasLabel>("CcFilterHeader", x, yOffset, 200, 20);
        filterHeader->SetText("Hide Flows Below:");
        filterHeader->SetFontWeight(FontWeight::Bold);
        container->AddChild(filterHeader);
        yOffset += 25;

        // Filtering rebuilds the arcs from the surviving flows, so the ring
        // always accounts for exactly what is drawn.
        auto filterSlider = std::make_shared<UltraCanvasSlider>("CcMinFlow", x, yOffset, 180, controlHeight);
        filterSlider->SetRange(0.0f, 300.0f);
        filterSlider->SetStep(10.0f);
        filterSlider->SetValue(0.0f);
        filterSlider->onValueChanged = [charts](float value) {
            for (auto& c : charts) c->SetMinFlowValue(value);
        };
        container->AddChild(filterSlider);
        yOffset += controlHeight + spacing;

        auto dimHeader = std::make_shared<UltraCanvasLabel>("CcDimHeader", x, yOffset, 200, 20);
        dimHeader->SetText("Dim Non-Hovered:");
        dimHeader->SetFontWeight(FontWeight::Bold);
        container->AddChild(dimHeader);
        yOffset += 25;

        auto dimSlider = std::make_shared<UltraCanvasSlider>("CcDim", x, yOffset, 180, controlHeight);
        dimSlider->SetRange(0.0f, 1.0f);
        dimSlider->SetStep(0.05f);
        dimSlider->SetValue(0.25f);
        dimSlider->onValueChanged = [charts](float value) {
            for (auto& c : charts) c->SetDimmedOpacity(value);
        };
        container->AddChild(dimSlider);
        yOffset += controlHeight + spacing;

        auto hoverCheckbox = std::make_shared<UltraCanvasCheckbox>("CcHover", x, yOffset, 180, controlHeight);
        hoverCheckbox->SetText("Hover Highlight");
        hoverCheckbox->SetChecked(true);
        hoverCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetHoverHighlightEnabled(newState == CheckedState::Checked);
        };
        container->AddChild(hoverCheckbox);
        yOffset += controlHeight + spacing;

        auto hintLabel = std::make_shared<UltraCanvasLabel>("CcHint", x, yOffset, 200, 70);
        hintLabel->SetText("Hover a category arc to isolate\nits ribbons, or a single ribbon\nfor the exact flow and its\nreturn leg.");
        hintLabel->SetFontSize(10);
        hintLabel->SetTextColor(Color(110, 110, 110, 255));
        container->AddChild(hintLabel);
    }

// ===== MAIN CHORD CHART EXAMPLES CREATOR =====

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateChordChartExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("ChordChartContainer", 0, 0, 1200, 810);

        // === TITLE ===
        auto titleLabel = std::make_shared<UltraCanvasLabel>("CcTitleLabel", 20, 10, 1160, 35);
        titleLabel->SetText("Chord Chart Examples - Directed Flows Between Categories");
        titleLabel->SetFontSize(18);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetAlignment(TextAlignment::Center);
        titleLabel->SetBackgroundColor(Color(240, 240, 250, 255));
        container->AddChild(titleLabel);

        // === DESCRIPTION ===
        auto descLabel = std::make_shared<UltraCanvasLabel>("CcDescLabel", 20, 55, 1160, 60);
        descLabel->SetText(
            "Categories occupy arcs sized by total throughput; ribbons join them with a width proportional to the flow.\n"
            "Each direction gets its own sub-arc, so an asymmetric relationship reads as a ribbon that is wide at one end\n"
            "and narrow at the other. Hover isolates a category or a single ribbon; tooltips report the flow, its return\n"
            "leg and the net difference."
        );
        descLabel->SetFontSize(11);
        descLabel->SetWrap(TextWrap::WrapWord);
        container->AddChild(descLabel);

        // ===== EXAMPLE 1: GLOBAL TRADE FLOWS =====
        auto tradeLabel = std::make_shared<UltraCanvasLabel>("CcTradeLabel", 240, 130, 460, 25);
        tradeLabel->SetText("Global Merchandise Trade (8 Regions, billions USD)");
        tradeLabel->SetFontSize(13);
        tradeLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(tradeLabel);

        auto tradeChart = CreateChordChart("TradeChord", 240, 160, 460, 320);
        BuildGlobalTradeData(tradeChart);
        tradeChart->SetTooltipsEnabled(true);
        tradeChart->SetValueFormatter([](double v) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "$%.0fB", v);
            return std::string(buf);
        });
        container->AddChild(tradeChart);

        // ===== EXAMPLE 2: MIGRATION CORRIDORS (STRONG ASYMMETRY) =====
        auto migrationLabel = std::make_shared<UltraCanvasLabel>("CcMigrationLabel", 720, 130, 460, 25);
        migrationLabel->SetText("Migration Corridors (asymmetric, thousands of people)");
        migrationLabel->SetFontSize(13);
        migrationLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(migrationLabel);

        auto migrationChart = CreateChordChart("MigrationChord", 720, 160, 460, 320);
        BuildMigrationData(migrationChart);
        migrationChart->SetTooltipsEnabled(true);
        // Source colouring makes the direction of each corridor obvious.
        migrationChart->SetRibbonColorMode(ChordRibbonColorMode::Source);
        migrationChart->SetValueFormatter([](double v) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.0fk", v);
            return std::string(buf);
        });
        container->AddChild(migrationChart);

        // ===== EXAMPLE 3: PROJECT PHASE HANDOVERS =====
        auto phaseLabel = std::make_shared<UltraCanvasLabel>("CcPhaseLabel", 240, 490, 460, 25);
        phaseLabel->SetText("Project Phase Handovers (sparse matrix with feedback loops)");
        phaseLabel->SetFontSize(13);
        phaseLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(phaseLabel);

        auto phaseChart = CreateChordChart("PhaseChord", 240, 520, 460, 280);
        BuildProjectPhaseData(phaseChart);
        phaseChart->SetTooltipsEnabled(true);
        phaseChart->SetCategoryPadding(4.0f);
        phaseChart->SetRibbonColorMode(ChordRibbonColorMode::Source);
        phaseChart->SetRibbonOpacity(0.8f);
        container->AddChild(phaseChart);

        // ===== EXAMPLE 4: TEAM COLLABORATION (BLENDED, RADIAL LABELS) =====
        auto teamLabel = std::make_shared<UltraCanvasLabel>("CcTeamLabel", 720, 490, 460, 25);
        teamLabel->SetText("Cross-Team Handovers (blended colours, radial labels)");
        teamLabel->SetFontSize(13);
        teamLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(teamLabel);

        auto teamChart = CreateChordChart("TeamChord", 720, 520, 460, 280);
        BuildTeamCollaborationData(teamChart);
        teamChart->SetTooltipsEnabled(true);
        teamChart->SetRibbonColorMode(ChordRibbonColorMode::Blend);
        teamChart->SetLabelPlacement(ChordLabelPlacement::Radial);
        teamChart->SetRingThickness(14.0f);
        teamChart->SetRibbonBorder(Color(255, 255, 255, 140), 1.0f);
        container->AddChild(teamChart);

        // === CONTROL PANEL (left column) ===
        CreateChordControlPanel(container,
                                {tradeChart, migrationChart, phaseChart, teamChart},
                                20, 130);

        return container;
    }

} // namespace UltraCanvas

// Apps/DemoApp/UltraCanvasChordChartExamples.cpp
// Chord diagram demonstration: relationships and flows between categories
// Version: 1.1.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework
//
// Changelog:
//   v1.1.0 (2026-07-30):
//     - Each example now sits in its own tab, so one chord chart gets the whole
//       display area instead of a quarter of it.
//     - Migrated the page from absolute positioning to flex layout: the page is
//       a stretchable flex column (title, description, tabs) that fills the host
//       and grows with the window, and every tab is a flex row whose chart takes
//       all the width the control panel leaves. No manual resize handler.
//     - The control panel is per-tab and drives only that tab's chart, so its
//       controls can start out showing the settings that chart really uses.

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/UltraCanvasChordChart.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "CSSLayout/CSSLayout.h"
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

    // The panel starts from the settings its own chart was built with, so a
    // control never shows a value the chart does not actually use.
    struct ChordPanelDefaults {
        float ribbonOpacity    = 0.7f;
        float ribbonCurvature  = 1.0f;
        float categoryPadding  = 2.0f;
        float ringThickness    = 20.0f;
        int   colorModeIndex   = 0;   // Larger / Source / Target / Blend
        int   labelModeIndex   = 0;   // Outside / Radial / None
    };

    static constexpr int kChordPanelWidth   = 214;
    static constexpr int kChordControlWidth = 190;

    // Fixed-width side column driving a single chart. A side column costs a
    // chord chart nothing: the ring is sized by the smaller of width and
    // height, and on a normal window that is the height.
    static std::shared_ptr<UltraCanvasContainer> CreateChordControlPanel(
        const std::string& idPrefix,
        std::shared_ptr<UltraCanvasChordChart> chart,
        const ChordPanelDefaults& defaults)
    {
        auto panel = std::make_shared<UltraCanvasContainer>(idPrefix + "Panel",
                                                           kChordPanelWidth, 0);
        panel->SetBackgroundColor(Color(247, 247, 250, 255));
        panel->SetPadding(8, 10);
        panel->layout.SetFlexColumn().SetFlexGap(4)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Start);

        // Controls are in flow (no explicit origin) and never grow or shrink,
        // so the column keeps its natural height whatever the window does.
        auto addHeader = [&](const std::string& suffix, const std::string& text) {
            auto header = std::make_shared<UltraCanvasLabel>(
                idPrefix + suffix, kChordControlWidth, 17, text);
            header->SetFontSize(11);
            header->SetFontWeight(FontWeight::Bold);
            header->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            panel->AddChild(header);
        };

        auto addSlider = [&](const std::string& suffix, float minValue, float maxValue,
                             float step, float value, std::function<void(float)> onChange) {
            auto slider = std::make_shared<UltraCanvasSlider>(
                idPrefix + suffix, kChordControlWidth, 24);
            slider->SetRange(minValue, maxValue);
            slider->SetStep(step);
            slider->SetValue(value);
            slider->onValueChanged = std::move(onChange);
            slider->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            panel->AddChild(slider);
        };

        addHeader("OpacityHeader", "Ribbon Opacity:");
        addSlider("Opacity", 0.1f, 1.0f, 0.05f, defaults.ribbonOpacity,
                  [chart](float value) { chart->SetRibbonOpacity(value); });

        // 1.0 pulls both bezier controls onto the centre (classic chord look);
        // lower values let the ribbons bulge outward.
        addHeader("CurveHeader", "Ribbon Curvature:");
        addSlider("Curvature", 0.0f, 1.0f, 0.05f, defaults.ribbonCurvature,
                  [chart](float value) { chart->SetRibbonCurvature(value); });

        addHeader("PadHeader", "Category Gap (degrees):");
        addSlider("Padding", 0.0f, 10.0f, 0.5f, defaults.categoryPadding,
                  [chart](float value) { chart->SetCategoryPadding(value); });

        addHeader("RingHeader", "Ring Thickness:");
        addSlider("RingThickness", 6.0f, 40.0f, 1.0f, defaults.ringThickness,
                  [chart](float value) { chart->SetRingThickness(value); });

        addHeader("ColorHeader", "Ribbon Colour:");
        auto colorDropdown = std::make_shared<UltraCanvasDropdown>(
            idPrefix + "ColorMode", kChordControlWidth, 24);
        colorDropdown->AddItem("Larger Endpoint");
        colorDropdown->AddItem("Source");
        colorDropdown->AddItem("Target");
        colorDropdown->AddItem("Blend");
        colorDropdown->SetSelectedIndex(defaults.colorModeIndex);
        colorDropdown->onSelectionChanged = [chart](int index, const DropdownItem&) {
            ChordRibbonColorMode mode;
            switch (index) {
                case 1:  mode = ChordRibbonColorMode::Source; break;
                case 2:  mode = ChordRibbonColorMode::Target; break;
                case 3:  mode = ChordRibbonColorMode::Blend;  break;
                default: mode = ChordRibbonColorMode::Larger; break;
            }
            chart->SetRibbonColorMode(mode);
        };
        colorDropdown->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        panel->AddChild(colorDropdown);

        addHeader("LabelHeader", "Labels:");
        auto labelDropdown = std::make_shared<UltraCanvasDropdown>(
            idPrefix + "LabelMode", kChordControlWidth, 24);
        labelDropdown->AddItem("Outside (horizontal)");
        labelDropdown->AddItem("Radial");
        labelDropdown->AddItem("None");
        labelDropdown->SetSelectedIndex(defaults.labelModeIndex);
        labelDropdown->onSelectionChanged = [chart](int index, const DropdownItem&) {
            ChordLabelPlacement placement;
            switch (index) {
                case 1:  placement = ChordLabelPlacement::Radial; break;
                case 2:  placement = ChordLabelPlacement::None;   break;
                default: placement = ChordLabelPlacement::Outside; break;
            }
            chart->SetLabelPlacement(placement);
        };
        labelDropdown->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        panel->AddChild(labelDropdown);

        // Filtering rebuilds the arcs from the surviving flows, so the ring
        // always accounts for exactly what is drawn.
        addHeader("FilterHeader", "Hide Flows Below:");
        addSlider("MinFlow", 0.0f, 300.0f, 10.0f, 0.0f,
                  [chart](float value) { chart->SetMinFlowValue(value); });

        addHeader("DimHeader", "Dim Non-Hovered:");
        addSlider("Dim", 0.0f, 1.0f, 0.05f, 0.25f,
                  [chart](float value) { chart->SetDimmedOpacity(value); });

        auto hoverCheckbox = std::make_shared<UltraCanvasCheckbox>(
            idPrefix + "Hover", kChordControlWidth, 24);
        hoverCheckbox->SetText("Hover Highlight");
        hoverCheckbox->SetChecked(true);
        hoverCheckbox->onStateChanged = [chart](CheckedState, CheckedState newState) {
            chart->SetHoverHighlightEnabled(newState == CheckedState::Checked);
        };
        hoverCheckbox->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        panel->AddChild(hoverCheckbox);

        auto hintLabel = std::make_shared<UltraCanvasLabel>(
            idPrefix + "Hint", kChordControlWidth, 66,
            "Hover a category arc to isolate\nits ribbons, or a single ribbon\nfor the exact flow and its\nreturn leg.");
        hintLabel->SetFontSize(10);
        hintLabel->SetTextColor(Color(110, 110, 110, 255));
        hintLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        panel->AddChild(hintLabel);

        return panel;
    }

// ===== TAB ASSEMBLY =====

    // One tab: a caption over a chart that takes every pixel the control panel
    // leaves, both horizontally and vertically.
    static std::shared_ptr<UltraCanvasContainer> MakeChordTab(
        const std::string& idPrefix,
        const std::string& caption,
        std::shared_ptr<UltraCanvasChordChart> chart,
        const ChordPanelDefaults& defaults)
    {
        auto tab = std::make_shared<UltraCanvasContainer>(idPrefix + "Tab");
        tab->layout.SetFlexRow().SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        tab->SetPadding(6);

        auto chartColumn = std::make_shared<UltraCanvasContainer>(idPrefix + "ChartColumn");
        chartColumn->layout.SetFlexColumn().SetFlexGap(4)
                           .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        chartColumn->layoutItem.SetFlex(1, 1, CSSLayout::Dimension::Px(0))
                               .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        auto captionLabel = std::make_shared<UltraCanvasLabel>(
            idPrefix + "Caption", 0, 22, caption);
        captionLabel->SetFontSize(12);
        captionLabel->SetTextColor(Color(90, 90, 100, 255));
        captionLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        chartColumn->AddChild(captionLabel);

        // The chart is built at zero size and sized entirely by the layout
        // engine, so it re-fits its ring whenever the window changes.
        chart->layoutItem.SetFlex(1, 1, CSSLayout::Dimension::Px(0))
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        chartColumn->AddChild(chart);
        tab->AddChild(chartColumn);

        auto panel = CreateChordControlPanel(idPrefix, chart, defaults);
        panel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        tab->AddChild(panel);

        return tab;
    }

    // ----- TAB 1: GLOBAL TRADE FLOWS -----
    static std::shared_ptr<UltraCanvasContainer> MakeTradeTab() {
        auto chart = CreateChordChart("TradeChord", 0, 0, 0, 0);
        BuildGlobalTradeData(chart);
        chart->SetTooltipsEnabled(true);
        chart->SetValueFormatter([](double v) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "$%.0fB", v);
            return std::string(buf);
        });

        return MakeChordTab("CcTrade",
                            "Global merchandise trade between eight world regions, in billions USD.",
                            chart, ChordPanelDefaults{});
    }

    // ----- TAB 2: MIGRATION CORRIDORS (STRONG ASYMMETRY) -----
    static std::shared_ptr<UltraCanvasContainer> MakeMigrationTab() {
        auto chart = CreateChordChart("MigrationChord", 0, 0, 0, 0);
        BuildMigrationData(chart);
        chart->SetTooltipsEnabled(true);
        // Source colouring makes the direction of each corridor obvious.
        chart->SetRibbonColorMode(ChordRibbonColorMode::Source);
        chart->SetValueFormatter([](double v) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.0fk", v);
            return std::string(buf);
        });

        ChordPanelDefaults defaults;
        defaults.colorModeIndex = 1;  // Source
        return MakeChordTab("CcMigration",
                            "Migration corridors in thousands of people - several run almost entirely one way.",
                            chart, defaults);
    }

    // ----- TAB 3: PROJECT PHASE HANDOVERS -----
    static std::shared_ptr<UltraCanvasContainer> MakePhaseTab() {
        auto chart = CreateChordChart("PhaseChord", 0, 0, 0, 0);
        BuildProjectPhaseData(chart);
        chart->SetTooltipsEnabled(true);
        chart->SetCategoryPadding(4.0f);
        chart->SetRibbonColorMode(ChordRibbonColorMode::Source);
        chart->SetRibbonOpacity(0.8f);

        ChordPanelDefaults defaults;
        defaults.ribbonOpacity   = 0.8f;
        defaults.categoryPadding = 4.0f;
        defaults.colorModeIndex  = 1;  // Source
        return MakeChordTab("CcPhase",
                            "Handovers between project phases: a sparse matrix with cyclical feedback edges.",
                            chart, defaults);
    }

    // ----- TAB 4: TEAM COLLABORATION (BLENDED, RADIAL LABELS) -----
    static std::shared_ptr<UltraCanvasContainer> MakeTeamTab() {
        auto chart = CreateChordChart("TeamChord", 0, 0, 0, 0);
        BuildTeamCollaborationData(chart);
        chart->SetTooltipsEnabled(true);
        chart->SetRibbonColorMode(ChordRibbonColorMode::Blend);
        chart->SetLabelPlacement(ChordLabelPlacement::Radial);
        chart->SetRingThickness(14.0f);
        chart->SetRibbonBorder(Color(255, 255, 255, 140), 1.0f);

        ChordPanelDefaults defaults;
        defaults.ringThickness  = 14.0f;
        defaults.colorModeIndex = 3;  // Blend
        defaults.labelModeIndex = 1;  // Radial
        return MakeChordTab("CcTeam",
                            "Cross-team ticket handovers: blended ribbon colours, radial labels, thin ring.",
                            chart, defaults);
    }

// ===== MAIN CHORD CHART EXAMPLES CREATOR =====

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateChordChartExamples() {
        // Stretchable flex column: title and description keep their height, the
        // tabs take the rest. Auto size so the host's flex-grow/stretch sizes the
        // page to the display area and a window resize propagates down through
        // Measure/Arrange to each chart.
        auto container = std::make_shared<UltraCanvasContainer>("ChordChartContainer");
        container->SetBackgroundColor(Color(255, 255, 255, 255));
        container->SetPadding(8, 10);
        container->layout.SetFlexColumn().SetFlexGap(6)
                         .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        container->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        auto titleLabel = std::make_shared<UltraCanvasLabel>("CcTitleLabel", 0, 32);
        titleLabel->SetText("Chord Chart Examples - Directed Flows Between Categories");
        titleLabel->SetFontSize(18);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetAlignment(TextAlignment::Center);
        titleLabel->SetBackgroundColor(Color(240, 240, 250, 255));
        titleLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        container->AddChild(titleLabel);

        // Explicit line breaks and a fixed height rather than word wrap: the
        // description is re-measured whenever the page is re-laid out, and a
        // wrapped label re-measures against an unconstrained width, which leaves
        // the text on one over-long line that the page then clips.
        auto descLabel = std::make_shared<UltraCanvasLabel>("CcDescLabel", 0, 56);
        descLabel->SetText(
            "Categories occupy arcs sized by total throughput; ribbons join them with a width proportional to the flow.\n"
            "Each direction gets its own sub-arc, so an asymmetric relationship reads as a ribbon that is wide at one end\n"
            "and narrow at the other. Hover isolates a category or a ribbon; tooltips report the flow and its return leg."
        );
        descLabel->SetFontSize(11);
        descLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        container->AddChild(descLabel);

        auto tabs = std::make_shared<UltraCanvasTabbedContainer>("ChordTabs", 0, 0, 0, 0);
        tabs->SetTabPosition(TabPosition::Top);
        tabs->SetTabStyle(TabStyle::Modern);
        tabs->layoutItem.SetFlex(1, 1, CSSLayout::Dimension::Px(0))
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        tabs->AddTab("Global Merchandise Trade", MakeTradeTab());
        tabs->AddTab("Migration Corridors",      MakeMigrationTab());
        tabs->AddTab("Project Phase Handovers",  MakePhaseTab());
        tabs->AddTab("Cross-Team Handovers",     MakeTeamTab());

        container->AddChild(tabs);

        return container;
    }

} // namespace UltraCanvas

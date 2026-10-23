// Apps/DemoApp/UltraCanvasSunburstChartExamples.cpp
// Comprehensive sunburst chart demonstration with all features
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the controls in a
//                  card above the charts, one card per chart, checkboxes sized to their text
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Charts/UltraCanvasSunburstChart.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include <cstdio>

namespace UltraCanvas {

// ===== SAMPLE DATA BUILDERS =====

    // Three-level retail hierarchy: Region -> Category -> Sub-category.
    // Modeled after the classic "Total Sales" sunburst dashboards.
    static std::shared_ptr<SunburstNode> BuildSalesHierarchy() {
        auto root = std::make_shared<SunburstNode>("Total Sales");

        auto central = root->AddChild("Central");
        auto cFurniture = central->AddChild("Furniture");
        cFurniture->AddChild("Chairs", 85);
        cFurniture->AddChild("Tables", 48);
        cFurniture->AddChild("Bookcases", 34);
        auto cOffice = central->AddChild("Office Supplies");
        cOffice->AddChild("Binders", 56);
        cOffice->AddChild("Storage", 44);
        cOffice->AddChild("Paper", 31);
        auto cTech = central->AddChild("Technology");
        cTech->AddChild("Phones", 98);
        cTech->AddChild("Machines", 62);

        auto east = root->AddChild("East");
        auto eFurniture = east->AddChild("Furniture");
        eFurniture->AddChild("Chairs", 96);
        eFurniture->AddChild("Tables", 39);
        auto eOffice = east->AddChild("Office Supplies");
        eOffice->AddChild("Binders", 53);
        eOffice->AddChild("Appliances", 47);
        auto eTech = east->AddChild("Technology");
        eTech->AddChild("Phones", 114);
        eTech->AddChild("Copiers", 74);
        eTech->AddChild("Accessories", 45);

        auto south = root->AddChild("South");
        auto sFurniture = south->AddChild("Furniture");
        sFurniture->AddChild("Chairs", 45);
        sFurniture->AddChild("Tables", 22);
        auto sOffice = south->AddChild("Office Supplies");
        sOffice->AddChild("Binders", 37);
        sOffice->AddChild("Storage", 27);
        auto sTech = south->AddChild("Technology");
        sTech->AddChild("Phones", 58);
        sTech->AddChild("Machines", 40);

        auto west = root->AddChild("West");
        auto wFurniture = west->AddChild("Furniture");
        wFurniture->AddChild("Chairs", 101);
        wFurniture->AddChild("Bookcases", 43);
        auto wOffice = west->AddChild("Office Supplies");
        wOffice->AddChild("Binders", 68);
        wOffice->AddChild("Storage", 51);
        wOffice->AddChild("Art", 24);
        auto wTech = west->AddChild("Technology");
        wTech->AddChild("Phones", 126);
        wTech->AddChild("Copiers", 83);

        return root;
    }

    // Two-level demographic data: continent -> country (median age of population).
    static std::shared_ptr<UltraCanvasSunburstChart> BuildMedianAgeChart(
        const std::string& id, int x, int y, int w, int h)
    {
        std::vector<std::pair<std::string,
            std::vector<std::pair<std::string, double>>>> geoData = {
            {"North America", {{"United States", 38.5}, {"Canada", 41.8},
                               {"Mexico", 29.2}, {"Cuba", 42.1}}},
            {"South America", {{"Brazil", 33.2}, {"Argentina", 31.9},
                               {"Chile", 35.4}, {"Colombia", 31.0}}},
            {"Europe",        {{"Germany", 47.8}, {"France", 41.7},
                               {"United Kingdom", 40.6}, {"Italy", 46.5},
                               {"Spain", 44.9}}},
            {"Africa",        {{"Nigeria", 18.1}, {"Ethiopia", 19.5},
                               {"Egypt", 24.6}, {"South Africa", 27.3}}},
            {"Asia",          {{"China", 38.4}, {"India", 28.2},
                               {"Japan", 48.6}, {"Indonesia", 30.2},
                               {"Vietnam", 32.5}}},
            {"Oceania",       {{"Australia", 38.7}, {"New Zealand", 38.4},
                               {"Fiji", 28.9}}}
        };
        return CreateSunburstChartFromGroups(id, x, y, w, h, geoData);
    }

    // Hierarchical approval workflow, rendered as a partial (fan) sunburst.
    static std::shared_ptr<SunburstNode> BuildApprovalHierarchy() {
        auto root = std::make_shared<SunburstNode>("Requests");

        auto approved = root->AddChild("Approved", 0, Color(76, 175, 80, 255));
        approved->AddChild("Automatic", 380);
        approved->AddChild("Manual", 175);
        approved->AddChild("Escalated", 85);

        auto pending = root->AddChild("Pending", 0, Color(255, 152, 0, 255));
        pending->AddChild("In Review", 140);
        pending->AddChild("Waiting Info", 90);

        auto rejected = root->AddChild("Rejected", 0, Color(211, 47, 47, 255));
        rejected->AddChild("Policy", 120);
        rejected->AddChild("Fraud Check", 45);
        rejected->AddChild("Incomplete", 65);

        return root;
    }

// ===== CONTROL PANEL =====

    // The groups sit side by side and wrap under each other when narrow;
    // dropdowns and sliders keep their 180 px, checkboxes fit their text.
    static void CreateSunburstControlPanel(
        std::shared_ptr<UltraCanvasContainer> container,
        std::vector<std::shared_ptr<UltraCanvasSunburstChart>> charts)
    {
        const int controlWidth = 180;
        const int controlHeight = 25;

        auto groups = DemoKit::AddRow(container, "SbControlGroups", 32);
        auto addGroup = [&groups](const std::string& id, const std::string& headerId,
                                  const std::string& header) {
            auto group = DemoKit::CreateColumn(id, 8);
            DemoKit::Add(groups, group);
            auto label = DemoKit::AddText(group, headerId, header);
            label->SetFontWeight(FontWeight::Bold);
            return group;
        };

        auto labelsGroup = addGroup("SbLabelsGroup", "SbLabelsHeader", "Labels:");

        auto contentDropdown = std::make_shared<UltraCanvasDropdown>("SbLabelContent", 0, 0, controlWidth, controlHeight);
        contentDropdown->AddItem("Name");
        contentDropdown->AddItem("Name + Value");
        contentDropdown->AddItem("Name + Percentage");
        contentDropdown->AddItem("Value");
        contentDropdown->AddItem("Percentage");
        contentDropdown->AddItem("None");
        contentDropdown->SetSelectedIndex(0);
        contentDropdown->onSelectionChanged = [charts](int index, const DropdownItem&) {
            SunburstLabelContent content;
            switch (index) {
                case 1:  content = SunburstLabelContent::NameValue; break;
                case 2:  content = SunburstLabelContent::NamePercentage; break;
                case 3:  content = SunburstLabelContent::Value; break;
                case 4:  content = SunburstLabelContent::Percentage; break;
                case 5:  content = SunburstLabelContent::NoLabel; break;
                default: content = SunburstLabelContent::Name; break;
            }
            for (auto& c : charts) c->SetLabelContent(content);
        };
        DemoKit::AddFixed(labelsGroup, contentDropdown);

        auto orientationDropdown = std::make_shared<UltraCanvasDropdown>("SbLabelOrientation", 0, 0, controlWidth, controlHeight);
        orientationDropdown->AddItem("Auto");
        orientationDropdown->AddItem("Horizontal");
        orientationDropdown->AddItem("Radial");
        orientationDropdown->SetSelectedIndex(0);
        orientationDropdown->onSelectionChanged = [charts](int index, const DropdownItem&) {
            SunburstLabelOrientation orientation;
            switch (index) {
                case 1:  orientation = SunburstLabelOrientation::Horizontal; break;
                case 2:  orientation = SunburstLabelOrientation::Radial; break;
                default: orientation = SunburstLabelOrientation::Auto; break;
            }
            for (auto& c : charts) c->SetLabelOrientation(orientation);
        };
        DemoKit::AddFixed(labelsGroup, orientationDropdown);

        auto shadeGroup = addGroup("SbShadeGroup", "SbShadeHeader", "Depth Shading:");

        auto shadeDropdown = std::make_shared<UltraCanvasDropdown>("SbShadeMode", 0, 0, controlWidth, controlHeight);
        shadeDropdown->AddItem("Lighten By Depth");
        shadeDropdown->AddItem("Darken By Depth");
        shadeDropdown->AddItem("None");
        shadeDropdown->SetSelectedIndex(0);
        shadeDropdown->onSelectionChanged = [charts](int index, const DropdownItem&) {
            SunburstShadeMode mode;
            switch (index) {
                case 1:  mode = SunburstShadeMode::DarkenByDepth; break;
                case 2:  mode = SunburstShadeMode::NoShade; break;
                default: mode = SunburstShadeMode::LightenByDepth; break;
            }
            for (auto& c : charts) c->SetShadeMode(mode);
        };
        DemoKit::AddFixed(shadeGroup, shadeDropdown);

        auto geometryGroup = addGroup("SbGeometryGroup", "SbGeometryHeader", "Center Hole Size:");

        auto innerSlider = std::make_shared<UltraCanvasSlider>("SbInnerRadius", 0, 0, controlWidth, controlHeight);
        innerSlider->SetRange(0.05, 0.6);
        innerSlider->SetStep(0.01);
        innerSlider->SetValue(0.25);
        innerSlider->onValueChanged = [charts](double value) {
            for (auto& c : charts) c->SetInnerRadiusFraction(static_cast<float>(value));
        };
        DemoKit::AddFixed(geometryGroup, innerSlider);

        auto gapHeader = DemoKit::AddText(geometryGroup, "SbGapHeader", "Segment Gap (degrees):");
        gapHeader->SetFontWeight(FontWeight::Bold);

        auto gapSlider = std::make_shared<UltraCanvasSlider>("SbSegmentGap", 0, 0, controlWidth, controlHeight);
        gapSlider->SetRange(0.0, 4.0);
        gapSlider->SetStep(0.1);
        gapSlider->SetValue(0.4);
        gapSlider->onValueChanged = [charts](double value) {
            for (auto& c : charts) c->SetSegmentSpacingAngle(static_cast<float>(value));
        };
        DemoKit::AddFixed(geometryGroup, gapSlider);

        auto borderHeader = DemoKit::AddText(geometryGroup, "SbBorderHeader", "Border Width:");
        borderHeader->SetFontWeight(FontWeight::Bold);

        auto borderSlider = std::make_shared<UltraCanvasSlider>("SbBorderWidth", 0, 0, controlWidth, controlHeight);
        borderSlider->SetRange(0.0, 5.0);
        borderSlider->SetStep(0.5);
        borderSlider->SetValue(1.5);
        borderSlider->onValueChanged = [charts](double value) {
            for (auto& c : charts) c->SetBorderWidth(static_cast<float>(value));
        };
        DemoKit::AddFixed(geometryGroup, borderSlider);

        auto interactionGroup = addGroup("SbInteractionGroup", "SbInteractionHeader", "Interaction:");

        auto hoverCheckbox = std::make_shared<UltraCanvasCheckbox>("SbHover", "Hover Highlight");
        hoverCheckbox->SetChecked(true);
        hoverCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetHoverHighlightEnabled(newState == CheckedState::Checked);
        };
        DemoKit::Add(interactionGroup, hoverCheckbox);

        auto drillCheckbox = std::make_shared<UltraCanvasCheckbox>("SbDrill", "Drill-Down On Click");
        drillCheckbox->SetChecked(true);
        drillCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetDrillDownEnabled(newState == CheckedState::Checked);
        };
        DemoKit::Add(interactionGroup, drillCheckbox);

        DemoKit::AddCaption(interactionGroup, "SbHint",
            "Click a branch segment to zoom into it; click the center to go back up one level.");
    }

// ===== MAIN SUNBURST CHART EXAMPLES CREATOR =====

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSunburstChartExamples() {
        auto page = DemoKit::CreatePage("SunburstChartContainer",
            "Sunburst Chart Examples - Hierarchical Insights in a Single View",
            "Multi-level radial partition charts: angular spans are proportional to aggregated values, rings represent "
            "hierarchy depth. Features drill-down zoom, depth shading, per-node colors, tooltips with breadcrumb paths, "
            "auto-fitting radial/horizontal labels, partial sweeps and configurable geometry."
        );

        auto controlsCard = DemoKit::AddCard(page, "SbControls");
        // A card holds a 460 px chart without squeezing it.
        constexpr float chartCardWidth = 460 + 2 * DemoKit::kCardPadding + 2;
        auto charts = DemoKit::AddRow(page, "SbCharts");

        // ===== EXAMPLE 1: THREE-LEVEL SALES HIERARCHY WITH DRILL-DOWN =====
        auto salesCard = DemoKit::AddCard(charts, "SbSalesLabel", "Regional Sales (Region › Category › Sub-Category)", "", chartCardWidth);

        auto salesChart = CreateSunburstChart("SalesSunburst", 0, 0, 460, 320);
        salesChart->SetRootNode(BuildSalesHierarchy());
        salesChart->SetCenterText("Total Sales");
        salesChart->SetValueFormatter([](double v) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "$%.0fK", v);
            return std::string(buf);
        });
        salesChart->SetInnerRadiusFraction(0.25f);
        salesChart->SetDrillDownEnabled(true);
        salesChart->SetHoverHighlightEnabled(true);
        salesChart->SetTooltipsEnabled(true);
        salesChart->SetLabelContent(SunburstLabelContent::Name);
        salesChart->SetLabelOrientation(SunburstLabelOrientation::Auto);
        DemoKit::AddFixed(salesCard, salesChart);

        // ===== EXAMPLE 2: DEMOGRAPHIC TWO-LEVEL SUNBURST =====
        auto geoCard = DemoKit::AddCard(charts, "SbGeoLabel", "Median Age of Population (Continent › Country)", "", chartCardWidth);

        auto geoChart = BuildMedianAgeChart("GeoSunburst", 0, 0, 460, 320);
        geoChart->SetColorPalette({
            Color( 33, 150, 243, 255),   // Blue - North America
            Color(255, 193,   7, 255),   // Amber - South America
            Color(  3, 169, 244, 255),   // Light blue - Europe
            Color( 76, 175,  80, 255),   // Green - Africa
            Color(233,  30,  99, 255),   // Pink - Asia
            Color(156,  39, 176, 255)    // Purple - Oceania
        });
        geoChart->SetShowCenterTotal(false);
        geoChart->SetCenterText("Median Age");
        geoChart->SetInnerRadiusFraction(0.22f);
        geoChart->SetDrillDownEnabled(true);
        geoChart->SetTooltipsEnabled(true);
        geoChart->SetLabelContent(SunburstLabelContent::Name);
        geoChart->SetValueFormatter([](double v) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f yrs", v);
            return std::string(buf);
        });
        DemoKit::AddFixed(geoCard, geoChart);

        // ===== EXAMPLE 3: PARTIAL-SWEEP (FAN) SUNBURST =====
        auto fanCard = DemoKit::AddCard(charts, "SbFanLabel", "Approval Workflow (240° Fan, Explicit Branch Colors)", "", chartCardWidth);

        auto fanChart = CreateSunburstChart("FanSunburst", 0, 0, 460, 280);
        fanChart->SetRootNode(BuildApprovalHierarchy());
        fanChart->SetSweepAngle(240.0f);
        fanChart->SetStartAngle(-210.0f);
        fanChart->SetInnerRadiusFraction(0.3f);
        fanChart->SetSegmentSpacingAngle(1.0f);
        fanChart->SetCenterText("Requests");
        fanChart->SetTooltipsEnabled(true);
        fanChart->SetLabelContent(SunburstLabelContent::NamePercentage);
        DemoKit::AddFixed(fanCard, fanChart);

        // ===== EXAMPLE 4: DEPTH-LIMITED OVERVIEW =====
        auto overviewCard = DemoKit::AddCard(charts, "SbOverviewLabel", "Overview Mode (Depth Limited To 2, Darken Shading)", "", chartCardWidth);

        auto overviewChart = CreateSunburstChart("OverviewSunburst", 0, 0, 460, 280);
        overviewChart->SetRootNode(BuildSalesHierarchy());
        overviewChart->SetMaxVisibleDepth(2);
        overviewChart->SetShadeMode(SunburstShadeMode::DarkenByDepth);
        overviewChart->SetInnerRadiusFraction(0.35f);
        overviewChart->SetRingSpacing(3.0f);
        overviewChart->SetBorderWidth(2.0f);
        overviewChart->SetCenterText("Sales");
        overviewChart->SetTooltipsEnabled(true);
        overviewChart->SetLabelContent(SunburstLabelContent::NamePercentage);
        overviewChart->SetValueFormatter([](double v) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "$%.0fK", v);
            return std::string(buf);
        });
        DemoKit::AddFixed(overviewCard, overviewChart);

        // === CONTROL PANEL (above the charts) ===
        CreateSunburstControlPanel(controlsCard,
                                   {salesChart, geoChart, fanChart, overviewChart});

        return page;
    }

} // namespace UltraCanvas

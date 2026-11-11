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
        auto root = std::make_shared<SunburstNode>(Tr("Total Sales"));

        auto central = root->AddChild(Tr("Central"));
        auto cFurniture = central->AddChild(Tr("Furniture"));
        cFurniture->AddChild(Tr("Chairs"), 85);
        cFurniture->AddChild(Tr("Tables"), 48);
        cFurniture->AddChild(Tr("Bookcases"), 34);
        auto cOffice = central->AddChild(Tr("Office Supplies"));
        cOffice->AddChild(Tr("Binders"), 56);
        cOffice->AddChild(Tr("Storage"), 44);
        cOffice->AddChild(Tr("Paper"), 31);
        auto cTech = central->AddChild(Tr("Technology"));
        cTech->AddChild(Tr("Phones"), 98);
        cTech->AddChild(Tr("Machines"), 62);

        auto east = root->AddChild(Tr("East"));
        auto eFurniture = east->AddChild(Tr("Furniture"));
        eFurniture->AddChild(Tr("Chairs"), 96);
        eFurniture->AddChild(Tr("Tables"), 39);
        auto eOffice = east->AddChild(Tr("Office Supplies"));
        eOffice->AddChild(Tr("Binders"), 53);
        eOffice->AddChild(Tr("Appliances"), 47);
        auto eTech = east->AddChild(Tr("Technology"));
        eTech->AddChild(Tr("Phones"), 114);
        eTech->AddChild(Tr("Copiers"), 74);
        eTech->AddChild(Tr("Accessories"), 45);

        auto south = root->AddChild(Tr("South"));
        auto sFurniture = south->AddChild(Tr("Furniture"));
        sFurniture->AddChild(Tr("Chairs"), 45);
        sFurniture->AddChild(Tr("Tables"), 22);
        auto sOffice = south->AddChild(Tr("Office Supplies"));
        sOffice->AddChild(Tr("Binders"), 37);
        sOffice->AddChild(Tr("Storage"), 27);
        auto sTech = south->AddChild(Tr("Technology"));
        sTech->AddChild(Tr("Phones"), 58);
        sTech->AddChild(Tr("Machines"), 40);

        auto west = root->AddChild(Tr("West"));
        auto wFurniture = west->AddChild(Tr("Furniture"));
        wFurniture->AddChild(Tr("Chairs"), 101);
        wFurniture->AddChild(Tr("Bookcases"), 43);
        auto wOffice = west->AddChild(Tr("Office Supplies"));
        wOffice->AddChild(Tr("Binders"), 68);
        wOffice->AddChild(Tr("Storage"), 51);
        wOffice->AddChild(Tr("Art"), 24);
        auto wTech = west->AddChild(Tr("Technology"));
        wTech->AddChild(Tr("Phones"), 126);
        wTech->AddChild(Tr("Copiers"), 83);

        return root;
    }

    // Two-level demographic data: continent -> country (median age of population).
    static std::shared_ptr<UltraCanvasSunburstChart> BuildMedianAgeChart(
        const std::string& id, int x, int y, int w, int h)
    {
        std::vector<std::pair<std::string,
            std::vector<std::pair<std::string, double>>>> geoData = {
            {Tr("North America"), {{Tr("United States"), 38.5}, {Tr("Canada"), 41.8},
                               {Tr("Mexico"), 29.2}, {Tr("Cuba"), 42.1}}},
            {Tr("South America"), {{Tr("Brazil"), 33.2}, {Tr("Argentina"), 31.9},
                               {Tr("Chile"), 35.4}, {Tr("Colombia"), 31.0}}},
            {Tr("Europe"),        {{Tr("Germany"), 47.8}, {Tr("France"), 41.7},
                               {Tr("United Kingdom"), 40.6}, {Tr("Italy"), 46.5},
                               {Tr("Spain"), 44.9}}},
            {Tr("Africa"),        {{Tr("Nigeria"), 18.1}, {Tr("Ethiopia"), 19.5},
                               {Tr("Egypt"), 24.6}, {Tr("South Africa"), 27.3}}},
            {Tr("Asia"),          {{Tr("China"), 38.4}, {Tr("India"), 28.2},
                               {Tr("Japan"), 48.6}, {Tr("Indonesia"), 30.2},
                               {Tr("Vietnam"), 32.5}}},
            {Tr("Oceania"),       {{Tr("Australia"), 38.7}, {Tr("New Zealand"), 38.4},
                               {Tr("Fiji"), 28.9}}}
        };
        return CreateSunburstChartFromGroups(id, x, y, w, h, geoData);
    }

    // Hierarchical approval workflow, rendered as a partial (fan) sunburst.
    static std::shared_ptr<SunburstNode> BuildApprovalHierarchy() {
        auto root = std::make_shared<SunburstNode>(Tr("Requests"));

        auto approved = root->AddChild(Tr("Approved"), 0, Color(76, 175, 80, 255));
        approved->AddChild(Tr("Automatic"), 380);
        approved->AddChild(Tr("Manual"), 175);
        approved->AddChild(Tr("Escalated"), 85);

        auto pending = root->AddChild(Tr("Pending"), 0, Color(255, 152, 0, 255));
        pending->AddChild(Tr("In Review"), 140);
        pending->AddChild(Tr("Waiting Info"), 90);

        auto rejected = root->AddChild(Tr("Rejected"), 0, Color(211, 47, 47, 255));
        rejected->AddChild(Tr("Policy"), 120);
        rejected->AddChild(Tr("Fraud Check"), 45);
        rejected->AddChild(Tr("Incomplete"), 65);

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

        auto labelsGroup = addGroup("SbLabelsGroup", "SbLabelsHeader", Tr("Labels:"));

        auto contentDropdown = std::make_shared<UltraCanvasDropdown>("SbLabelContent", 0, 0, controlWidth, controlHeight);
        contentDropdown->AddItem(Tr("Name"));
        contentDropdown->AddItem(Tr("Name + Value"));
        contentDropdown->AddItem(Tr("Name + Percentage"));
        contentDropdown->AddItem(Tr("Value"));
        contentDropdown->AddItem(Tr("Percentage"));
        contentDropdown->AddItem(Tr("None"));
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
        orientationDropdown->AddItem(Tr("Auto"));
        orientationDropdown->AddItem(Tr("Horizontal"));
        orientationDropdown->AddItem(Tr("Radial"));
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

        auto shadeGroup = addGroup("SbShadeGroup", "SbShadeHeader", Tr("Depth Shading:"));

        auto shadeDropdown = std::make_shared<UltraCanvasDropdown>("SbShadeMode", 0, 0, controlWidth, controlHeight);
        shadeDropdown->AddItem(Tr("Lighten By Depth"));
        shadeDropdown->AddItem(Tr("Darken By Depth"));
        shadeDropdown->AddItem(Tr("None"));
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

        auto geometryGroup = addGroup("SbGeometryGroup", "SbGeometryHeader", Tr("Center Hole Size:"));

        auto innerSlider = std::make_shared<UltraCanvasSlider>("SbInnerRadius", 0, 0, controlWidth, controlHeight);
        innerSlider->SetRange(0.05, 0.6);
        innerSlider->SetStep(0.01);
        innerSlider->SetValue(0.25);
        innerSlider->onValueChanged = [charts](double value) {
            for (auto& c : charts) c->SetInnerRadiusFraction(static_cast<float>(value));
        };
        DemoKit::AddFixed(geometryGroup, innerSlider);

        auto gapHeader = DemoKit::AddText(geometryGroup, "SbGapHeader", Tr("Segment Gap (degrees):"));
        gapHeader->SetFontWeight(FontWeight::Bold);

        auto gapSlider = std::make_shared<UltraCanvasSlider>("SbSegmentGap", 0, 0, controlWidth, controlHeight);
        gapSlider->SetRange(0.0, 4.0);
        gapSlider->SetStep(0.1);
        gapSlider->SetValue(0.4);
        gapSlider->onValueChanged = [charts](double value) {
            for (auto& c : charts) c->SetSegmentSpacingAngle(static_cast<float>(value));
        };
        DemoKit::AddFixed(geometryGroup, gapSlider);

        auto borderHeader = DemoKit::AddText(geometryGroup, "SbBorderHeader", Tr("Border Width:"));
        borderHeader->SetFontWeight(FontWeight::Bold);

        auto borderSlider = std::make_shared<UltraCanvasSlider>("SbBorderWidth", 0, 0, controlWidth, controlHeight);
        borderSlider->SetRange(0.0, 5.0);
        borderSlider->SetStep(0.5);
        borderSlider->SetValue(1.5);
        borderSlider->onValueChanged = [charts](double value) {
            for (auto& c : charts) c->SetBorderWidth(static_cast<float>(value));
        };
        DemoKit::AddFixed(geometryGroup, borderSlider);

        auto interactionGroup = addGroup("SbInteractionGroup", "SbInteractionHeader", Tr("Interaction:"));

        auto hoverCheckbox = std::make_shared<UltraCanvasCheckbox>("SbHover", Tr("Hover Highlight"));
        hoverCheckbox->SetChecked(true);
        hoverCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetHoverHighlightEnabled(newState == CheckedState::Checked);
        };
        DemoKit::Add(interactionGroup, hoverCheckbox);

        auto drillCheckbox = std::make_shared<UltraCanvasCheckbox>("SbDrill", Tr("Drill-Down On Click"));
        drillCheckbox->SetChecked(true);
        drillCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetDrillDownEnabled(newState == CheckedState::Checked);
        };
        DemoKit::Add(interactionGroup, drillCheckbox);

        DemoKit::AddCaption(interactionGroup, "SbHint",
            Tr("Click a branch segment to zoom into it; click the center to go back up one level."));
    }

// ===== MAIN SUNBURST CHART EXAMPLES CREATOR =====

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSunburstChartExamples() {
        auto page = DemoKit::CreatePage("SunburstChartContainer",
            Tr("Sunburst Chart Examples - Hierarchical Insights in a Single View"),
            Tr("Multi-level radial partition charts: angular spans are proportional to aggregated values, rings represent "
            "hierarchy depth. Features drill-down zoom, depth shading, per-node colors, tooltips with breadcrumb paths, "
            "auto-fitting radial/horizontal labels, partial sweeps and configurable geometry.")
        );

        auto controlsCard = DemoKit::AddCard(page, "SbControls");
        // A card holds a 460 px chart without squeezing it.
        constexpr float chartCardWidth = 460 + 2 * DemoKit::kCardPadding + 2;
        auto charts = DemoKit::AddRow(page, "SbCharts");

        // ===== EXAMPLE 1: THREE-LEVEL SALES HIERARCHY WITH DRILL-DOWN =====
        auto salesCard = DemoKit::AddCard(charts, "SbSalesLabel", Tr("Regional Sales (Region › Category › Sub-Category)"), "", chartCardWidth);

        auto salesChart = CreateSunburstChart("SalesSunburst", 0, 0, 460, 320);
        salesChart->SetRootNode(BuildSalesHierarchy());
        salesChart->SetCenterText(Tr("Total Sales"));
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
        auto geoCard = DemoKit::AddCard(charts, "SbGeoLabel", Tr("Median Age of Population (Continent › Country)"), "", chartCardWidth);

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
        geoChart->SetCenterText(Tr("Median Age"));
        geoChart->SetInnerRadiusFraction(0.22f);
        geoChart->SetDrillDownEnabled(true);
        geoChart->SetTooltipsEnabled(true);
        geoChart->SetLabelContent(SunburstLabelContent::Name);
        geoChart->SetValueFormatter([](double v) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f", v);
            return TrF("{0} yrs", std::string(buf));
        });
        DemoKit::AddFixed(geoCard, geoChart);

        // ===== EXAMPLE 3: PARTIAL-SWEEP (FAN) SUNBURST =====
        auto fanCard = DemoKit::AddCard(charts, "SbFanLabel", Tr("Approval Workflow (240° Fan, Explicit Branch Colors)"), "", chartCardWidth);

        auto fanChart = CreateSunburstChart("FanSunburst", 0, 0, 460, 280);
        fanChart->SetRootNode(BuildApprovalHierarchy());
        fanChart->SetSweepAngle(240.0f);
        fanChart->SetStartAngle(-210.0f);
        fanChart->SetInnerRadiusFraction(0.3f);
        fanChart->SetSegmentSpacingAngle(1.0f);
        fanChart->SetCenterText(Tr("Requests"));
        fanChart->SetTooltipsEnabled(true);
        fanChart->SetLabelContent(SunburstLabelContent::NamePercentage);
        DemoKit::AddFixed(fanCard, fanChart);

        // ===== EXAMPLE 4: DEPTH-LIMITED OVERVIEW =====
        auto overviewCard = DemoKit::AddCard(charts, "SbOverviewLabel", Tr("Overview Mode (Depth Limited To 2, Darken Shading)"), "", chartCardWidth);

        auto overviewChart = CreateSunburstChart("OverviewSunburst", 0, 0, 460, 280);
        overviewChart->SetRootNode(BuildSalesHierarchy());
        overviewChart->SetMaxVisibleDepth(2);
        overviewChart->SetShadeMode(SunburstShadeMode::DarkenByDepth);
        overviewChart->SetInnerRadiusFraction(0.35f);
        overviewChart->SetRingSpacing(3.0f);
        overviewChart->SetBorderWidth(2.0f);
        overviewChart->SetCenterText(Tr("Sales"));
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

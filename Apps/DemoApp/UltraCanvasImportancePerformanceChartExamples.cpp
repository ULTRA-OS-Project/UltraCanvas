// Apps/DemoApp/UltraCanvasImportancePerformanceChartExamples.cpp
// Importance-performance chart examples: Slack's zones, smooth and stepped,
// over the service factors of an invented parcel carrier
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the chart and its
//                  controls in cards, every text wraps, the listing as tall as it needs
// Version: 1.0.0
// Last Modified: 2026-10-24
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasImportancePerformanceChart.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    // The factors of each zone, listed under the zone's title: the reading a
    // manager takes away from the chart.
    static std::string ZoneListing(const UltraCanvasImportancePerformanceChart& chart) {
        const ImportancePerformanceModel& model = chart.GetModel();
        const std::vector<int>& zones = chart.GetFactorZones();
        std::string text;
        // Most pressing first: urgent action, improve, then the rest in model order.
        std::vector<size_t> order;
        for (const char* id : {"urgent", "improve"}) {
            for (size_t z = 0; z < model.zones.size(); ++z) {
                if (model.zones[z].id == id) order.push_back(z);
            }
        }
        for (size_t z = 0; z < model.zones.size(); ++z) {
            if (std::find(order.begin(), order.end(), z) == order.end()) order.push_back(z);
        }
        for (size_t z : order) {
            text += model.zones[z].title + "\n";
            bool any = false;
            for (size_t k = 0; k < model.factors.size() && k < zones.size(); ++k) {
                if (zones[k] != static_cast<int>(z)) continue;
                text += "  - " + model.factors[k].label + "\n";
                any = true;
            }
            if (!any) text += "  (none)\n";
            text += "\n";
        }
        return text;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateImportancePerformanceChartExamples() {
        auto page = DemoKit::CreatePage("ipcMainContainer", "Importance-Performance Chart",
                "Slack's matrix: each factor of a service, by how much customers care about it and "
                "how it compares with competitors - the zone it lands in names the action");

        // The chart beside its controls; a narrow window puts the controls under it.
        auto row = DemoKit::AddRow(page, "ipcContent");

        // ===== TABS (the two forms of Slack's zones) =====
        auto chartCard = DemoKit::AddCard(row, "ipcChartCard");
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("ipcTabs", 0, 0, 860, 670);
        tabbedContainer->SetTabPosition(TabPosition::Top);
        DemoKit::AddFixed(chartCard, tabbedContainer);

        const int CHART_W = 840, CHART_H = 610;
        std::vector<std::shared_ptr<UltraCanvasImportancePerformanceChart>> charts;
        for (bool smooth : {true, false}) {
            ImportancePerformanceModel model = ImportancePerformanceSamples::SlackZones(smooth);
            model.factors = ImportancePerformanceSamples::SampleFactors();
            model.title = smooth ? "Parcel service: importance and performance"
                                 : "Parcel service: the textbook staircase";
            if (smooth) {
                // The slide form carries an icon per zone (reference image of the proposal).
                for (ImportancePerformanceZone& zone : model.zones) {
                    if (zone.id == "appropriate") zone.iconPath = DemoIconPath("check.png");
                    else if (zone.id == "improve") zone.iconPath = DemoIconPath("arrow-up.svg");
                    else if (zone.id == "urgent") zone.iconPath = DemoIconPath("warning.png");
                    else if (zone.id == "excess") zone.iconPath = DemoIconPath("info.png");
                }
            }
            auto chart = CreateImportancePerformanceChart(smooth ? "ipcSmooth" : "ipcStepped",
                                                          10, 10, CHART_W, CHART_H);
            chart->SetModel(model);

            auto tabContent = std::make_shared<UltraCanvasContainer>(
                    chart->GetIdentifier() + "Tab", 0, 0, 860, 632);
            tabContent->SetBackgroundColor(Color(255, 255, 255, 255));
            tabContent->AddChild(chart);
            tabbedContainer->AddTab(smooth ? "Slack zones (smooth)" : "Slack zones (stepped)", tabContent);
            charts.push_back(chart);
        }

        // ===== SIDEBAR: controls and the reading =====
        auto sidebar = DemoKit::AddCard(row, "ipcSidebar", "", "", 240);

        auto addCaption = [&](const std::string& id, const std::string& text) {
            DemoKit::AddCaption(sidebar, id, text)->SetFontWeight(FontWeight::Bold);
        };

        addCaption("ipcLabelsCaption", "Factor names");
        auto labelsSelector = std::make_shared<UltraCanvasDropdown>("ipcLabelsSelector", 0, 0, 190, 28);
        labelsSelector->AddItem("Beside the marks", "outside");
        labelsSelector->AddItem("Hidden (hover for them)", "hidden");
        labelsSelector->SetSelectedIndex(0);
        DemoKit::AddStretched(sidebar, labelsSelector);

        addCaption("ipcAxesCaption", "Axis ticks");
        auto axesSelector = std::make_shared<UltraCanvasDropdown>("ipcAxesSelector", 0, 0, 190, 28);
        axesSelector->AddItem("Named level ranges", "levels");
        axesSelector->AddItem("Numbers (1 = best)", "numbers");
        axesSelector->SetSelectedIndex(0);
        DemoKit::AddStretched(sidebar, axesSelector);

        addCaption("ipcGridCaption", "Grid");
        auto gridSelector = std::make_shared<UltraCanvasDropdown>("ipcGridSelector", 0, 0, 190, 28);
        gridSelector->AddItem("Off", "off");
        gridSelector->AddItem("On", "on");
        gridSelector->SetSelectedIndex(0);
        DemoKit::AddStretched(sidebar, gridSelector);

        addCaption("ipcListCaption", "FACTORS BY ZONE");
        // As tall as the zones and factors it lists: the card grows with it.
        auto listing = DemoKit::AddCaption(sidebar, "ipcListing", ZoneListing(*charts.front()));
        listing->SetTextColor(DemoKit::BodyText());
        listing->SetBackgroundColor(Color(248, 249, 250, 255));
        listing->SetPadding(10);

        // ===== CALLBACKS (raw pointers: no cycle through the elements) =====
        std::vector<UltraCanvasImportancePerformanceChart*> chartPtrs;
        for (auto& chart : charts) chartPtrs.push_back(chart.get());
        UltraCanvasLabel* listingPtr = listing.get();

        tabbedContainer->onTabChange = [chartPtrs, listingPtr](int, int newIndex) {
            if (newIndex >= 0 && newIndex < static_cast<int>(chartPtrs.size())) {
                listingPtr->SetText(ZoneListing(*chartPtrs[static_cast<size_t>(newIndex)]));
            }
        };
        labelsSelector->onSelectionChanged = [chartPtrs](int, const DropdownItem& item) {
            const EngineScatterLabelMode mode = item.value == "hidden" ? EngineScatterLabelMode::Hidden
                                                                       : EngineScatterLabelMode::Outside;
            for (auto* chart : chartPtrs) chart->SetPointLabelMode(mode);
        };
        axesSelector->onSelectionChanged = [chartPtrs](int, const DropdownItem& item) {
            for (auto* chart : chartPtrs) chart->SetShowLevelRanges(item.value == "levels");
        };
        gridSelector->onSelectionChanged = [chartPtrs](int, const DropdownItem& item) {
            for (auto* chart : chartPtrs) chart->SetShowGrid(item.value == "on");
        };

        return page;
    }

} // namespace UltraCanvas

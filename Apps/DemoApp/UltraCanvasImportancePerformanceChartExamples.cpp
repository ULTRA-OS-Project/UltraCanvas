// Apps/DemoApp/UltraCanvasImportancePerformanceChartExamples.cpp
// Importance-performance chart examples: Slack's zones, smooth and stepped,
// over the service factors of an invented parcel carrier
// Version: 1.0.0
// Last Modified: 2026-10-24
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
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
        const int PAD = 15;

        auto mainContainer = std::make_shared<UltraCanvasContainer>("ipcMainContainer", 0, 0, 1140, 800);
        mainContainer->SetBackgroundColor(Color(248, 249, 250, 255));

        auto headerContainer = std::make_shared<UltraCanvasContainer>("ipcHeader", 0, 0, 1140, 90);
        headerContainer->SetBackgroundColor(Color(255, 255, 255, 255));
        mainContainer->AddChild(headerContainer);

        auto mainTitle = std::make_shared<UltraCanvasLabel>("ipcMainTitle", 30, 15, 1080, 35);
        mainTitle->SetText("Importance-Performance Chart");
        mainTitle->SetFontSize(22);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainTitle->SetAlignment(TextAlignment::Left);
        mainTitle->SetTextColor(Color(33, 37, 41, 255));
        headerContainer->AddChild(mainTitle);

        auto subtitle = std::make_shared<UltraCanvasLabel>("ipcSubtitle", 30, 52, 1080, 30);
        subtitle->SetText("Slack's matrix: each factor of a service, by how much customers care about it and "
                          "how it compares with competitors - the zone it lands in names the action");
        subtitle->SetFontSize(13);
        subtitle->SetAlignment(TextAlignment::Left);
        subtitle->SetTextColor(Color(108, 117, 125, 255));
        headerContainer->AddChild(subtitle);

        auto contentContainer = std::make_shared<UltraCanvasContainer>("ipcContent", 0, 90, 1140, 710);
        mainContainer->AddChild(contentContainer);

        // ===== TABS (the two forms of Slack's zones) =====
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("ipcTabs", 20, 20, 860, 670);
        tabbedContainer->SetTabPosition(TabPosition::Top);
        contentContainer->AddChild(tabbedContainer);

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

        // ===== RIGHT SIDEBAR: controls and the reading =====
        auto sidebar = std::make_shared<UltraCanvasContainer>("ipcSidebar", 900, 20, 220, 670);
        sidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        contentContainer->AddChild(sidebar);
        const int SIDEBAR_W = 190;

        auto addCaption = [&](const std::string& id, const std::string& text, int y) {
            auto caption = std::make_shared<UltraCanvasLabel>(id, PAD, y, SIDEBAR_W, 20);
            caption->SetText(text);
            caption->SetFontSize(10);
            caption->SetFontWeight(FontWeight::Bold);
            caption->SetTextColor(Color(73, 80, 87, 255));
            caption->SetAlignment(TextAlignment::Left);
            sidebar->AddChild(caption);
        };

        addCaption("ipcLabelsCaption", "Factor names", PAD);
        auto labelsSelector = std::make_shared<UltraCanvasDropdown>("ipcLabelsSelector", PAD, PAD + 23, SIDEBAR_W, 28);
        labelsSelector->AddItem("Beside the marks", "outside");
        labelsSelector->AddItem("Hidden (hover for them)", "hidden");
        labelsSelector->SetSelectedIndex(0);
        sidebar->AddChild(labelsSelector);

        addCaption("ipcAxesCaption", "Axis ticks", PAD + 63);
        auto axesSelector = std::make_shared<UltraCanvasDropdown>("ipcAxesSelector", PAD, PAD + 86, SIDEBAR_W, 28);
        axesSelector->AddItem("Named level ranges", "levels");
        axesSelector->AddItem("Numbers (1 = best)", "numbers");
        axesSelector->SetSelectedIndex(0);
        sidebar->AddChild(axesSelector);

        addCaption("ipcGridCaption", "Grid", PAD + 126);
        auto gridSelector = std::make_shared<UltraCanvasDropdown>("ipcGridSelector", PAD, PAD + 149, SIDEBAR_W, 28);
        gridSelector->AddItem("Off", "off");
        gridSelector->AddItem("On", "on");
        gridSelector->SetSelectedIndex(0);
        sidebar->AddChild(gridSelector);

        addCaption("ipcListCaption", "FACTORS BY ZONE", PAD + 196);
        auto listing = std::make_shared<UltraCanvasLabel>("ipcListing", PAD, PAD + 220, SIDEBAR_W, 420);
        listing->SetFontSize(10);
        listing->SetAlignment(TextAlignment::Left);
        listing->SetBackgroundColor(Color(248, 249, 250, 255));
        listing->SetPadding(10);
        listing->SetText(ZoneListing(*charts.front()));
        sidebar->AddChild(listing);

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

        return mainContainer;
    }

} // namespace UltraCanvas

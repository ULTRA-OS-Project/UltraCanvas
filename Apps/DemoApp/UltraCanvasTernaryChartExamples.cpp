// Apps/DemoApp/UltraCanvasTernaryChartExamples.cpp
// Ternary chart examples: three-party vote shares by region (read
// anticlockwise) and soil samples on the USDA layout (read clockwise)
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasTernaryChart.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTernaryChartExamples() {
        auto page = DemoKit::CreatePage("ternaryMainContainer", "Ternary Chart",
                "Things made of three parts - votes, soil, alloys - as points in a triangle: each "
                "corner is all of one part, and the three shares of every point add up to 100%");

        // The chart beside its controls; a narrow window puts the controls under it.
        auto row = DemoKit::AddRow(page, "ternaryContent");

        // ===== TABS =====
        auto chartCard = DemoKit::AddCard(row, "ternaryChartCard");
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("ternaryTabs", 0, 0, 760, 640);
        tabbedContainer->SetTabPosition(TabPosition::Top);
        DemoKit::AddFixed(chartCard, tabbedContainer);

        struct Example {
            std::string tab;
            TernaryModel model;
        };
        const std::vector<Example> examples = {
            {"Votes (anticlockwise)", TernarySamples::ThreePartyVotes()},
            {"Soil samples (clockwise)", TernarySamples::SoilSamples()},
        };
        std::vector<std::shared_ptr<UltraCanvasTernaryChart>> charts;
        for (size_t i = 0; i < examples.size(); ++i) {
            auto chart = CreateTernaryChart("ternaryChart" + std::to_string(i), 10, 10, 740, 580);
            chart->SetModel(examples[i].model);
            auto tabContent = std::make_shared<UltraCanvasContainer>("ternaryTab" + std::to_string(i), 0, 0, 760, 602);
            tabContent->SetBackgroundColor(Color(255, 255, 255, 255));
            tabContent->AddChild(chart);
            tabbedContainer->AddTab(examples[i].tab, tabContent);
            charts.push_back(chart);
        }

        // ===== SIDEBAR =====
        auto sidebar = DemoKit::AddCard(row, "ternarySidebar", "", "", 240);
        auto addCaption = [&](const std::string& id, const std::string& text) {
            DemoKit::AddCaption(sidebar, id, text)->SetFontWeight(FontWeight::Bold);
        };

        addCaption("ternaryReadingCaption", "Reading direction");
        auto readingSelector = std::make_shared<UltraCanvasDropdown>("ternaryReadingSelector", 0, 0, 190, 28);
        readingSelector->AddItem("As the sample reads", "sample");
        readingSelector->AddItem("Clockwise", "clockwise");
        readingSelector->AddItem("Anticlockwise", "anticlockwise");
        readingSelector->SetSelectedIndex(0);
        DemoKit::AddStretched(sidebar, readingSelector);

        addCaption("ternaryLabelsCaption", "Point names");
        auto labelsSelector = std::make_shared<UltraCanvasDropdown>("ternaryLabelsSelector", 0, 0, 190, 28);
        labelsSelector->AddItem("Hidden (hover for them)", "hidden");
        labelsSelector->AddItem("Beside the points", "shown");
        labelsSelector->SetSelectedIndex(0);
        DemoKit::AddStretched(sidebar, labelsSelector);

        addCaption("ternaryGridCaption", "Grid and titles");
        auto gridSelector = std::make_shared<UltraCanvasDropdown>("ternaryGridSelector", 0, 0, 190, 28);
        gridSelector->AddItem("Grid and edge titles", "both");
        gridSelector->AddItem("Grid only", "grid");
        gridSelector->AddItem("Edge titles only", "titles");
        gridSelector->SetSelectedIndex(0);
        DemoKit::AddStretched(sidebar, gridSelector);

        addCaption("ternarySelectionCaption", "SELECTED");
        auto selection = DemoKit::AddCaption(sidebar, "ternarySelection",
                "Click a point for its three shares. Each share is read along the grid line "
                "that runs parallel to the edge opposite its corner, to the edge that carries "
                "that part's numbers. Click a legend entry to hide a series.");
        selection->SetTextColor(DemoKit::BodyText());
        selection->SetBackgroundColor(Color(248, 249, 250, 255));
        selection->SetPadding(10);

        // ===== CALLBACKS (raw pointers: no cycle through the elements) =====
        std::vector<UltraCanvasTernaryChart*> chartPtrs;
        for (auto& chart : charts) chartPtrs.push_back(chart.get());
        UltraCanvasLabel* selectionPtr = selection.get();
        std::vector<TernaryReading> sampleReadings;
        for (const Example& example : examples) sampleReadings.push_back(example.model.reading);

        for (size_t i = 0; i < chartPtrs.size(); ++i) {
            charts[i]->onPointClick = [chartPtrs, i, selectionPtr](size_t series, size_t point) {
                selectionPtr->SetText(chartPtrs[i]->BuildPointTooltip(series, point));
            };
        }
        readingSelector->onSelectionChanged = [chartPtrs, sampleReadings](int, const DropdownItem& item) {
            for (size_t i = 0; i < chartPtrs.size(); ++i) {
                if (item.value == "clockwise") chartPtrs[i]->SetReading(TernaryReading::Clockwise);
                else if (item.value == "anticlockwise") chartPtrs[i]->SetReading(TernaryReading::Anticlockwise);
                else chartPtrs[i]->SetReading(sampleReadings[i]);
            }
        };
        labelsSelector->onSelectionChanged = [chartPtrs](int, const DropdownItem& item) {
            for (auto* chart : chartPtrs) chart->SetShowPointLabels(item.value == "shown");
        };
        gridSelector->onSelectionChanged = [chartPtrs](int, const DropdownItem& item) {
            for (auto* chart : chartPtrs) {
                chart->SetShowGrid(item.value != "titles");
                chart->SetShowEdgeTitles(item.value != "grid");
            }
        };

        return page;
    }

} // namespace UltraCanvas

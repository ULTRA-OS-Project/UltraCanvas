// Apps/DemoApp/UltraCanvasDivergingChartExamples.cpp
// Implementation of all component example creators
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the charts and
//                  their controls in one card, the feature list in a card beside it
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
//#include "UltraCanvasButton3Sections.h"
#include "Plugins/Charts/UltraCanvasDivergingBarChart.h"
#include "UltraCanvasSwitch.h"
#include <sstream>
#include <random>
#include <map>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDivergingChartExamples() {
        auto container = DemoKit::CreatePage("DivergingChartExamples", Tr("Diverging Bar Chart Examples"),
                Tr("Likert scale, population pyramid and tornado charts, with a grid switch and random data"));
        auto mainRow = DemoKit::AddRow(container, "DivergingChartRow");
        auto chartCard = DemoKit::AddCard(mainRow, "DivergingChartCard", Tr("Chart types"),
                Tr("One tab per chart style; the controls under the tabs act on all three"));

        // Create tabbed container for the three chart types
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("divergingChartTabs", 0, 0, 700, 500);
        tabbedContainer->SetTabHeight(35);
        tabbedContainer->SetTabStyle(TabStyle::Modern);

        // Helper function to generate sample data
        auto generateSampleData = []() -> std::vector<std::pair<std::string, std::map<std::string, float>>> {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<> dist(10.0, 50.0);

            std::vector<std::pair<std::string, std::map<std::string, float>>> data;
            std::vector<std::string> rowLabels = {Tr("Question 1"), Tr("Question 2"), Tr("Question 3"), Tr("Question 4"), Tr("Question 5")};

            for (const auto& label : rowLabels) {
                std::map<std::string, float> values;
                values[Tr("Strongly Disagree")] = dist(gen);
                values[Tr("Disagree")] = dist(gen);
                values[Tr("Neutral")] = dist(gen) * 0.5f;
                values[Tr("Agree")] = dist(gen);
                values[Tr("Strongly Agree")] = dist(gen);
                data.push_back({label, values});
            }
            return data;
        };

        // Set up Likert scale categories
        std::vector<DivergingCategory> categories;
        categories.emplace_back(Tr("Strongly Disagree"), Color(0xff, 0x50, 0x33, 255), false);
        categories.emplace_back(Tr("Disagree"), Color(244, 165, 130, 255), false);
        categories.emplace_back(Tr("Neutral"), Color(220, 220, 220, 255), false);
        categories.emplace_back(Tr("Agree"), Color(04, 0xd1, 0xd6, 255), true);
        categories.emplace_back(Tr("Strongly Agree"), Color(33, 102, 172, 255), true);

        // Create Likert Scale chart
        auto likertContainer = std::make_shared<UltraCanvasContainer>("likertContainer", 0, 0, 700, 450);
        auto likertChart = std::make_shared<UltraCanvasDivergingBarChart>("likertChart", 0, 0, 700, 450);
        likertChart->SetChartTitle(Tr("Likert Scale Survey Responses"));
        likertChart->SetChartStyle(DivergingChartStyle::LikertScale);
        likertChart->SetBarHeight(0.85f);
        likertChart->SetCenterGap(5.0f);
        likertChart->SetShowGrid(true);
        likertChart->SetShowCenterLine(true);
        likertChart->SetShowRowLabels(true);
        likertChart->SetCategories(categories);
        auto likertData = generateSampleData();
        for (const auto& [label, values] : likertData) {
            likertChart->AddDataRow(label, values);
        }
        likertContainer->AddChild(likertChart);

        // Create Population Pyramid chart
        auto pyramidContainer = std::make_shared<UltraCanvasContainer>("pyramidContainer", 0, 0, 700, 450);
        auto pyramidChart = std::make_shared<UltraCanvasDivergingBarChart>("pyramidChart", 0, 0, 700, 450);
        pyramidChart->SetChartTitle(Tr("Population Pyramid Distribution"));
        pyramidChart->SetChartStyle(DivergingChartStyle::PopulationPyramid);
        pyramidChart->SetBarHeight(0.85f);
        pyramidChart->SetCenterGap(5.0f);
        pyramidChart->SetShowGrid(true);
        pyramidChart->SetShowCenterLine(true);
        pyramidChart->SetShowRowLabels(true);
        pyramidChart->SetCategories(categories);
        auto pyramidData = generateSampleData();
        for (const auto& [label, values] : pyramidData) {
            pyramidChart->AddDataRow(label, values);
        }
        pyramidContainer->AddChild(pyramidChart);

        // Create Tornado chart
        auto tornadoContainer = std::make_shared<UltraCanvasContainer>("tornadoContainer", 0, 0, 700, 450);
        auto tornadoChart = std::make_shared<UltraCanvasDivergingBarChart>("tornadoChart", 0, 0, 700, 450);
        tornadoChart->SetChartTitle(Tr("Tornado Chart Analysis"));
        tornadoChart->SetChartStyle(DivergingChartStyle::TornadoChart);
        tornadoChart->SetBarHeight(0.85f);
        tornadoChart->SetCenterGap(5.0f);
        tornadoChart->SetShowGrid(true);
        tornadoChart->SetShowCenterLine(true);
        tornadoChart->SetShowRowLabels(true);
        tornadoChart->SetCategories(categories);
        auto tornadoData = generateSampleData();
        for (const auto& [label, values] : tornadoData) {
            tornadoChart->AddDataRow(label, values);
        }
        tornadoContainer->AddChild(tornadoChart);

        // Add tabs
        tabbedContainer->AddTab(Tr("Likert Scale"), likertContainer);
        tabbedContainer->AddTab(Tr("Population Pyramid"), pyramidContainer);
        tabbedContainer->AddTab(Tr("Tornado Chart"), tornadoContainer);
        tabbedContainer->SetActiveTab(0);

        DemoKit::AddFixed(chartCard, tabbedContainer);

        // Control buttons
        auto controls = DemoKit::AddRow(chartCard, "DivergingChartControls");
        controls->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);

        // Grid switch
        auto swGrid = UltraCanvasSwitch::Create("swGrid", 0, 0, Tr("Grid"), true);
        swGrid->onStateChanged = [likertChart, pyramidChart, tornadoChart](CheckedState, CheckedState newState) {
            bool showGrid = (newState == CheckedState::Checked);
            likertChart->SetShowGrid(showGrid);
            pyramidChart->SetShowGrid(showGrid);
            tornadoChart->SetShowGrid(showGrid);
        };
        DemoKit::Add(controls, swGrid);

        // Width 0: as wide as its text.
        auto btnGenerateData = std::make_shared<UltraCanvasButton>("btnGenerate", 0, 0, 0, 30);
        btnGenerateData->SetText(Tr("Random Data"));
        btnGenerateData->SetColors(Color(70, 130, 180, 255), Color(90, 150, 200, 255), Color(50, 100, 160, 255), Color(150, 200, 240, 255));
        btnGenerateData->SetTextColors(Colors::White, Colors::White, Colors::White, Colors::White);
        btnGenerateData->onClick = [likertChart, pyramidChart, tornadoChart, categories]() {
            // Generate new random data for all three charts
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<> dist(5.0, 45.0);
            std::vector<std::string> newLabels = {Tr("Item A"), Tr("Item B"), Tr("Item C"), Tr("Item D"), Tr("Item E"), Tr("Item F")};

            // Update Likert chart
            likertChart->ClearData();
            for (const auto& label : newLabels) {
                std::map<std::string, float> values;
                values[Tr("Strongly Disagree")] = static_cast<float>(dist(gen));
                values[Tr("Disagree")] = static_cast<float>(dist(gen));
                values[Tr("Neutral")] = static_cast<float>(dist(gen) * 0.4);
                values[Tr("Agree")] = static_cast<float>(dist(gen));
                values[Tr("Strongly Agree")] = static_cast<float>(dist(gen));
                likertChart->AddDataRow(label, values);
            }

            // Update Pyramid chart
            pyramidChart->ClearData();
            for (const auto& label : newLabels) {
                std::map<std::string, float> values;
                values[Tr("Strongly Disagree")] = static_cast<float>(dist(gen));
                values[Tr("Disagree")] = static_cast<float>(dist(gen));
                values[Tr("Neutral")] = static_cast<float>(dist(gen) * 0.4);
                values[Tr("Agree")] = static_cast<float>(dist(gen));
                values[Tr("Strongly Agree")] = static_cast<float>(dist(gen));
                pyramidChart->AddDataRow(label, values);
            }

            // Update Tornado chart
            tornadoChart->ClearData();
            for (const auto& label : newLabels) {
                std::map<std::string, float> values;
                values[Tr("Strongly Disagree")] = static_cast<float>(dist(gen));
                values[Tr("Disagree")] = static_cast<float>(dist(gen));
                values[Tr("Neutral")] = static_cast<float>(dist(gen) * 0.4);
                values[Tr("Agree")] = static_cast<float>(dist(gen));
                values[Tr("Strongly Agree")] = static_cast<float>(dist(gen));
                tornadoChart->AddDataRow(label, values);
            }
        };
        DemoKit::Add(controls, btnGenerateData);

        // Info
        auto infoCard = DemoKit::AddNotes(mainRow, "DivergingChartInfo", Tr("Diverging Bar Chart Features"), {
                Tr("Likert scale visualization"),
                Tr("Population pyramid style"),
                Tr("Tornado chart format"),
                Tr("Interactive controls"),
                Tr("Multiple data categories"),
                Tr("Customizable colors"),
                Tr("Grid and center line options"),
                Tr("Dynamic data updates")});
        DemoKit::AddHeading(infoCard, "DivergingChartUseTitle", Tr("Use the controls below the chart to:"));
        DemoKit::Add(infoCard, DemoKit::CreateBullets("DivergingChartUse", {
                Tr("Switch the grid on or off"),
                Tr("Generate random data")}));

        return container;
    }
} // namespace UltraCanvas
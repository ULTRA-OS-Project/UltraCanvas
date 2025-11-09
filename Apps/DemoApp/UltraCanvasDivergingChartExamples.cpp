// Apps/DemoApp/UltraCanvasDivergingChartExamples.cpp
// Implementation of all component example creators
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
//#include "UltraCanvasButton3Sections.h"
#include "UltraCanvasFormulaEditor.h"
#include "Plugins/Charts/UltraCanvasDivergingBarChart.h"
#include <sstream>
#include <random>
#include <map>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDivergingChartExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("DivergingChartExamples", 2500, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("DivergingChartTitle", 2501, 10, 10, 350, 30);
        title->SetText("Diverging Bar Chart Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Create tabbed container for the three chart types
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("divergingChartTabs", 2502, 10, 50, 700, 500);
        tabbedContainer->SetTabHeight(35);
        tabbedContainer->SetTabStyle(TabStyle::Modern);

        // Helper function to generate sample data
        auto generateSampleData = []() -> std::vector<std::pair<std::string, std::map<std::string, float>>> {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<> dist(10.0, 50.0);

            std::vector<std::pair<std::string, std::map<std::string, float>>> data;
            std::vector<std::string> rowLabels = {"Question 1", "Question 2", "Question 3", "Question 4", "Question 5"};

            for (const auto& label : rowLabels) {
                std::map<std::string, float> values;
                values["Strongly Disagree"] = dist(gen);
                values["Disagree"] = dist(gen);
                values["Neutral"] = dist(gen) * 0.5f;
                values["Agree"] = dist(gen);
                values["Strongly Agree"] = dist(gen);
                data.push_back({label, values});
            }
            return data;
        };

        // Set up Likert scale categories
        std::vector<DivergingCategory> categories;
        categories.emplace_back("Strongly Disagree", Color(0xff, 0x50, 0x33, 255), false);
        categories.emplace_back("Disagree", Color(244, 165, 130, 255), false);
        categories.emplace_back("Neutral", Color(220, 220, 220, 255), false);
        categories.emplace_back("Agree", Color(04, 0xd1, 0xd6, 255), true);
        categories.emplace_back("Strongly Agree", Color(33, 102, 172, 255), true);

        // Create Likert Scale chart
        auto likertContainer = std::make_shared<UltraCanvasContainer>("likertContainer", 2520, 0, 0, 700, 450);
        auto likertChart = std::make_shared<UltraCanvasDivergingBarChart>("likertChart", 2521, 0, 0, 700, 450);
        likertChart->SetChartTitle("Likert Scale Survey Responses");
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
        auto pyramidContainer = std::make_shared<UltraCanvasContainer>("pyramidContainer", 2530, 0, 0, 700, 450);
        auto pyramidChart = std::make_shared<UltraCanvasDivergingBarChart>("pyramidChart", 2531, 0, 0, 700, 450);
        pyramidChart->SetChartTitle("Population Pyramid Distribution");
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
        auto tornadoContainer = std::make_shared<UltraCanvasContainer>("tornadoContainer", 2540, 0, 0, 700, 450);
        auto tornadoChart = std::make_shared<UltraCanvasDivergingBarChart>("tornadoChart", 2541, 0, 0, 700, 450);
        tornadoChart->SetChartTitle("Tornado Chart Analysis");
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
        tabbedContainer->AddTab("Likert Scale", likertContainer);
        tabbedContainer->AddTab("Population Pyramid", pyramidContainer);
        tabbedContainer->AddTab("Tornado Chart", tornadoContainer);
        tabbedContainer->SetActiveTab(0);

        container->AddChild(tabbedContainer);

        // Control buttons
        int buttonY = 560;
        int buttonWidth = 160;
        int buttonHeight = 30;
        int buttonSpacing = 10;
        int currentX = 10;

        // Toggle buttons
        auto btnToggleGrid = std::make_shared<UltraCanvasButton>("btnGrid", 2506, currentX, buttonY, buttonWidth, buttonHeight);
        btnToggleGrid->SetText("Toggle Grid");
        btnToggleGrid->SetColors(Color(70, 130, 180, 255), Color(90, 150, 200, 255), Color(50, 100, 160, 255), Color(150, 200, 240, 255));
        btnToggleGrid->SetTextColors(Colors::White, Colors::White, Colors::White, Colors::White);
        btnToggleGrid->onClick = [likertChart, pyramidChart, tornadoChart]() {
            static bool showGrid = true;
            showGrid = !showGrid;
            likertChart->SetShowGrid(showGrid);
            pyramidChart->SetShowGrid(showGrid);
            tornadoChart->SetShowGrid(showGrid);
        };
        container->AddChild(btnToggleGrid);
        currentX += buttonWidth + buttonSpacing;

        auto btnGenerateData = std::make_shared<UltraCanvasButton>("btnGenerate", 2507, currentX, buttonY, buttonWidth, buttonHeight);
        btnGenerateData->SetText("Random Data");
        btnGenerateData->SetColors(Color(70, 130, 180, 255), Color(90, 150, 200, 255), Color(50, 100, 160, 255), Color(150, 200, 240, 255));
        btnGenerateData->SetTextColors(Colors::White, Colors::White, Colors::White, Colors::White);
        btnGenerateData->onClick = [likertChart, pyramidChart, tornadoChart, categories]() {
            // Generate new random data for all three charts
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<> dist(5.0, 45.0);
            std::vector<std::string> newLabels = {"Item A", "Item B", "Item C", "Item D", "Item E", "Item F"};

            // Update Likert chart
            likertChart->ClearData();
            for (const auto& label : newLabels) {
                std::map<std::string, float> values;
                values["Strongly Disagree"] = static_cast<float>(dist(gen));
                values["Disagree"] = static_cast<float>(dist(gen));
                values["Neutral"] = static_cast<float>(dist(gen) * 0.4);
                values["Agree"] = static_cast<float>(dist(gen));
                values["Strongly Agree"] = static_cast<float>(dist(gen));
                likertChart->AddDataRow(label, values);
            }

            // Update Pyramid chart
            pyramidChart->ClearData();
            for (const auto& label : newLabels) {
                std::map<std::string, float> values;
                values["Strongly Disagree"] = static_cast<float>(dist(gen));
                values["Disagree"] = static_cast<float>(dist(gen));
                values["Neutral"] = static_cast<float>(dist(gen) * 0.4);
                values["Agree"] = static_cast<float>(dist(gen));
                values["Strongly Agree"] = static_cast<float>(dist(gen));
                pyramidChart->AddDataRow(label, values);
            }

            // Update Tornado chart
            tornadoChart->ClearData();
            for (const auto& label : newLabels) {
                std::map<std::string, float> values;
                values["Strongly Disagree"] = static_cast<float>(dist(gen));
                values["Disagree"] = static_cast<float>(dist(gen));
                values["Neutral"] = static_cast<float>(dist(gen) * 0.4);
                values["Agree"] = static_cast<float>(dist(gen));
                values["Strongly Agree"] = static_cast<float>(dist(gen));
                tornadoChart->AddDataRow(label, values);
            }
        };
        container->AddChild(btnGenerateData);

        // Info label
        auto infoLabel = std::make_shared<UltraCanvasLabel>("DivergingChartInfo", 2508, 740, 60, 250, 350);
        infoLabel->SetText("Diverging Bar Chart Features:\n\n• Likert scale visualization\n• Population pyramid style\n• Tornado chart format\n• Interactive controls\n• Multiple data categories\n• Customizable colors\n• Grid and center line options\n• Dynamic data updates\n\nClick the buttons below to:\nn• Toggle grid display\n• Generate random data");
        infoLabel->SetFontSize(11);
        infoLabel->SetTextColor(Color(80, 80, 80, 255));
        infoLabel->SetBackgroundColor(Color(250, 250, 250, 255));
        infoLabel->SetBorders(1.0f);
        infoLabel->SetPadding(10.0f);
        container->AddChild(infoLabel);

        return container;
    }
} // namespace UltraCanvas
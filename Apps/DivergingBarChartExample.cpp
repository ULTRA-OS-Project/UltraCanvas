// DivergingBarChartExample.cpp
// Example demonstrating diverging bar chart with Likert scale data (matching the provided image)
// Version: 1.0.0
// Last Modified: 2025-09-23
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasDivergingBarChart.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include <memory>
#include <map>
#include <vector>
#include <random>

using namespace UltraCanvas;

class DivergingChartDemo {
private:
    UltraCanvasApplication* app;
    std::shared_ptr<UltraCanvasWindow> window;
    std::shared_ptr<UltraCanvasDivergingBarChart> divergingChart;
    std::shared_ptr<UltraCanvasLabel> titleLabel;
    std::shared_ptr<UltraCanvasLabel> subtitleLabel;

    // Control buttons
    std::shared_ptr<UltraCanvasButton> btnPyramid;
    std::shared_ptr<UltraCanvasButton> btnLikert;
    std::shared_ptr<UltraCanvasButton> btnTornado;
    std::shared_ptr<UltraCanvasButton> btnToggleGrid;
    std::shared_ptr<UltraCanvasButton> btnToggleLabels;
    std::shared_ptr<UltraCanvasButton> btnGenerateData;

public:
    DivergingChartDemo() {
        InitializeApplication();
        CreateWindow();
        CreateChart();
        CreateControls();
        LoadLikertData();
        CreateLegend();
        SetupEventHandlers();
    }

    void InitializeApplication() {
        app = new UltraCanvasApplication();
        app->Initialize();
    }

    void CreateWindow() {
        WindowConfig config;
        config.title = "Diverging Bar Chart - Likert Scale Survey Results";
        config.width = 1200;
        config.height = 900;
        config.resizable = true;
        config.x = 100;
        config.y = 50;

        window = std::make_shared<UltraCanvasWindow>(config);
//        window->SetBackgroundColor(Color(250, 250, 250, 255));
    }

    void CreateChart() {
        // Create the diverging bar chart
        divergingChart = CreateDivergingBarChart("divergingChart", 1001, 100, 120, 800, 600);
        divergingChart->SetChartTitle("Survey Response Distribution");
        divergingChart->SetChartStyle(DivergingChartStyle::PopulationPyramid);
        divergingChart->SetBarHeight(0.85f);  // Thick bars like in the image
        divergingChart->SetCenterGap(5.0f);   // Small gap at center
        divergingChart->SetGridEnabled(true);
        divergingChart->SetShowCenterLine(true);
        divergingChart->SetShowRowLabels(true);

        // Set up Likert scale categories matching the image colors
        std::vector<DivergingCategory> categories;

        // Left side (negative responses)
        categories.emplace_back("Agree", Color(178, 24, 43, 255), false);           // Dark red
        categories.emplace_back("Disagree", Color(244, 165, 130, 255), false);      // Light orange/peach
        categories.emplace_back("Neutral", Color(220, 220, 220, 255), false);       // Light gray

        // Right side (positive responses)
        categories.emplace_back("Strongly Agree", Color(146, 197, 222, 255), true); // Light blue
        categories.emplace_back("Strongly disagree", Color(33, 102, 172, 255), true); // Dark blue

        divergingChart->SetCategories(categories);

        window->AddOrMoveChild(divergingChart);

        // Create title label
        titleLabel = std::make_shared<UltraCanvasLabel>("title", 2001, 100, 30, 800, 35);
        titleLabel->SetText("Likert Scale Survey Responses");
        titleLabel->SetFontSize(20);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetAlignment(TextAlignment::Center);
        titleLabel->SetTextColor(Color(40, 40, 40, 255));

        window->AddOrMoveChild(titleLabel);

        // Create subtitle
        subtitleLabel = std::make_shared<UltraCanvasLabel>("subtitle", 2002, 100, 70, 800, 25);
        subtitleLabel->SetText("Distribution of responses across different survey questions");
        subtitleLabel->SetFontSize(14);
        subtitleLabel->SetAlignment(TextAlignment::Center);
        subtitleLabel->SetTextColor(Color(100, 100, 100, 255));

        window->AddOrMoveChild(subtitleLabel);
    }

    void CreateControls() {
        int buttonY = 750;
        int buttonWidth = 130;
        int buttonHeight = 35;
        int buttonSpacing = 10;
        int currentX = 100;

        // Style selection buttons
        btnPyramid = CreateButton("btnPyramid", 3001, currentX, buttonY, buttonWidth, buttonHeight, "Pyramid");
//        btnPyramid->SetHighlighted(true); // Default style
        currentX += buttonWidth + buttonSpacing;

        btnLikert = CreateButton("btnLikert", 3002, currentX, buttonY, buttonWidth, buttonHeight, "Likert");
        currentX += buttonWidth + buttonSpacing;

        btnTornado = CreateButton("btnTornado", 3003, currentX, buttonY, buttonWidth, buttonHeight, "Tornado");
        currentX += buttonWidth + buttonSpacing;

        // Toggle buttons
        btnToggleGrid = CreateButton("btnGrid", 3004, currentX, buttonY, buttonWidth, buttonHeight, "Toggle Grid");
        currentX += buttonWidth + buttonSpacing;

        btnToggleLabels = CreateButton("btnLabels", 3005, currentX, buttonY, buttonWidth, buttonHeight, "Toggle Labels");
        currentX += buttonWidth + buttonSpacing;

        btnGenerateData = CreateButton("btnGenerate", 3006, currentX, buttonY, buttonWidth, buttonHeight, "Random Data");
    }

    std::shared_ptr<UltraCanvasButton> CreateButton(const std::string& id, long uid,
                                                    int x, int y, int width, int height,
                                                    const std::string& text) {
        auto button = std::make_shared<UltraCanvasButton>(id, uid, x, y, width, height);
        button->SetText(text);
        button->SetColors(Color(70, 130, 180, 255), Color(90, 150, 200, 255), Color(50, 100, 160, 255), Color(150, 200, 240, 255));
        button->SetTextColors(Color(255, 255, 255, 255), Color(255, 255, 255, 255), Color(255, 255, 255, 255), Color(255, 255, 255, 255));
        button->SetFont("Sans", 11);
        button->SetCornerRadius(5);

        window->AddOrMoveChild(button);
        return button;
    }

    void CreateLegend() {
        int legendX = 950;
        int legendY = 200;
        int legendItemHeight = 35;

        auto legendTitle = std::make_shared<UltraCanvasLabel>("legendTitle", 4000, legendX, legendY - 35, 180, 25);
        legendTitle->SetText("Response Categories");
        legendTitle->SetFontWeight(FontWeight::Bold);
        legendTitle->SetFontSize(13);
        window->AddOrMoveChild(legendTitle);

        std::vector<std::pair<std::string, Color>> legendItems = {
                {"Agree", Color(178, 24, 43, 255)},
                {"Disagree", Color(244, 165, 130, 255)},
                {"Neutral", Color(220, 220, 220, 255)},
                {"Strongly Agree", Color(146, 197, 222, 255)},
                {"Strongly disagree", Color(33, 102, 172, 255)}
        };

        for (size_t i = 0; i < legendItems.size(); ++i) {
            int itemY = legendY + (i * legendItemHeight);

            // Color box
            auto colorBox = std::make_shared<UltraCanvasLabel>(
                    "legendBox_" + std::to_string(i), 4100 + i,
                    legendX, itemY, 25, 25
            );
            colorBox->SetBackgroundColor(legendItems[i].second);
            window->AddOrMoveChild(colorBox);

            // Label
            auto label = std::make_shared<UltraCanvasLabel>(
                    "legendText_" + std::to_string(i), 4200 + i,
                    legendX + 35, itemY + 2, 145, 20
            );
            label->SetText(legendItems[i].first);
            label->SetFontSize(11);
            label->SetAlignment(TextAlignment::Left);
            window->AddOrMoveChild(label);
        }
    }

    void LoadLikertData() {
        // Clear existing data
        divergingChart->ClearData();

        // Generate row labels matching the image (X16 to X98)
        std::vector<std::string> rowLabels;
        for (int i = 98; i >= 16; i -= 2) {
            rowLabels.push_back("X" + std::to_string(i));
        }

        // Generate realistic Likert scale data with some pattern
        std::random_device rd;
        std::mt19937 gen(rd());
        std::normal_distribution<> dist(0.0, 1.0);

        int rowCount = 0;
        for (const auto& label : rowLabels) {
            std::map<std::string, float> values;

            // Generate values that create a pattern similar to the image
            // Base value affects overall bar length
            float baseValue = 20.0f + std::abs(dist(gen)) * 10.0f;

            // Add some variation based on row position for visual interest
            float positionFactor = 1.0f + (rowCount * 0.02f);

            // Create distribution with controlled variance
            values["Agree"] = (baseValue + std::abs(dist(gen)) * 15.0f) * positionFactor;
            values["Disagree"] = (baseValue * 0.8f + std::abs(dist(gen)) * 10.0f) * positionFactor;
            values["Neutral"] = (baseValue * 0.3f + std::abs(dist(gen)) * 5.0f);  // Neutral is usually smaller
            values["Strongly Agree"] = (baseValue * 0.9f + std::abs(dist(gen)) * 12.0f) * positionFactor;
            values["Strongly disagree"] = (baseValue * 0.7f + std::abs(dist(gen)) * 8.0f) * positionFactor;

            divergingChart->AddDataRow(label, values);
            rowCount++;
        }

//        statusLabel->SetText("Loaded " + std::to_string(rowCount) + " rows of survey data");
//        statusLabel->SetTextColor(Color(60, 120, 60, 255));
    }

    void GenerateRandomData() {
        divergingChart->ClearData();

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dist(5.0, 50.0);

        // Generate row labels
        std::vector<std::string> rowLabels;
        for (int i = 98; i >= 16; i -= 2) {
            rowLabels.push_back("X" + std::to_string(i));
        }

        // Generate random values
        for (const auto& label : rowLabels) {
            std::map<std::string, float> values;

            values["Agree"] = dist(gen);
            values["Disagree"] = dist(gen);
            values["Neutral"] = dist(gen) * 0.3f;  // Neutral typically smaller
            values["Strongly Agree"] = dist(gen);
            values["Strongly disagree"] = dist(gen);

            divergingChart->AddDataRow(label, values);
        }
    }

    void SetupEventHandlers() {
        // Style change handlers
        btnPyramid->onClick = [this]() {
            divergingChart->SetChartStyle(DivergingChartStyle::PopulationPyramid);
            UpdateButtonHighlights(btnPyramid);
        };

        btnLikert->onClick = [this]() {
            divergingChart->SetChartStyle(DivergingChartStyle::LikertScale);
            UpdateButtonHighlights(btnLikert);
        };

        btnTornado->onClick = [this]() {
            divergingChart->SetChartStyle(DivergingChartStyle::TornadoChart);
            UpdateButtonHighlights(btnTornado);
        };

        // Toggle controls
        static bool showGrid = true;
        btnToggleGrid->onClick = [this]() {
            showGrid = !showGrid;
            divergingChart->SetGridEnabled(showGrid);
        };

        static bool showLabels = true;
        btnToggleLabels->onClick = [this]() {
            showLabels = !showLabels;
            divergingChart->SetShowRowLabels(showLabels);
        };

        // Generate new data
        btnGenerateData->onClick = [this]() {
            GenerateRandomData();
        };

        // Chart interaction callbacks
        divergingChart->onBarClick = [](int rowIndex, const std::string& category) {
            printf("Clicked: Row %d, Category: %s\n", rowIndex, category.c_str());
        };

        divergingChart->onBarHover = [](int rowIndex, const std::string& category) {
            // Handle hover if needed
        };
    }

    void UpdateButtonHighlights(std::shared_ptr<UltraCanvasButton> activeButton) {
//        btnPyramid->SetHighlighted(false);
//        btnLikert->SetHighlighted(false);
//        btnTornado->SetHighlighted(false);

//        if (activeButton) {
//            activeButton->SetHighlighted(true);
//        }
    }

    void Run() {
        window->Show();
        app->Run();
    }

    ~DivergingChartDemo() {
        delete app;
    }
};

// Main function
int main(int argc, char* argv[]) {
    DivergingChartDemo demo;
    demo.Run();
    return 0;
}
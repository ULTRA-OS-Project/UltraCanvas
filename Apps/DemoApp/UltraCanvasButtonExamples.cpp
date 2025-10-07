// Apps/DemoApp/UltraCanvasButtonDemo.cpp
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

// ===== BASIC UI ELEMENTS =====


    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateButtonExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("ButtonExamples", 100, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("ButtonTitle", 101, 10, 10, 300, 30);
        title->SetText("Button Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Standard Button
        auto standardBtn = std::make_shared<UltraCanvasButton>("StandardButton", 102, 20, 50, 120, 35);
        standardBtn->SetText("Standard Button");
        standardBtn->onClick = []() { std::cout << "Standard button clicked!" << std::endl; };

        // Icon Button
//        auto iconBtn = std::make_shared<UltraCanvasButton>("IconButton", 103, 160, 50, 40, 35);
//        iconBtn->SetIcon("assets/icons/save.png");
//        iconBtn->SetTooltip("Save File");
//        iconBtn->onClick = []() { std::cout << "Save button clicked!" << std::endl; };
//        container->AddChild(iconBtn);

        // Toggle Button
//        auto toggleBtn = std::make_shared<UltraCanvasButton>("ToggleButton", 104, 220, 50, 100, 35);
//        toggleBtn->SetText("Toggle");
//        toggleBtn->SetToggleable(true);
//        toggleBtn->onClick = [toggleBtn]() {
//            std::cout << "Toggle state: " << (toggleBtn->IsToggled() ? "ON" : "OFF") << std::endl;
//        };
//        container->AddChild(toggleBtn);

        // Three-Section Button
//        auto threeSectionBtn = std::make_shared<UltraCanvasButton3Sections>("ThreeSectionButton", 105, 340, 50, 180, 35);
//        threeSectionBtn->SetLeftText("File");
//        threeSectionBtn->SetCenterText("Edit");
//        threeSectionBtn->SetRightText("View");
//        threeSectionBtn->onLeftClicked = []() { std::cout << "File section clicked!" << std::endl; };
//        threeSectionBtn->onCenterClicked = []() { std::cout << "Edit section clicked!" << std::endl; };
//        threeSectionBtn->onRightClicked = []() { std::cout << "View section clicked!" << std::endl; };
//        container->AddChild(threeSectionBtn);

        // Disabled Button
        auto disabledBtn = std::make_shared<UltraCanvasButton>("DisabledButton", 106, 20, 100, 120, 35);
        disabledBtn->SetText("Disabled");
        disabledBtn->SetEnabled(false);

//        // Colored Buttons
        auto primaryBtn = std::make_shared<UltraCanvasButton>("PrimaryButton", 107, 160, 100, 100, 35);
        primaryBtn->SetText("Primary");
        primaryBtn->SetColors(Color(0, 123, 255, 255), Color(0, 100, 225, 255), Color(0, 90, 215, 255), Color(100, 133, 255, 255));
        primaryBtn->SetTextColors(Colors::White, Colors::White, Colors::White, Colors::LightGray);

        auto dangerBtn = std::make_shared<UltraCanvasButton>("DangerButton", 108, 280, 100, 100, 35);
        dangerBtn->SetText("Danger");
//        dangerBtn->SetColors(Color(0, 123, 255, 255), Color(0, 100, 225, 255), Color(0, 90, 215, 255), Color(100, 133, 255, 255));
        dangerBtn->SetColors(Color(220, 53, 69, 255), Color(210, 43, 59, 255), Color(200, 33, 49, 255), Color(255, 153, 169, 255));
        dangerBtn->SetTextColors(Colors::White, Colors::White, Colors::White, Colors::LightGray);

        container->AddChild(disabledBtn);
        container->AddChild(dangerBtn);
        container->AddChild(primaryBtn);
        container->AddChild(standardBtn);

        return container;
    }
} // namespace UltraCanvas
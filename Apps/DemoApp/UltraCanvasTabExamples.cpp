// Apps/DemoApp/UltraCanvasDemoExamples.cpp
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
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTabExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TabExamples", 700, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("TabTitle", 701, 10, 10, 300, 30);
        title->SetText("Tabbed Container Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Top Tabs
        auto topTabs = std::make_shared<UltraCanvasTabbedContainer>("TopTabs", 702, 20, 50, 600, 250);
        topTabs->SetTabPosition(TabPosition::Top);

        // Create tab content
        auto tab1Content = std::make_shared<UltraCanvasLabel>("Tab1Content", 703, 10, 10, 580, 200);
        tab1Content->SetText("This is the content of Tab 1.\nYou can put any UltraCanvas elements here.");
        tab1Content->SetAlignment(TextAlignment::Center);
        tab1Content->SetBackgroundColor(Color(250, 250, 250, 255));
        tab1Content->SetPadding(10);

        auto tab2Content = std::make_shared<UltraCanvasLabel>("Tab2Content", 704, 10, 10, 580, 200);
        tab2Content->SetText("Tab 2 contains different content.\nTabs can hold complex layouts and controls.");
        tab2Content->SetAlignment(TextAlignment::Center);
        tab2Content->SetBackgroundColor(Color(240, 255, 240, 255));
        tab2Content->SetPadding(10);

        auto tab3Content = std::make_shared<UltraCanvasLabel>("Tab3Content", 705, 10, 10, 580, 200);
        tab3Content->SetText("Third tab with more information.\nEach tab maintains its own state.");
        tab3Content->SetAlignment(TextAlignment::Center);
        tab3Content->SetBackgroundColor(Color(240, 240, 255, 255));
        tab3Content->SetPadding(10);

        auto tab4Content = std::make_shared<UltraCanvasLabel>("Tab4Content", 580, 200);
        tab4Content->SetText("Fourth tab with more information.\nEach tab maintains its own state.");
        tab4Content->SetAlignment(TextAlignment::Center);
        tab4Content->SetBackgroundColor(Color(240, 255, 255, 255));
        tab4Content->SetPadding(10);

        topTabs->AddTab("General", tab1Content);
        topTabs->AddTab("Settings", tab2Content);
        topTabs->AddTab("Advanced", tab3Content);
        topTabs->AddTab("Panel", tab4Content);
        topTabs->SetActiveTab(0);

        container->AddChild(topTabs);

        return container;
    }
} // namespace UltraCanvas
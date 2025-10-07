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

        auto tab2Content = std::make_shared<UltraCanvasLabel>("Tab2Content", 704, 10, 10, 580, 200);
        tab2Content->SetText("Tab 2 contains different content.\nTabs can hold complex layouts and controls.");
        tab2Content->SetAlignment(TextAlignment::Center);
        tab2Content->SetBackgroundColor(Color(240, 255, 240, 255));

        auto tab3Content = std::make_shared<UltraCanvasLabel>("Tab3Content", 705, 10, 10, 580, 200);
        tab3Content->SetText("Third tab with more information.\nEach tab maintains its own state.");
        tab3Content->SetAlignment(TextAlignment::Center);
        tab3Content->SetBackgroundColor(Color(240, 240, 255, 255));

        topTabs->AddTab("General", tab1Content);
        topTabs->AddTab("Settings", tab2Content);
        topTabs->AddTab("Advanced", tab3Content);
        topTabs->SetActiveTab(0);

        container->AddChild(topTabs);

        // Side Tabs
        auto sideTabs = std::make_shared<UltraCanvasTabbedContainer>("SideTabs", 706, 20, 320, 400, 200);
        sideTabs->SetTabPosition(TabPosition::Left);
        sideTabs->SetTabHeight(25);
        sideTabs->SetTabMinWidth(80);

//        auto sideTab1 = std::make_shared<UltraCanvasLabel>("SideTab1", 707, 10, 10, 300, 170);
//        sideTab1->SetText("Left-side tabs are useful\nfor tool palettes and\nnavigation panels.");
//        sideTab1->SetBackgroundColor(Color(255, 250, 240, 255));
//
//        auto sideTab2 = std::make_shared<UltraCanvasLabel>("SideTab2", 708, 10, 10, 300, 170);
//        sideTab2->SetText("Second side tab content.\nVertical orientation.");
//        sideTab2->SetBackgroundColor(Color(240, 250, 255, 255));
//
//        sideTabs->AddTab("Tools", sideTab1);
//        sideTabs->AddTab("Props", sideTab2);
//        sideTabs->SetActiveTab(0);
//
//        container->AddChild(sideTabs);

        return container;
    }
} // namespace UltraCanvas
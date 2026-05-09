// Apps/DemoApp/UltraCanvasVennDiagramExamples.cpp
// Professional Venn Diagram examples with Sankey-quality UI
// Version: 3.0.2 (Simplified, working)
// Last Modified: 2025-04-02

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasVennDiagram.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include <vector>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateVennDiagramExamples() {
        auto mainContainer = std::make_shared<UltraCanvasContainer>("vennMainContainer", 1000, 0, 0, 1200, 800);
        mainContainer->SetBackgroundColor(Color(248, 249, 250, 255));
        
        auto headerContainer = std::make_shared<UltraCanvasContainer>("headerContainer", 1001, 0, 0, 1200, 90);
        headerContainer->SetBackgroundColor(Color(255, 255, 255, 255));
        mainContainer->AddChild(headerContainer);
        
        auto mainTitle = std::make_shared<UltraCanvasLabel>("mainTitle", 1002, 30, 15, 1140, 35);
        mainTitle->SetText("Venn Diagram Analysis Suite");
        mainTitle->SetFontSize(22);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainTitle->SetAlignment(TextAlignment::Left);
        mainTitle->SetTextColor(Color(33, 37, 41, 255));
        headerContainer->AddChild(mainTitle);
        
        auto subtitle = std::make_shared<UltraCanvasLabel>("subtitle", 1003, 30, 50, 800, 30);
        subtitle->SetText("Interactive visualization of set relationships");
        subtitle->SetFontSize(13);
        subtitle->SetAlignment(TextAlignment::Left);
        subtitle->SetTextColor(Color(108, 117, 125, 255));
        headerContainer->AddChild(subtitle);
        
        auto contentContainer = std::make_shared<UltraCanvasContainer>("contentContainer", 1004, 0, 90, 1200, 710);
        mainContainer->AddChild(contentContainer);
        
        auto leftSidebar = std::make_shared<UltraCanvasContainer>("leftSidebar", 1005, 20, 20, 220, 670);
        leftSidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        leftSidebar->SetPadding(15);
        contentContainer->AddChild(leftSidebar);
        
        auto controlsTitle = std::make_shared<UltraCanvasLabel>("controlsTitle", 1006, 0, 0, 190, 30);
        controlsTitle->SetText("CONTROLS");
        controlsTitle->SetFontSize(11);
        controlsTitle->SetFontWeight(FontWeight::Bold);
        controlsTitle->SetTextColor(Color(73, 80, 87, 255));
        controlsTitle->SetAlignment(TextAlignment::Left);
        leftSidebar->AddChild(controlsTitle);
        
        auto layoutLabel = std::make_shared<UltraCanvasLabel>("layoutLabel", 1007, 0, 45, 190, 20);
        layoutLabel->SetText("Diagram Layout");
        layoutLabel->SetFontSize(10);
        layoutLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(layoutLabel);
        
        auto layoutSelector = std::make_shared<UltraCanvasDropdown>("layoutSelector", 1008, 0, 68, 190, 28);
        layoutSelector->AddItem("Two Circles", "2");
        layoutSelector->AddItem("Three Circles", "3");
        layoutSelector->AddItem("Four Circles", "4");
        layoutSelector->AddItem("Custom Layout", "custom");
        layoutSelector->SetSelectedIndex(1);
        leftSidebar->AddChild(layoutSelector);
        
        auto styleLabel = std::make_shared<UltraCanvasLabel>("styleLabel", 1009, 0, 110, 190, 20);
        styleLabel->SetText("Visual Style");
        styleLabel->SetFontSize(10);
        styleLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(styleLabel);
        
        auto styleSelector = std::make_shared<UltraCanvasDropdown>("styleSelector", 1010, 0, 133, 190, 28);
        styleSelector->AddItem("Classic", "classic");
        styleSelector->AddItem("Modern", "modern");
        styleSelector->AddItem("Minimal", "minimal");
        styleSelector->AddItem("Filled", "filled");
        styleSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(styleSelector);
        
        auto btnReset = std::make_shared<UltraCanvasButton>("btnReset", 1016, 0, 180, 190, 32);
        btnReset->SetText("Reset View");
        btnReset->SetBackgroundColor(Color(108, 117, 125, 255));
        leftSidebar->AddChild(btnReset);
        
        auto btnExport = std::make_shared<UltraCanvasButton>("btnExport", 1017, 0, 217, 190, 32);
        btnExport->SetText("Export Data");
        btnExport->SetBackgroundColor(Color(25, 135, 84, 255));
        leftSidebar->AddChild(btnExport);
        
        auto helpBox = std::make_shared<UltraCanvasLabel>("helpBox", 1018, 0, 300, 190, 80);
        helpBox->SetText("INTERACTION TIPS\n\n"
                         "- Drag circles to move\n"
                         "- Hover for details\n"
                         "- Switch tabs to explore");
        helpBox->SetFontSize(9);
        helpBox->SetAlignment(TextAlignment::Left);
        helpBox->SetBackgroundColor(Color(255, 243, 205, 255));
        helpBox->SetPadding(10);
        leftSidebar->AddChild(helpBox);
        
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("vennTabs", 1019, 260, 20, 680, 580);
        tabbedContainer->SetTabPosition(TabPosition::Top);
        contentContainer->AddChild(tabbedContainer);
        
        auto rightSidebar = std::make_shared<UltraCanvasContainer>("rightSidebar", 1020, 960, 20, 220, 580);
        rightSidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        rightSidebar->SetPadding(15);
        contentContainer->AddChild(rightSidebar);
        
        auto statsTitle = std::make_shared<UltraCanvasLabel>("statsTitle", 1021, 0, 0, 190, 30);
        statsTitle->SetText("SUMMARY STATISTICS");
        statsTitle->SetFontSize(11);
        statsTitle->SetFontWeight(FontWeight::Bold);
        statsTitle->SetTextColor(Color(73, 80, 87, 255));
        statsTitle->SetAlignment(TextAlignment::Left);
        rightSidebar->AddChild(statsTitle);
        
        auto statsPanel = std::make_shared<UltraCanvasLabel>("statsPanel", 1022, 0, 40, 190, 180);
        statsPanel->SetText("Total Sets: 3\nTotal Items: 35\nUnique Regions: 8\n\nIntersections:\n- 2-way: 3\n- 3-way: 1");
        statsPanel->SetFontSize(10);
        statsPanel->SetAlignment(TextAlignment::Left);
        statsPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        statsPanel->SetPadding(12);
        rightSidebar->AddChild(statsPanel);
        
        auto legendTitle = std::make_shared<UltraCanvasLabel>("legendTitle", 1023, 0, 240, 190, 30);
        legendTitle->SetText("INTERACTIVE LEGEND");
        legendTitle->SetFontSize(11);
        legendTitle->SetFontWeight(FontWeight::Bold);
        legendTitle->SetTextColor(Color(73, 80, 87, 255));
        legendTitle->SetAlignment(TextAlignment::Left);
        rightSidebar->AddChild(legendTitle);
        
        auto legendPanel = std::make_shared<UltraCanvasLabel>("legendPanel", 1024, 0, 280, 190, 160);
        legendPanel->SetText("Set A: Frontend\nSet B: Backend\nSet C: DevOps\n\nIntersections\nHover for details");
        legendPanel->SetFontSize(9);
        legendPanel->SetAlignment(TextAlignment::Left);
        legendPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        legendPanel->SetPadding(12);
        rightSidebar->AddChild(legendPanel);
        
        auto statusBar = std::make_shared<UltraCanvasLabel>("statusBar", 1025, 260, 610, 680, 60);
        statusBar->SetText("Ready - Select a tab to explore different Venn diagram examples");
        statusBar->SetFontSize(11);
        statusBar->SetAlignment(TextAlignment::Left);
        statusBar->SetBackgroundColor(Color(233, 236, 239, 255));
        statusBar->SetPadding(15);
        statusBar->SetTextColor(Color(73, 80, 87, 255));
        contentContainer->AddChild(statusBar);
        
        // TAB 1: TWO CIRCLES
        auto twoCirclesTab = std::make_shared<UltraCanvasContainer>("twoCirclesTab", 2000, 0, 0, 680, 542);
        twoCirclesTab->SetBackgroundColor(Color(255, 255, 255, 255));
        
        auto twoCirclesTitle = std::make_shared<UltraCanvasLabel>("twoCirclesTitle", 2001, 20, 20, 640, 35);
        twoCirclesTitle->SetText("Cats vs Dogs: Pet Preferences");
        twoCirclesTitle->SetFontSize(18);
        twoCirclesTitle->SetFontWeight(FontWeight::Bold);
        twoCirclesTitle->SetAlignment(TextAlignment::Center);
        twoCirclesTab->AddChild(twoCirclesTitle);
        
        auto twoCirclesVenn = std::make_shared<UltraCanvasVennDiagramElement>("twoCirclesVenn", 2003, 90, 100, 500, 400);
        twoCirclesVenn->ClearCircles();
        twoCirclesVenn->AddCircle("Cats", Color(255, 99, 132, 180));
        twoCirclesVenn->AddCircle("Dogs", Color(54, 162, 235, 180));
        twoCirclesVenn->AddItemToCircle(0, "Independent");
        twoCirclesVenn->AddItemToCircle(0, "Low maintenance");
        twoCirclesVenn->AddItemToCircle(0, "Litter trained");
        twoCirclesVenn->AddItemToCircle(1, "Loyal companion");
        twoCirclesVenn->AddItemToCircle(1, "Needs walks");
        twoCirclesVenn->AddItemToCircle(1, "Protective");
        twoCirclesVenn->AddItemToIntersection({0, 1}, "Furry");
        twoCirclesVenn->AddItemToIntersection({0, 1}, "Companionship");
        twoCirclesVenn->AddItemToIntersection({0, 1}, "Playful");
        twoCirclesVenn->SetLayout(VennLayout::TwoCircles);
        twoCirclesVenn->SetStyle(VennStyle::Classic);
        twoCirclesVenn->SetShowLabels(true);
        twoCirclesVenn->SetShowItemCounts(true);
        twoCirclesTab->AddChild(twoCirclesVenn);
        
        // TAB 2: THREE CIRCLES
        auto threeCirclesTab = std::make_shared<UltraCanvasContainer>("threeCirclesTab", 3000, 0, 0, 680, 542);
        threeCirclesTab->SetBackgroundColor(Color(255, 255, 255, 255));
        
        auto threeCirclesTitle = std::make_shared<UltraCanvasLabel>("threeCirclesTitle", 3001, 20, 20, 640, 35);
        threeCirclesTitle->SetText("Developer Skill Overlap Analysis");
        threeCirclesTitle->SetFontSize(18);
        threeCirclesTitle->SetFontWeight(FontWeight::Bold);
        threeCirclesTitle->SetAlignment(TextAlignment::Center);
        threeCirclesTab->AddChild(threeCirclesTitle);
        
        auto threeCirclesVenn = std::make_shared<UltraCanvasVennDiagramElement>("threeCirclesVenn", 3003, 90, 100, 500, 400);
        threeCirclesVenn->ClearCircles();
        threeCirclesVenn->AddCircle("Frontend", Color(54, 162, 235, 180));
        threeCirclesVenn->AddCircle("Backend", Color(255, 99, 132, 180));
        threeCirclesVenn->AddCircle("DevOps", Color(255, 205, 86, 180));
        
        threeCirclesVenn->AddItemToCircle(0, "React");
        threeCirclesVenn->AddItemToCircle(0, "Vue.js");
        threeCirclesVenn->AddItemToCircle(0, "CSS/SCSS");
        
        threeCirclesVenn->AddItemToCircle(1, "Node.js");
        threeCirclesVenn->AddItemToCircle(1, "Python");
        threeCirclesVenn->AddItemToCircle(1, "SQL");
        
        threeCirclesVenn->AddItemToCircle(2, "Docker");
        threeCirclesVenn->AddItemToCircle(2, "Kubernetes");
        threeCirclesVenn->AddItemToCircle(2, "AWS");
        
        threeCirclesVenn->AddItemToIntersection({0, 1}, "TypeScript");
        threeCirclesVenn->AddItemToIntersection({0, 1}, "GraphQL");
        
        threeCirclesVenn->AddItemToIntersection({1, 2}, "Monitoring");
        threeCirclesVenn->AddItemToIntersection({1, 2}, "Logging");
        
        threeCirclesVenn->AddItemToIntersection({0, 2}, "Performance");
        
        threeCirclesVenn->AddItemToIntersection({0, 1, 2}, "Git");
        threeCirclesVenn->AddItemToIntersection({0, 1, 2}, "Testing");
        
        threeCirclesVenn->SetLayout(VennLayout::ThreeCircles);
        threeCirclesVenn->SetStyle(VennStyle::Classic);
        threeCirclesVenn->SetShowLabels(true);
        threeCirclesVenn->SetShowItemCounts(true);
        threeCirclesTab->AddChild(threeCirclesVenn);
        
        // TAB 3: FOUR CIRCLES
        auto fourCirclesTab = std::make_shared<UltraCanvasContainer>("fourCirclesTab", 4000, 0, 0, 680, 542);
        fourCirclesTab->SetBackgroundColor(Color(255, 255, 255, 255));
        
        auto fourCirclesTitle = std::make_shared<UltraCanvasLabel>("fourCirclesTitle", 4001, 20, 20, 640, 35);
        fourCirclesTitle->SetText("Software Development Lifecycle");
        fourCirclesTitle->SetFontSize(18);
        fourCirclesTitle->SetFontWeight(FontWeight::Bold);
        fourCirclesTitle->SetAlignment(TextAlignment::Center);
        fourCirclesTab->AddChild(fourCirclesTitle);
        
        auto fourCirclesVenn = std::make_shared<UltraCanvasVennDiagramElement>("fourCirclesVenn", 4003, 90, 100, 500, 400);
        fourCirclesVenn->ClearCircles();
        fourCirclesVenn->AddCircle("Planning", Color(75, 192, 192, 180));
        fourCirclesVenn->AddCircle("Development", Color(255, 159, 64, 180));
        fourCirclesVenn->AddCircle("Testing", Color(153, 102, 255, 180));
        fourCirclesVenn->AddCircle("Deployment", Color(255, 99, 132, 180));
        
        fourCirclesVenn->AddItemToCircle(0, "Requirements");
        fourCirclesVenn->AddItemToCircle(0, "User Stories");
        fourCirclesVenn->AddItemToCircle(1, "Coding");
        fourCirclesVenn->AddItemToCircle(1, "Code Review");
        fourCirclesVenn->AddItemToCircle(2, "Unit Tests");
        fourCirclesVenn->AddItemToCircle(2, "Integration Tests");
        fourCirclesVenn->AddItemToCircle(3, "Release Notes");
        fourCirclesVenn->AddItemToCircle(3, "Production");
        
        fourCirclesVenn->AddItemToIntersection({0, 1}, "Design Docs");
        fourCirclesVenn->AddItemToIntersection({1, 2}, "TDD");
        fourCirclesVenn->AddItemToIntersection({2, 3}, "Staging");
        
        fourCirclesVenn->SetLayout(VennLayout::FourCircles);
        fourCirclesVenn->SetStyle(VennStyle::Classic);
        fourCirclesVenn->SetShowLabels(true);
        fourCirclesVenn->SetShowItemCounts(true);
        fourCirclesTab->AddChild(fourCirclesVenn);
        
        // TAB 4: CLOUD PLATFORMS
        auto cloudTab = std::make_shared<UltraCanvasContainer>("cloudTab", 5000, 0, 0, 680, 542);
        cloudTab->SetBackgroundColor(Color(255, 255, 255, 255));
        
        auto cloudTitle = std::make_shared<UltraCanvasLabel>("cloudTitle", 5001, 20, 20, 640, 35);
        cloudTitle->SetText("Cloud Platform Service Comparison");
        cloudTitle->SetFontSize(18);
        cloudTitle->SetFontWeight(FontWeight::Bold);
        cloudTitle->SetAlignment(TextAlignment::Center);
        cloudTab->AddChild(cloudTitle);
        
        auto cloudVenn = std::make_shared<UltraCanvasVennDiagramElement>("cloudVenn", 5003, 90, 100, 500, 400);
        cloudVenn->ClearCircles();
        cloudVenn->AddCircle("AWS", Color(255, 153, 51, 180));
        cloudVenn->AddCircle("Azure", Color(0, 120, 215, 180));
        cloudVenn->AddCircle("GCP", Color(66, 133, 244, 180));
        
        cloudVenn->AddItemToCircle(0, "EC2");
        cloudVenn->AddItemToCircle(0, "S3");
        cloudVenn->AddItemToCircle(0, "Lambda");
        
        cloudVenn->AddItemToCircle(1, "VMs");
        cloudVenn->AddItemToCircle(1, "Blob Storage");
        cloudVenn->AddItemToCircle(1, "Functions");
        
        cloudVenn->AddItemToCircle(2, "Compute Engine");
        cloudVenn->AddItemToCircle(2, "Cloud Storage");
        cloudVenn->AddItemToCircle(2, "Cloud Functions");
        
        cloudVenn->AddItemToIntersection({0, 1}, "Container Registry");
        cloudVenn->AddItemToIntersection({1, 2}, "Kubernetes");
        cloudVenn->AddItemToIntersection({0, 2}, "Data Lake");
        cloudVenn->AddItemToIntersection({0, 1, 2}, "Compute");
        cloudVenn->AddItemToIntersection({0, 1, 2}, "Storage");
        
        cloudVenn->SetLayout(VennLayout::ThreeCircles);
        cloudVenn->SetStyle(VennStyle::Classic);
        cloudVenn->SetShowLabels(true);
        cloudVenn->SetShowItemCounts(true);
        cloudTab->AddChild(cloudVenn);
        
        // TAB 5: CUSTOM PRESET
        auto customTab = std::make_shared<UltraCanvasContainer>("customTab", 6000, 0, 0, 680, 542);
        customTab->SetBackgroundColor(Color(255, 255, 255, 255));
        
        auto customTitle = std::make_shared<UltraCanvasLabel>("customTitle", 6001, 20, 20, 640, 35);
        customTitle->SetText("Custom Venn Diagram");
        customTitle->SetFontSize(18);
        customTitle->SetFontWeight(FontWeight::Bold);
        customTitle->SetAlignment(TextAlignment::Center);
        customTab->AddChild(customTitle);
        
        auto customVenn = std::make_shared<UltraCanvasVennDiagramElement>("customVenn", 6003, 90, 100, 500, 400);
        customVenn->ClearCircles();
        customVenn->AddCircle("Design", Color(255, 99, 132, 180));
        customVenn->AddCircle("Engineering", Color(54, 162, 235, 180));
        customVenn->AddCircle("Product", Color(255, 205, 86, 180));
        
        customVenn->AddItemToCircle(0, "UX Research");
        customVenn->AddItemToCircle(0, "Wireframes");
        customVenn->AddItemToCircle(1, "Architecture");
        customVenn->AddItemToCircle(1, "Implementation");
        customVenn->AddItemToCircle(2, "Roadmap");
        customVenn->AddItemToCircle(2, "Metrics");
        
        customVenn->AddItemToIntersection({0, 1}, "Prototypes");
        customVenn->AddItemToIntersection({1, 2}, "Technical Specs");
        customVenn->AddItemToIntersection({0, 2}, "User Stories");
        customVenn->AddItemToIntersection({0, 1, 2}, "Sprint Planning");
        
        customVenn->SetLayout(VennLayout::ThreeCircles);
        customVenn->SetStyle(VennStyle::Modern);
        customVenn->SetShowLabels(true);
        customVenn->SetShowItemCounts(true);
        customTab->AddChild(customVenn);
        
        // Add all tabs
        tabbedContainer->AddTab("Two Circles", twoCirclesTab);
        tabbedContainer->AddTab("Three Circles", threeCirclesTab);
        tabbedContainer->AddTab("Four Circles", fourCirclesTab);
        tabbedContainer->AddTab("Cloud Platforms", cloudTab);
        tabbedContainer->AddTab("Custom", customTab);
        
        layoutSelector->onSelectionChanged = [layoutSelector, twoCirclesVenn, threeCirclesVenn, fourCirclesVenn, cloudVenn, customVenn](int index, const DropdownItem& item) {
            auto getVenn = [layoutSelector]() -> std::shared_ptr<UltraCanvasVennDiagramElement> {
                int tabIdx = 0;
                if (layoutSelector->GetSelectedIndex() == 0) return nullptr;
                return nullptr;
            };
            
            if (item.value == "2") {
                twoCirclesVenn->SetLayout(VennLayout::TwoCircles);
            } else if (item.value == "3") {
                threeCirclesVenn->SetLayout(VennLayout::ThreeCircles);
            } else if (item.value == "4") {
                fourCirclesVenn->SetLayout(VennLayout::FourCircles);
            } else if (item.value == "custom") {
                customVenn->SetLayout(VennLayout::Custom);
            }
        };
        
        styleSelector->onSelectionChanged = [styleSelector, twoCirclesVenn, threeCirclesVenn, fourCirclesVenn, cloudVenn, customVenn](int index, const DropdownItem& item) {
            VennStyle style = VennStyle::Classic;
            if (item.value == "modern") style = VennStyle::Modern;
            else if (item.value == "minimal") style = VennStyle::Minimal;
            else if (item.value == "filled") style = VennStyle::Filled;
            
            twoCirclesVenn->SetStyle(style);
            threeCirclesVenn->SetStyle(style);
            fourCirclesVenn->SetStyle(style);
            cloudVenn->SetStyle(style);
            customVenn->SetStyle(style);
        };
        
        btnReset->onClick = [layoutSelector, styleSelector, twoCirclesVenn, threeCirclesVenn, fourCirclesVenn, cloudVenn, customVenn]() {
            layoutSelector->SetSelectedIndex(1);
            styleSelector->SetSelectedIndex(0);
            
            twoCirclesVenn->SetLayout(VennLayout::TwoCircles);
            threeCirclesVenn->SetLayout(VennLayout::ThreeCircles);
            fourCirclesVenn->SetLayout(VennLayout::FourCircles);
            cloudVenn->SetLayout(VennLayout::ThreeCircles);
            customVenn->SetLayout(VennLayout::ThreeCircles);
            
            twoCirclesVenn->SetStyle(VennStyle::Classic);
            threeCirclesVenn->SetStyle(VennStyle::Classic);
            fourCirclesVenn->SetStyle(VennStyle::Classic);
            cloudVenn->SetStyle(VennStyle::Classic);
            customVenn->SetStyle(VennStyle::Modern);
        };
        
        btnExport->onClick = [statusBar]() {
            statusBar->SetText("Export functionality coming soon!");
        };
        
        // Setup hover callbacks
        twoCirclesVenn->SetOnCircleHover([statusBar](size_t idx, const VennCircle& circle) {
            statusBar->SetText("Hovering: " + circle.label + " (" + std::to_string(circle.items.size()) + " items)");
        });
        
        threeCirclesVenn->SetOnCircleHover([statusBar](size_t idx, const VennCircle& circle) {
            statusBar->SetText("Hovering: " + circle.label + " (" + std::to_string(circle.items.size()) + " items)");
        });
        
        fourCirclesVenn->SetOnCircleHover([statusBar](size_t idx, const VennCircle& circle) {
            statusBar->SetText("Hovering: " + circle.label + " (" + std::to_string(circle.items.size()) + " items)");
        });
        
        cloudVenn->SetOnCircleHover([statusBar](size_t idx, const VennCircle& circle) {
            statusBar->SetText("Hovering: " + circle.label + " (" + std::to_string(circle.items.size()) + " items)");
        });
        
        customVenn->SetOnCircleHover([statusBar](size_t idx, const VennCircle& circle) {
            statusBar->SetText("Hovering: " + circle.label + " (" + std::to_string(circle.items.size()) + " items)");
        });
        
        return mainContainer;
    }

} // namespace UltraCanvas

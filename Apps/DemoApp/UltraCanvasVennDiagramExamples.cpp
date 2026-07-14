// Apps/DemoApp/UltraCanvasVennDiagramExamples.cpp
// Professional Venn Diagram examples with Sankey-quality UI
// Version: 4.0.0 (Rectangular/nested variant + corrected label layout)
// Last Modified: 2026-07-13

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasVennDiagram.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include <vector>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateVennDiagramExamples() {
        // Inset used for every child placed directly inside a sidebar/panel
        // container. The containers do NOT use SetPadding(): their content area
        // would then clip absolutely-positioned children, cutting the left edge
        // of each label (the "shows only partial" bug). Insetting explicitly –
        // the same pattern the Sankey demo uses – keeps every label fully drawn.
        const int PAD = 15;

        auto mainContainer = std::make_shared<UltraCanvasContainer>("vennMainContainer", 0, 0, 1140, 800);
        mainContainer->SetBackgroundColor(Color(248, 249, 250, 255));

        auto headerContainer = std::make_shared<UltraCanvasContainer>("headerContainer", 0, 0, 1140, 90);
        headerContainer->SetBackgroundColor(Color(255, 255, 255, 255));
        mainContainer->AddChild(headerContainer);

        auto mainTitle = std::make_shared<UltraCanvasLabel>("mainTitle", 30, 15, 1080, 35);
        mainTitle->SetText("Venn Diagram Analysis Suite");
        mainTitle->SetFontSize(22);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainTitle->SetAlignment(TextAlignment::Left);
        mainTitle->SetTextColor(Color(33, 37, 41, 255));
        headerContainer->AddChild(mainTitle);

        auto subtitle = std::make_shared<UltraCanvasLabel>("subtitle", 30, 52, 900, 30);
        subtitle->SetText("Interactive visualization of set relationships - circular and rectangular styles");
        subtitle->SetFontSize(13);
        subtitle->SetAlignment(TextAlignment::Left);
        subtitle->SetTextColor(Color(108, 117, 125, 255));
        headerContainer->AddChild(subtitle);

        auto contentContainer = std::make_shared<UltraCanvasContainer>("contentContainer", 0, 90, 1140, 710);
        mainContainer->AddChild(contentContainer);

        // ===== LEFT SIDEBAR (controls) =====
        auto leftSidebar = std::make_shared<UltraCanvasContainer>("leftSidebar", 20, 20, 200, 670);
        leftSidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        contentContainer->AddChild(leftSidebar);

        const int SIDEBAR_W = 170;   // usable child width inside the 200px sidebar

        auto controlsTitle = std::make_shared<UltraCanvasLabel>("controlsTitle", PAD, PAD, SIDEBAR_W, 22);
        controlsTitle->SetText("CONTROLS");
        controlsTitle->SetFontSize(11);
        controlsTitle->SetFontWeight(FontWeight::Bold);
        controlsTitle->SetTextColor(Color(73, 80, 87, 255));
        controlsTitle->SetAlignment(TextAlignment::Left);
        leftSidebar->AddChild(controlsTitle);

        auto layoutLabel = std::make_shared<UltraCanvasLabel>("layoutLabel", PAD, 55, SIDEBAR_W, 20);
        layoutLabel->SetText("Diagram Layout");
        layoutLabel->SetFontSize(10);
        layoutLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(layoutLabel);

        auto layoutSelector = std::make_shared<UltraCanvasDropdown>("layoutSelector", PAD, 78, SIDEBAR_W, 28);
        layoutSelector->AddItem("Two Circles", "2");
        layoutSelector->AddItem("Three Circles", "3");
        layoutSelector->AddItem("Four Circles", "4");
        layoutSelector->AddItem("Nested (Boxes)", "nested");
        layoutSelector->SetSelectedIndex(1);
        leftSidebar->AddChild(layoutSelector);

        auto styleLabel = std::make_shared<UltraCanvasLabel>("styleLabel", PAD, 118, SIDEBAR_W, 20);
        styleLabel->SetText("Visual Style");
        styleLabel->SetFontSize(10);
        styleLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(styleLabel);

        auto styleSelector = std::make_shared<UltraCanvasDropdown>("styleSelector", PAD, 141, SIDEBAR_W, 28);
        styleSelector->AddItem("Classic", "classic");
        styleSelector->AddItem("Modern", "modern");
        styleSelector->AddItem("Minimal", "minimal");
        styleSelector->AddItem("Filled", "filled");
        styleSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(styleSelector);

        auto shapeLabel = std::make_shared<UltraCanvasLabel>("shapeLabel", PAD, 181, SIDEBAR_W, 20);
        shapeLabel->SetText("Set Shape");
        shapeLabel->SetFontSize(10);
        shapeLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(shapeLabel);

        auto shapeSelector = std::make_shared<UltraCanvasDropdown>("shapeSelector", PAD, 204, SIDEBAR_W, 28);
        shapeSelector->AddItem("Circles", "circle");
        shapeSelector->AddItem("Rounded Boxes", "rect");
        shapeSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(shapeSelector);

        auto btnReset = std::make_shared<UltraCanvasButton>("btnReset", PAD, 250, SIDEBAR_W, 32);
        btnReset->SetText("Reset View");
        btnReset->SetBackgroundColor(Color(108, 117, 125, 255));
        leftSidebar->AddChild(btnReset);

        auto btnExport = std::make_shared<UltraCanvasButton>("btnExport", PAD, 287, SIDEBAR_W, 32);
        btnExport->SetText("Export Data");
        btnExport->SetBackgroundColor(Color(25, 135, 84, 255));
        leftSidebar->AddChild(btnExport);

        auto helpBox = std::make_shared<UltraCanvasLabel>("helpBox", PAD, 340, SIDEBAR_W, 110);
        helpBox->SetText("INTERACTION TIPS\n\n"
                         "- Drag shapes to move\n"
                         "- Hover for details\n"
                         "- Switch tabs to explore\n"
                         "- Try the Boxes shape");
        helpBox->SetFontSize(9);
        helpBox->SetAlignment(TextAlignment::Left);
        helpBox->SetBackgroundColor(Color(255, 243, 205, 255));
        helpBox->SetPadding(10);
        leftSidebar->AddChild(helpBox);

        // ===== TABS =====
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("vennTabs", 240, 20, 660, 580);
        tabbedContainer->SetTabPosition(TabPosition::Top);
        contentContainer->AddChild(tabbedContainer);

        // ===== RIGHT SIDEBAR (stats/legend) =====
        auto rightSidebar = std::make_shared<UltraCanvasContainer>("rightSidebar", 920, 20, 200, 580);
        rightSidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        contentContainer->AddChild(rightSidebar);

        auto statsTitle = std::make_shared<UltraCanvasLabel>("statsTitle", PAD, PAD, SIDEBAR_W, 24);
        statsTitle->SetText("SUMMARY STATISTICS");
        statsTitle->SetFontSize(11);
        statsTitle->SetWrap(TextWrap::WrapWord);
        statsTitle->SetFontWeight(FontWeight::Bold);
        statsTitle->SetTextColor(Color(73, 80, 87, 255));
        statsTitle->SetAlignment(TextAlignment::Left);
        rightSidebar->AddChild(statsTitle);

        auto statsPanel = std::make_shared<UltraCanvasLabel>("statsPanel", PAD, 48, SIDEBAR_W, 170);
        statsPanel->SetText("Total Sets: 3\nTotal Items: 16\nUnique Regions: 7\n\nIntersections:\n- 2-way: 3\n- 3-way: 1");
        statsPanel->SetFontSize(10);
        statsPanel->SetAlignment(TextAlignment::Left);
        statsPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        statsPanel->SetPadding(12);
        rightSidebar->AddChild(statsPanel);

        auto legendTitle = std::make_shared<UltraCanvasLabel>("legendTitle", PAD, 240, SIDEBAR_W, 24);
        legendTitle->SetText("INTERACTIVE LEGEND");
        legendTitle->SetFontSize(11);
        legendTitle->SetFontWeight(FontWeight::Bold);
        legendTitle->SetWrap(TextWrap::WrapWord);
        legendTitle->SetTextColor(Color(73, 80, 87, 255));
        legendTitle->SetAlignment(TextAlignment::Left);
        rightSidebar->AddChild(legendTitle);

        auto legendPanel = std::make_shared<UltraCanvasLabel>("legendPanel", PAD, 272, SIDEBAR_W, 170);
        legendPanel->SetText("Set A: Frontend\nSet B: Backend\nSet C: DevOps\n\nOverlaps show shared\nskills. Hover a shape\nfor its item count.");
        legendPanel->SetFontSize(9);
        legendPanel->SetAlignment(TextAlignment::Left);
        legendPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        legendPanel->SetPadding(12);
        rightSidebar->AddChild(legendPanel);

        auto statusBar = std::make_shared<UltraCanvasLabel>("statusBar", 240, 610, 660, 60);
        statusBar->SetText("Ready - Select a tab to explore different Venn diagram examples");
        statusBar->SetFontSize(11);
        statusBar->SetAlignment(TextAlignment::Left);
        statusBar->SetBackgroundColor(Color(233, 236, 239, 255));
        statusBar->SetPadding(15);
        statusBar->SetTextColor(Color(73, 80, 87, 255));
        contentContainer->AddChild(statusBar);

        // The Venn element sits below the tab's own title label; 110px of top
        // room leaves space for the (now centred) set labels above each shape.
        const int VENN_X = 80, VENN_Y = 110, VENN_W = 500, VENN_H = 380;

        // TAB 1: TWO CIRCLES
        auto twoCirclesTab = std::make_shared<UltraCanvasContainer>("twoCirclesTab", 0, 0, 660, 542);
        twoCirclesTab->SetBackgroundColor(Color(255, 255, 255, 255));

        auto twoCirclesTitle = std::make_shared<UltraCanvasLabel>("twoCirclesTitle", 20, 20, 620, 35);
        twoCirclesTitle->SetText("Cats vs Dogs: Pet Preferences");
        twoCirclesTitle->SetFontSize(18);
        twoCirclesTitle->SetFontWeight(FontWeight::Bold);
        twoCirclesTitle->SetAlignment(TextAlignment::Center);
        twoCirclesTab->AddChild(twoCirclesTitle);

        auto twoCirclesVenn = std::make_shared<UltraCanvasVennDiagramElement>("twoCirclesVenn", VENN_X, VENN_Y, VENN_W, VENN_H);
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
        auto threeCirclesTab = std::make_shared<UltraCanvasContainer>("threeCirclesTab", 0, 0, 660, 542);
        threeCirclesTab->SetBackgroundColor(Color(255, 255, 255, 255));

        auto threeCirclesTitle = std::make_shared<UltraCanvasLabel>("threeCirclesTitle", 20, 20, 620, 35);
        threeCirclesTitle->SetText("Developer Skill Overlap Analysis");
        threeCirclesTitle->SetFontSize(18);
        threeCirclesTitle->SetFontWeight(FontWeight::Bold);
        threeCirclesTitle->SetAlignment(TextAlignment::Center);
        threeCirclesTab->AddChild(threeCirclesTitle);

        auto threeCirclesVenn = std::make_shared<UltraCanvasVennDiagramElement>("threeCirclesVenn", VENN_X, VENN_Y, VENN_W, VENN_H);
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
        auto fourCirclesTab = std::make_shared<UltraCanvasContainer>("fourCirclesTab", 0, 0, 660, 542);
        fourCirclesTab->SetBackgroundColor(Color(255, 255, 255, 255));

        auto fourCirclesTitle = std::make_shared<UltraCanvasLabel>("fourCirclesTitle", 20, 20, 620, 35);
        fourCirclesTitle->SetText("Software Development Lifecycle");
        fourCirclesTitle->SetFontSize(18);
        fourCirclesTitle->SetFontWeight(FontWeight::Bold);
        fourCirclesTitle->SetAlignment(TextAlignment::Center);
        fourCirclesTab->AddChild(fourCirclesTitle);

        auto fourCirclesVenn = std::make_shared<UltraCanvasVennDiagramElement>("fourCirclesVenn", VENN_X, VENN_Y, VENN_W, VENN_H);
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

        // TAB 4: SET HIERARCHY (rectangular / LaTeX style)
        auto hierarchyTab = std::make_shared<UltraCanvasContainer>("hierarchyTab", 0, 0, 660, 542);
        hierarchyTab->SetBackgroundColor(Color(255, 255, 255, 255));

        auto hierarchyTitle = std::make_shared<UltraCanvasLabel>("hierarchyTitle", 20, 20, 620, 35);
        hierarchyTitle->SetText("Algebraic Structures (Set Hierarchy)");
        hierarchyTitle->SetFontSize(18);
        hierarchyTitle->SetFontWeight(FontWeight::Bold);
        hierarchyTitle->SetAlignment(TextAlignment::Center);
        hierarchyTab->AddChild(hierarchyTitle);

        auto hierarchyHint = std::make_shared<UltraCanvasLabel>("hierarchyHint", 20, 56, 620, 24);
        hierarchyHint->SetText("Nested rectangular sets: Group superset Abelian Group superset Ring superset Field");
        hierarchyHint->SetFontSize(11);
        hierarchyHint->SetAlignment(TextAlignment::Center);
        hierarchyHint->SetTextColor(Color(108, 117, 125, 255));
        hierarchyTab->AddChild(hierarchyHint);

        auto hierarchyVenn = std::make_shared<UltraCanvasVennDiagramElement>("hierarchyVenn", 60, 90, 540, 420);
        hierarchyVenn->ClearCircles();
        hierarchyVenn->AddCircle("Group", Color(210, 244, 180, 200));
        hierarchyVenn->AddCircle("Abelian Group", Color(248, 200, 210, 210));
        hierarchyVenn->AddCircle("Ring", Color(190, 200, 255, 210));
        hierarchyVenn->AddCircle("Field", Color(255, 205, 205, 220));

        hierarchyVenn->AddItemToCircle(0, "GL(n)");
        hierarchyVenn->AddItemToCircle(0, "O(n)");
        hierarchyVenn->AddItemToCircle(1, "(Z, +)");
        hierarchyVenn->AddItemToCircle(2, "(Z, +, *)");
        hierarchyVenn->AddItemToCircle(3, "(Q, +, *)");
        hierarchyVenn->AddItemToCircle(3, "(R, +, *)");
        hierarchyVenn->AddItemToCircle(3, "(C, +, *)");

        hierarchyVenn->SetShape(VennShape::RoundedRectangle);
        hierarchyVenn->SetLayout(VennLayout::Nested);
        hierarchyVenn->SetStyle(VennStyle::Filled);
        hierarchyVenn->SetShowLabels(true);
        hierarchyVenn->SetShowItemCounts(true);
        hierarchyTab->AddChild(hierarchyVenn);

        // TAB 5: CLOUD PLATFORMS
        auto cloudTab = std::make_shared<UltraCanvasContainer>("cloudTab", 0, 0, 660, 542);
        cloudTab->SetBackgroundColor(Color(255, 255, 255, 255));

        auto cloudTitle = std::make_shared<UltraCanvasLabel>("cloudTitle", 20, 20, 620, 35);
        cloudTitle->SetText("Cloud Platform Service Comparison");
        cloudTitle->SetFontSize(18);
        cloudTitle->SetFontWeight(FontWeight::Bold);
        cloudTitle->SetAlignment(TextAlignment::Center);
        cloudTab->AddChild(cloudTitle);

        auto cloudVenn = std::make_shared<UltraCanvasVennDiagramElement>("cloudVenn", VENN_X, VENN_Y, VENN_W, VENN_H);
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

        // TAB 6: CUSTOM PRESET
        auto customTab = std::make_shared<UltraCanvasContainer>("customTab", 0, 0, 660, 542);
        customTab->SetBackgroundColor(Color(255, 255, 255, 255));

        auto customTitle = std::make_shared<UltraCanvasLabel>("customTitle", 20, 20, 620, 35);
        customTitle->SetText("Cross-Functional Team Overlap");
        customTitle->SetFontSize(18);
        customTitle->SetFontWeight(FontWeight::Bold);
        customTitle->SetAlignment(TextAlignment::Center);
        customTab->AddChild(customTitle);

        auto customVenn = std::make_shared<UltraCanvasVennDiagramElement>("customVenn", VENN_X, VENN_Y, VENN_W, VENN_H);
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
        tabbedContainer->AddTab("Set Hierarchy", hierarchyTab);
        tabbedContainer->AddTab("Cloud Platforms", cloudTab);
        tabbedContainer->AddTab("Custom", customTab);

        // The overlapping-Venn diagrams that respond to the layout dropdown.
        std::vector<std::shared_ptr<UltraCanvasVennDiagramElement>> allVenns = {
            twoCirclesVenn, threeCirclesVenn, fourCirclesVenn, hierarchyVenn, cloudVenn, customVenn
        };

        layoutSelector->onSelectionChanged =
            [twoCirclesVenn, threeCirclesVenn, fourCirclesVenn](int, const DropdownItem& item) {
                if (item.value == "2") {
                    twoCirclesVenn->SetLayout(VennLayout::TwoCircles);
                } else if (item.value == "3") {
                    threeCirclesVenn->SetLayout(VennLayout::ThreeCircles);
                } else if (item.value == "4") {
                    fourCirclesVenn->SetLayout(VennLayout::FourCircles);
                } else if (item.value == "nested") {
                    // Nested layout reads best as boxes.
                    threeCirclesVenn->SetShape(VennShape::RoundedRectangle);
                    threeCirclesVenn->SetLayout(VennLayout::Nested);
                }
            };

        styleSelector->onSelectionChanged =
            [allVenns](int, const DropdownItem& item) {
                VennStyle style = VennStyle::Classic;
                if (item.value == "modern") style = VennStyle::Modern;
                else if (item.value == "minimal") style = VennStyle::Minimal;
                else if (item.value == "filled") style = VennStyle::Filled;
                for (auto& v : allVenns) v->SetStyle(style);
            };

        shapeSelector->onSelectionChanged =
            [allVenns](int, const DropdownItem& item) {
                VennShape shape = (item.value == "rect") ? VennShape::RoundedRectangle
                                                         : VennShape::Circle;
                for (auto& v : allVenns) v->SetShape(shape);
            };

        btnReset->onClick =
            [layoutSelector, styleSelector, shapeSelector,
             twoCirclesVenn, threeCirclesVenn, fourCirclesVenn, hierarchyVenn, cloudVenn, customVenn]() {
                layoutSelector->SetSelectedIndex(1);
                styleSelector->SetSelectedIndex(0);
                shapeSelector->SetSelectedIndex(0);

                twoCirclesVenn->SetShape(VennShape::Circle);
                threeCirclesVenn->SetShape(VennShape::Circle);
                fourCirclesVenn->SetShape(VennShape::Circle);
                cloudVenn->SetShape(VennShape::Circle);
                customVenn->SetShape(VennShape::Circle);

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

                // The hierarchy tab is inherently rectangular + nested.
                hierarchyVenn->SetShape(VennShape::RoundedRectangle);
                hierarchyVenn->SetLayout(VennLayout::Nested);
                hierarchyVenn->SetStyle(VennStyle::Filled);
            };

        btnExport->onClick = [statusBar]() {
            statusBar->SetText("Export functionality coming soon!");
        };

        // Hover callbacks - report the hovered set into the status bar.
        auto hoverCb = [statusBar](size_t, const VennCircle& circle) {
            statusBar->SetText("Hovering: " + circle.label +
                               " (" + std::to_string(circle.items.size()) + " items)");
        };
        for (auto& v : allVenns) v->SetOnCircleHover(hoverCb);

        return mainContainer;
    }

} // namespace UltraCanvas

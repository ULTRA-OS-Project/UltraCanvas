// Apps/DemoApp/UltraCanvasVennDiagramExamples.cpp
// Professional Venn Diagram examples with Sankey-quality UI
// Version: 4.1.0 - the shared page look (UltraCanvasDemoPageKit): controls, tabs and
//                  statistics as cards in a wrapping row, every label and panel
//                  sized to its text, buttons as wide as their text
// Version: 4.0.0 (Rectangular/nested variant + corrected label layout)
// Last Modified: 2026-10-23

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Diagrams/UltraCanvasVennDiagram.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include <vector>

namespace UltraCanvas {

    // One tab's page: its title centred over the diagram. The tabbed
    // container gives it its size; it scrolls when a longer title wraps and
    // the diagram no longer fits under it.
    static std::shared_ptr<UltraCanvasContainer> MakeVennTabPage(const std::string& id,
                                                                 const std::string& titleId,
                                                                 const std::string& title) {
        auto tab = std::make_shared<UltraCanvasContainer>(id);
        tab->SetBackgroundColor(Color(255, 255, 255, 255));
        tab->SetPadding(20, 20, 12, 20);
        tab->layout.SetFlexColumn().SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        ContainerStyle style;
        style.autoShowScrollbars = true;
        tab->SetContainerStyle(style);

        auto titleLabel = DemoKit::CreateText(titleId, title);
        titleLabel->SetFontSize(18);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Top);
        DemoKit::Add(tab, titleLabel);
        return tab;
    }

    // A diagram keeps the size it was built with, centred in its tab.
    static void AddVennDiagram(const std::shared_ptr<UltraCanvasContainer>& tab,
                               const std::shared_ptr<UltraCanvasVennDiagramElement>& venn) {
        DemoKit::AddFixed(tab, venn);
        venn->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
    }

    // A sidebar section label: small and bold above its control.
    static void AddVennControlLabel(const std::shared_ptr<UltraCanvasContainer>& parent,
                                    const std::string& id, const std::string& text) {
        auto label = DemoKit::AddText(parent, id, text);
        label->SetFontSize(10);
        label->SetFontWeight(FontWeight::Bold);
        label->SetMargin(6, 0, 0, 0);
    }

    // A sidebar heading in the sidebars' small capitals style.
    static void AddVennSidebarHeading(const std::shared_ptr<UltraCanvasContainer>& parent,
                                      const std::string& id, const std::string& text) {
        auto heading = DemoKit::AddText(parent, id, text);
        heading->SetFontSize(11);
        heading->SetFontWeight(FontWeight::Bold);
        heading->SetTextColor(Color(73, 80, 87, 255));
    }

    // A grey or tinted text panel in a sidebar: wraps and grows with its text.
    static std::shared_ptr<UltraCanvasLabel> AddVennPanelText(const std::shared_ptr<UltraCanvasContainer>& parent,
                                                              const std::string& id, const std::string& text,
                                                              float fontSize, const Color& background,
                                                              int padding) {
        auto panel = DemoKit::AddText(parent, id, text);
        panel->SetFontSize(fontSize);
        panel->SetBackgroundColor(background);
        panel->SetPadding(padding);
        return panel;
    }

    // A sidebar button as wide as its text, at least the width it had.
    static std::shared_ptr<UltraCanvasButton> MakeVennButton(const std::string& id, const std::string& text) {
        auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, 32, text);
        CSSLayout::BoxConstraints limits;
        limits.minWidth = CSSLayout::Dimension::Px(170);
        button->boxConstraints = limits;
        return button;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateVennDiagramExamples() {
        auto page = DemoKit::CreatePage("vennMainContainer", "Venn Diagram Analysis Suite",
                "Interactive visualization of set relationships - circular and rectangular styles");

        // Controls, the tabs and the statistics side by side; a narrow window
        // puts the sidebars under the tabs.
        auto contentContainer = DemoKit::AddRow(page, "contentContainer");

        // ===== LEFT SIDEBAR (controls) =====
        auto leftSidebar = DemoKit::AddCard(contentContainer, "leftSidebar", "", "", 200);
        AddVennSidebarHeading(leftSidebar, "controlsTitle", "CONTROLS");

        AddVennControlLabel(leftSidebar, "layoutLabel", "Diagram Layout");
        auto layoutSelector = std::make_shared<UltraCanvasDropdown>("layoutSelector", 0, 0, 170, 28);
        layoutSelector->AddItem("Two Circles", "2");
        layoutSelector->AddItem("Three Circles", "3");
        layoutSelector->AddItem("Four Circles", "4");
        layoutSelector->AddItem("Nested (Boxes)", "nested");
        layoutSelector->SetSelectedIndex(1);
        DemoKit::AddStretched(leftSidebar, layoutSelector);

        AddVennControlLabel(leftSidebar, "styleLabel", "Visual Style");
        auto styleSelector = std::make_shared<UltraCanvasDropdown>("styleSelector", 0, 0, 170, 28);
        styleSelector->AddItem("Classic", "classic");
        styleSelector->AddItem("Modern", "modern");
        styleSelector->AddItem("Minimal", "minimal");
        styleSelector->AddItem("Filled", "filled");
        styleSelector->SetSelectedIndex(0);
        DemoKit::AddStretched(leftSidebar, styleSelector);

        AddVennControlLabel(leftSidebar, "shapeLabel", "Set Shape");
        auto shapeSelector = std::make_shared<UltraCanvasDropdown>("shapeSelector", 0, 0, 170, 28);
        shapeSelector->AddItem("Circles", "circle");
        shapeSelector->AddItem("Rounded Boxes", "rect");
        shapeSelector->SetSelectedIndex(0);
        DemoKit::AddStretched(leftSidebar, shapeSelector);

        auto btnReset = MakeVennButton("btnReset", "Reset View");
        btnReset->SetBackgroundColor(Color(108, 117, 125, 255));
        btnReset->SetMargin(12, 0, 0, 0);
        DemoKit::Add(leftSidebar, btnReset);

        auto btnExport = MakeVennButton("btnExport", "Export Data");
        btnExport->SetBackgroundColor(Color(25, 135, 84, 255));
        DemoKit::Add(leftSidebar, btnExport);

        auto helpBox = AddVennPanelText(leftSidebar, "helpBox",
                "INTERACTION TIPS\n\n"
                "- Drag shapes to move\n"
                "- Hover for details\n"
                "- Switch tabs to explore\n"
                "- Try the Boxes shape",
                9, Color(255, 243, 205, 255), 10);
        helpBox->SetMargin(12, 0, 0, 0);

        // ===== TABS =====
        auto tabsCard = DemoKit::AddCard(contentContainer, "vennTabsCard", "", "", 690);
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("vennTabs", 0, 0, 660, 580);
        tabbedContainer->SetTabPosition(TabPosition::Top);
        // As wide as the card, never narrower than the diagrams need.
        DemoKit::AddStretched(tabsCard, tabbedContainer);
        {
            CSSLayout::BoxConstraints limits;
            limits.minWidth = CSSLayout::Dimension::Px(660);
            tabbedContainer->boxConstraints = limits;
        }

        auto statusBar = AddVennPanelText(tabsCard, "statusBar",
                "Ready - Select a tab to explore different Venn diagram examples",
                11, Color(233, 236, 239, 255), 15);
        statusBar->SetTextColor(Color(73, 80, 87, 255));

        // ===== RIGHT SIDEBAR (stats/legend) =====
        auto rightSidebar = DemoKit::AddCard(contentContainer, "rightSidebar", "", "", 200);
        AddVennSidebarHeading(rightSidebar, "statsTitle", "SUMMARY STATISTICS");
        AddVennPanelText(rightSidebar, "statsPanel",
                "Total Sets: 3\nTotal Items: 16\nUnique Regions: 7\n\nIntersections:\n- 2-way: 3\n- 3-way: 1",
                10, Color(248, 249, 250, 255), 12);

        auto legendTitle = DemoKit::CreateText("legendTitle", "INTERACTIVE LEGEND");
        legendTitle->SetFontSize(11);
        legendTitle->SetFontWeight(FontWeight::Bold);
        legendTitle->SetTextColor(Color(73, 80, 87, 255));
        legendTitle->SetMargin(12, 0, 0, 0);
        DemoKit::Add(rightSidebar, legendTitle);
        AddVennPanelText(rightSidebar, "legendPanel",
                "Set A: Frontend\nSet B: Backend\nSet C: DevOps\n\nOverlaps show shared "
                "skills. Hover a shape for its item count.",
                9, Color(248, 249, 250, 255), 12);

        // Every overlapping diagram is built at the same size.
        const int VENN_W = 500, VENN_H = 380;

        // TAB 1: TWO CIRCLES
        auto twoCirclesTab = MakeVennTabPage("twoCirclesTab", "twoCirclesTitle", "Cats vs Dogs: Pet Preferences");

        auto twoCirclesVenn = std::make_shared<UltraCanvasVennDiagramElement>("twoCirclesVenn", 0, 0, VENN_W, VENN_H);
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
        AddVennDiagram(twoCirclesTab, twoCirclesVenn);

        // TAB 2: THREE CIRCLES
        auto threeCirclesTab = MakeVennTabPage("threeCirclesTab", "threeCirclesTitle", "Developer Skill Overlap Analysis");

        auto threeCirclesVenn = std::make_shared<UltraCanvasVennDiagramElement>("threeCirclesVenn", 0, 0, VENN_W, VENN_H);
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
        AddVennDiagram(threeCirclesTab, threeCirclesVenn);

        // TAB 3: FOUR CIRCLES
        auto fourCirclesTab = MakeVennTabPage("fourCirclesTab", "fourCirclesTitle", "Software Development Lifecycle");

        auto fourCirclesVenn = std::make_shared<UltraCanvasVennDiagramElement>("fourCirclesVenn", 0, 0, VENN_W, VENN_H);
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
        AddVennDiagram(fourCirclesTab, fourCirclesVenn);

        // TAB 4: SET HIERARCHY (rectangular / LaTeX style)
        auto hierarchyTab = MakeVennTabPage("hierarchyTab", "hierarchyTitle", "Algebraic Structures (Set Hierarchy)");

        auto hierarchyHint = DemoKit::AddText(hierarchyTab, "hierarchyHint",
                "Nested rectangular sets: Group superset Abelian Group superset Ring superset Field");
        hierarchyHint->SetFontSize(11);
        hierarchyHint->SetAlignment(TextAlignment::Center, VerticalAlignment::Top);
        hierarchyHint->SetTextColor(Color(108, 117, 125, 255));

        auto hierarchyVenn = std::make_shared<UltraCanvasVennDiagramElement>("hierarchyVenn", 0, 0, 540, 420);
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
        AddVennDiagram(hierarchyTab, hierarchyVenn);

        // TAB 5: CLOUD PLATFORMS
        auto cloudTab = MakeVennTabPage("cloudTab", "cloudTitle", "Cloud Platform Service Comparison");

        auto cloudVenn = std::make_shared<UltraCanvasVennDiagramElement>("cloudVenn", 0, 0, VENN_W, VENN_H);
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
        AddVennDiagram(cloudTab, cloudVenn);

        // TAB 6: CUSTOM PRESET
        auto customTab = MakeVennTabPage("customTab", "customTitle", "Cross-Functional Team Overlap");

        auto customVenn = std::make_shared<UltraCanvasVennDiagramElement>("customVenn", 0, 0, VENN_W, VENN_H);
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
        AddVennDiagram(customTab, customVenn);

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

        return page;
    }

} // namespace UltraCanvas

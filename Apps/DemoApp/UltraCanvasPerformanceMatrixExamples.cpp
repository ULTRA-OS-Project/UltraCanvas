// Apps/DemoApp/UltraCanvasPerformanceMatrixExamples.cpp
// Performance matrix examples: the 9-box, performance vs potential tiles,
// the performance management matrix with header tabs (vivid and in one hue),
// talent review cards and skill / will
// Version: 1.0.0
// Last Modified: 2026-10-27
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasPerformanceMatrix.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreatePerformanceMatrixExamples() {
        const int PAD = 15;

        auto mainContainer = std::make_shared<UltraCanvasContainer>("pmMainContainer", 0, 0, 1140, 800);
        mainContainer->SetBackgroundColor(Color(248, 249, 250, 255));

        auto headerContainer = std::make_shared<UltraCanvasContainer>("pmHeader", 0, 0, 1140, 90);
        headerContainer->SetBackgroundColor(Color(255, 255, 255, 255));
        mainContainer->AddChild(headerContainer);

        auto mainTitle = std::make_shared<UltraCanvasLabel>("pmMainTitle", 30, 15, 1080, 35);
        mainTitle->SetText("Performance Matrix");
        mainTitle->SetFontSize(22);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainTitle->SetAlignment(TextAlignment::Left);
        mainTitle->SetTextColor(Color(33, 37, 41, 255));
        headerContainer->AddChild(mainTitle);

        auto subtitle = std::make_shared<UltraCanvasLabel>("pmSubtitle", 30, 52, 1080, 30);
        subtitle->SetText("Two rated axes cross into named categories: the 9-box, performance vs potential, "
                          "the performance management matrix, talent review cards, skill / will");
        subtitle->SetFontSize(13);
        subtitle->SetAlignment(TextAlignment::Left);
        subtitle->SetTextColor(Color(108, 117, 125, 255));
        headerContainer->AddChild(subtitle);

        auto contentContainer = std::make_shared<UltraCanvasContainer>("pmContent", 0, 90, 1140, 710);
        mainContainer->AddChild(contentContainer);

        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("pmTabs", 20, 20, 860, 670);
        tabbedContainer->SetTabPosition(TabPosition::Top);
        contentContainer->AddChild(tabbedContainer);

        // ===== THE EXAMPLES =====
        struct Example {
            std::string tab;
            PerformanceMatrixModel model;
            PerformanceMatrixDesign design;
            PerformanceCellColoring coloring;
            PerformanceMatrixPalette palette;
            bool panel;
        };
        PerformanceMatrixModel management = PerformanceMatrixSamples::PerformanceManagement();
        const char* columnIcons[] = {"rating-heart-on.svg", "chart.svg", "user-plus.svg"};
        const char* rowIcons[] = {"warning.png", "check.png", "arrow-up.svg"};
        for (size_t i = 0; i < 3; ++i) {
            management.xAxis.levels[i].iconPath = DemoIconPath(columnIcons[i]);
            management.yAxis.levels[i].iconPath = DemoIconPath(rowIcons[i]);
        }
        std::vector<Example> examples = {
            {"9-box", PerformanceMatrixSamples::NineBox(), PerformanceMatrixDesign::Grid,
             PerformanceCellColoring::DiagonalBands, PerformanceMatrixPalette::TrafficLight(), false},
            {"Performance vs potential", PerformanceMatrixSamples::PerformanceVsPotential(),
             PerformanceMatrixDesign::Tiles, PerformanceCellColoring::DiagonalBands,
             PerformanceMatrixPalette::TrafficLight(), false},
            {"Management", management, PerformanceMatrixDesign::HeaderTabs, PerformanceCellColoring::Plain,
             PerformanceMatrixPalette::Vivid(), false},
            {"One hue", management, PerformanceMatrixDesign::HeaderTabs, PerformanceCellColoring::Plain,
             PerformanceMatrixPalette::Monochrome(Color(36, 104, 178, 255)), true},
            {"Talent review", PerformanceMatrixSamples::TalentReview(), PerformanceMatrixDesign::Cards,
             PerformanceCellColoring::ByRow, PerformanceMatrixPalette::Pastel(), false},
            {"Skill / will", PerformanceMatrixSamples::SkillWill(), PerformanceMatrixDesign::Grid,
             PerformanceCellColoring::DiagonalBands, PerformanceMatrixPalette::TrafficLight(), false},
        };

        std::vector<std::shared_ptr<UltraCanvasPerformanceMatrix>> matrices;
        for (size_t i = 0; i < examples.size(); ++i) {
            const Example& example = examples[i];
            auto matrix = CreatePerformanceMatrix("pmMatrix" + std::to_string(i), 10, 10, 840, 612);
            matrix->SetModel(example.model);
            matrix->SetDesign(example.design);
            matrix->SetCellColoring(example.coloring);
            matrix->SetPalette(example.palette);
            matrix->SetShowContainerPanel(example.panel);

            auto tabContent = std::make_shared<UltraCanvasContainer>("pmTab" + std::to_string(i), 0, 0, 860, 632);
            tabContent->SetBackgroundColor(Color(255, 255, 255, 255));
            tabContent->AddChild(matrix);
            tabbedContainer->AddTab(example.tab, tabContent);
            matrices.push_back(matrix);
        }

        // ===== SIDEBAR =====
        auto sidebar = std::make_shared<UltraCanvasContainer>("pmSidebar", 900, 20, 220, 670);
        sidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        contentContainer->AddChild(sidebar);
        const int SIDEBAR_W = 190;

        auto addCaption = [&](const std::string& id, const std::string& text, int y) {
            auto caption = std::make_shared<UltraCanvasLabel>(id, PAD, y, SIDEBAR_W, 20);
            caption->SetText(text);
            caption->SetFontSize(10);
            caption->SetFontWeight(FontWeight::Bold);
            caption->SetTextColor(Color(73, 80, 87, 255));
            caption->SetAlignment(TextAlignment::Left);
            sidebar->AddChild(caption);
        };

        addCaption("pmDesignCaption", "Design (this tab)", PAD);
        auto designSelector = std::make_shared<UltraCanvasDropdown>("pmDesignSelector", PAD, PAD + 23, SIDEBAR_W, 28);
        designSelector->AddItem("Grid", "grid");
        designSelector->AddItem("Tiles", "tiles");
        designSelector->AddItem("Header tabs", "tabs");
        designSelector->AddItem("Cards", "cards");
        designSelector->SetSelectedIndex(0);
        sidebar->AddChild(designSelector);

        addCaption("pmColoringCaption", "Cell colours (this tab)", PAD + 63);
        auto coloringSelector = std::make_shared<UltraCanvasDropdown>("pmColoringSelector", PAD, PAD + 86, SIDEBAR_W, 28);
        coloringSelector->AddItem("By rank from the best corner", "rank");
        coloringSelector->AddItem("By row", "row");
        coloringSelector->AddItem("By column", "column");
        coloringSelector->AddItem("Plain", "plain");
        coloringSelector->SetSelectedIndex(0);
        sidebar->AddChild(coloringSelector);

        addCaption("pmPaletteCaption", "Palette (this tab)", PAD + 126);
        auto paletteSelector = std::make_shared<UltraCanvasDropdown>("pmPaletteSelector", PAD, PAD + 149, SIDEBAR_W, 28);
        paletteSelector->AddItem("Traffic light", "traffic");
        paletteSelector->AddItem("Vivid", "vivid");
        paletteSelector->AddItem("One hue (blue)", "mono");
        paletteSelector->AddItem("Pastel", "pastel");
        paletteSelector->SetSelectedIndex(0);
        sidebar->AddChild(paletteSelector);

        addCaption("pmSelectionCaption", "SELECTED CELL", PAD + 196);
        auto selection = std::make_shared<UltraCanvasLabel>("pmSelection", PAD, PAD + 220, SIDEBAR_W, 250);
        selection->SetFontSize(10);
        selection->SetAlignment(TextAlignment::Left);
        selection->SetBackgroundColor(Color(248, 249, 250, 255));
        selection->SetPadding(10);
        selection->SetText("Click a cell. Hover for its full text: a cell too small for its "
                           "content keeps the rest in its tooltip.");
        sidebar->AddChild(selection);

        // ===== CALLBACKS (raw pointers: no cycle through the elements) =====
        std::vector<UltraCanvasPerformanceMatrix*> matrixPtrs;
        for (auto& matrix : matrices) matrixPtrs.push_back(matrix.get());
        UltraCanvasLabel* selectionPtr = selection.get();
        UltraCanvasTabbedContainer* tabsPtr = tabbedContainer.get();
        auto active = [matrixPtrs, tabsPtr]() -> UltraCanvasPerformanceMatrix* {
            const int index = tabsPtr->GetActiveTab();
            return (index >= 0 && index < static_cast<int>(matrixPtrs.size())) ? matrixPtrs[static_cast<size_t>(index)]
                                                                               : nullptr;
        };

        for (size_t i = 0; i < matrixPtrs.size(); ++i) {
            matrices[i]->onCellClick = [matrixPtrs, i, selectionPtr](const PerformanceCellRef& ref) {
                const UltraCanvasPerformanceMatrix* clicked = matrixPtrs[i];
                selectionPtr->SetText(clicked->GetSelectedCell().IsValid() ? clicked->BuildCellTooltip(ref)
                                                                           : std::string("(nothing selected)"));
            };
        }
        designSelector->onSelectionChanged = [active](int, const DropdownItem& item) {
            UltraCanvasPerformanceMatrix* matrix = active();
            if (!matrix) return;
            if (item.value == "tiles") matrix->SetDesign(PerformanceMatrixDesign::Tiles);
            else if (item.value == "tabs") matrix->SetDesign(PerformanceMatrixDesign::HeaderTabs);
            else if (item.value == "cards") matrix->SetDesign(PerformanceMatrixDesign::Cards);
            else matrix->SetDesign(PerformanceMatrixDesign::Grid);
        };
        coloringSelector->onSelectionChanged = [active](int, const DropdownItem& item) {
            UltraCanvasPerformanceMatrix* matrix = active();
            if (!matrix) return;
            if (item.value == "row") matrix->SetCellColoring(PerformanceCellColoring::ByRow);
            else if (item.value == "column") matrix->SetCellColoring(PerformanceCellColoring::ByColumn);
            else if (item.value == "plain") matrix->SetCellColoring(PerformanceCellColoring::Plain);
            else matrix->SetCellColoring(PerformanceCellColoring::DiagonalBands);
        };
        paletteSelector->onSelectionChanged = [active](int, const DropdownItem& item) {
            UltraCanvasPerformanceMatrix* matrix = active();
            if (!matrix) return;
            if (item.value == "vivid") matrix->SetPalette(PerformanceMatrixPalette::Vivid());
            else if (item.value == "mono") matrix->SetPalette(PerformanceMatrixPalette::Monochrome(Color(36, 104, 178, 255)));
            else if (item.value == "pastel") matrix->SetPalette(PerformanceMatrixPalette::Pastel());
            else matrix->SetPalette(PerformanceMatrixPalette::TrafficLight());
        };

        return mainContainer;
    }

} // namespace UltraCanvas

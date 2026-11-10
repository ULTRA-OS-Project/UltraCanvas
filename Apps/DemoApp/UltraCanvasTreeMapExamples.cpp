// Apps/DemoApp/UltraCanvasTreeMapExamples.cpp
// TreeMap example implementations
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the tree map
//                  and its controls in a card, buttons as wide as their text
// Version: 1.0.0
// Last Modified: 2026-10-23

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Diagrams/UltraCanvasTreeMapElement.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSwitch.h"

namespace UltraCanvas {

    namespace {
        // A button as wide as its text, at least the 160 px it had.
        std::shared_ptr<UltraCanvasButton> MakeTreeMapButton(const std::string& id, const std::string& text) {
            auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, 30, text);
            CSSLayout::BoxConstraints limits;
            limits.minWidth = CSSLayout::Dimension::Px(160);
            button->boxConstraints = limits;
            return button;
        }
    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTreeMapExamples() {
        auto container = DemoKit::CreatePage("treeMapContainer", Tr("TreeMap - Hierarchical Data Example"),
                Tr("Sales by category and product, drawn as nested rectangles sized by value"));
        auto card = DemoKit::AddCard(container, "treeMapCard", Tr("Sales tree map"),
                Tr("Cycle the layout algorithm, the visual style and the colour scheme; toggle values and labels"));
        
        auto treeMap = std::make_shared<UltraCanvasTreeMapElement>("treeMap", 0, 0, 600, 400);
        
        std::vector<std::tuple<std::string, std::string, double, double>> data = {
            {Tr("Technology"), Tr("Computers"), 45000, 500},
            {Tr("Technology"), Tr("Phones"), 32000, 400},
            {Tr("Technology"), Tr("Tablets"), 18000, 200},
            {Tr("Food"), Tr("Fruits"), 25000, 300},
            {Tr("Food"), Tr("Vegetables"), 18000, 250},
            {Tr("Food"), Tr("Snacks"), 12000, 150},
            {Tr("Clothing"), Tr("Men"), 22000, 280},
            {Tr("Clothing"), Tr("Women"), 28000, 350},
            {Tr("Clothing"), Tr("Kids"), 12000, 150},
            {Tr("Electronics"), Tr("TVs"), 35000, 450},
            {Tr("Electronics"), Tr("Audio"), 22000, 280},
            {Tr("Electronics"), Tr("Gaming"), 28000, 360}
        };
        
        treeMap->SetDataFromHierarchy(data);
        treeMap->SetColorScheme(TreeMapColorScheme::Categorical);
        
        DemoKit::AddFixed(card, treeMap);
        
        auto buttonRow = DemoKit::AddRow(card, "treeMapButtons", 10);
        auto btnCycleAlgorithm = MakeTreeMapButton("btnCycleAlgorithm", Tr("Cycle Algorithm"));
        DemoKit::Add(buttonRow, btnCycleAlgorithm);
        
        auto btnCycleStyle = MakeTreeMapButton("btnCycleStyle", Tr("Cycle Style"));
        DemoKit::Add(buttonRow, btnCycleStyle);
        
        auto btnCycleColor = MakeTreeMapButton("btnCycleColor", Tr("Cycle Colors"));
        DemoKit::Add(buttonRow, btnCycleColor);
        
        auto btnReset = MakeTreeMapButton("btnReset", Tr("Reset"));
        DemoKit::Add(buttonRow, btnReset);

        auto switchRow = DemoKit::AddRow(card, "treeMapSwitches", 24);
        
        static int currentAlgorithm = 0;
        static int currentStyle = 0;
        static int currentColor = 0;
        static bool showValues = true;
        static bool showLabels = true;
        
        std::vector<TreeMapAlgorithm> algorithms = {
            TreeMapAlgorithm::Squarified,
            TreeMapAlgorithm::SliceAndDice,
            TreeMapAlgorithm::Strip,
            TreeMapAlgorithm::Binary,
            TreeMapAlgorithm::Spiral
        };
        
        std::vector<TreeMapStyle> styles = {
            TreeMapStyle::Flat,
            TreeMapStyle::Raised,
            TreeMapStyle::Sunken,
            TreeMapStyle::Gradient,
            TreeMapStyle::Minimal
        };
        
        std::vector<TreeMapColorScheme> colorSchemes = {
            TreeMapColorScheme::Categorical,
            TreeMapColorScheme::Sequential,
            TreeMapColorScheme::Diverging,
            TreeMapColorScheme::Monochrome
        };
        
        btnCycleAlgorithm->SetOnClick([treeMap, algorithms]() {
            currentAlgorithm = (currentAlgorithm + 1) % algorithms.size();
            treeMap->SetLayoutAlgorithm(algorithms[currentAlgorithm]);
        });

        btnCycleStyle->SetOnClick([treeMap, styles]() {
            currentStyle = (currentStyle + 1) % styles.size();
            treeMap->SetVisualStyle(styles[currentStyle]);
        });

        btnCycleColor->SetOnClick([treeMap, colorSchemes]() {
            currentColor = (currentColor + 1) % colorSchemes.size();
            treeMap->SetColorScheme(colorSchemes[currentColor]);
        });

        treeMap->SetDisplayOptions(showLabels, showValues, false, true);

        auto swValues = UltraCanvasSwitch::Create("swToggleValues", 0, 0, Tr("Values"), showValues);
        swValues->onStateChanged = [treeMap](CheckedState, CheckedState newState) {
            showValues = (newState == CheckedState::Checked);
            treeMap->SetDisplayOptions(showLabels, showValues, false, true);
        };
        DemoKit::Add(switchRow, swValues);

        auto swLabels = UltraCanvasSwitch::Create("swToggleLabels", 0, 0, Tr("Labels"), showLabels);
        swLabels->onStateChanged = [treeMap](CheckedState, CheckedState newState) {
            showLabels = (newState == CheckedState::Checked);
            treeMap->SetDisplayOptions(showLabels, showValues, false, true);
        };
        DemoKit::Add(switchRow, swLabels);

        btnReset->SetOnClick([treeMap, swValues, swLabels]() {
            currentAlgorithm = 0;
            currentStyle = 0;
            currentColor = 0;
            showValues = true;
            showLabels = true;
            swValues->SetChecked(true);
            swLabels->SetChecked(true);
            
            std::vector<std::tuple<std::string, std::string, double, double>> data = {
                {Tr("Technology"), Tr("Computers"), 45000, 500},
                {Tr("Technology"), Tr("Phones"), 32000, 400},
                {Tr("Technology"), Tr("Tablets"), 18000, 200},
                {Tr("Food"), Tr("Fruits"), 25000, 300},
                {Tr("Food"), Tr("Vegetables"), 18000, 250},
                {Tr("Food"), Tr("Snacks"), 12000, 150},
                {Tr("Clothing"), Tr("Men"), 22000, 280},
                {Tr("Clothing"), Tr("Women"), 28000, 350},
                {Tr("Clothing"), Tr("Kids"), 12000, 150},
                {Tr("Electronics"), Tr("TVs"), 35000, 450},
                {Tr("Electronics"), Tr("Audio"), 22000, 280},
                {Tr("Electronics"), Tr("Gaming"), 28000, 360}
            };
            
            treeMap->SetDataFromHierarchy(data);
            treeMap->SetLayoutAlgorithm(TreeMapAlgorithm::Squarified);
            treeMap->SetVisualStyle(TreeMapStyle::Flat);
            treeMap->SetColorScheme(TreeMapColorScheme::Categorical);
            treeMap->SetDisplayOptions(true, true, false, true);
        });
        
        return container;
    }

} // namespace UltraCanvas

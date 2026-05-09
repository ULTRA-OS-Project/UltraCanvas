// Apps/DemoApp/UltraCanvasTreeMapExamples.cpp
// TreeMap example implementations
// Version: 1.0.0
// Last Modified: 2025-04-02

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasTreeMapElement.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTreeMapExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("treeMapContainer", 1100, 0, 0, 800, 700);
        
        auto treeMap = std::make_shared<UltraCanvasTreeMapElement>("treeMap", 1101, 100, 120, 600, 400);
        
        std::vector<std::tuple<std::string, std::string, double, double>> data = {
            {"Technology", "Computers", 45000, 500},
            {"Technology", "Phones", 32000, 400},
            {"Technology", "Tablets", 18000, 200},
            {"Food", "Fruits", 25000, 300},
            {"Food", "Vegetables", 18000, 250},
            {"Food", "Snacks", 12000, 150},
            {"Clothing", "Men", 22000, 280},
            {"Clothing", "Women", 28000, 350},
            {"Clothing", "Kids", 12000, 150},
            {"Electronics", "TVs", 35000, 450},
            {"Electronics", "Audio", 22000, 280},
            {"Electronics", "Gaming", 28000, 360}
        };
        
        treeMap->SetDataFromHierarchy(data);
        treeMap->SetColorScheme(TreeMapColorScheme::Categorical);
        
        container->AddChild(treeMap);
        
        auto titleLabel = std::make_shared<UltraCanvasLabel>("treeMapTitle", 1102, 50, 30, 700, 30);
        titleLabel->SetText("TreeMap - Hierarchical Data Example");
        titleLabel->SetFontSize(18);
        container->AddChild(titleLabel);
        
        auto btnCycleAlgorithm = std::make_shared<UltraCanvasButton>("btnCycleAlgorithm", 1103, 50, 530, 140, 30);
        btnCycleAlgorithm->SetText("Cycle Algorithm");
        container->AddChild(btnCycleAlgorithm);
        
        auto btnCycleStyle = std::make_shared<UltraCanvasButton>("btnCycleStyle", 1104, 200, 530, 140, 30);
        btnCycleStyle->SetText("Cycle Style");
        container->AddChild(btnCycleStyle);
        
        auto btnCycleColor = std::make_shared<UltraCanvasButton>("btnCycleColor", 1105, 350, 530, 140, 30);
        btnCycleColor->SetText("Cycle Colors");
        container->AddChild(btnCycleColor);
        
        auto btnToggleValues = std::make_shared<UltraCanvasButton>("btnToggleValues", 1106, 50, 570, 140, 30);
        btnToggleValues->SetText("Toggle Values");
        container->AddChild(btnToggleValues);
        
        auto btnToggleLabels = std::make_shared<UltraCanvasButton>("btnToggleLabels", 1107, 200, 570, 140, 30);
        btnToggleLabels->SetText("Toggle Labels");
        container->AddChild(btnToggleLabels);
        
        auto btnReset = std::make_shared<UltraCanvasButton>("btnReset", 1108, 350, 570, 140, 30);
        btnReset->SetText("Reset");
        container->AddChild(btnReset);
        
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

        btnToggleValues->SetOnClick([treeMap]() {
            showValues = !showValues;
            treeMap->SetDisplayOptions(showLabels, showValues, false, true);
        });

        btnToggleLabels->SetOnClick([treeMap]() {
            showLabels = !showLabels;
            treeMap->SetDisplayOptions(showLabels, showValues, false, true);
        });

        btnReset->SetOnClick([treeMap]() {
            currentAlgorithm = 0;
            currentStyle = 0;
            currentColor = 0;
            showValues = true;
            showLabels = true;
            
            std::vector<std::tuple<std::string, std::string, double, double>> data = {
                {"Technology", "Computers", 45000, 500},
                {"Technology", "Phones", 32000, 400},
                {"Technology", "Tablets", 18000, 200},
                {"Food", "Fruits", 25000, 300},
                {"Food", "Vegetables", 18000, 250},
                {"Food", "Snacks", 12000, 150},
                {"Clothing", "Men", 22000, 280},
                {"Clothing", "Women", 28000, 350},
                {"Clothing", "Kids", 12000, 150},
                {"Electronics", "TVs", 35000, 450},
                {"Electronics", "Audio", 22000, 280},
                {"Electronics", "Gaming", 28000, 360}
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

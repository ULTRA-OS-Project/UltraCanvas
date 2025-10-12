// Apps/DemoApp/UltraCanvasDropDownExamples.cpp
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
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDropdownExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("DropdownExamples", 300, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("DropdownTitle", 301, 10, 10, 300, 30);
        title->SetText("Dropdown/ComboBox Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Simple Dropdown
        auto simpleDropdown = std::make_shared<UltraCanvasDropdown>("SimpleDropdown", 302, 20, 50, 200, 30);
        simpleDropdown->AddItem("Option 1");
        simpleDropdown->AddItem("Option 2");
        simpleDropdown->AddItem("Option 3");
        simpleDropdown->AddItem("Very Long Option Text");
        simpleDropdown->SetSelectedIndex(0);
        container->AddChild(simpleDropdown);

        auto simpleLabel = std::make_shared<UltraCanvasLabel>("SimpleLabel", 303, 20, 85, 200, 20);
        simpleLabel->SetText("Simple Dropdown");
        simpleLabel->SetFontSize(12);
        container->AddChild(simpleLabel);

        // Editable ComboBox
//        auto editableCombo = std::make_shared<UltraCanvasDropdown>("EditableCombo", 304, 250, 50, 200, 30);
//        editableCombo->SetEditable(true);
//        editableCombo->AddItem("Predefined Item 1");
//        editableCombo->AddItem("Predefined Item 2");
//        editableCombo->SetText("Type or select...");
//        container->AddChild(editableCombo);
//
//        auto editableLabel = std::make_shared<UltraCanvasLabel>("EditableLabel", 305, 250, 85, 200, 20);
//        editableLabel->SetText("Editable ComboBox");
//        editableLabel->SetFontSize(12);
//        container->AddChild(editableLabel);

        // Multi-Select Dropdown
//        auto multiSelect = std::make_shared<UltraCanvasDropdown>("MultiSelect", 306, 480, 50, 200, 30);
//        multiSelect->SetMultiSelect(true);
//        multiSelect->AddItem("Apple");
//        multiSelect->AddItem("Banana");
//        multiSelect->AddItem("Cherry");
//        multiSelect->AddItem("Date");
//        multiSelect->AddItem("Elderberry");
//        container->AddChild(multiSelect);

//        auto multiLabel = std::make_shared<UltraCanvasLabel>("MultiLabel", 307, 480, 85, 200, 20);
//        multiLabel->SetText("Multi-Select Dropdown");
//        multiLabel->SetFontSize(12);
//        container->AddChild(multiLabel);

        return container;
    }
} // namespace UltraCanvas
// Apps/DemoApp/UltraCanvasTextInputExamples.cpp
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


    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTextInputExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TextInputExamples", 200, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("TextInputTitle", 201, 10, 10, 300, 30);
        title->SetText("Text Input Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Single Line Input
        auto singleLineInput = std::make_shared<UltraCanvasTextInput>("SingleLineInput", 202, 20, 50, 300, 30);
        singleLineInput->SetPlaceholder("Enter single line text...");
        singleLineInput->SetMaxLength(100);
        container->AddChild(singleLineInput);

        auto singleLineLabel = std::make_shared<UltraCanvasLabel>("SingleLineLabel", 203, 20, 85, 200, 20);
        singleLineLabel->SetText("Single Line Input");
        singleLineLabel->SetFontSize(12);
        container->AddChild(singleLineLabel);

        // Multi-line Text Area
        auto multiLineInput = std::make_shared<UltraCanvasTextInput>("MultiLineInput", 204, 20, 120, 400, 100);
        multiLineInput->SetInputType(TextInputType::Multiline);
        multiLineInput->SetPlaceholder("Enter multi-line text...\nSupports line breaks.");
//        multiLineInput->SetWordWrap(true);
        container->AddChild(multiLineInput);

        auto multiLineLabel = std::make_shared<UltraCanvasLabel>("MultiLineLabel", 205, 20, 225, 200, 20);
        multiLineLabel->SetText("Multi-line Text Input");
        multiLineLabel->SetFontSize(12);
        container->AddChild(multiLineLabel);

        // Password Field
        auto passwordInput = std::make_shared<UltraCanvasTextInput>("PasswordInput", 206, 450, 50, 300, 30);
        passwordInput->SetInputType(TextInputType::Password);
        passwordInput->SetPlaceholder("Enter password...");
        container->AddChild(passwordInput);

        auto passwordLabel = std::make_shared<UltraCanvasLabel>("PasswordLabel", 207, 450, 85, 200, 20);
        passwordLabel->SetText("Password Field");
        passwordLabel->SetFontSize(12);
        container->AddChild(passwordLabel);

        // Numeric Input
        auto numericInput = std::make_shared<UltraCanvasTextInput>("NumericInput", 208, 450, 120, 200, 30);
        numericInput->SetInputType(TextInputType::Number);
        numericInput->SetPlaceholder("0.00");
//        numericInput->SetMinValue(0.0);
//        numericInput->SetMaxValue(1000.0);
        container->AddChild(numericInput);

        auto numericLabel = std::make_shared<UltraCanvasLabel>("NumericLabel", 209, 450, 155, 200, 20);
        numericLabel->SetText("Numeric Input (0-1000)");
        numericLabel->SetFontSize(12);
        container->AddChild(numericLabel);

        auto textAreaInput = std::make_shared<UltraCanvasTextArea>("TextArea", 204, 20, 270, 400, 100);
//        textAreaInput->SetPlaceholder("Enter multi-line text...\nSupports line breaks.");
//        multiLineInput->SetWordWrap(true);
        container->AddChild(textAreaInput);

        auto textAreaLabel = std::make_shared<UltraCanvasLabel>("TextAreaLabel", 205, 20, 375, 200, 20);
        textAreaLabel->SetText("Text Area");
        textAreaLabel->SetFontSize(12);
        container->AddChild(textAreaLabel);

        return container;
    }
} // namespace UltraCanvas
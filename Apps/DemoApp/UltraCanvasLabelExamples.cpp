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
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateLabelExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("LabelExamples", 500, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("LabelTitle", 501, 10, 10, 300, 30);
        title->SetText("Label Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Basic Label
        auto basicLabel = std::make_shared<UltraCanvasLabel>("BasicLabel", 502, 20, 50, 400, 25);
        basicLabel->SetText("This is a basic label with default styling.");
        container->AddChild(basicLabel);

        // Header Text
        auto headerLabel = std::make_shared<UltraCanvasLabel>("HeaderLabel", 503, 20, 90, 500, 35);
        headerLabel->SetText("Header Label Example");
        headerLabel->SetFontSize(24);
        headerLabel->SetFontWeight(FontWeight::Bold);
        headerLabel->SetTextColor(Color(0, 100, 200, 255));
        container->AddChild(headerLabel);

        // Status Labels
        auto successLabel = std::make_shared<UltraCanvasLabel>("SuccessLabel", 504, 20, 140, 150, 25);
        successLabel->SetText("✓ Success");
        successLabel->SetBackgroundColor(Color(200, 255, 200, 255));
        successLabel->SetTextColor(Color(0, 150, 0, 255));
        successLabel->SetAlignment(TextAlignment::Center);
        container->AddChild(successLabel);

        auto warningLabel = std::make_shared<UltraCanvasLabel>("WarningLabel", 505, 180, 140, 150, 25);
        warningLabel->SetText("⚠ Warning");
        warningLabel->SetBackgroundColor(Color(255, 255, 200, 255));
        warningLabel->SetTextColor(Color(200, 150, 0, 255));
        warningLabel->SetAlignment(TextAlignment::Center);
        container->AddChild(warningLabel);

        auto errorLabel = std::make_shared<UltraCanvasLabel>("ErrorLabel", 506, 340, 140, 150, 25);
        errorLabel->SetText("✗ Error");
        errorLabel->SetBackgroundColor(Color(255, 200, 200, 255));
        errorLabel->SetTextColor(Color(200, 0, 0, 255));
        errorLabel->SetAlignment(TextAlignment::Center);
        container->AddChild(errorLabel);

        // Multi-line Label
        auto multiLabel = std::make_shared<UltraCanvasLabel>("MultiLabel", 507, 20, 190, 450, 80);
        multiLabel->SetText("This is a multi-line label that demonstrates\nhow text wrapping works with longer content.\nIt supports multiple lines and proper alignment.");
        multiLabel->SetWordWrap(true);
        multiLabel->SetAlignment(TextAlignment::Left);
        multiLabel->SetBackgroundColor(Color(245, 245, 245, 255));
//        multiLabel->SetBorderStyle(BorderStyle::Solid);
        multiLabel->SetBorders(1.0f);
        multiLabel->SetPadding(10.0f);
        container->AddChild(multiLabel);

        return container;
    }

} // namespace UltraCanvas
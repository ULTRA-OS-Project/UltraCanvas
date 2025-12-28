// Apps/DemoApp/UltraCanvasTextInputExamples.cpp
// Implementation of all component example creators
// Version: 1.1.0
// Last Modified: 2025-10-21
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasPasswordStrengthMeter.h"
#include "UltraCanvasPasswordRuleLegend.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextArea.h"
#include <sstream>

namespace UltraCanvas {

// ===== BASIC UI ELEMENTS =====

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTextInputExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TextInputExamples", 200, 0, 0, 900, 1000);
        container->SetPadding(0,0,10,0);
        // Title
        auto title = std::make_shared<UltraCanvasLabel>("TextInputTitle", 201, 10, 10, 300, 30);
        title->SetText("Text Input Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // ===== COLUMN 1: BASIC INPUTS =====

        auto singleLineLabel = std::make_shared<UltraCanvasLabel>("SingleLineLabel", 203, 20, 45, 200, 20);
        singleLineLabel->SetText("Single Line Input");
        singleLineLabel->SetFontSize(12);
        container->AddChild(singleLineLabel);

        // Single Line Input
        auto singleLineInput = std::make_shared<UltraCanvasTextInput>("SingleLineInput", 202, 20, 70, 300, 30);
        singleLineInput->SetPlaceholder("Enter single line text...");
        singleLineInput->SetMaxLength(100);
        container->AddChild(singleLineInput);


        // Multi-line Text Area
        auto multiLineLabel = std::make_shared<UltraCanvasLabel>("MultiLineLabel", 205, 20, 130, 200, 20);
        multiLineLabel->SetText("Multi-line Text Input");
        multiLineLabel->SetFontSize(12);
        container->AddChild(multiLineLabel);

        auto multiLineInput = std::make_shared<UltraCanvasTextInput>("MultiLineInput", 204, 20, 155, 300, 100);
        multiLineInput->SetInputType(TextInputType::Multiline);
        multiLineInput->SetPlaceholder("Enter multi-line text...\nSupports line breaks.");
        container->AddChild(multiLineInput);


        auto passwordLabel = std::make_shared<UltraCanvasLabel>("PasswordLabel", 207, 20, 280, 200, 20);
        passwordLabel->SetText("Basic Password Field");
        passwordLabel->SetFontSize(12);
        container->AddChild(passwordLabel);

        // Password Field (Basic)
        auto passwordInput = std::make_shared<UltraCanvasTextInput>("PasswordInput", 206, 20, 305, 300, 30);
        passwordInput->SetInputType(TextInputType::Password);
        passwordInput->SetPlaceholder("Enter password...");
        container->AddChild(passwordInput);


        auto numericLabel = std::make_shared<UltraCanvasLabel>("NumericLabel", 209, 20, 360, 200, 20);
        numericLabel->SetText("Numeric Input (0-1000)");
        numericLabel->SetFontSize(12);
        container->AddChild(numericLabel);

        // Numeric Input
        auto numericInput = std::make_shared<UltraCanvasTextInput>("NumericInput", 208, 20, 385, 300, 30);
        numericInput->SetInputType(TextInputType::Number);
        numericInput->SetPlaceholder("0.00");
        container->AddChild(numericInput);


        auto textAreaLabel = std::make_shared<UltraCanvasLabel>("TextAreaLabel", 211, 20, 440, 200, 20);
        textAreaLabel->SetText("Text Area Component");
        textAreaLabel->SetFontSize(12);
        container->AddChild(textAreaLabel);

        // Text Area Component
        auto textAreaInput = std::make_shared<UltraCanvasTextArea>("TextArea", 210, 20, 465, 300, 100);
        container->AddChild(textAreaInput);


        // ===== PASSWORD WITH BAR STRENGTH METER =====

        auto passwordTitle1 = std::make_shared<UltraCanvasLabel>("PasswordTitle1", 212, 350, 45, 350, 20);
        passwordTitle1->SetText("Password with Bar Strength Meter");
        passwordTitle1->SetFontSize(12);
        //passwordTitle1->SetFontWeight(FontWeight::Bold);
        container->AddChild(passwordTitle1);

        auto passwordInput1 = CreatePasswordInput("Password1", 213, 350, 70, 350, 30);
        passwordInput1->SetPlaceholder("Enter password...");

        // Add validation rules
        passwordInput1->AddValidationRule(ValidationRule::MinLength(8));
        passwordInput1->AddValidationRule(ValidationRule::RequireUppercase());
        passwordInput1->AddValidationRule(ValidationRule::RequireLowercase());
        passwordInput1->AddValidationRule(ValidationRule::RequireDigit());
        passwordInput1->AddValidationRule(ValidationRule::RequireSpecialChar());

        container->AddChild(passwordInput1);

        // Bar Strength Meter
        auto strengthBar = CreateBarStrengthMeter("StrengthBar", 214, 350, 100, 350, 20);
        strengthBar->LinkToInput(passwordInput1.get());
        strengthBar->SetShowLabel(true);
        strengthBar->SetShowPercentage(true);
        container->AddChild(strengthBar);

        // Description
        auto barDescription = std::make_shared<UltraCanvasLabel>("BarDesc", 215, 350, 140, 350, 40);
        barDescription->SetText("Real-time strength indicator with animated\ncolor transitions (red → yellow → green)");
        barDescription->SetFontSize(11);
        barDescription->SetTextColor(Color(100, 100, 100));
        container->AddChild(barDescription);

        // ===== PASSWORD WITH SEGMENTED METER =====

//        auto passwordTitle2 = std::make_shared<UltraCanvasLabel>("PasswordTitle2", 216, 350, 200, 350, 25);passwordTitle2->SetText("Password with Segmented Meter");
//        passwordTitle2->SetFontSize(14);
//        //passwordTitle2->SetFontWeight(FontWeight::Bold);
//        container->AddChild(passwordTitle2);
//
//        auto passwordInput2 = CreatePasswordInput("Password2", 217, 350, 230, 350, 30);
//        passwordInput2->SetPlaceholder("Try a strong password...");
//        container->AddChild(passwordInput2);
//
//        // Segmented Strength Meter
//        auto strengthSegments = CreateSegmentedStrengthMeter("StrengthSegments", 218, 350, 270, 350, 15);
//        strengthSegments->LinkToInput(passwordInput2.get());
//        strengthSegments->SetShowLabel(true);
//
//        StrengthMeterConfig segmentConfig;
//        segmentConfig.segmentCount = 5;
//        segmentConfig.animateTransitions = true;
//        strengthSegments->SetConfig(segmentConfig);
//
//        container->AddChild(strengthSegments);
//
//        // Description
//        auto segmentDescription = std::make_shared<UltraCanvasLabel>("SegmentDesc", 219, 350, 310, 350, 40);
//        segmentDescription->SetText("Gaming-style segmented display with\n5 colored strength indicators");
//        segmentDescription->SetFontSize(11);
//        segmentDescription->SetTextColor(Color(100, 100, 100));
//        container->AddChild(segmentDescription);

        // ===== PASSWORD WITH CHECKLIST =====

        auto passwordTitle3 = std::make_shared<UltraCanvasLabel>("PasswordTitle3", 220, 350, 200, 350, 20);
        passwordTitle3->SetText("Password with Requirements Checklist");
//        passwordTitle3->SetFontSize(14);
//        passwordTitle3->SetFontWeight(FontWeight::Bold);
        container->AddChild(passwordTitle3);

        auto passwordInput3 = CreatePasswordInput("Password3", 221, 350, 230, 350, 30);
        passwordInput3->SetPlaceholder("Meet all requirements...");
        container->AddChild(passwordInput3);

        // Checklist Legend
        auto ruleLegend = CreateChecklistLegend("RuleLegend", 222, 350, 260, 350, 140);
        ruleLegend->LinkToInput(passwordInput3.get());
        ruleLegend->SetShowMetRules(true);

        // Setup callbacks for status updates
        ruleLegend->onAllRulesMet = [](bool allMet) {
            if (allMet) {
                std::cout << "✓ All password requirements met!" << std::endl;
            }
        };

        ruleLegend->onRuleStatusChanged = [](int met, int total) {
            std::cout << "Password rules: " << met << "/" << total << " met" << std::endl;
        };

        container->AddChild(ruleLegend);

        // Description
        auto checklistDescription = std::make_shared<UltraCanvasLabel>("ChecklistDesc", 223, 350, 410, 350, 40);
        checklistDescription->SetText("Interactive checklist with ✓/✗ indicators\nshowing real-time validation status");
        checklistDescription->SetFontSize(11);
        checklistDescription->SetTextColor(Color(100, 100, 100));
        container->AddChild(checklistDescription);

        // ===== COMPLETE PASSWORD SETUP =====

        auto passwordTitle4 = std::make_shared<UltraCanvasLabel>("PasswordTitle4", 224, 350, 480, 450, 25);
        passwordTitle4->SetText("Complete Setup: Circular Meter + Detailed Legend");
//        passwordTitle4->SetFontSize(14);
        //passwordTitle4->SetFontWeight(FontWeight::Bold);
        container->AddChild(passwordTitle4);

        auto passwordInput4 = CreatePasswordInput("Password4", 225, 350, 530, 350, 30);
        passwordInput4->SetPlaceholder("Create strong password...");
        container->AddChild(passwordInput4);

        // Circular Strength Meter
        auto circularMeter = CreateCircularStrengthMeter("CircularMeter", 226, 710, 510, 70);
        circularMeter->LinkToInput(passwordInput4.get());

        StrengthMeterConfig circularConfig;
        circularConfig.style = StrengthMeterStyle::Circular;
        circularConfig.showPercentage = true;
        circularMeter->SetConfig(circularConfig);

        container->AddChild(circularMeter);

        // Detailed Legend with Strict Rules
        auto detailedLegend = CreatePasswordRuleLegend("DetailedLegend", 227,
                                                       350, 610, 435, 280,
                                                       LegendStyle::Detailed);
        detailedLegend->LinkToInput(passwordInput4.get());
        detailedLegend->SetupStrictRules();  // Use strict validation rules

        PasswordRuleLegendConfig legendConfig;
        legendConfig.style = LegendStyle::Detailed;
        //legendConfig.animateChanges = true;
        legendConfig.showMetRules = true;
        detailedLegend->SetConfig(legendConfig);

        // Add callbacks for strength updates
        circularMeter->onStrengthChanged = [](float strength) {
            std::cout << "Password strength: " << strength << "%" << std::endl;
        };

        circularMeter->onStrengthLevelChanged = [](const std::string& level) {
            std::cout << "Strength level: " << level << std::endl;
        };

        container->AddChild(detailedLegend);

        // Description
        auto completeDescription = std::make_shared<UltraCanvasLabel>("CompleteDesc", 228, 350, 910, 435, 60);
        completeDescription->SetText("Professional registration form setup with:\n"
                                     "• Circular strength meter with percentage\n"
                                     "• Detailed rule legend with backgrounds\n"
                                     "• Strict validation (12+ chars, no patterns)");
        completeDescription->SetFontSize(11);
        completeDescription->SetTextColor(Color(100, 100, 100));
        container->AddChild(completeDescription);

//        // ===== COMPACT LEGEND EXAMPLE =====
//
//        auto compactTitle = std::make_shared<UltraCanvasLabel>("CompactTitle", 229, 730, 520, 350, 25);
//        compactTitle->SetText("Compact Legend Display");
//        compactTitle->SetFontSize(14);
//        compactTitle->SetFontWeight(FontWeight::Bold);
//        container->AddChild(compactTitle);
//
//        auto passwordInput5 = CreatePasswordInput("Password5", 230, 730, 550, 350, 30);
//        passwordInput5->SetPlaceholder("Space-efficient display...");
//        container->AddChild(passwordInput5);
//
//        // Compact Legend
//        auto compactLegend = CreateCompactLegend("CompactLegend", 231, 730, 590, 350, 30);
//        compactLegend->LinkToInput(passwordInput5.get());
//        container->AddChild(compactLegend);
//
//        // Description
//        auto compactDescription = std::make_shared<UltraCanvasLabel>("CompactDesc", 232, 730, 625, 350, 40);
//        compactDescription->SetText("Minimalist single-line display showing\n\"X of Y requirements met\" with indicator");
//        compactDescription->SetFontSize(11);
//        compactDescription->SetTextColor(Color(100, 100, 100));
//        container->AddChild(compactDescription);

        return container;
    }

} // namespace UltraCanvas
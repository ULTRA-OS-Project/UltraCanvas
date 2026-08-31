// Apps/DemoApp/UltraCanvasTextInputExamples.cpp
// Implementation of all component example creators
// Version: 1.2.0
// Last Modified: 2026-08-31
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasPasswordStrengthMeter.h"
#include "UltraCanvasPasswordRuleLegend.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextArea.h"
#include <sstream>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

// ===== BASIC UI ELEMENTS =====

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTextInputExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TextInputExamples", 0, 0, 900, 1000);
        container->SetPadding(0,0,10,0);
        // Title
        auto title = std::make_shared<UltraCanvasLabel>("TextInputTitle", 10, 10, 300, 30);
        title->SetText("Text Input Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // ===== COLUMN 1: BASIC INPUTS =====

        auto singleLineLabel = std::make_shared<UltraCanvasLabel>("SingleLineLabel", 20, 45, 200, 20);
        singleLineLabel->SetText("Single Line Input");
        singleLineLabel->SetFontSize(12);
        container->AddChild(singleLineLabel);

        // Single Line Input
        auto singleLineInput = std::make_shared<UltraCanvasTextInput>("SingleLineInput", 20, 70, 300, 30);
        singleLineInput->SetPlaceholder("Enter single line text...");
        singleLineInput->SetMaxLength(100);
        container->AddChild(singleLineInput);


        // Multi-line Text Area
        auto multiLineLabel = std::make_shared<UltraCanvasLabel>("MultiLineLabel", 20, 130, 200, 20);
        multiLineLabel->SetText("Multi-line Text Input");
        multiLineLabel->SetFontSize(12);
        container->AddChild(multiLineLabel);

        auto multiLineInput = std::make_shared<UltraCanvasTextInput>("MultiLineInput", 20, 155, 300, 100);
        multiLineInput->SetInputType(TextInputType::Multiline);
        multiLineInput->SetPlaceholder("Enter multi-line text...\nSupports line breaks.");
        container->AddChild(multiLineInput);


        auto passwordLabel = std::make_shared<UltraCanvasLabel>("PasswordLabel", 20, 280, 250, 20);
        passwordLabel->SetText("Basic Password Field (eye reveals text)");
        passwordLabel->SetFontSize(12);
        container->AddChild(passwordLabel);

        // Password Field (Basic) - the in-field eye button shows the typed text
        auto passwordInput = std::make_shared<UltraCanvasTextInput>("PasswordInput", 20, 305, 300, 30);
        passwordInput->SetInputType(TextInputType::Password);
        passwordInput->SetPlaceholder("Enter password...");
        passwordInput->SetShowPasswordToggle(true);
        container->AddChild(passwordInput);


        auto numericLabel = std::make_shared<UltraCanvasLabel>("NumericLabel", 20, 360, 200, 20);
        numericLabel->SetText("Numeric Input (0-1000)");
        numericLabel->SetFontSize(12);
        container->AddChild(numericLabel);

        // Numeric Input
        auto numericInput = std::make_shared<UltraCanvasTextInput>("NumericInput", 20, 385, 300, 30);
        numericInput->SetInputType(TextInputType::Number);
        numericInput->SetPlaceholder("0.00");
        container->AddChild(numericInput);


        auto textAreaLabel = std::make_shared<UltraCanvasLabel>("TextAreaLabel", 20, 440, 200, 20);
        textAreaLabel->SetText("Text Area Component");
        textAreaLabel->SetFontSize(12);
        container->AddChild(textAreaLabel);

        // Text Area Component
        auto textAreaInput = std::make_shared<UltraCanvasTextArea>("TextArea", 20, 465, 300, 100);
        container->AddChild(textAreaInput);


        // ===== PASSWORD WITH BAR STRENGTH METER =====

        auto passwordTitle1 = std::make_shared<UltraCanvasLabel>("PasswordTitle1", 350, 45, 350, 20);
        passwordTitle1->SetText("Password with Bar Strength Meter");
        passwordTitle1->SetFontSize(12);
        //passwordTitle1->SetFontWeight(FontWeight::Bold);
        container->AddChild(passwordTitle1);

        // Reveal ("eye") button on the right of the field: click it to read back the
        // password in clear text, click again to mask it.
        auto passwordInput1 = CreateRevealablePasswordInput("Password1", 350, 70, 350, 30);
        passwordInput1->SetPlaceholder("Enter password...");

        // Add validation rules
        passwordInput1->AddValidationRule(ValidationRule::MinLength(8));
        passwordInput1->AddValidationRule(ValidationRule::RequireUppercase());
        passwordInput1->AddValidationRule(ValidationRule::RequireLowercase());
        passwordInput1->AddValidationRule(ValidationRule::RequireDigit());
        passwordInput1->AddValidationRule(ValidationRule::RequireSpecialChar());

        container->AddChild(passwordInput1);

        // Bar Strength Meter
        auto strengthBar = CreateBarStrengthMeter("StrengthBar", 350, 100, 350, 20);
        strengthBar->LinkToInput(passwordInput1.get());
        strengthBar->SetShowLabel(true);
        strengthBar->SetShowPercentage(true);
        container->AddChild(strengthBar);

        // Description
        auto barDescription = std::make_shared<UltraCanvasLabel>("BarDesc", 350, 130, 350, 58);
        barDescription->SetText("Real-time strength indicator with animated\n"
                                "color transitions (red → yellow → green).\n"
                                "Click the eye icon in the field to show the password.");
        barDescription->SetFontSize(11);
        barDescription->SetTextColor(Color(100, 100, 100));
        container->AddChild(barDescription);

        // ===== PASSWORD WITH SEGMENTED METER =====

//        auto passwordTitle2 = std::make_shared<UltraCanvasLabel>("PasswordTitle2", 350, 200, 350, 25);passwordTitle2->SetText("Password with Segmented Meter");
//        passwordTitle2->SetFontSize(14);
//        //passwordTitle2->SetFontWeight(FontWeight::Bold);
//        container->AddChild(passwordTitle2);
//
//        auto passwordInput2 = CreatePasswordInput("Password2", 350, 230, 350, 30);
//        passwordInput2->SetPlaceholder("Try a strong password...");
//        container->AddChild(passwordInput2);
//
//        // Segmented Strength Meter
//        auto strengthSegments = CreateSegmentedStrengthMeter("StrengthSegments", 350, 270, 350, 15);
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
//        auto segmentDescription = std::make_shared<UltraCanvasLabel>("SegmentDesc", 350, 310, 350, 40);
//        segmentDescription->SetText("Gaming-style segmented display with\n5 colored strength indicators");
//        segmentDescription->SetFontSize(11);
//        segmentDescription->SetTextColor(Color(100, 100, 100));
//        container->AddChild(segmentDescription);

        // ===== PASSWORD WITH CHECKLIST =====

        auto passwordTitle3 = std::make_shared<UltraCanvasLabel>("PasswordTitle3", 350, 195, 350, 20);
        passwordTitle3->SetText("Password with Requirements Checklist");
//        passwordTitle3->SetFontSize(14);
//        passwordTitle3->SetFontWeight(FontWeight::Bold);
        container->AddChild(passwordTitle3);

        auto passwordInput3 = CreatePasswordInput("Password3", 350, 220, 350, 30);
        passwordInput3->SetPlaceholder("Meet all requirements...");
        container->AddChild(passwordInput3);

        // Explicit "Show password" flag instead of the in-field eye icon: an
        // external control driving the same SetPasswordRevealed() state.
        auto showPassword3 = UltraCanvasCheckbox::CreateCheckbox(
                "ShowPassword3", 350, 253, 160, 20, "Show password", false);
        auto* password3 = passwordInput3.get();
        showPassword3->onStateChanged = [password3](CheckedState, CheckedState newState) {
            password3->SetPasswordRevealed(newState == CheckedState::Checked);
        };
        container->AddChild(showPassword3);

        // Checklist Legend
        auto ruleLegend = CreateChecklistLegend("RuleLegend", 350, 278, 350, 140);
        ruleLegend->LinkToInput(passwordInput3.get());
        ruleLegend->SetShowMetRules(true);
        // Size the box to what the rule list actually paints so it cannot spill
        // over the description below it. SetElementSize() (not SetHeight) so the
        // CSS height the layout pass uses is updated too.
        ruleLegend->SetElementSize(Size2Df(350, ruleLegend->GetContentHeight()));

        // Setup callbacks for status updates
        ruleLegend->onAllRulesMet = [](bool allMet) {
            if (allMet) {
                debugOutput << "✓ All password requirements met!" << std::endl;
            }
        };

        ruleLegend->onRuleStatusChanged = [](int met, int total) {
            debugOutput << "Password rules: " << met << "/" << total << " met" << std::endl;
        };

        container->AddChild(ruleLegend);

        // Description
        auto checklistDescription = std::make_shared<UltraCanvasLabel>("ChecklistDesc", 350, 415, 350, 58);
        checklistDescription->SetText("Interactive checklist with ✓/✗ indicators\n"
                                      "showing real-time validation status.\n"
                                      "\"Show password\" flag unmasks the field.");
        checklistDescription->SetFontSize(11);
        checklistDescription->SetTextColor(Color(100, 100, 100));
        container->AddChild(checklistDescription);

        // ===== COMPLETE PASSWORD SETUP =====

        // Width kept clear of the circular meter's column (x >= 710).
        auto passwordTitle4 = std::make_shared<UltraCanvasLabel>("PasswordTitle4", 350, 480, 355, 25);
        passwordTitle4->SetText("Complete Setup: Circular Meter + Detailed Legend");
//        passwordTitle4->SetFontSize(14);
        //passwordTitle4->SetFontWeight(FontWeight::Bold);
        container->AddChild(passwordTitle4);

        // Both reveal controls on one field: the in-field eye icon and the explicit
        // "Show password" flag below stay in sync.
        auto passwordInput4 = CreateRevealablePasswordInput("Password4", 350, 510, 350, 30);
        passwordInput4->SetPlaceholder("Create strong password...");
        container->AddChild(passwordInput4);

        auto showPassword4 = UltraCanvasCheckbox::CreateCheckbox(
                "ShowPassword4", 350, 548, 160, 20, "Show password", false);
        auto* password4 = passwordInput4.get();
        auto* showPassword4Ptr = showPassword4.get();
        showPassword4->onStateChanged = [password4](CheckedState, CheckedState newState) {
            password4->SetPasswordRevealed(newState == CheckedState::Checked);
        };
        // Clicking the eye icon keeps the flag in step (SetPasswordRevealed() ignores
        // a no-op change, so the two controls cannot ping-pong).
        passwordInput4->onPasswordVisibilityChanged = [showPassword4Ptr](bool revealed) {
            showPassword4Ptr->SetChecked(revealed);
        };
        container->AddChild(showPassword4);

        // Circular Strength Meter
        auto circularMeter = CreateCircularStrengthMeter("CircularMeter", 710, 490, 70);
        circularMeter->LinkToInput(passwordInput4.get());

        StrengthMeterConfig circularConfig;
        circularConfig.style = StrengthMeterStyle::Circular;
        circularConfig.showPercentage = true;
        circularMeter->SetConfig(circularConfig);

        container->AddChild(circularMeter);

        // Detailed Legend with Strict Rules
        const float detailedLegendY = 580;
        auto detailedLegend = CreatePasswordRuleLegend("DetailedLegend",
                                                       350, detailedLegendY, 435, 280,
                                                       LegendStyle::Detailed);
        detailedLegend->LinkToInput(passwordInput4.get());
        detailedLegend->SetupStrictRules();  // Use strict validation rules

        PasswordRuleLegendConfig legendConfig;
        legendConfig.style = LegendStyle::Detailed;
        //legendConfig.animateChanges = true;
        legendConfig.showMetRules = true;
        detailedLegend->SetConfig(legendConfig);

        // The 7 strict rules paint taller than the 280px the box used to be given,
        // and the legend does not clip - the overflowing rows used to land on top of
        // the description text below. Size the box to its content and place the
        // description underneath it.
        const float detailedLegendHeight = detailedLegend->GetContentHeight();
        detailedLegend->SetElementSize(Size2Df(435, detailedLegendHeight));

        // Add callbacks for strength updates
        circularMeter->onStrengthChanged = [](float strength) {
            debugOutput << "Password strength: " << strength << "%" << std::endl;
        };

        circularMeter->onStrengthLevelChanged = [](const std::string& level) {
            debugOutput << "Strength level: " << level << std::endl;
        };

        container->AddChild(detailedLegend);

        // Description
        auto completeDescription = std::make_shared<UltraCanvasLabel>(
                "CompleteDesc", 350, detailedLegendY + detailedLegendHeight + 12, 435, 80);
        completeDescription->SetText("Professional registration form setup with:\n"
                                     "• Eye icon and \"Show password\" flag (kept in sync)\n"
                                     "• Circular strength meter with percentage\n"
                                     "• Detailed rule legend with backgrounds\n"
                                     "• Strict validation (12+ chars, no patterns)");
        completeDescription->SetFontSize(11);
        completeDescription->SetTextColor(Color(100, 100, 100));
        container->AddChild(completeDescription);

//        // ===== COMPACT LEGEND EXAMPLE =====
//
//        auto compactTitle = std::make_shared<UltraCanvasLabel>("CompactTitle", 730, 520, 350, 25);
//        compactTitle->SetText("Compact Legend Display");
//        compactTitle->SetFontSize(14);
//        compactTitle->SetFontWeight(FontWeight::Bold);
//        container->AddChild(compactTitle);
//
//        auto passwordInput5 = CreatePasswordInput("Password5", 730, 550, 350, 30);
//        passwordInput5->SetPlaceholder("Space-efficient display...");
//        container->AddChild(passwordInput5);
//
//        // Compact Legend
//        auto compactLegend = CreateCompactLegend("CompactLegend", 730, 590, 350, 30);
//        compactLegend->LinkToInput(passwordInput5.get());
//        container->AddChild(compactLegend);
//
//        // Description
//        auto compactDescription = std::make_shared<UltraCanvasLabel>("CompactDesc", 730, 625, 350, 40);
//        compactDescription->SetText("Minimalist single-line display showing\n\"X of Y requirements met\" with indicator");
//        compactDescription->SetFontSize(11);
//        compactDescription->SetTextColor(Color(100, 100, 100));
//        container->AddChild(compactDescription);

        return container;
    }

} // namespace UltraCanvas
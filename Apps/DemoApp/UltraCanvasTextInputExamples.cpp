// Apps/DemoApp/UltraCanvasTextInputExamples.cpp
// Implementation of all component example creators
// Version: 1.4.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  example in a wrapping row, inputs as wide as their card,
//                  every text and checkbox sized to fit
// Version: 1.3.0 - In-field buttons: the clear cross, and the same button as "Reset";
//                 the text areas get a frame, and no title is cut off
// Version: 1.2.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
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
        auto container = DemoKit::CreatePage("TextInputExamples", "Text Input Examples",
                "Single- and multi-line inputs, password fields with strength meters and rule "
                "legends, and buttons inside the field");

        // The examples share the width and wrap under each other when narrow.
        auto examples = DemoKit::AddRow(container, "TextInputCards");

        // ===== COLUMN 1: BASIC INPUTS =====
        auto basicCard = DemoKit::AddCard(examples, "TextInputBasicCard", "Basic Inputs");

        DemoKit::AddText(basicCard, "SingleLineLabel", "Single Line Input");

        // Single Line Input
        auto singleLineInput = std::make_shared<UltraCanvasTextInput>("SingleLineInput", 0, 0, 300, 30);
        singleLineInput->SetPlaceholder("Enter single line text...");
        singleLineInput->SetMaxLength(100);
        DemoKit::AddStretched(basicCard, singleLineInput);


        // Multi-line Text Area
        DemoKit::AddText(basicCard, "MultiLineLabel", "Multi-line Text Input");

        // A text area has no frame of its own - it is also the editor that
        // fills a window - so these two get the text inputs' border.
        auto multiLineInput = std::make_shared<UltraCanvasTextArea>("MultiLineInput", 0, 0, 300, 100);
        multiLineInput->SetBorders(1.0f, Color(200, 200, 200, 255), 4.0f);
        multiLineInput->SetWordWrap(true);
        multiLineInput->SetPlaceholder("Enter multi-line text...\nSupports line breaks.");
        DemoKit::AddStretched(basicCard, multiLineInput);


        DemoKit::AddText(basicCard, "PasswordLabel", "Basic Password Field (eye reveals text)");

        // Password Field (Basic) - the in-field eye button shows the typed text
        auto passwordInput = std::make_shared<UltraCanvasTextInput>("PasswordInput", 0, 0, 300, 30);
        passwordInput->SetInputType(TextInputType::Password);
        passwordInput->SetPlaceholder("Enter password...");
        passwordInput->SetShowPasswordToggle(true);
        DemoKit::AddStretched(basicCard, passwordInput);


        DemoKit::AddText(basicCard, "NumericLabel", "Numeric Input (0-1000)");

        // Numeric Input
        auto numericInput = std::make_shared<UltraCanvasTextInput>("NumericInput", 0, 0, 300, 30);
        numericInput->SetInputType(TextInputType::Number);
        numericInput->SetPlaceholder("0.00");
        DemoKit::AddStretched(basicCard, numericInput);


        DemoKit::AddText(basicCard, "TextAreaLabel", "Text Area Component");

        // Text Area Component
        auto textAreaInput = std::make_shared<UltraCanvasTextArea>("TextArea", 0, 0, 300, 100);
        textAreaInput->SetBorders(1.0f, Color(200, 200, 200, 255), 4.0f);
        textAreaInput->SetPlaceholder("A plain text area: type, paste, undo...");
        DemoKit::AddStretched(basicCard, textAreaInput);

        // ===== IN-FIELD BUTTONS: CLEAR AND "RESET" =====
        // The button inside the field is UltraCanvasTextInput's own:
        // SetShowClearButton puts it at the right end while the field has
        // text, and SetClearButtonText gives it a word instead of the cross -
        // the demo's tree filter uses "Reset". A click empties the field and
        // calls onTextChanged, then onCleared. Both start filled, so the
        // buttons are on show.
        auto clearCard = DemoKit::AddCard(examples, "ClearButtonLabel",
                "Buttons inside the field: clear and \"Reset\"");

        auto clearInput = std::make_shared<UltraCanvasTextInput>("ClearButtonInput", 0, 0, 300, 30);
        clearInput->SetPlaceholder("Type, then click the cross...");
        clearInput->SetShowClearButton(true);
        clearInput->SetText("Click the cross to clear me");
        DemoKit::AddStretched(clearCard, clearInput);

        auto resetInput = std::make_shared<UltraCanvasTextInput>("ResetButtonInput", 0, 0, 300, 30);
        resetInput->SetPlaceholder("Filter components");
        resetInput->SetShowClearButton(true);
        resetInput->SetClearButtonText("Reset");
        resetInput->SetText("chart");
        DemoKit::AddStretched(clearCard, resetInput);

        auto clearStatus = DemoKit::AddCaption(clearCard, "ClearButtonStatus",
                "SetShowClearButton(true) shows the button,\n"
                "SetClearButtonText(\"Reset\") gives it a word");

        clearInput->onCleared = [clearStatus]() {
            clearStatus->SetText("The cross emptied the field\n(onTextChanged, then onCleared)");
        };
        resetInput->onCleared = [clearStatus]() {
            clearStatus->SetText("\"Reset\" emptied the field\n(onTextChanged, then onCleared)");
        };


        // ===== PASSWORD WITH BAR STRENGTH METER =====
        auto barCard = DemoKit::AddCard(examples, "PasswordTitle1", "Password with Bar Strength Meter");

        // Reveal ("eye") button on the right of the field: click it to read back the
        // password in clear text, click again to mask it.
        auto passwordInput1 = CreateRevealablePasswordInput("Password1", 0, 0, 350, 30);
        passwordInput1->SetPlaceholder("Enter password...");

        // Add validation rules
        passwordInput1->AddValidationRule(ValidationRule::MinLength(8));
        passwordInput1->AddValidationRule(ValidationRule::RequireUppercase());
        passwordInput1->AddValidationRule(ValidationRule::RequireLowercase());
        passwordInput1->AddValidationRule(ValidationRule::RequireDigit());
        passwordInput1->AddValidationRule(ValidationRule::RequireSpecialChar());

        DemoKit::AddStretched(barCard, passwordInput1);

        // Bar Strength Meter
        auto strengthBar = CreateBarStrengthMeter("StrengthBar", 0, 0, 350, 20);
        strengthBar->LinkToInput(passwordInput1.get());
        strengthBar->SetShowLabel(true);
        strengthBar->SetShowPercentage(true);
        DemoKit::AddStretched(barCard, strengthBar);

        // Description
        DemoKit::AddCaption(barCard, "BarDesc",
                "Real-time strength indicator with animated color transitions (red, then "
                "yellow, then green). Click the eye icon in the field to show the password.");

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
        auto checklistCard = DemoKit::AddCard(examples, "PasswordTitle3", "Password with Requirements Checklist");

        auto passwordInput3 = CreatePasswordInput("Password3", 0, 0, 350, 30);
        passwordInput3->SetPlaceholder("Meet all requirements...");
        DemoKit::AddStretched(checklistCard, passwordInput3);

        // Explicit "Show password" flag instead of the in-field eye icon: an
        // external control driving the same SetPasswordRevealed() state.
        auto showPassword3 = std::make_shared<UltraCanvasCheckbox>("ShowPassword3", "Show password");
        showPassword3->SetChecked(false);
        auto* password3 = passwordInput3.get();
        showPassword3->onStateChanged = [password3](CheckedState, CheckedState newState) {
            password3->SetPasswordRevealed(newState == CheckedState::Checked);
        };
        DemoKit::Add(checklistCard, showPassword3);

        // Checklist Legend
        auto ruleLegend = CreateChecklistLegend("RuleLegend", 0, 0, 350, 140);
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

        DemoKit::AddFixed(checklistCard, ruleLegend);

        // Description
        DemoKit::AddCaption(checklistCard, "ChecklistDesc",
                "Interactive checklist with met / unmet indicators showing real-time "
                "validation status. \"Show password\" flag unmasks the field.");

        // ===== COMPLETE PASSWORD SETUP =====
        // Wide enough for the 435 px legend before it wraps.
        auto completeCard = DemoKit::AddCard(examples, "PasswordTitle4", "Complete Setup: Meter + Detailed Legend",
                                             "", 435 + 2 * DemoKit::kCardPadding + 2);

        // The field and its flag beside the circular meter.
        auto completeRow = DemoKit::CreateRow("CompleteSetupRow", 10, false);
        DemoKit::Add(completeCard, completeRow);
        auto completeFields = DemoKit::CreateColumn("CompleteSetupFields", 8);
        DemoKit::Add(completeRow, completeFields, 1);

        // Both reveal controls on one field: the in-field eye icon and the explicit
        // "Show password" flag below stay in sync.
        auto passwordInput4 = CreateRevealablePasswordInput("Password4", 0, 0, 350, 30);
        passwordInput4->SetPlaceholder("Create strong password...");
        DemoKit::AddStretched(completeFields, passwordInput4);

        auto showPassword4 = std::make_shared<UltraCanvasCheckbox>("ShowPassword4", "Show password");
        showPassword4->SetChecked(false);
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
        DemoKit::Add(completeFields, showPassword4);

        // Circular Strength Meter
        auto circularMeter = CreateCircularStrengthMeter("CircularMeter", 0, 0, 70);
        circularMeter->LinkToInput(passwordInput4.get());

        StrengthMeterConfig circularConfig;
        circularConfig.style = StrengthMeterStyle::Circular;
        circularConfig.showPercentage = true;
        circularMeter->SetConfig(circularConfig);

        DemoKit::AddFixed(completeRow, circularMeter);

        // Detailed Legend with Strict Rules
        auto detailedLegend = CreatePasswordRuleLegend("DetailedLegend",
                                                       0, 0, 435, 280,
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
        // the description text below. Size the box to its content.
        const float detailedLegendHeight = detailedLegend->GetContentHeight();
        detailedLegend->SetElementSize(Size2Df(435, detailedLegendHeight));

        // Add callbacks for strength updates
        circularMeter->onStrengthChanged = [](float strength) {
            debugOutput << "Password strength: " << strength << "%" << std::endl;
        };

        circularMeter->onStrengthLevelChanged = [](const std::string& level) {
            debugOutput << "Strength level: " << level << std::endl;
        };

        DemoKit::AddFixed(completeCard, detailedLegend);

        // Description
        DemoKit::AddCaption(completeCard, "CompleteDesc",
                "Professional registration form setup with:\n"
                "• Eye icon and \"Show password\" flag (kept in sync)\n"
                "• Circular strength meter with percentage\n"
                "• Detailed rule legend with backgrounds\n"
                "• Strict validation (12+ chars, no patterns)");

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

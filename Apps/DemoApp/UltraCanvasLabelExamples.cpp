// Apps/DemoApp/UltraCanvasDemoExamples.cpp
// Implementation of all component example creators
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
//#include "UltraCanvasButton3Sections.h"
#include "Plugins/Charts/UltraCanvasDivergingBarChart.h"
#include <sstream>
#include <random>
#include <map>

namespace UltraCanvas {
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateLabelExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("LabelExamples", 0, 0, 1000, 720);
        container->SetPadding(0,0,10,0);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("LabelTitle", 10, 10, 300, 25);
        title->SetText("Label Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Basic Label
        auto basicLabel = std::make_shared<UltraCanvasLabel>("BasicLabel", 20, 50, 400, 25);
        basicLabel->SetText("This is a basic label with default styling.");
        container->AddChild(basicLabel);

        // Header Text
        auto headerLabel = std::make_shared<UltraCanvasLabel>("HeaderLabel", 20, 90, 500, 35);
        headerLabel->SetText("Header Label Example");
        headerLabel->SetFontSize(24);
        headerLabel->SetFontWeight(FontWeight::Bold);
        headerLabel->SetTextColor(Color(0, 100, 200, 255));
        container->AddChild(headerLabel);

        // Status Labels (square corners)
        auto successLabel = std::make_shared<UltraCanvasLabel>("SuccessLabel", 20, 140, 150, 25);
        successLabel->SetText("✓ Success");
        successLabel->SetBackgroundColor(Color(200, 255, 200, 255));
        successLabel->SetTextColor(Color(0, 150, 0, 255));
        successLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        container->AddChild(successLabel);

        auto warningLabel = std::make_shared<UltraCanvasLabel>("WarningLabel", 180, 140, 150, 25);
        warningLabel->SetText("⚠ Warning");
        warningLabel->SetBackgroundColor(Color(255, 255, 200, 255));
        warningLabel->SetTextColor(Color(200, 150, 0, 255));
        warningLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        container->AddChild(warningLabel);

        auto errorLabel = std::make_shared<UltraCanvasLabel>("ErrorLabel", 340, 140, 150, 25);
        errorLabel->SetText("✗ Error");
        errorLabel->SetBackgroundColor(Color(255, 200, 200, 255));
        errorLabel->SetTextColor(Color(200, 0, 0, 255));
        errorLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        container->AddChild(errorLabel);

        // Multi-line Label
        auto multiLabel = std::make_shared<UltraCanvasLabel>("MultiLabel", 20, 190, 450, 80);
        multiLabel->SetText("This is a multi-line label that demonstrates\nhow text wrapping works with longer content.\nIt supports multiple lines and proper alignment.");
        multiLabel->SetWrap(TextWrap::WrapWord);
        multiLabel->SetAlignment(TextAlignment::Left);
        multiLabel->SetBackgroundColor(Color(245, 245, 245, 255));
//        multiLabel->SetBorderStyle(BorderStyle::Solid);
        multiLabel->SetBorders(1.0f);
        multiLabel->SetPadding(10.0f);
        container->AddChild(multiLabel);

        // ===== ROUNDED CORNER LABELS =====
        // Rounded corners are produced by passing a border radius to SetBorders().
        // The radius is honoured for the background fill even when the border
        // colour is transparent, which lets us draw rounded labels both WITH a
        // visible border and WITHOUT one.

        auto roundedSection = std::make_shared<UltraCanvasLabel>("RoundedSection", 20, 290, 500, 25);
        roundedSection->SetText("Rounded Corner Labels");
        roundedSection->SetFontSize(16);
        roundedSection->SetFontWeight(FontWeight::Bold);
        container->AddChild(roundedSection);

        // ----- Rounded WITH border -----
        auto withBorderCaption = std::make_shared<UltraCanvasLabel>("RoundedWithBorderCaption", 20, 325, 460, 20);
        withBorderCaption->SetText("With border — SetBorders(width, color, radius):");
        withBorderCaption->SetTextColor(Color(90, 90, 90, 255));
        container->AddChild(withBorderCaption);

        // Filled, rounded, with a coloured border.
        auto roundedFilled = std::make_shared<UltraCanvasLabel>("RoundedFilledBorder", 20, 350, 200, 32);
        roundedFilled->SetText("Rounded + Border");
        roundedFilled->SetBackgroundColor(Color(225, 240, 255, 255));
        roundedFilled->SetTextColor(Color(0, 90, 170, 255));
        roundedFilled->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        roundedFilled->SetBorders(1.5f, Color(0, 120, 215, 255), 10.0f);
        container->AddChild(roundedFilled);

        // Outline-only pill: transparent background, visible border, large radius.
        auto roundedOutline = std::make_shared<UltraCanvasLabel>("RoundedOutlinePill", 240, 350, 200, 32);
        roundedOutline->SetText("Outlined Pill");
        roundedOutline->SetTextColor(Color(120, 60, 160, 255));
        roundedOutline->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        roundedOutline->SetBorders(2.0f, Color(150, 90, 200, 255), 16.0f); // radius >= h/2 -> pill
        container->AddChild(roundedOutline);

        // ----- Rounded WITHOUT border -----
        auto noBorderCaption = std::make_shared<UltraCanvasLabel>("RoundedNoBorderCaption", 20, 395, 460, 20);
        noBorderCaption->SetText("Without border — transparent border colour keeps the rounded fill:");
        noBorderCaption->SetTextColor(Color(90, 90, 90, 255));
        container->AddChild(noBorderCaption);

        // Filled status pills with no visible border (border colour transparent).
        auto pillSuccess = std::make_shared<UltraCanvasLabel>("PillSuccess", 20, 420, 150, 28);
        pillSuccess->SetText("✓ Success");
        pillSuccess->SetBackgroundColor(Color(76, 175, 80, 255));
        pillSuccess->SetTextColor(Colors::White);
        pillSuccess->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        pillSuccess->SetBorders(1.0f, Colors::Transparent, 14.0f);
        container->AddChild(pillSuccess);

        auto pillWarning = std::make_shared<UltraCanvasLabel>("PillWarning", 180, 420, 150, 28);
        pillWarning->SetText("⚠ Warning");
        pillWarning->SetBackgroundColor(Color(255, 179, 0, 255));
        pillWarning->SetTextColor(Colors::White);
        pillWarning->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        pillWarning->SetBorders(1.0f, Colors::Transparent, 14.0f);
        container->AddChild(pillWarning);

        auto pillError = std::make_shared<UltraCanvasLabel>("PillError", 340, 420, 150, 28);
        pillError->SetText("✗ Error");
        pillError->SetBackgroundColor(Color(229, 57, 53, 255));
        pillError->SetTextColor(Colors::White);
        pillError->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        pillError->SetBorders(1.0f, Colors::Transparent, 14.0f);
        container->AddChild(pillError);

        // ----- Rounded multi-line cards -----
        // Card with a visible border and rounded corners.
        auto roundedCard = std::make_shared<UltraCanvasLabel>("RoundedCardBorder", 20, 470, 450, 90);
        roundedCard->SetText("This is a rounded card with a visible border.\nThe border radius rounds every corner while\nthe stroke colour stays visible.");
        roundedCard->SetWrap(TextWrap::WrapWord);
        roundedCard->SetAlignment(TextAlignment::Left);
        roundedCard->SetBackgroundColor(Color(248, 249, 250, 255));
        roundedCard->SetBorders(1.0f, Color(190, 190, 190, 255), 12.0f);
        roundedCard->SetPadding(12.0f);
        container->AddChild(roundedCard);

        // Card with rounded corners but no visible border (soft filled panel).
        auto roundedCardNoBorder = std::make_shared<UltraCanvasLabel>("RoundedCardNoBorder", 490, 470, 450, 90);
        roundedCardNoBorder->SetText("This rounded card has no visible border —\njust a soft filled background with rounded\ncorners for a modern, flat look.");
        roundedCardNoBorder->SetWrap(TextWrap::WrapWord);
        roundedCardNoBorder->SetAlignment(TextAlignment::Left);
        roundedCardNoBorder->SetBackgroundColor(Color(232, 244, 253, 255));
        roundedCardNoBorder->SetBorders(1.0f, Colors::Transparent, 12.0f);
        roundedCardNoBorder->SetPadding(12.0f);
        container->AddChild(roundedCardNoBorder);

        return container;
    }

} // namespace UltraCanvas
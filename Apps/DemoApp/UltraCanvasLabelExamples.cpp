// Apps/DemoApp/UltraCanvasLabelExamples.cpp
// Implementation of all component example creators
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  group, every label as large as its text (badges keep their
//                  old size as a minimum)
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
//#include "UltraCanvasButton3Sections.h"
#include "Plugins/Charts/UltraCanvasDivergingBarChart.h"
#include <sstream>
#include <random>
#include <map>

namespace UltraCanvas {

    namespace {
        // A badge or pill is as large as its text, at least the size it was
        // drawn at before, so a longer word widens it instead of being cut.
        void SetMinimumSize(const std::shared_ptr<UltraCanvasLabel>& label, float w, float h) {
            CSSLayout::BoxConstraints limits;
            limits.minWidth = CSSLayout::Dimension::Px(w);
            limits.minHeight = CSSLayout::Dimension::Px(h);
            label->boxConstraints = limits;
            label->SetPadding(4.0f, 10.0f, 4.0f, 10.0f);
        }
    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateLabelExamples() {
        auto container = DemoKit::CreatePage("LabelExamples", "Label Examples", "");

        // ===== BASIC AND HEADER LABELS =====
        auto basicCard = DemoKit::AddCard(container, "LabelBasicCard", "Basic labels");

        // Basic Label
        auto basicLabel = std::make_shared<UltraCanvasLabel>("BasicLabel",
                "This is a basic label with default styling.");
        basicLabel->SetWrap(TextWrap::WrapWord);
        DemoKit::Add(basicCard, basicLabel);

        // Header Text
        auto headerLabel = std::make_shared<UltraCanvasLabel>("HeaderLabel", "Header Label Example");
        headerLabel->SetFontSize(24);
        headerLabel->SetFontWeight(FontWeight::Bold);
        headerLabel->SetTextColor(Color(0, 100, 200, 255));
        headerLabel->SetWrap(TextWrap::WrapWord);
        DemoKit::Add(basicCard, headerLabel);

        // ===== STATUS LABELS (square corners) =====
        auto statusCard = DemoKit::AddCard(container, "LabelStatusCard", "Status labels");
        auto statusRow = DemoKit::AddRow(statusCard, "LabelStatusRow", 10);

        auto successLabel = std::make_shared<UltraCanvasLabel>("SuccessLabel", "Success");
        successLabel->SetBackgroundColor(Color(200, 255, 200, 255));
        successLabel->SetTextColor(Color(0, 150, 0, 255));
        successLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        SetMinimumSize(successLabel, 150, 25);
        DemoKit::Add(statusRow, successLabel);

        auto warningLabel = std::make_shared<UltraCanvasLabel>("WarningLabel", "Warning");
        warningLabel->SetBackgroundColor(Color(255, 255, 200, 255));
        warningLabel->SetTextColor(Color(200, 150, 0, 255));
        warningLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        SetMinimumSize(warningLabel, 150, 25);
        DemoKit::Add(statusRow, warningLabel);

        auto errorLabel = std::make_shared<UltraCanvasLabel>("ErrorLabel", "Error");
        errorLabel->SetBackgroundColor(Color(255, 200, 200, 255));
        errorLabel->SetTextColor(Color(200, 0, 0, 255));
        errorLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        SetMinimumSize(errorLabel, 150, 25);
        DemoKit::Add(statusRow, errorLabel);

        // ===== MULTI-LINE LABEL =====
        auto multiCard = DemoKit::AddCard(container, "LabelMultiCard", "Multi-line label");
        auto multiLabel = std::make_shared<UltraCanvasLabel>("MultiLabel",
                "This is a multi-line label that demonstrates\nhow text wrapping works with longer content.\nIt supports multiple lines and proper alignment.");
        multiLabel->SetWrap(TextWrap::WrapWord);
        multiLabel->SetAlignment(TextAlignment::Left);
        multiLabel->SetBackgroundColor(Color(245, 245, 245, 255));
//        multiLabel->SetBorderStyle(BorderStyle::Solid);
        multiLabel->SetBorders(1.0f);
        multiLabel->SetPadding(10.0f);
        DemoKit::Add(multiCard, multiLabel);

        // ===== ROUNDED CORNER LABELS =====
        // Rounded corners are produced by passing a border radius to SetBorders().
        // The radius is honoured for the background fill even when the border
        // colour is transparent, which lets us draw rounded labels both WITH a
        // visible border and WITHOUT one.
        auto roundedSection = DemoKit::AddCard(container, "RoundedSection", "Rounded Corner Labels");

        // ----- Rounded WITH border -----
        DemoKit::AddCaption(roundedSection, "RoundedWithBorderCaption",
                "With border — SetBorders(width, color, radius):");
        auto withBorderRow = DemoKit::AddRow(roundedSection, "RoundedWithBorderRow", 20);

        // Filled, rounded, with a coloured border.
        auto roundedFilled = std::make_shared<UltraCanvasLabel>("RoundedFilledBorder", "Rounded + Border");
        roundedFilled->SetBackgroundColor(Color(225, 240, 255, 255));
        roundedFilled->SetTextColor(Color(0, 90, 170, 255));
        roundedFilled->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        roundedFilled->SetBorders(1.5f, Color(0, 120, 215, 255), 10.0f);
        SetMinimumSize(roundedFilled, 200, 32);
        DemoKit::Add(withBorderRow, roundedFilled);

        // Outline-only pill: transparent background, visible border, large radius.
        auto roundedOutline = std::make_shared<UltraCanvasLabel>("RoundedOutlinePill", "Outlined Pill");
        roundedOutline->SetTextColor(Color(120, 60, 160, 255));
        roundedOutline->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        roundedOutline->SetBorders(2.0f, Color(150, 90, 200, 255), 16.0f); // radius >= h/2 -> pill
        SetMinimumSize(roundedOutline, 200, 32);
        DemoKit::Add(withBorderRow, roundedOutline);

        // ----- Rounded WITHOUT border -----
        DemoKit::AddCaption(roundedSection, "RoundedNoBorderCaption",
                "Without border — transparent border colour keeps the rounded fill:");
        auto pillRow = DemoKit::AddRow(roundedSection, "RoundedPillRow", 10);

        // Filled status pills with no visible border (border colour transparent).
        auto pillSuccess = std::make_shared<UltraCanvasLabel>("PillSuccess", "Success");
        pillSuccess->SetBackgroundColor(Color(76, 175, 80, 255));
        pillSuccess->SetTextColor(Colors::White);
        pillSuccess->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        pillSuccess->SetBorders(1.0f, Colors::Transparent, 14.0f);
        SetMinimumSize(pillSuccess, 150, 28);
        DemoKit::Add(pillRow, pillSuccess);

        auto pillWarning = std::make_shared<UltraCanvasLabel>("PillWarning", "Warning");
        pillWarning->SetBackgroundColor(Color(255, 179, 0, 255));
        pillWarning->SetTextColor(Colors::White);
        pillWarning->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        pillWarning->SetBorders(1.0f, Colors::Transparent, 14.0f);
        SetMinimumSize(pillWarning, 150, 28);
        DemoKit::Add(pillRow, pillWarning);

        auto pillError = std::make_shared<UltraCanvasLabel>("PillError", "Error");
        pillError->SetBackgroundColor(Color(229, 57, 53, 255));
        pillError->SetTextColor(Colors::White);
        pillError->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        pillError->SetBorders(1.0f, Colors::Transparent, 14.0f);
        SetMinimumSize(pillError, 150, 28);
        DemoKit::Add(pillRow, pillError);

        // ----- Rounded multi-line cards -----
        // Side by side, sharing the width; they wrap their text when narrow.
        auto roundedCardRow = DemoKit::AddRow(roundedSection, "RoundedCardRow", 20);

        // Card with a visible border and rounded corners.
        auto roundedCard = std::make_shared<UltraCanvasLabel>("RoundedCardBorder",
                "This is a rounded card with a visible border.\nThe border radius rounds every corner while\nthe stroke colour stays visible.");
        roundedCard->SetWrap(TextWrap::WrapWord);
        roundedCard->SetAlignment(TextAlignment::Left);
        roundedCard->SetBackgroundColor(Color(248, 249, 250, 255));
        roundedCard->SetBorders(1.0f, Color(190, 190, 190, 255), 12.0f);
        roundedCard->SetPadding(12.0f);
        DemoKit::Add(roundedCardRow, roundedCard, 1);

        // Card with rounded corners but no visible border (soft filled panel).
        auto roundedCardNoBorder = std::make_shared<UltraCanvasLabel>("RoundedCardNoBorder",
                "This rounded card has no visible border —\njust a soft filled background with rounded\ncorners for a modern, flat look.");
        roundedCardNoBorder->SetWrap(TextWrap::WrapWord);
        roundedCardNoBorder->SetAlignment(TextAlignment::Left);
        roundedCardNoBorder->SetBackgroundColor(Color(232, 244, 253, 255));
        roundedCardNoBorder->SetBorders(1.0f, Colors::Transparent, 12.0f);
        roundedCardNoBorder->SetPadding(12.0f);
        DemoKit::Add(roundedCardRow, roundedCardNoBorder, 1);

        return container;
    }

} // namespace UltraCanvas

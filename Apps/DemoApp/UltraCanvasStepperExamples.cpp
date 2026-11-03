// Apps/DemoApp/UltraCanvasStepperExamples.cpp
// Demonstration of UltraCanvasStepper: horizontal numbered wizard driven by
// Next/Prev buttons, a version with descriptions and an error step, a vertical
// stepper, a compact dot style, and a non-linear (click-any-step) stepper.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  example, horizontal steppers as wide as their card, readouts
//                  and buttons sized to their text
// Version: 1.0.1
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasStepper.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

    namespace {
        // A live readout on a grey background, as tall as its text.
        std::shared_ptr<UltraCanvasLabel> AddReadout(const std::shared_ptr<UltraCanvasContainer>& parent,
                                                     const std::string& id, const std::string& text) {
            auto label = DemoKit::CreateText(id, text);
            label->SetBackgroundColor(Color(240, 240, 240));
            label->SetPadding(3);
            label->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
            DemoKit::Add(parent, label);
            return label;
        }

        // As wide as its text, at least the old 90 px.
        std::shared_ptr<UltraCanvasButton> MakeStepButton(const std::string& id, const std::string& text) {
            auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, 30.0f, text);
            CSSLayout::BoxConstraints limits;
            limits.minWidth = CSSLayout::Dimension::Px(90);
            button->boxConstraints = limits;
            return button;
        }
    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateStepperExamples() {
        auto page = DemoKit::CreatePage("StepperExamples", "Stepper / Wizard Examples",
                "A progress stepper for multi-step flows: completed / active / upcoming / error "
                "states, connectors, four marker styles, horizontal & vertical, linear & non-linear.");

        // ===== 1. HORIZONTAL WIZARD (linear) with Next/Prev =====
        auto card1 = DemoKit::AddCard(page, "StL1", "Horizontal wizard (linear, numbered):");
        auto wiz = CreateStepperWizard("StWiz", 0, 0, 620, 70,
                                       {"Account", "Profile", "Payment", "Review", "Done"});
        DemoKit::AddStretched(card1, wiz);

        auto wizControls = DemoKit::AddRow(card1, "StWizControls", 10);
        auto prevBtn = MakeStepButton("StPrev", "Back");
        prevBtn->onClick = [wiz]() { wiz->PrevStep(true); };
        DemoKit::Add(wizControls, prevBtn);

        auto nextBtn = MakeStepButton("StNext", "Next");
        nextBtn->onClick = [wiz]() { wiz->NextStep(true); };
        DemoKit::Add(wizControls, nextBtn);

        auto status = AddReadout(wizControls, "StWizStatus", "Step 1 of 5: Account");

        auto updateStatus = [wiz, status]() {
            int i = wiz->GetCurrentStep();
            StepItem* s = wiz->GetStep(i);
            status->SetText("Step " + std::to_string(i + 1) + " of " +
                            std::to_string(wiz->GetStepCount()) +
                            (s ? ": " + s->title : ""));
        };
        wiz->onStepChanged = [updateStatus](int) { updateStatus(); };

        // ===== 2. HORIZONTAL WITH DESCRIPTIONS + ERROR STEP =====
        auto card2 = DemoKit::AddCard(page, "StL2", "With descriptions and an error step:");
        auto desc = std::make_shared<UltraCanvasStepper>("StDesc", 0, 0, 900, 80);
        desc->SetSteps({
            StepItem("Cart", "3 items"),
            StepItem("Shipping", "Address saved"),
            StepItem("Payment", "Card declined"),
            StepItem("Confirm", "Pending"),
        });
        desc->SetCurrentStep(2);
        desc->SetStepError(2, true);          // Payment failed
        DemoKit::AddStretched(card2, desc);

        auto cards = DemoKit::AddRow(page, "StepperCards");

        // ===== 3. VERTICAL STEPPER =====
        auto card3 = DemoKit::AddCard(cards, "StL3", "Vertical:");
        auto vert = CreateVerticalStepper("StVert", 0, 0, 320, 300);
        vert->SetSteps({
            StepItem("Select campaign", "Choose your target"),
            StepItem("Create ad group", "Pick keywords"),
            StepItem("Create an ad", "Write copy"),
            StepItem("Launch", "Review & publish"),
        });
        vert->SetCurrentStep(1);
        DemoKit::AddFixed(card3, vert);

        auto rightColumn = DemoKit::CreateColumn("StRightColumn", DemoKit::kPageGap);
        rightColumn->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                               .SetFlexBasis(CSSLayout::Dimension::Px(DemoKit::kCardMinWidth));
        cards->AddChild(rightColumn);

        // ===== 4. DOT STYLE (compact, non-linear) =====
        auto card4 = DemoKit::AddCard(rightColumn, "StL4", "Dot markers (compact, non-linear, no labels):");
        auto dots = std::make_shared<UltraCanvasStepper>("StDots", 0, 0, 320, 40);
        dots->SetSteps({StepItem(""), StepItem(""), StepItem(""), StepItem(""), StepItem("")});
        dots->SetMarkerStyle(StepMarkerStyle::Dot);
        dots->SetShowLabels(false);
        dots->SetNavigation(StepperNavigation::NonLinear);
        dots->SetCurrentStep(2);
        DemoKit::AddFixed(card4, dots);

        // ===== 5. NON-LINEAR CLICKABLE =====
        auto card5 = DemoKit::AddCard(rightColumn, "StL5", "Non-linear (click any step to jump):");
        auto nl = CreateStepperWizard("StNL", 0, 0, 520, 70,
                                      {"One", "Two", "Three", "Four"});
        nl->SetNavigation(StepperNavigation::NonLinear);
        nl->SetCurrentStep(1);
        DemoKit::AddStretched(card5, nl);

        auto nlStatus = AddReadout(card5, "StNLStatus", "Clicked step: (none)");
        nl->onStepChanged = [nlStatus, nl = nl.get()](int i) {
            StepItem* s = nl->GetStep(i);
            nlStatus->SetText("Jumped to step " + std::to_string(i + 1) +
                              (s ? ": " + s->title : ""));
        };

        // ===== INSTRUCTIONS =====
        DemoKit::AddNotes(page, "StepperInstructions", "API", {
                "AddStep(title, desc) / SetSteps({...}); SetCurrentStep / NextStep / PrevStep",
                "SetOrientation(Horizontal|Vertical); SetMarkerStyle(NumberedCircle|IconCheck|Dot|Custom)",
                "SetNavigation(Linear|NonLinear|Display); SetStepError / SetStepDisabled / SetStepIcon; onStepChanged"});

        return page;
    }

} // namespace UltraCanvas

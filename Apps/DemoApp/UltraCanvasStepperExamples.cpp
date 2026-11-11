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
        auto page = DemoKit::CreatePage("StepperExamples", Tr("Stepper / Wizard Examples"),
                Tr("A progress stepper for multi-step flows: completed / active / upcoming / error "
                "states, connectors, four marker styles, horizontal & vertical, linear & non-linear."));

        // ===== 1. HORIZONTAL WIZARD (linear) with Next/Prev =====
        auto card1 = DemoKit::AddCard(page, "StL1", Tr("Horizontal wizard (linear, numbered):"));
        auto wiz = CreateStepperWizard("StWiz", 0, 0, 620, 70,
                                       {Tr("Account"), Tr("Profile"), Tr("Payment"), Tr("Review"), Tr("Done")});
        DemoKit::AddStretched(card1, wiz);

        auto wizControls = DemoKit::AddRow(card1, "StWizControls", 10);
        auto prevBtn = MakeStepButton("StPrev", Tr("Back"));
        prevBtn->onClick = [wiz]() { wiz->PrevStep(true); };
        DemoKit::Add(wizControls, prevBtn);

        auto nextBtn = MakeStepButton("StNext", Tr("Next"));
        nextBtn->onClick = [wiz]() { wiz->NextStep(true); };
        DemoKit::Add(wizControls, nextBtn);

        auto status = AddReadout(wizControls, "StWizStatus", TrF("Step {0} of {1}: {2}", 1, 5, Tr("Account")));

        auto updateStatus = [wiz, status]() {
            int i = wiz->GetCurrentStep();
            StepItem* s = wiz->GetStep(i);
            status->SetText(s ? TrF("Step {0} of {1}: {2}", i + 1, wiz->GetStepCount(), s->title)
                              : TrF("Step {0} of {1}", i + 1, wiz->GetStepCount()));
        };
        wiz->onStepChanged = [updateStatus](int) { updateStatus(); };

        // ===== 2. HORIZONTAL WITH DESCRIPTIONS + ERROR STEP =====
        auto card2 = DemoKit::AddCard(page, "StL2", Tr("With descriptions and an error step:"));
        auto desc = std::make_shared<UltraCanvasStepper>("StDesc", 0, 0, 900, 80);
        desc->SetSteps({
            StepItem(Tr("Cart"), Tr("3 items")),
            StepItem(Tr("Shipping"), Tr("Address saved")),
            StepItem(Tr("Payment"), Tr("Card declined")),
            StepItem(Tr("Confirm"), Tr("Pending")),
        });
        desc->SetCurrentStep(2);
        desc->SetStepError(2, true);          // Payment failed
        DemoKit::AddStretched(card2, desc);

        auto cards = DemoKit::AddRow(page, "StepperCards");

        // ===== 3. VERTICAL STEPPER =====
        auto card3 = DemoKit::AddCard(cards, "StL3", Tr("Vertical:"));
        auto vert = CreateVerticalStepper("StVert", 0, 0, 320, 300);
        vert->SetSteps({
            StepItem(Tr("Select campaign"), Tr("Choose your target")),
            StepItem(Tr("Create ad group"), Tr("Pick keywords")),
            StepItem(Tr("Create an ad"), Tr("Write copy")),
            StepItem(Tr("Launch"), Tr("Review & publish")),
        });
        vert->SetCurrentStep(1);
        DemoKit::AddFixed(card3, vert);

        auto rightColumn = DemoKit::CreateColumn("StRightColumn", DemoKit::kPageGap);
        rightColumn->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                               .SetFlexBasis(CSSLayout::Dimension::Px(DemoKit::kCardMinWidth));
        cards->AddChild(rightColumn);

        // ===== 4. DOT STYLE (compact, non-linear) =====
        auto card4 = DemoKit::AddCard(rightColumn, "StL4", Tr("Dot markers (compact, non-linear, no labels):"));
        auto dots = std::make_shared<UltraCanvasStepper>("StDots", 0, 0, 320, 40);
        dots->SetSteps({StepItem(""), StepItem(""), StepItem(""), StepItem(""), StepItem("")});
        dots->SetMarkerStyle(StepMarkerStyle::Dot);
        dots->SetShowLabels(false);
        dots->SetNavigation(StepperNavigation::NonLinear);
        dots->SetCurrentStep(2);
        DemoKit::AddFixed(card4, dots);

        // ===== 5. NON-LINEAR CLICKABLE =====
        auto card5 = DemoKit::AddCard(rightColumn, "StL5", Tr("Non-linear (click any step to jump):"));
        auto nl = CreateStepperWizard("StNL", 0, 0, 520, 70,
                                      {Tr("One"), Tr("Two"), Tr("Three"), Tr("Four")});
        nl->SetNavigation(StepperNavigation::NonLinear);
        nl->SetCurrentStep(1);
        DemoKit::AddStretched(card5, nl);

        auto nlStatus = AddReadout(card5, "StNLStatus", Tr("Clicked step: (none)"));
        nl->onStepChanged = [nlStatus, nl = nl.get()](int i) {
            StepItem* s = nl->GetStep(i);
            nlStatus->SetText(s ? TrF("Jumped to step {0}: {1}", i + 1, s->title)
                                : TrF("Jumped to step {0}", i + 1));
        };

        // ===== INSTRUCTIONS =====
        DemoKit::AddNotes(page, "StepperInstructions", Tr("API"), {
                Tr("AddStep(title, desc) / SetSteps({...}); SetCurrentStep / NextStep / PrevStep"),
                Tr("SetOrientation(Horizontal|Vertical); SetMarkerStyle(NumberedCircle|IconCheck|Dot|Custom)"),
                Tr("SetNavigation(Linear|NonLinear|Display); SetStepError / SetStepDisabled / SetStepIcon; onStepChanged")});

        return page;
    }

} // namespace UltraCanvas

// Apps/DemoApp/UltraCanvasStepperExamples.cpp
// Demonstration of UltraCanvasStepper: horizontal numbered wizard driven by
// Next/Prev buttons, a version with descriptions and an error step, a vertical
// stepper, a compact dot style, and a non-linear (click-any-step) stepper.
// Version: 1.0.1
// Last Modified: 2026-07-11
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasStepper.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateStepperExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("StepperExamples", 0, 0, 1000, 800);
        container->SetPadding(0, 0, 10, 0);

        auto title = CreateLabel("StepperTitle", 20, 10, 0, 30);
        title->SetText("Stepper / Wizard Examples");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto subtitle = CreateLabel("StepperSubtitle", 20, 42, 940, 40);
        subtitle->SetText("A progress stepper for multi-step flows: completed / active / upcoming / error "
                          "states, connectors, four marker styles, horizontal & vertical, linear & non-linear.");
        subtitle->SetFontSize(12);
        container->AddChild(subtitle);

        auto sectionAt = [&](const std::string& id, const std::string& text, int x, int y) {
            auto l = CreateLabel(id, x, y, 520, 20);
            l->SetText(text);
            l->SetFontWeight(FontWeight::Bold);
            l->SetFontSize(12);
            container->AddChild(l);
        };
        auto section = [&](const std::string& id, const std::string& text, int y) {
            sectionAt(id, text, 20, y);
        };

        // ===== 1. HORIZONTAL WIZARD (linear) with Next/Prev =====
        section("StL1", "Horizontal wizard (linear, numbered):", 92);
        auto wiz = CreateStepperWizard("StWiz", 20, 118, 620, 70,
                                       {"Account", "Profile", "Payment", "Review", "Done"});
        container->AddChild(wiz);

        auto status = CreateLabel("StWizStatus", 660, 138, 300, 24);
        status->SetText("Step 1 of 5: Account");
        status->SetBackgroundColor(Color(240, 240, 240));
        status->SetPadding(3);
        container->AddChild(status);

        auto updateStatus = [wiz, status]() {
            int i = wiz->GetCurrentStep();
            StepItem* s = wiz->GetStep(i);
            status->SetText("Step " + std::to_string(i + 1) + " of " +
                            std::to_string(wiz->GetStepCount()) +
                            (s ? ": " + s->title : ""));
        };
        wiz->onStepChanged = [updateStatus](int) { updateStatus(); };

        auto prevBtn = std::make_shared<UltraCanvasButton>("StPrev", 660, 172, 90.0f, 30.0f, "Back");
        prevBtn->onClick = [wiz]() { wiz->PrevStep(true); };
        container->AddChild(prevBtn);

        auto nextBtn = std::make_shared<UltraCanvasButton>("StNext", 760, 172, 90.0f, 30.0f, "Next");
        nextBtn->onClick = [wiz]() { wiz->NextStep(true); };
        container->AddChild(nextBtn);

        // ===== 2. HORIZONTAL WITH DESCRIPTIONS + ERROR STEP =====
        section("StL2", "With descriptions and an error step:", 224);
        auto desc = std::make_shared<UltraCanvasStepper>("StDesc", 20, 250, 900, 80);
        desc->SetSteps({
            StepItem("Cart", "3 items"),
            StepItem("Shipping", "Address saved"),
            StepItem("Payment", "Card declined"),
            StepItem("Confirm", "Pending"),
        });
        desc->SetCurrentStep(2);
        desc->SetStepError(2, true);          // Payment failed
        container->AddChild(desc);

        // ===== 3. VERTICAL STEPPER =====
        section("StL3", "Vertical:", 360);
        auto vert = CreateVerticalStepper("StVert", 20, 386, 320, 300);
        vert->SetSteps({
            StepItem("Select campaign", "Choose your target"),
            StepItem("Create ad group", "Pick keywords"),
            StepItem("Create an ad", "Write copy"),
            StepItem("Launch", "Review & publish"),
        });
        vert->SetCurrentStep(1);
        container->AddChild(vert);

        // ===== 4. DOT STYLE (compact, non-linear) =====
        sectionAt("StL4", "Dot markers (compact, non-linear, no labels):", 400, 386);
        auto dots = std::make_shared<UltraCanvasStepper>("StDots", 400, 412, 320, 40);
        dots->SetSteps({StepItem(""), StepItem(""), StepItem(""), StepItem(""), StepItem("")});
        dots->SetMarkerStyle(StepMarkerStyle::Dot);
        dots->SetShowLabels(false);
        dots->SetNavigation(StepperNavigation::NonLinear);
        dots->SetCurrentStep(2);
        container->AddChild(dots);

        // ===== 5. NON-LINEAR CLICKABLE =====
        sectionAt("StL5", "Non-linear (click any step to jump):", 400, 470);
        auto nl = CreateStepperWizard("StNL", 400, 496, 520, 70,
                                      {"One", "Two", "Three", "Four"});
        nl->SetNavigation(StepperNavigation::NonLinear);
        nl->SetCurrentStep(1);
        container->AddChild(nl);

        auto nlStatus = CreateLabel("StNLStatus", 400, 572, 520, 24);
        nlStatus->SetText("Clicked step: (none)");
        nlStatus->SetBackgroundColor(Color(240, 240, 240));
        nlStatus->SetPadding(3);
        container->AddChild(nlStatus);
        nl->onStepChanged = [nlStatus, nl](int i) {
            StepItem* s = nl->GetStep(i);
            nlStatus->SetText("Jumped to step " + std::to_string(i + 1) +
                              (s ? ": " + s->title : ""));
        };

        // ===== INSTRUCTIONS =====
        auto instructions = CreateLabel("StepperInstructions", 20, 700, 940, 84);
        instructions->SetText(
                "API:\n"
                "\xE2\x80\xA2 AddStep(title, desc) / SetSteps({...}); SetCurrentStep / NextStep / PrevStep\n"
                "\xE2\x80\xA2 SetOrientation(Horizontal|Vertical); SetMarkerStyle(NumberedCircle|IconCheck|Dot|Custom)\n"
                "\xE2\x80\xA2 SetNavigation(Linear|NonLinear|Display); SetStepError / SetStepDisabled / SetStepIcon; onStepChanged");
        instructions->SetFontSize(11);
        instructions->SetBackgroundColor(Color(255, 255, 240));
        instructions->SetPadding(6);
        container->AddChild(instructions);

        return container;
    }

} // namespace UltraCanvas

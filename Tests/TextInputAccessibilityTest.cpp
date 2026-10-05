// Tests/TextInputAccessibilityTest.cpp
// What a text field tells assistive technology about itself.
//
// UltraCanvasTextInput described nothing: no role, so the bridges showed it as
// an unknown element, and the Windows bridge answered UI Automation's
// IsPassword with a constant false. A screen reader then treated every
// password field - UltraPassword's master password and entry fields among
// them - as plain text and spoke each character typed into it. A text field
// now says it is one, and a password field that its content is a secret,
// which the bridges turn into IsPassword (UI Automation) and the password
// text role (AT-SPI).
//
// Runs headless: only the element's answers are checked, no window or bridge.
// Version: 1.0.0
// Last Modified: 2026-10-05
// Author: UltraCanvas Framework

#include "UltraCanvasAccessibility.h"
#include "UltraCanvasTextInput.h"

#include <iostream>
#include <memory>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Text Input Accessibility Suite"        << std::endl;
    std::cerr << "========================================" << std::endl;

    std::cerr << "\n--- A plain field ---" << std::endl;
    {
        auto field = CreateTextInput("plain", 0, 0, 200, 24);
        TEST("is a text field", field->GetAccessibleRole() == AccessibleRole::TextField);
        TEST("and not a password field", !field->IsAccessiblePassword());
    }

    std::cerr << "\n--- A password field ---" << std::endl;
    {
        auto field = CreatePasswordInput("secret", 0, 0, 200, 24);
        field->SetText("hunter2");
        TEST("is a text field", field->GetAccessibleRole() == AccessibleRole::TextField);
        TEST("and a password field", field->IsAccessiblePassword());
        TEST("whose text is not handed out", field->GetAccessibleTextInterface() == nullptr);
        field->SetPasswordRevealed(true);
        TEST("still a password field while its text is shown", field->IsAccessiblePassword());
        field->SetInputType(TextInputType::Text);
        TEST("not one once it is switched to plain text", !field->IsAccessiblePassword());
        field->SetInputType(TextInputType::Password);
        TEST("and one again when switched back", field->IsAccessiblePassword());
    }

    std::cerr << "\n--- Fields built on the text input ---" << std::endl;
    {
        auto field = CreateRevealablePasswordInput("revealable", 0, 0, 200, 24);
        TEST("The revealable password input is a password field", field->IsAccessiblePassword());
    }

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}

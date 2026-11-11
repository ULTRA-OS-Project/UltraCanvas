// Tests/LabelNaturalTextWidthTest.cpp
// UltraCanvasLabel::GetNaturalTextWidth: the width a label's text takes, not
// the box it was given - what an application needs to put something right
// after the text (UltraAuthenticator's copy button after a code). Checks that
// it measures the text rather than the box, grows with the text, ignores
// padding, agrees with where the label itself says its text ends
// (TextIndexAtPoint), and answers 0 for empty text and for a label that
// cannot reach a render context yet.
//
// Opens a real window (the text layout needs a render context), so it runs
// under Xvfb (xvfb-run -a) and skips itself without a DISPLAY.
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasWindow.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

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

#define SKIP_ALL(reason)                                                      \
    do {                                                                      \
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Label Natural Text Width Suite"       << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("LabelNaturalTextWidthTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "LabelNaturalTextWidthTest";
    cfg.width = 600;
    cfg.height = 300;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();

    // A label not in any window has no render context to measure with.
    auto loose = std::make_shared<UltraCanvasLabel>("loose", 0, 0, 400, 36, "123 456");
    TEST("a label outside a window measures 0", loose->GetNaturalTextWidth() == 0.0f);

    // A code in a monospaced face, in a box far wider than the text.
    auto code = std::make_shared<UltraCanvasLabel>("code", 20, 20, 400, 40, "123 456");
    code->SetFont("Monospace", 28.0f, FontWeight::Bold);
    window->AddChild(code);
    const float six = code->GetNaturalTextWidth();
    std::cerr << "  '123 456' measures " << six << " px in a 400 px box" << std::endl;
    TEST("the text is measured, not the box", six > 0.0f && six < 400.0f);

    // The label's own hit test (the nearest text position) agrees that the
    // last character ends at the measured width: a point in its left half is
    // still before it, one just past the width is the end of the text.
    const int len = static_cast<int>(code->GetText().size());
    const float y = code->GetHeight() / 2.0f;
    const float charWidth = six / static_cast<float>(len);
    const int inLastChar = code->TextIndexAtPoint(Point2Df(six - charWidth * 0.75f, y));
    const int past = code->TextIndexAtPoint(Point2Df(six + 2.0f, y));
    TEST("the last character's left half is before it", inLastChar == len - 1);
    TEST("just past the width is the end of the text", past == len);

    // Longer text, wider; in a monospaced face in proportion to its length.
    code->SetText("1234 5678");
    const float eight = code->GetNaturalTextWidth();
    std::cerr << "  '1234 5678' measures " << eight << " px" << std::endl;
    TEST("a longer text is wider", eight > six);
    TEST("monospaced: 9 characters are 9/7 of 7",
         std::fabs(eight / six - 9.0f / 7.0f) < 0.05f);

    // Padding moves the text, it does not widen it.
    code->SetPadding(12.0f);
    TEST("padding does not change the text's width",
         std::fabs(code->GetNaturalTextWidth() - eight) < 0.5f);

    code->SetText("");
    TEST("an empty text measures 0", code->GetNaturalTextWidth() == 0.0f);

    std::cerr << std::endl << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}

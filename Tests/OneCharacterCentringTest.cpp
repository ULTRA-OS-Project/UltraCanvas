// Tests/OneCharacterCentringTest.cpp
// A centred one-character text sits in the middle of its element. Drawn
// offscreen and read back, the ink of the text is measured against the
// middle of what it is centred in:
//
//  1. A button's one-character label - "+", "×", "‹", "✓" - is centred on
//     its ink, across and down: a round "add" button with a "+" drew it
//     2.5 px low (UltraToDo), because the line box the text was centred on
//     holds the font's ascender and descender room, and a lone glyph sits
//     where the font puts it in that box.
//  2. A word on a button stays centred on its capitals ("Done", "OK").
//  3. The same for a label centred both ways ("×" was 2 px low in 32 px,
//     1.5 px in UltraDesktop's 18 px close mark), a badge (in its own
//     pill, which it sizes to the text) and a segment of a segmented
//     control (a pager's "‹" and "›").
//
// The rule is one helper, DrawTextCentredOnInk (UltraCanvasTextLayoutCache.h).
//
// Usage: OneCharacterCentringTest
// Exit code is the number of failed checks.
// Version: 2.0.0 - the label, the badge and the segmented control too (was ButtonTextCentringTest)
// Version: 1.0.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework

#include "UltraCanvasBadge.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

// Where the ink is: the box of every pixel darker than the white face.
struct Ink {
    int left = 1 << 30, top = 1 << 30, right = -1, bottom = -1;
    bool Empty() const { return right < 0; }
    double CentreX() const { return (left + right + 1) / 2.0; }
    double CentreY() const { return (top + bottom + 1) / 2.0; }
};

std::shared_ptr<UltraCanvasButton> MakeButton(const std::string& text, int w, int h, float fontSize) {
    auto button = std::make_shared<UltraCanvasButton>("button", 0, 0, w, h, text);
    button->SetPadding(0);
    ButtonStyle style = button->GetStyle();
    style.normalColor = Colors::White;
    style.normalTextColor = Colors::Black;
    style.borderWidth = 0;
    style.hasShadow = false;
    style.cornerRadius = 0;
    style.fontSize = fontSize;
    button->SetStyle(style);
    CSSLayout::LayoutContext lctx;
    button->Arrange(Rect2Df(0, 0, static_cast<float>(w), static_cast<float>(h)), lctx);
    return button;
}

// Draws `element` in a white w x h canvas. `ink`: the pixels darker than
// 160 (the text) in columns [x0, x1); `face`: those darker than 250 (the
// text and a face that is not white, a badge's pill).
struct Drawn {
    Ink ink, face;
};

Drawn DrawElement(UltraCanvasUIElement& element, int w, int h, int x0, int x1) {
    CSSLayout::LayoutContext lctx;
    element.Arrange(Rect2Df(0, 0, static_cast<float>(w), static_cast<float>(h)), lctx);
    auto ctx = CreateRenderContext(Size2Di(w, h), nullptr);
    ctx->Clear(Color(255, 255, 255, 255));
    element.Render(ctx.get(), Rect2Df(0, 0, static_cast<float>(w), static_cast<float>(h)));
    UCPixmap pixmap;
    pixmap.Init(w, h);
    ctx->FlushToSurface(pixmap.GetSurface(), Point2Dd(0, 0));
    pixmap.MarkDirty();
    pixmap.Flush();
    Drawn d;
    auto add = [](Ink& box, int x, int y) {
        box.left = std::min(box.left, x);
        box.right = std::max(box.right, x);
        box.top = std::min(box.top, y);
        box.bottom = std::max(box.bottom, y);
    };
    for (int y = 0; y < h; ++y) {
        for (int x = x0; x < x1; ++x) {
            const int green = static_cast<int>((pixmap.GetPixel(x, y) >> 8) & 0xFF);
            if (green < 250) add(d.face, x, y);
            if (green <= 160) add(d.ink, x, y);   // the text, not the faintest antialiasing
        }
    }
    return d;
}

Ink Draw(UltraCanvasButton& button, int w, int h) {
    return DrawElement(button, w, h, 0, w).ink;
}

std::string Off(const Ink& ink, int w, int h) {
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "(ink centre %.1f, %.1f; the button's %.1f, %.1f)",
                  ink.CentreX(), ink.CentreY(), w / 2.0, h / 2.0);
    return buffer;
}

void TestSymbol(const std::string& text, int size, float fontSize) {
    const Ink ink = Draw(*MakeButton(text, size, size, fontSize), size, size);
    Check(!ink.Empty(), "\"" + text + "\" is drawn");
    if (ink.Empty()) return;
    const bool across = std::fabs(ink.CentreX() - size / 2.0) <= 1.0;
    const bool down = std::fabs(ink.CentreY() - size / 2.0) <= 1.0;
    Check(across && down, "\"" + text + "\" in a " + std::to_string(size) + " px button is centred " +
                              Off(ink, size, size));
}

void TestWord(const std::string& text, int w, int h, float fontSize) {
    const Ink ink = Draw(*MakeButton(text, w, h, fontSize), w, h);
    Check(!ink.Empty(), "\"" + text + "\" is drawn");
    if (ink.Empty()) return;
    const bool across = std::fabs(ink.CentreX() - w / 2.0) <= 1.5;
    const bool down = std::fabs(ink.CentreY() - h / 2.0) <= 1.0;
    Check(across && down, "\"" + text + "\" in " + std::to_string(w) + " x " + std::to_string(h) +
                              " is centred on its capitals " + Off(ink, w, h));
}

// The ink's centre within one pixel of (cx, cy).
void CheckCentred(const std::string& what, const Ink& ink, double cx, double cy) {
    if (ink.Empty()) {
        Check(false, what + ": drawn");
        return;
    }
    const bool ok = std::fabs(ink.CentreX() - cx) <= 1.0 && std::fabs(ink.CentreY() - cy) <= 1.0;
    char where[96];
    std::snprintf(where, sizeof(where), " (ink centre %.1f, %.1f; the middle %.1f, %.1f)", ink.CentreX(),
                  ink.CentreY(), cx, cy);
    Check(ok, what + where);
}

void TestOtherElements(const std::string& text) {
    const std::string quoted = "\"" + text + "\"";
    {
        auto label = std::make_shared<UltraCanvasLabel>("label", 0, 0, 32, 32, text);
        label->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        label->SetFontSize(18);
        label->SetTextColor(Colors::Black);
        CheckCentred("a label of " + quoted + " in 32 x 32", DrawElement(*label, 32, 32, 0, 32).ink, 16, 16);
    }
    {
        // A badge draws its pill at the size it measures for the text: the
        // text is centred in the pill, wherever that ends.
        auto badge = CreateBadge("badge", 0, 0, text);
        BadgeStyle style = badge->GetStyle();
        style.height = 24;
        style.minWidth = 24;
        style.fontSize = 16;
        style.textColor = Colors::Black;
        style.borderWidth = 0;
        badge->SetStyle(style);
        badge->SetColor(Color(210, 210, 210));
        const Drawn d = DrawElement(*badge, 40, 24, 0, 40);
        CheckCentred("a badge of " + quoted + " in its pill", d.ink, d.face.CentreX(), d.face.CentreY());
    }
    {
        auto control = CreateSegmentedControl("pager", 0, 0, 80, 32);
        SegmentedControlStyle style = control->GetStyle();
        style.normalColor = style.selectedColor = style.hoverColor = Colors::White;
        style.normalTextColor = style.selectedTextColor = style.hoverTextColor = Colors::Black;
        style.borderWidth = 0;
        style.separatorWidth = 0;
        style.fontSize = 18;
        control->SetStyle(style);
        control->AddSegment(text);
        control->AddSegment(text);
        const Drawn first = DrawElement(*control, 80, 32, 0, 40);
        const Drawn second = DrawElement(*control, 80, 32, 40, 80);
        CheckCentred("a segment of " + quoted + " (the first of two in 80 x 32)", first.ink, 20, 16);
        CheckCentred("a segment of " + quoted + " (the second)", second.ink, 60, 16);
    }
}

} // namespace

int main() {
    UCImage::InitializeImageSubsysterm("OneCharacterCentringTest");
    std::printf("1. a one-character label is centred on its ink\n");
    TestSymbol("+", 48, 26.0f);
    TestSymbol("+", 28, 14.0f);
    TestSymbol("\xC3\x97", 40, 22.0f);           // ×
    TestSymbol("\xE2\x80\xB9", 32, 18.0f);       // ‹
    TestSymbol("\xE2\x9C\x93", 32, 18.0f);       // ✓
    std::printf("2. a word stays centred on its capitals\n");
    TestWord("Done", 100, 28, 12.0f);
    TestWord("OK", 80, 32, 14.0f);
    std::printf("3. a label, a badge and a segment centre one character the same way\n");
    TestOtherElements("+");
    TestOtherElements("\xC3\x97");          // ×
    TestOtherElements("\xE2\x80\xB9");      // ‹
    {
        // UltraDesktop's window-preview close mark, as it builds it.
        auto close = CreateLabel("close", 0, 0, 18, 18, "\xC3\x97");
        close->SetFontSize(12.0f);
        close->SetTextColor(Colors::Black);
        close->SetAlignment(TextAlignment::Center);
        CheckCentred("UltraDesktop's 18 px close mark", DrawElement(*close, 18, 18, 0, 18).ink, 9, 9);
    }
    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}

// Tests/ButtonTextCentringTest.cpp
// A button's label sits in the middle of the button. Drawn offscreen and
// read back, the ink of the label is measured against the button's middle:
//
//  1. A one-character label - "+", "×", "‹", "✓" - is centred on its ink,
//     across and down: a round "add" button with a "+" drew it about 3 px
//     low and 1 px left of the circle's centre (UltraToDo), because the
//     line box the text was centred on holds the font's ascender and
//     descender room, and the glyph sits where the font puts it in it.
//  2. A word stays centred down its capitals ("Done", "OK"): the ink of a
//     word without descenders is within a pixel of the middle.
//  3. A centred icon is centred (the icon path, for comparison).
//
// Usage: ButtonTextCentringTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework

#include "UltraCanvasButton.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>
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

Ink Draw(UltraCanvasButton& button, int w, int h) {
    auto ctx = CreateRenderContext(Size2Di(w, h), nullptr);
    ctx->Clear(Color(255, 255, 255, 255));
    button.Render(ctx.get(), Rect2Df(0, 0, static_cast<float>(w), static_cast<float>(h)));
    UCPixmap pixmap;
    pixmap.Init(w, h);
    ctx->FlushToSurface(pixmap.GetSurface(), Point2Dd(0, 0));
    pixmap.MarkDirty();
    pixmap.Flush();
    Ink ink;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int green = static_cast<int>((pixmap.GetPixel(x, y) >> 8) & 0xFF);
            if (green > 160) continue;   // the face, and the faintest antialiasing
            ink.left = std::min(ink.left, x);
            ink.right = std::max(ink.right, x);
            ink.top = std::min(ink.top, y);
            ink.bottom = std::max(ink.bottom, y);
        }
    }
    return ink;
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

} // namespace

int main() {
    UCImage::InitializeImageSubsysterm("ButtonTextCentringTest");
    std::printf("1. a one-character label is centred on its ink\n");
    TestSymbol("+", 48, 26.0f);
    TestSymbol("+", 28, 14.0f);
    TestSymbol("\xC3\x97", 40, 22.0f);           // ×
    TestSymbol("\xE2\x80\xB9", 32, 18.0f);       // ‹
    TestSymbol("\xE2\x9C\x93", 32, 18.0f);       // ✓
    std::printf("2. a word stays centred on its capitals\n");
    TestWord("Done", 100, 28, 12.0f);
    TestWord("OK", 80, 32, 14.0f);
    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}

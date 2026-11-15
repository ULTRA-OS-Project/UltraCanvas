// Tests/LightTextBoostTest.cpp
// Light text on a dark ground is drawn firmer. Cairo blends a glyph's edge in
// sRGB numbers, so light text loses weight on a dark window - a dark
// UltraFiler read thinner and greyer than the Explorer window beside it -
// and the render context draws text whose colour is light a second time
// (RenderContextCairo::SetLightTextBoost, on by default), which lifts the
// partly covered pixels and leaves the covered ones, the extents and the
// layout as they are.
//
// A line is drawn offscreen and read back: light ink on a dark ground
// carries more light with the boost than without and covers the same
// pixels; dark ink and translucent ink are drawn exactly as before.
//
// Usage: LightTextBoostTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-11-15
// Author: UltraCanvas Framework

#include "UltraCanvasImage.h"
#include "UltraCanvasRenderContext.h"
#include "../libspecific/Cairo/RenderContextCairo.h"

#include <cstdio>
#include <string>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

struct Ink {
    long sum = 0;        // the green channel's distance from the ground, summed
    int touched = 0;     // pixels that are not the ground
    int left = 9999, right = -1, top = 9999, bottom = -1;
    int brightest = 0, darkest = 255;
};

const int kWidth = 240;
const int kHeight = 40;

Ink Draw(const Color& ground, const Color& ink, double boost) {
    RenderContextCairo::SetLightTextBoost(boost);
    auto ctx = CreateRenderContext(Size2Di(kWidth, kHeight), nullptr);
    ctx->Clear(ground);
    FontStyle font;
    font.fontFamily = "Ubuntu";
    font.fontSize = 9.0;
    ctx->SetFontStyle(font);
    ctx->SetTextPaint(ink);
    ctx->DrawText("Mazah Work Videos illumination", Point2Dd(6, 10));
    UCPixmap pixmap;
    pixmap.Init(kWidth, kHeight);
    ctx->FlushToSurface(pixmap.GetSurface(), Point2Dd(0, 0));
    pixmap.MarkDirty();
    pixmap.Flush();
    Ink result;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const int green = static_cast<int>((pixmap.GetPixel(x, y) >> 8) & 0xFF);
            if (green > result.brightest) result.brightest = green;
            if (green < result.darkest) result.darkest = green;
            if (green == ground.g) continue;
            result.sum += green > ground.g ? green - ground.g : ground.g - green;
            ++result.touched;
            if (x < result.left) result.left = x;
            if (x > result.right) result.right = x;
            if (y < result.top) result.top = y;
            if (y > result.bottom) result.bottom = y;
        }
    }
    return result;
}

bool SameBox(const Ink& a, const Ink& b) {
    return a.left == b.left && a.right == b.right && a.top == b.top && a.bottom == b.bottom;
}

} // namespace

int main() {
    const Color dark(30, 30, 30);
    const Color white(255, 255, 255);
    const Color lightInk(255, 255, 255);
    const Color darkInk(0, 0, 0);

    std::printf("the strength for an ink\n");
    Check(RenderContextCairo::GetLightTextBoost() == 1.0, "the boost is on by default");
    Check(RenderContextCairo::LightTextBoostFor(lightInk) == 1.0, "the dark theme's ink takes the whole boost");
    Check(RenderContextCairo::LightTextBoostFor(Color(190, 190, 190)) == 1.0, "its muted ink too");
    const double faint = RenderContextCairo::LightTextBoostFor(Color(150, 150, 150));
    Check(faint > 0.3 && faint < 1.0, "its faint ink takes part of it");
    Check(RenderContextCairo::LightTextBoostFor(darkInk) == 0.0, "black ink takes none");
    Check(RenderContextCairo::LightTextBoostFor(Color(96, 96, 96)) == 0.0, "nor a dark grey");
    Check(RenderContextCairo::LightTextBoostFor(Color(235, 235, 235, 128)) == 0.0, "nor translucent light ink");
    RenderContextCairo::SetLightTextBoost(0.5);
    Check(RenderContextCairo::LightTextBoostFor(lightInk) == 0.5, "a halved boost is halved for light ink");
    RenderContextCairo::SetLightTextBoost(7.0);
    Check(RenderContextCairo::GetLightTextBoost() == 1.0, "a strength above one is one");

    std::printf("light ink on a dark ground\n");
    const Ink plain = Draw(dark, lightInk, 0.0);
    const Ink firm = Draw(dark, lightInk, 1.0);
    Check(plain.touched > 200, "the line was drawn (" + std::to_string(plain.touched) + " pixels)");
    Check(firm.sum > plain.sum * 115 / 100,
          "it carries at least 15 % more light (" + std::to_string(plain.sum) + " -> " + std::to_string(firm.sum) + ")");
    Check(firm.sum < plain.sum * 2, "and less than a bold would");
    Check(firm.touched == plain.touched && SameBox(firm, plain), "it covers the same pixels: no extent changed");
    Check(firm.brightest <= lightInk.g, "no pixel is lighter than the ink");

    std::printf("ink the boost leaves alone\n");
    const Ink blackPlain = Draw(white, darkInk, 0.0);
    const Ink blackFirm = Draw(white, darkInk, 1.0);
    Check(blackPlain.touched > 200 && blackFirm.sum == blackPlain.sum, "dark ink on white is drawn as before");
    const Color veiled(235, 235, 235, 128);
    const Ink veiledPlain = Draw(dark, veiled, 0.0);
    const Ink veiledFirm = Draw(dark, veiled, 1.0);
    Check(veiledPlain.touched > 200 && veiledFirm.sum == veiledPlain.sum, "translucent light ink is drawn as before");

    RenderContextCairo::SetLightTextBoost(1.0);
    std::printf("%d failed\n", failures);
    return failures;
}

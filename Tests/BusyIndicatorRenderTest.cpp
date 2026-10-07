// Tests/BusyIndicatorRenderTest.cpp
// UltraCanvasBusyIndicator drawn into an offscreen surface: the DualRing's
// second colour and the DotRing's three fades.
//
// There is no application here, so Start() does nothing and every indicator
// rests at the start of its cycle. hideWhenStopped = false makes it draw
// anyway, which pins the pictures down: the DualRing's outer arc starts at 12
// o'clock and the inner one at 6, and the DotRing's head sits on dot 0 (12
// o'clock), with dot 1 (the next one clockwise) the one about to fade in.
// Version: 1.0.0
// Last Modified: 2026-10-04
// Author: UltraCanvas Framework

#include "UltraCanvasBusyIndicator.h"
#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

// A white surface to draw one indicator into, read back pixel by pixel.
struct Canvas {
    std::unique_ptr<IRenderContext> ctx;
    cairo_t* cr = nullptr;

    Canvas(int w, int h) {
        ctx = CreateRenderContext(Size2Di(w, h), nullptr);
        if (!ctx) return;
        cr = static_cast<cairo_t*>(ctx->GetNativeContext());
        if (!cr) return;
        cairo_save(cr);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    // The opaque colour at (x, y); the surface is painted white first, so
    // every pixel is opaque and premultiplying changes nothing.
    Color At(double x, double y) const {
        cairo_surface_t* s = cairo_get_target(cr);
        cairo_surface_flush(s);
        const unsigned char* data = cairo_image_surface_get_data(s);
        const int stride = cairo_image_surface_get_stride(s);
        const auto px = *reinterpret_cast<const uint32_t*>(
                data + static_cast<int>(std::lround(y)) * stride + static_cast<int>(std::lround(x)) * 4);
        return Color((px >> 16) & 0xFF, (px >> 8) & 0xFF, px & 0xFF, 255);
    }
};

bool Near(const Color& a, const Color& b, int tolerance = 12) {
    return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance &&
           std::abs(a.b - b.b) <= tolerance;
}

bool IsWhite(const Color& c) { return Near(c, Color(255, 255, 255), 2); }

const Color kBlue(0, 0, 255);
const Color kRed(255, 0, 0);

std::shared_ptr<UltraCanvasBusyIndicator> MakeStill(float side, BusyIndicatorKind kind) {
    auto busy = CreateBusyIndicator("busy", 0, 0, side, side, kind);
    BusyIndicatorStyle style = busy->GetStyle();
    style.arcColor = kBlue;
    style.trackColor = Color(0, 0, 0, 0);
    style.hideWhenStopped = false;
    busy->SetStyle(style);
    return busy;
}

void TestDualRingColours() {
    std::cout << "\nA DualRing\n";
    Canvas canvas(48, 48);
    if (!canvas.cr) { Check(false, "no context"); return; }
    auto busy = MakeStill(48, BusyIndicatorKind::DualRing);
    BusyIndicatorStyle style = busy->GetStyle();
    style.secondArcColor = kRed;
    busy->SetStyle(style);
    busy->Render(canvas.ctx.get(), Rect2Df(0, 0, 48, 48));

    // Stroke 48 / 8 = 6; outer radius (48 - 6) / 2 = 21, inner 21 - 9 = 12.
    // Each arc is 135 degrees: the outer one from 12 o'clock past 3, the
    // inner one from 6 o'clock past 9.
    Check(Near(canvas.At(24 + 21, 24), kBlue), "draws the outer arc in arcColor");
    Check(Near(canvas.At(24 - 12, 24), kRed), "and the inner arc in secondArcColor");
}

// DotRing geometry for a 64 px box with the default 8 dots: ring radius
// 32 / (1 + 0.75 sin(pi/8)), dot i at 12 o'clock plus i eighths of a turn.
double RingRadius() { return 32.0 / (1.0 + 0.75 * std::sin(3.14159265358979 / 8.0)); }
double DotX(int i) { return 32.0 + RingRadius() * std::cos(i * 3.14159265358979 / 4.0 - 3.14159265358979 / 2.0); }
double DotY(int i) { return 32.0 + RingRadius() * std::sin(i * 3.14159265358979 / 4.0 - 3.14159265358979 / 2.0); }

void RenderDotRing(Canvas& canvas, BusyDotRingFade fade, UltraCanvasBusyIndicator* reuse = nullptr) {
    std::shared_ptr<UltraCanvasBusyIndicator> owned;
    UltraCanvasBusyIndicator* busy = reuse;
    if (!busy) {
        owned = MakeStill(64, BusyIndicatorKind::DotRing);
        busy = owned.get();
    }
    BusyIndicatorStyle style = busy->GetStyle();
    style.dotRingFade = fade;
    busy->SetStyle(style);
    busy->Render(canvas.ctx.get(), Rect2Df(0, 0, 64, 64));
}

void TestDotRingNoFade() {
    std::cout << "\nA DotRing with NoFade\n";
    Canvas canvas(64, 64);
    if (!canvas.cr) { Check(false, "no context"); return; }
    RenderDotRing(canvas, BusyDotRingFade::NoFade);

    bool allBlue = true;
    for (int i = 0; i < 8; ++i) allBlue = allBlue && Near(canvas.At(DotX(i), DotY(i)), kBlue);
    Check(allBlue, "draws every dot in full arcColor");
    Check(IsWhite(canvas.At(DotX(1) + 5, DotY(1))) && !IsWhite(canvas.At(DotX(0) + 5, DotY(0))),
          "and shows the head by size: the dot under it is larger than the one ahead");
}

void TestDotRingFade() {
    std::cout << "\nA DotRing with Fade\n";
    Canvas canvas(64, 64);
    if (!canvas.cr) { Check(false, "no context"); return; }
    RenderDotRing(canvas, BusyDotRingFade::Fade);

    const Color head = canvas.At(DotX(0), DotY(0));
    const Color behind = canvas.At(DotX(7), DotY(7));
    const Color far = canvas.At(DotX(4), DotY(4));
    Check(Near(head, kBlue), "draws the dot under the head in full arcColor");
    Check(behind.r > head.r && behind.r < far.r, "fades the dots behind it, more the further back");
    Check(!IsWhite(far), "keeps the far side of the ring visible");
    Check(IsWhite(canvas.At(DotX(1), DotY(1))), "and has faded out the dot about to fade back in");
}

void TestDotRingRandomColour() {
    std::cout << "\nA DotRing with FadeRandomColor\n";
    Canvas first(64, 64), second(64, 64);
    if (!first.cr || !second.cr) { Check(false, "no context"); return; }
    auto busy = MakeStill(64, BusyIndicatorKind::DotRing);
    RenderDotRing(first, BusyDotRingFade::FadeRandomColor, busy.get());
    RenderDotRing(second, BusyDotRingFade::FadeRandomColor, busy.get());

    const Color head = first.At(DotX(0), DotY(0));
    const int spread = std::max({head.r, head.g, head.b}) - std::min({head.r, head.g, head.b});
    Check(spread > 120, "draws the head dot in a saturated colour, not grey");
    Check(IsWhite(first.At(DotX(1), DotY(1))), "fades out like Fade");
    bool same = true;
    for (int i = 0; i < 8; ++i) same = same && Near(first.At(DotX(i), DotY(i)), second.At(DotX(i), DotY(i)), 0);
    Check(same, "and keeps each dot's colour from frame to frame until it fades back in");
}

} // namespace

int main() {
    std::cout << "===== Busy indicator rendering =====\n";
    TestDualRingColours();
    TestDotRingNoFade();
    TestDotRingFade();
    TestDotRingRandomColour();
    std::cout << "\n" << (g_failures ? "FAILED" : "PASSED") << " ("
              << g_failures << " failure" << (g_failures == 1 ? "" : "s") << ")\n";
    return g_failures == 0 ? 0 : 1;
}

// Tests/RenderAlphaTest.cpp
// IRenderContext::SetAlpha fades everything drawn after it.
//
// The Cairo context applied the global alpha to images only: a fill, a
// stroke and text reset the source to their colour at its own alpha, so a
// caller dimming a whole row (the file display's dimmed entries, a drag
// ghost, a slideshow transition, a vector group's opacity) got a faded
// picture next to full-strength text and shapes. A colour's alpha is now
// scaled by the global alpha, and a gradient is painted through a group laid
// down at it.
//
// Draws into an offscreen surface and reads the pixels back; needs no display.
// Version: 1.0.1
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

// A white surface, read back pixel by pixel.
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

    // The surface is opaque white underneath, so every pixel is opaque.
    Color At(int x, int y) const {
        cairo_surface_t* s = cairo_get_target(cr);
        cairo_surface_flush(s);
        const unsigned char* data = cairo_image_surface_get_data(s);
        const int stride = cairo_image_surface_get_stride(s);
        const auto px = *reinterpret_cast<const uint32_t*>(data + y * stride + x * 4);
        return Color((px >> 16) & 0xFF, (px >> 8) & 0xFF, px & 0xFF, 255);
    }

    // The darkest grey level inside a rectangle (0 black .. 255 white),
    // clipped to the surface.
    int Darkest(int x0, int y0, int x1, int y1) const {
        cairo_surface_t* s = cairo_get_target(cr);
        x1 = std::min(x1, cairo_image_surface_get_width(s));
        y1 = std::min(y1, cairo_image_surface_get_height(s));
        int darkest = 255;
        for (int y = std::max(y0, 0); y < y1; ++y)
            for (int x = std::max(x0, 0); x < x1; ++x) {
                const Color c = At(x, y);
                darkest = std::min(darkest, (c.r + c.g + c.b) / 3);
            }
        return darkest;
    }
};

bool Near(int value, int expected, int tolerance = 6) {
    return std::abs(value - expected) <= tolerance;
}

void Report(const char* what, int value) {
    std::cout << "         " << what << ": " << value << "\n";
}

} // namespace

int main() {
    std::cout << "RenderAlphaTest\n";
    Canvas canvas(200, 220);
    if (!canvas.ctx || !canvas.cr) {
        std::cout << "SKIP: no offscreen render context\n";
        return 0;
    }
    IRenderContext* ctx = canvas.ctx.get();

    // A black fill at half alpha over white: mid grey.
    ctx->PushState();
    ctx->SetAlpha(0.5);
    ctx->SetFillPaint(Colors::Black);
    ctx->FillRectangle(Rect2Dd(10, 10, 30, 30));
    ctx->PopState();
    const Color fill = canvas.At(25, 25);
    Report("fill at 0.5", fill.r);
    Check(Near(fill.r, 128) && fill.r == fill.g && fill.g == fill.b, "a fill is faded by SetAlpha");

    // PopState restores full strength.
    ctx->SetFillPaint(Colors::Black);
    ctx->FillRectangle(Rect2Dd(50, 10, 30, 30));
    Check(canvas.At(65, 25).r == 0, "PopState puts the alpha back to one");

    // A colour's own alpha and the global alpha multiply: 0.5 x 0.5.
    ctx->PushState();
    ctx->SetAlpha(0.5);
    ctx->SetFillPaint(Color(0, 0, 0, 128));
    ctx->FillRectangle(Rect2Dd(90, 10, 30, 30));
    ctx->PopState();
    const Color both = canvas.At(105, 25);
    Report("fill alpha 0.5 at 0.5", both.r);
    Check(Near(both.r, 191), "a colour's alpha and the global alpha multiply");

    // A stroke.
    ctx->PushState();
    ctx->SetAlpha(0.5);
    ctx->SetStrokePaint(Colors::Black);
    ctx->SetStrokeWidth(8.0);
    ctx->DrawLine(Point2Dd(130, 25), Point2Dd(190, 25));
    ctx->PopState();
    const Color stroke = canvas.At(160, 25);
    Report("stroke at 0.5", stroke.r);
    Check(Near(stroke.r, 128), "a stroke is faded by SetAlpha");

    // A gradient (a pattern, faded through a group): black to black.
    ctx->PushState();
    ctx->SetAlpha(0.5);
    ctx->SetFillPaint(ctx->CreateLinearGradientPattern(10, 0, 40, 0,
            {GradientStop(0.0, Colors::Black), GradientStop(1.0, Colors::Black)}));
    ctx->FillRectangle(Rect2Dd(10, 50, 30, 30));
    ctx->PopState();
    const Color gradient = canvas.At(25, 65);
    Report("gradient at 0.5", gradient.r);
    Check(Near(gradient.r, 128), "a gradient fill is faded by SetAlpha");

    // Text: full strength reaches black, a quarter never gets near it. Each
    // on a row of its own, read within the text's measured size: how wide
    // "MM" is depends on the font the system has (74 px in DejaVu Sans Bold,
    // 70 in Inter), and the edge of the strong line must not be read as part
    // of the faded one.
    FontStyle font;
    font.fontSize = 28;
    font.fontWeight = FontWeight::Bold;
    ctx->SetFontStyle(font);
    const Size2Di text = ctx->GetTextLineDimensions("MM");
    const int strongTop = 90, fadedTop = strongTop + text.height + 10;
    ctx->SetTextPaint(Colors::Black);
    ctx->DrawText("MM", Point2Dd(10, strongTop));
    const int strong = canvas.Darkest(10, strongTop, 10 + text.width, strongTop + text.height);
    ctx->PushState();
    ctx->SetAlpha(0.25);
    ctx->SetFontStyle(font);
    ctx->SetTextPaint(Colors::Black);
    ctx->DrawText("MM", Point2Dd(10, fadedTop));
    ctx->PopState();
    const int faded = canvas.Darkest(10, fadedTop, 10 + text.width, fadedTop + text.height);
    Report("text width", text.width);
    Report("text height", text.height);
    Report("darkest text at 1.0", strong);
    Report("darkest text at 0.25", faded);
    Check(strong < 40, "text at full strength is black");
    Check(faded > 170, "text is faded by SetAlpha");

    std::cout << (g_failures == 0 ? "All alpha checks passed.\n" : "Alpha checks FAILED.\n");
    return g_failures == 0 ? 0 : 1;
}

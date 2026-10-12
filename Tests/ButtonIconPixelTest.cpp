// Tests/ButtonIconPixelTest.cpp
// A button draws its icon on whole pixels. An icon centred in a button whose
// content box has an odd width came out half a pixel off, and every edge in
// it was resampled across two pixels: the 1-2 px lines and dashes of a tool
// icon blurred into grey (UltraPaint's tool palette). The icon's corner is
// now snapped to the surface's pixel grid, through whatever translation the
// button is drawn under.
//
// A 10 x 10 black square icon in a 31 x 31 white button - centred at 10.5 -
// is drawn offscreen and read back: it must cover exactly 100 black pixels
// and leave no grey edge, drawn at the origin and under a translation of a
// fraction of a pixel.
//
// Usage: ButtonIconPixelTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework

#include "UltraCanvasButton.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>
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

// A black square as an image decoded from a PNG made in memory.
std::shared_ptr<UCImage> BlackSquare(int size) {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t* cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    std::vector<uint8_t> png;
    cairo_surface_write_to_png_stream(surface, [](void* closure, const unsigned char* data, unsigned int length) {
        auto* out = static_cast<std::vector<uint8_t>*>(closure);
        out->insert(out->end(), data, data + length);
        return CAIRO_STATUS_SUCCESS;
    }, &png);
    cairo_surface_destroy(surface);
    return UCImage::GetFromMemory(png.data(), png.size());
}

struct Coverage {
    int black = 0;   // pixels the icon covers in full
    int grey = 0;    // pixels it covers in part: a blurred edge
};

// The button drawn into a white 32 x 32 surface under a translation of
// `shift`, read back pixel by pixel.
Coverage Draw(UltraCanvasButton& button, double shift) {
    auto ctx = CreateRenderContext(Size2Di(32, 32), nullptr);
    ctx->Clear(Color(255, 255, 255, 255));
    ctx->PushState();
    ctx->Translate(shift, shift);
    button.Render(ctx.get(), Rect2Df(0, 0, 31, 31));
    ctx->PopState();
    UCPixmap pixmap;
    pixmap.Init(32, 32);
    ctx->FlushToSurface(pixmap.GetSurface(), Point2Dd(0, 0));
    pixmap.MarkDirty();
    pixmap.Flush();
    Coverage c;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const int green = static_cast<int>((pixmap.GetPixel(x, y) >> 8) & 0xFF);
            if (green < 40) ++c.black;
            else if (green < 215) ++c.grey;
        }
    }
    return c;
}

} // namespace

int main() {
    UCImage::InitializeImageSubsysterm("ButtonIconPixelTest");
    auto icon = BlackSquare(10);
    Check(icon != nullptr, "an icon decoded from a PNG made in memory");
    if (!icon) return failures;

    auto button = std::make_shared<UltraCanvasButton>("icon-button", 0, 0, 31, 31, "");
    button->SetPadding(0);
    ButtonStyle style = button->GetStyle();
    style.normalColor = Colors::White;
    style.normalTextColor = Colors::Black;
    style.borderWidth = 0;
    style.hasShadow = false;
    style.cornerRadius = 0;
    style.useIconAsMask = true;
    button->SetStyle(style);
    button->SetIcon(icon);
    button->SetIconSize(10, 10);
    CSSLayout::LayoutContext lctx;
    button->Arrange(Rect2Df(0, 0, 31, 31), lctx);

    const Coverage atOrigin = Draw(*button, 0.0);
    std::printf("  (at the origin: %d black, %d grey)\n", atOrigin.black, atOrigin.grey);
    Check(atOrigin.black == 100 && atOrigin.grey == 0,
          "an icon centred at a half pixel is drawn on whole pixels: 10 x 10 black, no grey edge");

    const Coverage shifted = Draw(*button, 0.3);
    std::printf("  (under a 0.3 px translation: %d black, %d grey)\n", shifted.black, shifted.grey);
    Check(shifted.black == 100 && shifted.grey == 0,
          "and so is a button drawn under a translation of a fraction of a pixel");

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}

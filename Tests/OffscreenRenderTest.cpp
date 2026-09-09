// Tests/OffscreenRenderTest.cpp
// Rendering widgets into an offscreen surface, with no window anywhere.
//
// A context from CreateRenderContext(size, nullptr) is a real render target -
// it is how the QR code plugin exports a PNG - but an element drawn into one
// has no window to reach back to. Anything a widget needs at paint time has to
// come from the context it was handed, and a widget that cannot get it must
// draw less rather than crash.
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework

#include "UltraCanvasLabel.h"
#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>

#include <cstdint>
#include <iostream>
#include <string>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

// A white surface with something drawn on it has non-white pixels. Counting
// them is enough to tell "drew the text" from "drew nothing", which is the
// only distinction these tests need.
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

    int InkPixels() const {
        if (!cr) return -1;
        cairo_surface_t* s = cairo_get_target(cr);
        cairo_surface_flush(s);
        if (cairo_image_surface_get_format(s) != CAIRO_FORMAT_ARGB32 &&
            cairo_image_surface_get_format(s) != CAIRO_FORMAT_RGB24) return -1;
        const unsigned char* data = cairo_image_surface_get_data(s);
        const int stride = cairo_image_surface_get_stride(s);
        const int w = cairo_image_surface_get_width(s);
        const int h = cairo_image_surface_get_height(s);
        int ink = 0;
        for (int y = 0; y < h; ++y) {
            const uint32_t* row = reinterpret_cast<const uint32_t*>(data + y * stride);
            for (int x = 0; x < w; ++x) {
                if ((row[x] & 0x00FFFFFFu) != 0x00FFFFFFu) ++ink;
            }
        }
        return ink;
    }
};

void TestContextExists() {
    std::cout << "\nAn offscreen context\n";
    Canvas canvas(300, 80);
    Check(canvas.ctx != nullptr, "is created without a window");
    Check(canvas.cr != nullptr, "and has a drawable surface behind it");
    Check(canvas.InkPixels() == 0, "starts blank");
    if (!canvas.ctx) return;
    auto layout = canvas.ctx->CreateTextLayout("text", false);
    Check(layout != nullptr, "and can lay out text - there is no window in it");
}

void TestLabelDrawsItsText() {
    std::cout << "\nA label with no window\n";
    Canvas canvas(300, 80);
    if (!canvas.ctx) { Check(false, "no context"); return; }

    UltraCanvasLabel label("offscreen", 0, 0, 300, 40, "");
    label.SetText("Offscreen");
    // Used to be a segfault: the label built its layout from the context
    // reachable through its window, had none, and drew the null it got back.
    label.Render(canvas.ctx.get(), Rect2Df(0, 0, 300, 40));
    const int ink = canvas.InkPixels();
    Check(ink > 0, "draws its text into the surface it was handed");
    Check(ink < 300 * 80, "and does not fill the whole surface");
}

void TestEmptyLabelDrawsNothing() {
    std::cout << "\nA label with no text\n";
    Canvas canvas(300, 80);
    if (!canvas.ctx) { Check(false, "no context"); return; }

    UltraCanvasLabel label("blank", 0, 0, 300, 40, "");
    label.Render(canvas.ctx.get(), Rect2Df(0, 0, 300, 40));
    Check(canvas.InkPixels() == 0, "leaves the surface blank rather than crashing");
}

void TestMeasuredBeforeItIsDrawn() {
    std::cout << "\nA label measured before it has a context\n";
    Canvas canvas(300, 80);
    if (!canvas.ctx) { Check(false, "no context"); return; }

    UltraCanvasLabel label("late", 0, 0, 300, 40, "");
    label.SetText("Measured first");
    // Intrinsic sizing runs with no context reachable and leaves the layout
    // unbuilt. Painting must still build it, from the context it is given.
    label.InvalidateLayout();
    label.SetBounds(Rect2Df(0, 0, 300, 40));
    label.Render(canvas.ctx.get(), Rect2Df(0, 0, 300, 40));
    Check(canvas.InkPixels() > 0, "still draws when it is finally painted");
}

void TestRepeatedRenders() {
    std::cout << "\nDrawn more than once\n";
    Canvas first(300, 80);
    Canvas second(300, 80);
    if (!first.ctx || !second.ctx) { Check(false, "no context"); return; }

    UltraCanvasLabel label("twice", 0, 0, 300, 40, "");
    label.SetText("Twice");
    label.Render(first.ctx.get(), Rect2Df(0, 0, 300, 40));
    label.Render(second.ctx.get(), Rect2Df(0, 0, 300, 40));
    const int a = first.InkPixels(), b = second.InkPixels();
    Check(a > 0 && b > 0, "draws into both surfaces");
    Check(a == b, "and draws the same thing in each");
}

} // namespace

int main() {
    std::cout << "===== Offscreen rendering =====\n";
    TestContextExists();
    TestLabelDrawsItsText();
    TestEmptyLabelDrawsNothing();
    TestMeasuredBeforeItIsDrawn();
    TestRepeatedRenders();
    std::cout << "\n" << (g_failures ? "FAILED" : "PASSED") << " ("
              << g_failures << " failure" << (g_failures == 1 ? "" : "s") << ")\n";
    return g_failures == 0 ? 0 : 1;
}

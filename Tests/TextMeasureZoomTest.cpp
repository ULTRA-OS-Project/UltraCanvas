// Tests/TextMeasureZoomTest.cpp
// Text drawn under a scaled transform is as wide as the same text measured
// unscaled, times the scale: an element that lays out at one zoom and draws
// at another (a node diagram, a drawing, a map) can trust its measurements.
// Headless: an offscreen Cairo surface, no window.
//
// Version: 1.0.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework

#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }               \
    } while (0)

namespace {

const int kWidth = 1600;
const int kHeight = 200;

struct Canvas {
    std::unique_ptr<IRenderContext> ctx;
    cairo_t* cr = nullptr;
    Canvas() {
        ctx = CreateRenderContext(Size2Di(kWidth, kHeight), nullptr);
        if (ctx) cr = static_cast<cairo_t*>(ctx->GetNativeContext());
    }
    void Clear() {
        ctx->ResetTransform();
        ctx->SetFillPaint(Color(255, 255, 255, 255));
        ctx->FillRectangle(Rect2Dd(0, 0, kWidth, kHeight));
    }
    // The leftmost and rightmost device columns holding ink, or -1.
    void InkColumns(int& first, int& last) const {
        first = -1;
        last = -1;
        cairo_surface_t* s = cairo_get_target(cr);
        cairo_surface_flush(s);
        const unsigned char* data = cairo_image_surface_get_data(s);
        const int stride = cairo_image_surface_get_stride(s);
        for (int y = 0; y < kHeight; ++y) {
            const uint32_t* row = reinterpret_cast<const uint32_t*>(data + y * stride);
            for (int x = 0; x < kWidth; ++x) {
                if ((row[x] & 0x00FFFFFFu) != 0x00FFFFFFu) {
                    if (first < 0 || x < first) first = x;
                    if (x > last) last = x;
                }
            }
        }
    }
};

struct Sample { const char* text; double fontSize; FontWeight weight; };

// The ink width of `text` drawn at `scale`, in device pixels.
double DrawnInkWidth(Canvas& c, const Sample& s, double scale) {
    c.Clear();
    c.ctx->PushState();
    c.ctx->Translate(10.0, 10.0);
    c.ctx->Scale(scale, scale);
    c.ctx->SetFontFace("Arial", s.weight, FontSlant::Normal);
    c.ctx->SetFontSize(s.fontSize);
    c.ctx->SetTextPaint(Color(0, 0, 0, 255));
    c.ctx->DrawText(s.text, Point2Dd(0.0, 0.0));
    c.ctx->PopState();
    int first = 0, last = 0;
    c.InkColumns(first, last);
    return first < 0 ? 0.0 : static_cast<double>(last - first + 1);
}

} // namespace

int main() {
    std::printf("TextMeasureZoomTest\n");
    Canvas canvas;
    CHECK(canvas.ctx != nullptr, "an offscreen context is available");
    if (!canvas.ctx) return 1;

    const Sample samples[] = {
        {"Kim Park", 11.0, FontWeight::Bold},
        {"Clara Fischer", 11.0, FontWeight::Bold},
        {"Quality Control", 10.0, FontWeight::Bold},
        {"Business Solutions and Client Services, a long cluster title", 12.0, FontWeight::Normal},
        {"iiiiiiiiiiiiiiiiiiii", 9.0, FontWeight::Normal},
        {"WWWWWWWWWW", 24.0, FontWeight::Bold},
    };
    const double scales[] = {0.4, 0.5, 0.61, 0.75, 1.5, 2.0, 3.0};

    for (const Sample& s : samples) {
        std::printf("\n[%s, %.0f pt]\n", s.text, s.fontSize);
        canvas.ctx->SetFontFace("Arial", s.weight, FontSlant::Normal);
        canvas.ctx->SetFontSize(s.fontSize);
        const double measured = canvas.ctx->GetTextLineDimensions(s.text).width;
        const double inkAtOne = DrawnInkWidth(canvas, s, 1.0);
        std::printf("  measured %.1f, drawn at 1.0: %.1f px\n", measured, inkAtOne);
        CHECK(inkAtOne > 0.0 && inkAtOne <= measured + 2.0, "the text drawn unscaled is no wider than its measurement");
        for (double scale : scales) {
            const double ink = DrawnInkWidth(canvas, s, scale);
            const double expected = inkAtOne * scale;
            // Antialiasing adds up to a pixel at each end; nothing else may.
            const double tolerance = 2.5 + expected * 0.01;
            char msg[200];
            std::snprintf(msg, sizeof msg, "at zoom %.2f the ink is %.1f px, measured x zoom is %.1f (within %.1f)",
                          scale, ink, expected, tolerance);
            CHECK(std::fabs(ink - expected) <= tolerance, msg);
        }
    }

    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}

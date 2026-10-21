// Tests/GaugeLinearModesTest.cpp
// The gauge's six linear (bar) modes, headless.
//
//  1. Every linear mode, horizontal and vertical, at sizes from a dashboard
//     card down to a status-line strip, draws nothing outside its element.
//     Before, five of the six laid out for the box they wanted (a 44 px LED
//     strip, 80 px kept for MultiPointer's labels, 60 px for WithArrow's) and
//     drew over their neighbours; vertical LinearBar rounded its ends with
//     half its length and drew past the element.
//  2. The bar is really drawn where there is room for it: a vertical
//     LinearBar in 320 x 160 (it drew a single dot), and a horizontal one in
//     a 10 px row (the bar alone, across the row).
//  3. The value is clamped to the range whenever the range changes:
//     SetValue(150) then SetMaxValue(200) is 150 (it was 100), and a range
//     shrunk under the value takes the value with it.
//  4. LinearSegmented runs vertically: its lit bricks are at the bottom.
//  5. A pointer label pushed to the edge keeps clear of it (flush with the
//     clip, its antialiasing bled a pixel past it on cairo 1.16).
//
// Each gauge is drawn into a canvas with a margin round it; a pixel in the
// margin that is not white was drawn outside the element.
// Version: 1.1.0 - pointer labels keep clear of the edge
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h"
#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

using namespace UltraCanvas;

namespace {

int g_failures = 0;
int g_count = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
    ++g_count;
}

constexpr int kMargin = 30;

// A gauge drawn at (kMargin, kMargin) of a white canvas.
struct Drawn {
    std::unique_ptr<IRenderContext> ctx;
    cairo_t* cr = nullptr;
    int w = 0, h = 0;

    uint32_t Pixel(int x, int y) const {
        cairo_surface_t* s = cairo_get_target(cr);
        cairo_surface_flush(s);
        const unsigned char* data = cairo_image_surface_get_data(s);
        return *reinterpret_cast<const uint32_t*>(data + y * cairo_image_surface_get_stride(s) + x * 4) & 0xFFFFFF;
    }
    // Inside the element, by element coordinates.
    uint32_t At(int x, int y) const { return Pixel(x + kMargin, y + kMargin); }

    int PixelsOutside() const {
        int n = 0;
        for (int y = 0; y < h + 2 * kMargin; ++y) {
            for (int x = 0; x < w + 2 * kMargin; ++x) {
                const bool inside = x >= kMargin && x < kMargin + w && y >= kMargin && y < kMargin + h;
                if (!inside && Pixel(x, y) != 0xFFFFFF) ++n;
            }
        }
        return n;
    }
};

Drawn Draw(const std::shared_ptr<UltraCanvasGaugeDiagramElement>& gauge, int w, int h) {
    Drawn d;
    d.w = w;
    d.h = h;
    d.ctx = CreateRenderContext(Size2Di(w + 2 * kMargin, h + 2 * kMargin), nullptr);
    d.cr = static_cast<cairo_t*>(d.ctx->GetNativeContext());
    cairo_set_source_rgb(d.cr, 1, 1, 1);
    cairo_paint(d.cr);
    d.ctx->PushState();
    d.ctx->Translate(kMargin, kMargin);
    gauge->Render(d.ctx.get(), Rect2Df(0, 0, static_cast<float>(w), static_cast<float>(h)));
    d.ctx->PopState();
    return d;
}

std::shared_ptr<UltraCanvasGaugeDiagramElement> Gauge(GaugeMode mode, int w, int h, bool vertical) {
    auto g = CreateGaugeDiagramElement("gauge", 0, 0, static_cast<float>(w), static_cast<float>(h));
    g->SetMode(mode);
    g->SetOrientation(vertical ? GaugeOrientation::Vertical : GaugeOrientation::Horizontal);
    g->SetTitle("Power");
    g->SetUnit("W");
    g->SetMinValue(0);
    g->SetMaxValue(200);
    g->SetValue(130);
    g->SetGaugeColor(Color(0, 120, 255));
    // Everything a mode draws around its bar, so the room for it is tested.
    g->AddExternalPointer(GaugeExternalPointer(60, Color(200, 60, 60), "Low"));
    g->AddExternalPointer(GaugeExternalPointer(190, Color(40, 160, 90), "Peak"));
    g->AddRange(GaugeRangeSegment(0, 80, Color(120, 200, 120)));
    g->AddRange(GaugeRangeSegment(80, 200, Color(250, 200, 80)));
    for (double v : {20.0, 120.0, 190.0}) {
        GaugeThreshold t;
        t.value = v;
        t.label = "Station " + std::to_string(static_cast<int>(v));
        t.color = Color(80, 80, 200);
        g->AddThreshold(t);
    }
    return g;
}

bool IsBlue(uint32_t px) {
    const int r = (px >> 16) & 0xFF, gr = (px >> 8) & 0xFF, b = px & 0xFF;
    return b > 200 && r < 60 && gr > 90 && gr < 160;
}

void TestNothingOutside() {
    std::cout << "1. nothing is drawn outside the element\n";
    const struct { GaugeMode mode; const char* name; } modes[] = {
        {GaugeMode::LinearBar, "LinearBar"}, {GaugeMode::LinearLED, "LinearLED"},
        {GaugeMode::LinearSegmented, "LinearSegmented"}, {GaugeMode::LinearMultiPointer, "LinearMultiPointer"},
        {GaugeMode::LinearWithArrow, "LinearWithArrow"}, {GaugeMode::LinearScale, "LinearScale"}};
    const int sizes[][2] = {{320, 160}, {320, 60}, {320, 24}, {140, 40}, {80, 240}, {60, 120}, {300, 8}};
    for (const auto& m : modes) {
        for (bool vertical : {false, true}) {
            int worst = 0;
            std::string where;
            for (const auto& size : sizes) {
                const int outside = Draw(Gauge(m.mode, size[0], size[1], vertical), size[0], size[1]).PixelsOutside();
                if (outside > worst) {
                    worst = outside;
                    where = std::to_string(size[0]) + " x " + std::to_string(size[1]);
                }
            }
            Check(worst == 0, std::string(m.name) + (vertical ? " vertical" : " horizontal") +
                                  ": inside its box at every size" +
                                  (worst ? " (" + std::to_string(worst) + " px outside at " + where + ")" : ""));
        }
    }
}

void TestBarIsDrawn() {
    std::cout << "2. the bar is drawn where there is room\n";
    // Vertical, 320 x 160: a track up the middle with the fill in its lower
    // two thirds (130 of 200).
    auto v = Gauge(GaugeMode::LinearBar, 320, 160, true);
    v->ClearRanges();   // the fill in the gauge colour, not a range's
    Drawn dv = Draw(v, 320, 160);
    int blueRows = 0;
    for (int y = 0; y < 160; ++y) blueRows += IsBlue(dv.At(160, y)) ? 1 : 0;
    Check(blueRows >= 30, "vertical LinearBar in 320 x 160 fills a column (was a single dot)");

    // A list row: 10 px, the bar alone across it.
    auto row = Gauge(GaugeMode::LinearBar, 300, 10, false);
    row->ClearRanges();
    Drawn dr = Draw(row, 300, 10);
    Check(IsBlue(dr.At(100, 5)) && !IsBlue(dr.At(280, 5)), "a 10 px LinearBar is the bar, filled to 65 %");
}

void TestValueFollowsRange() {
    std::cout << "3. the value follows the range\n";
    auto g = CreateGaugeDiagramElement("g", 0, 0, 200, 100);
    g->SetValue(150);   // the default range is 0..100
    Check(g->GetValue() == 100.0, "SetValue(150) on 0..100 shows 100");
    g->SetMaxValue(200);
    Check(g->GetValue() == 150.0, "raising the maximum to 200 shows the 150 asked for (was stuck at 100)");
    g->SetMaxValue(120);
    Check(g->GetValue() == 120.0, "a maximum lowered under the value takes it down");
    g->SetMinValue(130);
    g->SetMaxValue(300);
    Check(g->GetValue() == 150.0, "and back up again to 150");
}

void TestSegmentedVertical() {
    std::cout << "4. LinearSegmented runs vertically\n";
    auto g = Gauge(GaugeMode::LinearSegmented, 120, 260, true);
    g->ClearRanges();
    Drawn d = Draw(g, 120, 260);
    // The lit bricks (130 of 200: six of ten) are at the bottom of a
    // vertical bar in the middle; the top ones are the track's grey.
    int litLow = 0, litHigh = 0;
    for (int y = 0; y < 260; ++y) {
        const uint32_t px = d.At(60, y);
        if (!IsBlue(px)) continue;
        (y > 130 ? litLow : litHigh)++;
    }
    Check(litLow > 40 && litHigh < litLow, "vertical bricks light from the bottom");
}

void TestLabelsClearOfEdge() {
    std::cout << "5. pointer labels keep clear of the edge\n";
    // "190 W - Peak" sits at 95 % of the bar: centred on its arrow it would
    // run past the right edge, so it is pushed back inside.
    Drawn d = Draw(Gauge(GaugeMode::LinearMultiPointer, 320, 160, false), 320, 160);
    int edge = 0;
    for (int y = 0; y < 160; ++y) {
        for (int x : {0, 319}) edge += d.At(x, y) != 0xFFFFFF ? 1 : 0;
    }
    Check(edge == 0, "nothing in the outermost columns of a 320 x 160 LinearMultiPointer");
}

} // namespace

int main() {
    TestNothingOutside();
    TestBarIsDrawn();
    TestValueFollowsRange();
    TestSegmentedVertical();
    TestLabelsClearOfEdge();
    std::cout << (g_count - g_failures) << " of " << g_count << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}

// Tests/TickerBoardRenderTest.cpp
// UltraCanvasTickerBoard drawn into an offscreen surface: MakeTickerQuote's
// formatting, the DotMatrix grid (a lit column where the glyph is, the
// background where the gap is), hit testing on both faces, and SetOffset
// wrapping to the strip length.
//
// There is no application here, so no timer runs and the board rests at
// offset 0: the first item's label starts at the left edge.
// Version: 1.0.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasTickerBoard.h"
#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>

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

const Color kBlack(0, 0, 0);
const Color kAmber(255, 170, 0);

void TestQuoteFormatting() {
    std::cout << "\nMakeTickerQuote\n";
    TickerItem up = MakeTickerQuote("UCNV", 189.30, 1.25);
    Check(up.label == "UCNV", "keeps the symbol as the label");
    Check(up.value == "189.30", "formats the price at two places: " + up.value);
    Check(up.change == "+1.25 (+0.66%)", "signs the change and adds the percentage: " + up.change);
    Check(up.trend == TickerTrend::Up, "a positive change trends Up");

    TickerItem down = MakeTickerQuote("VFX", 42.18, -0.64);
    Check(down.change == "-0.64 (-1.49%)", "a negative change keeps its sign: " + down.change);
    Check(down.trend == TickerTrend::Down, "and trends Down");

    TickerItem flat = MakeTickerQuote("NETM", 77.05, 0.0);
    Check(flat.change == "0.00 (0.00%)", "a zero change is unsigned: " + flat.change);
    Check(flat.trend == TickerTrend::Flat, "and trends Flat");

    TickerItem plain = MakeTickerQuote("BTC", 64123.5, -812.26, 1, false);
    Check(plain.value == "64123.5" && plain.change == "-812.3",
          "decimals and withPercent are honoured: " + plain.value + " " + plain.change);
}

// A 100 x 40 DotMatrix board: pitch 40 / 8 = 5, row 0 centred at y = 5,
// grid column g centred at x = 2.5 + 5 g. SetText("I") lays out the 'I'
// glyph trimmed to its three lit columns (0x41, 0x7F, 0x41), then the
// item gap, the separator and the gap again.
void TestDotMatrix() {
    std::cout << "\nDotMatrix face\n";
    Canvas canvas(100, 40);
    if (!canvas.cr) { Check(false, "no context"); return; }

    auto board = CreateTickerBoard("dots", 0, 0, 100, 40, TickerBoardFace::DotMatrix);
    TickerBoardStyle style = board->GetStyle();
    style.backgroundColor = kBlack;
    style.labelColor = kAmber;
    style.showOffDots = false;
    board->SetText("I");
    board->SetStyle(style);
    board->Render(canvas.ctx.get(), Rect2Df(0, 0, 100, 40));

    Check(board->GetStripLength() == (3 + 8 + 2 + 8) * 5.0,
          "the strip is the glyph, the gap, the separator and the gap: " +
          std::to_string(board->GetStripLength()));
    Check(Near(canvas.At(7.5, 5), kAmber), "the stem of the I is lit at the top row");
    Check(Near(canvas.At(7.5, 35), kAmber), "and at the bottom row");
    Check(Near(canvas.At(2.5, 20), kBlack), "the serif column is dark in the middle rows");
    Check(Near(canvas.At(17.5, 20), kBlack), "the gap column after the glyph is dark");
    Check(board->GetItemAt(7) == 0, "GetItemAt over the glyph is item 0");
    Check(board->GetItemAt(17) == SIZE_MAX, "GetItemAt in the gap is nothing");
    Check(board->GetItemAt(11 * 5 + 2) == SIZE_MAX, "GetItemAt on the separator is nothing");

    // One column on: the glyph has moved one grid column to the left.
    board->SetOffset(5.0);
    board->Render(canvas.ctx.get(), Rect2Df(0, 0, 100, 40));
    Check(Near(canvas.At(2.5, 5), kAmber), "after one column the stem sits in column 0");
    Check(board->GetItemAt(2) == 0, "and hit testing follows it");
}

void TestTextFace() {
    std::cout << "\nText face\n";
    Canvas canvas(400, 40);
    if (!canvas.cr) { Check(false, "no context"); return; }

    auto board = CreateTickerBoard("text", 0, 0, 400, 40, TickerBoardFace::Text);
    TickerBoardStyle style = board->GetStyle();
    style.backgroundColor = kBlack;
    style.labelColor = kAmber;
    style.itemGap = 20.0f;
    board->SetStyle(style);
    board->SetItems({TickerItem("HELLO"), TickerItem("WORLD")});
    board->Render(canvas.ctx.get(), Rect2Df(0, 0, 400, 40));

    Check(board->GetStripLength() > 40.0, "the strip has a length once drawn");
    Check(board->GetItemAt(2) == 0, "the first label starts at the left edge");
    // Walk right: the label ends, then a gap, the separator, a gap, then WORLD.
    int gapAt = -1, secondAt = -1;
    for (int x = 0; x < 400 && secondAt < 0; ++x) {
        const size_t item = board->GetItemAt(x);
        if (gapAt < 0 && item == SIZE_MAX) gapAt = x;
        if (gapAt >= 0 && item == 1) secondAt = x;
    }
    Check(gapAt > 10, "the label is wider than ten pixels");
    Check(secondAt > gapAt + 40, "the second item follows after two gaps and the separator");

    // Something amber was drawn in the first label's span.
    bool lit = false;
    for (int x = 0; x < gapAt && !lit; ++x)
        for (int y = 8; y < 32 && !lit; ++y)
            if (Near(canvas.At(x, y), kAmber, 60)) lit = true;
    Check(lit, "the label is drawn in labelColor");

    const double length = board->GetStripLength();
    board->SetOffset(length + 3.0);
    Check(std::fabs(board->GetOffset() - 3.0) < 1e-9, "SetOffset wraps past the strip length");
    board->SetOffset(-3.0);
    Check(std::fabs(board->GetOffset() - (length - 3.0)) < 1e-9, "and below zero");
}

void TestAccessibleValue() {
    std::cout << "\nAccessibility\n";
    auto board = CreateTickerBoard("a11y", 0, 0, 100, 40);
    board->SetItems({MakeTickerQuote("UCNV", 189.30, 1.25), TickerItem("Platform 4", "12:45")});
    Check(board->GetAccessibleValueText() == "UCNV 189.30 +1.25 (+0.66%); Platform 4 12:45",
          "reads the items in order: " + board->GetAccessibleValueText());
}

} // namespace

int main() {
    std::cout << "TickerBoardRenderTest\n";
    TestQuoteFormatting();
    TestDotMatrix();
    TestTextFace();
    TestAccessibleValue();
    std::cout << "\n" << (g_failures == 0 ? "All checks passed" : std::to_string(g_failures) + " check(s) failed") << "\n";
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

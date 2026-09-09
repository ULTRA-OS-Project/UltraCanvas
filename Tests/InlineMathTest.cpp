// Tests/InlineMathTest.cpp
// Inline math for the text stack (Phase 2 of UltraCanvasLaTeXEngineProposal.md):
// the core's UltraCanvasInlineMath handle on the LaTeX module, the Markdown
// parser's placeholder for $...$, and the TextArea drawing a formula inside a
// rendered line.
//
// The LaTeX module is loaded with dlopen from <build>/lib (the CMake target
// depends on it), so this is a real cross-module test: if the module or its
// math font cannot be found the first section fails and says why.
//
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework

#include "UltraCanvasInlineMath.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasRenderContext.h"
#include "Plugins/LaTeX/UltraCanvasLaTeXView.h"

#include <cairo/cairo.h>

#include <cmath>
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

std::string F(double v) { char b[32]; std::snprintf(b, sizeof(b), "%.1f", v); return b; }

// Reaches the protected static parser and the element's own render context.
struct TextAreaProbe : UltraCanvasTextArea {
    TextAreaProbe(float w, float h) : UltraCanvasTextArea("probe", 0, 0, w, h) {}
    using UltraCanvasTextArea::ParseInlineMarkdownRuns;
    using UltraCanvasTextArea::InlineRun;
    void Attach(std::unique_ptr<IRenderContext> ctx) { renderContext = std::move(ctx); }
};

void TestHandle() {
    std::cout << "\nUltraCanvasInlineMath\n";
    Check(IsLaTeXModuleAvailable(), "LaTeX module loads: " + GetLaTeXModuleError());
    Check(UltraCanvasInlineMath::IsAvailable(), "inline entry points available");

    auto m = UltraCanvasInlineMath::Typeset("x^2 + \\frac{a}{b}", 20.f, Colors::Black, false);
    Check(m != nullptr, "a formula typesets (math font found)");
    if (!m) return;
    Check(m->IsValid() && m->GetError().empty(), "...without errors");
    Check(m->GetWidth() > 20.f && m->GetAscent() > 10.f && m->GetDescent() > 0.f,
          "...with metrics " + F(m->GetWidth()) + " x " + F(m->GetAscent()) + " + " + F(m->GetDescent()));

    auto text = UltraCanvasInlineMath::Typeset("\\sum_{k=1}^{n} k", 20.f, Colors::Black, false);
    auto disp = UltraCanvasInlineMath::Typeset("\\sum_{k=1}^{n} k", 20.f, Colors::Black, true);
    Check(text && disp && disp->GetHeight() > text->GetHeight() * 1.3f && disp->GetWidth() < text->GetWidth(),
          "text style keeps the limits beside the sum, display style stacks them");

    auto big = UltraCanvasInlineMath::Typeset("x", 40.f, Colors::Black, false);
    auto small = UltraCanvasInlineMath::Typeset("x", 20.f, Colors::Black, false);
    Check(big && small && std::fabs(big->GetWidth() / small->GetWidth() - 2.f) < 0.05f, "metrics scale with the font size");

    auto bad = UltraCanvasInlineMath::Typeset("x + \\nosuchcommand", 20.f, Colors::Black, false);
    Check(bad && !bad->IsValid() && bad->GetError().find("unknown command") != std::string::npos && bad->GetWidth() > 0.f,
          "an error still typesets and is reported: " + (bad ? bad->GetError() : std::string("(null)")));

    auto empty = UltraCanvasInlineMath::Typeset("", 20.f, Colors::Black, false);
    Check(empty && empty->GetWidth() == 0.f && empty->IsValid(), "an empty formula is an empty box");
}

void TestParser() {
    std::cout << "\nMarkdown inline runs\n";
    std::string visible;
    std::vector<TextAreaProbe::InlineRun> runs;
    std::vector<CpRun> cpMap;
    TextAreaProbe::ParseInlineMarkdownRuns("see $E = mc^2$ now", visible, runs, &cpMap);
    Check(visible == std::string("see ") + kInlineMathPlaceholder + " now",
          "$...$ becomes one U+FFFC placeholder in the visible text");
    Check(runs.size() == 1 && runs[0].kind == TextAreaProbe::InlineRun::Math && runs[0].url == "E = mc^2" &&
          runs[0].alt.empty() && runs[0].startByte == 4 && runs[0].endByte == 4 + kInlineMathPlaceholderBytes,
          "...with a Math run carrying the LaTeX source");
    // The cp map: visible cp 4 (the placeholder) starts at source cp 5 (after "see $").
    Check(VisibleCpToSourceCp(cpMap, 4) == 5 && VisibleCpToSourceCp(cpMap, 5) == 14,
          "...and the cursor map skips the markup");

    visible.clear(); runs.clear();
    TextAreaProbe::ParseInlineMarkdownRuns("$$\\sum x$$ tail", visible, runs);
    Check(runs.size() == 1 && runs[0].alt == "display" && runs[0].url == "\\sum x" &&
          visible == std::string(kInlineMathPlaceholder) + " tail", "$$...$$ on one line is a display-style run");

    visible.clear(); runs.clear();
    TextAreaProbe::ParseInlineMarkdownRuns("costs $5 and $10", visible, runs);
    Check(runs.empty() && visible == "costs $5 and $10", "dollar signs that are not a pair stay text");

    visible.clear(); runs.clear();
    TextAreaProbe::ParseInlineMarkdownRuns("a $\\alpha$ **b** $\\beta$", visible, runs);
    int math = 0;
    for (const auto& r : runs) if (r.kind == TextAreaProbe::InlineRun::Math) ++math;
    Check(math == 2 && runs.size() == 3, "several formulas in one line, mixed with other runs");
}

// Counts inked (clearly non-white) pixels in a rectangle of the surface; the
// formula colour is the Markdown style's math green, not black.
long DarkPixels(cairo_surface_t* surface, int x0, int y0, int x1, int y1) {
    cairo_surface_flush(surface);
    const unsigned char* data = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    const int w = cairo_image_surface_get_width(surface), h = cairo_image_surface_get_height(surface);
    long n = 0;
    for (int y = std::max(0, y0); y < std::min(h, y1); ++y) {
        for (int x = std::max(0, x0); x < std::min(w, x1); ++x) {
            const unsigned char* p = data + y * stride + x * 4;   // BGRA
            if (p[0] < 180 || p[1] < 180 || p[2] < 180) ++n;
        }
    }
    return n;
}

void TestRendering() {
    std::cout << "\nTextArea rendering\n";
    const int W = 480, H = 240;
    auto ctx = CreateRenderContext(Size2Di(W, H), nullptr);
    Check(ctx != nullptr, "offscreen render context");
    if (!ctx) return;
    ctx->SetFillPaint(Colors::White);
    ctx->FillRectangle(Rect2Dd(0, 0, W, H));

    // Line 1 is the cursor line and shows its raw text in hybrid mode, so the
    // formula goes on line 2: a solid 3em x 1em rule the engine draws as one
    // black rectangle - the most checkable inline object there is.
    auto area = std::make_shared<TextAreaProbe>(static_cast<float>(W), static_cast<float>(H));
    area->Attach(std::move(ctx));
    IRenderContext* rc = area->GetRenderContext();
    area->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
    area->SetText("\nx $\\rule{3em}{1em}$ y\n");
    area->Render(rc, Rect2Df(0, 0, static_cast<float>(W), static_cast<float>(H)));

    cairo_t* cr = static_cast<cairo_t*>(rc->GetNativeContext());
    cairo_surface_t* surface = cr ? cairo_get_target(cr) : nullptr;
    Check(surface != nullptr, "native surface accessible");
    if (!surface) return;
    const long dark = DarkPixels(surface, 0, 0, W, H);
    // 3em x 1em at the default font size is at least 12*3 x 12 pixels of solid ink.
    Check(dark >= 12 * 3 * 12, "the inline rule is drawn inside the line (" + std::to_string(dark) + " dark pixels)");

    // A second render with text style vs display style through the block form.
    auto area2 = std::make_shared<TextAreaProbe>(static_cast<float>(W), static_cast<float>(H));
    auto ctx2 = CreateRenderContext(Size2Di(W, H), nullptr);
    ctx2->SetFillPaint(Colors::White);
    ctx2->FillRectangle(Rect2Dd(0, 0, W, H));
    area2->Attach(std::move(ctx2));
    IRenderContext* rc2 = area2->GetRenderContext();
    area2->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
    area2->SetText("\nbefore\n\n$$\n\\rule{4em}{2em}\n$$\n\nafter\n");
    area2->Render(rc2, Rect2Df(0, 0, static_cast<float>(W), static_cast<float>(H)));
    cairo_surface_t* surface2 = cairo_get_target(static_cast<cairo_t*>(rc2->GetNativeContext()));
    const long dark2 = DarkPixels(surface2, 0, 0, W, H);
    Check(dark2 >= 12 * 4 * 24, "a $$ block renders its formula (" + std::to_string(dark2) + " dark pixels)");
    // The block is centred: ink in the middle third, not hugging the left edge.
    const long left = DarkPixels(surface2, 0, 0, W / 6, H);
    const long middle = DarkPixels(surface2, W / 3, 0, 2 * W / 3, H);
    Check(middle > left, "...centred in the text area");
}

} // namespace

int main() {
    std::cout << "UltraCanvasInlineMath test\n";
    TestHandle();
    if (UltraCanvasInlineMath::IsAvailable()) {
        TestParser();
        TestRendering();
    } else {
        std::cout << "  (module unavailable - parser and rendering sections skipped, see above)\n";
    }
    std::cout << "\n" << (g_failures == 0 ? "ALL PASSED" : std::to_string(g_failures) + " FAILURE(S)") << "\n";
    return g_failures == 0 ? 0 : 1;
}

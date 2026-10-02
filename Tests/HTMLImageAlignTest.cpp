// Tests/HTMLImageAlignTest.cpp
// Where the HTML reader places an <img> on its line: at the start by
// default, like a browser, and centred or right-aligned only when text-align
// (CSS, align="...", <center>) says so. Also: an image wider than the column
// shrinks to it, keeping its aspect ratio. And an image in the middle of a
// sentence flows in the text, on its baseline, instead of taking a line.
//
// Headless: builds the element tree with HTMLElementBuilder and lays it out
// with the CSSLayout engine; inline images are laid out and drawn on an
// offscreen render context - no window or display needed.
// Version: 1.1.0 - a width:100% picture in a 600px mail table stays 600px
// Version: 1.0.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLElementBuilder.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasImageElement.h"
#include "CSSLayout/CSSLayout.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRenderContext.h"

#include <cairo.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool cond, const std::string& what) {
    std::printf("  %s %s\n", cond ? "ok:  " : "FAIL:", what.c_str());
    if (!cond) ++g_failures;
}

void CheckNear(float actual, float expected, const std::string& what, float tol = 1.0f) {
    const bool ok = std::fabs(actual - expected) <= tol;
    std::printf("  %s %s = %.1f (expected %.1f)\n", ok ? "ok:  " : "FAIL:", what.c_str(),
                actual, expected);
    if (!ok) ++g_failures;
}

// Solid-colour PNGs: 160x60 and 1000x100.
const uint8_t kPng160x60[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0xA0, 0x00, 0x00, 0x00, 0x3C, 0x08, 0x02, 0x00, 0x00, 0x00, 0x6E, 0x09, 0x26,
    0x42, 0x00, 0x00, 0x00, 0x75, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0xED, 0xD1, 0x01, 0x0D, 0x00,
    0x00, 0x08, 0xC3, 0xB0, 0xCB, 0x41, 0x1D, 0x3A, 0x91, 0x85, 0x0F, 0x68, 0x32, 0x05, 0x6B, 0xAA,
    0x47, 0x87, 0x8B, 0x05, 0x80, 0x05, 0x58, 0x80, 0x05, 0x58, 0x80, 0x05, 0x58, 0x80, 0x01, 0x0B,
    0xB0, 0x00, 0x0B, 0xB0, 0x00, 0x0B, 0xB0, 0x00, 0x03, 0x16, 0x60, 0x01, 0x16, 0x60, 0x01, 0x16,
    0x60, 0x01, 0x16, 0x60, 0xC0, 0x02, 0x2C, 0xC0, 0x02, 0x2C, 0xC0, 0x02, 0x2C, 0xC0, 0x80, 0x05,
    0x58, 0x80, 0x05, 0x58, 0x80, 0x05, 0x58, 0x80, 0x01, 0x0B, 0xB0, 0x00, 0x0B, 0xB0, 0x00, 0x0B,
    0xB0, 0x00, 0x0B, 0x30, 0x60, 0x01, 0x16, 0x60, 0x01, 0x16, 0x60, 0x01, 0x16, 0x60, 0xC0, 0x02,
    0x2C, 0xC0, 0x02, 0x2C, 0xC0, 0x02, 0x2C, 0xC0, 0xDF, 0x5B, 0xB4, 0x0F, 0x47, 0xFE, 0x20, 0xB4,
    0x15, 0x34, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
};

const uint8_t kPng1000x100[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x03, 0xE8, 0x00, 0x00, 0x00, 0x64, 0x08, 0x02, 0x00, 0x00, 0x00, 0x9A, 0x5B, 0x8D,
    0x8D, 0x00, 0x00, 0x01, 0xCB, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0xED, 0xD6, 0x31, 0x0D, 0x00,
    0x00, 0x0C, 0xC3, 0xB0, 0xC2, 0x19, 0xBA, 0xE1, 0x1C, 0xAC, 0xD1, 0xE8, 0x61, 0xC9, 0x08, 0x72,
    0x25, 0xB3, 0x07, 0x00, 0x00, 0x94, 0x8B, 0x04, 0x00, 0x00, 0x60, 0xDC, 0x01, 0x00, 0x00, 0xE3,
    0x0E, 0x00, 0x00, 0xC6, 0x1D, 0x00, 0x00, 0x30, 0xEE, 0x00, 0x00, 0x80, 0x71, 0x07, 0x00, 0x00,
    0xE3, 0x0E, 0x00, 0x00, 0x18, 0x77, 0x00, 0x00, 0xC0, 0xB8, 0x03, 0x00, 0x80, 0x71, 0x07, 0x00,
    0x00, 0x8C, 0x3B, 0x00, 0x00, 0x18, 0x77, 0x00, 0x00, 0xC0, 0xB8, 0x03, 0x00, 0x00, 0xC6, 0x1D,
    0x00, 0x00, 0x8C, 0x3B, 0x00, 0x00, 0x60, 0xDC, 0x01, 0x00, 0x00, 0xE3, 0x0E, 0x00, 0x00, 0xC6,
    0x1D, 0x00, 0x00, 0x30, 0xEE, 0x00, 0x00, 0x80, 0x71, 0x07, 0x00, 0x00, 0xE3, 0x0E, 0x00, 0x00,
    0x18, 0x77, 0x00, 0x00, 0x30, 0xEE, 0x00, 0x00, 0x80, 0x71, 0x07, 0x00, 0x00, 0x8C, 0x3B, 0x00,
    0x00, 0x18, 0x77, 0x00, 0x00, 0xC0, 0xB8, 0x03, 0x00, 0x00, 0xC6, 0x1D, 0x00, 0x00, 0x8C, 0x3B,
    0x00, 0x00, 0x60, 0xDC, 0x01, 0x00, 0xC0, 0xB8, 0x03, 0x00, 0x00, 0xC6, 0x1D, 0x00, 0x00, 0x30,
    0xEE, 0x00, 0x00, 0x60, 0xDC, 0x01, 0x00, 0x00, 0xE3, 0x0E, 0x00, 0x00, 0x18, 0x77, 0x00, 0x00,
    0x30, 0xEE, 0x00, 0x00, 0x80, 0x71, 0x07, 0x00, 0x00, 0x8C, 0x3B, 0x00, 0x00, 0x18, 0x77, 0x00,
    0x00, 0xC0, 0xB8, 0x03, 0x00, 0x80, 0x71, 0x07, 0x00, 0x00, 0x8C, 0x3B, 0x00, 0x00, 0x60, 0xDC,
    0x01, 0x00, 0xC0, 0xB8, 0x03, 0x00, 0x00, 0xC6, 0x1D, 0x00, 0x00, 0x30, 0xEE, 0x00, 0x00, 0x60,
    0xDC, 0x01, 0x00, 0x00, 0xE3, 0x0E, 0x00, 0x00, 0xC6, 0x1D, 0x00, 0x00, 0x30, 0xEE, 0x00, 0x00,
    0x80, 0x71, 0x07, 0x00, 0x00, 0xE3, 0x0E, 0x00, 0x00, 0x18, 0x77, 0x00, 0x00, 0xC0, 0xB8, 0x03,
    0x00, 0x80, 0x71, 0x07, 0x00, 0x00, 0x8C, 0x3B, 0x00, 0x00, 0x60, 0xDC, 0x01, 0x00, 0xC0, 0xB8,
    0x03, 0x00, 0x00, 0xC6, 0x1D, 0x00, 0x00, 0x8C, 0x3B, 0x00, 0x00, 0x60, 0xDC, 0x01, 0x00, 0x00,
    0xE3, 0x0E, 0x00, 0x00, 0xC6, 0x1D, 0x00, 0x00, 0x30, 0xEE, 0x00, 0x00, 0x80, 0x71, 0x07, 0x00,
    0x00, 0xE3, 0x0E, 0x00, 0x00, 0x18, 0x77, 0x00, 0x00, 0x30, 0xEE, 0x00, 0x00, 0x80, 0x71, 0x07,
    0x00, 0x00, 0x8C, 0x3B, 0x00, 0x00, 0x18, 0x77, 0x00, 0x00, 0xC0, 0xB8, 0x03, 0x00, 0x00, 0xC6,
    0x1D, 0x00, 0x00, 0x8C, 0x3B, 0x00, 0x00, 0x60, 0xDC, 0x01, 0x00, 0x00, 0xE3, 0x0E, 0x00, 0x00,
    0xC6, 0x1D, 0x00, 0x00, 0x30, 0xEE, 0x00, 0x00, 0x60, 0xDC, 0x01, 0x00, 0x00, 0xE3, 0x0E, 0x00,
    0x00, 0x18, 0x77, 0x00, 0x00, 0x30, 0xEE, 0x00, 0x00, 0x80, 0x71, 0x07, 0x00, 0x00, 0x8C, 0x3B,
    0x00, 0x00, 0x18, 0x77, 0x00, 0x00, 0xC0, 0xB8, 0x03, 0x00, 0x80, 0x71, 0x07, 0x00, 0x00, 0x8C,
    0x3B, 0x00, 0x00, 0x60, 0xDC, 0x01, 0x00, 0xC0, 0xB8, 0x03, 0x00, 0x00, 0xC6, 0x1D, 0x00, 0x00,
    0x30, 0xEE, 0x00, 0x00, 0x60, 0xDC, 0x01, 0x00, 0x00, 0xE3, 0x0E, 0x00, 0x00, 0x18, 0x77, 0x00,
    0x00, 0x30, 0xEE, 0x00, 0x00, 0x80, 0x71, 0x07, 0x00, 0x00, 0xE3, 0x0E, 0x00, 0x00, 0x34, 0x79,
    0x91, 0x09, 0x2E, 0x0B, 0xAD, 0x3E, 0x9E, 0x1A, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
    0xAE, 0x42, 0x60, 0x82
};

struct Placed {
    UltraCanvasImageElement* image = nullptr;
    float rowWidth = 0.f;   // the line the image sits on
};

// The first image element in the tree, with the width of its parent line.
void FindImage(UltraCanvasUIElement* e, UltraCanvasUIElement* parent, Placed& out) {
    if (!e || out.image) return;
    if (auto* img = dynamic_cast<UltraCanvasImageElement*>(e)) {
        out.image = img;
        out.rowWidth = parent ? parent->GetBounds().width : 0.f;
        return;
    }
    if (auto* c = dynamic_cast<UltraCanvasContainer*>(e))
        for (auto& child : c->GetChildren()) FindImage(child.get(), e, out);
}

// Build `html`, lay it out `width` wide, and find the image.
Placed LayOut(const std::string& html, float width,
              std::shared_ptr<UltraCanvasContainer>& keepAlive) {
    HTML::BuildOptions opts;
    opts.enableImages = true;
    opts.resourceLoader = [](const std::string& src) {
        if (src == "big.png") return std::vector<uint8_t>(std::begin(kPng1000x100), std::end(kPng1000x100));
        return std::vector<uint8_t>(std::begin(kPng160x60), std::end(kPng160x60));
    };
    HTML::ElementBuilder builder;
    HTML::BuildResult r = builder.Build(html, opts);
    keepAlive = r.root;
    Placed placed;
    if (!r.root) return placed;
    r.root->size.width = CSSLayout::Dimension::Px(width);

    CSSLayout::LayoutContext ctx;
    ctx.viewportWidth = width;
    ctx.viewportHeight = 2000;
    CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, width },
                                      { CSSLayout::ConstraintMode::Unbounded, INFINITY } };
    r.root->Measure(mc, ctx);
    r.root->Arrange(Rect2Df{ 0, 0, width, r.root->measured.measuredHeight }, ctx);
    FindImage(r.root.get(), nullptr, placed);
    return placed;
}

void ExpectPlacement(const char* name, const std::string& html, const char* where) {
    std::printf("%s\n", name);
    std::shared_ptr<UltraCanvasContainer> keep;
    Placed p = LayOut(html, 640.f, keep);
    Check(p.image != nullptr, "image built");
    if (!p.image) return;
    const Rect2Df b = p.image->GetBounds();
    CheckNear(b.width, 160.f, "image keeps its own width");
    CheckNear(b.height, 60.f, "image keeps its own height");
    const float free = p.rowWidth - b.width;
    const float expected = std::string(where) == "start"  ? 0.f
                         : std::string(where) == "center" ? free / 2.f
                                                          : free;
    CheckNear(b.x, expected, std::string("x on its line (") + where + ")");
}

// The first label carrying inline images.
UltraCanvasLabel* FindInlineLabel(UltraCanvasUIElement* e) {
    if (!e) return nullptr;
    if (auto* label = dynamic_cast<UltraCanvasLabel*>(e))
        if (!label->GetInlineImages().empty()) return label;
    if (auto* c = dynamic_cast<UltraCanvasContainer*>(e))
        for (auto& child : c->GetChildren())
            if (auto* found = FindInlineLabel(child.get())) return found;
    return nullptr;
}

// RGB of one pixel of an offscreen context.
bool PixelAt(IRenderContext* ctx, int x, int y, int& r, int& g, int& b) {
    auto* cr = static_cast<cairo_t*>(ctx->GetNativeContext());
    if (!cr) return false;
    cairo_surface_t* s = cairo_get_target(cr);
    cairo_surface_flush(s);
    if (x < 0 || y < 0 || x >= cairo_image_surface_get_width(s) ||
        y >= cairo_image_surface_get_height(s)) return false;
    const unsigned char* data = cairo_image_surface_get_data(s);
    const uint32_t px = reinterpret_cast<const uint32_t*>(
        data + y * cairo_image_surface_get_stride(s))[x];
    r = (px >> 16) & 0xFF; g = (px >> 8) & 0xFF; b = px & 0xFF;
    return true;
}

void TestInlineImageFlowsInText() {
    std::printf("image in the middle of a sentence\n");
    HTML::BuildOptions opts;
    opts.enableImages = true;
    opts.resourceLoader = [](const std::string& src) {
        if (src == "big.png") return std::vector<uint8_t>(std::begin(kPng1000x100), std::end(kPng1000x100));
        return std::vector<uint8_t>(std::begin(kPng160x60), std::end(kPng160x60));
    };
    HTML::ElementBuilder builder;
    HTML::BuildResult r = builder.Build("<p>Before <img src=\"x\"> after the picture.</p>", opts);
    Placed placed;
    FindImage(r.root.get(), nullptr, placed);
    Check(placed.image == nullptr, "no line of its own (no separate image element)");
    UltraCanvasLabel* label = FindInlineLabel(r.root.get());
    Check(label != nullptr, "the text run carries the image");
    if (!label) return;

    auto ctx = CreateRenderContext(Size2Di(640, 200), nullptr);
    Check(ctx != nullptr, "offscreen render context");
    if (!ctx) return;
    if (auto* cr = static_cast<cairo_t*>(ctx->GetNativeContext())) {
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
    }
    label->SetBounds(Rect2Df(0, 0, 640, 200));
    label->UpdateInternalLayout(ctx.get());
    const Rect2Df box = label->InlineImageRect(0);
    CheckNear(box.width, 160.f, "inline image width");
    CheckNear(box.height, 60.f, "inline image height");
    Check(box.x > 20.f && box.x < 200.f, "it follows the word before it on the same line");
    label->Render(ctx.get(), Rect2Df(0, 0, 640, 200));
    int red = 0, green = 0, blue = 0;
    const bool read = PixelAt(ctx.get(), static_cast<int>(box.x + box.width / 2),
                              static_cast<int>(box.y + box.height / 2), red, green, blue);
    Check(read && std::abs(red - 40) < 8 && std::abs(green - 110) < 8 && std::abs(blue - 200) < 8,
          "and is drawn there");

    std::printf("inline image wider than the line\n");
    HTML::BuildResult wide = builder.Build("<p>A <img src=\"big.png\"> B</p>", opts);
    UltraCanvasLabel* wideLabel = FindInlineLabel(wide.root.get());
    Check(wideLabel != nullptr, "the text run carries the image");
    if (!wideLabel) return;
    wideLabel->SetBounds(Rect2Df(0, 0, 300, 400));
    wideLabel->UpdateInternalLayout(ctx.get());
    const Rect2Df fitted = wideLabel->InlineImageRect(0);
    Check(fitted.width <= 300.5f && fitted.width > 0.f, "scaled to the line");
    CheckNear(fitted.height, fitted.width / 10.f, "keeps its 10:1 aspect ratio");
}

// Two inline images in one line: the first on the baseline (the reference),
// the second aligned by `secondImage`'s attributes. Returns both boxes.
bool TwoInlineImages(const std::string& secondImage, Rect2Df& reference, Rect2Df& other) {
    HTML::BuildOptions opts;
    opts.enableImages = true;
    opts.resourceLoader = [](const std::string&) {
        return std::vector<uint8_t>(std::begin(kPng160x60), std::end(kPng160x60));
    };
    HTML::ElementBuilder builder;
    HTML::BuildResult r = builder.Build(
        "<p>Text <img src=\"a\"> and <img src=\"b\" " + secondImage + "> end</p>", opts);
    UltraCanvasLabel* label = FindInlineLabel(r.root.get());
    auto ctx = CreateRenderContext(Size2Di(640, 200), nullptr);
    if (!label || !ctx || label->GetInlineImages().size() != 2) return false;
    label->SetBounds(Rect2Df(0, 0, 640, 200));
    label->UpdateInternalLayout(ctx.get());
    reference = label->InlineImageRect(0);
    other = label->InlineImageRect(1);
    return reference.height > 0 && other.height > 0;
}

void TestInlineImageVerticalAlign() {
    std::printf("vertical alignment of inline images\n");
    Rect2Df base, img;
    // The reference stands on the baseline: its bottom is the baseline.
    Check(TwoInlineImages("", base, img), "two images on one line");
    CheckNear(img.y, base.y, "baseline: same place as the reference");

    Check(TwoInlineImages("style=\"vertical-align:middle\"", base, img), "middle (CSS)");
    Check(img.y > base.y + 15.f && img.y + img.height > base.y + base.height + 15.f,
          "middle: centred near the text, reaching below the baseline");
    Rect2Df cssMiddle = img;
    Check(TwoInlineImages("align=\"absmiddle\"", base, img), "absmiddle (attribute)");
    CheckNear(img.y, cssMiddle.y, "align=absmiddle places it like vertical-align:middle");

    Check(TwoInlineImages("style=\"vertical-align:text-top\"", base, img), "text-top");
    Check(img.y > base.y + 30.f, "text-top: its top at the text's top, far below the tall reference's");

    Check(TwoInlineImages("align=\"bottom\"", base, img), "bottom (attribute)");
    const float below = (img.y + img.height) - (base.y + base.height);
    Check(below > 0.5f && below < 12.f, "bottom: its bottom just under the baseline (the descent)");
}

} // namespace

int main() {
    UCImage::InitializeImageSubsysterm("HTMLImageAlignTest");

    ExpectPlacement("plain paragraph", "<p><img src=\"x\"></p>", "start");
    ExpectPlacement("bare image in body", "<body><img src=\"x\"></body>", "start");
    ExpectPlacement("CSS text-align:center", "<p style=\"text-align:center\"><img src=\"x\"></p>", "center");
    ExpectPlacement("align=center on the paragraph", "<p align=\"center\"><img src=\"x\"></p>", "center");
    ExpectPlacement("<center>", "<center><img src=\"x\"></center>", "center");
    ExpectPlacement("align=center on a table cell",
                    "<table><tr><td align=\"center\"><img src=\"x\"></td></tr></table>", "center");
    ExpectPlacement("align=right on a div", "<div align=\"right\"><img src=\"x\"></div>", "end");
    ExpectPlacement("align=center on the image", "<p><img align=\"center\" src=\"x\"></p>", "center");
    ExpectPlacement("linked image", "<p><a href=\"https://x.example\"><img src=\"x\"></a></p>", "start");
    ExpectPlacement("linked image, centred",
                    "<p align=\"center\"><a href=\"https://x.example\"><img src=\"x\"></a></p>", "center");
    ExpectPlacement("CSS beats the align attribute",
                    "<p align=\"center\" style=\"text-align:left\"><img src=\"x\"></p>", "start");

    std::printf("image wider than the column\n");
    {
        std::shared_ptr<UltraCanvasContainer> keep;
        Placed p = LayOut("<p><img src=\"big.png\"></p>", 640.f, keep);
        Check(p.image != nullptr, "image built");
        if (p.image) {
            const Rect2Df b = p.image->GetBounds();
            Check(b.width <= p.rowWidth + 0.5f, "shrinks to the column");
            CheckNear(b.x, 0.f, "starts at the left");
            CheckNear(b.height, b.width / 10.f, "keeps its 10:1 aspect ratio");
        }
    }

    // A mail template's picture: width:100% in a 600px table. Its natural
    // width (1000px) is no minimum - a percentage-sized image can shrink to
    // nothing, as in a browser - so the table stays 600px instead of growing
    // to the picture and pushing the message off the right of the pane.
    std::printf("width:100%% picture in a 600px mail table\n");
    {
        std::shared_ptr<UltraCanvasContainer> keep;
        Placed p = LayOut(
            "<table width=\"600\" style=\"width:600px\" cellpadding=\"0\" cellspacing=\"0\">"
            "<tr><td style=\"width:100%\"><div style=\"max-width:580px\">"
            "<a href=\"https://x.example\"><img src=\"big.png\" width=\"580\" height=\"auto\" "
            "style=\"display:block;height:auto;border:0;width:100%\"></a>"
            "</div></td></tr></table>", 640.f, keep);
        Check(p.image != nullptr, "image built");
        if (p.image) {
            const Rect2Df b = p.image->GetBounds();
            Check(b.width <= 600.5f, "no wider than the 600px table");
            Check(b.width >= 500.f, "fills the column");
            CheckNear(b.height, b.width / 10.f, "keeps its 10:1 aspect ratio");
        }
    }

    TestInlineImageFlowsInText();
    TestInlineImageVerticalAlign();

    std::printf("clicking a linked image\n");
    {
        HTML::BuildOptions opts;
        opts.enableImages = true;
        opts.resourceLoader = [](const std::string&) {
            return std::vector<uint8_t>(std::begin(kPng160x60), std::end(kPng160x60));
        };
        std::string activated;
        opts.onLinkActivated = [&activated](const std::string& href) { activated = href; };
        HTML::ElementBuilder builder;
        HTML::BuildResult r = builder.Build(
            "<p><a href=\"https://shop.example/sale\"><span><img src=\"x\"></span></a></p>", opts);
        Placed p;
        FindImage(r.root.get(), nullptr, p);
        Check(p.image != nullptr, "image built inside <a><span>");
        if (p.image && p.image->onClick) p.image->onClick();
        Check(activated == "https://shop.example/sale", "click activates the link");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}

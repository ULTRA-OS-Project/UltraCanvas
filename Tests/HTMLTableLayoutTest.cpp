// Tests/HTMLTableLayoutTest.cpp
// HTML tables on the CSSLayout table engine, as mail uses them: every row
// shares one set of columns (with colspan / rowspan), px and % widths size
// their columns, nowrap cells keep their line, a narrow table follows
// align="right" - and the "button" idiom (an inline-block <a> around an
// inline <table> whose cell carries the background) is a shrink-to-fit
// coloured box with its caption, not white text on white. Also: CSS px are
// converted to the label's points.
//
// Headless: builds the element tree with HTMLElementBuilder and lays it out
// with the CSSLayout engine; text is measured on an offscreen render context.
// Version: 1.16.0 - merged with main's 1.2.0-1.3.0
// Version: 1.15.0 - letter-spacing
// Version: 1.14.0 - mail tables: placement, unstretched cell children, overflow,
//                  line-height, cell margins, cell percentage heights
// Version: 1.13.0 - percentage heights on blocks; content-box percentage width
// Version: 1.12.0 - percentage min / max sizes; border-box percentage max-width
// Version: 1.11.0 - <img> min / max sizes keep the picture's shape; max-width in %
// Version: 1.10.0 - min-width, min-height, max-height
// Version: 1.9.0 - width / height of a block are its content's (box-sizing)
// Version: 1.8.0 - per-side inline image borders, collapsed table borders, mitred corners
// Version: 1.7.0 - vertical-align on a shared image line
// Version: 1.6.0 - images in a block without text share a line
// Version: 1.5.0 - <img> border, background, padding, margins, border-radius
// Version: 1.4.0 - object-fit, object-position
// Version: 1.3.0 - background-repeat
// Version: 1.2.0 - background-position
// From main:
// Version: 1.2.0 - display:block cells stack (mail columns on a narrow screen);
//                  align=center places a max-width block; a px width is the
//                  content box unless box-sizing: border-box; floats
// Version: 1.3.0 - content flows around floats; clear; a table without a
//                  width is shrink-to-fit; a list marker starts the item's
//                  first block; vertical-align: top on side-by-side boxes
// Version: 1.1.0 - background pictures, margin: auto, @media width
// Last Modified: 2026-10-03
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLElementBuilder.h"
#include "CSSLayout/CSSLayout.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRenderContext.h"

#include <cairo.h>

#include <cmath>
#include <cstdio>
#include <functional>
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

// A container that owns an offscreen render context, so the labels below it
// can measure their text without a window.
struct Host : UltraCanvasContainer {
    Host() : UltraCanvasContainer("host") {}
    void Adopt(std::unique_ptr<IRenderContext> c) { renderContext = std::move(c); }
};

struct Laid {
    std::shared_ptr<Host> host;
    std::shared_ptr<UltraCanvasContainer> root;
};

Laid LayOut(const std::string& html, float width) {
    Laid out;
    out.host = std::make_shared<Host>();
    out.host->Adopt(CreateRenderContext(Size2Di(static_cast<int>(width), 800), nullptr));
    HTML::BuildOptions opts;
    opts.style.baseFontSizePx = 12.f;
    HTML::ElementBuilder builder;
    out.root = builder.Build(html, opts).root;
    if (!out.root) return out;
    out.root->size.width = CSSLayout::Dimension::Px(width);
    out.host->AddChild(out.root);

    CSSLayout::LayoutContext ctx;
    ctx.viewportWidth = width;
    ctx.viewportHeight = 800;
    CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, width },
                                      { CSSLayout::ConstraintMode::Unbounded, INFINITY } };
    out.root->Measure(mc, ctx);
    out.root->Arrange(Rect2Df{ 0, 0, width, out.root->measured.measuredHeight }, ctx);
    return out;
}

// Window-relative rect of every element, in tree order.
struct Placed {
    UltraCanvasUIElement* element;
    Rect2Df rect;
};
void Collect(UltraCanvasUIElement* e, float ox, float oy, std::vector<Placed>& out) {
    const Rect2Df b = e->GetBounds();
    out.push_back({ e, Rect2Df(ox + b.x, oy + b.y, b.width, b.height) });
    if (auto* c = dynamic_cast<UltraCanvasContainer*>(e))
        for (auto& child : c->GetChildren()) Collect(child.get(), ox + b.x, oy + b.y, out);
}

// Cells built for <td>, in document order.
std::vector<Placed> Cells(const Laid& laid) {
    std::vector<Placed> all, cells;
    Collect(laid.root.get(), 0, 0, all);
    for (const auto& p : all)
        if (p.element->GetIdentifier().rfind("html_td_", 0) == 0) cells.push_back(p);
    return cells;
}

const Placed* LabelWith(const std::vector<Placed>& all, const std::string& text) {
    for (const auto& p : all)
        if (auto* l = dynamic_cast<UltraCanvasLabel*>(p.element))
            if (l->GetText().find(text) != std::string::npos) return &p;
    return nullptr;
}

void TestSharedColumns() {
    std::printf("rows share their columns\n");
    Laid laid = LayOut(
        "<table width='100%' cellspacing='2' cellpadding='1'>"
        "<tr><td nowrap>First Name</td><td>Krystal</td><td colspan='2'>buttons</td></tr>"
        "<tr><td nowrap>Hair color / Eyes color</td><td>Black</td><td rowspan='2'>r</td><td>x</td></tr>"
        "<tr><td nowrap>Age</td><td>24 years</td><td>z</td></tr>"
        "</table>", 600.f);
    std::vector<Placed> c = Cells(laid);
    Check(c.size() == 10, "ten cells built");
    if (c.size() != 10) return;
    // Column 0: rows 0, 1, 2 - one x, one width.
    CheckNear(c[3].rect.x, c[0].rect.x, "row 2 label starts where row 1 does");
    CheckNear(c[7].rect.x, c[0].rect.x, "row 3 label starts where row 1 does");
    CheckNear(c[3].rect.width, c[0].rect.width, "label column is one width");
    CheckNear(c[7].rect.width, c[0].rect.width, "on every row");
    // The widest nowrap label sets the column.
    std::vector<Placed> all;
    Collect(laid.root.get(), 0, 0, all);
    const Placed* hair = LabelWith(all, "Hair color");
    Check(hair && hair->rect.width <= c[3].rect.width, "nowrap label fits its column");
    // Column 1 lines up too.
    CheckNear(c[4].rect.x, c[1].rect.x, "value column lines up");
    // colspan=2 covers columns 2 and 3 plus the spacing between them.
    CheckNear(c[2].rect.x, c[5].rect.x, "spanning cell starts at column 2");
    CheckNear(c[2].rect.x + c[2].rect.width, c[6].rect.x + c[6].rect.width,
              "and ends with column 3");
    // rowspan=2 covers rows 2 and 3; the last cell of row 3 skips its slot.
    CheckNear(c[9].rect.x, c[6].rect.x, "a cell after a row-spanning one takes the next column");
    CheckNear(c[5].rect.y + c[5].rect.height, c[9].rect.y + c[9].rect.height,
              "row-spanning cell reaches the bottom of row 3");
    // A 100% table fills the line.
    CheckNear(c[2].rect.x + c[2].rect.width, 600.f - 2.f, "table fills the width");
}

void TestWidths() {
    std::printf("px and %% widths size their columns\n");
    Laid laid = LayOut(
        "<table width='100%' cellspacing='0' cellpadding='0'><tr>"
        "<td width='128'>a</td><td width='25%'>b</td><td>c</td></tr></table>", 528.f);
    std::vector<Placed> c = Cells(laid);
    Check(c.size() == 3, "three cells");
    if (c.size() != 3) return;
    CheckNear(c[0].rect.width, 128.f, "width=128 column");
    CheckNear(c[1].rect.width, 132.f, "width=25% column");
    CheckNear(c[2].rect.width, 268.f, "the auto column takes the rest");
}

void TestButtonAndAlignment() {
    std::printf("mail button: inline-block link around an inline table\n");
    const std::string button =
        "<a href='https://x.example/p' style='color:#ffffff;display:inline-block;'>"
        "<table border='0' cellspacing='0' cellpadding='0' style='border-collapse:collapse;display:inline'>"
        "<tbody><tr><td style='border-radius:4px;display:block;border:solid 1px #0054ab;"
        "background:#0054ab;padding:4px 14px;'>"
        "<a href='https://x.example/p' style='color:#ffffff;display:block;'>"
        "<center><font size='3'><span style='white-space:nowrap;font-size:15px'>Profile</span>"
        "</font></center></a></td></tr></tbody></table></a>";
    Laid laid = LayOut(
        "<table width='100%'><tr><td>Name</td>"
        "<td align='right'><table cellspacing='2' cellpadding='0'><tr><td>" + button +
        "</td></tr></table></td></tr></table>", 600.f);
    std::vector<Placed> all;
    Collect(laid.root.get(), 0, 0, all);
    const Placed* caption = LabelWith(all, "Profile");
    Check(caption != nullptr, "the caption is built");
    if (!caption) return;
    Check(caption->rect.width > 20.f && caption->rect.height > 8.f, "and has a size");
    // The box behind it: the nearest blue ancestor, as wide as the caption
    // plus its 14px padding each side, not the whole line.
    const Placed* blue = nullptr;
    for (const auto& p : all) {
        const Color& bg = p.element->GetBackgroundColor();
        if (bg.r == 0x00 && bg.g == 0x54 && bg.b == 0xAB && bg.a == 255 &&
            p.rect.x <= caption->rect.x && p.rect.x + p.rect.width >= caption->rect.x + caption->rect.width)
            blue = &p;
    }
    Check(blue != nullptr, "the caption sits on the blue background");
    if (!blue) return;
    Check(blue->rect.width < 150.f, "the button shrinks to its caption");
    Check(caption->rect.x - blue->rect.x >= 14.f, "keeping its left padding");
    auto* label = dynamic_cast<UltraCanvasLabel*>(caption->element);
    Check(label && label->GetStyle().textColor.r == 255, "the caption is white");
    // align="right": the button ends at the right edge of the line.
    Check(blue->rect.x + blue->rect.width > 600.f - 12.f, "align=right puts it at the right edge");
}

void TestFontSize() {
    std::printf("CSS px become label points\n");
    Laid laid = LayOut("<p style='font-size:16px'>Sixteen</p>", 400.f);
    std::vector<Placed> all;
    Collect(laid.root.get(), 0, 0, all);
    const Placed* p = LabelWith(all, "Sixteen");
    auto* label = p ? dynamic_cast<UltraCanvasLabel*>(p->element) : nullptr;
    Check(label != nullptr, "label built");
    if (label) CheckNear(label->GetStyle().fontStyle.fontSize, 12.f, "16px is 12pt", 0.01f);
}

void TestBackgroundAndAutoMargins() {
    std::printf("background picture, margin: auto, @media width\n");
    HTML::BuildOptions opts;
    opts.style.baseFontSizePx = 12.f;
    opts.viewportWidth = 600.f;
    opts.resourceLoader = [](const std::string& src) {
        // A 1x1 PNG for the poster; the GIF "fails to load".
        static const std::vector<uint8_t> png = {
            0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
            0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x02,0x00,0x00,0x00,0x90,0x77,0x53,
            0xDE,0x00,0x00,0x00,0x0C,0x49,0x44,0x41,0x54,0x78,0x9C,0x63,0xF8,0x70,0xC0,0x01,
            0x00,0x04,0x94,0x01,0xF1,0xA1,0xC1,0x05,0x69,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,
            0x44,0xAE,0x42,0x60,0x82 };
        return src == "poster.png" ? png : std::vector<uint8_t>{};
    };
    HTML::ElementBuilder builder;
    auto host = std::make_shared<Host>();
    host->Adopt(CreateRenderContext(Size2Di(600, 400), nullptr));
    auto root = builder.Build(
        "<div style='width:146px;height:146px;margin:0 auto;border-radius:20px;"
        "background:url(wave.gif) center / contain no-repeat, url(poster.png) center / contain no-repeat'></div>"
        "<style>@media (min-width:480px) { .col { width:50% !important } }</style>"
        "<div style='font-size:0'><div class='col' style='display:inline-block;width:100%'><p>a</p></div>"
        "<div class='col' style='display:inline-block;width:100%'><p>b</p></div></div>", opts).root;
    Check(root != nullptr, "built");
    if (!root) return;
    root->size.width = CSSLayout::Dimension::Px(600.f);
    host->AddChild(root);
    CSSLayout::LayoutContext ctx;
    ctx.viewportWidth = 600;
    ctx.viewportHeight = 400;
    CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, 600.f },
                                      { CSSLayout::ConstraintMode::Unbounded, INFINITY } };
    root->Measure(mc, ctx);
    root->Arrange(Rect2Df{ 0, 0, 600.f, root->measured.measuredHeight }, ctx);

    std::vector<Placed> all;
    Collect(root.get(), 0, 0, all);
    const Placed* picture = nullptr;
    for (const auto& p : all)
        if (p.element->GetIdentifier().rfind("html_bgimg_", 0) == 0) picture = &p;
    Check(picture != nullptr, "the poster layer is the background (the GIF did not load)");
    if (picture) {
        CheckNear(picture->rect.width, 146.f, "it fills its box's width");
        CheckNear(picture->rect.height, 146.f, "and height");
        CheckNear(picture->rect.x, (600.f - 146.f) / 2.f, "margin: 0 auto centres the box");
    }
    const Placed* a = LabelWith(all, "a");
    const Placed* b = LabelWith(all, "b");
    Check(a && b, "both columns built");
    if (a && b) {
        CheckNear(a->rect.y, b->rect.y, "from 480px the @media rule puts them side by side");
        Check(b->rect.x >= 299.f, "the second in the right half");
    }
}

// A 40x20 picture, solid blue (#2060C0).
const std::vector<uint8_t>& BackgroundPicture40x20() {
    static const std::vector<uint8_t> png = {
        0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
        0x00,0x00,0x00,0x28,0x00,0x00,0x00,0x14,0x08,0x02,0x00,0x00,0x00,0x70,0x24,0xE8,
        0xEC,0x00,0x00,0x00,0x24,0x49,0x44,0x41,0x54,0x78,0x9C,0x63,0x50,0x48,0x38,0x30,
        0x20,0x88,0x61,0xD4,0xE2,0x51,0x8B,0x47,0x2D,0x1E,0xB5,0x78,0xD4,0xE2,0x51,0x8B,
        0x47,0x2D,0x1E,0xB5,0x78,0xE4,0x58,0x0C,0x00,0xBA,0x4F,0xE8,0x2E,0x68,0xAC,0xCD,
        0xD0,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,0x44,0xAE,0x42,0x60,0x82 };
    return png;
}

// background-position places the picture in its box (a 40x20 picture in a
// 200x100 box, unscaled).
void TestBackgroundPosition() {
    std::printf("background-position places the picture\n");

    auto drawRect = [](const std::string& position) {
        HTML::BuildOptions opts;
        opts.style.baseFontSizePx = 12.f;
        opts.resourceLoader = [](const std::string&) { return BackgroundPicture40x20(); };
        HTML::ElementBuilder builder;
        auto host = std::make_shared<Host>();
        host->Adopt(CreateRenderContext(Size2Di(400, 300), nullptr));
        auto root = builder.Build("<div style='width:200px;height:100px;background:url(p.png) no-repeat " +
                                  position + "'></div>", opts).root;
        if (!root) return Rect2Df();
        root->size.width = CSSLayout::Dimension::Px(400.f);
        host->AddChild(root);
        CSSLayout::LayoutContext ctx;
        ctx.viewportWidth = 400;
        ctx.viewportHeight = 300;
        CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, 400.f },
                                          { CSSLayout::ConstraintMode::Unbounded, INFINITY } };
        root->Measure(mc, ctx);
        root->Arrange(Rect2Df{ 0, 0, 400.f, root->measured.measuredHeight }, ctx);
        std::vector<Placed> all;
        Collect(root.get(), 0, 0, all);
        for (const auto& p : all)
            if (auto* img = dynamic_cast<UltraCanvasImageElement*>(p.element))
                return img->ImageDrawRect();
        return Rect2Df();
    };
    Rect2Df r = drawRect("");
    CheckNear(r.x, 0.f, "default: left");
    CheckNear(r.y, 0.f, "default: top");
    CheckNear(r.width, 40.f, "unscaled width");
    r = drawRect("center");
    CheckNear(r.x, 80.f, "center: x");
    CheckNear(r.y, 40.f, "center: y");
    r = drawRect("right bottom");
    CheckNear(r.x, 160.f, "right");
    CheckNear(r.y, 80.f, "bottom");
    r = drawRect("10px 15px");
    CheckNear(r.x, 10.f, "10px from the left");
    CheckNear(r.y, 15.f, "15px from the top");
    r = drawRect("right 10px bottom 5px");
    CheckNear(r.x, 150.f, "10px from the right");
    CheckNear(r.y, 75.f, "5px from the bottom");
    r = drawRect("right top / contain");   // scaled to 200x100: fills the box
    CheckNear(r.width, 200.f, "contain: scaled to the box");
    CheckNear(r.x, 0.f, "no free space left to move in");
}

// background-repeat: the element gets the layer's axes, and the tiles are
// drawn - a 40x20 picture repeated across a 200x100 box, read back from an
// offscreen surface.
void TestBackgroundRepeat() {
    std::printf("background-repeat tiles the picture\n");
    auto build = [](const std::string& background, UltraCanvasImageElement*& out,
                    std::shared_ptr<Host>& host) {
        HTML::BuildOptions opts;
        opts.style.baseFontSizePx = 12.f;
        opts.resourceLoader = [](const std::string&) { return BackgroundPicture40x20(); };
        HTML::ElementBuilder builder;
        host = std::make_shared<Host>();
        host->Adopt(CreateRenderContext(Size2Di(400, 300), nullptr));
        auto root = builder.Build("<div style='width:200px;height:100px;background:#ffffff url(p.png) " +
                                  background + "'></div>", opts).root;
        out = nullptr;
        if (!root) return;
        root->size.width = CSSLayout::Dimension::Px(400.f);
        host->AddChild(root);
        CSSLayout::LayoutContext ctx;
        ctx.viewportWidth = 400;
        ctx.viewportHeight = 300;
        CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, 400.f },
                                          { CSSLayout::ConstraintMode::Unbounded, INFINITY } };
        root->Measure(mc, ctx);
        root->Arrange(Rect2Df{ 0, 0, 400.f, root->measured.measuredHeight }, ctx);
        std::vector<Placed> all;
        Collect(root.get(), 0, 0, all);
        for (const auto& p : all)
            if (auto* img = dynamic_cast<UltraCanvasImageElement*>(p.element)) out = img;
    };
    UltraCanvasImageElement* image = nullptr;
    std::shared_ptr<Host> host;
    build("", image, host);
    Check(image && image->GetImageRepeatX() && image->GetImageRepeatY(), "default: repeats both ways");
    build("repeat-x", image, host);
    Check(image && image->GetImageRepeatX() && !image->GetImageRepeatY(), "repeat-x");
    build("no-repeat", image, host);
    Check(image && !image->GetImageRepeatX() && !image->GetImageRepeatY(), "no-repeat");

    // Drawn: the element alone, 200x100, on a white offscreen surface.
    auto ctx = CreateRenderContext(Size2Di(200, 100), nullptr);
    Check(ctx != nullptr, "offscreen render context");
    if (!ctx) return;
    auto* cr = static_cast<cairo_t*>(ctx->GetNativeContext());
    auto draw = [&](bool rx, bool ry) {
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        auto tiles = std::make_shared<UltraCanvasImageElement>("tiles");
        tiles->LoadFromImage(UCImageRaster::LoadFromMemory(BackgroundPicture40x20()));
        tiles->SetFitMode(ImageFitMode::NoScale);
        tiles->SetImagePosition(ImagePosition{ ImageAxisPosition::Fraction(0.f),
                                               ImageAxisPosition::Fraction(0.f) });
        tiles->SetImageRepeat(rx, ry);
        tiles->SetBounds(Rect2Df(0, 0, 200, 100));
        tiles->Render(ctx.get(), Rect2Df(0, 0, 200, 100));
    };
    auto isPicture = [&](int x, int y) {
        cairo_surface_t* s = cairo_get_target(cr);
        cairo_surface_flush(s);
        const uint32_t px = reinterpret_cast<const uint32_t*>(
            cairo_image_surface_get_data(s) + y * cairo_image_surface_get_stride(s))[x];
        return ((px >> 16) & 0xFF) < 100 && (px & 0xFF) > 150;   // the picture's blue
    };
    draw(true, true);
    Check(isPicture(5, 5) && isPicture(185, 85), "repeat: tiles reach the far corner");
    draw(true, false);
    Check(isPicture(185, 5) && !isPicture(185, 85), "repeat-x: along the top row only");
    draw(false, true);
    Check(isPicture(5, 85) && !isPicture(185, 5), "repeat-y: down the left column only");
    draw(false, false);
    Check(isPicture(5, 5) && !isPicture(185, 5) && !isPicture(5, 85), "no-repeat: one picture");
}

// object-fit / object-position place an <img>'s picture in its box (a 40x20
// picture in a 200x100 <img>).
void TestObjectFitPosition() {
    std::printf("object-fit / object-position place the picture\n");

    auto drawRect = [](const std::string& style) {
        HTML::BuildOptions opts;
        opts.style.baseFontSizePx = 12.f;
        opts.resourceLoader = [](const std::string&) { return BackgroundPicture40x20(); };
        HTML::ElementBuilder builder;
        auto host = std::make_shared<Host>();
        host->Adopt(CreateRenderContext(Size2Di(400, 300), nullptr));
        auto root = builder.Build("<div><img src='p.png' width='200' height='100' style='" +
                                  style + "'></div>", opts).root;
        if (!root) return Rect2Df();
        root->size.width = CSSLayout::Dimension::Px(400.f);
        host->AddChild(root);
        CSSLayout::LayoutContext ctx;
        ctx.viewportWidth = 400;
        ctx.viewportHeight = 300;
        CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, 400.f },
                                          { CSSLayout::ConstraintMode::Unbounded, INFINITY } };
        root->Measure(mc, ctx);
        root->Arrange(Rect2Df{ 0, 0, 400.f, root->measured.measuredHeight }, ctx);
        std::vector<Placed> all;
        Collect(root.get(), 0, 0, all);
        for (const auto& p : all)
            if (auto* img = dynamic_cast<UltraCanvasImageElement*>(p.element)) {
                Rect2Df r = img->ImageDrawRect();
                Rect2Df box = img->GetLocalContentRect();
                return Rect2Df(r.x - box.x, r.y - box.y, r.width, r.height);
            }
        return Rect2Df();
    };
    Rect2Df r = drawRect("");
    CheckNear(r.width, 200.f, "default fill: stretched across");
    CheckNear(r.height, 100.f, "default fill: stretched down");
    r = drawRect("object-fit:contain;object-position:right");
    CheckNear(r.width, 200.f, "contain: 2:1 picture fills the 2:1 box");
    r = drawRect("object-fit:none");
    CheckNear(r.width, 40.f, "none: natural width");
    CheckNear(r.x, 80.f, "none: centred by default");
    CheckNear(r.y, 40.f, "none: centred vertically");
    r = drawRect("object-fit:none;object-position:right bottom");
    CheckNear(r.x, 160.f, "none: right");
    CheckNear(r.y, 80.f, "none: bottom");
    r = drawRect("object-fit:none;object-position:10px 15px");
    CheckNear(r.x, 10.f, "none: 10px from the left");
    CheckNear(r.y, 15.f, "none: 15px from the top");
    r = drawRect("object-fit:scale-down;object-position:left top");
    CheckNear(r.width, 40.f, "scale-down: never enlarged");
    CheckNear(r.x, 0.f, "scale-down: left");
    r = drawRect("object-fit:cover;object-position:left");
    CheckNear(r.width, 200.f, "cover: covers the box");
}

// An <img>'s CSS box: width / height size the picture, border and padding
// go around it, horizontal margins stay outside; an inline image reserves its
// whole frame on the line; percent radii round a square to a circle.
void TestImageBox() {
    std::printf("<img> border, background, padding and radius\n");
    struct Built {
        std::shared_ptr<Host> host;
        std::shared_ptr<UltraCanvasUIElement> root;
        std::vector<Placed> all;
    };
    auto build = [](const std::string& html) {
        Built b;
        HTML::BuildOptions opts;
        opts.style.baseFontSizePx = 12.f;
        opts.resourceLoader = [](const std::string&) { return BackgroundPicture40x20(); };
        HTML::ElementBuilder builder;
        b.host = std::make_shared<Host>();
        b.host->Adopt(CreateRenderContext(Size2Di(400, 300), nullptr));
        b.root = builder.Build(html, opts).root;
        if (!b.root) return b;
        b.root->size.width = CSSLayout::Dimension::Px(400.f);
        b.host->AddChild(b.root);
        CSSLayout::LayoutContext ctx;
        ctx.viewportWidth = 400;
        ctx.viewportHeight = 300;
        CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, 400.f },
                                          { CSSLayout::ConstraintMode::Unbounded, INFINITY } };
        b.root->Measure(mc, ctx);
        b.root->Arrange(Rect2Df{ 0, 0, 400.f, b.root->measured.measuredHeight }, ctx);
        Collect(b.root.get(), 0, 0, b.all);
        return b;
    };
    auto imageIn = [](const Built& b) -> const Placed* {
        for (const auto& p : b.all)
            if (p.element->GetIdentifier().rfind("html_img_", 0) == 0) return &p;
        return nullptr;
    };

    // Block: 160x90 picture + 5px padding + 3px border = 176x106, 20px in.
    Built b = build("<div><img src='p.png' width='160' height='90' style='border:3px solid #2244aa;"
                    "background:#ffe080;padding:5px;margin-left:20px'></div>");
    const Placed* img = imageIn(b);
    Check(img != nullptr, "block image built");
    if (img) {
        CheckNear(img->rect.width, 176.f, "width: picture + padding + border");
        CheckNear(img->rect.height, 106.f, "height: picture + padding + border");
        CheckNear(img->rect.x, 20.f, "margin-left is a margin");
        auto* e = static_cast<UltraCanvasImageElement*>(img->element);
        Rect2Df content = e->GetLocalContentRect();
        CheckNear(content.width, 160.f, "the picture keeps its 160px");
        CheckNear(content.x, 8.f, "inside border and padding");
        Check(e->GetBackgroundColor().a > 0, "background colour set");
    }

    // <img border="2"> inside a link: a 2px border in the link colour.
    b = build("<div><a href='x'><img src='p.png' border='2' width='40' height='20'></a></div>");
    img = imageIn(b);
    if (img) {
        CheckNear(img->rect.width, 44.f, "border attribute adds its width");
        CheckNear(img->element->GetBorderLeftWidth(), 2.f, "border attribute: 2px");
    } else Check(false, "linked image built");

    // Inline: the frame is reserved on the line and the picture sits inside.
    b = build("<p>text <img src='p.png' style='border:2px solid #2244aa;background:#ffe080;"
              "padding:4px;margin:0 6px;border-radius:50%'> more</p>");
    const Placed* line = LabelWith(b.all, "text");
    auto* label = line ? dynamic_cast<UltraCanvasLabel*>(line->element) : nullptr;
    Check(label && label->GetInlineImages().size() == 1, "inline image in the label");
    if (label && label->GetInlineImages().size() == 1) {
        const LabelInlineImageFrame& f = label->GetInlineImages()[0].frame;
        CheckNear(f.borderLeft.width, 2.f, "inline border width");
        CheckNear(f.paddingLeft, 4.f, "inline padding");
        CheckNear(f.marginLeft, 6.f, "inline margin");
        Check(f.background.a > 0, "inline background");
        CheckNear(f.borderRadius, 16.f, "50% of the 52x32 box, the shorter side");
        Rect2Df box = label->InlineImageBoxRect(0);
        Rect2Df pic = label->InlineImageRect(0);
        CheckNear(box.width, 52.f, "border box: 40 + 2*4 + 2*2");
        CheckNear(box.height, 32.f, "border box: 20 + 2*4 + 2*2");
        CheckNear(pic.width, 40.f, "the picture at its own size");
        CheckNear(pic.x - box.x, 6.f, "inside border and padding");
    }
}

// Images in a block without text share a line, side by side, a space apart
// where the HTML has whitespace; they wrap when the line is full, and
// display:block or <br> still breaks the line.
void TestImagesShareLine() {
    std::printf("images share a line\n");
    auto images = [](const std::string& html, float width) {
        HTML::BuildOptions opts;
        opts.style.baseFontSizePx = 12.f;
        opts.resourceLoader = [](const std::string&) { return BackgroundPicture40x20(); };
        HTML::ElementBuilder builder;
        auto host = std::make_shared<Host>();
        host->Adopt(CreateRenderContext(Size2Di(400, 300), nullptr));
        auto root = builder.Build(html, opts).root;
        std::vector<Rect2Df> out;
        if (!root) return out;
        root->size.width = CSSLayout::Dimension::Px(width);
        host->AddChild(root);
        CSSLayout::LayoutContext ctx;
        ctx.viewportWidth = width;
        ctx.viewportHeight = 300;
        CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, width },
                                          { CSSLayout::ConstraintMode::Unbounded, INFINITY } };
        root->Measure(mc, ctx);
        root->Arrange(Rect2Df{ 0, 0, width, root->measured.measuredHeight }, ctx);
        std::vector<Placed> all;
        Collect(root.get(), 0, 0, all);
        for (const auto& p : all)
            if (p.element->GetIdentifier().rfind("html_img_", 0) == 0) out.push_back(p.rect);
        return out;
    };
    auto r = images("<p><a href='a'><img src='p.png'></a> <a href='b'><img src='p.png'></a>"
                    "<img src='p.png' height='40' width='80'></p>", 400.f);
    Check(r.size() == 3, "three images");
    if (r.size() == 3) {
        Check(r[1].x > r[0].x + 40.f && r[1].x < r[0].x + 46.f, "second beside the first, a space apart");
        CheckNear(r[2].x, r[1].x + 40.f, "no whitespace: no gap");
        CheckNear(r[0].y + r[0].height, r[2].y + r[2].height, "they stand on one bottom line");
    }
    r = images("<div><img src='p.png' width='150'> <img src='p.png' width='150'> "
               "<img src='p.png' width='150'></div>", 400.f);
    if (r.size() == 3) {
        CheckNear(r[2].x, 0.f, "the third wraps to the start of the next line");
        Check(r[2].y >= r[0].y + r[0].height - 0.5f, "below the first two");
        CheckNear(r[1].y, r[0].y, "the first two on one line");
    } else Check(false, "three wide images");
    r = images("<div><img src='p.png' style='display:block'><img src='p.png'></div>", 400.f);
    if (r.size() == 2) Check(r[1].y >= r[0].y + 20.f - 0.5f, "display:block keeps its own line");
    else Check(false, "two images (block)");
    r = images("<div><img src='p.png'><br><img src='p.png'></div>", 400.f);
    if (r.size() == 2) Check(r[1].y >= r[0].y + 20.f - 0.5f, "<br> breaks the line");
    else Check(false, "two images (br)");
    r = images("<div><img src='p.png' width='80' height='60'> "
               "<img src='p.png' style='vertical-align:top'> "
               "<img src='p.png' style='vertical-align:middle'> <img src='p.png'></div>", 400.f);
    if (r.size() == 4) {
        CheckNear(r[1].y, r[0].y, "vertical-align: top");
        CheckNear(r[2].y + 10.f, r[0].y + 30.f, "vertical-align: middle");
        CheckNear(r[3].y + 20.f, r[0].y + 60.f, "baseline: on the line's bottom");
    } else Check(false, "four images (vertical-align)");
}

// Borders: an inline image's sides each their own; a collapsed table draws a
// shared cell edge once (the wider wins); two coloured sides meet on the
// corner's diagonal.
void TestBorderSides() {
    std::printf("borders per side, collapsed, mitred\n");
    auto build = [](const std::string& html, std::shared_ptr<Host>& host) {
        HTML::BuildOptions opts;
        opts.style.baseFontSizePx = 12.f;
        opts.resourceLoader = [](const std::string&) { return BackgroundPicture40x20(); };
        HTML::ElementBuilder builder;
        host = std::make_shared<Host>();
        host->Adopt(CreateRenderContext(Size2Di(400, 300), nullptr));
        auto root = builder.Build(html, opts).root;
        std::vector<Placed> all;
        if (!root) return all;
        root->size.width = CSSLayout::Dimension::Px(400.f);
        host->AddChild(root);
        CSSLayout::LayoutContext ctx;
        ctx.viewportWidth = 400;
        ctx.viewportHeight = 300;
        CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, 400.f },
                                          { CSSLayout::ConstraintMode::Unbounded, INFINITY } };
        root->Measure(mc, ctx);
        root->Arrange(Rect2Df{ 0, 0, 400.f, root->measured.measuredHeight }, ctx);
        Collect(root.get(), 0, 0, all);
        return all;
    };
    std::shared_ptr<Host> host;

    // Inline image: left 3px, bottom 5px, nothing else.
    auto all = build("<p>text <img src='p.png' style='border-left:3px solid #00f;"
                     "border-bottom:5px dashed #f00'> more</p>", host);
    const Placed* line = LabelWith(all, "text");
    auto* label = line ? dynamic_cast<UltraCanvasLabel*>(line->element) : nullptr;
    if (label && label->GetInlineImages().size() == 1) {
        const LabelInlineImageFrame& f = label->GetInlineImages()[0].frame;
        CheckNear(f.borderLeft.width, 3.f, "inline: left side");
        CheckNear(f.borderBottom.width, 5.f, "inline: bottom side");
        CheckNear(f.borderTop.width, 0.f, "inline: no top");
        Check(!f.borderBottom.dash.dashes.empty() && f.borderLeft.dash.dashes.empty(), "inline: dashed bottom only");
        Rect2Df box = label->InlineImageBoxRect(0), pic = label->InlineImageRect(0);
        CheckNear(box.width, 43.f, "inline box: 40 + 3 left");
        CheckNear(box.height, 25.f, "inline box: 20 + 5 bottom");
        CheckNear(pic.x - box.x, 3.f, "picture inside the left border");
        CheckNear(pic.y - box.y, 0.f, "no top border to clear");
    } else Check(false, "inline image built");

    // Collapsed: a | b with 1px each, then c with 3px - shared edges once.
    all = build("<table style='border-collapse:collapse'><tr>"
                "<td style='border:1px solid #999'>a</td><td style='border:1px solid #999'>b</td>"
                "<td style='border:3px solid #c00'>c</td></tr></table>", host);
    std::vector<UltraCanvasUIElement*> cells;
    for (const auto& p : all)
        if (p.element->GetIdentifier().rfind("html_td_", 0) == 0) cells.push_back(p.element);
    if (cells.size() == 3) {
        CheckNear(cells[0]->GetBorderRightWidth(), 1.f, "a keeps the shared a|b edge");
        CheckNear(cells[1]->GetBorderLeftWidth(), 0.f, "b drops it");
        CheckNear(cells[1]->GetBorderRightWidth(), 3.f, "the wider c border wins b|c");
        CheckNear(cells[2]->GetBorderLeftWidth(), 0.f, "c drops its left");
        CheckNear(cells[2]->GetBorderRightWidth(), 3.f, "outer edges stay");
    } else Check(false, "three collapsed cells");

    // Separate (the default): every cell keeps every side.
    all = build("<table><tr><td style='border:1px solid #999'>a</td>"
                "<td style='border:1px solid #999'>b</td></tr></table>", host);
    cells.clear();
    for (const auto& p : all)
        if (p.element->GetIdentifier().rfind("html_td_", 0) == 0) cells.push_back(p.element);
    if (cells.size() == 2) CheckNear(cells[1]->GetBorderLeftWidth(), 1.f, "separate: both sides kept");
    else Check(false, "two separate cells");

    // Mitred: a 10px red top meets a 10px blue left on the diagonal.
    auto ctx = CreateRenderContext(Size2Di(40, 40), nullptr);
    if (!ctx) { Check(false, "offscreen context"); return; }
    auto* cr = static_cast<cairo_t*>(ctx->GetNativeContext());
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    ctx->DrawRoundedRectangleWidthBorders(Rect2Dd(0, 0, 40, 40), false, 10, 10, 10, 10,
        Color(0, 0, 255, 255), Color(0, 0, 255, 255), Color(255, 0, 0, 255), Color(255, 0, 0, 255),
        0, 0, 0, 0, UCDashPattern(), UCDashPattern(), UCDashPattern(), UCDashPattern());
    cairo_surface_t* surf = cairo_get_target(cr);
    cairo_surface_flush(surf);
    auto pixel = [&](int x, int y) {
        return reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(surf) +
                                                 y * cairo_image_surface_get_stride(surf))[x];
    };
    auto isRed = [&](int x, int y) { uint32_t p = pixel(x, y); return ((p >> 16) & 0xFF) > 200 && (p & 0xFF) < 50; };
    auto isBlue = [&](int x, int y) { uint32_t p = pixel(x, y); return (p & 0xFF) > 200 && ((p >> 16) & 0xFF) < 50; };
    Check(isRed(7, 2), "above the diagonal: the top's colour");
    Check(isBlue(2, 7), "below it: the left's colour");
    Check(isRed(20, 5) && isBlue(5, 20), "along the sides");
    Check(isRed(33, 2) && isBlue(37, 7), "top-right corner mitred too");
}

// width / height size a block's content (CSS content-box): padding and
// border go around them. box-sizing: border-box keeps the box whole; a
// percentage is the content's share of the line; max-width limits the
// content, or the box with border-box.
void TestBoxSizing() {
    std::printf("width and height are the content's\n");
    auto box = [](const std::string& css) {
        HTML::BuildOptions opts;
        opts.style.baseFontSizePx = 12.f;
        HTML::ElementBuilder builder;
        auto host = std::make_shared<Host>();
        host->Adopt(CreateRenderContext(Size2Di(400, 300), nullptr));
        auto root = builder.Build("<div id='t' style='" + css + "'>x</div>", opts).root;
        if (!root) return Rect2Df();
        root->size.width = CSSLayout::Dimension::Px(400.f);
        host->AddChild(root);
        CSSLayout::LayoutContext ctx;
        ctx.viewportWidth = 400;
        ctx.viewportHeight = 300;
        CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, 400.f },
                                          { CSSLayout::ConstraintMode::Unbounded, INFINITY } };
        root->Measure(mc, ctx);
        root->Arrange(Rect2Df{ 0, 0, 400.f, root->measured.measuredHeight }, ctx);
        std::vector<Placed> all;
        Collect(root.get(), 0, 0, all);
        for (const auto& p : all)
            if (p.element->GetIdentifier().rfind("html_div_", 0) == 0) return p.rect;
        return Rect2Df();
    };
    Rect2Df r = box("width:120px;height:30px;padding:4px;border:10px solid #000");
    CheckNear(r.width, 148.f, "120 + 2*4 padding + 2*10 border");
    CheckNear(r.height, 58.f, "30 + 2*4 + 2*10");
    r = box("width:120px;height:30px;padding:4px;border:10px solid #000;box-sizing:border-box");
    CheckNear(r.width, 120.f, "border-box: the whole box");
    CheckNear(r.height, 30.f, "border-box height");
    r = box("width:50%;padding:0 10px;border:2px solid #000");
    CheckNear(r.width, 224.f, "50% of 400 + padding + border");
    r = box("max-width:200px;padding:10px;border:2px solid #000");
    CheckNear(r.width, 224.f, "max-width limits the content");
    r = box("max-width:200px;padding:10px;border:2px solid #000;box-sizing:border-box");
    CheckNear(r.width, 200.f, "border-box max-width limits the box");
    r = box("padding:8px;background:#eee");
    CheckNear(r.width, 400.f, "no width: the line's");
    r = box("min-height:60px;border:1px solid #000");
    CheckNear(r.height, 62.f, "min-height: the content's, border around it");
    r = box("max-height:30px;height:80px");
    CheckNear(r.height, 30.f, "max-height beats height");
    r = box("min-width:300px;width:100px");
    CheckNear(r.width, 300.f, "min-width beats width");
    r = box("min-height:60px;padding:10px;box-sizing:border-box");
    CheckNear(r.height, 60.f, "border-box min-height: the whole box");
    r = box("min-height:auto;max-height:none");
    Check(r.height > 0.f && r.height < 30.f, "auto / none: no limit");
}

// <img> with min / max width and height: shaped as CSS shapes a replaced
// element (40x20 picture); max-width in percent on images, tables, blocks.
void TestImageLimits() {
    std::printf("image min / max sizes, percentage max-width\n");
    auto first = [](const std::string& html, const char* prefix) {
        HTML::BuildOptions opts;
        opts.style.baseFontSizePx = 12.f;
        opts.resourceLoader = [](const std::string&) { return BackgroundPicture40x20(); };
        HTML::ElementBuilder builder;
        auto host = std::make_shared<Host>();
        host->Adopt(CreateRenderContext(Size2Di(400, 300), nullptr));
        auto root = builder.Build(html, opts).root;
        if (!root) return Rect2Df();
        root->size.width = CSSLayout::Dimension::Px(400.f);
        host->AddChild(root);
        CSSLayout::LayoutContext ctx;
        ctx.viewportWidth = 400;
        ctx.viewportHeight = 300;
        CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, 400.f },
                                          { CSSLayout::ConstraintMode::Unbounded, INFINITY } };
        root->Measure(mc, ctx);
        root->Arrange(Rect2Df{ 0, 0, 400.f, root->measured.measuredHeight }, ctx);
        std::vector<Placed> all;
        Collect(root.get(), 0, 0, all);
        for (const auto& p : all)
            if (p.element->GetIdentifier().rfind(prefix, 0) == 0) return p.rect;
        return Rect2Df();
    };
    Rect2Df r = first("<div><img src='p.png' style='max-height:10px'></div>", "html_img_");
    CheckNear(r.width, 20.f, "max-height: the width follows the shape");
    CheckNear(r.height, 10.f, "max-height");
    r = first("<div><img src='p.png' style='min-width:80px'></div>", "html_img_");
    CheckNear(r.width, 80.f, "min-width enlarges");
    CheckNear(r.height, 40.f, "in proportion");
    r = first("<div><img src='p.png' width='40' style='max-height:10px'></div>", "html_img_");
    CheckNear(r.width, 40.f, "a given width stays");
    CheckNear(r.height, 10.f, "the height is held");
    r = first("<div><img src='p.png' style='min-width:80px;max-height:30px'></div>", "html_img_");
    CheckNear(r.width, 80.f, "both limits: min-width");
    CheckNear(r.height, 30.f, "both limits: max-height");
    r = first("<div><img src='p.png' width='400' height='200' style='max-width:50%'></div>", "html_img_");
    CheckNear(r.width, 200.f, "max-width: 50% of the line");
    r = first("<div><img src='p.png' width='800' style='max-width:100%'></div>", "html_img_");
    CheckNear(r.width, 400.f, "max-width: 100%");
    CheckNear(r.height, 200.f, "shrunk in proportion");
    r = first("<table style='max-width:50%'><tr><td>a long line of text that would like the "
              "whole width of the page and more besides</td></tr></table>", "html_table_");
    Check(r.width > 0.f && r.width <= 200.5f, "a table's max-width: 50%");
    r = first("<div id='x' style='max-width:50%;margin:0 auto'>x</div>", "html_div_");
    CheckNear(r.width, 200.f, "a block's max-width: 50%");
    CheckNear(r.x, 100.f, "centred by margin: auto");
    r = first("<div style='max-width:50%;padding:10px;border:2px solid #000;box-sizing:border-box'>"
              "a line of text long enough to fill the whole width of the page</div>", "html_div_");
    CheckNear(r.width, 200.f, "border-box max-width: 50% is the whole box");
    r = first("<div style='min-width:75%;width:10px'>x</div>", "html_div_");
    CheckNear(r.width, 300.f, "min-width: 75%");
    auto inner = [&](const std::string& html) {
        // The second div: the one inside the 100px-high box.
        HTML::BuildOptions opts;
        opts.style.baseFontSizePx = 12.f;
        HTML::ElementBuilder builder;
        auto host = std::make_shared<Host>();
        host->Adopt(CreateRenderContext(Size2Di(400, 300), nullptr));
        auto root = builder.Build(html, opts).root;
        if (!root) return Rect2Df();
        root->size.width = CSSLayout::Dimension::Px(400.f);
        host->AddChild(root);
        CSSLayout::LayoutContext ctx;
        ctx.viewportWidth = 400;
        ctx.viewportHeight = 300;
        CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, 400.f },
                                          { CSSLayout::ConstraintMode::Unbounded, INFINITY } };
        root->Measure(mc, ctx);
        root->Arrange(Rect2Df{ 0, 0, 400.f, root->measured.measuredHeight }, ctx);
        std::vector<Placed> all;
        Collect(root.get(), 0, 0, all);
        int n = 0;
        for (const auto& p : all)
            if (p.element->GetIdentifier().rfind("html_div_", 0) == 0 && ++n == 2) return p.rect;
        return Rect2Df();
    };
    r = inner("<div style='height:100px'><div style='min-height:50%'>x</div></div>");
    CheckNear(r.height, 50.f, "min-height: 50% of a set height");
    r = inner("<div style='height:100px'><div style='max-height:20%;height:90px'>x</div></div>");
    CheckNear(r.height, 20.f, "max-height: 20% of a set height");
    r = inner("<div><div style='min-height:50%'>x</div></div>");
    Check(r.height > 0.f && r.height < 30.f, "an auto-height parent: no limit");
    r = inner("<div style='height:200px'><div style='height:50%'>x</div></div>");
    CheckNear(r.height, 100.f, "height: 50% of a set height");
    r = inner("<div style='height:200px'><div style='height:25%;padding:10px;border:2px solid #000'>x</div></div>");
    CheckNear(r.height, 74.f, "height: 25% is the content's, padding and border around it");
    r = inner("<div><div style='height:50%'>x</div></div>");
    Check(r.height > 0.f && r.height < 30.f, "height: 50% of an auto height is auto");
    r = first("<div style='width:50%;padding:0 10px;border:2px solid #000'>x</div>", "html_div_");
    CheckNear(r.width, 224.f, "width: 50% content-box: 200 + padding + border");
    r = first("<div style='width:30%;width:120px'>x</div>", "html_div_");
    CheckNear(r.width, 120.f, "a later width replaces a percentage");
    r = first("<div><img src='p.png' style='min-width:25%'></div>", "html_img_");
    CheckNear(r.width, 100.f, "an image's min-width: 25%");
    CheckNear(r.height, 50.f, "its height follows");
}

// What a mail like Yahoo's sign-in notice needs: tables placed by their
// container's align, a cell's children keeping their own width, content
// drawn past a narrower box, line-height, a cell keeping its last child's
// bottom margin, and percentage heights on cells against the table's height.
void TestMailTables() {
    std::printf("mail tables: placement, widths, overflow, line-height\n");
    struct Built { std::shared_ptr<Host> host; std::vector<Placed> all; };
    auto build = [](const std::string& html) {
        Built b;
        HTML::BuildOptions opts;
        opts.style.baseFontSizePx = 12.f;
        HTML::ElementBuilder builder;
        b.host = std::make_shared<Host>();
        b.host->Adopt(CreateRenderContext(Size2Di(400, 300), nullptr));
        auto root = builder.Build(html, opts).root;
        if (!root) return b;
        root->size.width = CSSLayout::Dimension::Px(400.f);
        b.host->AddChild(root);
        CSSLayout::LayoutContext ctx;
        ctx.viewportWidth = 400;
        ctx.viewportHeight = 300;
        CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, 400.f },
                                          { CSSLayout::ConstraintMode::Unbounded, INFINITY } };
        root->Measure(mc, ctx);
        root->Arrange(Rect2Df{ 0, 0, 400.f, root->measured.measuredHeight }, ctx);
        Collect(root.get(), 0, 0, b.all);
        return b;
    };
    auto nth = [](const Built& b, const char* prefix, int n) -> const Placed* {
        for (const auto& p : b.all)
            if (p.element->GetIdentifier().rfind(prefix, 0) == 0 && n-- == 0) return &p;
        return nullptr;
    };
    // A table with text-align:left in a centring cell is still centred.
    Built b = build("<table width='100%' cellpadding='0' cellspacing='0'><tr><td align='center'>"
                    "<table width='200' style='text-align:left'><tr><td>x</td></tr></table>"
                    "</td></tr></table>");
    const Placed* t = nth(b, "html_table_", 1);
    if (t) {
        CheckNear(t->rect.width, 200.f, "the inner table keeps width='200'");
        CheckNear(t->rect.x, 100.f, "and is centred by the cell's align");
    } else Check(false, "inner table");
    // A div with a width in a cell is not stretched across it.
    b = build("<table width='300' cellpadding='0' cellspacing='0'><tr><td>"
              "<div style='width:120px'>x</div></td></tr></table>");
    const Placed* d = nth(b, "html_div_", 0);
    if (d) CheckNear(d->rect.width, 120.f, "a div's own width in a cell");
    else Check(false, "div in a cell");
    // Overflow is drawn: HTML boxes do not clip, unless overflow: hidden.
    b = build("<div style='width:100px'><p style='width:200px'>x</p></div>"
              "<div style='width:100px;overflow:hidden'>y</div>");
    const Placed* v = nth(b, "html_div_", 0);
    const Placed* h = nth(b, "html_div_", 1);
    auto* vc = v ? dynamic_cast<UltraCanvasContainer*>(v->element) : nullptr;
    auto* hc = h ? dynamic_cast<UltraCanvasContainer*>(h->element) : nullptr;
    Check(vc && !vc->GetContainerStyle().clipChildren, "overflow visible: no clip");
    Check(hc && hc->GetContainerStyle().clipChildren, "overflow: hidden clips");
    // line-height: three lines of 20px.
    b = build("<p style='width:60px;line-height:20px'>one two three four five six</p>");
    const Placed* lab = LabelWith(b.all, "one");
    if (lab) {
        Check(lab->rect.height >= 59.f && std::fmod(lab->rect.height + 0.5f, 20.f) < 1.5f,
              "line-height: 20px per line");
    } else Check(false, "line-height label");
    // letter-spacing: the block's around the run, an inline element's inside.
    b = build("<p style='letter-spacing:2px'>ab <span style='letter-spacing:5px'>cd</span></p>");
    lab = LabelWith(b.all, "ab");
    if (lab) {
        auto* l = static_cast<UltraCanvasLabel*>(lab->element);
        Check(l->GetText().rfind("<span letter_spacing=\"2048\">", 0) == 0, "block spacing: 2px");
        Check(l->GetText().find("letter_spacing=\"5120\">cd") != std::string::npos, "span spacing: 5px");
    } else Check(false, "letter-spacing label");
    // A shrink-to-fit button with letter-spacing keeps its caption on one line
    // (Pango breaks on the spacing after the last letter that its extents
    // leave out).
    b = build("<table><tr><td style='padding:15px 30px;font-size:14px;line-height:18px;"
              "letter-spacing:2px'>FIND OUT WHO</td></tr></table>");
    lab = LabelWith(b.all, "FIND");
    if (lab) CheckNear(lab->rect.height, 18.f, "spaced caption: one line");
    else Check(false, "spaced caption");
    // A cell keeps its last child's bottom margin.
    b = build("<table cellpadding='0' cellspacing='0'><tr><td>"
              "<div style='height:30px;margin-bottom:20px'></div></td></tr></table>");
    const Placed* cell = nth(b, "html_td_", 0);
    if (cell) CheckNear(cell->rect.height, 50.f, "30px box + 20px bottom margin");
    else Check(false, "margin cell");
    // Percentage heights on cells: of the table's set height.
    b = build("<table style='height:300px' cellpadding='0' cellspacing='0'>"
              "<tr><td style='height:50%'>e</td></tr><tr><td>f</td></tr></table>");
    const Placed* c0 = nth(b, "html_td_", 0);
    const Placed* c1 = nth(b, "html_td_", 1);
    if (c0 && c1) {
        CheckNear(c0->rect.height, 150.f, "a cell's height: 50% of the table");
        CheckNear(c1->rect.height, 150.f, "the other row takes the rest");
    } else Check(false, "two rows");
    b = build("<table style='height:300px' cellpadding='0' cellspacing='0'>"
              "<tr><td style='min-height:80%'>g</td></tr><tr><td>h</td></tr></table>");
    c0 = nth(b, "html_td_", 0);
    if (c0) CheckNear(c0->rect.height, 240.f, "a cell's min-height: 80%");
    b = build("<table style='height:300px' cellpadding='0' cellspacing='0'>"
              "<tr><td style='max-height:10%;height:200px'>i</td></tr><tr><td>j</td></tr></table>");
    c0 = nth(b, "html_td_", 0);
    if (c0) CheckNear(c0->rect.height, 30.f, "a cell's max-height: 10%");
    b = build("<table cellpadding='0' cellspacing='0'><tr><td style='height:200px'>"
              "<div style='height:50%'>a</div></td></tr></table>");
    d = nth(b, "html_div_", 0);
    if (d) CheckNear(d->rect.height, 100.f, "a flex child's height: 50% of its cell");
}

} // namespace

// A mail template's two columns: side by side in a wide pane, and one under
// the other in a narrow one, where its @media rule makes each <td>
// display:block - they then share one anonymous cell and stack in it.
void TestStackedColumns() {
    std::printf("mail columns stack in a narrow pane\n");
    const std::string html =
        "<style>@media (max-width:620px){.row-content{width:100%!important}"
        ".stack .column{width:100%!important;display:block}}</style>"
        "<table class='row-content stack' width='600' style='width:600px' cellpadding='0' cellspacing='0'>"
        "<tr><td class='column' width='50%' style='width:50%;background-color:#e5f5e4'>LEFTCOL</td>"
        "<td class='column' width='50%' style='width:50%'>RIGHTCOL</td></tr></table>";
    for (float width : { 800.f, 580.f }) {
        const bool narrow = width < 620.f;
        HTML::BuildOptions opts;
        opts.style.baseFontSizePx = 12.f;
        opts.viewportWidth = width;
        HTML::ElementBuilder builder;
        auto host = std::make_shared<Host>();
        host->Adopt(CreateRenderContext(Size2Di(static_cast<int>(width), 400), nullptr));
        auto root = builder.Build(html, opts).root;
        Check(root != nullptr, "built");
        if (!root) return;
        root->size.width = CSSLayout::Dimension::Px(width);
        host->AddChild(root);
        CSSLayout::LayoutContext ctx;
        ctx.viewportWidth = width;
        ctx.viewportHeight = 400;
        CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, width },
                                          { CSSLayout::ConstraintMode::Unbounded, INFINITY } };
        root->Measure(mc, ctx);
        root->Arrange(Rect2Df{ 0, 0, width, root->measured.measuredHeight }, ctx);
        std::vector<Placed> all;
        Collect(root.get(), 0, 0, all);
        const Placed* left = LabelWith(all, "LEFTCOL");
        const Placed* right = LabelWith(all, "RIGHTCOL");
        Check(left && right, "both columns built");
        if (!left || !right) return;
        if (narrow) {
            Check(right->rect.y >= left->rect.y + left->rect.height - 0.5f, "580px: right column below the left");
            CheckNear(right->rect.x, left->rect.x, "580px: both start at the same x");
            Check(left->rect.x + left->rect.width <= width + 0.5f, "580px: inside the pane");
        } else {
            CheckNear(right->rect.y, left->rect.y, "800px: side by side");
            Check(right->rect.x >= left->rect.x + 250.f, "800px: right column to the right");
        }
    }
}

// <td align="center"> centres a narrowed block in it, as browsers do: the
// mail template's 280px picture in a stacked, 580px-wide column.
// A 400x400 one-colour PNG.
const std::vector<uint8_t>& Png400() {
    static const std::vector<uint8_t> png = {
        0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
        0x00,0x00,0x01,0x90,0x00,0x00,0x01,0x90,0x08,0x02,0x00,0x00,0x00,0x0F,0xDD,0xA1,
        0x9B,0x00,0x00,0x03,0xEB,0x49,0x44,0x41,0x54,0x78,0xDA,0xED,0xD4,0x31,0x0D,0x00,
        0x00,0x08,0x04,0xB1,0x97,0x83,0x44,0x24,0x22,0x0B,0x15,0x0C,0x24,0x4D,0xAA,0xE0,
        0x86,0x4B,0xF5,0x00,0xBC,0x10,0x09,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,
        0x0B,0xC0,0xB0,0x00,0xC3,0x02,0x30,0x2C,0x00,0xC3,0x02,0x0C,0x0B,0xC0,0xB0,0x00,
        0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0xC0,0xB0,
        0x00,0xC3,0x02,0x30,0x2C,0x00,0xC3,0x02,0x0C,0x0B,0xC0,0xB0,0x00,0x0C,0x0B,0x30,
        0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x15,0x00,0xC3,
        0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0xC0,0xB0,0x00,0xC3,0x02,0x30,0x2C,0x00,
        0xC3,0x02,0x0C,0x0B,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,
        0xC0,0xB0,0x00,0x0C,0x0B,0xC0,0xB0,0x00,0xC3,0x02,0x30,0x2C,0x00,0xC3,0x02,0x0C,
        0x0B,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,
        0x0C,0x0B,0x30,0x2C,0x15,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0xC0,
        0xB0,0x00,0xC3,0x02,0x30,0x2C,0x00,0xC3,0x02,0x0C,0x0B,0xC0,0xB0,0x00,0x0C,0x0B,
        0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0xC0,0xB0,0x00,0xC3,
        0x02,0x30,0x2C,0x00,0xC3,0x02,0x0C,0x0B,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,
        0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,
        0xC0,0xB0,0x00,0x0C,0x0B,0xC0,0xB0,0x00,0xC3,0x02,0x30,0x2C,0x00,0xC3,0x02,0x0C,
        0x0B,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,
        0x0C,0x0B,0xC0,0xB0,0x00,0xC3,0x02,0x30,0x2C,0x00,0xC3,0x02,0x0C,0x0B,0xC0,0xB0,
        0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0x30,
        0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0xC0,0xB0,0x00,0xC3,0x02,
        0x30,0x2C,0x00,0xC3,0x02,0x0C,0x0B,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,
        0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0xC0,0xB0,0x00,0xC3,0x02,0x30,0x2C,0x00,
        0xC3,0x02,0x0C,0x0B,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,
        0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,
        0x0B,0xC0,0xB0,0x00,0xC3,0x02,0x30,0x2C,0x00,0xC3,0x02,0x0C,0x0B,0xC0,0xB0,0x00,
        0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0xC0,0xB0,
        0x00,0xC3,0x02,0x30,0x2C,0x00,0xC3,0x02,0x0C,0x0B,0xC0,0xB0,0x00,0x0C,0x0B,0x30,
        0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,
        0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0xC0,0xB0,0x00,0xC3,0x02,0x30,0x2C,0x00,0xC3,
        0x02,0x0C,0x0B,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,
        0xB0,0x00,0x0C,0x0B,0xC0,0xB0,0x00,0xC3,0x02,0x30,0x2C,0x00,0xC3,0x02,0x0C,0x0B,
        0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,
        0x0B,0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0xC0,0xB0,0x00,
        0xC3,0x02,0x30,0x2C,0x00,0xC3,0x02,0x0C,0x0B,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,
        0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0xC0,0xB0,0x00,0xC3,0x02,0x30,
        0x2C,0x00,0xC3,0x02,0x0C,0x0B,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,
        0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,
        0x00,0x0C,0x0B,0xC0,0xB0,0x00,0xC3,0x02,0x30,0x2C,0x00,0xC3,0x02,0x0C,0x0B,0xC0,
        0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,
        0xC0,0xB0,0x00,0xC3,0x02,0x30,0x2C,0x00,0xC3,0x02,0x0C,0x0B,0xC0,0xB0,0x00,0x0C,
        0x0B,0x30,0x2C,0x00,0xC3,0x02,0x0C,0x4B,0x02,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,
        0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0xC0,0xB0,0x00,0xC3,0x02,0x30,
        0x2C,0x00,0xC3,0x02,0x0C,0x0B,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,
        0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0xC0,0xB0,0x00,0xC3,0x02,0x30,0x2C,0x00,0xC3,
        0x02,0x0C,0x0B,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x0C,0x4B,0x05,
        0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,
        0x0B,0xC0,0xB0,0x00,0xC3,0x02,0x30,0x2C,0x00,0xC3,0x02,0x0C,0x0B,0xC0,0xB0,0x00,
        0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0xC0,0xB0,
        0x00,0xC3,0x02,0x30,0x2C,0x00,0xC3,0x02,0x0C,0x0B,0xC0,0xB0,0x00,0x0C,0x0B,0x30,
        0x2C,0x00,0xC3,0x02,0x0C,0x4B,0x05,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,
        0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0xC0,0xB0,0x00,0xC3,0x02,0x30,0x2C,0x00,
        0xC3,0x02,0x0C,0x0B,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,
        0xC0,0xB0,0x00,0x0C,0x0B,0xC0,0xB0,0x00,0xC3,0x02,0x30,0x2C,0x00,0xC3,0x02,0x0C,
        0x0B,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x0C,0x0B,0xC0,0xB0,0x00,
        0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0xC0,0xB0,
        0x00,0xC3,0x02,0x30,0x2C,0x00,0xC3,0x02,0x0C,0x0B,0xC0,0xB0,0x00,0x0C,0x0B,0x30,
        0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,0x0B,0xC0,0xB0,0x00,0xC3,0x02,
        0x30,0x2C,0x00,0xC3,0x02,0x0C,0x0B,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,
        0x02,0x0C,0x0B,0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,
        0xB0,0x00,0x0C,0x0B,0xC0,0xB0,0x00,0xC3,0x02,0x30,0x2C,0x00,0xC3,0x02,0x0C,0x0B,
        0xC0,0xB0,0x00,0x0C,0x0B,0x30,0x2C,0x00,0xC3,0x02,0x30,0x2C,0xC0,0xB0,0x00,0x0C,
        0x0B,0xC0,0xB0,0x00,0xC3,0x02,0x30,0x2C,0x00,0xC3,0x02,0x0C,0x0B,0xE0,0xD2,0x02,
        0xEC,0xC4,0x1B,0x82,0xD2,0xD3,0x19,0xBE,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,0x44,
        0xAE,0x42,0x60,0x82 };
    return png;
}

void TestAlignCentresNarrowBlock() {
    std::printf("align=center places a max-width block\n");
    const std::vector<uint8_t>& png = Png400();
    HTML::BuildOptions opts;
    opts.style.baseFontSizePx = 12.f;
    opts.resourceLoader = [&png](const std::string&) { return png; };
    // Without align the block stays at the start of the cell - and still
    // 280px: the cell's stretch must not widen it past its max-width.
    for (const bool centred : { true, false }) {
    std::printf("  %s\n", centred ? "<td align=center>" : "<td>");
    HTML::ElementBuilder builder;
    auto host = std::make_shared<Host>();
    host->Adopt(CreateRenderContext(Size2Di(580, 800), nullptr));
    auto root = builder.Build(
        std::string("<table width='100%' cellpadding='0' cellspacing='0'><tr><td") +
        (centred ? " align='center'" : "") + ">"
        "<div id='pic' style='max-width:280px'><img src='p.png' width='280' height='auto' "
        "style='display:block;height:auto;width:100%'></div></td></tr></table><p>BELOW</p>",
        opts).root;
    Check(root != nullptr, "built");
    if (!root) return;
    root->size.width = CSSLayout::Dimension::Px(580.f);
    host->AddChild(root);
    CSSLayout::LayoutContext ctx;
    ctx.viewportWidth = 580;
    ctx.viewportHeight = 800;
    CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, 580.f },
                                      { CSSLayout::ConstraintMode::Unbounded, INFINITY } };
    root->Measure(mc, ctx);
    root->Arrange(Rect2Df{ 0, 0, 580.f, root->measured.measuredHeight }, ctx);
    std::vector<Placed> all;
    Collect(root.get(), 0, 0, all);
    const Placed* image = nullptr;
    for (const auto& p : all)
        if (dynamic_cast<UltraCanvasImageElement*>(p.element)) image = &p;
    const Placed* below = LabelWith(all, "BELOW");
    Check(image && below, "picture and text built");
    if (!image || !below) return;
    CheckNear(image->rect.width, 280.f, "picture keeps the 280px max-width");
    CheckNear(image->rect.height, 280.f, "and is as tall as wide");
    CheckNear(image->rect.x, centred ? 150.f : 0.f,
              centred ? "centred in the 580px cell" : "at the start of the cell");
    Check(below->rect.y >= image->rect.y + image->rect.height - 0.5f, "text below, not under the picture");
    }
}

// A px width is the content box (CSS default): Mailchimp's footer icon cell
// <td style="width:25px; padding:0 10px"> is 45px wide and its width:100%
// picture 25px - not 5px, as when the padding was counted inside the 25.
// box-sizing: border-box keeps the padding inside, as CSS says.
void TestContentBoxWidth() {
    std::printf("a px width is the content box unless box-sizing: border-box\n");
    for (const bool borderBox : { false, true }) {
        HTML::BuildOptions opts;
        opts.style.baseFontSizePx = 12.f;
        opts.resourceLoader = [](const std::string&) { return Png400(); };
        HTML::ElementBuilder builder;
        auto host = std::make_shared<Host>();
        host->Adopt(CreateRenderContext(Size2Di(400, 300), nullptr));
        auto root = builder.Build(
            std::string("<table cellpadding='0' cellspacing='0' align='center'><tr>"
                        "<td style='padding:0 10px;width:25px;height:25px;text-align:center") +
            (borderBox ? ";box-sizing:border-box" : "") +
            "'><a href='mailto:x@y'><img src='i.png' style='width:100%;height:auto;display:block'>"
            "</a></td></tr></table>", opts).root;
        Check(root != nullptr, "built");
        if (!root) return;
        root->size.width = CSSLayout::Dimension::Px(400.f);
        host->AddChild(root);
        CSSLayout::LayoutContext ctx;
        ctx.viewportWidth = 400;
        ctx.viewportHeight = 300;
        CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, 400.f },
                                          { CSSLayout::ConstraintMode::Unbounded, INFINITY } };
        root->Measure(mc, ctx);
        root->Arrange(Rect2Df{ 0, 0, 400.f, root->measured.measuredHeight }, ctx);
        std::vector<Placed> all;
        Collect(root.get(), 0, 0, all);
        const Placed* image = nullptr;
        for (const auto& p : all)
            if (dynamic_cast<UltraCanvasImageElement*>(p.element)) image = &p;
        Check(image != nullptr, "picture built");
        if (!image) return;
        CheckNear(image->rect.width, borderBox ? 5.f : 25.f,
                  borderBox ? "border-box: 25px minus the padding" : "content-box: the picture is 25px");
    }
}

// Floats: Mailchimp's two-column block is two <table align="left"
// width="300"> in a 600px cell - side by side, as in a browser, not one under
// the other. float:right goes to the end of the row; what follows the
// floats runs between them.
void TestFloats() {
    std::printf("floats sit side by side\n");
    HTML::BuildOptions opts;
    opts.style.baseFontSizePx = 12.f;
    HTML::ElementBuilder builder;
    auto host = std::make_shared<Host>();
    host->Adopt(CreateRenderContext(Size2Di(640, 400), nullptr));
    auto root = builder.Build(
        "<table width='600' cellpadding='0' cellspacing='0'><tr><td>"
        "<table align='left' width='300' cellpadding='0' cellspacing='0'><tr><td>COLONE</td></tr></table>\n"
        "<table align='left' width='300' cellpadding='0' cellspacing='0'><tr><td>COLTWO</td></tr></table>"
        "</td></tr></table>"
        "<div style='float:left;width:100px'>FLEFT</div> <div style='float:right;width:100px'>FRIGHT</div>"
        "<p style='margin:0'>AFTERFLOATS</p>", opts).root;
    Check(root != nullptr, "built");
    if (!root) return;
    root->size.width = CSSLayout::Dimension::Px(640.f);
    host->AddChild(root);
    CSSLayout::LayoutContext ctx;
    ctx.viewportWidth = 640;
    ctx.viewportHeight = 400;
    CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, 640.f },
                                      { CSSLayout::ConstraintMode::Unbounded, INFINITY } };
    root->Measure(mc, ctx);
    root->Arrange(Rect2Df{ 0, 0, 640.f, root->measured.measuredHeight }, ctx);
    std::vector<Placed> all;
    Collect(root.get(), 0, 0, all);
    const Placed* one = LabelWith(all, "COLONE");
    const Placed* two = LabelWith(all, "COLTWO");
    const Placed* left = LabelWith(all, "FLEFT");
    const Placed* right = LabelWith(all, "FRIGHT");
    const Placed* after = LabelWith(all, "AFTERFLOATS");
    Check(one && two && left && right && after, "all built");
    if (!one || !two || !left || !right || !after) return;
    CheckNear(two->rect.y, one->rect.y, "the two columns on one row");
    CheckNear(two->rect.x - one->rect.x, 300.f, "the second column 300px right of the first");
    CheckNear(left->rect.x, 0.f, "float:left at the start");
    CheckNear(right->rect.x + right->rect.width, 640.f, "float:right at the end");
    CheckNear(right->rect.y, left->rect.y, "both floats on one row");
    CheckNear(after->rect.y, left->rect.y, "what follows sits beside the floats");
    Check(after->rect.x >= left->rect.x + left->rect.width - 0.5f, "... right of float:left");
    Check(after->rect.x + after->rect.width <= right->rect.x + 0.5f, "... left of float:right");
}

// Text after a float runs beside it; clear starts below it; a block that does
// not fit in the room beside a float (a 300px table beside a 500px float)
// goes below it.
void TestFlowAroundFloats() {
    std::printf("content flows around floats\n");
    Laid laid = LayOut(
        "<div style='float:left;width:100px;height:60px'>BOX</div>"
        "<p style='margin:0'>BESIDE</p>"
        "<p style='clear:both;margin:0'>CLEARED</p>"
        "<div style='float:right;width:500px;height:40px'>WIDE</div>"
        "<table width='300' cellpadding='0' cellspacing='0'><tr><td>NOROOM</td></tr></table>", 640.f);
    Check(laid.root != nullptr, "built");
    if (!laid.root) return;
    std::vector<Placed> all;
    Collect(laid.root.get(), 0, 0, all);
    const Placed* box = LabelWith(all, "BOX");
    const Placed* beside = LabelWith(all, "BESIDE");
    const Placed* cleared = LabelWith(all, "CLEARED");
    const Placed* wide = LabelWith(all, "WIDE");
    const Placed* noroom = LabelWith(all, "NOROOM");
    Check(box && beside && cleared && wide && noroom, "all built");
    if (!box || !beside || !cleared || !wide || !noroom) return;
    CheckNear(beside->rect.x, 100.f, "the paragraph starts right of the float");
    CheckNear(beside->rect.y, box->rect.y, "... on the float's first line");
    CheckNear(beside->rect.width, 540.f, "... in the room left beside it");
    Check(cleared->rect.y >= box->rect.y + 60.f - 0.5f, "clear:both starts below the float");
    CheckNear(cleared->rect.x, 0.f, "... at the left edge");
    CheckNear(wide->rect.x, 140.f, "float:right at the right edge");
    Check(noroom->rect.y >= wide->rect.y + 40.f - 0.5f, "a table with no room beside goes below");
}

// A table without a width is as wide as its content: the mail's 30px logo
// cell, and its button - left in its cell, not centred across the line.
void TestShrinkToFitTable() {
    std::printf("a table without a width is shrink-to-fit\n");
    // In a table cell, as mail nests them: the cell stretched them to its width.
    Laid laid = LayOut(
        "<table width='600' cellpadding='0' cellspacing='0'><tr><td>"
        "<table cellpadding='0' cellspacing='0'><tr><td style='width:30px'>L</td></tr></table>"
        "<table cellpadding='0' cellspacing='0'><tr>"
        "<td align='center' style='padding:10px 25px'><a href='#'>BUTTON</a></td></tr></table>"
        "</td></tr></table>", 640.f);
    Check(laid.root != nullptr, "built");
    if (!laid.root) return;
    std::vector<Placed> all;
    Collect(laid.root.get(), 0, 0, all);
    std::vector<const Placed*> tables;
    for (const auto& p : all)
        if (p.element->GetIdentifier().rfind("html_table_", 0) == 0) tables.push_back(&p);
    const Placed* logo = tables.size() > 1 ? tables[1] : nullptr;   // the first one inside
    const Placed* button = LabelWith(all, "BUTTON");
    Check(logo && button, "both built");
    if (!logo || !button) return;
    CheckNear(logo->rect.width, 30.f, "the logo table is its cell's 30px");
    CheckNear(logo->rect.x, 0.f, "... at the left");
    Check(button->rect.x < 40.f, "the button sits at the left, not centred");
}

// <li><div>text</div></li>: the marker starts the div's line, as in a
// browser - not a line of its own above it.
void TestMarkerInBlock() {
    std::printf("a list marker starts the item's first block\n");
    Laid laid = LayOut("<ul><li> <div>ITEMTEXT</div> </li></ul>", 640.f);
    Check(laid.root != nullptr, "built");
    if (!laid.root) return;
    std::vector<Placed> all;
    Collect(laid.root.get(), 0, 0, all);
    const Placed* item = LabelWith(all, "ITEMTEXT");
    Check(item != nullptr, "built");
    if (!item) return;
    auto* label = dynamic_cast<UltraCanvasLabel*>(item->element);
    Check(label && label->GetText().find("\u2022") != std::string::npos, "the marker is on the text's line");
    int markersAlone = 0;
    for (const auto& p : all)
        if (auto* l = dynamic_cast<UltraCanvasLabel*>(p.element))
            if (l->GetText().find("\u2022") != std::string::npos &&
                l->GetText().find("ITEMTEXT") == std::string::npos) ++markersAlone;
    Check(markersAlone == 0, "no marker on a line of its own");
}

// Side-by-side inline-block columns with vertical-align: top start level, as
// the mail footer's two columns do.
void TestInlineBoxTop() {
    std::printf("vertical-align: top on side-by-side boxes\n");
    Laid laid = LayOut(
        "<div>"
        "<div style='display:inline-block;vertical-align:top;width:200px'><p>SHORTCOL</p></div>"
        "<div style='display:inline-block;vertical-align:top;width:200px'>"
        "<p>TALLCOL</p><p>more</p><p>more</p></div>"
        "</div>", 640.f);
    Check(laid.root != nullptr, "built");
    if (!laid.root) return;
    std::vector<Placed> all;
    Collect(laid.root.get(), 0, 0, all);
    const Placed* shortCol = LabelWith(all, "SHORTCOL");
    const Placed* tallCol = LabelWith(all, "TALLCOL");
    Check(shortCol && tallCol, "both built");
    if (!shortCol || !tallCol) return;
    CheckNear(shortCol->rect.y, tallCol->rect.y, "both columns start at the top");
}

int main() {
    UCImage::InitializeImageSubsysterm("HTMLTableLayoutTest");
    TestSharedColumns();
    TestWidths();
    TestButtonAndAlignment();
    TestFontSize();
    TestBackgroundAndAutoMargins();
    TestBackgroundPosition();
    TestBackgroundRepeat();
    TestObjectFitPosition();
    TestImageBox();
    TestImagesShareLine();
    TestBorderSides();
    TestBoxSizing();
    TestImageLimits();
    TestMailTables();
    TestStackedColumns();
    TestAlignCentresNarrowBlock();
    TestContentBoxWidth();
    TestFloats();
    TestFlowAroundFloats();
    TestShrinkToFitTable();
    TestMarkerInBlock();
    TestInlineBoxTop();
    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

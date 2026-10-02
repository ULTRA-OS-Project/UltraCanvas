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
// Version: 1.2.0 - display:block cells stack (mail columns on a narrow screen);
//                  align=center places a max-width block
// Version: 1.1.0 - background pictures, margin: auto, @media width
// Last Modified: 2026-10-02
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLElementBuilder.h"
#include "CSSLayout/CSSLayout.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRenderContext.h"

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
void TestAlignCentresNarrowBlock() {
    std::printf("align=center places a max-width block\n");
    static const std::vector<uint8_t> png = {   // 400x400, one colour
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
    HTML::BuildOptions opts;
    opts.style.baseFontSizePx = 12.f;
    opts.resourceLoader = [](const std::string&) { return png; };
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

int main() {
    UCImage::InitializeImageSubsysterm("HTMLTableLayoutTest");
    TestSharedColumns();
    TestWidths();
    TestButtonAndAlignment();
    TestFontSize();
    TestBackgroundAndAutoMargins();
    TestStackedColumns();
    TestAlignCentresNarrowBlock();
    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

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
// Version: 1.0.0
// Last Modified: 2026-09-30
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLElementBuilder.h"
#include "CSSLayout/CSSLayout.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasImage.h"
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

} // namespace

int main() {
    UCImage::InitializeImageSubsysterm("HTMLTableLayoutTest");
    TestSharedColumns();
    TestWidths();
    TestButtonAndAlignment();
    TestFontSize();
    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

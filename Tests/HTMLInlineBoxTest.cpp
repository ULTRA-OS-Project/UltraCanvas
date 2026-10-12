// Tests/HTMLInlineBoxTest.cpp
// A plain inline element's box in the HTML reader: its horizontal margin,
// border and padding are room before its first letter and after its last,
// kept with its text when a line breaks, white space collapsing across it,
// the margin outside its link; its vertical margins move nothing. Its
// background is painted behind its text and padding; with a border, or a
// background with vertical padding (the mail "button"), it is a box on the
// line.
//
// Headless: builds the element tree with HTMLElementBuilder and lays it out
// with the CSSLayout engine; text is measured on, and pages are painted
// into, an offscreen render context.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLElementBuilder.h"
#include "CSSLayout/CSSLayout.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRenderContext.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool cond, const std::string& what) {
    std::printf("  %s %s\n", cond ? "ok:  " : "FAIL:", what.c_str());
    if (!cond) ++g_failures;
}

void CheckNear(float actual, float expected, const std::string& what, float tol = 1.5f) {
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
    std::unordered_map<std::string, Rect2Df> byId;
    std::vector<std::pair<UltraCanvasUIElement*, Rect2Df>> all;

    Rect2Df Of(const std::string& id) const {
        auto it = byId.find(id);
        return it == byId.end() ? Rect2Df(-1000, -1000, 0, 0) : it->second;
    }
    std::vector<UltraCanvasLabel*> Labels() const {
        std::vector<UltraCanvasLabel*> labels;
        for (const auto& [e, r] : all)
            if (auto* l = dynamic_cast<UltraCanvasLabel*>(e)) labels.push_back(l);
        return labels;
    }
};

void Collect(UltraCanvasUIElement* e, float ox, float oy,
             std::vector<std::pair<UltraCanvasUIElement*, Rect2Df>>& out) {
    const Rect2Df b = e->GetBounds();
    out.push_back({ e, Rect2Df(ox + b.x, oy + b.y, b.width, b.height) });
    if (auto* c = dynamic_cast<UltraCanvasContainer*>(e))
        for (auto& child : c->GetChildren()) Collect(child.get(), ox + b.x, oy + b.y, out);
}

Laid LayOut(const std::string& html, float width) {
    Laid out;
    out.host = std::make_shared<Host>();
    out.host->Adopt(CreateRenderContext(Size2Di(static_cast<int>(width), 800), nullptr));
    HTML::BuildOptions opts;
    opts.style.baseFontSizePx = 16.f;
    opts.viewportWidth = width;
    opts.onLinkActivated = [](const std::string&) {};
    HTML::ElementBuilder builder;
    HTML::BuildResult built = builder.Build(
        "<!DOCTYPE html><html><body style='margin:0'>" + html + "</body></html>", opts);
    out.root = built.root;
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

    Collect(out.root.get(), 0, 0, out.all);
    for (const auto& [id, element] : built.anchors)
        for (const auto& [e, r] : out.all)
            if (e == element.get()) out.byId[id] = r;
    return out;
}

// The laid-out page painted on white, as 0xRRGGBB per pixel.
struct Painted {
    int width = 0, height = 0;
    std::vector<uint32_t> pixels;
    uint32_t At(int x, int y) const { return pixels[static_cast<size_t>(y) * width + x] & 0xFFFFFF; }
    // The box around the pixels near `rgb` (each channel within 40); empty when none.
    Rect2Df Around(uint32_t rgb) const {
        int x0 = width, y0 = height, x1 = -1, y1 = -1;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const uint32_t p = At(x, y);
                bool near = true;
                for (int shift = 0; shift <= 16; shift += 8) {
                    const int a = static_cast<int>((p >> shift) & 0xFF), b = static_cast<int>((rgb >> shift) & 0xFF);
                    if (std::abs(a - b) > 40) near = false;
                }
                if (!near) continue;
                x0 = std::min(x0, x); y0 = std::min(y0, y);
                x1 = std::max(x1, x); y1 = std::max(y1, y);
            }
        if (x1 < 0) return Rect2Df(0, 0, 0, 0);
        return Rect2Df(static_cast<float>(x0), static_cast<float>(y0),
                       static_cast<float>(x1 - x0 + 1), static_cast<float>(y1 - y0 + 1));
    }
    // How many pixels inside `r` are light (all channels above 200).
    int LightIn(const Rect2Df& r) const {
        int n = 0;
        for (int y = static_cast<int>(r.y); y < static_cast<int>(r.y + r.height); ++y)
            for (int x = static_cast<int>(r.x); x < static_cast<int>(r.x + r.width); ++x) {
                const uint32_t p = At(x, y);
                if (((p >> 16) & 0xFF) > 200 && ((p >> 8) & 0xFF) > 200 && (p & 0xFF) > 200) ++n;
            }
        return n;
    }
};

Painted Paint(const Laid& laid, int width, int height) {
    Painted out;
    out.width = width;
    out.height = height;
    if (!laid.root) return out;
    auto ctx = CreateRenderContext(Size2Di(width, height), nullptr);
    ctx->Clear(Color(255, 255, 255, 255));
    laid.root->Render(ctx.get(), Rect2Df(0, 0, static_cast<float>(width), static_cast<float>(height)));
    UCPixmap pixmap;
    pixmap.Init(width, height);
    ctx->FlushToSurface(pixmap.GetSurface(), Point2Dd(0, 0));
    pixmap.MarkDirty();
    pixmap.Flush();
    out.pixels.resize(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) out.pixels[static_cast<size_t>(y) * width + x] = pixmap.GetPixel(x, y);
    return out;
}

// The width of `inner` laid out as one line: a flex item is as wide as its
// content, so the item's width is the text's.
float LineWidth(const std::string& inner) {
    Laid laid = LayOut("<div style='display:flex'><div id='t'>" + inner + "</div></div>", 800.f);
    return laid.Of("t").width;
}

float LineHeight(const std::string& inner) {
    Laid laid = LayOut("<div id='t'>" + inner + "</div>", 800.f);
    return laid.Of("t").height;
}

void TestHorizontalRoom() {
    std::printf("horizontal margin, border and padding are room in the line\n");
    const float plain = LineWidth("<a href='/a'>About</a><a href='/i'>Index</a>");
    Check(plain > 20.f, "the plain line has a width");

    CheckNear(LineWidth("<a href='/a' style='margin-right:20px'>About</a><a href='/i'>Index</a>"),
              plain + 20.f, "margin-right: 20px");
    CheckNear(LineWidth("<a href='/a'>About</a><a href='/i' style='margin-left:1em'>Index</a>"),
              plain + 16.f, "margin-left: 1em at 16px");
    CheckNear(LineWidth("<a href='/a' style='margin:0 8px'>About</a><a href='/i'>Index</a>"),
              plain + 16.f, "margin: 0 8px on both sides");
    CheckNear(LineWidth("<span style='padding:0 5px;border:2px solid red'>About</span>Index"),
              LineWidth("<span>About</span>Index") + 14.f, "padding 5px and a 2px border on each side");
    CheckNear(LineWidth("<span style='margin-left:-30px'>About</span>Index"),
              LineWidth("<span>About</span>Index"), "a negative margin takes no room");
    CheckNear(LineWidth("<span style='margin-left:10px'><b style='margin-left:6px'>About</b></span>Index"),
              LineWidth("<span><b>About</b></span>Index") + 16.f, "nested elements' margins add up");
}

void TestVerticalMarginsIgnored() {
    std::printf("vertical margins do not move an inline element's line\n");
    CheckNear(LineHeight("Text <span style='margin:40px 0'>more</span> text"),
              LineHeight("Text <span>more</span> text"), "margin: 40px 0 leaves the line's height");
}

void TestWhiteSpaceCollapses() {
    std::printf("white space collapses across the room\n");
    const float one = LineWidth("About <span style='margin-left:10px'>Index</span>");
    CheckNear(LineWidth("About <span style='margin-left:10px'> Index</span>"), one,
              "a space after the room joins the space before it");
    CheckNear(one, LineWidth("About <span>Index</span>") + 10.f, "one space plus the margin");
}

void TestLinksAndRuns() {
    std::printf("the link's range leaves the margin out; empty elements build nothing\n");
    Laid laid = LayOut("<p><a href='/a' style='margin-right:20px'>About</a>"
                       "<a href='/i' style='margin-left:20px'>Index</a></p>", 800.f);
    UltraCanvasLabel* label = nullptr;
    for (auto* l : laid.Labels())
        if (l->GetText().find("About") != std::string::npos) label = l;
    Check(label != nullptr, "the line is one label");
    if (label) {
        const auto& links = label->GetTextLinks();
        Check(links.size() == 2, "two links");
        if (links.size() == 2) {
            Check(links[0].startByte == 0 && links[0].endByte == 5, "the first covers \"About\" only");
            Check(links[1].endByte - links[1].startByte == 5, "the second covers \"Index\" only");
            Check(links[1].startByte > links[0].endByte, "the margins lie between them");
        }
        int rooms = 0;
        for (const auto& image : label->GetInlineImages())
            if (!image.image) ++rooms;
        Check(rooms == 2, "two pieces of room, no picture in them");
    }

    Laid empty = LayOut("<div id='d'><span style='margin:0 10px'></span></div>", 800.f);
    Check(empty.Labels().empty(), "an empty element with a margin builds no line");
}

void TestRoomStaysWithItsWord() {
    std::printf("a line does not break between a word and its margin\n");
    // A box 10px wider than "About": the 30px margin glued to "About" ends
    // the first line (overflowing it, as CSS does), and "Index" is the
    // second. Were the margin free to break away it would open a line of its
    // own and "Index" would follow on a third.
    const float about = LineWidth("About");
    Laid laid = LayOut("<div id='box' style='width:" + std::to_string(static_cast<int>(about + 10.f)) +
                       "px'><span style='margin-right:30px'>About</span> Index</div>", 800.f);
    const float lineHeight = LineHeight("About");
    CheckNear(laid.Of("box").height, 2.f * lineHeight, "two lines", 3.f);
}


void TestBackgrounds() {
    std::printf("an inline element's background is painted behind its text and padding\n");
    Laid plain = LayOut("<p>Go <span style='background:#FF0000'>marked</span> on</p>", 400.f);
    const Rect2Df red = Paint(plain, 400, 120).Around(0xFF0000);
    Check(red.width > 20.f, "a background behind the text");
    Check(red.height > 10.f && red.height < 30.f, "as tall as the text");

    Laid padded = LayOut("<p>Go <span style='background:#FF0000;padding:0 20px'>marked</span> on</p>", 400.f);
    const Rect2Df redPadded = Paint(padded, 400, 120).Around(0xFF0000);
    CheckNear(redPadded.width, red.width + 40.f, "and behind its padding", 3.f);

    Laid margin = LayOut("<p>Go <span style='background:#FF0000;margin:0 20px'>marked</span> on</p>", 400.f);
    CheckNear(Paint(margin, 400, 120).Around(0xFF0000).width, red.width,
              "but not behind its margin", 3.f);

    Laid none = LayOut("<p>Go <span>plain</span> on</p>", 400.f);
    Check(Paint(none, 400, 120).Around(0xFF0000).width == 0.f, "no background, nothing painted");
}

void TestMailButton() {
    std::printf("a background with vertical padding - the mail button - is a box on the line\n");
    Laid laid = LayOut("<p>Order now: <a href='/buy' style='background:#0077FF;padding:12px 24px;"
                       "color:#FFFFFF;text-decoration:none'>Buy</a> today</p>", 400.f);
    const Painted painted = Paint(laid, 400, 160);
    const Rect2Df blue = painted.Around(0x0077FF);
    const float textHeight = LineHeight("Buy");
    Check(blue.width > 0.f, "the button is painted");
    CheckNear(blue.height, textHeight + 24.f, "its padding above and below the text", 3.f);
    Check(blue.width >= LineWidth("Buy") + 48.f - 3.f, "and to either side");
    Check(painted.LightIn(blue) > 10, "white text on the blue, not white on white");

    UltraCanvasLabel* buy = nullptr;
    for (auto* l : laid.Labels())
        if (l->GetText().find("Buy") != std::string::npos) buy = l;
    Check(buy && !buy->GetTextLinks().empty(), "the button's text is the link");

    // The spaces either side of the button stay: "Order now: [Buy] today".
    Laid spaced = LayOut("<p>Order now: <a id='b' href='/buy' style='background:#0077FF;padding:12px 24px'>Buy</a>"
                         " today</p><p>Order now:<a id='n' href='/buy' style='background:#0077FF;padding:12px 24px'>"
                         "Buy</a>today</p>", 400.f);
    Rect2Df before, after, beforeTight, afterTight;
    for (const auto& [e, r] : spaced.all)
        if (auto* l = dynamic_cast<UltraCanvasLabel*>(e)) {
            const bool second = r.y > spaced.Of("b").y + spaced.Of("b").height - 1.f;
            if (l->GetText().find("Order") != std::string::npos) (second ? beforeTight : before) = r;
            if (l->GetText().find("today") != std::string::npos) (second ? afterTight : after) = r;
        }
    const float gapBefore = spaced.Of("b").x - (before.x + before.width);
    const float gapAfter = after.x - (spaced.Of("b").x + spaced.Of("b").width);
    Check(gapBefore >= 3.f && gapBefore <= 8.f, "a space before the button (" + std::to_string(gapBefore) + "px)");
    Check(gapAfter >= 3.f && gapAfter <= 8.f, "and after it (" + std::to_string(gapAfter) + "px)");
    CheckNear(spaced.Of("n").x - (beforeTight.x + beforeTight.width), 0.f, "no space written, no gap");
    CheckNear(afterTight.x - (spaced.Of("n").x + spaced.Of("n").width), 0.f, "on either side");

    Laid inBlock = LayOut("<p>Text <a href='/x' style='background:#0077FF;padding:12px 24px'>"
                          "<div>block</div></a></p>", 400.f);
    Check(inBlock.root != nullptr, "an inline around a block keeps the old path");
}

void TestBorders() {
    std::printf("an inline element with a border is a box on the line\n");
    Laid laid = LayOut("<p>Tags: <span id='t' style='border:2px solid #00CC00;padding:0 6px'>html</span>"
                       " and more</p>", 400.f);
    const Rect2Df green = Paint(laid, 400, 120).Around(0x00CC00);
    Check(green.width > 20.f && green.height > 10.f, "its border is drawn");
    CheckNear(laid.Of("t").width, LineWidth("html") + 16.f, "the box is the text plus padding and border", 2.f);
    Check(laid.Of("t").y < 30.f, "on the first line, with the text");

    // Deeper in a run (inside <b>) the border takes room but is not drawn.
    Laid nested = LayOut("<p><b>Tags: <span style='border:2px solid #00CC00'>html</span></b></p>", 400.f);
    Check(nested.root != nullptr, "a nested bordered element builds");
}
} // namespace

int main() {
    TestHorizontalRoom();
    TestVerticalMarginsIgnored();
    TestWhiteSpaceCollapses();
    TestLinksAndRuns();
    TestRoomStaysWithItsWord();
    TestBackgrounds();
    TestMailButton();
    TestBorders();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}

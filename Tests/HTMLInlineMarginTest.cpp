// Tests/HTMLInlineMarginTest.cpp
// An inline element's horizontal margin, border and padding in the HTML
// reader: room before its first letter and after its last, kept with its
// text when a line breaks, white space collapsing across it, outside its
// link, and no room from its vertical margins.
//
// Headless: builds the element tree with HTMLElementBuilder and lays it out
// with the CSSLayout engine; text is measured on an offscreen render context.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLElementBuilder.h"
#include "CSSLayout/CSSLayout.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRenderContext.h"

#include <cmath>
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

} // namespace

int main() {
    TestHorizontalRoom();
    TestVerticalMarginsIgnored();
    TestWhiteSpaceCollapses();
    TestLinksAndRuns();
    TestRoomStaysWithItsWord();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}

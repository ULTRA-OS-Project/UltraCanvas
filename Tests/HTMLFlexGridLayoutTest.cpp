// Tests/HTMLFlexGridLayoutTest.cpp
// display: flex and display: grid in the HTML reader, laid out by the
// CSSLayout flex and grid engines: items at their own size, flex-grow /
// shrink / basis, gap, justify-content, align-items, wrap, order, anonymous
// text items; grid tracks in px / fr / %, repeat(), repeat(auto-fill,
// minmax()), named areas, line numbers counted from the end, spans - and the
// values a hostile page could use to make the layout hang or build a grid of
// a billion cells.
//
// Headless: builds the element tree with HTMLElementBuilder and lays it out
// with the CSSLayout engine; text is measured on an offscreen render context.
// Version: 1.0.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLElementBuilder.h"
#include "CSSLayout/CSSLayout.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRenderContext.h"

#include <chrono>
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

// The laid-out page, and the window-relative rect of every element with an
// id (BuildResult::anchors).
struct Laid {
    std::shared_ptr<Host> host;
    std::shared_ptr<UltraCanvasContainer> root;
    std::unordered_map<std::string, Rect2Df> byId;
    std::vector<std::pair<UltraCanvasUIElement*, Rect2Df>> all;

    bool Has(const std::string& id) const { return byId.count(id) != 0; }
    Rect2Df Of(const std::string& id) const {
        auto it = byId.find(id);
        return it == byId.end() ? Rect2Df(-1000, -1000, 0, 0) : it->second;
    }
    // The label whose text holds `text`.
    const Rect2Df* Label(const std::string& text) const {
        for (const auto& [e, r] : all)
            if (auto* l = dynamic_cast<UltraCanvasLabel*>(e))
                if (l->GetText().find(text) != std::string::npos) return &r;
        return nullptr;
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
    opts.style.baseFontSizePx = 12.f;
    opts.viewportWidth = width;
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

void TestFlexRow() {
    std::printf("flex row: items at their own width, gap, justify-content\n");
    Laid laid = LayOut(
        "<div id='c' style='display:flex;justify-content:space-between'>"
        "<div id='a' style='width:100px;height:20px'></div>"
        "<div id='b' style='width:100px;height:40px'></div>"
        "<div id='d' style='width:100px;height:30px'></div></div>", 600.f);
    CheckNear(laid.Of("a").x, 0.f, "first item at the start");
    CheckNear(laid.Of("b").x, 250.f, "second in the middle");
    CheckNear(laid.Of("d").x, 500.f, "last at the end");
    CheckNear(laid.Of("a").width, 100.f, "an item keeps its width (no 100%)");
    CheckNear(laid.Of("a").y, laid.Of("b").y, "items share one line");
    // align-items' initial value stretches an item without a height of its
    // own; these have one.
    CheckNear(laid.Of("a").height, 20.f, "a set height is kept");
    CheckNear(laid.Of("c").height, 40.f, "the line is as tall as its tallest item");

    Laid gap = LayOut(
        "<div style='display:flex;gap:10px'>"
        "<div id='a' style='width:100px;height:10px'></div>"
        "<div id='b' style='width:100px;height:10px'></div></div>", 600.f);
    CheckNear(gap.Of("b").x, 110.f, "gap: 10px between items");

    Laid centre = LayOut(
        "<div style='display:flex;justify-content:center'>"
        "<div id='a' style='width:100px;height:10px'></div></div>", 600.f);
    CheckNear(centre.Of("a").x, 250.f, "justify-content: center");
}

void TestFlexGrow() {
    std::printf("flex: grow, shrink, basis\n");
    Laid laid = LayOut(
        "<div style='display:flex'>"
        "<div id='a' style='flex:1;height:10px'></div>"
        "<div id='b' style='flex:2;height:10px'></div></div>", 600.f);
    CheckNear(laid.Of("a").width, 200.f, "flex: 1 takes one share");
    CheckNear(laid.Of("b").width, 400.f, "flex: 2 takes two");

    Laid fixed = LayOut(
        "<div style='display:flex'>"
        "<div id='side' style='flex:0 0 150px;height:10px'></div>"
        "<div id='main' style='flex-grow:1;height:10px'></div></div>", 600.f);
    CheckNear(fixed.Of("side").width, 150.f, "flex: 0 0 150px stays 150");
    CheckNear(fixed.Of("main").width, 450.f, "flex-grow: 1 takes the rest");

    Laid basis = LayOut(
        "<div style='display:flex'>"
        "<div id='a' style='flex-basis:25%;height:10px'></div></div>", 400.f);
    CheckNear(basis.Of("a").width, 100.f, "flex-basis: 25% of the container");

    Laid shrink = LayOut(
        "<div style='display:flex'>"
        "<div id='a' style='width:400px;height:10px'></div>"
        "<div id='b' style='width:400px;height:10px;flex-shrink:0'></div></div>", 600.f);
    CheckNear(shrink.Of("b").width, 400.f, "flex-shrink: 0 keeps its width");
    CheckNear(shrink.Of("a").width, 200.f, "the other item shrinks");
}

void TestFlexColumnAndAlign() {
    std::printf("flex column, align-items, align-self\n");
    Laid laid = LayOut(
        "<div style='display:flex;flex-direction:column;gap:5px'>"
        "<div id='a' style='height:20px'></div>"
        "<div id='b' style='height:20px'></div></div>", 300.f);
    CheckNear(laid.Of("a").width, 300.f, "a column stretches its items to its width");
    CheckNear(laid.Of("b").y - laid.Of("a").y, 25.f, "the gap goes between rows");

    Laid centre = LayOut(
        "<div style='display:flex;align-items:center;height:100px'>"
        "<div id='a' style='width:50px;height:20px'></div>"
        "<div id='b' style='width:50px;height:20px;align-self:flex-end'></div></div>", 300.f);
    CheckNear(centre.Of("a").y, 40.f, "align-items: center");
    CheckNear(centre.Of("b").y, 80.f, "align-self: flex-end");

    Laid stretch = LayOut(
        "<div style='display:flex;height:80px'>"
        "<div id='a' style='width:50px'></div></div>", 300.f);
    CheckNear(stretch.Of("a").height, 80.f, "an item without a height stretches (normal)");
}

void TestFlexWrapAndOrder() {
    std::printf("flex-wrap and order\n");
    Laid laid = LayOut(
        "<div style='display:flex;flex-wrap:wrap'>"
        "<div id='a' style='width:200px;height:10px'></div>"
        "<div id='b' style='width:200px;height:10px'></div>"
        "<div id='c' style='width:200px;height:10px'></div></div>", 500.f);
    CheckNear(laid.Of("b").y, laid.Of("a").y, "two fit on the first line");
    CheckNear(laid.Of("c").y, laid.Of("a").y + 10.f, "the third wraps");
    CheckNear(laid.Of("c").x, 0.f, "to the start of the next line");

    Laid order = LayOut(
        "<div style='display:flex'>"
        "<div id='a' style='width:100px;height:10px'></div>"
        "<div id='b' style='width:100px;height:10px;order:-1'></div></div>", 500.f);
    CheckNear(order.Of("b").x, 0.f, "order: -1 goes first");
    CheckNear(order.Of("a").x, 100.f, "the other after it");

    Laid reverse = LayOut(
        "<div style='display:flex;flex-direction:row-reverse'>"
        "<div id='a' style='width:100px;height:10px'></div></div>", 500.f);
    CheckNear(reverse.Of("a").x, 400.f, "row-reverse starts at the right");
}

void TestFlexItemsAreBoxes() {
    std::printf("items: inline elements are boxes, text between them is an item\n");
    Laid laid = LayOut(
        "<nav id='n' style='display:flex;gap:20px'>"
        "<a id='home' href='/'>Home</a> <a id='about' href='/about'>About</a>"
        " loose text </nav>", 600.f);
    Check(laid.Has("home") && laid.Has("about"), "each link is an item");
    Check(laid.Of("about").x >= laid.Of("home").x + laid.Of("home").width + 19.f,
          "side by side, the gap apart");
    Check(laid.Of("home").width < 100.f, "an item is as wide as its text");
    const Rect2Df* loose = laid.Label("loose text");
    Check(loose != nullptr, "the text after them is an anonymous item");
    if (loose) Check(loose->x > laid.Of("about").x, "after the links on the line");
    CheckNear(laid.Of("home").y, laid.Of("about").y, "on one line");

    Laid inlineFlex = LayOut(
        "<p>before <span id='chip' style='display:inline-flex;gap:4px;padding:2px'>"
        "<b id='x'>x</b><b id='y'>y</b></span> after</p>", 600.f);
    Check(inlineFlex.Has("x") && inlineFlex.Has("y"), "inline-flex builds its items");
    Check(inlineFlex.Of("y").x > inlineFlex.Of("x").x, "side by side");
    Check(inlineFlex.Of("chip").width < 200.f, "and is as wide as they are");
}

void TestGridTracks() {
    std::printf("grid: px, fr and %% tracks, gaps, auto rows\n");
    Laid laid = LayOut(
        "<div style='display:grid;grid-template-columns:100px 1fr 2fr;column-gap:10px;row-gap:5px'>"
        "<div id='a' style='height:20px'></div><div id='b' style='height:20px'></div>"
        "<div id='c' style='height:20px'></div><div id='d' style='height:20px'></div></div>", 420.f);
    CheckNear(laid.Of("a").width, 100.f, "100px column");
    CheckNear(laid.Of("b").width, 100.f, "1fr of the 300px left");
    CheckNear(laid.Of("c").width, 200.f, "2fr");
    CheckNear(laid.Of("b").x, 110.f, "column-gap");
    CheckNear(laid.Of("d").x, 0.f, "the fourth item starts a new row");
    CheckNear(laid.Of("d").y, 25.f, "row-gap");

    Laid repeat = LayOut(
        "<div style='display:grid;grid-template-columns:repeat(3, 1fr)'>"
        "<div id='a'>1</div><div>2</div><div>3</div><div id='d'>4</div></div>", 300.f);
    CheckNear(repeat.Of("a").width, 100.f, "repeat(3, 1fr)");
    CheckNear(repeat.Of("d").x, 0.f, "wraps after three");

    Laid pct = LayOut(
        "<div style='display:grid;grid-template-columns:25% 75%'>"
        "<div id='a'>1</div><div id='b'>2</div></div>", 400.f);
    CheckNear(pct.Of("a").width, 100.f, "25%");
    CheckNear(pct.Of("b").x, 100.f, "75% after it");
}

void TestGridAutoFill() {
    std::printf("grid: repeat(auto-fill / auto-fit, minmax())\n");
    const std::string cards =
        "<div id='g' style='display:grid;grid-template-columns:repeat(auto-fill, minmax(150px, 1fr))'>"
        "<div id='a'>1</div><div id='b'>2</div><div id='c'>3</div>"
        "<div id='d'>4</div><div id='e'>5</div></div>";
    Laid wide = LayOut(cards, 600.f);
    CheckNear(wide.Of("a").width, 150.f, "four 150px columns fit 600px");
    CheckNear(wide.Of("e").x, 0.f, "the fifth card starts row two");
    Laid narrow = LayOut(cards, 500.f);
    CheckNear(narrow.Of("a").width, 500.f / 3.f, "three fit 500px and share it");
    CheckNear(narrow.Of("d").x, 0.f, "the fourth starts row two");

    Laid fit = LayOut(
        "<div style='display:grid;grid-template-columns:repeat(auto-fit, minmax(100px, 1fr))'>"
        "<div id='a'>1</div><div id='b'>2</div></div>", 600.f);
    CheckNear(fit.Of("a").width, 300.f, "auto-fit: two items share the width");

    Laid padded = LayOut(
        "<div style='width:340px;padding:0 20px'>"
        "<div style='display:grid;grid-template-columns:repeat(auto-fill, 100px)'>"
        "<div id='a'>1</div><div id='b'>2</div><div id='c'>3</div><div id='d'>4</div></div></div>",
        800.f);
    CheckNear(padded.Of("d").x - padded.Of("a").x, 0.f,
              "counted in the grid's own width (340px: three columns)");
}

void TestGridPlacement() {
    std::printf("grid: lines, negative lines, spans, named areas\n");
    Laid laid = LayOut(
        "<div style='display:grid;grid-template-columns:repeat(3, 100px)'>"
        "<div id='head' style='grid-column:1 / -1'>header</div>"
        "<div id='a'>a</div><div id='b' style='grid-column:span 2'>b</div></div>", 600.f);
    CheckNear(laid.Of("head").width, 300.f, "grid-column: 1 / -1 spans every column");
    CheckNear(laid.Of("a").x, 0.f, "the next item starts the second row");
    CheckNear(laid.Of("b").width, 200.f, "span 2");

    Laid areas = LayOut(
        "<div style=\"display:grid;grid-template-columns:120px 1fr;"
        "grid-template-areas:'top top' 'side main'\">"
        "<div id='m' style='grid-area:main'>main</div>"
        "<div id='t' style='grid-area:top'>top</div>"
        "<div id='s' style='grid-area:side'>side</div></div>", 600.f);
    CheckNear(areas.Of("t").x, 0.f, "the top area starts the grid");
    CheckNear(areas.Of("t").width, 600.f, "and spans both columns");
    CheckNear(areas.Of("s").width, 120.f, "side is the first column");
    CheckNear(areas.Of("m").x, 120.f, "main the second");
    Check(areas.Of("m").y > areas.Of("t").y, "below the top, whatever the source order");
    CheckNear(areas.Of("m").y, areas.Of("s").y, "beside the side bar");

    Laid back = LayOut(
        "<div style='display:grid;grid-template-columns:repeat(4, 50px)'>"
        "<div id='x' style='grid-column:span 2 / 5;grid-row:1'>x</div></div>", 600.f);
    CheckNear(back.Of("x").x, 100.f, "span 2 / 5 ends at line 5");
    CheckNear(back.Of("x").width, 100.f, "and spans two columns");
}

void TestGridWithoutColumns() {
    std::printf("grid without columns: a full-width stack\n");
    Laid laid = LayOut(
        "<div style='display:grid;gap:12px'>"
        "<p id='a' style='margin:0'>A paragraph long enough to wrap if it were given the chance, "
        "which in a 300px wide column it certainly is, more than once.</p>"
        "<p id='b' style='margin:0'>Second.</p></div>", 300.f);
    CheckNear(laid.Of("a").width, 300.f, "the item is as wide as the grid");
    CheckNear(laid.Of("b").width, 300.f, "every item");
    CheckNear(laid.Of("b").y, laid.Of("a").y + laid.Of("a").height + 12.f, "gap: 12px between them");
    Check(laid.Of("a").height > 20.f, "long text wraps inside it");
}

void TestHostileValues() {
    std::printf("hostile values stay bounded\n");
    const auto start = std::chrono::steady_clock::now();
    Laid laid = LayOut(
        "<div style='display:grid;grid-template-columns:repeat(3, 50px)'>"
        "<div id='wide' style='grid-column:span 9'>spans more than there are</div>"
        "<div id='far' style='grid-column:99999;grid-row:99999'>far</div>"
        "<div id='many' style='grid-template-columns:repeat(99999999, 1px);display:grid'>m</div>"
        "</div>"
        "<div style='display:grid;grid-template-columns:repeat(auto-fill, 0.0001px)'><i>x</i></div>"
        "<div style='display:flex;order:2147483647;flex:99999999999 -1 -5px'><i>y</i></div>", 600.f);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    Check(laid.root != nullptr, "the page builds");
    Check(laid.Has("wide"), "an item spanning more columns than the grid has is placed");
    Check(laid.Of("far").x <= 1000.f * 50.f + 1.f, "a far line is clamped");
    Check(seconds < 10.0, "in reasonable time");
}

void TestFlowUnchanged() {
    std::printf("normal flow is unchanged\n");
    Laid laid = LayOut(
        "<div><div id='a' style='height:10px'></div><div id='b' style='height:10px'></div></div>",
        300.f);
    CheckNear(laid.Of("a").width, 300.f, "a block fills its line");
    CheckNear(laid.Of("b").y, 10.f, "blocks stack");
    Laid reset = LayOut(
        "<style>.f{display:flex}</style><div class='f' style='display:block'>"
        "<div id='a' style='height:10px'></div><div id='b' style='height:10px'></div></div>", 300.f);
    CheckNear(reset.Of("b").y, 10.f, "a later display: block undoes display: flex");
}

} // namespace

int main() {
    UCImage::InitializeImageSubsysterm("HTMLFlexGridLayoutTest");
    TestFlexRow();
    TestFlexGrow();
    TestFlexColumnAndAlign();
    TestFlexWrapAndOrder();
    TestFlexItemsAreBoxes();
    TestGridTracks();
    TestGridAutoFill();
    TestGridPlacement();
    TestGridWithoutColumns();
    TestHostileValues();
    TestFlowUnchanged();
    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

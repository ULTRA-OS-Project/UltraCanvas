// Tests/ScrollContainerGutterTest.cpp
// A scrolling container lays its children out clear of its vertical
// scrollbar. It used to lay them out across its full width and then clip
// them a scrollbar's width short, so the right edge of every row - a
// right-aligned value, a slider's end, a card - sat under the bar.
//
// The real layout engine over real containers, with an offscreen render
// context standing in for a window. Headless.
//
// Version: 1.0.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "UltraCanvasContainer.h"
#include "UltraCanvasRenderContext.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::CSSLayout;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::printf("  %s %s\n", condition ? "[ OK ]" : "[FAIL]", what.c_str());
    if (!condition) ++g_failures;
}

struct PageRoot : UltraCanvasContainer {
    PageRoot(float w, float h) : UltraCanvasContainer("Page", 0, 0, w, h) {
        renderContext = CreateRenderContext(Size2Di(static_cast<int>(w), static_cast<int>(h)), nullptr);
    }
};

void LayOut(PageRoot& page, float w, float h) {
    page.size.width = Dimension::Px(w);
    page.size.height = Dimension::Px(h);
    page.InvalidateLayout();
    LayoutContext ctx;
    ctx.viewportWidth = w;
    ctx.viewportHeight = h;
    MeasureConstraints mc{ { ConstraintMode::Exact, w }, { ConstraintMode::Exact, h } };
    page.Measure(mc, ctx);
    page.Arrange(Rect2Df{ 0, 0, w, h }, ctx);
}

// A column that scrolls, `rows` rows of 30 px stretched across it, inside
// a page 300 x 200.
struct Fixture {
    std::shared_ptr<PageRoot> page;
    std::shared_ptr<UltraCanvasContainer> view;
    std::vector<std::shared_ptr<UltraCanvasContainer>> rows;
};

Fixture Make(int rows, bool horizontalAllowed) {
    Fixture f;
    f.page = std::make_shared<PageRoot>(300.0f, 200.0f);
    f.page->layout.SetFlexColumn().SetFlexAlignItems(AlignItems::Stretch);
    f.view = CreateContainer("view", 0, 0, 0, 0);
    ContainerStyle style = f.view->GetContainerStyle();
    style.autoShowScrollbars = true;
    style.autoShowHorizontalScrollbar = horizontalAllowed;
    f.view->SetContainerStyle(style);
    f.view->layout.SetFlexColumn().SetFlexAlignItems(AlignItems::Stretch);
    f.view->SetPadding(8.0f);
    f.view->layoutItem.SetFlexGrow(1);
    f.view->layoutItem.SetFlexBasis(Dimension::Px(0));
    f.page->AddChild(f.view);
    for (int i = 0; i < rows; ++i) {
        auto row = CreateContainer("row" + std::to_string(i), 0, 0, 0, 0);
        row->SetElementSize(Dimension::Auto(), Dimension::Px(30));
        row->layoutItem.SetFlexShrink(0);
        f.view->AddChild(row);
        f.rows.push_back(row);
    }
    LayOut(*f.page, 300.0f, 200.0f);
    return f;
}

// The right edge of the rows, in the view's own coordinates.
float RightEdge(const Fixture& f) {
    float edge = 0.0f;
    for (const auto& row : f.rows) {
        const Rect2Df b = row->GetBounds();
        edge = std::max(edge, b.x + b.width);
    }
    return edge;
}

void TestTallContentClearsTheBar() {
    std::printf("\n[content taller than the view: a vertical bar]\n");
    Fixture f = Make(20, false);   // 600 px of rows in a 200 px view
    const Rect2Di area = f.view->GetContentArea();
    Check(f.view->GetContentArea().width < f.view->GetLocalContentRect().width, "the vertical scrollbar shows");
    const float contentRight = f.view->GetPaddingLeft() + area.width;
    std::printf("    rows end at %.1f, the visible content area at %.1f\n", RightEdge(f), contentRight);
    Check(RightEdge(f) <= contentRight + 0.5f, "every row ends inside the visible content area, not under the bar");
    Check(RightEdge(f) >= contentRight - 0.5f, "and still reaches its edge (stretched to it)");
}

void TestShortContentKeepsFullWidth() {
    std::printf("\n[content that fits: no bar]\n");
    Fixture f = Make(3, false);
    const Rect2Di area = f.view->GetContentArea();
    Check(area.width == static_cast<int>(f.view->GetLocalContentRect().width), "no vertical scrollbar");
    Check(RightEdge(f) >= f.view->GetPaddingLeft() + area.width - 0.5f, "the rows take the full width");
}

void TestNoFabricatedHorizontalBar() {
    std::printf("\n[both bars allowed]\n");
    Fixture f = Make(20, true);
    const Rect2Di area = f.view->GetContentArea();
    Check(area.height == static_cast<int>(f.view->GetLocalContentRect().height),
          "the vertical bar does not push the rows into a horizontal overflow");
}

} // namespace

int main() {
    std::printf("ScrollContainerGutterTest\n");
    TestTallContentClearsTheBar();
    TestShortContentKeepsFullWidth();
    TestNoFabricatedHorizontalBar();
    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}

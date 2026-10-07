// Tests/ScrollKeepsPositionOnResizeTest.cpp
// A scroll view keeps its scroll position when the window around it is
// resized, inside a split pane and inside a tabbed container.
//
// Both containers place their children themselves. They used to run the
// container's ordinary block layout first, which laid each pane or the
// active tab's content out stacked and as tall as its content, and only then
// put it in its real place. That throwaway pass let every scroll view inside
// see a viewport as tall as its content, which clamps the scroll position to
// 0: resizing UltraMail's window sent a scrolled message back to its top.
//
// The tests run the real layout engine over the real widgets (an offscreen
// render context stands in for a window), scroll a tall view down, lay the
// page out again at a new size, and check the position is unchanged.
//
// Version: 1.0.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework

#include "UltraCanvasContainer.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasTabbedContainer.h"

#include <cstdio>
#include <memory>
#include <string>

using namespace UltraCanvas;
using namespace UltraCanvas::CSSLayout;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::printf("  %s %s\n", condition ? "[ OK ]" : "[FAIL]", what.c_str());
    if (!condition) ++g_failures;
}

// A page stand-in that owns a render context, the way a window does.
struct PageRoot : UltraCanvasContainer {
    PageRoot(float w, float h) : UltraCanvasContainer("Page", 0, 0, w, h) {
        renderContext = CreateRenderContext(Size2Di(static_cast<int>(w),
                                                    static_cast<int>(h)), nullptr);
    }
};

constexpr float kContentHeight = 2000.0f;
constexpr int   kScrollTo      = 500;

void LayOut(PageRoot& page, float w, float h) {
    page.size.width  = Dimension::Px(w);
    page.size.height = Dimension::Px(h);
    page.InvalidateLayout();
    LayoutContext ctx;
    ctx.viewportWidth = w;
    ctx.viewportHeight = h;
    MeasureConstraints mc{ { ConstraintMode::Exact, w }, { ConstraintMode::Exact, h } };
    page.Measure(mc, ctx);
    page.Arrange(Rect2Df{ 0, 0, w, h }, ctx);
}

// A scroll view holding one block much taller than any page here.
std::shared_ptr<UltraCanvasContainer> MakeScrollView(const std::string& id) {
    auto view = CreateContainer(id, 0, 0, 0, 0);
    ContainerStyle style = view->GetContainerStyle();
    style.autoShowScrollbars = true;
    style.autoShowHorizontalScrollbar = false;
    view->SetContainerStyle(style);
    auto tall = CreateContainer(id + "Tall", 0, 0, 0, 0);
    tall->size.width  = Dimension::Pct(100.0f);
    tall->size.height = Dimension::Px(kContentHeight);
    view->AddChild(tall);
    return view;
}

// Fill a parent: the view takes the whole of what its parent gives it.
void Fill(const std::shared_ptr<UltraCanvasUIElement>& e) {
    e->size.width  = Dimension::Pct(100.0f);
    e->size.height = Dimension::Pct(100.0f);
}

void TestSplitPane() {
    std::printf("Split pane:\n");
    auto page = std::make_shared<PageRoot>(600.0f, 400.0f);
    auto split = std::make_shared<UltraCanvasSplitPane>("Split", 600.0f, 400.0f,
                                                        SplitOrientation::Horizontal);
    Fill(split);
    page->AddChild(split);
    split->AddPane(1.0);
    auto right = split->AddPane(1.0);
    auto view = MakeScrollView("SplitView");
    Fill(view);
    right->AddChild(view);

    LayOut(*page, 600.0f, 400.0f);
    Check(view->GetHeight() < kContentHeight, "the view is shorter than its content");
    view->ScrollToVertical(kScrollTo);
    Check(view->GetVerticalScrollPosition() == kScrollTo, "scrolled down");

    LayOut(*page, 640.0f, 380.0f);
    Check(view->GetVerticalScrollPosition() == kScrollTo,
          "the position survives a resize (got " +
          std::to_string(view->GetVerticalScrollPosition()) + ")");
}

void TestTabbedContainer() {
    std::printf("Tabbed container:\n");
    auto page = std::make_shared<PageRoot>(600.0f, 400.0f);
    auto tabs = std::make_shared<UltraCanvasTabbedContainer>("Tabs", 0.0f, 0.0f, 600.0f, 400.0f);
    Fill(tabs);
    page->AddChild(tabs);
    auto other = CreateContainer("OtherTab", 0, 0, 0, 0);
    auto view = MakeScrollView("TabView");
    tabs->AddTab("First", other);
    const int index = tabs->AddTab("Scrolled", view);
    tabs->SetActiveTab(index);

    LayOut(*page, 600.0f, 400.0f);
    Check(view->GetHeight() > 0.0f && view->GetHeight() < kContentHeight,
          "the view is shorter than its content (height " +
          std::to_string(view->GetHeight()) + ")");
    view->ScrollToVertical(kScrollTo);
    Check(view->GetVerticalScrollPosition() == kScrollTo, "scrolled down");

    LayOut(*page, 640.0f, 380.0f);
    Check(view->GetVerticalScrollPosition() == kScrollTo,
          "the position survives a resize (got " +
          std::to_string(view->GetVerticalScrollPosition()) + ")");
}

} // namespace

int main() {
    TestSplitPane();
    TestTabbedContainer();
    if (g_failures) {
        std::printf("%d check(s) failed.\n", g_failures);
        return 1;
    }
    std::printf("All checks passed.\n");
    return 0;
}

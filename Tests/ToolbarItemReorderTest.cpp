// Tests/ToolbarItemReorderTest.cpp
// UltraCanvasToolbar::EnableItemReordering: an item dragged past its
// neighbours takes their place, and the drop says where it ended up.
//
// MoveItem reorders the toolbar's children and asks for a layout, but their
// bounds stay where they were until that layout runs - and the hit test that
// picks the next target reads those bounds while walking the new order. A
// second drag step inside the same frame therefore found the dragged item's
// index on its old neighbour's rectangle and moved the item straight back,
// so a drag whose steps outran the frames ended where it began and the drop
// reordered nothing. UltraMail's account tiles were the first place anyone
// noticed; this is the framework case under it.
//
// Headless: a window with nothing native behind it, the toolbar laid out by
// hand, and the pointer events pushed through the window's filters the way
// the application's dispatch pushes them.
// Version: 1.0.0
// Last Modified: 2026-11-14
// Author: UltraCanvas Framework

#include "UltraCanvasToolbar.h"
#include "UltraCanvasWindow.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what.c_str());
    if (!condition) ++g_failures;
}

// A window with nothing native behind it, which hands an event to the
// filters its elements installed - all the reorder needs.
class HeadlessWindow : public UltraCanvasWindowBase {
public:
    HeadlessWindow() { SetBounds(Rect2Df(0, 0, 400, 100)); }
    bool Deliver(const UCEvent& event) { return HandleEventFilters(event); }

    void Show() override {}
    void Hide() override {}
    void RaiseAndFocus() override {}
    void SetWindowTitle(const std::string&) override {}
    void SetWindowIcon(const std::string&) override {}
    void SetWindowPosition(int, int) override {}
    void SetWindowSize(int, int) override {}
    void Minimize() override {}
    void Maximize() override {}
    void Restore() override {}
    void SetFullscreen(bool) override {}
    void SetResizable(bool) override {}
    void GetScreenSize(int& width, int& height) const override { width = 400; height = 100; }
    NativeWindowHandle GetNativeHandle() const override { return NativeWindowHandle{}; }
    void InvalidateWindowNative() override {}

protected:
    bool CreateNative() override { return true; }
    void DestroyNative() override {}
    void DoResizeNative() override {}
    bool RecreateNativeSurface() override { return true; }
};

std::shared_ptr<UltraCanvasUIElement> Item(const std::string& id, float w, float h) {
    auto item = std::make_shared<UltraCanvasUIElement>(id, 0, 0, w, h);
    item->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return item;
}

UCEvent Pointer(UCEventType type, int x, int y) {
    UCEvent event;
    event.type = type;
    event.button = UCMouseButton::Left;
    event.pointerWindow = Point2Di(x, y);
    event.pointer = event.pointerWindow;
    return event;
}

// The three items in the order the toolbar holds them.
std::string Order(const std::shared_ptr<UltraCanvasToolbar>& toolbar) {
    std::string ids;
    for (const auto& item : toolbar->GetItems()) {
        if (!ids.empty()) ids += ",";
        ids += item->GetIdentifier();
    }
    return ids;
}

struct Fixture {
    std::shared_ptr<HeadlessWindow> window = std::make_shared<HeadlessWindow>();
    std::shared_ptr<UltraCanvasToolbar> toolbar =
            std::make_shared<UltraCanvasToolbar>("tb", 0, 0, 0, 40);
    std::vector<std::shared_ptr<UltraCanvasUIElement>> items;
    int reorderCalls = 0;
    int from = -1, to = -1;

    Fixture() {
        for (const char* id : {"a", "b", "c"}) {
            items.push_back(Item(id, 100, 32));
            toolbar->AddChild(items.back());
        }
        toolbar->EnableItemReordering(true);
        toolbar->onItemReordered = [this](int f, int t) { ++reorderCalls; from = f; to = t; };
        window->AddChild(toolbar);
        LayOut();
    }

    // What a frame does to the toolbar: the layout the last change asked for.
    void LayOut() {
        CSSLayout::LayoutContext ctx;
        ctx.viewportWidth = 400;
        ctx.viewportHeight = 100;
        CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::AtMost, 400.0f },
                                          { CSSLayout::ConstraintMode::AtMost, 100.0f } };
        toolbar->Measure(mc, ctx);
        toolbar->Arrange(Rect2Df{ 0, 0, toolbar->measured.measuredWidth,
                                  toolbar->measured.measuredHeight }, ctx);
    }

    // The middle of the slot `index` items along, as it is drawn now.
    Point2Di SlotCentre(int index) const {
        const Rect2Df b = items[static_cast<size_t>(index)]->finalBounds;
        return Point2Di(static_cast<int>(b.x + b.width / 2), static_cast<int>(b.y + b.height / 2));
    }

    void Send(UCEventType type, const Point2Di& at) { window->Deliver(Pointer(type, at.x, at.y)); }
};

void TestDragPastOneNeighbour() {
    std::printf("An item dragged past its neighbour\n");
    Fixture f;
    const Point2Di a = f.SlotCentre(0), b = f.SlotCentre(1);
    Check(Order(f.toolbar) == "a,b,c", "the items start in the order they were added");

    f.Send(UCEventType::MouseDown, a);
    f.Send(UCEventType::MouseMove, b);
    Check(Order(f.toolbar) == "b,a,c", "a drag past the next item moves it there");
    Check(f.reorderCalls == 0, "and says nothing yet - the drop is what reports");

    // A second step inside the same slot, before the layout the move asked
    // for: the bounds still describe the old order, so this is where the item
    // used to be sent back to where it started.
    f.Send(UCEventType::MouseMove, Point2Di(b.x + 4, b.y));
    Check(Order(f.toolbar) == "b,a,c", "a second step before the next layout does not undo it");

    f.Send(UCEventType::MouseUp, Point2Di(b.x + 4, b.y));
    Check(f.reorderCalls == 1 && f.from == 0 && f.to == 1,
          "the drop reports the move once, from 0 to 1");
    Check(Order(f.toolbar) == "b,a,c", "and leaves the item where it was dropped");
}

void TestDragOnAcrossLayouts() {
    std::printf("A drag that goes on after the layout has caught up\n");
    Fixture f;
    const Point2Di a = f.SlotCentre(0), b = f.SlotCentre(1), c = f.SlotCentre(2);

    f.Send(UCEventType::MouseDown, a);
    f.Send(UCEventType::MouseMove, b);
    f.LayOut();                       // the frame that carries the move out
    f.Send(UCEventType::MouseMove, c);
    Check(Order(f.toolbar) == "b,c,a", "the item follows the pointer to the last place");
    f.Send(UCEventType::MouseUp, c);
    Check(f.reorderCalls == 1 && f.from == 0 && f.to == 2,
          "and the drop reports where it ended up, not every step on the way");
}

void TestClickMovesNothing() {
    std::printf("A press and release without a drag\n");
    Fixture f;
    const Point2Di a = f.SlotCentre(0);
    f.Send(UCEventType::MouseDown, a);
    f.Send(UCEventType::MouseMove, Point2Di(a.x + 2, a.y));   // under the threshold
    f.Send(UCEventType::MouseUp, Point2Di(a.x + 2, a.y));
    Check(Order(f.toolbar) == "a,b,c" && f.reorderCalls == 0,
          "moves nothing and reports nothing");
}

} // namespace

int main() {
    std::printf("UltraCanvasToolbar item reordering\n");
    TestDragPastOneNeighbour();
    TestDragOnAcrossLayouts();
    TestClickMovesNothing();
    std::printf(g_failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}

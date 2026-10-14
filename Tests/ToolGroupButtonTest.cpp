// Tests/ToolGroupButtonTest.cpp
// UltraCanvasToolGroupButton: several tools sharing one slot of a toolbar,
// with a bar of all of them folding out while the pointer rests on the slot.
//
// Drives the window's real pointer dispatch - the path that decides which
// element an event goes to while a popup is open - and checks that the bar
// folds out beside the slot, stays out while the pointer crosses from the
// slot into it, picks a tool on a click and folds back once the pointer
// leaves both; that the slot then shows the tool last picked (an adaptive
// toolbar) or always the group's main tool (a static one); and that a group
// of one tool is a plain button.
//
// Runs headless under Xvfb. Skips - rather than fails - when there is no
// display, so it stays usable on a bare CI machine.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasToolGroupButton.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasWindow.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

#define SKIP_ALL(reason)                                                      \
    do {                                                                      \
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

namespace {

struct Harness {
    UltraCanvasApplication& app;
    std::shared_ptr<UltraCanvasWindow> window;

    void Send(UCEventType type, Point2Di inWindow, UCMouseButton button = UCMouseButton::NoneButton) {
        UCEvent e;
        e.type = type;
        e.button = button;
        e.targetWindow = window;
        e.nativeWindowHandle = window->GetNativeHandle();
        e.pointerWindow = inWindow;
        e.pointer = inWindow;
        app.DispatchEvent(e);
    }
    void MoveTo(Point2Di p) { Send(UCEventType::MouseMove, p); }
    void Click(Point2Di p) {
        Send(UCEventType::MouseDown, p, UCMouseButton::Left);
        Send(UCEventType::MouseUp, p, UCMouseButton::Left);
    }
    void Key(UCKeys key) {
        UCEvent e;
        e.type = UCEventType::KeyDown;
        e.virtualKey = key;
        e.targetWindow = window;
        e.nativeWindowHandle = window->GetNativeHandle();
        app.DispatchEvent(e);
    }
    // Two frames: the first lays a newly opened popup out, the second paints it.
    void Frame() {
        for (int i = 0; i < 2; ++i) {
            window->RequestRedraw();
            window->UpdateAndRender();
        }
    }
};

Point2Di Centre(const Rect2Df& r) {
    return Point2Di(static_cast<int>(r.x + r.width / 2), static_cast<int>(r.y + r.height / 2));
}

std::vector<ToolGroupItem> Items(const std::vector<std::string>& ids) {
    std::vector<ToolGroupItem> out;
    for (const auto& id : ids) out.push_back(ToolGroupItem{id, "", "Tool " + id});
    return out;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Tool Group Button Suite"              << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("ToolGroupButtonTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "ToolGroupButtonTest";
    cfg.width = 600;
    cfg.height = 400;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();
    DisplayTest::ActivateWindow(app, window);
    Harness h{app, window};

    // ----- a vertical palette on the left, a horizontal bar along the top -----
    auto palette = std::make_shared<UltraCanvasToolbar>("palette", 0, 60, 48, 300);
    palette->SetOrientation(ToolbarOrientation::Vertical);
    window->AddChild(palette);

    std::vector<std::string> picked;
    auto group = palette->AddToolGroup("select", Items({"rect", "ellipse", "lasso"}),
                                       [&picked](const std::string& id) { picked.push_back(id); });
    group->SetElementSize(Size2Df(36, 36));
    group->SetFoldOutDelay(0);
    auto single = palette->AddToolGroup("line", Items({"line"}),
                                        [&picked](const std::string& id) { picked.push_back(id); });
    single->SetElementSize(Size2Df(36, 36));
    single->SetFoldOutDelay(0);

    auto topBar = std::make_shared<UltraCanvasToolbar>("top", 120, 0, 300, 44);
    window->AddChild(topBar);
    auto shapes = topBar->AddToolGroup("shapes", Items({"square", "circle"}));
    shapes->SetElementSize(Size2Df(36, 36));
    shapes->SetFoldOutDelay(0);
    h.Frame();

    const Rect2Df slot = group->GetBoundsInWindow();
    const Point2Di away(500, 300);

    TEST("the first item is current and shown", group->GetCurrentItem() == "rect" && group->GetSlotItem() == "rect");
    TEST("the slot's tooltip is its tool's", group->GetTooltip() == "Tool rect");
    TEST("nothing folded out before the pointer comes", !group->IsFoldOutOpen());

    // ----- the bar folds out while the pointer rests on the slot -----
    h.MoveTo(Centre(slot));
    h.Frame();
    TEST("resting on the slot folds the bar out", group->IsFoldOutOpen());
    auto bar = group->GetFoldOut();
    TEST("the bar holds one button per tool", bar && bar->Children().size() == 3);
    Rect2Df barRect = bar ? bar->GetBoundsInWindow() : Rect2Df();
    TEST("in a vertical toolbar the bar folds out to the right, touching the slot",
         static_cast<int>(barRect.x) == static_cast<int>(slot.x + slot.width));
    TEST("the bar is a row", barRect.width > barRect.height);

    // Crossing from the slot into the bar keeps it out.
    h.MoveTo(Point2Di(static_cast<int>(barRect.x) + 2, Centre(slot).y));
    h.MoveTo(Centre(barRect));
    TEST("the bar stays out while the pointer is on it", group->IsFoldOutOpen());

    // ----- a click in the bar picks that tool -----
    std::shared_ptr<UltraCanvasUIElement> lasso;
    if (bar && bar->Children().size() == 3) {
        lasso = std::static_pointer_cast<UltraCanvasUIElement>(bar->Children()[2]);
    }
    TEST("the bar's third button is there", lasso != nullptr);
    if (lasso) {
        const Rect2Df lassoRect = lasso->GetBoundsInWindow();
        h.MoveTo(Centre(lassoRect));
        h.Click(Centre(lassoRect));
    }
    h.Frame();
    TEST("the click picked the tool", picked.size() == 1 && picked.back() == "lasso");
    TEST("the bar folds back after a pick", !group->IsFoldOutOpen());
    TEST("an adaptive slot now shows the tool last picked",
         group->GetCurrentItem() == "lasso" && group->GetSlotItem() == "lasso");
    TEST("its tooltip follows", group->GetTooltip() == "Tool lasso");
    TEST("a pick presses the slot", group->IsPressed());

    // ----- a click on the slot picks the tool it shows, and keeps it pressed -----
    h.MoveTo(away);
    h.MoveTo(Centre(slot));
    h.Click(Centre(slot));
    h.Frame();
    TEST("a click on the slot picks its tool", picked.size() == 2 && picked.back() == "lasso");
    TEST("a click on a pressed slot leaves it pressed", group->IsPressed());
    TEST("a click on the slot folds the bar back", !group->IsFoldOutOpen());
    h.MoveTo(Point2Di(Centre(slot).x + 1, Centre(slot).y + 1));
    h.Frame();
    TEST("after the click it stays in while the pointer rests on the slot", !group->IsFoldOutOpen());

    // ----- leaving both folds the bar back -----
    h.MoveTo(away);
    h.MoveTo(Centre(slot));
    h.Frame();
    TEST("the bar folds out again", group->IsFoldOutOpen());
    h.MoveTo(away);
    h.Frame();
    TEST("leaving the slot and the bar folds it back", !group->IsFoldOutOpen());

    h.MoveTo(Centre(slot));
    h.Frame();
    h.Key(UCKeys::Escape);
    h.Frame();
    TEST("Escape folds it back", !group->IsFoldOutOpen());
    h.MoveTo(away);

    // ----- a static toolbar: the slot keeps the main tool -----
    group->SetSlotShowsLastPicked(false);
    TEST("a static slot shows the main tool", group->GetSlotItem() == "rect" && group->GetTooltip() == "Tool rect");
    TEST("the current tool is still the one last picked", group->GetCurrentItem() == "lasso");
    group->SetCurrentItem("ellipse");
    TEST("SetCurrentItem leaves a static slot on the main tool", group->GetSlotItem() == "rect");
    h.MoveTo(Centre(slot));
    h.Click(Centre(slot));
    h.Frame();
    TEST("a click on a static slot picks the main tool", picked.size() == 3 && picked.back() == "rect");
    group->SetSlotShowsLastPicked(true);
    TEST("back to adaptive, the slot shows the current tool", group->GetSlotItem() == "rect");
    h.MoveTo(away);

    // ----- a group of one tool is a plain button -----
    const Rect2Df singleRect = single->GetBoundsInWindow();
    h.MoveTo(Centre(singleRect));
    h.Frame();
    TEST("a group of one folds nothing out", !single->IsFoldOutOpen());
    h.Click(Centre(singleRect));
    TEST("a group of one picks its tool on a click", picked.size() == 4 && picked.back() == "line");
    h.MoveTo(away);

    // ----- the delay -----
    group->SetFoldOutDelay(150);
    h.MoveTo(Centre(slot));
    TEST("with a delay the bar does not fold out at once", !group->IsFoldOutOpen());
    h.MoveTo(away);
    group->OpenFoldOut();
    h.Frame();
    TEST("OpenFoldOut folds it out regardless", group->IsFoldOutOpen());
    group->CloseFoldOut();
    TEST("CloseFoldOut folds it back", !group->IsFoldOutOpen());

    // ----- a horizontal toolbar folds out below, as a column -----
    const Rect2Df shapesRect = shapes->GetBoundsInWindow();
    h.MoveTo(Centre(shapesRect));
    h.Frame();
    TEST("a group in a horizontal toolbar folds out", shapes->IsFoldOutOpen());
    if (auto column = shapes->GetFoldOut()) {
        const Rect2Df c = column->GetBoundsInWindow();
        TEST("below the slot, touching it", static_cast<int>(c.y) == static_cast<int>(shapesRect.y + shapesRect.height));
        TEST("as a column", c.height > c.width);
    }
    h.MoveTo(away);
    h.Frame();
    TEST("and folds back when the pointer leaves", !shapes->IsFoldOutOpen());

    // ----- no room on the right: the bar goes to the left -----
    auto edge = std::make_shared<UltraCanvasToolbar>("edge", 552, 60, 48, 120);
    edge->SetOrientation(ToolbarOrientation::Vertical);
    window->AddChild(edge);
    auto edgeGroup = edge->AddToolGroup("edge-group", Items({"x", "y", "z"}));
    edgeGroup->SetElementSize(Size2Df(36, 36));
    h.Frame();
    edgeGroup->OpenFoldOut();
    h.Frame();
    if (auto left = edgeGroup->GetFoldOut()) {
        const Rect2Df l = left->GetBoundsInWindow();
        const Rect2Df e = edgeGroup->GetBoundsInWindow();
        TEST("a bar with no room on the right folds out to the left",
             static_cast<int>(l.x + l.width) == static_cast<int>(e.x));
    } else {
        TEST("a bar with no room on the right folds out", false);
    }
    edgeGroup->CloseFoldOut();

    std::cerr << "========================================" << std::endl;
    std::cerr << testCount - failCount << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}

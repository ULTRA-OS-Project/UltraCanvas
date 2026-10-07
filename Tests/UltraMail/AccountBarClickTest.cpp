// Tests/UltraMail/AccountBarClickTest.cpp
// A click anywhere on an account tile selects that account.
//
// The window handed a click to the innermost element under the pointer and to
// no other. The tile's name, avatar and counters are elements of their own,
// and the rows holding them stretch across the tile, so a click on any of
// them went to a label or a row that ignores it: only the tile's padding
// switched the account, and in 0.10.32 a click on the second account's tile
// mostly did nothing. Every click here goes through the application's own
// dispatch, as the backend delivers it, at the centre of every part of the
// tile.
//
// Opens a real window, so it runs under Xvfb (xvfb-run -a) and skips itself
// without a DISPLAY.
// Version: 1.0.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasWindow.h"
#include "UltraMailAccountBar.h"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraMail;

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

// A left press and release at `at` (window coordinates), delivered the way the
// backend delivers one: to the application, which finds the element.
void Click(UltraCanvasApplication& app, const std::shared_ptr<UltraCanvasWindow>& window,
           const Point2Di& at) {
    UCEvent down;
    down.type = UCEventType::MouseDown;
    down.button = UCMouseButton::Left;
    down.targetWindow = window;
    down.nativeWindowHandle = window->GetNativeHandle();
    down.pointerWindow = at;
    down.pointer = at;
    app.DispatchEvent(down);
    UCEvent up = down;
    up.type = UCEventType::MouseUp;
    app.DispatchEvent(up);
}

Point2Di CentreOf(const UltraCanvasUIElement& element) {
    const Rect2Df b = element.GetBoundsInWindow();
    return Point2Di(static_cast<int>(b.x + b.width / 2), static_cast<int>(b.y + b.height / 2));
}

// The tile and everything inside it, outermost first.
void Collect(const std::shared_ptr<UltraCanvasUIElement>& element,
             std::vector<std::shared_ptr<UltraCanvasUIElement>>& out) {
    out.push_back(element);
    if (auto* box = dynamic_cast<UltraCanvasContainer*>(element.get()))
        for (const auto& child : box->GetChildren()) Collect(child, out);
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   UltraMail Account Bar Click Suite"    << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("AccountBarClickTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "AccountBarClickTest";
    cfg.width = 800;
    cfg.height = 260;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();   // UpdateAndRender() is a no-op on an unmapped window

    // The bar sits in a flex column, stretched, as in the main window.
    auto page = CreateContainer("page", 0, 0, 800, 260);
    page->layout.SetFlexColumn();
    window->AddChild(page);
    AccountBar bar;
    auto root = bar.Build();
    page->AddChild(root);
    root->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                    .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    std::string selected;
    bar.onSelectAccount = [&selected](const std::string& id) { selected = id; };

    std::vector<Account> accounts(2);
    accounts[0].accountId = "info-example-com";
    accounts[0].email = "info@example.com";
    accounts[1].accountId = "info-example-eu";
    accounts[1].email = "info@example.eu";
    std::vector<AccountStatus> status(2);
    status[0].accountId = accounts[0].accountId;
    status[0].unreadToday = 6;  status[0].unreadOlder = 8;   status[0].needsAnswer = 1;
    status[1].accountId = accounts[1].accountId;
    status[1].unreadToday = 4;  status[1].unreadOlder = 129; status[1].needsAnswer = 26;
    bar.Rebuild(accounts, status, accounts[0].accountId);
    for (int frame = 0; frame < 3; ++frame) {
        page->RequestRedraw();
        window->UpdateAndRender();
    }

    const auto tiles = root->GetChildren();
    TEST("two accounts make two tiles", tiles.size() == 2);
    if (tiles.size() != 2) return 1;
    TEST("the tiles were laid out",
         tiles[1]->GetBounds().width > 0 && tiles[1]->GetBounds().height > 0);

    // The second tile: its padding, the avatar, the name, the address, each
    // counter and the rows that hold them.
    std::vector<std::shared_ptr<UltraCanvasUIElement>> parts;
    Collect(tiles[1], parts);
    TEST("the tile has content to click on", parts.size() > 5);
    for (const auto& part : parts) {
        selected.clear();
        Click(app, window, CentreOf(*part));
        TEST("a click on " + part->GetIdentifier() + " selects the second account",
             selected == accounts[1].accountId);
    }

    // And back: the first tile answers the same way.
    selected.clear();
    Click(app, window, CentreOf(*tiles[0]));
    TEST("a click on the first tile selects the first account",
         selected == accounts[0].accountId);

    // The counters' captions live in the tile's tooltip now that the counters
    // no longer take the pointer.
    const auto& tip = tiles[1]->GetTooltipContent();
    TEST("the tile's tooltip names the counters", tip && tip->blocks.size() == 4);

    std::cerr << std::endl << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}

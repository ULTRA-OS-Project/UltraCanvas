// Tests/DropdownKeyboardTest.cpp
// Choosing an item of an open UltraCanvasDropdown from the keyboard.
//
// The arrow keys moved the highlight in the open list, but Return chose
// nothing: the list stayed open, the button kept its old text and
// onSelectionChanged never fired. A dropdown does not take the keyboard focus,
// so while its list is open the window hands the keys to the popup list: the
// list moves its highlight on Up / Down and reports Return through
// onItemActivated - which the dropdown did not listen to.
//
// Every event goes through the application's own dispatch. Opens a real
// window, so it runs under Xvfb (xvfb-run -a) and skips itself without a
// DISPLAY.
// Version: 1.0.0
// Last Modified: 2026-10-15
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasDropdown.h"
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

void Mouse(UltraCanvasApplication& app, const std::shared_ptr<UltraCanvasWindow>& window,
           UCEventType type, int x, int y) {
    UCEvent e;
    e.type = type;
    e.button = UCMouseButton::Left;
    e.targetWindow = window;
    e.nativeWindowHandle = window->GetNativeHandle();
    e.pointerWindow = Point2Di(x, y);
    e.pointer = e.pointerWindow;
    app.DispatchEvent(e);
}

void Key(UltraCanvasApplication& app, const std::shared_ptr<UltraCanvasWindow>& window,
         UCKeys key) {
    UCEvent e;
    e.type = UCEventType::KeyDown;
    e.virtualKey = key;
    e.targetWindow = window;
    e.nativeWindowHandle = window->GetNativeHandle();
    app.DispatchEvent(e);
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Dropdown Keyboard Suite"               << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("DropdownKeyboardTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "DropdownKeyboardTest";
    cfg.width = 300;
    cfg.height = 240;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();
    DisplayTest::ActivateWindow(app, window);

    auto dropdown = CreateDropdown("dd", 20, 20, 120, 24);
    dropdown->AddItem("One");
    dropdown->AddItem("Two");
    dropdown->AddItem("Three");
    dropdown->SetSelectedIndex(0, false);
    std::vector<int> chosen;
    dropdown->onSelectionChanged = [&chosen](int index, const DropdownItem&) { chosen.push_back(index); };
    window->AddChild(dropdown);
    DisplayTest::Frame(window, {dropdown});

    // ----- Opened by a click: the keys go where the window sends them -----
    Mouse(app, window, UCEventType::MouseDown, 60, 32);
    Mouse(app, window, UCEventType::MouseUp, 60, 32);
    TEST("a click opens the list", dropdown->IsDropdownOpen());
    Key(app, window, UCKeys::Down);
    Key(app, window, UCKeys::Down);
    Key(app, window, UCKeys::Return);
    TEST("Down, Down, Return from 'One' chooses 'Three'", dropdown->GetSelectedIndex() == 2);
    TEST("the choice is reported once", chosen.size() == 1 && chosen.back() == 2);
    TEST("Return closes the list", !dropdown->IsDropdownOpen());

    // ----- Again, moving up from the chosen item -----
    chosen.clear();
    Mouse(app, window, UCEventType::MouseDown, 60, 32);
    Mouse(app, window, UCEventType::MouseUp, 60, 32);
    TEST("a click opens the list again", dropdown->IsDropdownOpen());
    Key(app, window, UCKeys::Up);
    Key(app, window, UCKeys::Return);
    TEST("Up, Return from 'Three' chooses 'Two'", dropdown->GetSelectedIndex() == 1);
    TEST("that choice is reported once", chosen.size() == 1 && chosen.back() == 1);
    TEST("and Return closes the list", !dropdown->IsDropdownOpen());

    // ----- Escape leaves the choice alone -----
    chosen.clear();
    Mouse(app, window, UCEventType::MouseDown, 60, 32);
    Mouse(app, window, UCEventType::MouseUp, 60, 32);
    Key(app, window, UCKeys::Down);
    Key(app, window, UCKeys::Escape);
    TEST("Escape closes the list", !dropdown->IsDropdownOpen());
    TEST("Escape keeps the chosen item", dropdown->GetSelectedIndex() == 1 && chosen.empty());

    // ----- Return on the item already chosen just closes -----
    Mouse(app, window, UCEventType::MouseDown, 60, 32);
    Mouse(app, window, UCEventType::MouseUp, 60, 32);
    Key(app, window, UCKeys::Return);
    TEST("Return on the current item closes the list", !dropdown->IsDropdownOpen());
    TEST("and reports no change", dropdown->GetSelectedIndex() == 1 && chosen.empty());

    std::cerr << "========================================" << std::endl;
    std::cerr << "Tests: " << testCount << ", failed: " << failCount << std::endl;
    return failCount == 0 ? 0 : 1;
}

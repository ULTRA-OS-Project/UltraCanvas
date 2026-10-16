// Tests/DropdownKeyboardTest.cpp
// Choosing an item of an open UltraCanvasDropdown from the keyboard.
//
// The arrow keys moved the highlight in the open list, but Return chose
// nothing: the list stayed open, the button kept its old text and
// onSelectionChanged never fired. While nothing holds the keyboard focus the
// window hands the keys to the open popup list, which moves its highlight on
// Up / Down and reports Return through onItemActivated - which the dropdown
// did not listen to.
//
// A dropdown also could not take the keyboard focus at all (AcceptsFocus was
// false), so Tab never reached it and its own keys - Down or Space to open
// it - were never delivered. It takes the focus now, as a button does: from
// Tab and from a click, keeping it while its list is open, so a click on an
// entry still chooses it. SetAcceptsFocus(false) leaves it out again.
//
// Type-ahead: letters typed in the open list go to the item that starts with
// them - what a list of every installed font needs - and Return still chooses
// (Linux reports Return with "\r" as its text, which is not a letter).
//
// Every event goes through the application's own dispatch. Opens a real
// window, so it runs under Xvfb (xvfb-run -a) and skips itself without a
// DISPLAY.
// Version: 1.2.0 - type-ahead, through the focused dropdown and through its open list
// Version: 1.1.0 - the dropdown takes the keyboard focus: Tab, a click, Down / Space
// Version: 1.0.0
// Last Modified: 2026-10-15
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasWindow.h"

#include <cctype>
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
         UCKeys key, const std::string& text = "") {
    UCEvent e;
    e.type = UCEventType::KeyDown;
    e.virtualKey = key;
    e.text = text;
    if (text.size() == 1) e.character = text[0];
    e.targetWindow = window;
    e.nativeWindowHandle = window->GetNativeHandle();
    app.DispatchEvent(e);
}

// A letter as the Linux backend sends it: the key and its text.
void Type(UltraCanvasApplication& app, const std::shared_ptr<UltraCanvasWindow>& window,
          const std::string& letters) {
    for (char c : letters) {
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        Key(app, window, static_cast<UCKeys>(upper), std::string(1, c));
    }
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

    // ----- The keyboard focus -----
    TEST("a click gives the dropdown the keyboard focus", dropdown->IsFocused());
    auto button = CreateButton("after", 160, 20, 100, 24, "After");
    window->AddChild(button);
    DisplayTest::Frame(window, {dropdown, button});
    button->SetFocus(true);
    Key(app, window, UCKeys::Tab);
    TEST("Tab reaches the dropdown", dropdown->IsFocused());
    Key(app, window, UCKeys::Space);
    TEST("Space opens the focused dropdown", dropdown->IsDropdownOpen());
    Key(app, window, UCKeys::Down);
    Key(app, window, UCKeys::Return);
    TEST("Down, Return from 'Two' chooses 'Three'",
         dropdown->GetSelectedIndex() == 2 && chosen.size() == 1 && chosen.back() == 2);
    TEST("it keeps the focus after choosing", dropdown->IsFocused());
    Key(app, window, UCKeys::Down);
    TEST("Down opens it too", dropdown->IsDropdownOpen());
    Key(app, window, UCKeys::Escape);
    TEST("Escape closes it and leaves the focus on it",
         !dropdown->IsDropdownOpen() && dropdown->IsFocused());
    Key(app, window, UCKeys::Tab);
    TEST("Tab moves on from it", button->IsFocused());

    // ----- A click on an entry of the open list still chooses it -----
    // The list opens under the button (20, 44); rows are 24 high after a
    // 1-pixel border, so "One" is at y 45..69.
    chosen.clear();
    Mouse(app, window, UCEventType::MouseDown, 60, 32);
    Mouse(app, window, UCEventType::MouseUp, 60, 32);
    TEST("a click opens the list and focuses it", dropdown->IsDropdownOpen() && dropdown->IsFocused());
    Mouse(app, window, UCEventType::MouseDown, 60, 57);
    Mouse(app, window, UCEventType::MouseUp, 60, 57);
    TEST("a click on an entry chooses it",
         dropdown->GetSelectedIndex() == 0 && chosen.size() == 1 && chosen.back() == 0);
    TEST("and closes the list", !dropdown->IsDropdownOpen());
    TEST("the dropdown keeps the focus", dropdown->IsFocused());

    // ----- Tab away from an open list closes it -----
    Mouse(app, window, UCEventType::MouseDown, 60, 32);
    Mouse(app, window, UCEventType::MouseUp, 60, 32);
    Key(app, window, UCKeys::Tab);
    TEST("Tab from an open dropdown moves on and closes its list",
         button->IsFocused() && !dropdown->IsDropdownOpen());

    // ----- SetAcceptsFocus(false): out of the Tab order, the mouse still works -----
    dropdown->SetAcceptsFocus(false);
    Key(app, window, UCKeys::Tab);
    TEST("a dropdown that does not accept the focus is left out of the Tab order",
         !dropdown->IsFocused());
    chosen.clear();
    Mouse(app, window, UCEventType::MouseDown, 60, 32);
    Mouse(app, window, UCEventType::MouseUp, 60, 32);
    TEST("a click still opens it, without focusing it",
         dropdown->IsDropdownOpen() && !dropdown->IsFocused());
    Key(app, window, UCKeys::Down);
    Key(app, window, UCKeys::Return);
    TEST("and the keys still work its open list",
         dropdown->GetSelectedIndex() == 1 && chosen.size() == 1 && chosen.back() == 1);

    // ----- Type-ahead -----
    // The dropdown still does not take the focus (above), so the window hands
    // the keys to its open list, which passes the letters on to it.
    dropdown->ClearItems();
    for (const char* font : { "DejaVu Sans", "FreeMono", "FreeSans", "Liberation Mono", "Liberation Serif", "Sans" })
        dropdown->AddItem(font);
    dropdown->SetSelectedIndex(5, false);
    chosen.clear();
    Mouse(app, window, UCEventType::MouseDown, 60, 32);
    Mouse(app, window, UCEventType::MouseUp, 60, 32);
    TEST("the font list opens", dropdown->IsDropdownOpen());
    Type(app, window, "libe");
    Key(app, window, UCKeys::Return, "\r");
    TEST("'libe' then Return chooses 'Liberation Mono'", dropdown->GetSelectedIndex() == 3);
    TEST("Return closes the list after type-ahead", !dropdown->IsDropdownOpen());
    TEST("the choice is reported", chosen.size() == 1 && chosen.back() == 3);

    // The same letter again steps on through the items starting with it.
    chosen.clear();
    Mouse(app, window, UCEventType::MouseDown, 60, 32);
    Mouse(app, window, UCEventType::MouseUp, 60, 32);
    Type(app, window, "f");
    Type(app, window, "f");
    Key(app, window, UCKeys::Return, "\r");
    TEST("'f', 'f' passes FreeMono for FreeSans", dropdown->GetSelectedIndex() == 2);

    // Upper or lower case alike, and a letter nothing starts with moves nothing.
    Mouse(app, window, UCEventType::MouseDown, 60, 32);
    Mouse(app, window, UCEventType::MouseUp, 60, 32);
    Type(app, window, "D");
    Type(app, window, "q");
    Key(app, window, UCKeys::Return, "\r");
    TEST("'D' finds 'DejaVu Sans'; 'q' after it finds nothing and moves nothing",
         dropdown->GetSelectedIndex() == 0);

    // A space carries on a name being typed: font names have them.
    Mouse(app, window, UCEventType::MouseDown, 60, 32);
    Mouse(app, window, UCEventType::MouseUp, 60, 32);
    Type(app, window, "liberation s");
    Key(app, window, UCKeys::Return, "\r");
    TEST("'liberation s' then Return chooses 'Liberation Serif'", dropdown->GetSelectedIndex() == 4);

    // Focused, the dropdown takes the letters itself.
    dropdown->SetAcceptsFocus(true);
    chosen.clear();
    Mouse(app, window, UCEventType::MouseDown, 60, 32);
    Mouse(app, window, UCEventType::MouseUp, 60, 32);
    TEST("a click opens the font list and focuses the dropdown",
         dropdown->IsDropdownOpen() && dropdown->IsFocused());
    Type(app, window, "frees");
    Key(app, window, UCKeys::Return, "\r");
    TEST("focused, 'frees' then Return chooses 'FreeSans'",
         dropdown->GetSelectedIndex() == 2 && chosen.size() == 1 && chosen.back() == 2);
    Mouse(app, window, UCEventType::MouseDown, 60, 32);
    Mouse(app, window, UCEventType::MouseUp, 60, 32);
    Type(app, window, "liberation se");
    Key(app, window, UCKeys::Return, "\r");
    TEST("focused, 'liberation se' then Return chooses 'Liberation Serif', not 'Liberation Mono'",
         dropdown->GetSelectedIndex() == 4);
    // A space with nothing typed before it is not type-ahead: the focused,
    // closed dropdown opens on it, as before.
    Key(app, window, UCKeys::Space, " ");
    TEST("Space still opens the focused dropdown", dropdown->IsDropdownOpen());
    Key(app, window, UCKeys::Escape);

    std::cerr << "========================================" << std::endl;
    std::cerr << "Tests: " << testCount << ", failed: " << failCount << std::endl;
    return failCount == 0 ? 0 : 1;
}

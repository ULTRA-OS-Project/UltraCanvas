// Tests/TextAreaAutoCompleteTest.cpp
// Auto-completion in a real UltraCanvasTextArea, in a window:
//   - ShowAutoComplete opens the list, an empty one closes it;
//   - GetAutoCompletePrefix is the word before the caret, letters beyond
//     ASCII included, and stops at a '.';
//   - Down moves in the list and Enter puts the suggestion in place of that
//     word; Tab takes the first one; Escape closes it and changes nothing;
//   - another key edits as usual and leaves the list open;
//   - the keys are the editor's own again once the list is closed.
// Runs headless under Xvfb; skips when there is no display.
// Version: 1.0.0
// Last Modified: 2026-10-24
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasWindow.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

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

bool Key(UltraCanvasTextArea& area, UCKeys key, const std::string& text = "") {
    UCEvent event;
    event.type = UCEventType::KeyDown;
    event.virtualKey = key;
    event.text = text;
    return area.OnEvent(event);
}

// The caret at the end of `text`, the only line.
void Type(UltraCanvasTextArea& area, const std::string& text) {
    area.SetText(text);
    area.MoveCursorToEnd(false);
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   TextArea Auto-Complete Suite"          << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("TextAreaAutoCompleteTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "TextAreaAutoCompleteTest";
    cfg.width = 600;
    cfg.height = 320;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();

    auto area = std::make_shared<UltraCanvasTextArea>("Code", 0, 0, 580, 300);
    window->AddChild(area);
    area->SetFocus(true);
    window->UpdateAndRender();

    // ===== THE WORD BEFORE THE CARET =====
    Type(*area, "SELECT cu");
    TEST("The prefix is the word before the caret", area->GetAutoCompletePrefix() == "cu");
    Type(*area, "SELECT o.to");
    TEST("A '.' ends it", area->GetAutoCompletePrefix() == "to");
    Type(*area, "SELECT größe");
    TEST("Letters beyond ASCII belong to it", area->GetAutoCompletePrefix() == "größe");
    Type(*area, "SELECT ");
    TEST("After a space there is none", area->GetAutoCompletePrefix().empty());

    // ===== OPEN, MOVE, TAKE =====
    Type(*area, "SELECT cu");
    area->ShowAutoComplete({ "customers", "customer_id" });
    TEST("ShowAutoComplete opens the list", area->IsAutoCompleteOpen());
    Key(*area, UCKeys::Down);
    Key(*area, UCKeys::Return);
    TEST("Down and Enter take the second suggestion in place of the word",
         area->GetText() == "SELECT customer_id");
    TEST("and close the list", !area->IsAutoCompleteOpen());
    Key(*area, UCKeys::Return);
    TEST("Enter is the editor's own again once it is closed", area->GetText() == "SELECT customer_id\n");

    Type(*area, "SELECT * FROM or");
    area->ShowAutoComplete({ "orders", "order_items" });
    Key(*area, UCKeys::Tab);
    TEST("Tab takes the highlighted first one", area->GetText() == "SELECT * FROM orders");

    // ===== ESCAPE, AN EMPTY LIST =====
    Type(*area, "SELECT cu");
    area->ShowAutoComplete({ "customers" });
    Key(*area, UCKeys::Escape);
    TEST("Escape closes the list", !area->IsAutoCompleteOpen());
    TEST("and changes nothing", area->GetText() == "SELECT cu");
    area->ShowAutoComplete({ "customers" });
    area->ShowAutoComplete({});
    TEST("An empty list closes it", !area->IsAutoCompleteOpen());

    // ===== TYPING ON =====
    Type(*area, "SELECT c");
    area->ShowAutoComplete({ "customers", "count" });
    Key(*area, UCKeys::Unknown, "u");
    TEST("A letter typed edits as usual", area->GetText() == "SELECT cu");
    TEST("and leaves the list open for the host to narrow", area->IsAutoCompleteOpen());
    area->ShowAutoComplete({ "customers" });
    area->AcceptAutoComplete();
    TEST("AcceptAutoComplete takes the highlighted one", area->GetText() == "SELECT customers");

    // ===== READ ONLY =====
    Type(*area, "SELECT cu");
    area->SetReadOnly(true);
    area->ShowAutoComplete({ "customers" });
    TEST("A read-only area opens no list", !area->IsAutoCompleteOpen());
    area->SetReadOnly(false);

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "Results: " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}

// Tests/MouseClickBubblingTest.cpp
// A mouse press the element under the pointer does not handle goes to the
// elements around it.
//
// The window hands a press to the innermost element under the pointer. It
// used to go nowhere else: a click on the label inside a clickable card went
// to the label, which ignores it, and the card never heard of it - UltraMail's
// account tiles switched the account only from their padding. A press now
// climbs the parent chain until an element takes it, as the wheel, drag,
// touch and keyboard events already did. It stops below the window (which
// still gets an unhandled press once, at the end, as before), so a press in a
// popup never reaches what lies under the popup.
//
// Every press here goes through the application's own dispatch, as the
// backend delivers it, and a window event filter records each element it was
// handed to, in order.
//
// Opens a real window, so it runs under Xvfb (xvfb-run -a) and skips itself
// without a DISPLAY.
// Version: 1.0.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

// The elements a press was handed to, in order (the window's event filter
// sees every dispatch).
std::vector<std::string> handedTo;

void Press(UltraCanvasApplication& app, const std::shared_ptr<UltraCanvasWindow>& window,
           const UltraCanvasUIElement& at, UCEventType type = UCEventType::MouseDown,
           UCMouseButton button = UCMouseButton::Left) {
    const Rect2Df b = at.GetBoundsInWindow();
    const Point2Di p(static_cast<int>(b.x + b.width / 2), static_cast<int>(b.y + b.height / 2));
    UCEvent e;
    e.type = type;
    e.button = button;
    e.targetWindow = window;
    e.nativeWindowHandle = window->GetNativeHandle();
    e.pointerWindow = p;
    e.pointer = p;
    handedTo.clear();
    app.DispatchEvent(e);
}

// A press and its release, as one click.
void Click(UltraCanvasApplication& app, const std::shared_ptr<UltraCanvasWindow>& window,
           const UltraCanvasUIElement& at) {
    Press(app, window, at, UCEventType::MouseDown);
    std::vector<std::string> down = handedTo;
    Press(app, window, at, UCEventType::MouseUp);
    down.insert(down.end(), handedTo.begin(), handedTo.end());
    handedTo = down;
}

int Count(const std::string& id) {
    return static_cast<int>(std::count(handedTo.begin(), handedTo.end(), id));
}

// A container that takes the presses of `type` it is handed and counts them.
std::shared_ptr<UltraCanvasContainer> Card(const std::string& id, float x, float y, float w, float h,
                                           int& presses, bool takesThem = true) {
    auto card = CreateContainer(id, x, y, w, h);
    card->SetEventCallback([&presses, takesThem](const UCEvent& e) {
        if (e.type != UCEventType::MouseDown && e.type != UCEventType::MouseDoubleClick)
            return false;
        ++presses;
        return takesThem;
    });
    return card;
}

void Frame(const std::shared_ptr<UltraCanvasWindow>& window,
           const std::shared_ptr<UltraCanvasUIElement>& root) {
    for (int i = 0; i < 2; ++i) {
        root->RequestRedraw();
        window->UpdateAndRender();
    }
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Mouse Click Bubbling Suite"           << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("MouseClickBubblingTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "MouseClickBubblingTest";
    cfg.width = 640;
    cfg.height = 420;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();   // UpdateAndRender() is a no-op on an unmapped window

    const std::vector<UCEventType> pressTypes = {
        UCEventType::MouseDown, UCEventType::MouseUp, UCEventType::MouseDoubleClick};
    window->InstallEventFilter("record", [](const UCEvent& e) {
        if (e.targetElement) handedTo.push_back(e.targetElement->GetIdentifier());
        return false;   // only watching
    }, pressTypes);

    // outer (counts, does not take) > card (counts, takes) > row > label
    //                                                      > button
    int outerPresses = 0, cardPresses = 0, buttonClicks = 0;
    auto outer = Card("outer", 0, 0, 640, 420, outerPresses, /*takesThem=*/false);
    window->AddChild(outer);
    auto card = Card("card", 20, 20, 300, 160, cardPresses);
    outer->AddChild(card);
    auto row = CreateContainer("row", 10, 10, 280, 40);
    card->AddChild(row);
    auto label = CreateLabel("label", 10, 5, 200, 30, "A label inside a card");
    row->AddChild(label);
    auto button = CreateButton("button", 10, 100, 120, 32, "A button");
    button->SetOnClick([&buttonClicks]() { ++buttonClicks; });
    card->AddChild(button);
    Frame(window, outer);

    // ----- A press on the label reaches the card -----
    Press(app, window, *label);
    TEST("a press on a label is handed to the label first",
         !handedTo.empty() && handedTo.front() == "label");
    TEST("a press the label does not take climbs to the card, through the row",
         cardPresses == 1 && Count("row") == 1 && Count("card") == 1);
    TEST("the climb stops at the card, which took it",
         outerPresses == 0 && Count("outer") == 0);
    TEST("a taken press does not reach the window",
         Count(window->GetIdentifier()) == 0);

    Press(app, window, *label, UCEventType::MouseDoubleClick);
    TEST("a double-click on the label climbs to the card", cardPresses == 2);

    // ----- A button keeps its own press -----
    cardPresses = 0;
    Click(app, window, *button);
    TEST("a click on a button fires the button", buttonClicks == 1);
    TEST("a press the button took is not handed to the card",
         cardPresses == 0 && Count("card") == 0);

    // A right press the button has no use for (no context menu) goes on to
    // the card, as a context click on a card's content should.
    Press(app, window, *button, UCEventType::MouseDown, UCMouseButton::Right);
    TEST("a right press a button does not use climbs to the card", cardPresses == 1);

    // ----- Nobody takes it: every ancestor once, then the window once -----
    int loosePresses = 0;
    auto loose = Card("loose", 340, 20, 280, 160, loosePresses, /*takesThem=*/false);
    outer->AddChild(loose);
    auto looseLabel = CreateLabel("looseLabel", 10, 10, 200, 30, "Nobody takes this");
    loose->AddChild(looseLabel);
    Frame(window, outer);
    outerPresses = 0;
    Press(app, window, *looseLabel);
    TEST("an untaken press visits each ancestor once",
         loosePresses == 1 && outerPresses == 1 && Count("loose") == 1 && Count("outer") == 1);
    TEST("an untaken press reaches the window exactly once, last",
         Count(window->GetIdentifier()) == 1 && handedTo.back() == window->GetIdentifier());

    // ----- A popup is the top of its own chain -----
    // Opened over the card: the card is under the popup on screen but is not
    // its ancestor, and the climb must not wander from the popup to the
    // window's other children.
    int popupPresses = 0;
    auto popup = Card("popup", 0, 0, 220, 90, popupPresses, /*takesThem=*/false);
    auto popupLabel = CreateLabel("popupLabel", 10, 10, 180, 30, "In a popup");
    popup->AddChild(popupLabel);
    PopupElementSettings settings;
    settings.closeByClickOutside = false;
    window->OpenPopup(Point2Di(40, 40), *popup, settings);
    Frame(window, outer);
    cardPresses = outerPresses = 0;
    Press(app, window, *popupLabel);
    TEST("a press in a popup climbs to the popup", popupPresses == 1 && Count("popup") == 1);
    TEST("a press in a popup does not reach what lies under it",
         cardPresses == 0 && outerPresses == 0 && Count("card") == 0 && Count("outer") == 0);
    TEST("a press in a popup reaches the window once, as before",
         Count(window->GetIdentifier()) == 1);
    window->ClosePopup(*popup, ClosePopupReason::Manual);
    Frame(window, outer);

    // ----- A text input takes its double-click -----
    // A fast second click arrives as a double-click. The input dropped it,
    // so it would now climb - a spreadsheet took it as a double-click on the
    // cell and restarted the edit, losing what had been typed. It selects
    // the word under the pointer instead, and the release is the input's.
    int boxPresses = 0;
    auto box = Card("box", 20, 190, 300, 50, boxPresses);
    outer->AddChild(box);
    auto input = CreateTextInput("input", 10, 10, 260, 30);
    box->AddChild(input);
    input->SetText("hello world");
    Frame(window, outer);
    Press(app, window, *input);
    Press(app, window, *input, UCEventType::MouseUp);
    Press(app, window, *input, UCEventType::MouseDoubleClick);
    TEST("a text input takes a double-click", Count("box") == 0);
    Press(app, window, *input, UCEventType::MouseUp);
    TEST("and the release after it", Count("box") == 0 && boxPresses == 0);
    TEST("a double-click past the text selects the last word",
         input->GetSelectedText() == "world");

    // ----- A self-painting view ignores a press its own element left -----
    // The file view paints its files and holds a few real elements over them.
    // A right press on its "clear filter" button (which has no menu of its
    // own) climbs to the view, and must not be read as a right-click on the
    // folder behind the button: that took the keyboard, committed a rename
    // and opened the folder's menu.
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path folder = fs::temp_directory_path(ec) / "MouseClickBubblingTest-folder";
    fs::create_directories(folder, ec);
    { std::ofstream(folder / "a.txt") << "a"; }
    int clears = 0;
    auto filer = std::make_shared<UltraCanvasFilerWidget>("filer", 20, 250, 600, 160);
    outer->AddChild(filer);
    filer->SetPath(PathToUtf8(folder));
    filer->SetFilterEmptyAction("Clear filter", [&clears]() { ++clears; });
    filer->SetNameFilter("no-such-name");
    Frame(window, outer);
    std::shared_ptr<UltraCanvasUIElement> clearButton;
    for (const auto& child : filer->GetChildren()) {
        const std::string& id = child->GetIdentifier();
        if (child->IsVisible() && id.size() > 13 && id.substr(id.size() - 13) == "-filter-empty")
            clearButton = child;
    }
    TEST("the empty filter offers its clear button", clearButton != nullptr);
    if (clearButton) {
        DisplayTest::ActivateWindow(app, window);   // so a focus change would show
        Press(app, window, *clearButton, UCEventType::MouseDown, UCMouseButton::Right);
        Press(app, window, *clearButton, UCEventType::MouseUp, UCMouseButton::Right);
        TEST("a right press on the view's own button opens no folder menu",
             window->GetActivePopupElement() == nullptr);
        TEST("and does not take the keyboard", !filer->IsFocused());
        Click(app, window, *clearButton);
        TEST("a left click still clears the filter", clears == 1);
    }
    fs::remove_all(folder, ec);

    // ----- An element that leaves the tree while handling the press -----
    // Its press did what it was for; the elements it left must not get it.
    auto leaving = CreateLabel("leaving", 10, 60, 200, 30, "Removes itself");
    card->AddChild(leaving);
    Frame(window, outer);
    // `leaving` (held here) keeps the label alive once the card lets go of it.
    leaving->SetEventCallback([card = card.get(), self = leaving.get()](const UCEvent& e) {
        if (e.type == UCEventType::MouseDown) card->RemoveChild(self->shared_from_this());
        return false;
    });
    cardPresses = outerPresses = 0;
    Press(app, window, *leaving);
    TEST("a press whose element left the tree is not handed to its former parents",
         cardPresses == 0 && outerPresses == 0);
    leaving->SetEventCallback(nullptr);

    std::cerr << std::endl << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}

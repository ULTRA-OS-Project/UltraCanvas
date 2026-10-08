// Tests/LabelSelectionTest.cpp
// Selectable labels (UltraCanvasLabel::SetSelectable) and a selection shared
// by several labels (UltraCanvasTextSelection): what a press, a drag and a
// release do - on plain text and on a link - what is copied, and that a label
// nobody made selectable behaves exactly as before (a link opens on the
// press, a click handler clicks).
//
// Opens a real window (the text layout needs a render context), so it runs
// under Xvfb (xvfb-run -a) and skips itself without a DISPLAY.
// Version: 1.0.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextSelection.h"
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

UCEvent MouseAt(UCEventType type, UltraCanvasLabel& label, float x, float y) {
    const Rect2Df b = label.GetBoundsInWindow();
    UCEvent ev;
    ev.type = type;
    ev.button = UCMouseButton::Left;
    ev.pointer = Point2Di(static_cast<int>(x), static_cast<int>(y));
    ev.pointerWindow = Point2Di(static_cast<int>(b.x + x), static_cast<int>(b.y + y));
    return ev;
}

// A label-local x on the middle of the first line where the text index lies
// in [first, last); -1 when there is none.
float XForBytes(UltraCanvasLabel& label, int first, int last) {
    const float y = label.GetHeight() / 2.0f;
    for (float x = 0.0f; x < label.GetWidth(); x += 1.0f) {
        const int index = label.TextIndexAtPoint(Point2Df(x, y));
        if (index > first && index < last) return x;
    }
    return -1.0f;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Label Selection Suite"                << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("LabelSelectionTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "LabelSelectionTest";
    cfg.width = 500;
    cfg.height = 300;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();

    auto page = CreateContainer("page", 0, 0, 500, 300);
    page->layout.SetFlexColumn().SetFlexGap(4);
    window->AddChild(page);

    // A selectable label with a link on "terms" (bytes 9-14).
    int opened = 0;
    auto linked = CreateLabel("linked", "Read the terms or call us today");
    linked->SetTextLinks({ LabelTextLink{9, 14, "https://example.com/terms"} });
    linked->onLinkActivated = [&opened](const std::string&) { ++opened; };
    linked->SetSelectable(true);
    page->AddChild(linked);

    // The same, not selectable: everything as before.
    int openedOld = 0;
    auto old = CreateLabel("old", "Read the terms or call us today");
    old->SetTextLinks({ LabelTextLink{9, 14, "https://example.com/terms"} });
    old->onLinkActivated = [&openedOld](const std::string&) { ++openedOld; };
    page->AddChild(old);

    // A selectable label that is also a button: it stays a button.
    int clicked = 0;
    auto button = CreateLabel("button", "Click me");
    button->onClick = [&clicked]() { ++clicked; };
    button->SetSelectable(true);
    page->AddChild(button);

    // Three labels sharing one selection: two side by side, one below.
    auto row = CreateContainer("row", 0, 0, 0, 0);
    row->layout.SetFlexRow().SetFlexGap(20);
    auto cellA = CreateLabel("cellA", "Name");
    auto cellB = CreateLabel("cellB", "Anna");
    row->AddChild(cellA);
    row->AddChild(cellB);
    page->AddChild(row);
    auto below = CreateLabel("below", "Visit example.com, don't wait");
    page->AddChild(below);
    auto shared = std::make_shared<UltraCanvasTextSelection>();
    shared->AddLabelsIn(*row);
    shared->AddLabel(*below);

    // Text a reader copies differently from how it is stored.
    auto special = CreateLabel("special", "a\xC2\xA0" "b\xC2\xAD" "c");
    special->SetSelectable(true);
    page->AddChild(special);

    for (int frame = 0; frame < 3; ++frame) {
        page->RequestRedraw();
        window->UpdateAndRender();
    }

    // ===== Links in a selectable label =====
    const float linkX = XForBytes(*linked, 9, 14);
    const float midY = linked->GetHeight() / 2.0f;
    TEST("the link's text is found on the line", linkX > 0.0f);
    linked->OnEvent(MouseAt(UCEventType::MouseDown, *linked, linkX, midY));
    TEST("a selectable label's link does not open on the press", opened == 0);
    linked->OnEvent(MouseAt(UCEventType::MouseUp, *linked, linkX, midY));
    TEST("it opens on the release, when nothing was dragged", opened == 1);

    linked->OnEvent(MouseAt(UCEventType::MouseDown, *linked, linkX, midY));
    UCEvent drag = MouseAt(UCEventType::MouseMove, *linked, linked->GetWidth() - 2.0f, midY);
    linked->OnEvent(drag);
    drag.type = UCEventType::MouseUp;
    linked->OnEvent(drag);
    TEST("a drag that starts on a link selects instead of opening it", opened == 1);
    const std::string fromLink = linked->GetSelectedText();
    TEST("the drag selected from the link to the end of the line (" + fromLink + ")",
         !fromLink.empty() && fromLink.size() <= 22 &&
         std::string("Read the terms or call us today").find(fromLink) != std::string::npos &&
         fromLink.compare(fromLink.size() - 5, 5, "today") == 0);
    TEST("the pressed label takes the keyboard focus", linked->AcceptsFocus());

    // ===== Unchanged without SetSelectable =====
    const float oldX = XForBytes(*old, 9, 14);
    old->OnEvent(MouseAt(UCEventType::MouseDown, *old, oldX, old->GetHeight() / 2.0f));
    TEST("a label nobody made selectable opens its link on the press", openedOld == 1);
    TEST("and takes no keyboard focus", !old->AcceptsFocus());
    button->OnEvent(MouseAt(UCEventType::MouseDown, *button, 2.0f, button->GetHeight() / 2.0f));
    TEST("a label with a click handler clicks, selectable or not", clicked == 1 && !button->HasSelectedRange());

    // ===== Positions and words =====
    const int length = static_cast<int>(below->GetRenderedText().size());
    TEST("a point above the text is its start", below->TextIndexAtPoint(Point2Df(5.0f, -20.0f)) == 0);
    TEST("a point below the text is its end",
         below->TextIndexAtPoint(Point2Df(5.0f, below->GetHeight() + 20.0f)) == length);
    const std::pair<int, int> site = below->WordRangeAt(8);      // in "example"
    TEST("a word with a dot inside it is one word (example.com)",
         below->GetRenderedText().substr(static_cast<size_t>(site.first),
                                         static_cast<size_t>(site.second - site.first)) == "example.com");
    const std::pair<int, int> dont = below->WordRangeAt(20);     // in "don't"
    TEST("an apostrophe inside a word keeps it whole (don't)",
         below->GetRenderedText().substr(static_cast<size_t>(dont.first),
                                         static_cast<size_t>(dont.second - dont.first)) == "don't");

    // ===== Copy =====
    special->SetSelectedRange(0, 1000);
    TEST("a no-break space copies as a space, a soft hyphen not at all",
         special->GetSelectedText() == "a bc");
    special->SetText("new text");
    TEST("a new text drops the selection", !special->HasSelectedRange());

    // ===== One selection across labels =====
    shared->SelectAll();
    TEST("labels side by side are joined by a tab, a label below by a line break",
         shared->GetSelectedText() == "Name\tAnna\nVisit example.com, don't wait");
    TEST("every label of the selection is highlighted",
         cellA->HasSelectedRange() && cellB->HasSelectedRange() && below->HasSelectedRange());
    // A drag from the first cell down into the label below.
    cellA->OnEvent(MouseAt(UCEventType::MouseDown, *cellA, 1.0f, cellA->GetHeight() / 2.0f));
    UCEvent down = MouseAt(UCEventType::MouseMove, *below, XForBytes(*below, 0, 5), below->GetHeight() / 2.0f);
    cellA->OnEvent(down);
    down.type = UCEventType::MouseUp;
    cellA->OnEvent(down);
    const std::string across = shared->GetSelectedText();
    TEST("a drag runs on from one label into the next (" + across + ")",
         across.rfind("Name\tAnna\n", 0) == 0 && across.size() > 10 && across.size() < 16);
    shared->ClearSelection();
    TEST("ClearSelection clears every label",
         !shared->HasSelection() && !cellA->HasSelectedRange() && !below->HasSelectedRange());
    // A destroyed label leaves the selection.
    row->RemoveChild(cellB);
    cellB.reset();
    TEST("a destroyed label leaves the selection", shared->GetLabelCount() == 2);
    shared->SelectAll();
    TEST("and the rest still copies", shared->GetSelectedText() == "Name\nVisit example.com, don't wait");

    std::cerr << std::endl << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}

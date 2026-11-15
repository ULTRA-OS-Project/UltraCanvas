// Tests/ListViewFocusRingTest.cpp
// The list view's focused row - the one the arrow keys continue from -
// carries a dashed focus ring while the list holds the keyboard focus, and
// none once the focus leaves; read back from the composited window under Xvfb.
//
// - On a selected row the ring takes ListViewStyle::focusRingOnSelectionColor
//   (white here), on a plain row the selection colour; SetFocusRingColor
//   overrides both.
// - The ring is the view's, drawn over what the delegate drew, once across
//   the whole row: a multi-column list has one ring, not one per column.
// - The ring is looked for along the row's edges only (DisplayTest::RingSides):
//   the selected row's label is white as well, and a white pixel anywhere in
//   the row would match it.
//
// Skips itself without a DISPLAY, unless ULTRACANVAS_TEST_DISPLAY_REQUIRED=1
// (CI's virtual-display step), where a skip fails instead.
// Version: 1.0.0
// Last Modified: 2026-11-15
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasListModel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasWindow.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;
using DisplayTest::Look;
using DisplayTest::RingSides;
using DisplayTest::RowShows;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

namespace {

const Color kSelection(0, 120, 215);
const Color kMagenta(255, 0, 255);

// A skip would pass for a run where a display was asked for.
bool DisplayRequired() {
    const char* required = std::getenv("ULTRACANVAS_TEST_DISPLAY_REQUIRED");
    return required && std::string(required) == "1";
}

void Skip(const std::string& why) {
    if (DisplayRequired()) {
        TEST("a display was required, and " + why, false);
    } else {
        std::cerr << "SKIP: list view focus ring (" << why << ")" << std::endl;
    }
}

UCEvent Key(UCKeys key) {
    UCEvent e;
    e.type = UCEventType::KeyDown;
    e.virtualKey = key;
    return e;
}

// A list of 300x200 at the window's origin, 20 px rows, the selection in a
// known blue and the ring on it in white.
std::shared_ptr<UltraCanvasListView> MakeList(const std::shared_ptr<UltraCanvasWindow>& window,
                                              std::shared_ptr<IListModel> model) {
    auto list = std::make_shared<UltraCanvasListView>("list", 0, 0, 300, 200);
    ListViewStyle style = list->GetStyle();
    style.rowHeight = 20;
    style.showHeader = false;
    style.alternateRowColors = false;
    style.selectionBackgroundColor = kSelection;
    style.focusRingOnSelectionColor = Colors::White;
    list->SetStyle(style);
    list->SetModel(model);
    window->AddChild(list);
    return list;
}

// Row `row` of the list, inside its border: where the selection fills and
// the ring runs one pixel in from the edges.
Rect2Di RowRect(int row) { return Rect2Di(1, 1 + row * 20, 298, 20); }

void TestSingleColumn(UltraCanvasApplication& app, const std::shared_ptr<UltraCanvasWindow>& window) {
    auto model = std::make_shared<UltraCanvasSimpleListModel>();
    model->AddItem("a");
    model->AddItem("b");
    model->AddItem("c");
    auto list = MakeList(window, model);

    if (!DisplayTest::FocusElement(app, window, list)) {
        Skip("the window could not be activated");
        window->RemoveChild(list);
        return;
    }
    list->OnEvent(Key(UCKeys::Down));   // the first row
    list->OnEvent(Key(UCKeys::Down));   // then the second
    DisplayTest::Frame(window, {list});

    TEST("the focused row is drawn selected", RowShows(window, RowRect(1), kSelection, Look::Area));
    TEST("the focused row carries a white ring while the list is focused",
         RingSides(window, RowRect(1), Colors::White) == 4);
    // A ring there would be in the selection colour (the row is plain); the
    // row itself is the list's background, white in the light theme.
    TEST("a row that is not focused carries no ring", RingSides(window, RowRect(0), kSelection) == 0);
    TEST("a row that is not focused is not drawn selected",
         !RowShows(window, RowRect(0), kSelection, Look::Area));

    window->ClearFocus();
    DisplayTest::Frame(window, {list});
    TEST("the list is not focused once the window's focus is cleared", !list->IsFocused());
    TEST("the ring goes when the focus leaves, the selection stays",
         RingSides(window, RowRect(1), Colors::White) == 0 &&
         RowShows(window, RowRect(1), kSelection, Look::Area));

    list->SetFocus(true);
    DisplayTest::Frame(window, {list});
    TEST("the ring comes back with the focus", RingSides(window, RowRect(1), Colors::White) == 4);

    // Focused but no longer selected: the ring in the selection colour.
    list->GetSelection()->Clear();
    DisplayTest::Frame(window, {list});
    TEST("a focused row that is not selected is not drawn selected",
         !RowShows(window, RowRect(1), kSelection, Look::Area));
    TEST("on a plain row the ring takes the selection colour",
         RingSides(window, RowRect(1), kSelection) == 4);

    list->SetFocusRingColor(kMagenta);
    DisplayTest::Frame(window, {list});
    TEST("SetFocusRingColor picks the ring's colour", RingSides(window, RowRect(1), kMagenta) == 4);
    list->SetFocusRingColor(Colors::Transparent);
    DisplayTest::Frame(window, {list});
    TEST("Colors::Transparent goes back to the automatic colour",
         RingSides(window, RowRect(1), kMagenta) == 0 && RingSides(window, RowRect(1), kSelection) == 4);

    window->ClearFocus();
    window->RemoveChild(list);
}

// Two columns: the ring is drawn once across the row, so there is no edge of
// it where the columns meet.
void TestMultiColumn(UltraCanvasApplication& app, const std::shared_ptr<UltraCanvasWindow>& window) {
    auto model = std::make_shared<UltraCanvasMultiColumnListModel>();
    model->SetColumns({ListColumnDef("Name", 120), ListColumnDef("Size", 120)});
    model->SetItems({MultiColumnListItem({"a", "1"}), MultiColumnListItem({"b", "2"})});
    auto list = MakeList(window, model);

    if (!DisplayTest::FocusElement(app, window, list)) {
        Skip("the window could not be activated");
        window->RemoveChild(list);
        return;
    }
    list->OnEvent(Key(UCKeys::Down));
    list->OnEvent(Key(UCKeys::Down));
    list->SetFocusRingColor(kMagenta);
    DisplayTest::Frame(window, {list});

    TEST("a multi-column row carries the ring on all four sides",
         RingSides(window, RowRect(1), kMagenta) == 4);
    // Where the first column ends (x = 1 + 120): a ring per column would put
    // its right and left edges there, down the middle of the row.
    const Rect2Di columnSeam(118, 24, 6, 14);
    TEST("one ring across the row, not one per column",
         !RowShows(window, columnSeam, kMagenta));

    window->ClearFocus();
    DisplayTest::Frame(window, {list});
    TEST("the multi-column ring goes when the focus leaves",
         RingSides(window, RowRect(1), kMagenta) == 0);
    window->RemoveChild(list);
}

} // namespace

int main() {
    if (!std::getenv("DISPLAY")) {
        Skip("no DISPLAY");
    } else {
        UltraCanvasApplication app;
        if (!app.Initialize("ListViewFocusRingTest")) {
            Skip("the application would not initialise");
        } else {
            WindowConfig cfg;
            cfg.title = "ListViewFocusRingTest";
            cfg.width = 300;
            cfg.height = 200;
            auto window = CreateWindow(cfg);
            if (!window) {
                Skip("the window could not be created");
            } else {
                window->Show();
                TestSingleColumn(app, window);
                TestMultiColumn(app, window);
                window->Close();
            }
        }
    }
    std::cerr << testCount - failCount << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}

// Tests/TreeViewKeyboardTest.cpp
// The tree view takes the keyboard focus and the keys move its selection.
//
// - AcceptsFocus(): the tree is in the Tab order and a click focuses it.
// - Up / Down select the previous / next visible row, from the platform
//   independent virtualKey (the handler once compared nativeKeyCode against
//   Windows virtual-key numbers, which match nothing on Linux or macOS).
// - Left collapses an open parent, then goes to the parent; Right expands a
//   closed parent, then goes to its first child.
// - Home / End go to the first / last row; Page Up / Page Down move a page of
//   rows and clamp at the ends.
// - Enter toggles a parent and fires onNodeDoubleClicked; Space toggles the
//   check flag when the tree shows them.
// - A hidden root is never a row: Up from the first top-level row stays
//   there, and Left on it does nothing.
// - With no row focused yet, Down starts at the first row and Up at the last.
// - Under "jump to first entry" the keys step over open parents.
// - A handled key returns true from OnEvent, an unhandled one false.
// - With a DISPLAY, the focused row carries a dashed focus ring while the
//   tree holds the focus (white on the selection background), and none once
//   the focus leaves: read back from the composited window under Xvfb.
//
// Runs headless: keys are sent to the element as the application sends them
// to the focused element. The ring check opens a real window and skips
// itself without a DISPLAY.
// Version: 1.2.1 - RowShows / RingSides moved to DisplayTestSupport.h, for the list view's test
// Version: 1.2.0 - the ring is looked for along the row's edges, on all four
//                  sides: a white pixel anywhere in the row also matched the
//                  label, white on the selection, so the ring never seemed to
//                  go; ULTRACANVAS_TEST_DISPLAY_REQUIRED=1 makes a skip fail
// Version: 1.1.1 - the selection is looked for as an area of its colour, so the
//                  coloured fringes subpixel antialiasing leaves on black text
//                  no longer make the plain row look selected
// Version: 1.1.0 - the focus ring, on real pixels
// Version: 1.0.0
// Last Modified: 2026-11-15
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasTreeView.h"
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

namespace {

// A window with nothing native behind it; the focus moves in it as in a real
// one.
class FocusWindow : public UltraCanvasWindowBase {
public:
    FocusWindow() { SetBounds(Rect2Df(0, 0, 400, 300)); }
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
    void GetScreenSize(int& width, int& height) const override { width = 400; height = 300; }
    NativeWindowHandle GetNativeHandle() const override { return NativeWindowHandle{}; }
    void InvalidateWindowNative() override {}

protected:
    bool CreateNative() override { return true; }
    void DestroyNative() override {}
    void DoResizeNative() override {}
    bool RecreateNativeSurface() override { return true; }
};

UCEvent Key(UCKeys key, bool ctrl = false) {
    UCEvent event;
    event.type = UCEventType::KeyDown;
    event.virtualKey = key;
    event.ctrl = ctrl;
    return event;
}

TreeNodeData Node(const std::string& id) {
    TreeNodeData data;
    data.nodeId = id;
    data.text = id;
    return data;
}

// root
//   a
//     a1
//     a2
//   b
//     b1
//   c
struct Fixture {
    std::shared_ptr<FocusWindow> window;
    std::shared_ptr<UltraCanvasTreeView> tree;
    Fixture(int height = 300) {
        window = std::make_shared<FocusWindow>();
        tree = std::make_shared<UltraCanvasTreeView>("tree", 0, 0, 200, height);
        tree->SetRowHeight(20);
        window->AddChild(tree);
        tree->SetRootNode(Node("root"));
        tree->AddNode("root", Node("a"));
        tree->AddNode("a", Node("a1"));
        tree->AddNode("a", Node("a2"));
        tree->AddNode("root", Node("b"));
        tree->AddNode("b", Node("b1"));
        tree->AddNode("root", Node("c"));
        tree->ExpandAll();
    }
    std::string Selected() const {
        TreeNode* node = tree->GetFirstSelectedNode();
        return node ? node->data.nodeId : "";
    }
    std::string Focused() const {
        TreeNode* node = tree->GetFocusedNode();
        return node ? node->data.nodeId : "";
    }
    bool Send(UCKeys key, bool ctrl = false) { return tree->OnEvent(Key(key, ctrl)); }
    void ClickRow(int row) {
        UCEvent down;
        down.type = UCEventType::MouseDown;
        down.button = UCMouseButton::Left;
        down.pointer = Point2Di(100, row * 20 + 10);
        tree->OnEvent(down);
    }
};

void TestFocus() {
    Fixture f;
    TEST("the tree accepts the focus", f.tree->AcceptsFocus() && f.tree->CanReceiveFocus());
    f.window->FocusNextElement();
    TEST("Tab reaches the tree", f.window->GetFocusedElement() == f.tree.get());
    f.window->ClearFocus();
    f.ClickRow(1);
    TEST("a click on a row focuses the tree and the row",
         f.window->GetFocusedElement() == f.tree.get() && f.Selected() == "a" && f.Focused() == "a");
}

void TestUpDown() {
    Fixture f;
    f.tree->SelectNode(f.tree->FindNode("root"));
    TEST("Down with no focused row starts at the first row",
         f.Send(UCKeys::Down) && f.Selected() == "root" && f.Focused() == "root");
    TEST("Down selects the next row", f.Send(UCKeys::Down) && f.Selected() == "a");
    TEST("Down walks into an open parent", f.Send(UCKeys::Down) && f.Selected() == "a1");
    f.Send(UCKeys::Down);
    TEST("Down leaves the last child for the next parent",
         f.Send(UCKeys::Down) && f.Selected() == "b");
    TEST("Up selects the previous row", f.Send(UCKeys::Up) && f.Selected() == "a2");
    int selected = 0;
    f.tree->onNodeSelected = [&selected](TreeNode*) { ++selected; };
    f.Send(UCKeys::Up);
    TEST("a key selection fires onNodeSelected", selected == 1 && f.Selected() == "a1");
    TEST("an unhandled key returns false and leaves the row",
         !f.Send(UCKeys::F) && f.Selected() == "a1" && selected == 1);
}

void TestEnds() {
    Fixture f;
    TEST("Up with no focused row starts at the last row",
         f.Send(UCKeys::Up) && f.Selected() == "c");
    TEST("Up on the first row stays there",
         f.Send(UCKeys::Home) && f.Selected() == "root" && f.Send(UCKeys::Up) && f.Selected() == "root");
    TEST("End goes to the last visible row", f.Send(UCKeys::End) && f.Selected() == "c");
    TEST("Down on the last row stays there", f.Send(UCKeys::Down) && f.Selected() == "c");
    f.tree->CollapseNode(f.tree->FindNode("b"));
    f.Send(UCKeys::Home);
    f.Send(UCKeys::Down); f.Send(UCKeys::Down); f.Send(UCKeys::Down); f.Send(UCKeys::Down);
    TEST("a collapsed parent's children are not rows", f.Selected() == "b");
    TEST("after it comes the next row", f.Send(UCKeys::Down) && f.Selected() == "c");
}

void TestLeftRight() {
    Fixture f;
    f.ClickRow(2);   // a1
    TEST("Left on a leaf goes to the parent", f.Send(UCKeys::Left) && f.Selected() == "a");
    TEST("Left on an open parent collapses it, keeping the row",
         f.Send(UCKeys::Left) && !f.tree->FindNode("a")->IsExpanded() && f.Selected() == "a");
    TEST("Left on a closed parent goes to its parent", f.Send(UCKeys::Left) && f.Selected() == "root");
    f.Send(UCKeys::Down);
    TEST("Right on a closed parent expands it, keeping the row",
         f.Send(UCKeys::Right) && f.tree->FindNode("a")->IsExpanded() && f.Selected() == "a");
    TEST("Right on an open parent goes to its first child", f.Send(UCKeys::Right) && f.Selected() == "a1");
    TEST("Right on a leaf is taken and does nothing", f.Send(UCKeys::Right) && f.Selected() == "a1");
}

void TestHiddenRoot() {
    Fixture f;
    f.tree->SetRootVisible(false);
    TEST("Home with a hidden root goes to the first top-level row",
         f.Send(UCKeys::Home) && f.Selected() == "a");
    TEST("Up from the first top-level row never selects the hidden root",
         f.Send(UCKeys::Up) && f.Selected() == "a");
    f.tree->CollapseNode(f.tree->FindNode("a"));
    TEST("Left on a top-level row has no parent to go to", f.Send(UCKeys::Left) && f.Selected() == "a");
    f.tree->ExpandNode(f.tree->FindNode("a"));
    f.Send(UCKeys::Down);
    TEST("Left from a child reaches the top-level row", f.Send(UCKeys::Left) && f.Selected() == "a");
}

void TestPages() {
    // A page is what the rows are shown in - the content box - divided by the
    // row height, not the whole element: 80 px less the 1 px border top and
    // bottom shows three rows of 20, with 18 px over, so a page is three rows
    // and not the four the element's own height would suggest.
    Fixture f(80);
    f.Send(UCKeys::Home);
    TEST("Page Down moves a page of rows", f.Send(UCKeys::PageDown) && f.Selected() == "a2");
    TEST("Page Down clamps at the last row", f.Send(UCKeys::PageDown) && f.Selected() == "c"
                                            && f.Send(UCKeys::PageDown) && f.Selected() == "c");
    TEST("Page Up moves a page of rows back", f.Send(UCKeys::PageUp) && f.Selected() == "a2");
    TEST("Page Up clamps at the first row", f.Send(UCKeys::PageUp) && f.Selected() == "root"
                                            && f.Send(UCKeys::PageUp) && f.Selected() == "root");
}

void TestEnterSpace() {
    Fixture f;
    f.ClickRow(1);   // a
    TreeNode* activated = nullptr;
    f.tree->onNodeDoubleClicked = [&activated](TreeNode* node) { activated = node; };
    TEST("Enter collapses an open parent and fires onNodeDoubleClicked",
         f.Send(UCKeys::Enter) && !f.tree->FindNode("a")->IsExpanded() && activated == f.tree->FindNode("a"));
    TEST("Enter again expands it", f.Send(UCKeys::Enter) && f.tree->FindNode("a")->IsExpanded());
    f.tree->SetShowCheckboxes(true);
    TEST("Space toggles the check flag of the focused row",
         f.Send(UCKeys::Space) && f.tree->FindNode("a")->data.checkState == TreeCheckState::Checked
         && f.Selected() == "a");
    f.tree->SetShowCheckboxes(false);
    f.tree->SetSelectionMode(TreeSelectionMode::Multiple);
    f.Send(UCKeys::Down);
    f.tree->SelectNode(f.tree->FindNode("c"), true);
    TEST("Space without check flags selects the focused row alone",
         f.Send(UCKeys::Space) && f.tree->GetSelectedNodes().size() == 1 && f.Selected() == "a1");
}

// Rows: a, a1, a2, b, b1, c - every open parent is stepped over.
void TestJumpToFirstEntry() {
    Fixture f;
    f.tree->SetRootVisible(false);
    f.tree->SetShowFirstChildOnExpand(true);
    f.ClickRow(1);   // a1
    TEST("Up stays when every row above is an open parent",
         f.Send(UCKeys::Up) && f.Selected() == "a1");
    f.Send(UCKeys::Down);
    TEST("Down from the last child skips the next open parent to its first child",
         f.Send(UCKeys::Down) && f.Selected() == "b1");
    TEST("Up from a first child skips its parent to the last child before it",
         f.Send(UCKeys::Up) && f.Selected() == "a2");
    TEST("Home lands on the first row that is not stepped over",
         f.Send(UCKeys::Home) && f.Selected() == "a1");
    TEST("End lands on the last row", f.Send(UCKeys::End) && f.Selected() == "c");
    f.tree->ClearSelection();
    f.Send(UCKeys::Home);
    TEST("Down with no focused row skips an open parent too", f.Selected() == "a1");
}

using DisplayTest::Look;
using DisplayTest::RingSides;
using DisplayTest::RowShows;

// The focus ring on real pixels: a real window under Xvfb, activated by hand
// (DisplayTestSupport.h). Skips itself without a DISPLAY - unless
// ULTRACANVAS_TEST_DISPLAY_REQUIRED=1 (CI's virtual-display step), where a
// skip would pass for a run, so it fails instead.
void SkipRing(const char* why) {
    const char* required = std::getenv("ULTRACANVAS_TEST_DISPLAY_REQUIRED");
    if (required && std::string(required) == "1") {
        TEST(std::string("focus ring on screen: a display was required, and ") + why, false);
    } else {
        std::cerr << "SKIP: focus ring on screen (" << why << ")" << std::endl;
    }
}

void TestFocusRingOnScreen() {
    if (!std::getenv("DISPLAY")) {
        SkipRing("no DISPLAY");
        return;
    }
    UltraCanvasApplication app;
    if (!app.Initialize("TreeViewKeyboardTest")) {
        SkipRing("application would not initialise");
        return;
    }
    WindowConfig cfg;
    cfg.title = "TreeViewKeyboardTest";
    cfg.width = 300;
    cfg.height = 200;
    auto window = CreateWindow(cfg);
    if (!window) {
        SkipRing("window could not be created");
        return;
    }
    window->Show();

    auto tree = std::make_shared<UltraCanvasTreeView>("tree", 0, 0, 300, 200);
    tree->SetRowHeight(20);
    tree->SetSelectionColor(Color(0, 120, 215));
    window->AddChild(tree);
    tree->SetRootNode(Node("root"));
    tree->AddNode("root", Node("a"));
    tree->AddNode("root", Node("b"));
    tree->ExpandAll();

    if (!DisplayTest::FocusElement(app, window, tree)) {
        SkipRing("the window could not be activated");
        return;
    }
    tree->OnEvent(Key(UCKeys::Down));   // the first row: root
    tree->OnEvent(Key(UCKeys::Down));   // then a, the second row
    DisplayTest::Frame(window, {tree});

    // The second row's selection fill, inside the tree's border: the ring
    // runs one pixel in from its edges. The first row is plain.
    const Rect2Di focusedRow(2, 21, 296, 19);
    const Rect2Di otherRow(2, 1, 296, 18);
    TEST("the focused row is drawn selected", RowShows(window, focusedRow, Color(0, 120, 215), Look::Area));
    TEST("the focused row carries a white focus ring while the tree is focused",
         RingSides(window, focusedRow, Colors::White) == 4);
    TEST("an unfocused row is not drawn selected", !RowShows(window, otherRow, Color(0, 120, 215), Look::Area));

    window->ClearFocus();
    DisplayTest::Frame(window, {tree});
    TEST("the tree is not focused once the window's focus is cleared", !tree->IsFocused());
    TEST("the ring goes when the focus leaves, the selection stays",
         RingSides(window, focusedRow, Colors::White) == 0 &&
         RowShows(window, focusedRow, Color(0, 120, 215), Look::Area));

    tree->SetFocus(true);
    tree->SetFocusRingColor(Color(255, 0, 255));
    DisplayTest::Frame(window, {tree});
    TEST("SetFocusRingColor picks the ring's colour", RingSides(window, focusedRow, Color(255, 0, 255)) == 4);
    window->Close();
}

} // namespace

int main() {
    TestFocus();
    TestUpDown();
    TestEnds();
    TestLeftRight();
    TestHiddenRoot();
    TestPages();
    TestEnterSpace();
    TestJumpToFirstEntry();
    TestFocusRingOnScreen();
    std::cerr << testCount - failCount << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}

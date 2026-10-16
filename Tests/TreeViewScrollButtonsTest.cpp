// Tests/TreeViewScrollButtonsTest.cpp
// The tree view's two floating buttons in the bottom-right corner of its rows.
//
// - "Move to the top" appears once the view is scrolled down and always rests
//   in the corner: it no longer slides up as the view reaches the end of the
//   list.
// - "Go to the selected entry" sits directly above the top button's corner
//   slot, appears only while the selected row is not fully on screen, and a
//   click on it brings the row into view without changing the selection or
//   reaching the row underneath.
// - A selected row under a collapsed parent counts as out of view; the click
//   opens the parent first.
// - SetShowScrollToSelectionButton(false) switches the button off.
//
// Runs headless: no application, so the scrollbar jumps instead of gliding.
// Version: 1.0.0
// Last Modified: 2026-10-16
// Author: UltraCanvas Framework

#include "UltraCanvasTreeView.h"
#include "UltraCanvasWindow.h"

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

UCEvent Key(UCKeys key) {
    UCEvent event;
    event.type = UCEventType::KeyDown;
    event.virtualKey = key;
    return event;
}

TreeNodeData Node(const std::string& id) {
    TreeNodeData data;
    data.nodeId = id;
    data.text = id;
    return data;
}

// root, n0 .. n19, then p with one child p1 - 23 rows of 20 px in a tree
// 120 px high, so most of them are out of view.
struct Fixture {
    std::shared_ptr<FocusWindow> window;
    std::shared_ptr<UltraCanvasTreeView> tree;
    Fixture() {
        window = std::make_shared<FocusWindow>();
        tree = std::make_shared<UltraCanvasTreeView>("tree", 0, 0, 200, 120);
        tree->SetRowHeight(20);
        window->AddChild(tree);
        tree->SetRootNode(Node("root"));
        for (int i = 0; i < 20; ++i) tree->AddNode("root", Node("n" + std::to_string(i)));
        tree->AddNode("root", Node("p"));
        tree->AddNode("p", Node("p1"));
        tree->ExpandAll();
    }
    std::string Selected() const {
        TreeNode* node = tree->GetFirstSelectedNode();
        return node ? node->data.nodeId : "";
    }
    bool Press(const Rect2Di& r) {
        UCEvent down;
        down.type = UCEventType::MouseDown;
        down.button = UCMouseButton::Left;
        down.pointer = Point2Di(r.x + r.width / 2, r.y + r.height / 2);
        return tree->OnEvent(down);
    }
};

void TestTopButtonStaysInCorner() {
    Fixture f;
    TEST("no top button before the view is scrolled", !f.tree->IsScrollToTopButtonActive());

    f.tree->OnEvent(Key(UCKeys::End));   // selects p1 and scrolls to the end
    Rect2Di top = f.tree->GetScrollToTopButtonRect();
    Rect2Di content = f.tree->GetLocalContentRect();
    const TreeScrollToTopStyle& style = f.tree->GetScrollToTopButtonStyle();
    TEST("at the end of the list the top button shows", f.tree->IsScrollToTopButtonActive());
    TEST("at the end of the list it still rests in the corner",
         top.y + top.height == content.Bottom() - style.margin);
    TEST("the selected row is on screen, so no selection button",
         f.Selected() == "p1" && !f.tree->IsScrollToSelectionButtonActive());
}

void TestSelectionButton() {
    Fixture f;
    TEST("no selection button without a selection", !f.tree->IsScrollToSelectionButtonActive());

    f.tree->SelectNode(f.tree->FindNode("n1"));
    TEST("no selection button while the selected row is on screen",
         !f.tree->IsScrollToSelectionButtonActive());

    f.tree->OnEvent(Key(UCKeys::End));
    f.tree->ScrollToTop();
    TEST("the selection button shows once the selected row is scrolled away",
         f.Selected() == "p1" && f.tree->IsScrollToSelectionButtonActive());

    Rect2Di sel = f.tree->GetScrollToSelectionButtonRect();
    Rect2Di content = f.tree->GetLocalContentRect();
    const TreeScrollToTopStyle& style = f.tree->GetScrollToTopButtonStyle();
    int cornerTop = content.Bottom() - style.margin - style.size;
    TEST("it sits one button above the top button's corner slot",
         sel.width == style.size && sel.y + sel.height + style.margin == cornerTop);

    TEST("a click on it is taken by the button", f.Press(sel));
    TEST("it brings the selected row into view and keeps the selection",
         f.Selected() == "p1" && !f.tree->IsScrollToSelectionButtonActive()
         && f.tree->IsScrollToTopButtonActive());

    f.tree->SetShowScrollToSelectionButton(false);
    f.tree->ScrollToTop();
    TEST("SetShowScrollToSelectionButton(false) switches it off",
         !f.tree->IsScrollToSelectionButtonActive()
         && f.tree->GetScrollToSelectionButtonRect().width == 0);
}

void TestSelectionUnderCollapsedParent() {
    Fixture f;
    f.tree->SelectNode(f.tree->FindNode("p1"));
    f.tree->CollapseNode(f.tree->FindNode("p"));
    f.tree->ScrollToTop();
    TEST("a selected row under a collapsed parent counts as out of view",
         f.tree->IsScrollToSelectionButtonActive());
    f.Press(f.tree->GetScrollToSelectionButtonRect());
    TEST("the click opens the parent and shows the row",
         f.tree->FindNode("p")->IsExpanded() && f.Selected() == "p1"
         && !f.tree->IsScrollToSelectionButtonActive());
}

} // namespace

int main() {
    TestTopButtonStaysInCorner();
    TestSelectionButton();
    TestSelectionUnderCollapsedParent();
    std::cerr << testCount - failCount << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}

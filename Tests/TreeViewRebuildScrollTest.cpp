// Tests/TreeViewRebuildScrollTest.cpp
// UltraCanvasTreeView::BeginRebuild / EndRebuild (TreeRebuildScope): a tree
// built again from its source keeps the view where the user left it.
//
// A tree rebuilt row by row - SetRootNode and then an AddNode per row, which
// is what a folder rescan does - is empty for a moment, and the measure that
// follows used to clamp the scroll offset to 0: the rows came back with the
// view thrown to the first one, so the row the user had selected was off
// screen. Inside a rebuild scope:
//
// - the scroll position is the one the user left, row for row;
// - the row that was selected comes back at the same height, even when rows
//   were added above it;
// - a selection that moved on during the rebuild (another account, a folder
//   that is gone) is scrolled into view instead;
// - scopes nest: only the outermost one settles the view.
//
// Runs headless: no application, so nothing glides.
// Version: 1.0.0
// Last Modified: 2026-11-13
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

// A window with nothing native behind it.
class HeadlessWindow : public UltraCanvasWindowBase {
public:
    HeadlessWindow() { SetBounds(Rect2Df(0, 0, 400, 300)); }
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

TreeNodeData Node(const std::string& id) {
    TreeNodeData data;
    data.nodeId = id;
    data.text = id;
    return data;
}

// root plus 30 rows of 20 px in a tree 120 px high: six rows on screen at a
// time, the other twenty-five below it. `Fill` is the rebuild - every row
// replaced, as a folder rescan replaces them.
struct Fixture {
    static constexpr int kRowHeight = 20;
    static constexpr int kRows = 30;

    std::shared_ptr<HeadlessWindow> window;
    std::shared_ptr<UltraCanvasTreeView> tree;

    Fixture() {
        window = std::make_shared<HeadlessWindow>();
        tree = std::make_shared<UltraCanvasTreeView>("tree", 0, 0, 200, 120);
        tree->SetRowHeight(kRowHeight);
        window->AddChild(tree);
        Fill();
    }

    // `extraOnTop` rows before the usual ones: the folders another account
    // brought in while the tree was being rebuilt.
    void Fill(int extraOnTop = 0, int rows = kRows) {
        tree->SetRootNode(Node("root"));
        for (int i = 0; i < extraOnTop; ++i) tree->AddNode("root", Node("x" + std::to_string(i)));
        for (int i = 0; i < rows; ++i) tree->AddNode("root", Node("n" + std::to_string(i)));
        tree->ExpandAll();
    }

    std::string Selected() const {
        TreeNode* node = tree->GetFirstSelectedNode();
        return node ? node->data.nodeId : "";
    }

    // The row in screen slot `slot`, 0 being the first row on show.
    std::string RowOnScreen(int slot) const {
        const int y = static_cast<int>(tree->GetLocalContentRect().y) + slot * kRowHeight + 1;
        TreeNode* node = tree->GetNodeAtY(y);
        return node ? node->data.nodeId : "";
    }

    // Which screen slot `nodeId` sits in, or -1 when its row is not on screen.
    int SlotOf(const std::string& nodeId) const {
        for (int slot = 0; slot * kRowHeight < 120; ++slot)
            if (RowOnScreen(slot) == nodeId) return slot;
        return -1;
    }

    // Put the view where the user left it: n20 at the foot of the list, so
    // n15 is the first row on show.
    void ScrollDown() { tree->ScrollTo(tree->FindNode("n20")); }
};

void TestTheViewStaysWhereItWas() {
    Fixture f;
    f.ScrollDown();
    f.tree->SelectNode(f.tree->FindNode("n18"));
    TEST("the view starts scrolled down with the selected row on screen",
         f.RowOnScreen(0) == "n15" && f.SlotOf("n18") == 3);

    {
        TreeRebuildScope keepTheView(*f.tree);
        TEST("IsRebuilding() while the scope is open", f.tree->IsRebuilding());
        f.Fill();
        f.tree->SelectNode(f.tree->FindNode("n18"));   // the same folder, a new row
    }
    TEST("the rebuild is over with the scope", !f.tree->IsRebuilding());
    TEST("the rebuilt tree keeps the view it had",
         f.RowOnScreen(0) == "n15" && f.Selected() == "n18" && f.SlotOf("n18") == 3);
}

void TestWithoutTheScopeTheViewIsLost() {
    Fixture f;
    f.ScrollDown();
    f.tree->SelectNode(f.tree->FindNode("n18"));
    f.Fill();                                           // no scope: the old behaviour
    f.tree->SelectNode(f.tree->FindNode("n18"));
    TEST("a rebuild without the scope falls back to the first row",
         f.RowOnScreen(0) == "root" && f.SlotOf("n18") == -1);
}

void TestRowsAddedAboveTheSelection() {
    Fixture f;
    f.ScrollDown();
    f.tree->SelectNode(f.tree->FindNode("n18"));
    {
        TreeRebuildScope keepTheView(*f.tree);
        f.Fill(/*extraOnTop=*/3);                       // three folders above it now
        f.tree->SelectNode(f.tree->FindNode("n18"));
    }
    TEST("rows added above the selected one do not slide it",
         f.Selected() == "n18" && f.SlotOf("n18") == 3 && f.RowOnScreen(0) == "n15");
}

void TestTheSelectionThatMovedOnIsBroughtIntoView() {
    Fixture f;
    f.ScrollDown();
    f.tree->SelectNode(f.tree->FindNode("n18"));
    {
        TreeRebuildScope keepTheView(*f.tree);
        f.Fill(/*extraOnTop=*/0, /*rows=*/5);           // n18 is gone with its account
        f.tree->SelectNode(f.tree->FindNode("n1"));
    }
    TEST("the row selected by the end of the rebuild is on screen",
         f.Selected() == "n1" && f.SlotOf("n1") >= 0);
}

void TestNestedScopes() {
    Fixture f;
    f.ScrollDown();
    f.tree->SelectNode(f.tree->FindNode("n18"));
    {
        TreeRebuildScope outer(*f.tree);
        {
            TreeRebuildScope inner(*f.tree);
            f.Fill();
        }
        TEST("an inner scope does not end the rebuild", f.tree->IsRebuilding());
        f.tree->SelectNode(f.tree->FindNode("n18"));
    }
    TEST("the outermost scope is the one that puts the view back",
         !f.tree->IsRebuilding() && f.RowOnScreen(0) == "n15" && f.SlotOf("n18") == 3);
}

void TestEndBeforeTheBlockCloses() {
    Fixture f;
    f.ScrollDown();
    f.tree->SelectNode(f.tree->FindNode("n18"));
    {
        TreeRebuildScope keepTheView(*f.tree);
        f.Fill();
        f.tree->SelectNode(f.tree->FindNode("n18"));
        keepTheView.End();
        TEST("End() settles the view inside the block",
             !f.tree->IsRebuilding() && f.RowOnScreen(0) == "n15" && f.SlotOf("n18") == 3);
        f.tree->ScrollTo(f.tree->FindNode("n25"));
    }
    TEST("the destructor after End() leaves the view alone", f.SlotOf("n25") >= 0);
}

} // namespace

int main() {
    TestTheViewStaysWhereItWas();
    TestWithoutTheScopeTheViewIsLost();
    TestRowsAddedAboveTheSelection();
    TestTheSelectionThatMovedOnIsBroughtIntoView();
    TestNestedScopes();
    TestEndBeforeTheBlockCloses();
    std::cerr << testCount - failCount << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}

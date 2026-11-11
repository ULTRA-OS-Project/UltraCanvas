// Tests/TreeViewNodeDragTest.cpp
// Dragging a tree row onto another (UltraCanvasTreeView::SetNodeDragEnabled).
//
// onNodeDragDrop was declared in the header from the first version and
// never fired: nothing in the tree started a drag, so a program could not
// let its users rearrange a tree by dragging. What the test pins:
//
// - Off by default: a press and a move in a tree that does not reorder stay
//   a click, and nothing fires.
// - A press becomes a drag only after the pointer travels a few pixels.
// - While dragging, the row under the pointer is the drop target only when
//   it accepts the dragged row: never the row itself or one inside it, and
//   onNodeDragAccept decides when it is set.
// - Releasing over a target fires onNodeDragDrop(dragged, target) once,
//   after the drag has ended (so a handler may rebuild the tree); releasing
//   anywhere else, or Escape, drops nothing.
// - A pointer past the bottom edge scrolls the tree, so a row out of view
//   can be reached.
// - Removing the dragged row mid-drag ends the drag.
//
// Runs headless: events go to the element as the window dispatches them.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
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

TreeNodeData Node(const std::string& id) {
    TreeNodeData data;
    data.nodeId = id;
    data.text = id;
    return data;
}

UCEvent Mouse(UCEventType type, int x, int y) {
    UCEvent event;
    event.type = type;
    event.button = UCMouseButton::Left;
    event.pointer = Point2Di(x, y);
    event.pointerWindow = Point2Di(x, y);
    return event;
}

// Rows, 20 px each from y = 0:
//   0 root, 1 a, 2 a1, 3 a2, 4 b, 5 b1, 6 c
struct Fixture {
    std::shared_ptr<FocusWindow> window;
    std::shared_ptr<UltraCanvasTreeView> tree;
    int drops = 0;
    std::string dragged, target;
    explicit Fixture(int height = 300) {
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
        tree->onNodeDragDrop = [this](TreeNode* from, TreeNode* onto) {
            ++drops;
            dragged = from ? from->data.nodeId : "";
            target = onto ? onto->data.nodeId : "";
        };
    }
    static int RowY(int row) { return row * 20 + 10; }
    void Press(int row) { tree->OnEvent(Mouse(UCEventType::MouseDown, 100, RowY(row))); }
    void MoveTo(int y) { tree->OnEvent(Mouse(UCEventType::MouseMove, 100, y)); }
    void MoveToRow(int row) { MoveTo(RowY(row)); }
    bool Release(int y) { return tree->OnEvent(Mouse(UCEventType::MouseUp, 100, y)); }
    std::string DropTarget() const {
        TreeNode* node = tree->GetDropTargetNode();
        return node ? node->data.nodeId : "";
    }
};

void TestOffByDefault() {
    Fixture f;
    TEST("dragging rows is off by default", !f.tree->IsNodeDragEnabled());
    f.Press(1);
    f.MoveToRow(4);
    TEST("a press and a move are no drag in a tree that does not reorder",
         !f.tree->IsDraggingNode() && f.DropTarget().empty());
    f.Release(Fixture::RowY(4));
    TEST("and the release drops nothing", f.drops == 0);
}

void TestDragAndDrop() {
    Fixture f;
    f.tree->SetNodeDragEnabled(true);
    f.Press(1);
    TEST("the press selects the row as always",
         f.tree->GetFirstSelectedNode() && f.tree->GetFirstSelectedNode()->data.nodeId == "a");
    f.MoveTo(Fixture::RowY(1) + 3);
    TEST("a slip of a few pixels is not a drag yet", !f.tree->IsDraggingNode());
    f.MoveToRow(4);
    TEST("further, it is a drag of the row pressed",
         f.tree->IsDraggingNode() && f.tree->GetDraggedNode() &&
         f.tree->GetDraggedNode()->data.nodeId == "a");
    TEST("the row under the pointer is the drop target", f.DropTarget() == "b");
    TEST("the release over it is handled", f.Release(Fixture::RowY(4)));
    TEST("and fires onNodeDragDrop once, dragged then target",
         f.drops == 1 && f.dragged == "a" && f.target == "b");
    TEST("the drag is over afterwards",
         !f.tree->IsDraggingNode() && f.tree->GetDraggedNode() == nullptr && f.DropTarget().empty());

    // A click, not a drag: nothing dropped.
    f.Press(6);
    TEST("a click without a move is no drag", !f.Release(Fixture::RowY(6)) && f.drops == 1);
}

void TestNotIntoItself() {
    Fixture f;
    f.tree->SetNodeDragEnabled(true);
    f.Press(1);
    f.MoveToRow(2);
    TEST("a row inside the dragged one is no target", f.tree->IsDraggingNode() && f.DropTarget().empty());
    f.MoveToRow(1);
    TEST("nor is the dragged row itself", f.DropTarget().empty());
    f.Release(Fixture::RowY(1));
    TEST("a release where nothing accepts drops nothing", f.drops == 0 && !f.tree->IsDraggingNode());
}

void TestAccept() {
    Fixture f;
    f.tree->SetNodeDragEnabled(true);
    // Only rows without children take a drop.
    f.tree->onNodeDragAccept = [](TreeNode*, TreeNode* target) { return !target->HasChildren(); };
    f.Press(2);
    f.MoveToRow(4);
    TEST("onNodeDragAccept refuses a target", f.tree->IsDraggingNode() && f.DropTarget().empty());
    f.MoveToRow(6);
    TEST("and accepts another", f.DropTarget() == "c");
    f.MoveToRow(3);
    TEST("it decides alone once set: a sibling is a target", f.DropTarget() == "a2");
    f.Release(Fixture::RowY(3));
    TEST("the drop lands there", f.drops == 1 && f.dragged == "a1" && f.target == "a2");
}

void TestEscape() {
    Fixture f;
    f.tree->SetNodeDragEnabled(true);
    f.Press(1);
    f.MoveToRow(6);
    UCEvent escape;
    escape.type = UCEventType::KeyDown;
    escape.virtualKey = UCKeys::Escape;
    TEST("Escape is handled while dragging", f.tree->OnEvent(escape));
    TEST("and ends the drag", !f.tree->IsDraggingNode() && f.DropTarget().empty());
    f.Release(Fixture::RowY(6));
    TEST("so the release drops nothing", f.drops == 0);
}

void TestScrollPastEdge() {
    // Three rows show; seven are there.
    Fixture f(60);
    f.tree->SetNodeDragEnabled(true);
    f.Press(1);
    f.MoveToRow(2);
    TEST("dragging in a short tree", f.tree->IsDraggingNode());
    for (int i = 0; i < 6; ++i) f.MoveTo(70);
    TEST("a pointer past the bottom scrolls to the last row", f.DropTarget() == "c");
    f.Release(70);
    TEST("which takes the drop", f.drops == 1 && f.target == "c");
}

void TestRemovedMidDrag() {
    Fixture f;
    f.tree->SetNodeDragEnabled(true);
    f.Press(1);
    f.MoveToRow(6);
    f.tree->RemoveNode("a");
    TEST("removing the dragged row ends the drag", !f.tree->IsDraggingNode() && f.DropTarget().empty());
    f.Release(Fixture::RowY(4));
    TEST("and nothing is dropped", f.drops == 0);

    // A handler that rebuilds the tree on the drop.
    Fixture g;
    g.tree->SetNodeDragEnabled(true);
    g.tree->onNodeDragDrop = [&g](TreeNode*, TreeNode*) {
        ++g.drops;
        g.tree->SetRootNode(Node("fresh"));
    };
    g.Press(1);
    g.MoveToRow(6);
    g.Release(Fixture::RowY(6));
    TEST("a handler may rebuild the tree", g.drops == 1 && !g.tree->IsDraggingNode());
}

}  // namespace

int main() {
    TestOffByDefault();
    TestDragAndDrop();
    TestNotIntoItself();
    TestAccept();
    TestEscape();
    TestScrollPastEdge();
    TestRemovedMidDrag();
    std::cerr << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}

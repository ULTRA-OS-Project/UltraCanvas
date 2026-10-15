// Tests/TreeViewFilterTest.cpp
// UltraCanvasTreeView::SetFilterText - the tree narrowed to the rows matching
// a text, and put back when the filter ends.
//
// - Only the matching rows are on show, with the rows above them, and those
//   are expanded so every match is visible; GetFilterMatchCount counts them.
// - The match ignores case, in any script ("ÄRGER" is found by "ärg").
// - A heading that matches keeps its entries on show (expand it to see them)
//   and stays as it was; its entries are not counted unless they match too.
// - The root is the frame: its own text never matches, so it does not keep
//   every row on show.
// - Nothing matching leaves only the root.
// - An empty text ends the filter: every row comes back and every node takes
//   back the expanded state it had before the filter began - except the way
//   to the selected row, which is opened so it stays in view.
// - A row the application hid (TreeNodeData::visible) neither matches nor
//   comes back when the filter ends.
// - With a hidden root the top level is filtered the same, and the root's
//   own state is never touched.
// - Setting the same text again re-applies it to rows added since.
// - FirstShownChild skips the children a filter hid, and so does the tree's
//   "jump to first entry" when a click expands a heading under a filter.
//
// Runs headless.
// Version: 1.0.0
// Last Modified: 2026-10-15
// Author: UltraCanvas Framework

#include "UltraCanvasTreeView.h"
#include "UltraCanvasWindow.h"

#include <functional>
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

// A window with nothing native behind it.
class TestWindow : public UltraCanvasWindowBase {
public:
    TestWindow() { SetBounds(Rect2Df(0, 0, 400, 600)); }
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
    void GetScreenSize(int& width, int& height) const override { width = 400; height = 600; }
    NativeWindowHandle GetNativeHandle() const override { return NativeWindowHandle{}; }
    void InvalidateWindowNative() override {}
protected:
    bool CreateNative() override { return true; }
    void DestroyNative() override {}
    void DoResizeNative() override {}
    bool RecreateNativeSurface() override { return true; }
};

TreeNodeData Node(const std::string& id, const std::string& text) {
    TreeNodeData data;
    data.nodeId = id;
    data.text = text;
    return data;
}

// root  "UltraCanvas Components"
//   basic   "Basic UI"          (expanded before the filter)
//     button  "Button"
//     input   "Text Input"
//     check   "Checkbox"
//   charts  "Charts"
//     bar     "Bar Chart"
//     line    "Line Chart"
//   ext     "Extended"
//     tree    "Tree View"
//     list    "List View"
//     anger   "ÄRGER"
struct Fixture {
    std::shared_ptr<TestWindow> window;
    std::shared_ptr<UltraCanvasTreeView> tree;
    explicit Fixture(bool rootVisible = true) {
        window = std::make_shared<TestWindow>();
        tree = std::make_shared<UltraCanvasTreeView>("tree", 0, 0, 200, 500);
        tree->SetRowHeight(20);
        window->AddChild(tree);
        tree->SetRootNode(Node("root", "UltraCanvas Components"));
        tree->AddNode("root", Node("basic", "Basic UI"));
        tree->AddNode("basic", Node("button", "Button"));
        tree->AddNode("basic", Node("input", "Text Input"));
        tree->AddNode("basic", Node("check", "Checkbox"));
        tree->AddNode("root", Node("charts", "Charts"));
        tree->AddNode("charts", Node("bar", "Bar Chart"));
        tree->AddNode("charts", Node("line", "Line Chart"));
        tree->AddNode("root", Node("ext", "Extended"));
        tree->AddNode("ext", Node("tree", "Tree View"));
        tree->AddNode("ext", Node("list", "List View"));
        tree->AddNode("ext", Node("anger", "ÄRGER"));
        tree->SetRootVisible(rootVisible);
        tree->CollapseAll();
        if (rootVisible) tree->ExpandNode(tree->FindNode("root"));
        tree->ExpandNode(tree->FindNode("basic"));
    }
    TreeNode* N(const std::string& id) const { return tree->FindNode(id); }
    // The rows on show, top to bottom, as "id id id" - walked the way the
    // tree draws them: a row on show, then its children while it is open.
    std::string Rows() const {
        std::string rows;
        std::function<void(TreeNode*)> walk = [&](TreeNode* node) {
            if (!node->IsShown()) return;
            if (!rows.empty()) rows += ' ';
            rows += node->data.nodeId;
            if (!node->IsExpanded()) return;
            for (auto& child : node->children) walk(child.get());
        };
        TreeNode* root = tree->GetRootNode();
        if (tree->IsRootVisible()) {
            walk(root);
        } else {
            for (auto& child : root->children) walk(child.get());
        }
        return rows;
    }
};

void TestNarrowing() {
    Fixture f;
    TEST("before the filter: root, the open heading and its rows",
         f.Rows() == "root basic button input check charts ext");

    f.tree->SetFilterText("view");
    TEST("a filter shows the matches and the rows above them, expanded",
         f.Rows() == "root ext tree list");
    TEST("it counts the matching rows", f.tree->GetFilterMatchCount() == 2);
    TEST("IsFiltered / GetFilterText report it",
         f.tree->IsFiltered() && f.tree->GetFilterText() == "view");
    TEST("a heading with nothing matching is hidden, its state as it was",
         !f.N("basic")->IsShown() && f.N("basic")->IsExpanded() && !f.N("charts")->IsExpanded());

    f.tree->SetFilterText("BUTTON");
    TEST("the match ignores case", f.Rows() == "root basic button");

    f.tree->SetFilterText("ärg");
    TEST("and ignores case beyond ASCII", f.Rows() == "root ext anger");

    f.tree->SetFilterText("ultra");
    TEST("the root's own text matches nothing", f.Rows() == "root" && f.tree->GetFilterMatchCount() == 0);

    f.tree->SetFilterText("zzz");
    TEST("nothing matching leaves only the root",
         f.Rows() == "root" && f.tree->GetFilterMatchCount() == 0);
}

void TestHeadingMatch() {
    Fixture f;
    f.tree->SetFilterText("charts");
    TEST("a matching heading is shown as it was (closed)", f.Rows() == "root charts");
    TEST("its entries are on show once it is expanded",
         f.N("bar")->IsShown() && f.N("line")->IsShown());
    TEST("but not counted", f.tree->GetFilterMatchCount() == 1);

    f.tree->SetFilterText("chart");
    TEST("a heading whose entries match too is expanded to them",
         f.Rows() == "root charts bar line" && f.tree->GetFilterMatchCount() == 3);
}

void TestEnd() {
    Fixture f;
    f.tree->SetFilterText("line");
    TEST("Charts opens for its match", f.N("charts")->IsExpanded() && !f.N("ext")->IsExpanded());
    f.tree->ClearFilter();
    TEST("ending the filter puts back every row and the expanded state",
         f.Rows() == "root basic button input check charts ext");
    TEST("no row stays hidden by the filter",
         f.N("bar")->IsShown() && f.N("tree")->IsShown() && !f.tree->IsFiltered() &&
         f.tree->GetFilterMatchCount() == 0);

    f.tree->SetFilterText("list");
    f.tree->SelectNode(f.N("list"));
    f.tree->SetFilterText("");
    TEST("the row selected while filtering stays in view",
         f.Rows() == "root basic button input check charts ext tree list anger");
}

void TestApplicationHidden() {
    Fixture f;
    f.N("check")->data.visible = false;
    f.tree->SetFilterText("check");
    TEST("a row the application hid does not match",
         f.Rows() == "root" && f.tree->GetFilterMatchCount() == 0);
    f.tree->ClearFilter();
    TEST("and stays hidden when the filter ends",
         f.Rows() == "root basic button input charts ext" && !f.N("check")->IsShown());
}

void TestHiddenRoot() {
    Fixture f(false);
    TEST("a hidden root: the top level is the first rows", f.Rows() == "basic button input check charts ext");
    const bool rootWasExpanded = f.N("root")->IsExpanded();
    f.tree->SetFilterText("bar");
    TEST("the top level is filtered the same", f.Rows() == "charts bar");
    TEST("the hidden root's state is left alone", f.N("root")->IsExpanded() == rootWasExpanded);
    f.tree->ClearFilter();
    TEST("and ends the same", f.Rows() == "basic button input check charts ext" &&
                              f.N("root")->IsExpanded() == rootWasExpanded);
}

void TestReapply() {
    Fixture f;
    f.tree->SetFilterText("view");
    f.tree->AddNode("ext", Node("grid", "Grid View"));
    f.tree->AddNode("ext", Node("table", "Table"));
    f.tree->SetFilterText("view");
    TEST("the same text again filters the rows added since",
         f.Rows() == "root ext tree list grid" && f.tree->GetFilterMatchCount() == 3);
}

void TestFirstShownChild() {
    Fixture f;
    f.tree->SetFilterText("line");
    TEST("FirstShownChild skips a child the filter hid",
         f.N("charts")->FirstShownChild() == f.N("line") && f.N("charts")->FirstChild() == f.N("bar"));

    // "Jump to first entry": a click that expands a closed heading selects
    // its first child - the first one on show.
    Fixture g;
    g.tree->SetShowFirstChildOnExpand(true);
    g.tree->SetAutoExpandSelectedNode(true);
    g.tree->SetFilterText("Line");
    g.tree->CollapseNode(g.N("charts"));
    TEST("under the filter Charts is the second row", g.Rows() == "root charts");
    UCEvent down;
    down.type = UCEventType::MouseDown;
    down.button = UCMouseButton::Left;
    down.pointer = Point2Di(100, 1 * 20 + 10);
    g.tree->OnEvent(down);
    TreeNode* selected = g.tree->GetFirstSelectedNode();
    TEST("expanding a heading under a filter jumps to its first row on show",
         selected == g.N("line"));
}

} // namespace

int main() {
    TestNarrowing();
    TestHeadingMatch();
    TestEnd();
    TestApplicationHidden();
    TestHiddenRoot();
    TestReapply();
    TestFirstShownChild();
    std::cerr << testCount - failCount << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}

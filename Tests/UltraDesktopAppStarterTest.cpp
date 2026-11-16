// Tests/UltraDesktopAppStarterTest.cpp
// UltraDesktop's Apps window keeps the user's place when its page is built
// again - which it is on every keystroke in the filter box, on every new
// list of applications, and after every application moved to another
// category.
//
// The window used to make a fresh tree element and a fresh scrolling area
// each time, so the category the user had chosen, the categories they had
// opened and the place they had scrolled to went with the elements that were
// thrown away. Nothing could catch that: the window read the machine's own
// .desktop files and opened a window of its own, with no way in for a
// fixture. Hooks::applications is that way in - a list handed to the window,
// with nothing read from disk - and UltraDesktopAppStarter::Window() lets a
// test look the page's elements up by their identifiers.
//
// Opens a real window, so it runs under Xvfb (xvfb-run -a) and skips itself
// without a DISPLAY - unless ULTRACANVAS_TEST_DISPLAY_REQUIRED=1, where a
// skip would pass for a run.
// Version: 1.0.0
// Last Modified: 2026-11-15
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasWindow.h"
#include "UltraDesktopAppStarter.h"
#include "DisplayTestSupport.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraDesktop;

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
        if (UltraCanvas::DisplayTest::DisplayRequired()) {                    \
            std::cerr << "FAIL: a display was required, and " << reason       \
                      << std::endl;                                           \
            return 1;                                                         \
        }                                                                     \
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

namespace {

UCDesktopEntry Entry(const std::string& name, std::vector<std::string> categories) {
    UCDesktopEntry entry;
    entry.kind = UCDesktopEntry::Kind::Application;
    entry.name = name;
    entry.categories = std::move(categories);
    entry.program = "/usr/bin/" + name;
    entry.exec = entry.program;
    return entry;
}

// Enough applications, over enough categories and sub-categories, that both
// views are taller than the window: with every category open the tree has
// rows below its foot, and the tiles scroll. Two applications per
// sub-category, and splitAbove = 2 below, so every category really splits
// into the sections that make those rows.
AppList Applications() {
    auto apps = std::make_shared<std::vector<UCDesktopEntry>>();
    const std::vector<std::pair<std::string, std::vector<std::string>>> kinds = {
        {"Net",    {"Network", "WebBrowser"}},
        {"Mail",   {"Network", "Email"}},
        {"Chat",   {"Network", "InstantMessaging"}},
        {"Play",   {"AudioVideo", "Player"}},
        {"Rec",    {"AudioVideo", "Recorder"}},
        {"Tune",   {"AudioVideo", "Music"}},
        {"Paint",  {"Graphics", "RasterGraphics"}},
        {"Draw",   {"Graphics", "VectorGraphics"}},
        {"Photo",  {"Graphics", "Photography"}},
        {"Ide",    {"Development", "IDE"}},
        {"Debug",  {"Development", "Debugger"}},
        {"Write",  {"Office", "WordProcessor"}},
        {"Sheet",  {"Office", "Spreadsheet"}},
        {"Slides", {"Office", "Presentation"}},
        {"Arcade", {"Game", "ArcadeGame"}},
        {"Board",  {"Game", "BoardGame"}},
        {"Term",   {"Utility", "TerminalEmulator"}},
        {"Zip",    {"Utility", "Archiving"}},
    };
    for (const auto& kind : kinds) {
        for (int i = 1; i <= 2; ++i) {
            apps->push_back(Entry(kind.first + " " + std::to_string(i), kind.second));
        }
    }
    return apps;
}

UltraDesktopAppStarter::Hooks HooksFor(const std::string& view) {
    UltraDesktopAppStarter::Hooks hooks;
    hooks.view = view;
    hooks.keyOf = [](const UCDesktopEntry& entry) { return entry.program; };
    hooks.splitAbove = 2;   // every category splits into its sub-categories
    hooks.applications = []() { return Applications(); };
    return hooks;
}

UltraCanvasTreeView* TreeOf(UltraDesktopAppStarter& starter) {
    if (!starter.Window()) return nullptr;
    return dynamic_cast<UltraCanvasTreeView*>(starter.Window()->FindChildById("appsTree"));
}

UltraCanvasContainer* TilesOf(UltraDesktopAppStarter& starter) {
    if (!starter.Window()) return nullptr;
    return dynamic_cast<UltraCanvasContainer*>(starter.Window()->FindChildById("appsTiles"));
}

std::string SelectedRow(UltraCanvasTreeView* tree) {
    TreeNode* node = tree ? tree->GetFirstSelectedNode() : nullptr;
    return node ? node->data.nodeId : "";
}

// Where a row sits on screen: the topmost element-local y it covers, or -1
// when it is nowhere on screen. Scanned, because the scroll position is not
// a whole number of rows.
int ScreenYOf(UltraCanvasTreeView* tree, const std::string& nodeId) {
    const Rect2Di content = tree->GetLocalContentRect();
    for (int y = content.y; y < content.Bottom(); ++y) {
        TreeNode* node = tree->GetNodeAtY(y);
        if (node && node->data.nodeId == nodeId) return y;
    }
    return -1;
}

void Frames(const std::shared_ptr<UltraCanvasWindow>& window, int count = 3) {
    for (int frame = 0; frame < count; ++frame) window->UpdateAndRender();
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   UltraDesktop Apps window suite"       << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("UltraDesktopAppStarterTest")) SKIP_ALL("application would not initialise");

    // ----- the tree view -----
    {
        UltraDesktopAppStarter starter(HooksFor("tree"));
        starter.Open();
        if (!starter.Window()) SKIP_ALL("window could not be created");
        starter.ShowApps(Applications());
        Frames(starter.Window());

        UltraCanvasTreeView* tree = TreeOf(starter);
        TEST("the Apps window shows a category tree", tree != nullptr);
        if (!tree) return failCount == 0 ? 0 : 1;

        TEST("a category is chosen to begin with", !SelectedRow(tree).empty());

        // What the user does: open the categories, pick one, scroll down.
        // Through ExpandNode, as a click on the expander does, so the window
        // hears about it - ExpandAll would open the rows behind its back.
        const auto rows = tree->GetRootNode() ? tree->GetRootNode()->children.size() : 0u;
        TEST("every category has a row", rows >= 6);
        for (auto& row : tree->GetRootNode()->children) {
            if (row->HasChildren()) tree->ExpandNode(row.get());
        }
        // A row in the middle is the one to watch, and the view is scrolled
        // to the very last row - the last section of the last category, not
        // just its heading, or the tree would move by less than a row.
        TreeNode* middle = tree->GetRootNode()->children[rows / 2].get();
        const std::string chosen = middle->data.nodeId;
        tree->SelectNode(middle);
        TreeNode* last = tree->GetRootNode()->children[rows - 1].get();
        if (!last->children.empty()) last = last->children.back().get();
        tree->ScrollTo(last);
        Frames(starter.Window());
        const int whereBefore = ScreenYOf(tree, chosen);
        const bool openBefore = middle->IsExpanded();
        TEST("the view is scrolled away from the first row",
             ScreenYOf(tree, tree->GetRootNode()->children[0]->data.nodeId) < 0);
        TEST("and the category being watched is on screen", whereBefore >= 0);

        // A new list of applications: the page is built again.
        starter.ShowApps(Applications());
        Frames(starter.Window());

        UltraCanvasTreeView* after = TreeOf(starter);
        TEST("the tree itself is kept, not made again", after == tree);
        TEST("the category the user chose is still chosen", SelectedRow(after) == chosen);
        TreeNode* chosenNow = after->FindNode(chosen);
        TEST("the category they opened is still open",
             !openBefore || (chosenNow && chosenNow->IsExpanded()));
        TEST("and the view is where they left it, to the pixel",
             ScreenYOf(after, chosen) == whereBefore);
    }

    // ----- the tiles -----
    {
        UltraDesktopAppStarter starter(HooksFor("tiles"));
        starter.Open();
        if (!starter.Window()) SKIP_ALL("window could not be created");
        starter.ShowApps(Applications());
        Frames(starter.Window());

        UltraCanvasContainer* tiles = TilesOf(starter);
        TEST("the Apps window shows the tiles", tiles != nullptr);
        if (!tiles) return failCount == 0 ? 0 : 1;

        tiles->ScrollToVertical(200);
        Frames(starter.Window());
        const int wasAt = tiles->GetVerticalScrollPosition();
        TEST("the tiles scroll", wasAt > 0);

        starter.ShowApps(Applications());
        Frames(starter.Window());

        UltraCanvasContainer* tilesAfter = TilesOf(starter);
        TEST("the scrolling area is kept, not made again", tilesAfter == tiles);
        TEST("and the tiles are still where the user scrolled to",
             tilesAfter && tilesAfter->GetVerticalScrollPosition() == wasAt);
    }

    std::cerr << std::endl << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}

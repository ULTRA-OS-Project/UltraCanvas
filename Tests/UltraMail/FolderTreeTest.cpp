// Tests/UltraMail/FolderTreeTest.cpp
// The folder tree on the left of the mail view: its right-click menu (Add
// folder, Delete folder) and Settings > Display > Treeview (the account on
// screen only, or every account).
//
// The menu of a folder adds inside it and deletes it - greyed, with the
// reason, for the inbox, a folder with a role (Sent, Trash...) or one with
// folders below it; the menu of the account row or the inbox adds at the top
// of the account. With the current account only, the tree lists that one and
// follows the account bar to the next.
//
// Builds the mail view on an in-memory store in a real window, so it runs
// under Xvfb (xvfb-run -a) and skips itself without a DISPLAY.
// Version: 1.0.0
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasWindow.h"
#include "UltraMailMailView.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace UltraCanvas;
using namespace UltraMail;

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

Folder MakeFolder(const std::string& account, const std::string& name,
                  FolderRole role = FolderRole::Normal) {
    Folder f;
    f.accountId = account;
    f.name = name;
    f.role = role;
    f.delimiter = "/";
    return f;
}

const MenuItemData* Find(const std::vector<MenuItemData>& items, const std::string& label) {
    for (const auto& item : items)
        if (item.label == label) return &item;
    return nullptr;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   UltraMail Folder Tree Suite"          << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("FolderTreeTest")) SKIP_ALL("application would not initialise");

    LocalStore store;
    if (!store.Open("folder-tree-test", ":memory:").success) {
        std::cerr << "FAIL: the store would not open" << std::endl;
        return 1;
    }
    std::vector<Account> accounts(2);
    accounts[0].accountId = "erika";
    accounts[0].email = "erika@example.com";
    accounts[0].shortName = "erika";
    accounts[1].accountId = "work";
    accounts[1].email = "erika@work.example";
    accounts[1].shortName = "work";
    for (const auto& a : accounts) store.UpsertAccount(a);
    store.UpsertFolder(MakeFolder("erika", "INBOX", FolderRole::Inbox));
    store.UpsertFolder(MakeFolder("erika", "Sent", FolderRole::Sent));
    store.UpsertFolder(MakeFolder("erika", "Projects"));
    store.UpsertFolder(MakeFolder("erika", "Projects/2026"));
    store.UpsertFolder(MakeFolder("work", "INBOX", FolderRole::Inbox));
    store.UpsertFolder(MakeFolder("work", "Clients"));

    WindowConfig cfg;
    cfg.title = "FolderTreeTest";
    cfg.width = 1000;
    cfg.height = 600;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();

    auto page = CreateContainer("page", 0, 0, 1000, 600);
    page->layout.SetFlexColumn();
    window->AddChild(page);

    MailView view;
    view.SetStore(&store);
    auto root = view.Build();
    page->AddChild(root);
    root->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    view.SetAccounts(accounts);
    view.ShowAccount("erika");
    auto frames = [&]() {
        for (int frame = 0; frame < 3; ++frame) {
            page->RequestRedraw();
            window->UpdateAndRender();
        }
    };
    frames();

    auto* tree = dynamic_cast<UltraCanvasTreeView*>(root->FindChildById("folderTree"));
    TEST("the mail view has its folder tree", tree != nullptr);
    if (!tree) return 1;
    TEST("a right-click on a row opens a menu", static_cast<bool>(tree->onNodeRightClicked));

    // ----- Settings > Display > Treeview -----
    TEST("every account is listed by default",
         tree->FindNode("acct::erika") && tree->FindNode("acct::work"));
    view.SetTreeCurrentAccountOnly(true);
    TEST("current account only: the one on screen is listed",
         tree->FindNode("acct::erika") && tree->FindNode("f::erika::Projects"));
    TEST("current account only: the other is not",
         !tree->FindNode("acct::work") && !tree->FindNode("f::work::Clients"));
    view.ShowAccount("work");   // another tile clicked in the account bar
    TEST("the tree follows the account bar to the next account",
         tree->FindNode("acct::work") && tree->FindNode("f::work::Clients") &&
         !tree->FindNode("acct::erika"));
    view.SetTreeCurrentAccountOnly(false);
    TEST("all accounts again",
         tree->FindNode("acct::erika") && tree->FindNode("acct::work"));
    view.ShowAccount("erika");
    frames();

    // ----- the right-click menu -----
    std::vector<std::pair<std::string, std::string>> added, deleted;
    view.onAddFolder = [&](const std::string& acc, const std::string& parent) {
        added.emplace_back(acc, parent);
    };
    view.onDeleteFolder = [&](const std::string& acc, const std::string& folder) {
        deleted.emplace_back(acc, folder);
    };

    // A folder: Add folder inside it, Delete folder it.
    auto items = view.FolderMenuItems("f::erika::Projects/2026");
    const MenuItemData* add = Find(items, "Add folder…");
    const MenuItemData* remove = Find(items, "Delete folder…");
    TEST("a folder's menu has Add folder and Delete folder", add && remove);
    TEST("the menu names the folder",
         !items.empty() && items.front().type == MenuItemType::Header &&
         items.front().label == "Projects / 2026");
    TEST("a folder with nothing below it can be deleted", remove && remove->enabled);
    if (add && add->onClick) add->onClick();
    if (remove && remove->onClick) remove->onClick();
    TEST("Add folder adds inside the folder",
         added.size() == 1 && added[0] == std::make_pair(std::string("erika"),
                                                         std::string("Projects/2026")));
    TEST("Delete folder names the folder",
         deleted.size() == 1 && deleted[0] == std::make_pair(std::string("erika"),
                                                             std::string("Projects/2026")));

    // Not deletable: a folder with folders below it, one with a role, the inbox.
    items = view.FolderMenuItems("f::erika::Projects");
    remove = Find(items, "Delete folder…");
    TEST("a folder with folders below it cannot be deleted, and says why",
         remove && !remove->enabled && !remove->tooltip.empty());
    items = view.FolderMenuItems("f::erika::Sent");
    remove = Find(items, "Delete folder…");
    TEST("the Sent folder cannot be deleted", remove && !remove->enabled);
    items = view.FolderMenuItems("f::erika::INBOX");
    remove = Find(items, "Delete folder…");
    add = Find(items, "Add folder…");
    TEST("the inbox cannot be deleted", remove && !remove->enabled);
    added.clear();
    if (add && add->onClick) add->onClick();
    TEST("Add folder on the inbox adds at the top of the account",
         added.size() == 1 && added[0].first == "erika" && added[0].second.empty());

    // The account row: its address as the title, Add at the top.
    items = view.FolderMenuItems("acct::work");
    add = Find(items, "Add folder…");
    TEST("the account row's menu names the account",
         !items.empty() && items.front().label == "erika@work.example");
    added.clear();
    if (add && add->onClick) add->onClick();
    TEST("Add folder on the account row adds at the top of that account",
         added.size() == 1 && added[0].first == "work" && added[0].second.empty());
    TEST("a row that is no folder has no menu", view.FolderMenuItems("nothing").empty());

    std::cerr << std::endl << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}

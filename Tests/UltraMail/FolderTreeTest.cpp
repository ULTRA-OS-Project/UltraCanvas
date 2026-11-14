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
// The account bar's counters: the inbox under New today or Waiting for reply
// shows exactly the messages the counter counts, under Unread all unread mail
// (the counter counts the part from before today), and the
// filter is reported (onFilterChanged) until a folder switch clears it.
//
// A tree of several accounts is taller than its pane, and a folder rescan -
// which follows every mail check - rebuilds every row of it. The view stays
// where the user left it: the folder being read keeps its place on screen
// instead of the tree falling back to its first row.
//
// Builds the mail view on an in-memory store in a real window, so it runs
// under Xvfb (xvfb-run -a) and skips itself without a DISPLAY.
// Version: 1.3.0 - a folder closed by hand stays closed across a rescan
// Version: 1.2.0 - a rebuilt tree keeps the view, and a folder opened from
//                  outside the tree is scrolled into it
// Version: 1.1.0 - the account bar's counters as filters (ShowInbox, onFilterChanged)
// Version: 1.0.1 - UltraMail::Folder qualified (Windows' shldisp.h has a Folder)
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

// Qualified: on Windows the shell's headers, which the window headers pull
// in, declare a COM interface named Folder in the global namespace.
UltraMail::Folder MakeFolder(const std::string& account, const std::string& name,
                             FolderRole role = FolderRole::Normal) {
    UltraMail::Folder f;
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

// Where the row of `nodeId` is on screen: the topmost element-local y it
// covers, or -1 when it is not on screen at all. Scanned rather than
// calculated, because the scroll position is not a whole number of rows.
int ScreenYOf(UltraCanvasTreeView* tree, const std::string& nodeId) {
    const Rect2Df content = tree->GetLocalContentRect();
    const int bottom = static_cast<int>(content.y + content.height);
    for (int y = static_cast<int>(content.y); y < bottom; ++y) {
        TreeNode* node = tree->GetNodeAtY(y);
        if (node && node->data.nodeId == nodeId) return y;
    }
    return -1;
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

    // ----- the account bar's counters as filters -----
    // erika's inbox: 2 unread from today, 3 unread from before, 1 read from
    // today, and 1 read from before that waits for a reply.
    const int64_t midnight = LocalStore::StartOfToday();
    int64_t uid = 100;
    auto put = [&](int64_t date, bool read, bool waiting) {
        MessageEnvelope m;
        m.accountId = "erika";
        m.folder = "INBOX";
        m.uid = ++uid;
        m.subject = "Message " + std::to_string(uid);
        m.fromAddr = "anna@friends.example";
        m.to = {"erika@example.com"};
        m.date = date;
        m.flags = read ? Flag_Seen : Flag_None;
        store.UpsertMessage(m);
        store.SetNeedsAnswer("erika", "INBOX", m.uid, waiting);
    };
    put(midnight + 60, false, false);
    put(midnight + 120, false, false);
    put(midnight - 3600, false, false);
    put(midnight - 86400, false, false);
    put(midnight - 2 * 86400, false, false);
    put(midnight + 180, true, false);
    put(midnight - 3 * 86400, true, true);
    std::vector<AccountStatus> counts;
    store.GetAccountStatus(counts);
    AccountStatus erika;
    for (const auto& c : counts) if (c.accountId == "erika") erika = c;

    std::vector<std::pair<std::string, MessageFilter>> reported;
    view.onFilterChanged = [&](const std::string& acc, const MessageFilter& f) {
        reported.emplace_back(acc, f);
    };
    view.ShowFolder("erika", "Projects");   // the inbox is not on screen
    frames();
    reported.clear();
    view.ShowInbox("erika", {MessageFilterKind::NewToday, ""});
    frames();
    TEST("a counter opens the inbox", view.CurrentFolder() == "INBOX");
    TEST("New today: the counter's messages",
         static_cast<int>(view.ShownMessages().size()) == erika.unreadToday &&
         erika.unreadToday == 2);
    TEST("the filter is reported for the account",
         reported.size() == 1 && reported[0].first == "erika" &&
         reported[0].second.kind == MessageFilterKind::NewToday);
    view.ShowInbox("erika", {MessageFilterKind::Unread, ""});
    TEST("Unread: all unread mail, today's too",
         static_cast<int>(view.ShownMessages().size()) == erika.unread &&
         erika.unread == 5 && erika.unreadOlder == 3);
    view.ShowInbox("erika", {MessageFilterKind::NeedsAnswer, ""});
    TEST("Waiting for reply: the counter's messages",
         static_cast<int>(view.ShownMessages().size()) == erika.needsAnswer &&
         erika.needsAnswer == 1);
    TEST("each change is reported once", reported.size() == 3);
    view.SetFilter(MessageFilter{});
    TEST("Show all emails: the whole inbox", view.ShownMessages().size() == 7);
    TEST("and no filter is reported",
         reported.size() == 4 && !reported.back().second.Active());
    view.ShowInbox("erika", {MessageFilterKind::NewToday, ""});
    view.ShowFolder("erika", "Sent");
    TEST("another folder clears the filter, and says so",
         !view.Filter().Active() && !reported.back().second.Active());

    // ----- a rebuilt tree keeps the view the user had -----
    // A third account with folders enough to make the tree taller than its
    // pane, as a second or third mailbox does in practice.
    Account third;
    third.accountId = "many";
    third.email = "erika@mailbox.example";
    third.shortName = "many";
    store.UpsertAccount(third);
    accounts.push_back(third);
    store.UpsertFolder(MakeFolder("many", "INBOX", FolderRole::Inbox));
    // Numbered so that the tree's order by name is the order here: "Folder 40"
    // is its last row, far below the pane's foot.
    for (int i = 1; i <= 40; ++i) {
        const std::string number = (i < 10 ? "0" : "") + std::to_string(i);
        store.UpsertFolder(MakeFolder("many", "Folder " + number));
    }
    view.SetAccounts(accounts);
    view.ShowAccount("many");
    frames();

    // The last folder of the account: its row is below the pane's foot until
    // the tree scrolls to it.
    view.ShowFolder("many", "Folder 40");
    frames();
    TEST("a folder opened below the pane's foot is scrolled into view",
         ScreenYOf(tree, "f::many::Folder 40") >= 0);
    TEST("the tree is scrolled down, past the first account",
         ScreenYOf(tree, "acct::erika") == -1);

    // A folder the user then picks in the middle of the scrolled view: the
    // tree does not move for it, so its place on screen is the one the rebuild
    // has to give back - and not the one a scroll-into-view would, which is
    // the pane's foot.
    const std::string deepFolder = "f::many::Folder 30";
    view.ShowFolder("many", "Folder 30");
    frames();
    const int whereBefore = ScreenYOf(tree, deepFolder);
    const int paneFoot = static_cast<int>(tree->GetLocalContentRect().y +
                                          tree->GetLocalContentRect().height) - tree->GetRowHeight();
    TEST("the folder is on screen, and not at the pane's foot",
         whereBefore >= 0 && whereBefore < paneFoot - tree->GetRowHeight());

    // What a mail check ends in: the folders are read again and every row of
    // the tree is replaced.
    view.ShowAccount("many");
    frames();
    TEST("the rebuilt tree still has the folder selected",
         view.CurrentFolder() == "Folder 30" &&
         tree->GetFirstSelectedNode() &&
         tree->GetFirstSelectedNode()->data.nodeId == deepFolder);
    TEST("and leaves it exactly where it was on screen",
         ScreenYOf(tree, deepFolder) == whereBefore);
    TEST("the view did not fall back to the first row",
         ScreenYOf(tree, "acct::erika") == -1);

    // ----- a folder closed by hand stays closed -----
    // Every rescan builds the tree again and expands it in full, so without
    // the collapsed rows being remembered a mailbox closed by hand reopened
    // at the next mail check.
    TreeNode* workRow = tree->FindNode("acct::work");
    TEST("the other account's row is open to begin with",
         workRow != nullptr && workRow->IsExpanded());
    tree->CollapseNode(workRow);
    frames();
    TEST("closing it closes it", !tree->FindNode("acct::work")->IsExpanded());

    view.ShowAccount("many");   // the rescan again
    frames();
    workRow = tree->FindNode("acct::work");
    TEST("and it is still closed after the tree is rebuilt",
         workRow != nullptr && !workRow->IsExpanded());
    TEST("the folders of the account on screen are open as before",
         tree->FindNode("acct::many") && tree->FindNode("acct::many")->IsExpanded());

    tree->ExpandNode(tree->FindNode("acct::work"));
    view.ShowAccount("many");
    frames();
    TEST("opening it again is remembered too",
         tree->FindNode("acct::work") && tree->FindNode("acct::work")->IsExpanded());

    std::cerr << std::endl << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}

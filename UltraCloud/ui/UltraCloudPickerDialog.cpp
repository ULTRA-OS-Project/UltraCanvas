// UltraCloud/ui/UltraCloudPickerDialog.cpp
// Version: 0.1.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCloudPickerDialog.h"
#include "UltraCloudAccountDialog.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasColumnsTreeView.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"

#include <UltraCloud/UltraCloudWebDav.h>   // NormalizePath

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace UltraCanvas;

namespace UltraCloud {

namespace {

const char* kRowPrefix = "cloudEntry_";
const char* kUpId      = "cloudEntryUp";
const char* kRootId    = "cloudRoot";

std::string FormatSize(int64_t bytes) {
    if (bytes <= 0) return "";
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    char buf[32];
    if (u == 0) std::snprintf(buf, sizeof buf, "%lld B", static_cast<long long>(bytes));
    else        std::snprintf(buf, sizeof buf, "%.1f %s", v, units[u]);
    return buf;
}

std::string ParentPath(const std::string& path) {
    std::string p = NormalizePath(path);
    if (p == "/") return "/";
    auto slash = p.find_last_of('/');
    return slash == 0 ? "/" : p.substr(0, slash);
}

// Everything the picker's callbacks share. Held by shared_ptr from the
// widgets' callbacks (the dialog owns the widgets; the state does not own the
// dialog, so no cycle) and by the worker threads.
struct PickerState {
    CloudService* service = nullptr;
    UltraCanvasWindowBase* parent = nullptr;
    std::weak_ptr<UltraCanvasModalDialog> dialog;
    UltraCanvasModalDialog* dlg = nullptr;

    std::vector<Account> accounts;
    int  accountIndex = -1;
    std::string path = "/";
    std::vector<Entry> entries;
    int  selected = -1;          // index into entries
    bool busy = false;
    int  generation = 0;         // ignore answers from an older navigation

    std::shared_ptr<UltraCanvasDropdown>        accountBox;
    std::shared_ptr<UltraCanvasLabel>           pathLabel;
    std::shared_ptr<UltraCanvasLabel>           status;
    std::shared_ptr<UltraCanvasColumnsTreeView> list;
    std::shared_ptr<UltraCanvasButton>          upBtn;
    std::shared_ptr<UltraCanvasButton>          uploadBtn;
    std::shared_ptr<UltraCanvasButton>          linkBtn;

    CloudLinkPick result;
    bool picked = false;

    const Account* Current() const {
        if (accountIndex < 0 || accountIndex >= static_cast<int>(accounts.size())) return nullptr;
        return &accounts[static_cast<std::size_t>(accountIndex)];
    }
};

void Post(std::function<void()> fn) {
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->PostToUIThread(std::move(fn));
    else fn();
}

void SetStatus(const std::shared_ptr<PickerState>& st, const std::string& text, bool error = false) {
    if (!st->status) return;
    st->status->SetTextColor(error ? Color(180, 40, 40, 255) : Color(60, 60, 60, 255));
    st->status->SetText(text);
}

void UpdateButtons(const std::shared_ptr<PickerState>& st) {
    const bool haveAccount = st->Current() != nullptr;
    const bool fileSelected = st->selected >= 0 &&
        st->selected < static_cast<int>(st->entries.size()) &&
        !st->entries[static_cast<std::size_t>(st->selected)].isDirectory;
    if (st->upBtn)     st->upBtn->SetDisabled(!(haveAccount && !st->busy && st->path != "/"));
    if (st->uploadBtn) st->uploadBtn->SetDisabled(!(haveAccount && !st->busy));
    if (st->linkBtn)   st->linkBtn->SetDisabled(!(haveAccount && !st->busy && fileSelected));
}

void FillList(const std::shared_ptr<PickerState>& st) {
    if (!st->list) return;
    TreeNodeData root(kRootId, "");
    st->list->SetRootNode(root);
    if (st->path != "/") {
        TreeNodeData up(kUpId, "..");
        up.textColor = Color(90, 90, 90, 255);
        st->list->AddNode(kRootId, up);
    }
    for (std::size_t i = 0; i < st->entries.size(); ++i) {
        const Entry& e = st->entries[i];
        TreeNodeData node(kRowPrefix + std::to_string(i), (e.isDirectory ? "📁 " : "") + e.name);
        node.tooltip = e.path;
        node.SetCell("size", e.isDirectory ? "" : FormatSize(e.size));
        node.SetCell("modified", e.modified);
        st->list->AddNode(kRootId, node);
    }
    st->list->ExpandAll();
    st->selected = -1;
    if (st->pathLabel) st->pathLabel->SetText(st->path);
    UpdateButtons(st);
}

void Navigate(const std::shared_ptr<PickerState>& st, const std::string& path) {
    const Account* acc = st->Current();
    if (!acc) { st->entries.clear(); FillList(st); return; }
    const std::string accountId = acc->accountId;
    const std::string target = NormalizePath(path);
    const int gen = ++st->generation;
    st->busy = true;
    UpdateButtons(st);
    SetStatus(st, "Loading " + target + "…");

    CloudService* service = st->service;
    std::thread([st, service, accountId, target, gen]() {
        std::vector<Entry> entries;
        Result r = service->List(accountId, target, entries);
        Post([st, entries, r, target, gen]() {
            if (st->dialog.expired() || gen != st->generation) return;
            st->busy = false;
            if (!r) { SetStatus(st, r.message, true); UpdateButtons(st); return; }
            st->path = target;
            st->entries = entries;
            FillList(st);
            SetStatus(st, entries.empty() ? "Empty folder." : "");
        });
    }).detach();
}

void ReloadAccounts(const std::shared_ptr<PickerState>& st, const std::string& preferId) {
    st->accounts.clear();
    st->service->Accounts().List(st->accounts);
    if (st->accountBox) {
        st->accountBox->ClearItems();
        for (const auto& a : st->accounts) st->accountBox->AddItem(a.displayName, a.accountId);
    }
    st->accountIndex = -1;
    for (std::size_t i = 0; i < st->accounts.size(); ++i) {
        if ((!preferId.empty() && st->accounts[i].accountId == preferId) ||
            (preferId.empty() && st->accounts[i].isDefault)) {
            st->accountIndex = static_cast<int>(i);
            break;
        }
    }
    if (st->accountIndex < 0 && !st->accounts.empty()) st->accountIndex = 0;
    if (st->accountBox && st->accountIndex >= 0)
        st->accountBox->SetSelectedIndex(st->accountIndex, /*runNotifications=*/false);
    if (st->accounts.empty()) {
        st->entries.clear();
        FillList(st);
        SetStatus(st, "No cloud account yet — add one to attach links.");
        return;
    }
    Navigate(st, "/");
}

void UploadHere(const std::shared_ptr<PickerState>& st) {
    const Account* acc = st->Current();
    if (!acc) return;
    FileDialogOptions options;
    options.title = "Upload to " + acc->displayName;
    options.parentWindow = st->parent;
    UltraCanvasFileLoader::OpenFileDialog(options, [st](DialogResult result, const std::string& local) {
        if (result != DialogResult::OK || local.empty()) return;
        const Account* acc = st->Current();
        if (!acc) return;
        const std::string accountId = acc->accountId;
        const std::string folder = st->path;
        const std::string name = local.substr(local.find_last_of("/\\\\") + 1);
        const std::string remote = (folder == "/" ? "" : folder) + "/" + name;
        st->busy = true;
        UpdateButtons(st);
        SetStatus(st, "Uploading " + name + "…");
        CloudService* service = st->service;
        std::thread([st, service, accountId, local, remote, folder]() {
            Result r = service->Upload(accountId, local, remote);
            Post([st, r, folder]() {
                if (st->dialog.expired()) return;
                st->busy = false;
                if (!r) { SetStatus(st, r.message, true); UpdateButtons(st); return; }
                Navigate(st, folder);   // refresh; the new file shows up
            });
        }).detach();
    });
}

void MakeLinkAndClose(const std::shared_ptr<PickerState>& st) {
    const Account* acc = st->Current();
    if (!acc || st->selected < 0 || st->selected >= static_cast<int>(st->entries.size())) return;
    const Entry entry = st->entries[static_cast<std::size_t>(st->selected)];
    if (entry.isDirectory) return;
    const Account account = *acc;
    st->busy = true;
    UpdateButtons(st);
    SetStatus(st, "Creating link for " + entry.name + "…");
    CloudService* service = st->service;
    std::thread([st, service, account, entry]() {
        ShareLink link;
        Result r = service->CreateShareLink(account.accountId, entry.path, {}, link);
        Post([st, r, link, account, entry]() {
            if (st->dialog.expired()) return;
            st->busy = false;
            if (!r) { SetStatus(st, r.message, true); UpdateButtons(st); return; }
            st->result.account = account;
            st->result.entry = entry;
            st->result.link = link;
            st->picked = true;
            if (st->dlg) st->dlg->CloseDialog(DialogResult::OK);
        });
    }).detach();
}

} // namespace

void ShowCloudLinkPicker(UltraCanvasWindowBase* parent, CloudService& service,
                         std::function<void(const CloudLinkPick&)> onPicked) {
    DialogConfig config;
    config.title      = "Attach cloud link";
    config.width      = 640;
    config.height     = 480;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    auto st = std::make_shared<PickerState>();
    st->service = &service;
    st->parent  = parent;
    st->dialog  = dialog;
    st->dlg     = dialog.get();

    dialog->layout.SetFlexColumn()
                  .SetFlexGap(8)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(14);

    // Row 1: account + add.
    auto accountRow = CreateContainer("cloudPickAccountRow", 0, 0, 0, 30);
    accountRow->layout.SetFlexRow().SetFlexGap(8)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    accountRow->AddChild(CreateLabel("cloudPickAccountLbl", 0, 0, 70, 26, "Account"));
    st->accountBox = CreateDropdown("cloudPickAccount", 0, 0, 300, 28);
    accountRow->AddChild(st->accountBox);
    st->accountBox->layoutItem.SetFlexGrow(1);
    auto addBtn = CreateButton("cloudPickAdd", 0, 0, 120, 28, "Add account…");
    accountRow->AddChild(addBtn);
    dialog->AddChild(accountRow);

    // Row 2: path + Up + Upload.
    auto pathRow = CreateContainer("cloudPickPathRow", 0, 0, 0, 30);
    pathRow->layout.SetFlexRow().SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    pathRow->AddChild(CreateLabel("cloudPickPathLbl", 0, 0, 70, 26, "Folder"));
    st->pathLabel = CreateLabel("cloudPickPath", 0, 0, 0, 26, "/");
    pathRow->AddChild(st->pathLabel);
    st->pathLabel->layoutItem.SetFlexGrow(1);
    st->upBtn = CreateButton("cloudPickUp", 0, 0, 60, 28, "↑ Up");
    st->uploadBtn = CreateButton("cloudPickUpload", 0, 0, 120, 28, "Upload file…");
    pathRow->AddChild(st->upBtn);
    pathRow->AddChild(st->uploadBtn);
    dialog->AddChild(pathRow);

    // The file list.
    st->list = std::make_shared<UltraCanvasColumnsTreeView>("cloudPickList", 0, 0, 0, 0);
    st->list->SetDisplayMode(TreeDisplayMode::Columns);
    st->list->SetSelectionMode(TreeSelectionMode::Single);
    st->list->SetShowColumnHeader(true);
    st->list->SetRootVisible(false);
    st->list->SetRowHeight(24);
    st->list->SetColumns({
        { "name",     "Name",     0,   160, 1.0f, TextAlignment::Left,
          Color(30, 30, 30), Colors::Transparent, 0, /*isTreeColumn=*/true },
        { "size",     "Size",     90,  0,   1.0f, TextAlignment::Right,
          Color(90, 90, 90), Colors::Transparent, 0, false },
        { "modified", "Modified", 170, 0,   1.0f, TextAlignment::Left,
          Color(90, 90, 90), Colors::Transparent, 0, false },
    });
    TreeColumnStyle columnStyle;
    columnStyle.headerHeight      = 24;
    columnStyle.headerBackground  = Color(240, 240, 240, 255);
    columnStyle.headerTextColor   = Color(40, 40, 40, 255);
    columnStyle.headerBorderColor = Color(205, 205, 205, 255);
    st->list->SetColumnStyle(columnStyle);
    dialog->AddChild(st->list);
    st->list->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    st->status = CreateLabel("cloudPickStatus", 0, 0, 0, 22, "");
    dialog->AddChild(st->status);

    // Buttons.
    auto buttons = CreateContainer("cloudPickButtons", 0, 0, 0, 36);
    buttons->layout.SetFlexRow().SetFlexGap(10)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttons->AddStretchSpacer(1);
    st->linkBtn = CreateButton("cloudPickLink", 0, 0, 120, 28, "Insert link");
    st->linkBtn->SetStyle(ButtonStyles::PrimaryStyle());
    auto cancelBtn = CreateButton("cloudPickCancel", 0, 0, 80, 28, "Cancel");
    buttons->AddChild(st->linkBtn);
    buttons->AddChild(cancelBtn);
    dialog->AddChild(buttons);

    // Wiring.
    auto indexOf = [](const TreeNode* node) -> int {
        if (!node) return -1;
        const std::string& id = node->data.nodeId;
        if (id.rfind(kRowPrefix, 0) != 0) return -1;
        return std::atoi(id.c_str() + std::strlen(kRowPrefix));
    };
    st->list->onNodeSelected = [st, indexOf](TreeNode* node) {
        st->selected = indexOf(node);
        UpdateButtons(st);
    };
    st->list->onNodeDoubleClicked = [st, indexOf](TreeNode* node) {
        if (!node) return;
        if (node->data.nodeId == kUpId) { Navigate(st, ParentPath(st->path)); return; }
        int i = indexOf(node);
        if (i < 0 || i >= static_cast<int>(st->entries.size())) return;
        const Entry& e = st->entries[static_cast<std::size_t>(i)];
        if (e.isDirectory) Navigate(st, e.path);
        else { st->selected = i; MakeLinkAndClose(st); }
    };
    st->accountBox->onSelectionChanged = [st](int index, const DropdownItem&) {
        if (index < 0 || index >= static_cast<int>(st->accounts.size())) return;
        st->accountIndex = index;
        Navigate(st, "/");
    };
    st->upBtn->onClick     = [st]() { Navigate(st, ParentPath(st->path)); };
    st->uploadBtn->onClick = [st]() { UploadHere(st); };
    st->linkBtn->onClick   = [st]() { MakeLinkAndClose(st); };
    cancelBtn->onClick     = [st]() { if (st->dlg) st->dlg->CloseDialog(DialogResult::Cancel); };
    addBtn->onClick = [st, &service]() {
        ShowAddAccountDialog(st->parent, service, [st](const Account& added) {
            ReloadAccounts(st, added.accountId);
        });
    };

    ReloadAccounts(st, "");

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [st, onPicked](DialogResult result) {
            if (result == DialogResult::OK && st->picked && onPicked) onPicked(st->result);
        },
        parent);
}

} // namespace UltraCloud

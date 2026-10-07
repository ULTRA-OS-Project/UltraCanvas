// Apps/UltraPassword/ui/PasswordApp.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "PasswordApp.h"

#include "CreateVaultWizard.h"
#include "EntryCard.h"
#include "EntryDialog.h"
#include "ExportDialog.h"
#include "Theme.h"
#include "core/CsvExchange.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasUtils.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>

#ifndef ULTRAPASSWORD_VERSION
#error "ULTRAPASSWORD_VERSION must come from Docs/UltraPassword/CHANGELOG.md via CMake"
#endif

using namespace UltraCanvas;

namespace UltraPassword {

namespace {

constexpr const char* kGroupPrefix = "g:";
constexpr const char* kEntryPrefix = "e:";

std::string GroupNode(const std::string& groupId) { return kGroupPrefix + groupId; }
std::string EntryNode(const std::string& entryId) { return kEntryPrefix + entryId; }

bool StartsWith(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

std::string Icon(const char* name) {
    return NormalizePath(GetResourcesDir() + "media/icons/" + name);
}

std::string FileNameOf(const std::string& path) {
    return PathToUtf8(PathFromUtf8(path).filename());
}

std::string StemOf(const std::string& path) {
    return PathToUtf8(PathFromUtf8(path).stem());
}

// Where save and open dialogs start: the user's home folder, in UTF-8 like
// every path the dialogs take (on Windows a narrow getenv would answer in
// the ANSI code page).
std::string HomeDirectory() {
    if (std::string home = GetEnvUtf8("HOME"); !home.empty()) return home;
    return GetEnvUtf8("USERPROFILE");
}

std::string Plural(size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

UltraCryptSecureBuffer AdoptPassword(const std::string& typed) {
    std::string copy = typed;
    return UltraCryptSecureBuffer::AdoptString(copy);
}

} // namespace

PasswordApp::PasswordApp(UltraCanvasApplication& app, const std::string& vaultPath)
    : app_(app), vaultPath_(vaultPath) {}

PasswordApp::~PasswordApp() {
    if (tickRunning_) app_.StopTimer(tickTimer_);
    ClearClipboardIfOurs();
    vault_.Clear();
    file_.Close();
}

// ===========================================================================
// Views
// ===========================================================================
bool PasswordApp::Create() {
    WindowConfig config;
    config.title  = "UltraPassword " ULTRAPASSWORD_VERSION;
    config.width  = kWindowWidth;
    config.height = kWindowHeight;
    config.minWidth = 640;
    config.backgroundColor = Theme::kPageBackground;
    window_ = CreateWindow(config);
    if (!window_) return false;

    auto start = startPage_.Build();
    startPage_.onCreate    = [this]() { BeginCreateVault(); };
    startPage_.onOpenOther = [this]() { OpenOtherVault(); };
    startPage_.onUnlock    = [this](const std::string& pw) { Unlock(pw); };
    window_->AddChild(start);

    vaultView_ = BuildVaultView();
    window_->AddChild(vaultView_);

    ResizeViews(static_cast<float>(config.width), static_cast<float>(config.height));
    window_->onWindowResize = [this](int w, int h) {
        ResizeViews(static_cast<float>(w), static_cast<float>(h));
    };

    window_->onWindowClosing = [this]() {
        if (tickRunning_) { app_.StopTimer(tickTimer_); tickRunning_ = false; }
        ClearClipboardIfOurs();
        vault_.Clear();
        file_.Close();
        app_.RequestExit();
        return true;
    };

    // Idle means no input to this window. The filter only takes the time.
    window_->InstallEventFilter(
        "upw-activity",
        [this](const UCEvent&) { NoteActivity(); return false; },
        {UCEventType::MouseDown, UCEventType::MouseUp, UCEventType::MouseMove,
         UCEventType::MouseWheel, UCEventType::KeyDown, UCEventType::KeyUp});

    // Minimising means the user has looked away. The lock itself runs on the
    // next tick, so it is not done from inside the window manager's callback.
    window_->onWindowMinimize = [this]() { if (unlocked_) lockPending_ = true; };

    if (VaultFile::Exists(vaultPath_)) {
        ShowStartPage(StartPage::Mode::Unlock);
    } else {
        ShowStartPage(StartPage::Mode::Create);
    }

    NoteActivity();
    tickTimer_ = app_.StartTimer(1000, true, [this](TimerId) { Tick(); });
    tickRunning_ = true;
    return true;
}

void PasswordApp::Show() {
    if (!window_) return;
    window_->Show();
    startPage_.FocusPassword();
}

void PasswordApp::ResizeViews(float width, float height) {
    startPage_.Resize(width, height);
    if (vaultView_) vaultView_->SetElementSize(Size2Df(width, height));
}

std::shared_ptr<UltraCanvasContainer> PasswordApp::BuildToolbar() {
    auto bar = CreateContainer("upwToolbar", 0, 0, 0, Theme::kToolbarHeight + 12);
    bar->SetBackgroundColor(Theme::kCardBackground);
    bar->SetBorders(0.0f, Theme::kCardBorder, 0.0f);
    bar->SetPadding(6, Theme::kPagePadding);
    bar->layout.SetFlexRow()
               .SetFlexGap(Theme::kInnerGap)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bar->layoutItem.SetFlexShrink(0);

    auto add = Theme::MakeButton("upwAddEntry", "New password", 110, /*primary=*/true);
    add->onClick = [this]() { AddEntry(); };
    bar->AddChild(add);

    auto addGroup = Theme::MakeButton("upwAddGroup", "New group", 90);
    addGroup->SetTooltip("Add a group inside the selected one");
    addGroup->onClick = [this]() { AddGroup(); };
    bar->AddChild(addGroup);

    auto rename = Theme::MakeButton("upwRenameGroup", "Rename", 70);
    rename->SetTooltip("Rename the selected group (or the vault, at the top)");
    rename->onClick = [this]() { RenameGroup(); };
    bar->AddChild(rename);

    auto delGroup = Theme::MakeButton("upwDeleteGroup", "Delete group", 90);
    delGroup->onClick = [this]() { DeleteGroup(); };
    bar->AddChild(delGroup);

    searchInput_ = CreateTextInput("upwSearch", 0, 0, 200, Theme::kControlHeight);
    searchInput_->SetPlaceholder("Search title, website, user name");
    Theme::StyleInput(searchInput_);
    searchInput_->onTextChanged = [this](const std::string& text) { SetSearch(text); };
    searchInput_->layoutItem.SetFlexShrink(1);
    bar->AddChild(searchInput_);

    bar->AddStretchSpacer(1);

    auto import = Theme::MakeButton("upwImport", "Import…", 70);
    import->SetTooltip("Add passwords from a vault file or a CSV export");
    import->onClick = [this]() { Import(); };
    bar->AddChild(import);

    auto exportBtn = Theme::MakeButton("upwExport", "Export…", 70);
    exportBtn->onClick = [this]() { Export(); };
    bar->AddChild(exportBtn);

    auto master = Theme::MakeButton("upwMaster", "Master password…", 120);
    master->onClick = [this]() { ChangeMasterPassword(); };
    bar->AddChild(master);

    auto lock = Theme::MakeButton("upwLock", "Lock", 60);
    lock->onClick = [this]() { Lock("Locked."); };
    bar->AddChild(lock);
    return bar;
}

std::shared_ptr<UltraCanvasContainer> PasswordApp::BuildVaultView() {
    auto view = CreateContainer("upwVaultView", 0, 0, 0, 0);
    view->SetBackgroundColor(Theme::kPageBackground);
    view->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    view->AddChild(BuildToolbar());

    auto split = std::make_shared<UltraCanvasSplitPane>("upwSplit", SplitOrientation::Horizontal);
    split->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    SplitPaneStyle splitStyle;
    splitStyle.splitterThickness = 6;
    splitStyle.splitterHitMargin = 3;
    splitStyle.splitterColor     = Theme::kDivider;
    split->SetSplitPaneStyle(splitStyle);

    // ----- left: the group tree -----
    auto treePane = split->AddPane(1.0);
    split->SetPaneFixedSize(0, kTreeWidth);
    split->SetPaneMinSize(0, 180);
    treePane->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    treePane->SetBackgroundColor(Theme::kSidebar);

    tree_ = std::make_shared<UltraCanvasTreeView>("upwTree");
    tree_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    tree_->SetRowHeight(24);
    tree_->SetFontSize(Theme::kSizeBody);
    tree_->SetSelectionMode(TreeSelectionMode::Single);
    tree_->SetSelectionColor(Theme::kRowSelected);
    tree_->SetHoverColor(Theme::kRowHover);
    tree_->SetShowFirstChildOnExpand(false);
    tree_->onNodeSelected  = [this](TreeNode* node) { OnTreeSelection(node); };
    tree_->onNodeExpanded  = [this](TreeNode* node) { if (node) expandedNodes_.insert(node->data.nodeId); };
    tree_->onNodeCollapsed = [this](TreeNode* node) { if (node) expandedNodes_.erase(node->data.nodeId); };
    treePane->AddChild(tree_);

    // ----- right: heading + cards -----
    auto cardsSide = split->AddPane(3.0);
    split->SetPaneMinSize(1, 360);
    cardsSide->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    cardsSide->SetPadding(Theme::kPagePadding);

    paneTitle_ = Theme::MakeLine("upwPaneTitle", "", 24, Theme::kSizeTitle, Theme::kTextPrimary,
                                 FontWeight::Bold);
    paneTitle_->layoutItem.SetFlexShrink(0);
    cardsSide->AddChild(paneTitle_);
    paneSummary_ = Theme::MakeLine("upwPaneSummary", "", 20, Theme::kSizeBody, Theme::kTextSecondary);
    paneSummary_->layoutItem.SetFlexShrink(0);
    cardsSide->AddChild(paneSummary_);

    cardsPane_ = CreateScrollableContainer("upwCards", 0, 0, 0, 0);
    cardsPane_->layout.SetFlexColumn()
                      .SetFlexGap(Theme::kGap)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    cardsPane_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    // Room on the right for the vertical scrollbar, so a card never runs
    // under it and the pane never needs a horizontal one.
    cardsPane_->SetPadding(4, 16, 4, 2);
    cardsPane_->SetShowHorizontalScrollbar(false);
    cardsSide->AddChild(cardsPane_);

    view->AddChild(split);

    // ----- status line -----
    statusLabel_ = Theme::MakeLine("upwStatus", "", 22, Theme::kSizeSmall, Theme::kTextMuted);
    statusLabel_->SetPadding(0, Theme::kPagePadding);
    statusLabel_->layoutItem.SetFlexShrink(0);
    view->AddChild(statusLabel_);

    view->SetVisible(false);
    return view;
}

void PasswordApp::ShowStartPage(StartPage::Mode mode, const std::string& detail) {
    if (vaultView_) vaultView_->SetVisible(false);
    if (auto page = startPage_.Container()) page->SetVisible(true);
    std::string text = detail;
    if (mode == StartPage::Mode::Unlock) {
        const std::string where = "Vault: " + FileNameOf(vaultPath_) + ".";
        text = detail.empty() ? "Enter your master password to unlock the vault. " + where
                              : detail + " " + where;
    }
    startPage_.SetMode(mode, text);
    startPage_.FocusPassword();
}

void PasswordApp::ShowVaultView() {
    if (auto page = startPage_.Container()) page->SetVisible(false);
    if (vaultView_) vaultView_->SetVisible(true);
    selectedGroup_ = kRootGroupId;
    selectedEntry_.clear();
    searchText_.clear();
    if (searchInput_) searchInput_->SetText("");
    expandedNodes_ = {GroupNode(kRootGroupId)};
    RebuildTree();
    RebuildCards();
    SetStatus("Unlocked. Locks after 5 minutes without input, or when minimised.");
}

// ===========================================================================
// Vault lifecycle
// ===========================================================================
void PasswordApp::BeginCreateVault() {
    CreateVaultWizard::Show(window_.get(), [this](VaultDraft& draft) {
        CreateVault(draft.name, draft.password, draft.profile);
    });
}

void PasswordApp::CreateVault(const std::string& name, const std::string& password,
                              KdfProfile profile) {
    startPage_.SetError("");
    startPage_.SetMode(StartPage::Mode::Create, "Creating the vault - deriving the key "
                                                "from your master password…");
    auto secret = std::make_shared<UltraCryptSecureBuffer>(AdoptPassword(password));
    RunAfterPaint([this, name, profile, secret]() {
        PasswordVault fresh;
        fresh.SetName(name);
        // Two starting groups, so the tree shows what it is for.
        fresh.AddGroup("Personal");
        fresh.AddGroup("Work");
        VaultResult r = file_.Create(vaultPath_, *secret, fresh, KdfParamsFor(profile));
        secret->Clear();
        if (!r) {
            ShowStartPage(StartPage::Mode::Create);
            Alert("The vault could not be created.\n" + r.message);
            return;
        }
        vault_ = std::move(fresh);
        unlocked_ = true;
        throttle_.RecordSuccess();
        ShowVaultView();
        SetStatus("Vault created: " + vaultPath_);
    });
}

void PasswordApp::Unlock(const std::string& password) {
    const uint32_t wait = throttle_.SecondsToWait(NowSeconds());
    if (wait > 0) {
        startPage_.SetError("Too many wrong passwords. Try again in " +
                            Plural(wait, "second", "seconds") + ".");
        return;
    }
    startPage_.SetError("");
    startPage_.SetBusy("Unlocking…");
    auto secret = std::make_shared<UltraCryptSecureBuffer>(AdoptPassword(password));
    RunAfterPaint([this, secret]() {
        PasswordVault opened;
        VaultResult r = file_.Open(vaultPath_, *secret, opened);
        secret->Clear();
        startPage_.SetBusy("");
        if (!r) {
            if (r.code == VaultError::AuthenticationFailed) throttle_.RecordFailure(NowSeconds());
            startPage_.SetError(r.message);
            startPage_.FocusPassword();
            return;
        }
        throttle_.RecordSuccess();
        vault_ = std::move(opened);
        unlocked_ = true;
        NoteActivity();
        ShowVaultView();
    });
}

void PasswordApp::OpenOtherVault() {
    FileDialogOptions opts;
    opts.SetInitialDirectory(HomeDirectory());
    opts.SetTitle("Open a password vault")
        .AddFilter("UltraPassword vault", kVaultExtension)
        .SetRegisterAsRecent(false)
        .SetParentWindow(window_.get());
    UltraCanvasFileLoader::OpenFileDialog(opts, [this](DialogResult result, const std::string& path) {
        if (result != DialogResult::OK || path.empty()) return;
        if (unlocked_) Lock("");
        vaultPath_ = path;
        ShowStartPage(StartPage::Mode::Unlock);
    });
}

void PasswordApp::Lock(const std::string& reason) {
    lockPending_ = false;
    UltraCanvasDialogManager::CloseAllDialogs();
    ClearClipboardIfOurs();
    if (tree_) tree_->SetRootNode(TreeNodeData(GroupNode(kRootGroupId), ""));
    if (cardsPane_) cardsPane_->ClearChildren();
    vault_.Clear();
    file_.Close();
    unlocked_ = false;
    ShowStartPage(StartPage::Mode::Unlock, reason);
}

bool PasswordApp::SaveVault() {
    VaultResult r = file_.Save(vault_);
    if (!r) {
        Alert("The vault could not be saved. Your last change is only in memory.\n" + r.message);
        SetStatus("Save failed: " + r.message, true);
        return false;
    }
    return true;
}

// ===========================================================================
// Tree and cards
// ===========================================================================
void PasswordApp::RebuildTree() {
    if (!tree_) return;
    rebuildingTree_ = true;
    TreeNodeData root(GroupNode(kRootGroupId),
                      vault_.GetName().empty() ? "Vault" : vault_.GetName());
    root.leftIcon = TreeNodeIcon(Icon("folder-open.svg"), 16, 16);
    root.textColor = Theme::kTextPrimary;
    root.showFirstChildOnExpand = false;
    tree_->SetRootNode(root);
    AddGroupNodes(kRootGroupId, GroupNode(kRootGroupId));

    for (const std::string& id : expandedNodes_) {
        if (TreeNode* node = tree_->FindNode(id)) node->Expand();
    }
    if (TreeNode* r = tree_->GetRootNode()) r->Expand();

    const std::string selectedNode = !selectedEntry_.empty() ? EntryNode(selectedEntry_)
                                                             : GroupNode(selectedGroup_);
    if (TreeNode* node = tree_->FindNode(selectedNode)) {
        // Make sure the selection is visible: open every ancestor.
        for (TreeNode* p = node->parent; p; p = p->parent) p->Expand();
        tree_->SelectNode(node);
    }
    rebuildingTree_ = false;
    tree_->RequestRedraw();
}

void PasswordApp::AddGroupNodes(const std::string& parentGroupId, const std::string& parentNodeId) {
    for (const PasswordGroup* g : vault_.ChildGroups(parentGroupId)) {
        TreeNodeData data(GroupNode(g->id), g->name);
        data.leftIcon = TreeNodeIcon(Icon("folder.png"), 16, 16);
        data.textColor = Theme::kTextPrimary;
        data.showFirstChildOnExpand = false;
        tree_->AddNode(parentNodeId, data);
        AddGroupNodes(g->id, data.nodeId);
    }
    for (const PasswordEntry* e : vault_.EntriesIn(parentGroupId)) {
        TreeNodeData data(EntryNode(e->id), e->title);
        data.leftIcon = TreeNodeIcon(Icon("key.svg"), 14, 14);
        const auto& info = GetSignInMethodInfo(e->method);
        data.textColor = Theme::kTextSecondary;
        data.tooltip = std::string(info.name) + " - " + SecurityLevelName(info.level);
        tree_->AddNode(parentNodeId, data);
    }
}

void PasswordApp::OnTreeSelection(TreeNode* node) {
    if (rebuildingTree_ || !node || !unlocked_) return;
    const std::string& id = node->data.nodeId;
    if (StartsWith(id, kEntryPrefix)) {
        selectedEntry_ = id.substr(2);
        if (const PasswordEntry* e = vault_.FindEntry(selectedEntry_)) selectedGroup_ = e->groupId;
    } else if (StartsWith(id, kGroupPrefix)) {
        selectedEntry_.clear();
        selectedGroup_ = id.substr(2);
    }
    if (!searchText_.empty()) {
        searchText_.clear();
        if (searchInput_) searchInput_->SetText("");
    }
    RebuildCards();
}

void PasswordApp::SetSearch(const std::string& text) {
    if (!unlocked_) return;
    searchText_ = text;
    RebuildCards();
}

void PasswordApp::RebuildCards() {
    if (!cardsPane_) return;
    cardsPane_->ClearChildren();

    EntryCardCallbacks cb;
    cb.passwordOf = [this](const std::string& entryId) -> std::string {
        const PasswordEntry* e = vault_.FindEntry(entryId);
        return e ? e->password : std::string();
    };
    cb.copy        = [this](const std::string& text, const std::string& what) { CopyToClipboard(text, what); };
    cb.edit        = [this](const std::string& entryId) { EditEntry(entryId); };
    cb.remove      = [this](const std::string& entryId) { DeleteEntry(entryId); };
    cb.openWebsite = [this](const std::string& url) { OpenWebsite(url); };

    std::vector<const PasswordEntry*> shown;
    std::string title;
    std::string summary;

    if (!searchText_.empty()) {
        shown = vault_.Search(searchText_);
        title = "Search: \"" + searchText_ + "\"";
        summary = shown.empty() ? "Nothing matches." : Plural(shown.size(), "match", "matches") +
                                                       " in the whole vault.";
    } else if (!selectedEntry_.empty() && vault_.FindEntry(selectedEntry_)) {
        shown = {vault_.FindEntry(selectedEntry_)};
        title = shown.front()->title;
        const std::string path = vault_.GroupPath(shown.front()->groupId);
        summary = "In " + (path.empty() ? vault_.GetName() + " (top level)" : path) + ".";
    } else {
        if (!vault_.GroupExists(selectedGroup_)) selectedGroup_ = kRootGroupId;
        shown = vault_.EntriesIn(selectedGroup_);
        title = selectedGroup_ == kRootGroupId ? vault_.GetName() : vault_.GroupPath(selectedGroup_);
        const size_t subgroups = vault_.ChildGroups(selectedGroup_).size();
        summary = Plural(shown.size(), "password", "passwords");
        if (subgroups > 0) summary += ", " + Plural(subgroups, "group", "groups");
        size_t weak = 0;
        for (const PasswordEntry* e : shown)
            if (GetSignInMethodInfo(e->method).level == SecurityLevel::Weak) ++weak;
        if (weak > 0)
            summary += "  ·  " + std::to_string(weak) + " rel" + (weak == 1 ? "ies" : "y") +
                       " on a password alone";
        if (selectedGroup_ == kRootGroupId)
            summary += "  ·  " + Plural(vault_.EntryCount(), "password", "passwords") + " in the vault";
    }
    paneTitle_->SetText(title);
    paneSummary_->SetText(summary);

    for (const PasswordEntry* e : shown) {
        cardsPane_->AddChild(BuildEntryCard(*e, vault_.GroupPath(e->groupId),
                                            /*highlighted=*/e->id == selectedEntry_, cb));
    }
    if (shown.empty() && searchText_.empty()) {
        auto hint = Theme::MakeLine("upwEmptyHint",
            "No passwords here yet. \"New password\" adds one to this group; "
            "\"New group\" adds a group inside it.", 40, Theme::kSizeBody, Theme::kTextMuted);
        hint->SetWrap(TextWrap::WrapWord);
        cardsPane_->AddChild(hint);
    }
    cardsPane_->ScrollToVertical(0);
    cardsPane_->RequestRedraw();
}

// ===========================================================================
// Entry and group actions
// ===========================================================================
void PasswordApp::AddEntry() {
    if (!unlocked_) return;
    PasswordEntry draft;
    draft.groupId = selectedGroup_;
    EntryDialog::Show(window_.get(), vault_, draft, [this](PasswordEntry& entry) {
        const std::string id = vault_.AddEntry(entry);
        if (id.empty()) { Alert("The group for this password no longer exists."); return; }
        if (!SaveVault()) return;
        selectedGroup_ = entry.groupId;
        selectedEntry_ = id;
        expandedNodes_.insert(GroupNode(entry.groupId));
        RebuildTree();
        RebuildCards();
        SetStatus("Added \"" + entry.title + "\".");
    });
}

void PasswordApp::EditEntry(const std::string& entryId) {
    const PasswordEntry* stored = vault_.FindEntry(entryId);
    if (!stored) return;
    PasswordEntry copy = *stored;
    EntryDialog::Show(window_.get(), vault_, copy, [this](PasswordEntry& entry) {
        if (!vault_.UpdateEntry(entry)) { Alert("The password could not be updated."); return; }
        if (!SaveVault()) return;
        selectedGroup_ = entry.groupId;
        expandedNodes_.insert(GroupNode(entry.groupId));
        RebuildTree();
        RebuildCards();
        SetStatus("Saved \"" + entry.title + "\".");
    });
    copy.Wipe();
}

void PasswordApp::DeleteEntry(const std::string& entryId) {
    const PasswordEntry* e = vault_.FindEntry(entryId);
    if (!e) return;
    const std::string title = e->title;
    UltraCanvasAlert::Confirm("Delete \"" + title + "\"? This cannot be undone.",
                              "Delete password",
                              [this, entryId, title](bool yes) {
        if (!yes || !vault_.RemoveEntry(entryId)) return;
        if (!SaveVault()) return;
        if (selectedEntry_ == entryId) selectedEntry_.clear();
        RebuildTree();
        RebuildCards();
        SetStatus("Deleted \"" + title + "\".");
    }, window_.get());
}

void PasswordApp::AddGroup() {
    if (!unlocked_) return;
    const std::string parent = selectedGroup_;
    const std::string where = parent == kRootGroupId ? "at the top level"
                                                     : "inside \"" + vault_.GroupPath(parent) + "\"";
    UltraCanvasDialogManager::ShowInputDialog(
        "Name of the new group, " + where + ":", "New group", "", InputType::Text,
        [this, parent](DialogResult result, const std::string& name) {
            if (result != DialogResult::OK || name.empty()) return;
            const std::string id = vault_.AddGroup(name, parent);
            if (id.empty()) { Alert("The parent group no longer exists."); return; }
            if (!SaveVault()) return;
            expandedNodes_.insert(GroupNode(parent));
            selectedGroup_ = id;
            selectedEntry_.clear();
            RebuildTree();
            RebuildCards();
            SetStatus("Group \"" + name + "\" added.");
        },
        window_.get());
}

void PasswordApp::RenameGroup() {
    if (!unlocked_) return;
    const std::string id = selectedGroup_;
    const bool isVault = id == kRootGroupId;
    const std::string current = isVault ? vault_.GetName()
                                        : (vault_.FindGroup(id) ? vault_.FindGroup(id)->name : "");
    UltraCanvasDialogManager::ShowInputDialog(
        isVault ? "New name for the vault:" : "New name for the group:",
        isVault ? "Rename vault" : "Rename group", current, InputType::Text,
        [this, id, isVault](DialogResult result, const std::string& name) {
            if (result != DialogResult::OK || name.empty()) return;
            if (isVault) vault_.SetName(name);
            else if (!vault_.RenameGroup(id, name)) return;
            if (!SaveVault()) return;
            RebuildTree();
            RebuildCards();
        },
        window_.get());
}

void PasswordApp::DeleteGroup() {
    if (!unlocked_) return;
    const std::string id = selectedGroup_;
    const PasswordGroup* g = vault_.FindGroup(id);
    if (!g) {
        SetStatus("Select a group in the tree first - the vault itself cannot be deleted.", true);
        return;
    }
    // Count what goes with it, so the question says what is at stake.
    size_t entries = 0, groups = 0;
    std::vector<std::string> stack = {id};
    while (!stack.empty()) {
        const std::string current = stack.back();
        stack.pop_back();
        entries += vault_.EntriesIn(current).size();
        for (const PasswordGroup* c : vault_.ChildGroups(current)) { ++groups; stack.push_back(c->id); }
    }
    std::string message = "Delete the group \"" + g->name + "\"";
    if (entries + groups > 0)
        message += " with " + Plural(entries, "password", "passwords") +
                   (groups ? " and " + Plural(groups, "sub-group", "sub-groups") : "") + " in it";
    message += "? This cannot be undone.";
    const std::string name = g->name;
    const std::string parent = g->parentId;
    UltraCanvasAlert::Confirm(message, "Delete group", [this, id, name, parent](bool yes) {
        if (!yes || !vault_.RemoveGroup(id)) return;
        if (!SaveVault()) return;
        selectedGroup_ = parent;
        selectedEntry_.clear();
        RebuildTree();
        RebuildCards();
        SetStatus("Group \"" + name + "\" deleted.");
    }, window_.get());
}

// ===========================================================================
// Export / import
// ===========================================================================
void PasswordApp::Export() {
    if (!unlocked_) return;
    ExportDialog::Show(window_.get(), [this](ExportChoice choice) {
        switch (choice) {
            case ExportChoice::VaultCopy:         ExportCopy(); break;
            case ExportChoice::VaultWithPassword: ExportWithPassword(); break;
            case ExportChoice::PlainCsv:          ExportCsv(); break;
        }
    });
}

void PasswordApp::ExportCopy() {
    FileDialogOptions opts;
    opts.SetInitialDirectory(HomeDirectory());
    opts.SetTitle("Export the vault")
        .SetDefaultFileName(vault_.GetName() + " export." + kVaultExtension)
        .AddFilter("UltraPassword vault", kVaultExtension)
        .SetRegisterAsRecent(false)
        .SetParentWindow(window_.get());
    UltraCanvasFileLoader::SaveFileDialog(opts, [this](DialogResult result, const std::string& path) {
        if (result != DialogResult::OK || path.empty() || !unlocked_) return;
        VaultResult r = file_.ExportCopy(path, vault_);
        if (!r) { Alert("Export failed.\n" + r.message); return; }
        SetStatus("Exported to " + path + " - it opens with your master password.");
    });
}

void PasswordApp::ExportWithPassword() {
    UltraCanvasDialogManager::ShowInputDialog(
        "Password for the exported file (at least " +
            std::to_string(kMinMasterPasswordLength) + " characters):",
        "Export password", "", InputType::Password,
        [this](DialogResult result, const std::string& first) {
            if (result != DialogResult::OK) return;
            if (first.size() < kMinMasterPasswordLength) {
                Alert("The export password needs at least " +
                      std::to_string(kMinMasterPasswordLength) + " characters.");
                return;
            }
            auto secret = std::make_shared<UltraCryptSecureBuffer>(AdoptPassword(first));
            UltraCanvasDialogManager::ShowInputDialog(
                "Type the export password again:", "Export password", "", InputType::Password,
                [this, secret](DialogResult again, const std::string& second) {
                    if (again != DialogResult::OK) { secret->Clear(); return; }
                    if (!UltraCrypt_ConstantTimeEquals(secret->Data(), secret->GetSize(),
                                                       second.data(), second.size())) {
                        secret->Clear();
                        Alert("The two passwords are not the same. Nothing was exported.");
                        return;
                    }
                    FileDialogOptions opts;
                    opts.SetInitialDirectory(HomeDirectory());
    opts.SetTitle("Export the vault")
                        .SetDefaultFileName(vault_.GetName() + " shared." + kVaultExtension)
                        .AddFilter("UltraPassword vault", kVaultExtension)
                        .SetRegisterAsRecent(false)
                        .SetParentWindow(window_.get());
                    UltraCanvasFileLoader::SaveFileDialog(opts,
                        [this, secret](DialogResult result, const std::string& path) {
                            if (result != DialogResult::OK || path.empty() || !unlocked_) {
                                secret->Clear();
                                return;
                            }
                            UltraCryptKdfParams cost;
                            cost.iterations = file_.GetKdfIterations();
                            cost.memoryKiB  = file_.GetKdfMemoryKiB();
                            SetStatus("Exporting - deriving the export key…");
                            RunAfterPaint([this, secret, path, cost]() {
                                VaultResult r = VaultFile::ExportWithPassword(path, *secret, vault_, cost);
                                secret->Clear();
                                if (!r) { Alert("Export failed.\n" + r.message); return; }
                                SetStatus("Exported to " + path + " under its own password.");
                            });
                        });
                },
                window_.get());
        },
        window_.get());
}

void PasswordApp::ExportCsv() {
    UltraCanvasAlert::Confirm(
        "A CSV file is NOT encrypted. Every password in it can be read by anyone and "
        "any program that gets the file. Use it only to import into another password "
        "manager, and delete it afterwards.\n\nExport as plain CSV anyway?",
        "Unencrypted export",
        [this](bool yes) {
            if (!yes) return;
            FileDialogOptions opts;
            opts.SetInitialDirectory(HomeDirectory());
    opts.SetTitle("Export as plain CSV")
                .SetDefaultFileName(vault_.GetName() + " passwords.csv")
                .AddFilter("CSV file", "csv")
                .SetRegisterAsRecent(false)
                .SetParentWindow(window_.get());
            UltraCanvasFileLoader::SaveFileDialog(opts, [this](DialogResult result, const std::string& path) {
                if (result != DialogResult::OK || path.empty() || !unlocked_) return;
                std::string error;
                if (!WriteCsvFile(path, vault_, error)) { Alert(error); return; }
                SetStatus("Plain CSV written to " + path + " - delete it when you are done.");
            });
        },
        window_.get());
}

void PasswordApp::Import() {
    if (!unlocked_) return;
    FileDialogOptions opts;
    opts.SetInitialDirectory(HomeDirectory());
    opts.SetTitle("Import passwords")
        .AddFilter("UltraPassword vault or CSV", std::vector<std::string>{kVaultExtension, "csv"})
        .AddFilter("UltraPassword vault", kVaultExtension)
        .AddFilter("CSV from a browser or password manager", "csv")
        .SetRegisterAsRecent(false)
        .SetParentWindow(window_.get());
    UltraCanvasFileLoader::OpenFileDialog(opts, [this](DialogResult result, const std::string& path) {
        if (result != DialogResult::OK || path.empty() || !unlocked_) return;
        std::string ext = PathToUtf8(PathFromUtf8(path).extension());
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".csv") ImportCsvFile(path);
        else ImportVaultFile(path);
    });
}

void PasswordApp::ImportVaultFile(const std::string& path) {
    UltraCanvasDialogManager::ShowInputDialog(
        "Password of \"" + FileNameOf(path) + "\":", "Import vault", "", InputType::Password,
        [this, path](DialogResult result, const std::string& typed) {
            if (result != DialogResult::OK || typed.empty()) return;
            auto secret = std::make_shared<UltraCryptSecureBuffer>(AdoptPassword(typed));
            SetStatus("Opening " + FileNameOf(path) + "…");
            RunAfterPaint([this, path, secret]() {
                PasswordVault other;
                VaultResult r = VaultFile::ReadFile(path, *secret, other);
                secret->Clear();
                if (!r) { Alert("Import failed.\n" + r.message); SetStatus(""); return; }
                const std::string name = "Imported - " + (other.GetName().empty() ? StemOf(path)
                                                                                   : other.GetName());
                const std::string group = vault_.ImportFrom(other, name);
                if (group.empty() || !SaveVault()) return;
                selectedGroup_ = group;
                selectedEntry_.clear();
                expandedNodes_.insert(GroupNode(group));
                RebuildTree();
                RebuildCards();
                SetStatus("Imported " + Plural(other.EntryCount(), "password", "passwords") +
                          " into \"" + name + "\".");
            });
        },
        window_.get());
}

void PasswordApp::ImportCsvFile(const std::string& path) {
    const std::string name = "Imported - " + StemOf(path);
    const std::string group = vault_.AddGroup(name, kRootGroupId);
    std::string error;
    const int added = ReadCsvFile(path, vault_, group, error);
    if (added < 0) {
        vault_.RemoveGroup(group);
        Alert("Import failed.\n" + error);
        return;
    }
    if (!SaveVault()) return;
    selectedGroup_ = group;
    selectedEntry_.clear();
    expandedNodes_.insert(GroupNode(group));
    RebuildTree();
    RebuildCards();
    SetStatus("Imported " + Plural(static_cast<size_t>(added), "password", "passwords") +
              " into \"" + name + "\". Consider deleting the CSV file now.");
}

void PasswordApp::ChangeMasterPassword() {
    if (!unlocked_) return;
    UltraCanvasDialogManager::ShowInputDialog(
        "New master password (at least " + std::to_string(kMinMasterPasswordLength) +
            " characters):",
        "Change master password", "", InputType::Password,
        [this](DialogResult result, const std::string& first) {
            if (result != DialogResult::OK) return;
            if (first.size() < kMinMasterPasswordLength) {
                Alert("The master password needs at least " +
                      std::to_string(kMinMasterPasswordLength) + " characters.");
                return;
            }
            auto secret = std::make_shared<UltraCryptSecureBuffer>(AdoptPassword(first));
            UltraCanvasDialogManager::ShowInputDialog(
                "Type the new master password again:", "Change master password", "",
                InputType::Password,
                [this, secret](DialogResult again, const std::string& second) {
                    if (again != DialogResult::OK) { secret->Clear(); return; }
                    if (!UltraCrypt_ConstantTimeEquals(secret->Data(), secret->GetSize(),
                                                       second.data(), second.size())) {
                        secret->Clear();
                        Alert("The two passwords are not the same. The master password "
                              "was not changed.");
                        return;
                    }
                    SetStatus("Re-encrypting the vault with the new password…");
                    RunAfterPaint([this, secret]() {
                        VaultResult r = file_.ChangePassword(*secret, vault_);
                        secret->Clear();
                        if (!r) { Alert("The master password was not changed.\n" + r.message); return; }
                        SetStatus("Master password changed. Exports made earlier still open "
                                  "with the old one.");
                    });
                },
                window_.get());
        },
        window_.get());
}

void PasswordApp::CopyToClipboard(const std::string& text, const std::string& what) {
    if (text.empty()) return;
    // A password goes out marked secret, so no clipboard history keeps it -
    // the desktop's included. The 30-second clear only empties the clipboard;
    // a history that had recorded it would keep it.
    const bool isPassword = what == "Password";
    if (!SetClipboardText(text, isPassword ? ClipboardHint::Secret : ClipboardHint::Normal)) {
        SetStatus("The clipboard is not available.", true);
        return;
    }
    if (isPassword) {
        WipeString(clipboardText_);
        clipboardText_ = text;
        clipboardClearAt_ = NowSeconds() + kClipboardSeconds;
        SetStatus("Password copied. The clipboard is cleared in " +
                  std::to_string(kClipboardSeconds) + " seconds.");
    } else {
        SetStatus(what + " copied.");
    }
}

void PasswordApp::OpenWebsite(const std::string& url) {
    std::string target = url;
    if (target.find("://") == std::string::npos) target = "https://" + target;
    OpenURL(target);
}

// ===========================================================================
// Housekeeping
// ===========================================================================
void PasswordApp::Tick() {
    const int64_t now = NowSeconds();
    if (clipboardClearAt_ != 0 && now >= clipboardClearAt_) ClearClipboardIfOurs();

    if (!unlocked_) {
        // Keep the lock screen's countdown honest after repeated failures.
        const uint32_t wait = throttle_.SecondsToWait(now);
        if (wait > 0) {
            startPage_.SetError("Too many wrong passwords. Try again in " +
                                Plural(wait, "second", "seconds") + ".");
            showingWait_ = true;
        } else if (showingWait_) {
            startPage_.SetError("");
            showingWait_ = false;
        }
        return;
    }
    if (lockPending_ && UltraCanvasDialogManager::GetActiveDialogCount() == 0) {
        Lock("Locked because the window was minimised.");
        return;
    }
    // A dialog that is open is not idleness: do not lock under a half-filled form.
    if (UltraCanvasDialogManager::GetActiveDialogCount() > 0) return;
    if (now - lastActivity_ >= kAutoLockSeconds)
        Lock("Locked after 5 minutes without input.");
}

void PasswordApp::NoteActivity() {
    lastActivity_ = NowSeconds();
}

void PasswordApp::ClearClipboardIfOurs() {
    if (clipboardText_.empty()) { clipboardClearAt_ = 0; return; }
    std::string current;
    if (GetClipboardText(current) && current == clipboardText_) {
        SetClipboardText("");
        if (unlocked_) SetStatus("Clipboard cleared.");
    }
    WipeString(current);
    WipeString(clipboardText_);
    clipboardClearAt_ = 0;
}

void PasswordApp::SetStatus(const std::string& text, bool isError) {
    if (!statusLabel_) return;
    statusLabel_->SetText(text);
    statusLabel_->SetTextColor(isError ? Theme::kDanger : Theme::kTextMuted);
}

void PasswordApp::Alert(const std::string& message, bool isError) {
    if (isError) UltraCanvasAlert::Error(message, "UltraPassword", nullptr, window_.get());
    else UltraCanvasAlert::Info(message, "UltraPassword", nullptr, window_.get());
}

void PasswordApp::RunAfterPaint(std::function<void()> work) {
    // Long enough for the event loop to repaint the busy state first.
    app_.StartTimer(60, false, [work](TimerId) { work(); });
}

} // namespace UltraPassword

// Apps/UltraDBAdmin/ui/DbAdminWindowObjects.cpp
// The window's second source: the tabs of open objects, the tree's
// right-click menu and the operations it starts (DbAdminOperations: a new
// table, a copy, a rename, emptying, dropping, maintenance, a schema dumped
// as SQL), the Search tab and the SQL log under the tabs, and the console's
// saved snippets. DbAdminWindow.cpp has the rest; DbAdminWindow.h says what
// the window is.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminWindow.h"

#include "DbAdminOperationDialogs.h"
#include "DbAdminSqlScript.h"
#include "DbAdminWindowParts.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasListSelection.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"

#include <filesystem>
#include <set>
#include <utility>

using namespace UltraCanvas;

namespace UltraDBAdmin {
using namespace window;

namespace {

SqlEngine EngineOf(const ConnectionSpec& spec) { return spec.IsSqlite() ? SqlEngine::Sqlite : SqlEngine::Postgres; }

// A chip's text: a long name ellipsized, so ten tabs still fit.
std::string ChipText(const std::string& label) { return CellText(UltraDbValue(label), 28); }

const char* KindTitle(UltraDbObjectKind kind) {
    switch (kind) {
        case UltraDbObjectKind::Table:            return "Table";
        case UltraDbObjectKind::View:             return "View";
        case UltraDbObjectKind::MaterializedView: return "Materialized view";
        case UltraDbObjectKind::ForeignTable:     return "Foreign table";
        case UltraDbObjectKind::Sequence:         return "Sequence";
    }
    return "Object";
}

std::string ProductionNote(const ConnectionSpec& spec) {
    return spec.production ? spec.name + " is a production connection. " : std::string();
}

} // namespace

// ===== OPEN OBJECTS =====

int DbAdminWindow::FindTab(const NodeRef& ref) const {
    for (size_t i = 0; i < openTabs_.size(); ++i)
        if (IsObject(openTabs_[i].ref, ref)) return static_cast<int>(i);
    return -1;
}

bool DbAdminWindow::TabSwitchBlocked() {
    if (!committing_ && !applying_ && !importing_) return false;
    SetStatus(committing_ ? "Rows are being committed: the tab changes once that has finished."
              : applying_ ? "The structure is being changed: the tab changes once that has finished."
                          : "Rows are being imported: the tab changes once that has finished.");
    return true;
}

size_t DbAdminWindow::StagedChanges(const ObjectTab& tab) const {
    const bool shown = activeTab_ >= 0 && &openTabs_[static_cast<size_t>(activeTab_)] == &tab;
    return shown ? edits_.Count() + structure_.Count() : tab.edits.Count() + tab.structure.Count();
}

size_t DbAdminWindow::StagedEverywhere() const {
    size_t total = 0;
    for (const auto& tab : openTabs_) total += StagedChanges(tab);
    if (activeTab_ < 0) total += edits_.Count() + structure_.Count();
    return total;
}

void DbAdminWindow::StashActiveTab() {
    if (activeTab_ < 0 || static_cast<size_t>(activeTab_) >= openTabs_.size()) return;
    FinishCellEdit(true);
    ObjectTab& tab = openTabs_[static_cast<size_t>(activeTab_)];
    tab.pager = pager_;
    tab.pageIndex = pageIndex_;
    tab.edits = edits_;
    tab.structure = structure_;
    tab.source = source_;
    tab.table = pager_ ? pager_->Table() : structure_.Table();
    tab.definition = definitionText_;
    tab.info = structureInfoText_;
    tab.loaded = tab.loaded && pager_ != nullptr;
}

void DbAdminWindow::OpenFromTree(const NodeRef& ref) {
    const int existing = FindTab(ref);
    if (existing >= 0 && existing == activeTab_) return;   // shown already
    if (TabSwitchBlocked()) return;
    if (existing >= 0) { ShowTab(existing); return; }
    StashActiveTab();
    // At the limit, the oldest tab with nothing staged makes room.
    if (openTabs_.size() >= kMaxTabs) {
        int drop = -1;
        for (size_t i = 0; i < openTabs_.size() && drop < 0; ++i)
            if (static_cast<int>(i) != activeTab_ && StagedChanges(openTabs_[i]) == 0) drop = static_cast<int>(i);
        if (drop < 0) {
            SetStatus(std::to_string(kMaxTabs) + " tables are open with changes staged: commit or revert one, or close "
                      "its tab, to open another.", true);
            return;
        }
        openTabs_.erase(openTabs_.begin() + drop);
        if (activeTab_ > drop) --activeTab_;
    }
    ObjectTab tab;
    tab.ref = ref;
    tab.label = ref.schema + "." + ref.name;
    openTabs_.push_back(std::move(tab));
    activeTab_ = static_cast<int>(openTabs_.size()) - 1;
    OpenObject(ref);
    RebuildObjectStrip();
}

void DbAdminWindow::ActivateTab(int index) {
    if (index == activeTab_ || TabSwitchBlocked()) return;
    ShowTab(index);
}

void DbAdminWindow::ShowTab(int index) {
    if (index < 0 || static_cast<size_t>(index) >= openTabs_.size()) return;
    StashActiveTab();
    activeTab_ = index;
    const ObjectTab& tab = openTabs_[static_cast<size_t>(index)];
    if (tab.loaded && tab.pager) RestoreObject(tab);
    else OpenObject(tab.ref);        // not read yet, a sequence, or its read failed: read it again
    RebuildObjectStrip();
}

void DbAdminWindow::RestoreObject(const ObjectTab& tab) {
    const ConnectionSpec* spec = store_.Find(tab.ref.connectionId);
    if (!spec) { ClearObject("The connection is gone."); return; }
    ClearObject("");
    objectConnectionId_ = spec->id;
    objectRef_ = tab.ref;
    objectLabel_ = tab.label;
    structure_ = tab.structure;
    source_ = tab.source;
    columnModel_->Replace(tab.table);
    constraintModel_->Replace(tab.table);
    definitionArea_->SetText(tab.definition, false);
    definitionText_ = tab.definition;
    structureInfo_->SetText(tab.info);
    structureInfoText_ = tab.info;
    pager_ = tab.pager;
    edits_ = tab.edits;
    // Its order, its filters and its page, as it was left.
    const auto& columns = pager_->Table().columns;
    for (size_t i = 0; i < columns.size(); ++i)
        if (columns[i].name == pager_->SortColumn()) browseView_->SetSortIndicator(static_cast<int>(i), pager_->SortAscending());
    FillFilterColumns();
    AfterStructureEdit();
    UpdateEditBar();
    LoadPage(tab.pageIndex);
    CountRows();
}

void DbAdminWindow::CloseTab(int index) {
    if (index < 0 || static_cast<size_t>(index) >= openTabs_.size() || TabSwitchBlocked()) return;
    StashActiveTab();
    const ObjectTab& tab = openTabs_[static_cast<size_t>(index)];
    const size_t staged = StagedChanges(tab);
    if (staged == 0) { DropTab(index); return; }
    const NodeRef ref = tab.ref;
    UltraCanvasDialogManager::ShowConfirmation(
        "Drop " + std::to_string(staged) + (staged == 1 ? " staged change" : " staged changes") + " to " + tab.label +
            " and close its tab? " + (staged == 1 ? "It has" : "None of them has") + " been written.",
        "Uncommitted changes",
        [this, ref](bool confirmed) {
            if (!confirmed) return;
            const int at = FindTab(ref);
            if (at >= 0) DropTab(at);
        },
        window_.get());
}

void DbAdminWindow::DropTab(int index) {
    if (index < 0 || static_cast<size_t>(index) >= openTabs_.size()) return;
    const bool shown = index == activeTab_;
    openTabs_.erase(openTabs_.begin() + index);
    if (shown) {
        activeTab_ = -1;             // what the window shows is the dropped tab's: not stashed
        if (openTabs_.empty()) {
            ClearObject("Pick a table or view in the tree.");
            RebuildObjectStrip();
        } else {
            ShowTab(std::min(index, static_cast<int>(openTabs_.size()) - 1));
        }
        return;
    }
    if (activeTab_ > index) --activeTab_;
    RebuildObjectStrip();
}

void DbAdminWindow::CloseTabsOf(const std::string& connectionId) {
    StashActiveTab();
    const bool shownGoes = activeTab_ >= 0 && openTabs_[static_cast<size_t>(activeTab_)].ref.connectionId == connectionId;
    NodeRef shown;
    if (activeTab_ >= 0 && !shownGoes) shown = openTabs_[static_cast<size_t>(activeTab_)].ref;
    openTabs_.erase(std::remove_if(openTabs_.begin(), openTabs_.end(),
                                   [&](const ObjectTab& tab) { return tab.ref.connectionId == connectionId; }),
                    openTabs_.end());
    if (shownGoes || activeTab_ < 0) {
        activeTab_ = -1;
        if (openTabs_.empty()) ClearObject("Pick a table or view in the tree.");
        else ShowTab(0);
    } else {
        activeTab_ = FindTab(shown);
    }
    RebuildObjectStrip();
}

void DbAdminWindow::RebuildObjectStrip() {
    if (stripPending_) return;
    auto* app = UltraCanvasApplicationBase::GetCurrent();
    if (!app) { BuildObjectStrip(); return; }
    stripPending_ = true;
    std::weak_ptr<char> alive = alive_;
    app->PostToUIThread([this, alive]() {
        if (alive.expired()) return;
        stripPending_ = false;
        BuildObjectStrip();
    });
}

void DbAdminWindow::BuildObjectStrip() {
    if (!objectStrip_) return;
    objectStrip_->ClearChildren();
    if (openTabs_.empty()) {
        objectStrip_->AddChild(MakeBarStatus("dbaObjectsNone", "A table picked in the tree opens here in a tab of its own."));
    }
    for (size_t i = 0; i < openTabs_.size(); ++i) {
        const ObjectTab& tab = openTabs_[i];
        const bool shown = static_cast<int>(i) == activeTab_;
        const size_t staged = StagedChanges(tab);
        const std::string id = "dbaObject" + std::to_string(i);
        const int index = static_cast<int>(i);
        auto chip = MakeActionButton(id, ChipText(tab.label) + (staged > 0 ? " *" : ""), "",
                                     std::string(KindTitle(tab.ref.objectKind)) + " " + tab.label +
                                         (staged > 0 ? " - " + std::to_string(staged) + " staged, not written" : ""),
                                     shown ? ButtonRole::Primary : ButtonRole::Standard);
        chip->SetOnClick([this, index]() { ActivateTab(index); });
        objectStrip_->AddChild(chip);
        auto close = MakeActionButton(id + "-close", "", "close", "Close this tab");
        close->SetPadding(4.0f, 4.0f);
        close->SetOnClick([this, index]() { CloseTab(index); });
        objectStrip_->AddChild(close);
        if (i + 1 < openTabs_.size()) objectStrip_->AddChild(MakeBarDivider(id + "-divider"));
    }
    objectStrip_->InvalidateLayout();
    objectStrip_->RequestRedraw();
}

// ===== TREE MENU =====

std::string DbAdminWindow::WriteBlockReason(const ConnectionSpec& spec) const {
    if (spec.readOnly) return "The connection is read only.";
    return std::string();
}

std::vector<MenuItemData> DbAdminWindow::MaintenanceItems(const NodeRef& ref, const std::string& schema,
                                                         const std::string& table) {
    std::vector<MenuItemData> items;
    const ConnectionSpec* spec = store_.Find(ref.connectionId);
    if (!spec) return items;
    const SqlEngine engine = EngineOf(*spec);
    const std::string blocked = WriteBlockReason(*spec);
    for (const Maintenance task : MaintenanceTasks(engine)) {
        // A table's maintenance is what it has: ANALYZE (and VACUUM on PostgreSQL).
        if (!table.empty() && task != Maintenance::Analyze && !(task == Maintenance::Vacuum && engine == SqlEngine::Postgres))
            continue;
        MenuItemData item = MenuItemData::Action(MaintenanceText(task),
                                                 [this, ref, schema, table, task]() { RunMaintenance(ref, schema, table, task); });
        item.tooltip = MaintenanceAbout(task, engine);
        const bool reads = task == Maintenance::IntegrityCheck || task == Maintenance::QuickCheck;
        if (!reads && !blocked.empty()) { item.enabled = false; item.tooltip = blocked; }
        items.push_back(item);
    }
    if (engine == SqlEngine::Sqlite && table.empty()) {
        std::vector<MenuItemData> modes;
        modes.push_back(MenuItemData::Action("Show the journal mode", [this, ref, schema]() { JournalMode(ref, schema, ""); }));
        modes.push_back(MenuItemData::Separator());
        for (const auto& mode : JournalModes()) {
            MenuItemData item = MenuItemData::Action(mode, [this, ref, schema, mode]() { JournalMode(ref, schema, mode); });
            if (!blocked.empty()) item.enabled = false;
            modes.push_back(item);
        }
        items.push_back(MenuItemData::Separator());
        items.push_back(MenuItemData::Submenu("Journal mode", modes));
    }
    return items;
}

void DbAdminWindow::ShowTreeMenu(TreeNode* node, const UCEvent& event) {
    if (!node || !window_) return;
    const NodeRef* found = RefOf(node->data.nodeId);
    if (!found || found->kind == NodeRef::Kind::Placeholder) return;
    const NodeRef ref = *found;
    const ConnectionSpec* spec = store_.Find(ref.connectionId);
    if (!spec) return;
    const bool sqlite = spec->IsSqlite();
    const std::string blocked = WriteBlockReason(*spec);
    const std::string nodeId = node->data.nodeId;
    // A connection's or a schema's actions act on the selection: the node
    // clicked becomes it. An object's act on the object alone.
    auto select = [this, nodeId]() {
        if (TreeNode* target = tree_->FindNode(nodeId)) tree_->SelectNode(target);
        selectedNode_ = nodeId;
        UpdateSqlBar();
        UpdateSearchBar();
    };
    auto writing = [&](MenuItemData item) {
        if (!blocked.empty()) { item.enabled = false; item.tooltip = blocked; }
        return item;
    };
    menu_ = std::make_shared<UltraCanvasMenu>("dbaTreeMenu", 0, 0, 240, 0);
    menu_->SetMenuType(MenuType::PopupMenu);

    if (ref.kind == NodeRef::Kind::Connection || ref.kind == NodeRef::Kind::Schema) {
        // SQLite's schema is its file's "main": the connection's actions are its.
        const bool schemaLevel = ref.kind == NodeRef::Kind::Schema || sqlite;
        NodeRef target = ref;
        if (ref.kind == NodeRef::Kind::Connection) target.schema = "main";
        const std::string what = ref.kind == NodeRef::Kind::Connection ? std::string("database") : std::string("schema");
        menu_->AddItem(MenuItemData::Action("Open in the SQL tab", IconPath("run"), [this, select]() {
            select();
            tabs_->SetActiveTab(kSqlTab);
            sqlEditor_->SetFocus(true);
        }));
        if (schemaLevel) {
            menu_->AddItem(writing(MenuItemData::Action("New table", IconPath("table-add"), [this, target]() { NewTable(target); })));
            menu_->AddItem(MenuItemData::Action("Search every table", IconPath("search"), [this, select]() {
                select();
                tabs_->SetActiveTab(kSearchTab);
                searchInput_->SetFocus(true);
            }));
            menu_->AddItem(MenuItemData::Action("Export the " + what + " as SQL", IconPath("export"),
                                                [this, target]() { DumpSchema(target); }));
            const std::vector<MenuItemData> maintenance = MaintenanceItems(target, target.schema, "");
            if (!maintenance.empty()) menu_->AddItem(MenuItemData::Submenu("Maintenance", IconPath("maintenance"), maintenance));
        } else {
            // PostgreSQL's maintenance runs on the database, from the connection.
            const std::vector<MenuItemData> maintenance = MaintenanceItems(ref, "", "");
            if (!maintenance.empty()) menu_->AddItem(MenuItemData::Submenu("Maintenance", IconPath("maintenance"), maintenance));
        }
        menu_->AddItem(MenuItemData::Separator());
        menu_->AddItem(MenuItemData::Action("Refresh", IconPath("refresh"), [this, select]() { select(); RefreshSelected(); }));
        if (ref.kind == NodeRef::Kind::Connection) {
            menu_->AddItem(MenuItemData::Action("Edit connection", IconPath("edit"), [this, select]() { select(); EditConnection(); }));
            menu_->AddItem(MenuItemData::Action("Remove connection", IconPath("delete"),
                                                [this, select]() { select(); RemoveConnection(); }));
        }
        menu_->AddItem(MenuItemData::Action("Copy name", IconPath("copy"), [this, ref]() { CopyName(ref); }));
    } else {
        const bool table = ref.objectKind == UltraDbObjectKind::Table;
        menu_->AddItem(MenuItemData::Action("Open", IconPath("tab-browse"), [this, ref]() {
            OpenFromTree(ref);
            tabs_->SetActiveTab(kBrowseTab);
        }));
        menu_->AddItem(MenuItemData::Action("Structure", IconPath("tab-structure"), [this, ref]() {
            OpenFromTree(ref);
            tabs_->SetActiveTab(kStructureTab);
        }));
        if (ref.objectKind != UltraDbObjectKind::Sequence)
            menu_->AddItem(MenuItemData::Action("Export rows", IconPath("export"), [this, ref]() { ExportFromTree(ref); }));
        menu_->AddItem(MenuItemData::Action("Copy name", IconPath("copy"), [this, ref]() { CopyName(ref); }));
        menu_->AddItem(MenuItemData::Separator());
        if (table) menu_->AddItem(writing(MenuItemData::Action("Copy table", IconPath("copy"), [this, ref]() { CopyTable(ref); })));
        if (table || !sqlite)
            menu_->AddItem(writing(MenuItemData::Action("Rename", IconPath("rename"), [this, ref]() { RenameObject(ref); })));
        if (table) {
            const std::vector<MenuItemData> maintenance = MaintenanceItems(ref, ref.schema, ref.name);
            if (!maintenance.empty()) menu_->AddItem(MenuItemData::Submenu("Maintenance", IconPath("maintenance"), maintenance));
            menu_->AddItem(MenuItemData::Separator());
            menu_->AddItem(writing(MenuItemData::Action("Empty table", IconPath("clear"), [this, ref]() { EmptyTable(ref); })));
        }
        menu_->AddItem(writing(MenuItemData::Action(std::string("Drop ") + KindWord(ref.objectKind), IconPath("delete"),
                                                    [this, ref]() { DropObject(ref); })));
    }
    PopupElementSettings settings;
    menu_->OpenMenu(event.pointerWindow, *window_, settings);
}

// ===== OPERATIONS =====

void DbAdminWindow::RunOperation(const std::string& connectionId, const std::string& what, const OperationPlan& plan,
                                 std::function<void(const UltraDbResult&, const UltraDbResultSet&)> done) {
    const ConnectionSpec* spec = store_.Find(connectionId);
    if (!spec) return;
    const std::string connection = RegisteredName(*spec);
    SetStatus(what + "…");
    // One job, so no page load of the window lands inside its transaction.
    Background([this, connection, plan, done = std::move(done)]() -> std::function<void()> {
        UltraDbResultSet rows;
        size_t failed = 0;
        const UltraDbResult result = RunPlan(connection, plan, &rows, failed);
        std::string failedSql;
        if (!result && failed < plan.statements.size()) failedSql = plan.statements[failed];
        return [this, result, rows, failedSql, done]() {
            if (!result) {
                SetStatus("Not done: " + OneLine(result.message), true);
                if (!failedSql.empty()) statusLabel_->SetTooltip(failedSql + "\n\n" + result.message);
            }
            if (done) done(result, rows);
        };
    });
}

void DbAdminWindow::ConfirmOperation(const std::string& connectionId, const std::string& title, const std::string& intro,
                                     const OperationPlan& plan, const std::string& action, ButtonRole role,
                                     std::function<void(const UltraDbResult&, const UltraDbResultSet&)> done) {
    const ConnectionSpec* spec = store_.Find(connectionId);
    if (!spec) return;
    ShowPlanDialog(window_.get(), title, ProductionNote(*spec) + intro, PlanText(plan), action, role,
                   [this, connectionId, title, plan, done]() { RunOperation(connectionId, title, plan, done); });
}

void DbAdminWindow::ReadTableThen(const NodeRef& ref,
                                  std::function<void(const UltraDbTableSchema&, const SqliteTableSource&)> then) {
    const ConnectionSpec* spec = store_.Find(ref.connectionId);
    if (!spec) return;
    const std::string connection = RegisteredName(*spec);
    const bool sqliteTable = spec->IsSqlite() && ref.objectKind == UltraDbObjectKind::Table;
    const std::string schema = ref.schema, name = ref.name;
    SetStatus("Reading " + schema + "." + name + "…");
    Background([this, connection, schema, name, sqliteTable, then = std::move(then)]() -> std::function<void()> {
        UltraDbTableSchema table;
        UltraDbResult result = UltraDb_DescribeTable(connection, schema, name, table);
        SqliteTableSource source;
        if (result && sqliteTable) result = ReadSqliteTableSource(connection, schema, name, source);
        return [this, result, table, source, then]() {
            if (!result) { SetStatus(result.message, true); return; }
            SetStatus(std::string());
            then(table, source);
        };
    });
}

void DbAdminWindow::ReloadSchemaNode(const std::string& connectionId, const std::string& schema) {
    std::string schemaNode;
    for (const auto& [id, node] : nodes_)
        if (node.kind == NodeRef::Kind::Schema && node.connectionId == connectionId && node.schema == schema) schemaNode = id;
    if (schemaNode.empty()) return;
    ResetNode(schemaNode);
    if (TreeNode* tree = tree_->FindNode(schemaNode)) tree_->ExpandNode(tree);
    LoadChildren(schemaNode);
}

void DbAdminWindow::NewTable(const NodeRef& ref) {
    const ConnectionSpec* spec = store_.Find(ref.connectionId);
    if (!spec) return;
    const std::string blocked = WriteBlockReason(*spec);
    if (!blocked.empty()) { SetStatus(blocked); return; }
    const SqlEngine engine = EngineOf(*spec);
    const std::string schema = ref.schema;
    const std::string id = spec->id;
    ShowNewTableDialog(
        window_.get(), engine, schema,
        [engine, schema](const std::string& name, const std::vector<NewColumn>& columns, std::string& text) {
            OperationPlan plan;
            if (!CreateTablePlan(engine, schema, name, columns, plan, text)) return false;
            text = PlanText(plan);
            return true;
        },
        [this, engine, schema, id](const std::string& name, const std::vector<NewColumn>& columns) -> std::string {
            OperationPlan plan;
            std::string error;
            if (!CreateTablePlan(engine, schema, name, columns, plan, error)) return error;
            RunOperation(id, "Creating " + name, plan, [this, id, schema, name](const UltraDbResult& result, const UltraDbResultSet&) {
                if (!result) return;
                ReloadSchemaNode(id, schema);
                SetStatus("Table " + schema + "." + name + " created. Its structure can be changed in Structure.");
            });
            return std::string();
        });
}

void DbAdminWindow::CopyTable(const NodeRef& ref) {
    ReadTableThen(ref, [this, ref](const UltraDbTableSchema& table, const SqliteTableSource& source) {
        const ConnectionSpec* spec = store_.Find(ref.connectionId);
        if (!spec) return;
        const SqlEngine engine = EngineOf(*spec);
        OperationDialogSpec dialog;
        dialog.title = "Copy " + ref.schema + "." + ref.name;
        dialog.intro = ProductionNote(*spec) + "A new table of the same definition in the same schema" +
                       (engine == SqlEngine::Sqlite ? " - its CREATE TABLE as written, and its indexes."
                                                    : " - LIKE ... INCLUDING ALL: defaults, constraints, indexes, "
                                                      "identity and comments.");
        dialog.nameLabel = "New name";
        dialog.name = ref.name + "_copy";
        dialog.options = {{"With its rows", true}};
        dialog.optionTips = {"Copy every row too; without, the copy is empty"};
        dialog.action = "Copy table";
        auto plan = [engine, table, source](const OperationChoice& choice, OperationPlan& out, std::string& error) {
            return CopyTablePlan(engine, table, source, choice.name, !choice.options.empty() && choice.options[0], out, error);
        };
        const std::string id = ref.connectionId;
        const std::string schema = ref.schema;
        ShowOperationDialog(
            window_.get(), dialog,
            [plan](const OperationChoice& choice, std::string& text) {
                OperationPlan out;
                if (!plan(choice, out, text)) return false;
                text = PlanText(out);
                return true;
            },
            [this, plan, id, schema](const OperationChoice& choice) -> std::string {
                OperationPlan out;
                std::string error;
                if (!plan(choice, out, error)) return error;
                const std::string name = choice.name;
                RunOperation(id, "Copying to " + name, out, [this, id, schema, name](const UltraDbResult& result, const UltraDbResultSet&) {
                    if (!result) return;
                    ReloadSchemaNode(id, schema);
                    SetStatus("Copied to " + schema + "." + name + ".");
                });
                return std::string();
            });
    });
}

void DbAdminWindow::RenameObject(const NodeRef& ref) {
    const ConnectionSpec* spec = store_.Find(ref.connectionId);
    if (!spec) return;
    const int tab = FindTab(ref);
    if (tab >= 0) StashActiveTab();
    if (tab >= 0 && StagedChanges(openTabs_[static_cast<size_t>(tab)]) > 0) {
        SetStatus("Commit or revert the changes staged to " + ref.schema + "." + ref.name + " first.");
        return;
    }
    const SqlEngine engine = EngineOf(*spec);
    OperationDialogSpec dialog;
    dialog.title = "Rename " + ref.schema + "." + ref.name;
    dialog.intro = ProductionNote(*spec) + "What refers to it by name elsewhere - a query in a program, a function - "
                                           "is not changed.";
    dialog.nameLabel = "New name";
    dialog.name = ref.name;
    dialog.action = "Rename";
    ShowOperationDialog(
        window_.get(), dialog,
        [engine, ref](const OperationChoice& choice, std::string& text) {
            OperationPlan plan;
            if (!RenameObjectPlan(engine, ref.schema, ref.name, ref.objectKind, choice.name, plan, text)) return false;
            text = PlanText(plan);
            return true;
        },
        [this, engine, ref](const OperationChoice& choice) -> std::string {
            OperationPlan plan;
            std::string error;
            if (!RenameObjectPlan(engine, ref.schema, ref.name, ref.objectKind, choice.name, plan, error)) return error;
            const std::string name = choice.name;
            RunOperation(ref.connectionId, "Renaming " + ref.name, plan,
                         [this, ref, name](const UltraDbResult& result, const UltraDbResultSet&) {
                             if (!result) return;
                             const int at = FindTab(ref);
                             if (at >= 0) DropTab(at);     // it showed the old name; nothing was staged
                             ReloadSchemaNode(ref.connectionId, ref.schema);
                             SetStatus(ref.schema + "." + ref.name + " is now " + name + ".");
                         });
            return std::string();
        });
}

void DbAdminWindow::EmptyTable(const NodeRef& ref) {
    const int tab = FindTab(ref);
    if (tab >= 0) StashActiveTab();
    if (tab >= 0 && StagedChanges(openTabs_[static_cast<size_t>(tab)]) > 0) {
        SetStatus("Commit or revert the changes staged to " + ref.schema + "." + ref.name + " first.");
        return;
    }
    ReadTableThen(ref, [this, ref](const UltraDbTableSchema& table, const SqliteTableSource& source) {
        const ConnectionSpec* spec = store_.Find(ref.connectionId);
        if (!spec) return;
        const SqlEngine engine = EngineOf(*spec);
        bool numbered = engine == SqlEngine::Sqlite ? source.sequence >= 0 : false;
        for (const auto& column : table.columns) numbered = numbered || (engine == SqlEngine::Postgres && column.autoIncrement);
        OperationDialogSpec dialog;
        dialog.title = "Empty " + ref.schema + "." + ref.name;
        dialog.intro = ProductionNote(*spec) + "Every row of the table goes" +
                       (table.approxRows >= 0 ? " - about " + Grouped(table.approxRows) + " of them" : std::string()) +
                       ". The table, its columns and indexes stay.";
        if (numbered) {
            dialog.options = {{"Number new rows from 1 again", false}};
            dialog.optionTips = {engine == SqlEngine::Sqlite ? "Reset the AUTOINCREMENT counter in sqlite_sequence"
                                                             : "TRUNCATE ... RESTART IDENTITY"};
        }
        dialog.action = "Empty table";
        dialog.role = ButtonRole::Danger;
        auto plan = [engine, ref](const OperationChoice& choice) {
            return EmptyTablePlan(engine, ref.schema, ref.name, !choice.options.empty() && choice.options[0]);
        };
        ShowOperationDialog(
            window_.get(), dialog,
            [plan](const OperationChoice& choice, std::string& text) {
                text = PlanText(plan(choice));
                return true;
            },
            [this, plan, ref](const OperationChoice& choice) -> std::string {
                RunOperation(ref.connectionId, "Emptying " + ref.name, plan(choice),
                             [this, ref](const UltraDbResult& result, const UltraDbResultSet&) {
                                 if (!result) return;
                                 if (FindTab(ref) == activeTab_ && activeTab_ >= 0 && pager_) {
                                     LoadPage(0);
                                     CountRows();
                                 }
                                 SetStatus(ref.schema + "." + ref.name + " is empty.");
                             });
                return std::string();
            });
    });
}

void DbAdminWindow::DropObject(const NodeRef& ref) {
    const ConnectionSpec* spec = store_.Find(ref.connectionId);
    if (!spec) return;
    const int tab = FindTab(ref);
    if (tab >= 0) StashActiveTab();
    const size_t staged = tab >= 0 ? StagedChanges(openTabs_[static_cast<size_t>(tab)]) : 0;
    const OperationPlan plan = DropObjectPlan(EngineOf(*spec), ref.schema, ref.name, ref.objectKind);
    std::string intro = std::string("The ") + KindWord(ref.objectKind) + " " + ref.schema + "." + ref.name +
                        " goes for good: nothing here brings it back.";
    if (staged > 0)
        intro += " Its tab closes, and the " + std::to_string(staged) + (staged == 1 ? " change" : " changes") +
                 " staged to it with it.";
    ConfirmOperation(ref.connectionId, "Drop " + ref.schema + "." + ref.name, intro, plan,
                     std::string("Drop ") + KindWord(ref.objectKind), ButtonRole::Danger,
                     [this, ref](const UltraDbResult& result, const UltraDbResultSet&) {
                         if (!result) return;
                         const int at = FindTab(ref);
                         if (at >= 0) DropTab(at);
                         ReloadSchemaNode(ref.connectionId, ref.schema);
                         SetStatus(ref.schema + "." + ref.name + " dropped.");
                     });
}

void DbAdminWindow::RunMaintenance(const NodeRef& ref, const std::string& schema, const std::string& table,
                                   Maintenance task) {
    const ConnectionSpec* spec = store_.Find(ref.connectionId);
    if (!spec) return;
    const SqlEngine engine = EngineOf(*spec);
    const OperationPlan plan = MaintenancePlan(engine, task, schema, table);
    const std::string where = table.empty() ? (schema.empty() ? spec->name : schema) : schema + "." + table;
    const std::string what = std::string(MaintenanceText(task)) + " " + where;
    auto done = [this, task, where](const UltraDbResult& result, const UltraDbResultSet& rows) {
        if (!result) return;
        if (task == Maintenance::IntegrityCheck || task == Maintenance::QuickCheck) {
            std::string findings;
            for (size_t i = 0; i < rows.Size(); ++i) findings += rows.Row(i)[0].AsString() + "\n";
            if (rows.Size() == 1 && rows.Row(0)[0].AsString() == "ok") {
                SetStatus(std::string(MaintenanceText(task)) + " of " + where + ": ok - nothing is damaged.");
                return;
            }
            SetStatus(std::string(MaintenanceText(task)) + " of " + where + " found problems.", true);
            ShowTextDialog(window_.get(), std::string(MaintenanceText(task)) + " of " + where,
                           "SQLite reports what it found damaged, a line each. A backup taken before the damage, "
                           "or sqlite3's .recover, is the way back.",
                           findings);
            return;
        }
        SetStatus(std::string(MaintenanceText(task)) + " of " + where + " done.");
    };
    // VACUUM rewrites a SQLite file: said first, as it holds the database.
    if (task == Maintenance::Vacuum && engine == SqlEngine::Sqlite) {
        ConfirmOperation(ref.connectionId, "Vacuum " + where, std::string(MaintenanceAbout(task, engine)) + ".", plan,
                         "Vacuum", ButtonRole::Primary, done);
        return;
    }
    RunOperation(ref.connectionId, what, plan, done);
}

void DbAdminWindow::JournalMode(const NodeRef& ref, const std::string& schema, const std::string& mode) {
    OperationPlan plan;
    std::string error;
    if (!JournalModePlan(schema, mode, plan, error)) { SetStatus(error, true); return; }
    auto done = [this, schema](const UltraDbResult& result, const UltraDbResultSet& rows) {
        if (!result) return;
        const std::string now = rows.Size() == 1 ? rows.Row(0)[0].AsString() : std::string("?");
        SetStatus("The journal mode of " + schema + " is " + now + ".");
    };
    if (!plan.notes.empty()) {
        ConfirmOperation(ref.connectionId, "Journal mode " + mode, plan.notes.front(), plan, "Set " + mode,
                         ButtonRole::Danger, done);
        return;
    }
    RunOperation(ref.connectionId, mode.empty() ? std::string("Reading the journal mode") : "Setting the journal mode to " + mode,
                 plan, done);
}

void DbAdminWindow::DumpSchema(const NodeRef& ref) {
    const ConnectionSpec* spec = store_.Find(ref.connectionId);
    if (!spec || exporting_) return;
    const SqlEngine engine = EngineOf(*spec);
    const std::string schema = ref.schema.empty() ? std::string("main") : ref.schema;
    OperationDialogSpec dialog;
    dialog.title = "Export " + (engine == SqlEngine::Sqlite ? spec->name : schema) + " as SQL";
    dialog.intro = engine == SqlEngine::Sqlite
                       ? "Every table with its rows, indexes, triggers and views, as sqlite3's .dump writes them: the "
                         "file rebuilds the database - in Import SQL, or sqlite3."
                       : "Every table of " + schema + " with its rows, keys, constraints, indexes and comments, and its "
                         "views. Functions, triggers and types are pg_dump's to write.";
    dialog.options = {{"Structure (CREATE statements)", true}, {"Rows (INSERT statements)", true},
                      {"Drop what is there first (DROP ... IF EXISTS)", false}};
    dialog.optionTips = {"", "", "So the file can be run over a database that already has the tables"};
    dialog.action = "Choose a file";
    auto options = [](const OperationChoice& choice) {
        DumpOptions out;
        out.structure = choice.options.size() > 0 && choice.options[0];
        out.data = choice.options.size() > 1 && choice.options[1];
        out.dropFirst = choice.options.size() > 2 && choice.options[2];
        return out;
    };
    const std::string id = spec->id;
    const std::string base = engine == SqlEngine::Sqlite
                                 ? PathToUtf8(PathFromUtf8(spec->database).stem())
                                 : schema;
    ShowOperationDialog(
        window_.get(), dialog,
        [options, engine](const OperationChoice& choice, std::string& text) {
            const DumpOptions chosen = options(choice);
            if (!chosen.structure && !chosen.data) { text = "Tick the structure, the rows, or both."; return false; }
            text = std::string("-- The file holds, in this order:\n") +
                   (engine == SqlEngine::Sqlite ? "PRAGMA foreign_keys=OFF;\nBEGIN TRANSACTION;\n" : "BEGIN;\n") +
                   (chosen.dropFirst ? "DROP ... IF EXISTS ...;   -- every view and table first\n" : "") +
                   (chosen.structure ? "CREATE TABLE ...;          -- each table, then its rows\n" : "") +
                   (chosen.data ? "INSERT INTO ... VALUES (...);\n" : "") +
                   (chosen.structure ? "CREATE INDEX / TRIGGER / VIEW ...;\n" : "") + "COMMIT;\n";
            return true;
        },
        [this, options, id, schema, base, engine](const OperationChoice& choice) -> std::string {
            const DumpOptions chosen = options(choice);
            if (!chosen.structure && !chosen.data) return "Tick the structure, the rows, or both.";
            // The Save dialog once this one has closed.
            auto* app = UltraCanvasApplicationBase::GetCurrent();
            if (!app) return std::string();
            std::weak_ptr<char> alive = alive_;
            app->PostToUIThread([this, alive, chosen, id, schema, base, engine]() {
                if (alive.expired()) return;
                FileDialogOptions save;
                save.SetTitle("Export as SQL").SetDefaultFileName(base + ".sql").SetParentWindow(window_.get());
                save.AddFilter("SQL scripts (.sql)", std::vector<std::string>{"sql"});
                UltraCanvasFileLoader::SaveFileDialog(save, [this, chosen, id, schema, engine](DialogResult result,
                                                                                              const std::string& path) {
                    if (result != DialogResult::OK || path.empty() || exporting_) return;
                    const ConnectionSpec* spec = store_.Find(id);
                    if (!spec) return;
                    exporting_ = true;
                    UpdateExportButtons();
                    const std::string connection = RegisteredName(*spec);
                    const std::string name = PathToUtf8(PathFromUtf8(path).filename());
                    SetStatus("Exporting " + schema + " to " + name + "…");
                    std::weak_ptr<char> alive = alive_;
                    auto progress = [this, alive, name](const std::string& table, size_t rows) {
                        auto* app = UltraCanvasApplicationBase::GetCurrent();
                        if (!app || alive.expired()) return;
                        app->PostToUIThread([this, alive, name, table, rows]() {
                            if (!alive.expired() && exporting_)
                                SetStatus("Exporting to " + name + "… " + table + ", " +
                                          Grouped(static_cast<int64_t>(rows)) + " rows");
                        });
                    };
                    const std::shared_ptr<std::atomic<bool>> stop = exportStop_;
                    Background([this, connection, engine, schema, path, chosen, stop, progress, name]() -> std::function<void()> {
                        size_t tables = 0, rows = 0;
                        const UltraDbResult result =
                            UltraDBAdmin::DumpSchema(connection, engine, schema, path, chosen, tables, rows, stop.get(), progress);
                        return [this, result, tables, rows, path, name]() {
                            exporting_ = false;
                            UpdateExportButtons();
                            if (!result) { SetStatus("Not exported: " + OneLine(result.message), true); return; }
                            std::error_code ec;
                            const auto size = std::filesystem::file_size(PathFromUtf8(path), ec);
                            SetStatus(std::to_string(tables) + (tables == 1 ? " table, " : " tables, ") +
                                      Grouped(static_cast<int64_t>(rows)) + (rows == 1 ? " row" : " rows") +
                                      " exported to " + name +
                                      (ec ? std::string(".") : " (" + HumanSize(static_cast<int64_t>(size)) + ")."));
                        };
                    });
                });
            });
            return std::string();
        });
}

void DbAdminWindow::ExportFromTree(const NodeRef& ref) {
    if (exporting_) return;
    const int tab = FindTab(ref);
    if (tab >= 0 && tab == activeTab_ && pager_) { ExportRows(ExportSource::Table); return; }
    exportWhenOpen_ = true;
    whenOpenRef_ = ref;
    OpenFromTree(ref);
    if (FindTab(ref) != activeTab_ || activeTab_ < 0) { exportWhenOpen_ = false; return; }   // not opened
    // Shown again from its tab, it was not read anew: export it now.
    if (exportWhenOpen_ && pager_ && IsObject(objectRef_, ref)) {
        exportWhenOpen_ = false;
        ExportRows(ExportSource::Table);
    }
}

void DbAdminWindow::CopyName(const NodeRef& ref) {
    std::string text;
    if (ref.kind == NodeRef::Kind::Object) text = QualifiedName(ref.schema, ref.name);
    else if (ref.kind == NodeRef::Kind::Schema) text = QuotedName(ref.schema);
    else if (const ConnectionSpec* spec = store_.Find(ref.connectionId)) text = spec->name;
    if (text.empty()) return;
    if (!SetClipboardText(text)) { SetStatus("The clipboard cannot be reached.", true); return; }
    SetStatus(text + " copied.");
}

// ===== SEARCH =====

std::shared_ptr<UltraCanvasContainer> DbAdminWindow::BuildSearchPage() {
    auto page = MakeTabPage("dbaSearchPage");
    auto bar = MakeToolbar("dbaSearchBar");
    searchInput_ = CreateTextInput("dbaSearchText", 0, 0, 240, kControlHeight);
    searchInput_->layoutItem.SetFlexShrink(0);
    searchInput_->SetFollowTheme(true);
    searchInput_->SetShowValidationState(false);
    searchInput_->SetPlaceholder("text to find");
    searchInput_->onEnterPressed = [this](const std::string&) { StartSearch(); return true; };
    bar->AddChild(searchInput_);

    searchMode_ = CreateDropdown("dbaSearchMode", 0, 0, 150, kControlHeight);
    searchMode_->layoutItem.SetFlexShrink(0);
    searchMode_->SetFollowTheme(true);
    searchMode_->AddItem("contains");
    searchMode_->AddItem("is exactly");
    searchMode_->SetSelectedIndex(0, false);
    searchMode_->SetTooltip("Contains: anywhere in the value, in any case. Is exactly: the whole value, case and all.");
    bar->AddChild(searchMode_);

    searchButton_ = MakeActionButton("dbaSearchRun", "Search", "search",
                                     "Look for the text in every table and view of the schema picked in the tree (Enter)",
                                     ButtonRole::Primary);
    searchButton_->SetOnClick([this]() { StartSearch(); });
    bar->AddChild(searchButton_);
    searchStopButton_ = MakeActionButton("dbaSearchStop", "Stop", "stop", "Stop after the table being searched now",
                                         ButtonRole::Danger);
    searchStopButton_->SetOnClick([this]() { StopSearch("Stopped"); });
    bar->AddChild(searchStopButton_);
    searchInfo_ = MakeBarStatus("dbaSearchInfo");
    bar->AddChild(searchInfo_);
    page->AddChild(bar);
    PinToolbar(bar);

    searchModel_ = std::make_shared<SearchHitModel>();
    searchView_ = MakeList("dbaSearchHits", searchModel_);
    searchView_->onItemDoubleClicked = [this](int row) { OpenSearchHit(row); };
    FillWith(page, searchView_);
    return page;
}

bool DbAdminWindow::SearchScope(std::string& connectionId, std::string& schema) const {
    const NodeRef* ref = RefOf(selectedNode_);
    if (!ref || ref->kind == NodeRef::Kind::Placeholder) return false;
    const ConnectionSpec* spec = store_.Find(ref->connectionId);
    if (!spec) return false;
    connectionId = spec->id;
    if (ref->kind == NodeRef::Kind::Connection) {
        if (!spec->IsSqlite()) return false;          // a server has schemas: one must be picked
        schema = "main";
        return true;
    }
    schema = ref->schema;
    return true;
}

void DbAdminWindow::UpdateSearchBar() {
    if (!searchButton_) return;
    std::string connectionId, schema;
    const bool scoped = SearchScope(connectionId, schema);
    searchButton_->SetDisabled(searching_ || !scoped);
    searchStopButton_->SetDisabled(!searching_);
    if (searching_) return;                 // the search says where it is
    const ConnectionSpec* spec = scoped ? store_.Find(connectionId) : nullptr;
    std::string text = spec ? "In every table of " + schema + " (" + spec->name + ")"
                            : std::string("Pick a schema in the tree, or anything in it.");
    if (!searchSummary_.empty()) text = searchSummary_ + " - " + text;
    searchInfo_->SetText(text);
    searchInfo_->SetTooltip(text);
    searchInfo_->SetTextColor(StatusColor());
}

void DbAdminWindow::StartSearch() {
    if (searching_) return;
    const std::string text = searchInput_->GetText();
    if (text.empty()) { searchInfo_->SetText("Type the text to find."); return; }
    std::string connectionId, schema;
    if (!SearchScope(connectionId, schema)) { UpdateSearchBar(); return; }
    const ConnectionSpec* spec = store_.Find(connectionId);
    if (!spec) return;
    searching_ = true;
    const uint64_t generation = ++searchGen_;
    searchConnectionId_ = connectionId;
    searchSchema_ = schema;
    searchText_ = text;
    searchModeValue_ = searchMode_->GetSelectedIndex() == 1 ? SearchMode::Exactly : SearchMode::Contains;
    searchTargets_.clear();
    searchIndex_ = 0;
    searchFailures_.clear();
    searchSummary_.clear();
    searchModel_->Clear();
    UpdateSearchBar();
    searchInfo_->SetText("Listing the tables of " + schema + "…");
    const std::string connection = RegisteredName(*spec);
    Background([this, connection, schema, generation]() -> std::function<void()> {
        std::vector<UltraDbObjectSummary> targets;
        const UltraDbResult result = SearchTargets(connection, schema, targets);
        return [this, result, targets, generation]() {
            if (generation != searchGen_) return;
            if (!result) { StopSearch("Not searched: " + OneLine(result.message)); return; }
            searchTargets_ = targets;
            SearchNext(generation);
        };
    });
}

void DbAdminWindow::SearchNext(uint64_t generation) {
    if (generation != searchGen_) return;
    if (searchIndex_ >= searchTargets_.size()) {
        StopSearch("Done");
        return;
    }
    const UltraDbObjectSummary target = searchTargets_[searchIndex_++];
    const ConnectionSpec* spec = store_.Find(searchConnectionId_);
    if (!spec) { StopSearch("The connection is gone"); return; }
    searchInfo_->SetText("Searching " + target.name + " (" + std::to_string(searchIndex_) + " of " +
                         std::to_string(searchTargets_.size()) + ")…");
    searchInfo_->SetTextColor(StatusColor());
    const std::string connection = RegisteredName(*spec);
    const SqlEngine engine = EngineOf(*spec);
    const std::string schema = searchSchema_, text = searchText_;
    const SearchMode mode = searchModeValue_;
    // One job per table: a Browse page waits for one table at most.
    Background([this, connection, engine, schema, target, text, mode, generation]() -> std::function<void()> {
        UltraDbTableSchema table;
        std::vector<SearchHit> hits;
        UltraDbResult result = UltraDb_DescribeTable(connection, schema, target.name, table);
        if (result) result = SearchTable(connection, engine, table, text, mode, hits);
        return [this, result, hits, target, generation]() {
            if (generation != searchGen_) return;
            if (!result) searchFailures_.push_back(target.name + ": " + OneLine(result.message));
            searchModel_->Append(hits);
            SearchNext(generation);
        };
    });
}

void DbAdminWindow::StopSearch(const std::string& why) {
    if (!searching_) return;
    ++searchGen_;                       // what is still on its way is dropped
    searching_ = false;
    std::set<std::string> tables;
    for (int i = 0; i < searchModel_->GetRowCount(); ++i) tables.insert(searchModel_->At(i)->table);
    const int hits = searchModel_->GetRowCount();
    searchSummary_ = why + ": " + (hits == 0 ? std::string("not found")
                                             : std::to_string(hits) + (hits == 1 ? " column" : " columns") + " in " +
                                                   std::to_string(tables.size()) + (tables.size() == 1 ? " table" : " tables"));
    if (!searchFailures_.empty())
        searchSummary_ += ", " + std::to_string(searchFailures_.size()) + " not searched (" + searchFailures_.front() + ")";
    UpdateSearchBar();
}

void DbAdminWindow::OpenSearchHit(int row) {
    const SearchHit* hit = searchModel_->At(row);
    if (!hit) return;
    NodeRef ref;
    ref.kind = NodeRef::Kind::Object;
    ref.connectionId = searchConnectionId_;
    ref.schema = hit->schema;
    ref.name = hit->table;
    ref.objectKind = hit->kind;
    openFilter_ = ColumnFilter();
    openFilter_.column = hit->column;
    openFilter_.op = searchModeValue_ == SearchMode::Exactly ? FilterOp::Equals : FilterOp::Contains;
    openFilter_.value = searchText_;
    filterWhenOpen_ = true;
    whenOpenRef_ = ref;
    OpenFromTree(ref);
    if (FindTab(ref) != activeTab_ || activeTab_ < 0) { filterWhenOpen_ = false; return; }   // not opened
    // Shown again from its tab, it was not read anew: filter it now.
    if (filterWhenOpen_ && pager_ && IsObject(objectRef_, ref)) {
        filterWhenOpen_ = false;
        auto filtered = std::make_shared<TablePager>(*pager_);
        std::string error;
        if (filtered->SetFilter(openFilter_, error)) ReplacePager(filtered);
        FillFilterColumns();
    }
    tabs_->SetActiveTab(kBrowseTab);
}

// ===== SQL LOG =====

std::shared_ptr<UltraCanvasContainer> DbAdminWindow::BuildLogPanel() {
    auto pane = CreateContainer("dbaLogPane", 0, 0, 0, 0);
    MakePlainColumn(pane);
    MakeCard(*pane);
    pane->SetPadding(4.0f, 6.0f);
    pane->layout.SetFlexGap(2);
    auto bar = MakeToolbar("dbaLogBar", 30);
    auto heading = CreateLabel("dbaLogHeading", 0, 0, 0, 22, "SQL LOG");
    heading->SetFontSize(10.5f);
    heading->SetFontWeight(FontWeight::Bold);
    heading->SetTextColor(StatusColor());
    heading->layoutItem.SetFlexShrink(0);
    heading->SetMargin(0.0f, 4.0f);
    bar->AddChild(heading);
    logInfo_ = MakeBarStatus("dbaLogInfo", "Every statement the app runs, newest at the bottom; bound values are not shown.");
    bar->AddChild(logInfo_);
    logSchemaReads_ = UltraCanvasCheckbox::CreateCheckbox("dbaLogSchema", 0, 0, 136, 24, "Schema reads", false);
    logSchemaReads_->layoutItem.SetFlexShrink(0);
    logSchemaReads_->SetTooltip("Also the tree's and Structure's reads of the catalog");
    logSchemaReads_->onChecked = [this]() { RefillLog(); };
    logSchemaReads_->onUnchecked = [this]() { RefillLog(); };
    bar->AddChild(logSchemaReads_);
    auto copy = MakeActionButton("dbaLogCopy", "Copy", "copy",
                                 "The selected statements - or all of them - as SQL, each under a comment line");
    copy->SetOnClick([this]() { CopyLog(); });
    bar->AddChild(copy);
    auto clear = MakeActionButton("dbaLogClear", "Clear", "clear", "Empty the log");
    clear->SetOnClick([this]() {
        log_->Clear();
        logModel_->Clear();
        shownSeq_ = log_->LastSeq();
    });
    bar->AddChild(clear);
    pane->AddChild(bar);
    PinToolbar(bar);

    logModel_ = std::make_shared<LogModel>();
    logModel_->SetConnectionNames([this](const std::string& registered) { return ConnectionLabel(registered); });
    logView_ = MakeList("dbaLog", logModel_);
    logView_->SetSelection(std::make_shared<UltraCanvasMultiSelection>());
    // A statement double-clicked goes into the console's editor, to run again.
    logView_->onItemDoubleClicked = [this](int row) {
        const LogEntry* entry = logModel_->At(row);
        if (!entry || entry->kind == UltraDbStatementEvent::Kind::Introspection) return;
        sqlEditor_->SetText(entry->sql);
        tabs_->SetActiveTab(kSqlTab);
    };
    FillWith(pane, logView_);
    return pane;
}

std::string DbAdminWindow::ConnectionLabel(const std::string& registered) const {
    const ConnectionSpec* spec = store_.Find(ConnectionIdOf(registered));
    if (!spec) return registered;
    const bool console = registered.size() > 8 && registered.compare(registered.size() - 8, 8, ".console") == 0;
    return spec->name + (console ? " (SQL)" : "");
}

void DbAdminWindow::RefreshLog() {
    if (!logModel_) return;
    const uint64_t last = log_->LastSeq();
    if (last == shownSeq_) return;
    std::vector<LogEntry> entries = log_->Since(shownSeq_);
    shownSeq_ = last;
    const bool schemaReads = logSchemaReads_ && logSchemaReads_->IsChecked();
    if (!schemaReads)
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [](const LogEntry& e) { return e.kind == UltraDbStatementEvent::Kind::Introspection; }),
                      entries.end());
    if (entries.empty()) return;
    // At the bottom, the log follows; scrolled up to read, it stays.
    const UltraCanvasListView::ScrollMetrics metrics = logView_->GetScrollMetrics();
    const bool following = metrics.scrollOffset >= metrics.maxScroll - std::max(1, metrics.rowHeight);
    logModel_->Append(entries);
    logModel_->Trim(1000);
    if (following) logView_->ScrollToRow(logModel_->GetRowCount() - 1);
    size_t failed = 0;
    for (const auto& entry : logModel_->Entries()) failed += entry.ok ? 0 : 1;
    const std::string text = std::to_string(logModel_->GetRowCount()) +
                             (logModel_->GetRowCount() == 1 ? " statement" : " statements") +
                             (failed > 0 ? ", " + std::to_string(failed) + " failed" : std::string()) +
                             ". Double-click one to put it in the SQL tab.";
    logInfo_->SetText(text);
}

void DbAdminWindow::RefillLog() {
    logModel_->Clear();
    shownSeq_ = 0;
    RefreshLog();
}

void DbAdminWindow::CopyLog() {
    std::vector<LogEntry> entries;
    IListSelection* selection = logView_->GetSelection();
    std::vector<int> rows = selection ? selection->GetSelectedRows() : std::vector<int>();
    std::sort(rows.begin(), rows.end());
    for (const int row : rows)
        if (const LogEntry* entry = logModel_->At(row)) entries.push_back(*entry);
    if (entries.empty()) entries = logModel_->Entries();
    if (entries.empty()) { SetStatus("The log is empty."); return; }
    const std::string text = LogText(entries, [this](const std::string& registered) { return ConnectionLabel(registered); });
    if (!SetClipboardText(text)) { SetStatus("The clipboard cannot be reached.", true); return; }
    SetStatus(std::to_string(entries.size()) + (entries.size() == 1 ? " statement" : " statements") + " copied as SQL.");
}

// ===== SNIPPETS =====

void DbAdminWindow::ShowSnippetsMenu() {
    if (!window_ || !snippetsButton_) return;
    menu_ = std::make_shared<UltraCanvasMenu>("dbaSnippetsMenu", 0, 0, 260, 0);
    menu_->SetMenuType(MenuType::PopupMenu);
    if (snippets_.All().empty()) menu_->AddItem(MenuItemData::Header("No snippets kept yet"));
    for (const auto& snippet : snippets_.All()) {
        const std::string sql = snippet.sql;
        MenuItemData item = MenuItemData::Action(snippet.name, [this, sql]() {
            sqlEditor_->SetText(sql);
            sqlEditor_->SetFocus(true);
        });
        item.tooltip = CellText(UltraDbValue(sql), 400);
        menu_->AddItem(item);
    }
    menu_->AddItem(MenuItemData::Separator());
    MenuItemData save = MenuItemData::Action("Keep the editor's SQL as a snippet", IconPath("add"), [this]() { SaveSnippet(); });
    save.enabled = sqlEditor_->GetText().find_first_not_of(" \t\r\n;") != std::string::npos;
    menu_->AddItem(save);
    if (!snippets_.All().empty()) {
        std::vector<MenuItemData> remove;
        for (const auto& snippet : snippets_.All()) {
            const std::string name = snippet.name;
            remove.push_back(MenuItemData::Action(name, [this, name]() {
                std::string error;
                if (!snippets_.Remove(name, error)) SetStatus(error, true);
                else SetStatus("Snippet " + name + " removed.");
            }));
        }
        menu_->AddItem(MenuItemData::Submenu("Remove a snippet", IconPath("delete"), remove));
    }
    const Rect2Df bounds = snippetsButton_->GetBoundsInWindow();
    PopupElementSettings settings;
    menu_->OpenMenuAnchored(Rect2Di(static_cast<int>(bounds.x), static_cast<int>(bounds.y), static_cast<int>(bounds.width),
                                    static_cast<int>(bounds.height)),
                            *window_, settings);
}

void DbAdminWindow::SaveSnippet() {
    const std::string sql = sqlEditor_->HasSelection() ? sqlEditor_->GetSelectedText() : sqlEditor_->GetText();
    UltraCanvasDialogManager::ShowInputDialog(
        "A name for the snippet (one of the same name is replaced):", "Keep as a snippet", FirstLine(sql), InputType::Text,
        [this, sql](DialogResult result, const std::string& name) {
            if (result != DialogResult::OK) return;
            std::string error;
            if (!snippets_.Put({name, sql}, error)) { SetStatus(error, true); return; }
            SetStatus("Snippet " + name + " kept.");
        },
        window_.get());
}

} // namespace UltraDBAdmin

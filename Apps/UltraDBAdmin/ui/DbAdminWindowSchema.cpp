// Apps/UltraDBAdmin/ui/DbAdminWindowSchema.cpp
// The window's third source: the Relations tab (DbAdminRelations, drawn by
// UltraCanvasERDiagram), the Routines tab (DbAdminRoutines) and the Server
// tab (DbAdminServer). Each reads what it shows on the job queue, and on
// Refresh: Routines and Server whenever they are shown, Relations - a read of
// every table - when the tree's selection is in another schema than it shows.
// The Server tab's monitor reads the status every kMonitorIntervalMs while
// it is shown (DbAdminMonitor) and draws a line chart per measure.
// DbAdminWindow.h says what the window is.
// Version: 0.3.0 - the Server tab's monitor charts
// Version: 0.2.0 - a session stopped through UltraDb_StopSession; a layer wraps only well past the view
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminWindow.h"

#include "DbAdminOperationDialogs.h"
#include "DbAdminWindowParts.h"

#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineLineChart.h"
#include "Plugins/Diagrams/UltraCanvasERDiagram.h"
#include "UltraCanvasListSelection.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <set>
#include <utility>

using namespace UltraCanvas;

namespace UltraDBAdmin {
using namespace window;

namespace {


// A picture of the schema, not an editor of it: Delete would take a table
// out of the drawing alone, which reads as dropping it.
class RelationsDiagram : public UltraCanvasERDiagram {
public:
    using UltraCanvasERDiagram::UltraCanvasERDiagram;
    bool OnEvent(const UCEvent& event) override {
        if (event.type == UCEventType::KeyDown &&
            (event.virtualKey == UCKeys::Delete || event.virtualKey == UCKeys::Backspace))
            return true;
        return UltraCanvasERDiagram::OnEvent(event);
    }
};

std::string Joined(const std::vector<std::string>& names) {
    std::string out;
    for (const auto& name : names) out += (out.empty() ? "" : ", ") + name;
    return out;
}

// Crow's foot: the tables in their layers (RelationLayers) - across a
// wide view, a layer a column, parents on the left; down a tall one, a layer
// a row, parents on top. A layer longer than the view's shape allows wraps
// into the next column or row; each is centred. Chen's diamonds and ovals
// are better placed by the diagram's own layout.
void PlaceInLayers(UltraCanvasERDiagram& diagram, const std::vector<std::vector<std::string>>& layers, double aspect) {
    const bool columns = aspect >= 1.2;
    // Along a layer, and from one layer to the next.
    const double gapAlong = columns ? 40.0 : 60.0, gapAcross = columns ? 110.0 : 80.0;
    auto along = [columns](const ERDiagramEntity& e) { return columns ? e.height : e.width; };
    auto across = [columns](const ERDiagramEntity& e) { return columns ? e.width : e.height; };
    double area = 0.0, longest = 0.0;
    for (const auto& layer : layers)
        for (const auto& id : layer)
            if (const ERDiagramEntity* entity = diagram.GetEntity(id)) {
                area += (entity->width + 60.0) * (entity->height + 60.0);
                longest = std::max(longest, along(*entity));
            }
    // A layer wraps only when well past the view's shape: the tables it
    // pushes on sit beyond the next layer, their links crossing it.
    const double wrap =
        std::max(longest, 1.5 * (columns ? std::sqrt(area / aspect) : std::sqrt(area * aspect)));
    struct Line {
        std::vector<std::string> ids;
        double length = 0.0, thickness = 0.0;
    };
    std::vector<Line> lines;
    for (const auto& layer : layers) {
        Line line;
        for (const auto& id : layer) {
            const ERDiagramEntity* entity = diagram.GetEntity(id);
            if (!entity) continue;
            if (!line.ids.empty() && line.length + gapAlong + along(*entity) > wrap) {
                lines.push_back(line);
                line = Line();
            }
            line.length += (line.ids.empty() ? 0.0 : gapAlong) + along(*entity);
            line.thickness = std::max(line.thickness, across(*entity));
            line.ids.push_back(id);
        }
        if (!line.ids.empty()) lines.push_back(line);
    }
    double longestLine = 0.0;
    for (const auto& line : lines) longestLine = std::max(longestLine, line.length);
    double offset = 0.0;
    for (const auto& line : lines) {
        double at = (longestLine - line.length) / 2.0;
        for (const auto& id : line.ids) {
            const ERDiagramEntity* entity = diagram.GetEntity(id);
            if (!entity) continue;
            const double length = along(*entity);
            if (columns) diagram.SetEntityPosition(id, offset, at);
            else diagram.SetEntityPosition(id, at, offset);
            at += length + gapAlong;
        }
        offset += line.thickness + gapAcross;
    }
}

std::string Plural(size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

} // namespace

// ===== SHOWN =====

void DbAdminWindow::OnPageShown(int tab) {
    std::string connectionId, schema;
    if (tab == kRelationsTab) {
        const bool scoped = SchemaScope(connectionId, schema);
        if (scoped && (connectionId != relationsConnectionId_ || schema != relationsSchema_)) ReadRelations();
        else UpdateRelationsBar();
    } else if (tab == kRoutinesTab) {
        // Cheap to read, and the SQL tab may have changed them: read each time.
        const bool scoped = SchemaScope(connectionId, schema);
        const bool same = connectionId == routinesConnectionId_ && schema == routinesSchema_;
        if (scoped && !(same && routinesLoading_)) ReadRoutines();
        else UpdateRoutinesBar();
    } else if (tab == kServerTab) {
        // Sessions come and go: read each time too.
        const bool scoped = ServerScope(connectionId, schema);
        UpdateServerBar();
        const bool same = connectionId == serverConnectionId_ && schema == serverSchema_ &&
                          ChosenServerView() == serverShown_;
        if (scoped && !(same && serverLoading_)) ReadServer();
    }
}

bool DbAdminWindow::ServerScope(std::string& connectionId, std::string& schema) const {
    const NodeRef* ref = RefOf(selectedNode_);
    if (!ref || ref->kind == NodeRef::Kind::Placeholder) return false;
    const ConnectionSpec* spec = store_.Find(ref->connectionId);
    if (!spec) return false;
    connectionId = spec->id;
    schema = ref->kind == NodeRef::Kind::Connection ? (spec->IsSqlite() ? std::string("main") : std::string())
                                                    : ref->schema;
    return true;
}

// ===== RELATIONS =====

std::shared_ptr<UltraCanvasContainer> DbAdminWindow::BuildRelationsPage() {
    auto page = MakeTabPage("dbaRelationsPage");
    auto bar = MakeToolbar("dbaRelationsBar");
    relationsReadButton_ = MakeActionButton("dbaRelationsRead", "Refresh", "refresh",
                                            "Read the tables of the schema picked in the tree again");
    relationsReadButton_->SetOnClick([this]() { ReadRelations(); });
    bar->AddChild(relationsReadButton_);
    relationsFitButton_ = MakeActionButton("dbaRelationsFit", "Fit", "fit", "The whole diagram in view");
    relationsFitButton_->SetOnClick([this]() {
        relationsDiagram_->FitView(kRelationsFitPadding);
        relationsDiagram_->RequestRedraw();
    });
    bar->AddChild(relationsFitButton_);

    relationsNotation_ = CreateDropdown("dbaRelationsNotation", 0, 0, 150, kControlHeight);
    relationsNotation_->layoutItem.SetFlexShrink(0);
    relationsNotation_->SetFollowTheme(true);
    relationsNotation_->AddItem("Crow's foot");
    relationsNotation_->AddItem("Chen");
    relationsNotation_->SetSelectedIndex(0, false);
    relationsNotation_->SetTooltip("Crow's foot: each table a box of its columns. Chen: tables, their links as "
                                   "diamonds, columns as ovals.");
    relationsNotation_->onSelectionChanged = [this](int, const DropdownItem&) { DrawRelations(); };
    bar->AddChild(relationsNotation_);

    relationsTypes_ = UltraCanvasCheckbox::CreateCheckbox("dbaRelationsTypes", 0, 0, 132, 24, "Column types", true);
    relationsTypes_->layoutItem.SetFlexShrink(0);
    relationsTypes_->SetTooltip("Each column's type beside its name");
    relationsTypes_->onChecked = [this]() { DrawRelations(); };
    relationsTypes_->onUnchecked = [this]() { DrawRelations(); };
    bar->AddChild(relationsTypes_);

    relationsInfo_ = MakeBarStatus("dbaRelationsInfo");
    bar->AddChild(relationsInfo_);
    page->AddChild(bar);
    PinToolbar(bar);

    relationsDiagram_ = std::make_shared<RelationsDiagram>("dbaRelations", 0, 0, 600, 400);
    relationsDiagram_->SetTheme(ERDiagramTheme::Professional);
    relationsDiagram_->SetConnectorStyle(ERConnectorStyle::Orthogonal);
    relationsDiagram_->SetLegendVisible(false);
    relationsDiagram_->SetHighlightRelatedOnHover(true);
    relationsDiagram_->onEntityDoubleClick = [this](const std::string& id) { OpenRelationTable(id); };
    FillWith(page, relationsDiagram_);
    return page;
}

void DbAdminWindow::ReadRelations() {
    std::string connectionId, schema;
    if (!SchemaScope(connectionId, schema)) { UpdateRelationsBar(); return; }
    const ConnectionSpec* spec = store_.Find(connectionId);
    if (!spec) return;
    const uint64_t generation = ++relationsGen_;
    relationsConnectionId_ = connectionId;
    relationsSchema_ = schema;
    relationsLoading_ = true;
    relationsSummary_.clear();
    UpdateRelationsBar();
    const std::string connection = RegisteredName(*spec);
    const std::string where = schema + " (" + spec->name + ")";
    Background([this, connection, schema, where, generation]() -> std::function<void()> {
        std::vector<UltraDbTableSchema> tables;
        bool truncated = false;
        std::vector<std::string> failed;
        const UltraDbResult result = ReadSchemaTables(connection, schema, kRelationsLimit, tables, truncated, failed);
        size_t outside = 0;
        std::vector<RelationLink> links = RelationLinks(tables, outside);
        return [this, result, tables, links, truncated, failed, outside, where, generation]() {
            if (generation != relationsGen_) return;
            relationsLoading_ = false;
            if (!result) {
                relationsTables_.clear();
                relationsLinks_.clear();
                relationsSummary_ = "Not read: " + OneLine(result.message);
                DrawRelations();
                return;
            }
            relationsTables_ = tables;
            relationsLinks_ = links;
            std::string text = Plural(tables.size(), "table", "tables") + ", " +
                               Plural(links.size(), "link", "links") + " in " + where;
            if (truncated) text += " - the first " + std::to_string(kRelationsLimit) + " tables only";
            if (outside > 0) text += " - " + Plural(outside, "key", "keys") + " to tables not drawn";
            if (!failed.empty()) text += " - " + Plural(failed.size(), "table", "tables") + " not read (" + failed.front() + ")";
            if (tables.empty()) text = "No tables in " + where;
            relationsSummary_ = text;
            DrawRelations();
        };
    });
}

void DbAdminWindow::DrawRelations() {
    RelationsDiagram* diagram = static_cast<RelationsDiagram*>(relationsDiagram_.get());
    diagram->Clear();
    const bool chen = relationsNotation_->GetSelectedIndex() == 1;
    diagram->SetNotation(chen ? ERNotation::ChenMinMax : ERNotation::CrowsFoot);
    diagram->SetLineEndStyle(ERLineEndStyle::CrowsFoot);
    diagram->SetCardinalityLabelStyle(ERCardinalityLabels::NoLabels);
    diagram->SetShowDataTypes(relationsTypes_->IsChecked());
    for (const auto& table : relationsTables_) {
        diagram->AddEntity(table.name, table.name);
        std::set<std::string> keyColumns(table.primaryKey.begin(), table.primaryKey.end());
        std::set<std::string> foreign, unique;
        for (const auto& key : table.foreignKeys) foreign.insert(key.columns.begin(), key.columns.end());
        for (const auto& index : table.indexes)
            if (index.unique && !index.primary && index.columns.size() == 1) unique.insert(index.columns.front());
        for (const auto& column : table.columns) {
            const ERKeyRole role = keyColumns.count(column.name) ? ERKeyRole::Primary
                                   : foreign.count(column.name)  ? ERKeyRole::Foreign
                                   : unique.count(column.name)   ? ERKeyRole::Unique
                                                                 : ERKeyRole::NoKey;
            diagram->AddTypedAttribute(table.name, column.name, column.declaredType, role, column.nullable);
        }
    }
    // Parent end: exactly one, or none when the key may be NULL; child end:
    // any number, or at most one when the key is unique in it.
    for (const auto& link : relationsLinks_) {
        const ERCardinality parent = link.optional ? ERCardinality::ZeroOrOne : ERCardinality::ExactlyOne;
        const ERCardinality child = link.oneToOne ? ERCardinality::ZeroOrOne : ERCardinality::ZeroOrMany;
        const std::string label = Joined(link.columns);
        if (link.selfReference)
            diagram->AddRecursiveRelationship(link.id, label, link.child, "parent", "child", parent, child);
        else
            diagram->AddRelationship(link.id, label, link.parent, link.child, parent, child);
    }
    diagram->AutoSizeAll();
    if (chen) {
        diagram->SetLayout(ERDiagramLayout::ForceDirected);
    } else {
        const Rect2Df view = diagram->GetBounds();
        const double aspect = view.width > 50 && view.height > 50 ? view.width / view.height : 2.0;
        PlaceInLayers(*diagram, RelationLayers(relationsTables_, relationsLinks_), aspect);
        diagram->SetLayout(ERDiagramLayout::Manual);
    }
    diagram->RunLayout();
    diagram->FitView(kRelationsFitPadding);
    diagram->RequestRedraw();
    UpdateRelationsBar();
}

void DbAdminWindow::OpenRelationTable(const std::string& table) {
    if (relationsConnectionId_.empty() || !store_.Find(relationsConnectionId_)) return;
    NodeRef ref;
    ref.kind = NodeRef::Kind::Object;
    ref.connectionId = relationsConnectionId_;
    ref.schema = relationsSchema_;
    ref.name = table;
    ref.objectKind = UltraDbObjectKind::Table;
    OpenFromTree(ref);
    tabs_->SetActiveTab(kBrowseTab);
}

void DbAdminWindow::UpdateRelationsBar() {
    if (!relationsReadButton_) return;
    std::string connectionId, schema;
    const bool scoped = SchemaScope(connectionId, schema);
    relationsReadButton_->SetDisabled(relationsLoading_ || !scoped);
    relationsFitButton_->SetDisabled(relationsTables_.empty());
    std::string text;
    if (relationsLoading_) text = "Reading the tables of " + relationsSchema_ + "…";
    else if (!relationsSummary_.empty()) text = relationsSummary_ + (relationsTables_.empty() ? "" : " - double-click a table to open it");
    else text = "Pick a schema in the tree, or anything in it.";
    relationsInfo_->SetText(text);
    relationsInfo_->SetTooltip(text);
    relationsInfo_->SetTextColor(relationsSummary_.rfind("Not read", 0) == 0 ? ErrorColor() : StatusColor());
}

// ===== ROUTINES =====

std::shared_ptr<UltraCanvasContainer> DbAdminWindow::BuildRoutinesPage() {
    auto page = MakeTabPage("dbaRoutinesPage");
    auto bar = MakeToolbar("dbaRoutinesBar");
    routinesReadButton_ = MakeActionButton("dbaRoutinesRead", "Refresh", "refresh",
                                           "Read the routines of the schema picked in the tree again");
    routinesReadButton_->SetOnClick([this]() { ReadRoutines(); });
    bar->AddChild(routinesReadButton_);
    bar->AddChild(MakeBarDivider("dbaRoutinesDivider"));
    routineNewButton_ = MakeActionButton("dbaRoutineNew", "New", "add",
                                         "Start a trigger, a function or a procedure in the SQL tab");
    routineNewButton_->SetOnClick([this]() { ShowNewRoutineMenu(); });
    bar->AddChild(routineNewButton_);
    routineEditButton_ = MakeActionButton("dbaRoutineEdit", "Edit in SQL", "edit",
                                          "Its statement in the SQL tab, to change and run (or double-click it)");
    routineEditButton_->SetOnClick([this]() { EditRoutine(SelectedRoutine()); });
    bar->AddChild(routineEditButton_);
    routineDropButton_ = MakeActionButton("dbaRoutineDrop", "Drop", "delete", "Drop the selected routine",
                                          ButtonRole::Danger);
    routineDropButton_->SetOnClick([this]() { DropRoutine(); });
    bar->AddChild(routineDropButton_);
    routinesInfo_ = MakeBarStatus("dbaRoutinesInfo");
    bar->AddChild(routinesInfo_);
    page->AddChild(bar);
    PinToolbar(bar);

    auto split = CreateVerticalSplitPane("dbaRoutinesSplit", 0, 0, 0, 0);
    split->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    split->SetSplitPaneStyle(PaneStyle());
    auto listPane = split->AddPane(2.0);
    auto textPane = split->AddPane(3.0);
    split->SetPaneMinSize(0, 80);
    split->SetPaneMinSize(1, 80);
    MakePlainColumn(listPane);
    MakePlainColumn(textPane);

    routineModel_ = std::make_shared<RoutineModel>();
    routineView_ = MakeList("dbaRoutines", routineModel_);
    routineView_->onSelectionChanged = [this](const std::vector<int>& rows) {
        ShowRoutine(rows.empty() ? -1 : rows.front());
    };
    routineView_->onItemDoubleClicked = [this](int row) { EditRoutine(row); };
    FillWith(listPane, routineView_);

    routineText_ = std::make_shared<UltraCanvasTextArea>("dbaRoutineText");
    routineText_->ApplyCodeStyle("SQL");
    routineText_->SetBorders(1.0f, UltraCanvasTheme::Current().roles.line, 6.0f);
    routineText_->SetReadOnly(true);
    routineText_->SetWordWrap(true);
    FillWith(textPane, routineText_);
    FillWith(page, split);
    return page;
}

int DbAdminWindow::SelectedRoutine() const {
    IListSelection* selection = routineView_ ? routineView_->GetSelection() : nullptr;
    if (!selection) return -1;
    const std::vector<int> rows = selection->GetSelectedRows();
    return rows.empty() ? -1 : rows.front();
}

void DbAdminWindow::ReadRoutines() {
    std::string connectionId, schema;
    if (!SchemaScope(connectionId, schema)) { UpdateRoutinesBar(); return; }
    const ConnectionSpec* spec = store_.Find(connectionId);
    if (!spec) return;
    const uint64_t generation = ++routinesGen_;
    routinesConnectionId_ = connectionId;
    routinesSchema_ = schema;
    routinesLoading_ = true;
    routinesSummary_.clear();
    UpdateRoutinesBar();
    const std::string connection = RegisteredName(*spec);
    const SqlEngine engine = EngineOf(*spec);
    const std::string where = schema + " (" + spec->name + ")";
    Background([this, connection, engine, schema, where, generation]() -> std::function<void()> {
        std::vector<RoutineInfo> routines;
        const UltraDbResult result = ListRoutines(connection, engine, schema, routines);
        return [this, result, routines, where, generation]() {
            if (generation != routinesGen_) return;
            routinesLoading_ = false;
            if (routineView_->GetSelection()) routineView_->GetSelection()->Clear();
            routineModel_->Replace(result ? routines : std::vector<RoutineInfo>());
            ShowRoutine(-1);
            if (!result) routinesSummary_ = "Not read: " + OneLine(result.message);
            else if (routines.empty()) routinesSummary_ = "No triggers or routines in " + where;
            else routinesSummary_ = Plural(routines.size(), "routine", "routines") + " in " + where;
            UpdateRoutinesBar();
        };
    });
}

void DbAdminWindow::ShowRoutine(int row) {
    const RoutineInfo* routine = routineModel_->At(row);
    if (!routine) routineText_->SetText("");
    else if (routine->definition.empty())
        routineText_->SetText("-- " + std::string(RoutineKindText(routine->kind)) + " " + routine->name + "(" +
                              routine->arguments + ")\n-- The server gives no statement that makes an aggregate.");
    else routineText_->SetText(routine->definition);
    UpdateRoutinesBar();
}

void DbAdminWindow::EditRoutine(int row) {
    const RoutineInfo* routine = routineModel_->At(row);
    const ConnectionSpec* spec = store_.Find(routinesConnectionId_);
    if (!routine || !spec) return;
    const std::string sql = EditableDefinition(EngineOf(*spec), *routine);
    if (sql.empty()) { routinesInfo_->SetText("An aggregate has no statement to edit here."); return; }
    sqlEditor_->SetText(sql);
    tabs_->SetActiveTab(kSqlTab);
    sqlEditor_->SetFocus(true);
    SetStatus(std::string("The ") + RoutineKindText(routine->kind) + " " + routine->name +
              " is in the SQL tab: change it and Run, then Refresh Routines.");
}

void DbAdminWindow::ShowNewRoutineMenu() {
    const ConnectionSpec* spec = store_.Find(routinesConnectionId_);
    if (!spec || !window_) return;
    menu_ = std::make_shared<UltraCanvasMenu>("dbaNewRoutineMenu", 0, 0, 200, 0);
    menu_->SetMenuType(MenuType::PopupMenu);
    menu_->AddItem(MenuItemData::Action("Trigger", IconPath("add"), [this]() { NewRoutine(RoutineInfo::Kind::Trigger); }));
    if (!spec->IsSqlite()) {
        menu_->AddItem(MenuItemData::Action("Function", IconPath("add"), [this]() { NewRoutine(RoutineInfo::Kind::Function); }));
        menu_->AddItem(MenuItemData::Action("Procedure", IconPath("add"), [this]() { NewRoutine(RoutineInfo::Kind::Procedure); }));
    }
    const Rect2Df bounds = routineNewButton_->GetBoundsInWindow();
    PopupElementSettings settings;
    menu_->OpenMenuAnchored(Rect2Di(static_cast<int>(bounds.x), static_cast<int>(bounds.y), static_cast<int>(bounds.width),
                                    static_cast<int>(bounds.height)),
                            *window_, settings);
}

void DbAdminWindow::NewRoutine(RoutineInfo::Kind kind) {
    const ConnectionSpec* spec = store_.Find(routinesConnectionId_);
    if (!spec) return;
    // A trigger goes on the table picked in the tree, when one of this schema is.
    std::string table;
    const NodeRef* ref = RefOf(selectedNode_);
    if (ref && ref->kind == NodeRef::Kind::Object && ref->objectKind == UltraDbObjectKind::Table &&
        ref->connectionId == routinesConnectionId_ && ref->schema == routinesSchema_)
        table = ref->name;
    sqlEditor_->SetText(RoutineTemplate(EngineOf(*spec), kind, routinesSchema_, table));
    tabs_->SetActiveTab(kSqlTab);
    sqlEditor_->SetFocus(true);
    SetStatus(std::string("A new ") + RoutineKindText(kind) + " in the SQL tab: name it, write it, Run.");
}

void DbAdminWindow::DropRoutine() {
    const RoutineInfo* routine = routineModel_->At(SelectedRoutine());
    const ConnectionSpec* spec = store_.Find(routinesConnectionId_);
    if (!routine || !spec) return;
    const std::string kind = RoutineKindText(routine->kind);
    const std::string what = routine->kind == RoutineInfo::Kind::Trigger
                                 ? kind + " " + routine->name + " on " + routine->table
                                 : kind + " " + routine->name + "(" + routine->arguments + ")";
    const uint64_t generation = routinesGen_;
    ConfirmOperation(spec->id, "Drop " + kind, "Drop the " + what + "? It cannot be undone.",
                     DropRoutinePlan(EngineOf(*spec), *routine), "Drop", ButtonRole::Danger,
                     [this, what, generation](const UltraDbResult& result, const UltraDbResultSet&) {
                         if (!result) return;
                         SetStatus("Dropped the " + what + ".");
                         if (generation == routinesGen_) ReadRoutines();
                     });
}

void DbAdminWindow::UpdateRoutinesBar() {
    if (!routinesReadButton_) return;
    std::string connectionId, schema;
    const bool scoped = SchemaScope(connectionId, schema);
    routinesReadButton_->SetDisabled(routinesLoading_ || !scoped);
    const ConnectionSpec* spec = store_.Find(routinesConnectionId_);
    const std::string blocked = spec ? WriteBlockReason(*spec) : std::string("Nothing read yet");
    const RoutineInfo* routine = routineModel_->At(SelectedRoutine());
    routineNewButton_->SetDisabled(!spec || !blocked.empty() || routinesLoading_);
    routineEditButton_->SetDisabled(!routine || !blocked.empty() || routine->definition.empty());
    routineDropButton_->SetDisabled(!routine || !blocked.empty());
    for (auto* button : {routineNewButton_.get(), routineEditButton_.get(), routineDropButton_.get()})
        if (!blocked.empty() && spec) button->SetTooltip(blocked);
    std::string text;
    if (routinesLoading_) text = "Reading the routines of " + routinesSchema_ + "…";
    else if (!routinesSummary_.empty()) text = routinesSummary_;
    else text = "Pick a schema in the tree, or anything in it.";
    routinesInfo_->SetText(text);
    routinesInfo_->SetTooltip(text);
    routinesInfo_->SetTextColor(routinesSummary_.rfind("Not read", 0) == 0 ? ErrorColor() : StatusColor());
}

// ===== SERVER =====

std::shared_ptr<UltraCanvasContainer> DbAdminWindow::BuildServerPage() {
    auto page = MakeTabPage("dbaServerPage");
    auto bar = MakeToolbar("dbaServerBar");
    serverViewPick_ = CreateDropdown("dbaServerView", 0, 0, 130, kControlHeight);
    serverViewPick_->layoutItem.SetFlexShrink(0);
    serverViewPick_->SetFollowTheme(true);
    serverViewPick_->SetTooltip("What to show of the connection picked in the tree");
    serverViewPick_->onSelectionChanged = [this](int, const DropdownItem&) {
        UpdateServerBar();
        ReadServer();
    };
    bar->AddChild(serverViewPick_);
    serverReadButton_ = MakeActionButton("dbaServerRead", "Refresh", "refresh", "Read it again");
    serverReadButton_->SetOnClick([this]() {
        // On the monitor: the charts start again.
        if (ChosenServerView() == ServerView::Monitor) monitorConnectionId_.clear();
        ReadServer();
    });
    bar->AddChild(serverReadButton_);
    serverFilter_ = CreateTextInput("dbaServerFilter", 0, 0, 180, kControlHeight);
    serverFilter_->layoutItem.SetFlexShrink(0);
    serverFilter_->SetFollowTheme(true);
    serverFilter_->SetShowValidationState(false);
    serverFilter_->SetPlaceholder("settings with…");
    serverFilter_->SetTooltip("Only the settings whose name or description holds this, in any case (Enter)");
    serverFilter_->onEnterPressed = [this](const std::string&) { ReadServer(); return true; };
    bar->AddChild(serverFilter_);
    bar->AddChild(MakeBarDivider("dbaServerDivider"));
    serverCancelButton_ = MakeActionButton("dbaServerCancel", "Stop query", "stop",
                                           "Stop the query the selected session runs; the session stays");
    serverCancelButton_->SetOnClick([this]() { StopServerSession(false); });
    bar->AddChild(serverCancelButton_);
    serverEndButton_ = MakeActionButton("dbaServerEnd", "End session", "end-session",
                                        "Disconnect the selected session: what it has not committed is rolled back",
                                        ButtonRole::Danger);
    serverEndButton_->SetOnClick([this]() { StopServerSession(true); });
    bar->AddChild(serverEndButton_);
    serverInfo_ = MakeBarStatus("dbaServerInfo");
    bar->AddChild(serverInfo_);
    page->AddChild(bar);
    PinToolbar(bar);

    serverModel_ = std::make_shared<ResultSetModel>();
    serverProxy_ = std::make_shared<UltraCanvasListSortFilterProxy>(serverModel_);
    serverGrid_ = MakeList("dbaServerRows", serverProxy_);
    FitResultColumns(*serverGrid_);
    serverGrid_->onSelectionChanged = [this](const std::vector<int>&) { UpdateServerBar(); };
    UltraCanvasListView* rawView = serverGrid_.get();
    UltraCanvasListSortFilterProxy* rawProxy = serverProxy_.get();
    serverGrid_->onHeaderClicked = [rawView, rawProxy](int column) {
        const bool ascending = !(column == rawView->GetSortColumn() && rawView->GetSortAscending());
        rawProxy->SortByColumn(column, ascending ? ListSortOrder::Ascending : ListSortOrder::Descending);
        rawView->SetSortIndicator(column, ascending);
    };
    FillWith(page, serverGrid_);
    // The monitor's charts, in rows of three, in place of the grid.
    serverMonitorBox_ = CreateContainer("dbaServerMonitor", 0, 0, 0, 0);
    MakePlainColumn(serverMonitorBox_);
    serverMonitorBox_->layout.SetFlexGap(10);
    serverMonitorBox_->SetVisible(false);
    FillWith(page, serverMonitorBox_);
    return page;
}

ServerView DbAdminWindow::ChosenServerView() const {
    const int index = serverViewPick_ ? serverViewPick_->GetSelectedIndex() : -1;
    if (index < 0 || static_cast<size_t>(index) >= serverViews_.size())
        return serverViews_.empty() ? ServerView::FileInfo : serverViews_.front();
    return serverViews_[static_cast<size_t>(index)];
}

void DbAdminWindow::ReadServer() {
    std::string connectionId, schema;
    if (!ServerScope(connectionId, schema)) { UpdateServerBar(); return; }
    const ConnectionSpec* spec = store_.Find(connectionId);
    if (!spec) return;
    UpdateServerBar();                      // the views of this connection's engine
    const ServerView view = ChosenServerView();
    const uint64_t generation = ++serverGen_;
    serverConnectionId_ = connectionId;
    serverSchema_ = schema;
    serverShown_ = view;
    serverGrid_->SetVisible(view != ServerView::Monitor);
    serverMonitorBox_->SetVisible(view == ServerView::Monitor);
    if (view == ServerView::Monitor) {
        serverLoading_ = false;
        // The same server's charts carry on; another's start afresh.
        ReadMonitor(connectionId != monitorConnectionId_);
        return;
    }
    serverLoading_ = true;
    serverSummary_.clear();
    UpdateServerBar();
    const std::string connection = RegisteredName(*spec);
    const SqlEngine engine = EngineOf(*spec);
    const std::string filter = view == ServerView::Settings ? serverFilter_->GetText() : std::string();
    const std::string name = spec->name;
    Background([this, connection, engine, view, schema, filter, name, generation]() -> std::function<void()> {
        UltraDbResultSet rows;
        const UltraDbResult result = ReadServerView(connection, engine, view, schema, filter, rows);
        return [this, result, rows = std::move(rows), view, name, generation]() mutable {
            if (generation != serverGen_) return;
            serverLoading_ = false;
            if (serverGrid_->GetSelection()) serverGrid_->GetSelection()->Clear();
            const size_t count = rows.Size();
            serverModel_->Replace(result ? std::move(rows) : UltraDbResultSet());
            serverGrid_->SetSortIndicator(-1, true);
            if (!result) serverSummary_ = "Not read: " + OneLine(result.message);
            else if (view == ServerView::Sessions) serverSummary_ = Plural(count, "session", "sessions") + " on " + name;
            else if (view == ServerView::Settings) serverSummary_ = Plural(count, "setting", "settings") + " of " + name;
            else if (view == ServerView::Sizes) serverSummary_ = "Sizes on " + name;
            else serverSummary_ = "The file of " + name;
            UpdateServerBar();
        };
    });
}

void DbAdminWindow::StopServerSession(bool terminate) {
    const ConnectionSpec* spec = store_.Find(serverConnectionId_);
    IListSelection* selection = serverGrid_->GetSelection();
    if (!spec || !selection || serverShown_ != ServerView::Sessions) return;
    const std::vector<int> picked = selection->GetSelectedRows();
    const int source = picked.empty() ? -1 : serverProxy_->MapToSource(picked.front());
    const UltraDbResultSet& rows = serverModel_->Rows();
    if (source < 0 || static_cast<size_t>(source) >= rows.Size()) return;
    const UltraDbRow& row = rows.Row(static_cast<size_t>(source));
    const int64_t id = row[SessionIdColumn()].AsInt64();
    if (row["this_session"].AsBool()) {
        serverInfo_->SetText("That is the session this tab reads with: it is not stopped from here.");
        serverInfo_->SetTextColor(ErrorColor());
        return;
    }
    const std::string blocked = WriteBlockReason(*spec);
    if (!blocked.empty()) { SetStatus(blocked); return; }
    const std::string who = "session " + std::to_string(id) + " (" + row["user"].AsString() +
                            (row["application"].AsString().empty() ? std::string() : ", " + row["application"].AsString()) + ")";
    const std::string title = terminate ? "End session" : "Stop query";
    const std::string intro = terminate ? "Disconnect " + who + "?" : "Stop the query that " + who + " is running?";
    const std::string connection = RegisteredName(*spec);
    const std::string note = ProductionNote(*spec);
    // The statement is the driver's (UltraDb_StopSessionSql), shown first;
    // UltraDb_StopSession runs it once confirmed.
    Background([this, connection, id, terminate, who, title, intro, note]() -> std::function<void()> {
        std::string sql;
        const UltraDbResult result = UltraDb_StopSessionSql(connection, id, terminate, sql);
        return [this, connection, id, terminate, who, title, intro, note, result, sql]() {
            if (!result) { SetStatus("Not done: " + OneLine(result.message), true); return; }
            ShowPlanDialog(window_.get(), title, note + intro, sql + ";\n", title,
                           terminate ? ButtonRole::Danger : ButtonRole::Primary,
                           [this, connection, id, terminate, who, title]() {
                               SetStatus(title + "…");
                               Background([this, connection, id, terminate, who]() -> std::function<void()> {
                                   bool stopped = false;
                                   const UltraDbResult result = UltraDb_StopSession(connection, id, terminate, stopped);
                                   return [this, result, stopped, terminate, who]() {
                                       if (!result) SetStatus("Not done: " + OneLine(result.message), true);
                                       else
                                           SetStatus(stopped ? std::string(terminate ? "Ended " : "Stopped the query of ") + who + "."
                                                             : who + " was not stopped: it has ended, or it is not yours to stop.",
                                                     !stopped);
                                       ReadServer();
                                   };
                               });
                           });
        };
    });
}

void DbAdminWindow::ReadMonitor(bool restart) {
    const ConnectionSpec* spec = store_.Find(serverConnectionId_);
    if (!spec) return;
    if (restart || monitorConnectionId_ != serverConnectionId_) {
        monitor_.Clear();
        monitorConnectionId_ = serverConnectionId_;
        monitorReading_ = false;
        ++monitorGen_;
        serverSummary_ = "Reading the status of " + spec->name + "…";
        ShowMonitor();
        UpdateServerBar();
    }
    if (monitorReading_) return;          // the last reading is not back yet
    monitorReading_ = true;
    const uint64_t generation = monitorGen_;
    const std::string connection = RegisteredName(*spec);
    const SqlEngine engine = EngineOf(*spec);
    const std::string name = spec->name;
    Background([this, connection, engine, name, generation]() -> std::function<void()> {
        UltraDbResultSet rows;
        const UltraDbResult result = ReadServerView(connection, engine, ServerView::Monitor, "", "", rows);
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const std::time_t at = std::time(nullptr);
        return [this, result, rows = std::move(rows), seconds, at, name, generation]() {
            if (generation != monitorGen_) return;
            monitorReading_ = false;
            if (!result) {
                serverSummary_ = "Not read: " + OneLine(result.message);
            } else {
                monitor_.Add(seconds, rows, at);
                serverSummary_ = name + " - read every " + std::to_string(kMonitorIntervalMs / 1000) +
                                 " seconds while shown";
            }
            ShowMonitor();
            UpdateServerBar();
        };
    });
}

void DbAdminWindow::ShowMonitor() {
    const std::vector<MonitorChart>& charts = monitor_.Charts();
    bool same = charts.size() == monitorCards_.size();
    for (size_t i = 0; same && i < charts.size(); ++i) same = charts[i].key == monitorCards_[i].key;
    if (!same) {
        // A card per chart, three to a row.
        serverMonitorBox_->ClearChildren();
        monitorCards_.clear();
        const ThemeRoles& roles = UltraCanvasTheme::Current().roles;
        std::shared_ptr<UltraCanvasContainer> row;
        for (size_t i = 0; i < charts.size(); ++i) {
            const MonitorChart& chart = charts[i];
            if (i % 3 == 0) {
                row = CreateContainer("dbaMonitorRow" + std::to_string(i / 3), 0, 0, 0, 0);
                row->layout.SetFlexRow().SetFlexGap(10);
                FillWith(serverMonitorBox_, row);
            }
            auto card = CreateContainer("dbaMonitorCard_" + chart.key, 0, 0, 0, 0);
            MakePlainColumn(card);
            MakeCard(*card);
            card->layout.SetFlexGap(6);
            card->SetPadding(8.0f);
            FillWith(row, card);
            auto head = CreateContainer("dbaMonitorHead_" + chart.key, 0, 0, 0, 22);
            head->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
            PinToolbar(head);
            auto title = CreateLabel("dbaMonitorTitle_" + chart.key, 0, 0, 0, 20,
                                     chart.title + (chart.unit.empty() ? std::string() : " (" + chart.unit + ")"));
            title->SetFontWeight(FontWeight::Bold);
            title->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            head->AddChild(title);
            auto value = CreateLabel("dbaMonitorValue_" + chart.key, 0, 0, 110, 20, "");
            value->SetAlignment(TextAlignment::Right);
            value->SetTextColor(roles.accent);
            value->SetFontWeight(FontWeight::Bold);
            value->layoutItem.SetFlexShrink(0);
            head->AddChild(value);
            card->AddChild(head);
            // A filled line from zero, its time along the bottom.
            auto line = CreateEngineLineChart("dbaMonitorChart_" + chart.key, 0, 0, 300, 120);
            line->SetMode(EngineLineChartMode::Area);
            line->SetAreaOpacity(0.15f);
            line->SetShowEdgeLines(true);
            line->SetLineWidth(2.0f);
            line->SetShowMarkers(false);
            line->SetShowValueLabels(false);
            line->SetCategoryPadding(0.0);
            line->SetValueFormat("", "", true);
            // Redrawn every reading: no rise from the base each time, and an
            // axis that reads at rest - counts to 4, rates to 5, all of 100 %.
            line->SetAnimateOnDataChange(false);
            line->SetValueAtLeast(chart.unit == "%" ? 100.0 : chart.unit.empty() ? 4.0 : 5.0);
            line->SetBackgroundColor(roles.panel);
            line->SetPlotAreaColor(roles.panel);
            line->SetEmptyMessage("Waiting for the next reading", "A rate needs two");
            line->SetEmptyMessageColor(roles.inkMuted);
            line->SetEnableTooltips(true);
            FillWith(card, line);
            monitorCards_.push_back(MonitorCard{ chart.key, value, line });
        }
    }
    for (size_t i = 0; i < charts.size(); ++i) {
        const MonitorChart& chart = charts[i];
        MonitorCard& card = monitorCards_[i];
        std::vector<std::string> times;
        EngineLineSeries series;
        series.name = chart.title;
        series.color = UltraCanvasTheme::Current().roles.accent;
        for (const MonitorPoint& point : chart.points) {
            times.push_back(MonitorClockText(point.at));
            series.values.push_back(point.value);
        }
        card.chart->SetCategories(times);
        card.chart->SetSeries({ series });
        card.value->SetText(chart.points.empty() ? std::string()
                                                 : MonitorValueText(chart.points.back().value, chart.unit));
    }
}

void DbAdminWindow::UpdateServerBar() {
    if (!serverReadButton_) return;
    std::string connectionId, schema;
    const bool scoped = ServerScope(connectionId, schema);
    const ConnectionSpec* spec = scoped ? store_.Find(connectionId) : nullptr;
    // The dropdown offers the views of the picked connection's engine.
    const std::vector<ServerView> views = spec ? ServerViews(EngineOf(*spec)) : std::vector<ServerView>();
    if (views != serverViews_) {
        const ServerView before = ChosenServerView();
        serverViews_ = views;
        serverViewPick_->ClearItems();
        int keep = 0;
        for (size_t i = 0; i < views.size(); ++i) {
            serverViewPick_->AddItem(ServerViewText(views[i]));
            if (views[i] == before) keep = static_cast<int>(i);
        }
        if (!views.empty()) serverViewPick_->SetSelectedIndex(keep, false);
    }
    const ServerView view = ChosenServerView();
    serverViewPick_->SetDisabled(views.size() < 2);
    serverReadButton_->SetDisabled(!spec || serverLoading_);
    serverFilter_->SetDisabled(view != ServerView::Settings);
    serverReadButton_->SetTooltip(view == ServerView::Monitor ? "Start the charts again" : "Read it again");
    const bool sessions = serverShown_ == ServerView::Sessions && !serverLoading_;
    IListSelection* selection = serverGrid_->GetSelection();
    const bool picked = sessions && selection && !selection->GetSelectedRows().empty();
    const ConnectionSpec* shown = store_.Find(serverConnectionId_);
    const std::string blocked = shown ? WriteBlockReason(*shown) : std::string();
    serverCancelButton_->SetDisabled(!picked || !blocked.empty());
    serverEndButton_->SetDisabled(!picked || !blocked.empty());
    std::string text;
    if (serverLoading_) text = std::string("Reading ") + ServerViewText(view) + "…";
    else if (!serverSummary_.empty()) text = serverSummary_;
    else text = "Pick a connection in the tree.";
    if (!blocked.empty() && sessions) text += " - read only: no session is stopped";
    serverInfo_->SetText(text);
    serverInfo_->SetTooltip(text);
    serverInfo_->SetTextColor(serverSummary_.rfind("Not read", 0) == 0 ? ErrorColor() : StatusColor());
}

} // namespace UltraDBAdmin

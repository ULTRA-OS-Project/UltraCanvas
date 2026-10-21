// Apps/UltraDBAdmin/ui/DbAdminWindow.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminWindow.h"

#include "DbAdminConnectionDialog.h"
#include "DbAdminFormat.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasModalDialog.h"

#include <algorithm>
#include <cstdio>
#include <utility>

// ULTRADBADMIN_VERSION comes from the build alone: CMake reads the first line
// of Docs/UltraDBAdmin/CHANGELOG.md (cmake/UltraCanvasVersion.cmake) and
// passes it as a compile definition. No fallback here, so a build that lost
// it fails instead of reporting a wrong number.
#ifndef ULTRADBADMIN_VERSION
#error "ULTRADBADMIN_VERSION is not defined: build through CMake, which reads it from Docs/UltraDBAdmin/CHANGELOG.md"
#endif

using namespace UltraCanvas;

namespace UltraDBAdmin {
namespace {

constexpr float kWindowWidth   = 1180.0f;
constexpr float kWindowHeight  = 740.0f;
constexpr float kPagePadding   = 10.0f;
constexpr float kTabPadding    = 8.0f;
constexpr float kSectionGap    = 8.0f;
constexpr float kControlHeight = 28.0f;
constexpr int   kTreeWidth     = 280;
constexpr int   kTreeMinWidth  = 180;
constexpr int   kStatementWidth = 380;
constexpr size_t kPageSize     = 200;

// Tab order, as added below.
constexpr int kBrowseTab    = 0;
constexpr int kStructureTab = 1;
constexpr int kSqlTab       = 2;

const Color kErrorColor(176, 32, 32);
const Color kStatusColor(70, 70, 76);
const Color kProductionColor(190, 30, 30);

const char* const kPlaceholderText = "…";

// ----- layout helpers, as UltraNetMonitor has them -----

// A pane's single child fills it edge to edge: basis zero and grow one, so
// the pane's size decides and the child holds no size of its own against it.
void FillWith(const std::shared_ptr<UltraCanvasContainer>& container,
              const std::shared_ptr<UltraCanvasUIElement>& element, float grow = 1.0f) {
    container->AddChild(element);
    element->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    element->layoutItem.SetFlexGrow(grow);
    element->layoutItem.SetFlexShrink(1);
    element->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
}

void MakePlainColumn(const std::shared_ptr<UltraCanvasContainer>& container) {
    container->layout.SetFlexColumn()
                     .SetFlexGap(kSectionGap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    container->SetContainerStyle(plain);
}

std::shared_ptr<UltraCanvasContainer> MakeToolbar(const std::string& id) {
    auto bar = CreateContainer(id, 0, 0, 0, 34);
    bar->layout.SetFlexRow()
               .SetFlexGap(8)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bar->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(34));
    ContainerStyle plainBar;
    plainBar.autoShowScrollbars = false;
    bar->SetContainerStyle(plainBar);
    return bar;
}

void PinToolbar(const std::shared_ptr<UltraCanvasUIElement>& bar) {
    bar->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    bar->layoutItem.SetFlexGrow(0);
    bar->layoutItem.SetFlexShrink(0);
}

// As wide as its label: given no width, the layout measures the text.
std::shared_ptr<UltraCanvasButton> MakeToolbarButton(const std::string& id, const std::string& text,
                                                     const std::string& tooltip) {
    auto button = CreateButton(id, 0, 0, 0, kControlHeight, text);
    button->layoutItem.SetFlexShrink(0);
    button->SetTooltip(tooltip);
    return button;
}

// Takes what the controls leave, and gives it back first (ellipsized).
std::shared_ptr<UltraCanvasLabel> MakeBarStatus(const std::string& id, const std::string& text = "") {
    auto label = CreateLabel(id, 0, 0, 0, 24, text);
    label->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    label->layoutItem.SetFlexGrow(1);
    label->layoutItem.SetFlexShrink(1);
    label->SetTextColor(kStatusColor);
    label->SetAlignment(TextAlignment::Left);
    return label;
}

// A tab's page: no size of its own - the tabbed container sizes it.
std::shared_ptr<UltraCanvasContainer> MakeTabPage(const std::string& id) {
    auto page = CreateContainer(id, 0, 0, 0, 0);
    MakePlainColumn(page);
    page->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    page->SetPadding(kTabPadding);
    return page;
}

SplitPaneStyle PaneStyle() {
    SplitPaneStyle style;
    style.splitterThickness = 6;
    style.splitterHitMargin = 3;
    style.splitterColor     = Color(232, 232, 236);
    style.handle.shape      = SplitterHandleShape::RoundedSquare;
    style.handle.crossSize  = 9;
    style.handle.axisLength = 44;
    return style;
}

std::shared_ptr<UltraCanvasListView> MakeList(const std::string& id, const std::shared_ptr<IListModel>& model) {
    auto view = std::make_shared<UltraCanvasListView>(id, -1, -1, 400, 200);
    view->SetModel(model);
    view->SetShowHeader(true);
    view->SetShowItemTooltips(true);
    return view;
}

const char* KindWord(UltraDbObjectKind kind) {
    switch (kind) {
        case UltraDbObjectKind::Table:            return "table";
        case UltraDbObjectKind::View:             return "view";
        case UltraDbObjectKind::MaterializedView: return "materialized view";
        case UltraDbObjectKind::ForeignTable:     return "foreign table";
        case UltraDbObjectKind::Sequence:         return "sequence";
    }
    return "object";
}

// "4 512" rather than "4512": a row count is read at a glance.
std::string Grouped(int64_t value) {
    std::string digits = std::to_string(value < 0 ? -value : value);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += " ";   // thin space
        out += digits[i];
    }
    return value < 0 ? "-" + out : out;
}

std::string Seconds(double seconds) {
    // locale-ok: a duration for the reader, in the desktop's own decimal mark.
    char text[32];
    std::snprintf(text, sizeof text, seconds < 10.0 ? "%.2f s" : "%.1f s", seconds);
    return text;
}

// A message for a one-line label: a server's error can run to three lines
// ("... Is the server running on that host?"); the tooltip has all of it.
std::string OneLine(const std::string& text) {
    return CellText(UltraDbValue(text), 240);
}

// The first line of a script, for the history dropdown.
std::string FirstLine(const std::string& script) {
    return CellText(UltraDbValue(script), 70);
}

} // namespace

DbAdminWindow::~DbAdminWindow() {
    // Nothing posted from now on reaches the window; then the threads end.
    alive_.reset();
    jobs_.Stop();
    runners_.clear();
}

bool DbAdminWindow::Initialize(const std::string& dataDirectory) {
    std::string error;
    if (dataDirectory.empty()) {
        store_.OpenInMemory();
        storeNote_ = "No data folder: connections are kept until the app closes.";
    } else if (!store_.Open(dataDirectory, error)) {
        store_.OpenInMemory();
        storeNote_ = "Connections cannot be saved (" + error + "); they are kept until the app closes.";
    }

    WindowConfig config;
    config.title     = std::string("UltraDBAdmin ") + ULTRADBADMIN_VERSION;
    config.width     = static_cast<int>(kWindowWidth);
    config.height    = static_cast<int>(kWindowHeight);
    config.minWidth  = 860;
    config.minHeight = 520;
    window_ = CreateWindow(config);
    if (!window_) return false;

    page_ = CreateContainer("dbaPage", 0, 0, 0, 0);
    MakePlainColumn(page_);
    page_->SetPadding(kPagePadding);

    auto toolbar = BuildToolbar();
    page_->AddChild(toolbar);
    PinToolbar(toolbar);

    split_ = CreateHorizontalSplitPane("dbaSplit", 0, 0, 0, 0);
    split_->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    split_->SetSplitPaneStyle(PaneStyle());
    auto treePane = split_->AddPane(1.0);
    auto tabPane  = split_->AddPane(3.0);
    split_->SetPaneFixedSize(0, kTreeWidth);
    split_->SetPaneMinSize(0, kTreeMinWidth);
    split_->SetPaneMinSize(1, 420);
    MakePlainColumn(treePane);
    MakePlainColumn(tabPane);
    FillWith(treePane, BuildTreePane());

    tabs_ = CreateTabbedContainer("dbaTabs", 0, 0, 0, 0);
    tabs_->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    tabs_->AddTab("Browse", BuildBrowsePage());
    tabs_->AddTab("Structure", BuildStructurePage());
    tabs_->AddTab("SQL", BuildSqlPage());
    tabs_->SetActiveTab(kBrowseTab);
    FillWith(tabPane, tabs_);
    FillWith(page_, split_);

    window_->AddChild(page_);
    LayoutForSize(kWindowWidth, kWindowHeight);
    window_->onWindowResize = [this](int width, int height) {
        LayoutForSize(static_cast<float>(width), static_cast<float>(height));
    };

    // Every connection gets its two sessions; each opens on its first query.
    for (const auto& spec : store_.All()) {
        const UltraDbResult registered = store_.Register(spec);
        if (!registered) storeNote_ = spec.name + ": " + registered.message;
    }
    RebuildTree();
    ClearObject("Pick a table or view in the tree.");
    UpdateSqlBar();
    if (!storeNote_.empty()) SetStatus(storeNote_, true);
    else if (store_.All().empty()) SetStatus("No connections yet - Add connection… names a SQLite file or a PostgreSQL server.");
    else SetStatus(std::to_string(store_.All().size()) + " connections. Expand one to see its tables.");
    return true;
}

void DbAdminWindow::Show() {
    if (window_) window_->Show();
}

void DbAdminWindow::LayoutForSize(float width, float height) {
    if (!page_) return;
    // SetElementSize, not SetBounds: the layout engine sizes from the CSS
    // dimensions, so a bounds-only change is overwritten on the next pass.
    page_->SetElementAbsolutePosition(Point2Df(0.0f, 0.0f));
    page_->SetElementSize(Size2Df(std::max(400.0f, width), std::max(240.0f, height)));
    page_->InvalidateLayout();
    if (window_) window_->AddDirtyRectangle(
        Rect2Di(0, 0, static_cast<int>(width), static_cast<int>(height)));
}

// ===== CONSTRUCTION =====

std::shared_ptr<UltraCanvasContainer> DbAdminWindow::BuildToolbar() {
    auto bar = MakeToolbar("dbaToolbar");

    auto add = MakeToolbarButton("dbaAdd", "Add connection…", "A SQLite file or a PostgreSQL server");
    add->SetOnClick([this]() { AddConnection(); });
    bar->AddChild(add);

    editButton_ = MakeToolbarButton("dbaEdit", "Edit…", "Change the selected connection");
    editButton_->SetOnClick([this]() { EditConnection(); });
    bar->AddChild(editButton_);

    removeButton_ = MakeToolbarButton("dbaRemove", "Remove",
                                      "Take the selected connection off the list; the database is not touched");
    removeButton_->SetOnClick([this]() { RemoveConnection(); });
    bar->AddChild(removeButton_);

    auto refresh = MakeToolbarButton("dbaRefresh", "Refresh",
                                     "Read the selected connection's schemas and tables again");
    refresh->SetOnClick([this]() { RefreshSelected(); });
    bar->AddChild(refresh);

    statusLabel_ = MakeBarStatus("dbaStatus");
    bar->AddChild(statusLabel_);
    return bar;
}

std::shared_ptr<UltraCanvasContainer> DbAdminWindow::BuildTreePane() {
    auto pane = CreateContainer("dbaTreePane", 0, 0, 0, 0);
    MakePlainColumn(pane);
    tree_ = std::make_shared<UltraCanvasTreeView>("dbaTree");
    tree_->SetRowHeight(24);
    tree_->SetSelectionMode(TreeSelectionMode::Single);
    // A connection is opened only when it is expanded: the children are read
    // then, from the placeholder each unread node carries.
    tree_->onNodeExpanded = [this](TreeNode* node) {
        if (!node || node->children.size() != 1) return;
        const NodeRef* child = RefOf(node->children.front()->data.nodeId);
        if (child && child->kind == NodeRef::Kind::Placeholder) LoadChildren(node->data.nodeId);
    };
    tree_->onNodeSelected = [this](TreeNode* node) { OnNodeSelected(node); };
    FillWith(pane, tree_);
    return pane;
}

std::shared_ptr<UltraCanvasContainer> DbAdminWindow::BuildBrowsePage() {
    auto page = MakeTabPage("dbaBrowsePage");
    auto bar = MakeToolbar("dbaBrowseBar");

    prevButton_ = MakeToolbarButton("dbaPrev", "Previous", "The page before");
    prevButton_->SetOnClick([this]() { if (pageIndex_ > 0) LoadPage(pageIndex_ - 1); });
    bar->AddChild(prevButton_);

    nextButton_ = MakeToolbarButton("dbaNext", "Next", "The page after");
    nextButton_->SetOnClick([this]() { LoadPage(pageIndex_ + 1); });
    bar->AddChild(nextButton_);

    browseInfo_ = CreateLabel("dbaBrowseInfo", 0, 0, 220, 24, "");
    browseInfo_->layoutItem.SetFlexShrink(0);
    bar->AddChild(browseInfo_);

    browseNote_ = MakeBarStatus("dbaBrowseNote");
    bar->AddChild(browseNote_);

    reloadButton_ = MakeToolbarButton("dbaReload", "Reload", "Read this page and the row count again");
    reloadButton_->SetOnClick([this]() {
        if (!pager_) return;
        LoadPage(pageIndex_);
        CountRows();
    });
    bar->AddChild(reloadButton_);

    page->AddChild(bar);
    PinToolbar(bar);

    browseModel_ = std::make_shared<ResultSetModel>();
    browseView_ = MakeList("dbaBrowseGrid", browseModel_);
    // The order is the server's: a click on a header asks for the page again,
    // ORDER BY that column (then the key) - never a sort of the 200 rows shown.
    browseView_->onHeaderClicked = [this](int column) { SortBrowse(column); };
    FillWith(page, browseView_);
    return page;
}

std::shared_ptr<UltraCanvasContainer> DbAdminWindow::BuildStructurePage() {
    auto page = MakeTabPage("dbaStructurePage");
    structureInfo_ = CreateLabel("dbaStructureInfo", 0, 0, 0, 24, "");
    structureInfo_->SetTextColor(kStatusColor);
    page->AddChild(structureInfo_);
    PinToolbar(structureInfo_);

    auto split = CreateVerticalSplitPane("dbaStructureSplit", 0, 0, 0, 0);
    split->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    split->SetSplitPaneStyle(PaneStyle());
    auto columnsPane     = split->AddPane(3.0);
    auto constraintsPane = split->AddPane(2.0);
    auto definitionPane  = split->AddPane(1.5);
    for (int i = 0; i < 3; ++i) split->SetPaneMinSize(i, 60);
    MakePlainColumn(columnsPane);
    MakePlainColumn(constraintsPane);
    MakePlainColumn(definitionPane);

    columnModel_ = std::make_shared<ColumnSchemaModel>();
    columnView_ = MakeList("dbaColumns", columnModel_);
    FillWith(columnsPane, columnView_);

    constraintModel_ = std::make_shared<ConstraintModel>();
    constraintView_ = MakeList("dbaConstraints", constraintModel_);
    FillWith(constraintsPane, constraintView_);

    definitionArea_ = std::make_shared<UltraCanvasTextArea>("dbaDefinition");
    definitionArea_->ApplyCodeStyle("SQL");
    definitionArea_->SetReadOnly(true);
    definitionArea_->SetWordWrap(true);
    FillWith(definitionPane, definitionArea_);

    FillWith(page, split);
    return page;
}

std::shared_ptr<UltraCanvasContainer> DbAdminWindow::BuildSqlPage() {
    auto page = MakeTabPage("dbaSqlPage");
    auto bar = MakeToolbar("dbaSqlBar");

    runButton_ = MakeToolbarButton("dbaRun", "Run",
                                   "Run the selected text, or the whole script (Ctrl+Enter)");
    runButton_->SetOnClick([this]() { RunSql(); });
    bar->AddChild(runButton_);

    stopButton_ = MakeToolbarButton("dbaStop", "Stop", "Cancel the running statement");
    stopButton_->SetOnClick([this]() { StopSql(); });
    bar->AddChild(stopButton_);

    historyDropdown_ = CreateDropdown("dbaHistory", 0, 0, 260, kControlHeight);
    historyDropdown_->layoutItem.SetFlexShrink(0);
    historyDropdown_->SetTooltip("Scripts run on this connection, newest first: pick one to put it back in the editor");
    historyDropdown_->onSelectionChanged = [this](int index, const DropdownItem&) {
        if (index >= 0 && static_cast<size_t>(index) < historyShown_.size())
            sqlEditor_->SetText(historyShown_[static_cast<size_t>(index)]);
    };
    bar->AddChild(historyDropdown_);

    sqlInfo_ = MakeBarStatus("dbaSqlInfo");
    bar->AddChild(sqlInfo_);

    page->AddChild(bar);
    PinToolbar(bar);

    auto split = CreateVerticalSplitPane("dbaSqlSplit", 0, 0, 0, 0);
    split->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    split->SetSplitPaneStyle(PaneStyle());
    auto editorPane  = split->AddPane(2.0);
    auto resultsPane = split->AddPane(3.0);
    split->SetPaneMinSize(0, 80);
    split->SetPaneMinSize(1, 120);
    MakePlainColumn(editorPane);
    MakePlainColumn(resultsPane);

    sqlEditor_ = std::make_shared<UltraCanvasTextArea>("dbaSqlEditor");
    sqlEditor_->ApplyCodeStyle("SQL");
    sqlEditor_->onBeforeKeyDown = [this](const UCEvent& event) {
        const bool enter = event.virtualKey == UCKeys::Return || event.virtualKey == UCKeys::NumPadEnter;
        if (enter && (event.ctrl || event.meta)) {
            RunSql();
            return true;
        }
        return false;
    };
    FillWith(editorPane, sqlEditor_);

    auto results = CreateHorizontalSplitPane("dbaResultsSplit", 0, 0, 0, 0);
    results->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    results->SetSplitPaneStyle(PaneStyle());
    auto statementPane = results->AddPane(1.0);
    auto rowsPane      = results->AddPane(2.0);
    results->SetPaneFixedSize(0, kStatementWidth);
    results->SetPaneMinSize(0, 200);
    results->SetPaneMinSize(1, 240);
    MakePlainColumn(statementPane);
    MakePlainColumn(rowsPane);

    statementModel_ = std::make_shared<StatementModel>();
    statementView_ = MakeList("dbaStatements", statementModel_);
    statementView_->onSelectionChanged = [this](const std::vector<int>& rows) {
        if (!rows.empty()) ShowStatementRows(rows.front());
    };
    FillWith(statementPane, statementView_);

    // A console result is in memory already, so it sorts where it is.
    sqlResultModel_ = std::make_shared<ResultSetModel>();
    sqlResultProxy_ = std::make_shared<UltraCanvasListSortFilterProxy>(sqlResultModel_);
    sqlResultView_ = MakeList("dbaSqlRows", sqlResultProxy_);
    UltraCanvasListView* rawView = sqlResultView_.get();
    UltraCanvasListSortFilterProxy* rawProxy = sqlResultProxy_.get();
    sqlResultView_->onHeaderClicked = [rawView, rawProxy](int column) {
        const bool ascending = !(column == rawView->GetSortColumn() && rawView->GetSortAscending());
        rawProxy->SortByColumn(column, ascending ? ListSortOrder::Ascending : ListSortOrder::Descending);
        rawView->SetSortIndicator(column, ascending);
    };
    FillWith(rowsPane, sqlResultView_);

    FillWith(resultsPane, results);
    FillWith(page, split);
    return page;
}

// ===== BACKGROUND =====

void DbAdminWindow::Background(std::function<std::function<void()>()> work) {
    std::weak_ptr<char> alive = alive_;
    jobs_.Post([work = std::move(work), alive]() {
        std::function<void()> apply = work();
        if (!apply) return;
        auto* app = UltraCanvasApplicationBase::GetCurrent();
        if (!app || alive.expired()) return;
        app->PostToUIThread([apply = std::move(apply), alive]() {
            if (!alive.expired()) apply();
        });
    });
}

void DbAdminWindow::SetStatus(const std::string& text, bool error) {
    if (!statusLabel_) return;
    statusLabel_->SetText(OneLine(text));
    statusLabel_->SetTextColor(error ? kErrorColor : kStatusColor);
    statusLabel_->SetTooltip(text);
}

// ===== CONNECTIONS AND THE TREE =====

const DbAdminWindow::NodeRef* DbAdminWindow::RefOf(const std::string& nodeId) const {
    const auto it = nodes_.find(nodeId);
    return it == nodes_.end() ? nullptr : &it->second;
}

const ConnectionSpec* DbAdminWindow::SelectedConnection() const {
    const NodeRef* ref = RefOf(selectedNode_);
    return ref ? store_.Find(ref->connectionId) : nullptr;
}

void DbAdminWindow::RebuildTree() {
    nodes_.clear();
    loading_.clear();
    selectedNode_.clear();
    tree_->SetRootNode(TreeNodeData("root", "Connections"));
    tree_->SetRootVisible(false);
    for (const auto& spec : store_.All()) AddConnectionNode(spec);
    tree_->RequestRedraw();
}

void DbAdminWindow::AddConnectionNode(const ConnectionSpec& spec) {
    const std::string id = "c/" + spec.id;
    TreeNodeData data(id, spec.name);
    data.tooltip = Describe(spec) + (spec.readOnly ? "\nread only" : "") +
                   (spec.production ? "\nPRODUCTION" : "");
    if (spec.production) data.textColor = kProductionColor;
    tree_->AddNode("root", data);
    NodeRef ref;
    ref.kind = NodeRef::Kind::Connection;
    ref.connectionId = spec.id;
    nodes_[id] = ref;
    ResetNode(id);
}

void DbAdminWindow::ResetNode(const std::string& nodeId) {
    TreeNode* node = tree_->FindNode(nodeId);
    if (!node) return;
    std::vector<std::string> children;
    for (const auto& child : node->children) children.push_back(child->data.nodeId);
    for (const auto& child : children) {
        // Everything below goes, and so does what the window knew of it.
        for (auto it = nodes_.begin(); it != nodes_.end();) {
            if (it->first == child || it->first.rfind(child + "/", 0) == 0) it = nodes_.erase(it);
            else ++it;
        }
        tree_->RemoveNode(child);
    }
    loading_.erase(nodeId);
    if (node->IsExpanded()) tree_->CollapseNode(node);
    const std::string placeholder = nodeId + "/" + kPlaceholderText;
    tree_->AddNode(nodeId, TreeNodeData(placeholder, kPlaceholderText));
    NodeRef ref = nodes_[nodeId];
    ref.kind = NodeRef::Kind::Placeholder;
    nodes_[placeholder] = ref;
}

void DbAdminWindow::LoadChildren(const std::string& nodeId) {
    const NodeRef* ref = RefOf(nodeId);
    if (!ref || loading_.count(nodeId)) return;
    const ConnectionSpec* spec = store_.Find(ref->connectionId);
    if (!spec) return;
    if (TreeNode* placeholder = tree_->FindNode(nodeId + "/" + kPlaceholderText)) {
        placeholder->data.text = "Loading…";
        placeholder->data.textColor = kStatusColor;
        tree_->RequestRedraw();
    }
    loading_.insert(nodeId);
    const std::string connection = RegisteredName(*spec);
    if (ref->kind == NodeRef::Kind::Connection) {
        SetStatus("Connecting to " + spec->name + "…");
        Background([this, connection, nodeId]() -> std::function<void()> {
            std::vector<std::string> schemas;
            const UltraDbResult result = UltraDb_ListSchemas(connection, schemas);
            return [this, nodeId, result, schemas]() { ApplySchemas(nodeId, result, schemas); };
        });
    } else if (ref->kind == NodeRef::Kind::Schema) {
        const std::string schema = ref->schema;
        Background([this, connection, schema, nodeId]() -> std::function<void()> {
            std::vector<UltraDbObjectSummary> objects;
            const UltraDbResult result = UltraDb_ListObjects(connection, schema, objects);
            return [this, nodeId, result, objects]() { ApplyObjects(nodeId, result, objects); };
        });
    } else {
        loading_.erase(nodeId);
    }
}

void DbAdminWindow::ShowLoadError(const std::string& nodeId, const UltraDbResult& result) {
    // The placeholder stays, saying what went wrong; collapsing and expanding
    // the node tries again.
    if (TreeNode* placeholder = tree_->FindNode(nodeId + "/" + kPlaceholderText)) {
        placeholder->data.text = "Could not read: " + CellText(UltraDbValue(result.message), 80);
        placeholder->data.tooltip = result.message;
        placeholder->data.textColor = kErrorColor;
    }
    SetStatus(result.message, true);
    tree_->RequestRedraw();
}

void DbAdminWindow::ApplySchemas(const std::string& nodeId, const UltraDbResult& result,
                                 const std::vector<std::string>& schemas) {
    loading_.erase(nodeId);
    const NodeRef* ref = RefOf(nodeId);
    if (!ref || !tree_->FindNode(nodeId)) return;   // removed meanwhile
    if (!result) { ShowLoadError(nodeId, result); return; }
    const NodeRef parent = *ref;
    const std::string placeholder = nodeId + "/" + kPlaceholderText;
    tree_->RemoveNode(placeholder);
    nodes_.erase(placeholder);
    std::string only;
    for (const auto& schema : schemas) {
        const std::string id = nodeId + "/" + std::to_string(++nextNodeId_);
        TreeNodeData data(id, schema);
        tree_->AddNode(nodeId, data);
        NodeRef child = parent;
        child.kind = NodeRef::Kind::Schema;
        child.schema = schema;
        nodes_[id] = child;
        ResetNode(id);
        only = id;
    }
    if (schemas.empty()) {
        const std::string id = nodeId + "/" + std::to_string(++nextNodeId_);
        tree_->AddNode(nodeId, TreeNodeData(id, "(no schemas)"));
    }
    const ConnectionSpec* spec = store_.Find(parent.connectionId);
    SetStatus((spec ? spec->name : std::string("Connection")) + ": " + std::to_string(schemas.size()) +
              (schemas.size() == 1 ? " schema." : " schemas."));
    // A SQLite file is one schema, "main": open it straight away.
    if (schemas.size() == 1) {
        if (TreeNode* node = tree_->FindNode(only)) tree_->ExpandNode(node);
        LoadChildren(only);
    }
    tree_->RequestRedraw();
}

void DbAdminWindow::ApplyObjects(const std::string& nodeId, const UltraDbResult& result,
                                 const std::vector<UltraDbObjectSummary>& objects) {
    loading_.erase(nodeId);
    const NodeRef* ref = RefOf(nodeId);
    if (!ref || !tree_->FindNode(nodeId)) return;
    if (!result) { ShowLoadError(nodeId, result); return; }
    const NodeRef parent = *ref;
    const std::string placeholder = nodeId + "/" + kPlaceholderText;
    tree_->RemoveNode(placeholder);
    nodes_.erase(placeholder);
    for (const auto& object : objects) {
        const std::string id = nodeId + "/" + std::to_string(++nextNodeId_);
        std::string text = object.name;
        if (object.kind != UltraDbObjectKind::Table) text += std::string("  (") + KindWord(object.kind) + ")";
        TreeNodeData data(id, text);
        data.tooltip = object.schema + "." + object.name + " - " + KindWord(object.kind);
        tree_->AddNode(nodeId, data);
        NodeRef child = parent;
        child.kind = NodeRef::Kind::Object;
        child.name = object.name;
        child.objectKind = object.kind;
        nodes_[id] = child;
    }
    if (objects.empty()) {
        const std::string id = nodeId + "/" + std::to_string(++nextNodeId_);
        tree_->AddNode(nodeId, TreeNodeData(id, "(empty)"));
    }
    tree_->RequestRedraw();
}

void DbAdminWindow::OnNodeSelected(TreeNode* node) {
    if (!node) return;
    const NodeRef* ref = RefOf(node->data.nodeId);
    if (!ref || ref->kind == NodeRef::Kind::Placeholder) return;
    selectedNode_ = node->data.nodeId;
    UpdateSqlBar();
    if (ref->kind == NodeRef::Kind::Object) OpenObject(*ref);
}

void DbAdminWindow::AddConnection() {
    ShowConnectionDialog(window_.get(), nullptr,
                         [this](const ConnectionSpec& spec, const std::string& password, bool clearPassword) {
                             return SaveConnection(spec, password, clearPassword);
                         });
}

void DbAdminWindow::EditConnection() {
    const ConnectionSpec* spec = SelectedConnection();
    if (!spec) { SetStatus("Select a connection in the tree to edit it."); return; }
    ShowConnectionDialog(window_.get(), spec,
                         [this](const ConnectionSpec& edited, const std::string& password, bool clearPassword) {
                             return SaveConnection(edited, password, clearPassword);
                         });
}

std::string DbAdminWindow::SaveConnection(const ConnectionSpec& spec, const std::string& password,
                                          bool clearPassword) {
    const bool edited = !spec.id.empty();
    std::string error;
    const std::string id = store_.Put(spec, password, clearPassword, error);
    if (id.empty()) return "Not saved: " + error;
    const ConnectionSpec saved = *store_.Find(id);
    if (edited && objectConnectionId_ == id) ClearObject("The connection changed: pick the table again.");
    // New settings take effect with the next query; a script or page still
    // running finishes on the old ones.
    const UltraDbResult registered = store_.Register(saved);
    if (!registered) return "Saved, but it cannot be opened: " + registered.message;
    RebuildTree();
    if (TreeNode* node = tree_->FindNode("c/" + id)) {
        tree_->SelectNode(node);
        selectedNode_ = "c/" + id;
    }
    UpdateSqlBar();
    SetStatus(saved.name + (edited ? " saved." : " added. Expand it to see its tables."));
    return std::string();
}

void DbAdminWindow::RemoveConnection() {
    const ConnectionSpec* spec = SelectedConnection();
    if (!spec) { SetStatus("Select a connection in the tree to remove it."); return; }
    const std::string id = spec->id;
    const std::string name = spec->name;
    UltraCanvasDialogManager::ShowConfirmation(
        "Remove \"" + name + "\" from the list?\n\nThe database itself is not touched. A password "
        "stored for it is deleted from the vault.",
        "Remove connection",
        [this, id, name](bool confirmed) {
            if (!confirmed) return;
            auto runner = runners_.find(id);
            if (runner != runners_.end()) {
                if (runner->second->IsRunning()) {
                    SetStatus("A script is still running on " + name + ": stop it first.", true);
                    return;
                }
                runners_.erase(runner);
            }
            if (objectConnectionId_ == id) ClearObject("Pick a table or view in the tree.");
            std::string error;
            if (!store_.Remove(id, error)) { SetStatus("Not removed: " + error, true); return; }
            RebuildTree();
            UpdateSqlBar();
            SetStatus(name + " removed.");
        },
        window_.get());
}

void DbAdminWindow::RefreshSelected() {
    const NodeRef* ref = RefOf(selectedNode_);
    if (!ref) { SetStatus("Select a connection or schema to read it again."); return; }
    // An object row refreshes its schema; the schema row is its parent.
    std::string nodeId = selectedNode_;
    if (ref->kind == NodeRef::Kind::Object) nodeId = nodeId.substr(0, nodeId.rfind('/'));
    // A connection is opened afresh, so a server that came back is reached.
    const ConnectionSpec* spec = store_.Find(ref->connectionId);
    if (ref->kind == NodeRef::Kind::Connection && spec) store_.Register(*spec);
    ResetNode(nodeId);
    if (TreeNode* node = tree_->FindNode(nodeId)) {
        tree_->ExpandNode(node);
        tree_->SelectNode(node);
        selectedNode_ = nodeId;
    }
    LoadChildren(nodeId);
    tree_->RequestRedraw();
}

// ===== BROWSE AND STRUCTURE =====

void DbAdminWindow::ClearObject(const std::string& message) {
    ++objectGen_;
    ++viewGen_;
    objectConnectionId_.clear();
    objectLabel_.clear();
    pager_.reset();
    pageIndex_ = 0;
    pageRows_ = 0;
    rowCount_ = -1;
    pageLoading_ = false;
    browseModel_->Clear();
    browseView_->ClearSortIndicator();
    columnModel_->Replace(UltraDbTableSchema());
    constraintModel_->Replace(UltraDbTableSchema());
    definitionArea_->SetText("", false);
    structureInfo_->SetText(message);
    browseNote_->SetText(message);
    browseNote_->SetTextColor(kStatusColor);
    UpdateBrowseBar();
}

void DbAdminWindow::OpenObject(const NodeRef& ref) {
    const ConnectionSpec* spec = store_.Find(ref.connectionId);
    if (!spec) return;
    ClearObject("Reading " + ref.schema + "." + ref.name + "…");
    objectConnectionId_ = spec->id;
    objectLabel_ = ref.schema + "." + ref.name;
    const uint64_t generation = objectGen_;
    const std::string connection = RegisteredName(*spec);
    const std::string schema = ref.schema;
    const std::string name = ref.name;
    if (ref.objectKind == UltraDbObjectKind::Sequence) {
        // A sequence is not a table: its one row is its state.
        Background([this, connection, schema, name, generation]() -> std::function<void()> {
            UltraDbResultSet rows;
            std::string target;
            UltraDbResult result = UltraDb_QuoteQualifiedName(connection, schema, name, target);
            if (result) result = UltraDb_Query(connection, "SELECT * FROM " + target, {}, rows);
            return [this, generation, result, rows]() { ApplySequence(generation, result, rows); };
        });
        return;
    }
    Background([this, connection, schema, name, generation]() -> std::function<void()> {
        UltraDbTableSchema table;
        const UltraDbResult result = UltraDb_DescribeTable(connection, schema, name, table);
        return [this, generation, result, table]() { ApplyTable(generation, result, table); };
    });
}

void DbAdminWindow::ApplyTable(uint64_t generation, const UltraDbResult& result, const UltraDbTableSchema& table) {
    if (generation != objectGen_) return;
    if (!result) {
        const std::string label = objectLabel_;
        ClearObject(label + ": " + OneLine(result.message));
        browseNote_->SetTextColor(kErrorColor);
        SetStatus(result.message, true);
        return;
    }
    columnModel_->Replace(table);
    constraintModel_->Replace(table);

    std::string definition = table.viewDefinition;
    for (const auto& trigger : table.triggers) {
        if (!definition.empty()) definition += "\n\n";
        definition += trigger.definition;
    }
    definitionArea_->SetText(definition, false);

    std::string info = objectLabel_ + " - " + KindWord(table.kind) + ", " +
                       std::to_string(table.columns.size()) + (table.columns.size() == 1 ? " column" : " columns");
    if (table.approxRows >= 0) info += ", about " + Grouped(table.approxRows) + " rows";
    if (table.sizeBytes >= 0) info += ", " + HumanSize(table.sizeBytes);
    if (!table.comment.empty()) info += " - " + table.comment;
    structureInfo_->SetText(info);

    pager_ = std::make_shared<TablePager>(RegisteredName(*store_.Find(objectConnectionId_)), table, kPageSize);
    LoadPage(0);
    CountRows();
}

void DbAdminWindow::ApplySequence(uint64_t generation, const UltraDbResult& result, const UltraDbResultSet& rows) {
    if (generation != objectGen_) return;
    if (!result) {
        const std::string label = objectLabel_;
        ClearObject(label + ": " + OneLine(result.message));
        browseNote_->SetTextColor(kErrorColor);
        SetStatus(result.message, true);
        return;
    }
    browseModel_->Replace(rows);
    browseNote_->SetText("A sequence: its current state.");
    structureInfo_->SetText(objectLabel_ + " - sequence");
    UpdateBrowseBar();
}

void DbAdminWindow::LoadPage(size_t page) {
    if (!pager_) return;
    pageIndex_ = page;
    pageLoading_ = true;
    UpdateBrowseBar();
    const uint64_t generation = viewGen_;
    std::shared_ptr<const TablePager> pager = pager_;
    Background([this, pager, page, generation]() -> std::function<void()> {
        UltraDbResultSet rows;
        const UltraDbResult result = pager->LoadPage(page, rows);
        return [this, generation, page, result, rows]() { ApplyPage(generation, page, result, rows); };
    });
}

void DbAdminWindow::ApplyPage(uint64_t generation, size_t page, const UltraDbResult& result, UltraDbResultSet rows) {
    if (generation != viewGen_ || page != pageIndex_ || !pager_) return;
    pageLoading_ = false;
    if (!result) {
        browseModel_->Clear();
        pageRows_ = 0;
        browseNote_->SetText(OneLine(result.message));
        browseNote_->SetTextColor(kErrorColor);
        SetStatus(result.message, true);
        UpdateBrowseBar();
        return;
    }
    // The header tooltips name each column's declared type.
    std::vector<std::string> types;
    for (const auto& column : rows.Columns()) {
        std::string type;
        for (const auto& c : pager_->Table().columns) {
            if (c.name == column) { type = c.declaredType; break; }
        }
        types.push_back(type);
    }
    pageRows_ = rows.Size();
    browseModel_->Replace(std::move(rows), std::move(types));
    browseView_->ScrollToRow(0);
    if (pager_->HasStableOrder()) {
        browseNote_->SetText("");
    } else {
        browseNote_->SetText("No primary key: while the table changes, a row can move between pages.");
    }
    browseNote_->SetTextColor(kStatusColor);
    UpdateBrowseBar();
}

void DbAdminWindow::CountRows() {
    if (!pager_) return;
    rowCount_ = -1;
    UpdateBrowseBar();
    const uint64_t generation = objectGen_;
    std::shared_ptr<const TablePager> pager = pager_;
    Background([this, pager, generation]() -> std::function<void()> {
        int64_t rows = 0;
        const UltraDbResult result = pager->CountRows(rows);
        return [this, generation, result, rows]() { ApplyCount(generation, result, rows); };
    });
}

void DbAdminWindow::ApplyCount(uint64_t generation, const UltraDbResult& result, int64_t rows) {
    if (generation != objectGen_) return;
    rowCount_ = result ? rows : -1;
    if (!result) SetStatus("Rows not counted: " + result.message, true);
    UpdateBrowseBar();
}

void DbAdminWindow::SortBrowse(int column) {
    if (!pager_ || column < 0) return;
    const auto& columns = browseModel_->Rows().Columns();
    if (static_cast<size_t>(column) >= columns.size()) return;
    const std::string& name = columns[static_cast<size_t>(column)];
    const bool ascending = !(pager_->SortColumn() == name && pager_->SortAscending());
    auto sorted = std::make_shared<TablePager>(*pager_);
    sorted->SetSort(name, ascending);
    pager_ = sorted;
    ++viewGen_;
    browseView_->SetSortIndicator(column, ascending);
    LoadPage(0);
}

void DbAdminWindow::UpdateBrowseBar() {
    if (!browseInfo_) return;
    const bool open = pager_ != nullptr;
    std::string info;
    if (!open) {
        info = "";
    } else if (pageLoading_) {
        info = "Page " + std::to_string(pageIndex_ + 1) + ", reading…";
    } else if (pageRows_ == 0) {
        info = pageIndex_ == 0 ? "No rows" : "No rows on page " + std::to_string(pageIndex_ + 1);
    } else {
        const int64_t first = static_cast<int64_t>(pageIndex_ * kPageSize) + 1;
        const int64_t last = first + static_cast<int64_t>(pageRows_) - 1;
        info = "Rows " + Grouped(first) + "–" + Grouped(last) + " of " +
               (rowCount_ >= 0 ? Grouped(rowCount_) : std::string("…"));
    }
    browseInfo_->SetText(info);
    prevButton_->SetDisabled(!open || pageLoading_ || pageIndex_ == 0);
    const bool more = rowCount_ >= 0 ? static_cast<int64_t>((pageIndex_ + 1) * kPageSize) < rowCount_
                                     : pageRows_ == kPageSize;
    nextButton_->SetDisabled(!open || pageLoading_ || !more);
    reloadButton_->SetDisabled(!open);
}

// ===== SQL =====

SqlRunner* DbAdminWindow::RunnerFor(const ConnectionSpec& spec) {
    auto& runner = runners_[spec.id];
    if (!runner) runner = std::make_unique<SqlRunner>(ConsoleName(spec));
    return runner.get();
}

void DbAdminWindow::RunSql() {
    const ConnectionSpec* spec = SelectedConnection();
    if (!spec) {
        sqlInfo_->SetText("Pick a connection (or anything in it) in the tree first.");
        sqlInfo_->SetTextColor(kErrorColor);
        return;
    }
    const std::string script = sqlEditor_->HasSelection() ? sqlEditor_->GetSelectedText() : sqlEditor_->GetText();
    if (script.find_first_not_of(" \t\r\n;") == std::string::npos) return;
    SqlRunner* runner = RunnerFor(*spec);
    const std::string id = spec->id;
    std::weak_ptr<char> alive = alive_;
    const bool started = runner->Start(script, [this, alive, id](const RunOutcome& outcome) {
        // On the runner's thread: hand the outcome to the UI thread.
        auto* app = UltraCanvasApplicationBase::GetCurrent();
        if (!app || alive.expired()) return;
        app->PostToUIThread([this, alive, id, outcome]() {
            if (!alive.expired()) ApplyOutcome(id, outcome);
        });
    });
    if (!started) {
        sqlInfo_->SetText("A script is still running on this connection: Stop it, or wait.");
        sqlInfo_->SetTextColor(kErrorColor);
        return;
    }
    sqlConnectionId_ = id;
    UpdateSqlBar();
    FillHistory();
}

void DbAdminWindow::StopSql() {
    auto runner = runners_.find(sqlConnectionId_);
    if (runner == runners_.end() || !runner->second->IsRunning()) return;
    runner->second->Cancel();
    sqlInfo_->SetText("Stopping…");
}

void DbAdminWindow::ApplyOutcome(const std::string& connectionId, const RunOutcome& outcome) {
    const ConnectionSpec* spec = store_.Find(connectionId);
    const std::string where = spec ? spec->name : std::string("the connection");
    statementModel_->Replace(outcome.statements);
    // Show the rows of the last statement that returned any; the failure,
    // if there was one, is on the info line and in the list.
    int show = -1;
    for (int i = static_cast<int>(outcome.statements.size()) - 1; i >= 0 && show < 0; --i) {
        const auto& s = outcome.statements[static_cast<size_t>(i)];
        if (s.returnsRows && s.result.success) show = i;
    }
    ShowStatementRows(show);

    std::string info;
    bool bad = false;
    const size_t n = outcome.statements.size();
    if (outcome.cancelled) {
        info = "Stopped after " + Seconds(outcome.seconds) + ".";
        bad = true;
    } else if (!outcome.result.success) {
        info = (n > 0 ? "Statement " + std::to_string(n) + " failed: " : std::string("Failed: ")) +
               OneLine(outcome.result.message);
        bad = true;
    } else {
        info = std::to_string(n) + (n == 1 ? " statement" : " statements") + " on " + where +
               " in " + Seconds(outcome.seconds) + ".";
    }
    UpdateSqlBar();
    sqlInfo_->SetText(info);
    sqlInfo_->SetTooltip(outcome.result.message);
    sqlInfo_->SetTextColor(bad ? kErrorColor : kStatusColor);
}

void DbAdminWindow::ShowStatementRows(int row) {
    sqlResultProxy_->ClearSort();
    sqlResultView_->ClearSortIndicator();
    const UltraDbStatementResult* statement = statementModel_->At(row);
    if (statement && statement->returnsRows) sqlResultModel_->Replace(statement->rows);
    else sqlResultModel_->Clear();
}

void DbAdminWindow::UpdateSqlBar() {
    if (!runButton_) return;
    const ConnectionSpec* spec = SelectedConnection();
    auto running = runners_.find(sqlConnectionId_);
    const bool busy = running != runners_.end() && running->second->IsRunning();
    runButton_->SetDisabled(busy || !spec);
    stopButton_->SetDisabled(!busy);
    const bool selectedAny = spec != nullptr;
    editButton_->SetDisabled(!selectedAny);
    removeButton_->SetDisabled(!selectedAny);
    if (busy) {
        const ConnectionSpec* on = store_.Find(sqlConnectionId_);
        sqlInfo_->SetText("Running on " + (on ? on->name : std::string("the connection")) + "…");
        sqlInfo_->SetTextColor(kStatusColor);
    } else if (spec) {
        sqlInfo_->SetText(std::string("Runs on ") + spec->name + (spec->production ? " - PRODUCTION" : "") +
                          (spec->readOnly ? " (read only)" : ""));
        sqlInfo_->SetTextColor(spec->production ? kProductionColor : kStatusColor);
    } else {
        sqlInfo_->SetText("Pick a connection in the tree; the script runs there.");
        sqlInfo_->SetTextColor(kStatusColor);
    }
    FillHistory();
}

void DbAdminWindow::FillHistory() {
    const ConnectionSpec* spec = SelectedConnection();
    const auto runner = spec ? runners_.find(spec->id) : runners_.end();
    std::vector<std::string> history;
    if (runner != runners_.end()) history = runner->second->History();
    if (history == historyShown_) return;
    historyShown_ = std::move(history);
    historyDropdown_->ClearItems();
    for (const auto& script : historyShown_) historyDropdown_->AddItem(FirstLine(script));
}

} // namespace UltraDBAdmin

// Apps/UltraDBAdmin/ui/DbAdminWindow.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminWindow.h"

#include "DbAdminConnectionDialog.h"
#include "DbAdminFormat.h"
#include "DbAdminImportDialog.h"
#include "DbAdminSqlScript.h"
#include "DbAdminRowDialog.h"
#include "DbAdminStructureDialog.h"
#include "DbAdminTheme.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasCSVExportDialog.h"
#include "UltraCanvasCSVImportDialog.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasModalDialog.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
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
constexpr float kTabPadding    = 10.0f;
constexpr float kSectionGap    = 8.0f;
constexpr float kControlHeight = kButtonHeight;
constexpr float kBarHeight     = 34.0f;
constexpr float kHeaderHeight  = 48.0f;
constexpr int   kTreeWidth     = 280;
constexpr int   kTreeMinWidth  = 180;
constexpr int   kStatementWidth = 380;
constexpr size_t kPageSize     = 200;

// Tab order, as added below.
constexpr int kBrowseTab    = 0;
constexpr int kStructureTab = 1;
constexpr int kSqlTab       = 2;

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

std::shared_ptr<UltraCanvasContainer> MakeToolbar(const std::string& id, float height = kBarHeight) {
    auto bar = CreateContainer(id, 0, 0, 0, height);
    bar->layout.SetFlexRow()
               .SetFlexGap(6)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bar->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(height));
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

// Takes what the controls leave, and gives it back first (ellipsized).
std::shared_ptr<UltraCanvasLabel> MakeBarStatus(const std::string& id, const std::string& text = "") {
    auto label = CreateLabel(id, 0, 0, 0, 24, text);
    label->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    label->layoutItem.SetFlexGrow(1);
    label->layoutItem.SetFlexShrink(1);
    label->SetTextColor(StatusColor());
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

// The splitters are the gaps between the cards: the ground, a grip on it.
SplitPaneStyle PaneStyle() {
    SplitPaneStyle style;
    style.splitterThickness = 8;
    style.splitterHitMargin = 3;
    style.handle.shape      = SplitterHandleShape::RoundedSquare;
    style.handle.crossSize  = 8;
    style.handle.axisLength = 40;
    return SplitPaneStyle::FromTheme(UltraCanvasTheme::Current(), style);
}

// The tree's icon for a node of the given kind.
std::string ObjectIcon(UltraDbObjectKind kind) {
    switch (kind) {
        case UltraDbObjectKind::Table:            return IconPath("node-table");
        case UltraDbObjectKind::View:             return IconPath("node-view");
        case UltraDbObjectKind::MaterializedView: return IconPath("node-materialized-view");
        case UltraDbObjectKind::ForeignTable:     return IconPath("node-foreign-table");
        case UltraDbObjectKind::Sequence:         return IconPath("node-sequence");
    }
    return IconPath("node-table");
}

std::shared_ptr<UltraCanvasListView> MakeList(const std::string& id, const std::shared_ptr<IListModel>& model) {
    auto view = std::make_shared<UltraCanvasListView>(id, -1, -1, 400, 200);
    view->SetModel(model);
    view->SetShowHeader(true);
    view->SetShowItemTooltips(true);
    StyleList(*view);
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
    *exportStop_ = true;
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

    // White cards on the grey ground: the header, the tree and the tabs.
    page_ = CreateContainer("dbaPage", 0, 0, 0, 0);
    MakePlainColumn(page_);
    page_->SetPadding(kPagePadding);
    page_->SetBackgroundColor(UltraCanvasTheme::Current().roles.ground);

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
    // The tabs' card: the pane draws it, a pixel round the tabs.
    MakeCard(*tabPane);
    tabPane->SetPadding(1.0f);
    FillWith(treePane, BuildTreePane());

    tabs_ = CreateTabbedContainer("dbaTabs", 0, 0, 0, 0);
    tabs_->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    tabs_->SetTabStyle(TabStyle::Modern);
    tabs_->SetTabHeight(38);
    tabs_->SetTabMinWidth(110);
    tabs_->SetFollowTheme(true);
    tabs_->SetActiveTabIndicatorThickness(3);
    tabs_->AddTab("Browse", BuildBrowsePage());
    tabs_->AddTab("Structure", BuildStructurePage());
    tabs_->AddTab("SQL", BuildSqlPage());
    tabs_->SetTabIcon(kBrowseTab, IconPath("tab-browse"));
    tabs_->SetTabIcon(kStructureTab, IconPath("tab-structure"));
    tabs_->SetTabIcon(kSqlTab, IconPath("tab-sql"));
    tabs_->SetActiveTab(kBrowseTab);
    FillWith(tabPane, tabs_);
    FillWith(page_, split_);

    window_->AddChild(page_);
    LayoutForSize(kWindowWidth, kWindowHeight);
    window_->onWindowResize = [this](int width, int height) {
        LayoutForSize(static_cast<float>(width), static_cast<float>(height));
    };
    // Staged changes are not lost to a closed window without a word.
    window_->onWindowClosing = [this]() -> bool {
        if (committing_ || applying_ || importing_ || exporting_) {
            SetStatus("Changes are being written: close the window once that has finished.");
            return false;
        }
        if (edits_.Empty() && structure_.Empty()) return true;
        ConfirmDiscard("close UltraDBAdmin", [this]() { window_->Close(); });
        return false;
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
    else if (store_.All().empty()) SetStatus("No connections yet - Add connection names a SQLite file or a PostgreSQL server.");
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
    auto bar = MakeToolbar("dbaToolbar", kHeaderHeight);
    MakeCard(*bar);
    bar->SetPadding(0.0f, 10.0f);

    auto title = CreateLabel("dbaTitle", 0, 0, 0, 24, "UltraDBAdmin");
    title->SetFontSize(15.0f);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(UltraCanvasTheme::Current().roles.accentHover);
    title->layoutItem.SetFlexShrink(0);
    title->SetMargin(0.0f, 6.0f);
    bar->AddChild(title);
    bar->AddChild(MakeBarDivider("dbaTitleDivider"));

    auto add = MakeActionButton("dbaAdd", "Add connection", "add", "A SQLite file or a PostgreSQL server",
                                ButtonRole::Primary);
    add->SetOnClick([this]() { AddConnection(); });
    bar->AddChild(add);

    editButton_ = MakeActionButton("dbaEdit", "Edit", "edit", "Change the selected connection");
    editButton_->SetOnClick([this]() { EditConnection(); });
    bar->AddChild(editButton_);

    removeButton_ = MakeActionButton("dbaRemove", "Remove", "delete",
                                     "Take the selected connection off the list; the database is not touched",
                                     ButtonRole::Danger);
    removeButton_->SetOnClick([this]() { RemoveConnection(); });
    bar->AddChild(removeButton_);

    auto refresh = MakeActionButton("dbaRefresh", "Refresh", "refresh",
                                    "Read the selected connection's schemas and tables again");
    refresh->SetOnClick([this]() { RefreshSelected(); });
    bar->AddChild(refresh);
    bar->AddChild(MakeBarDivider("dbaStatusDivider"));

    statusLabel_ = MakeBarStatus("dbaStatus");
    bar->AddChild(statusLabel_);
    return bar;
}

std::shared_ptr<UltraCanvasContainer> DbAdminWindow::BuildTreePane() {
    auto pane = CreateContainer("dbaTreePane", 0, 0, 0, 0);
    MakePlainColumn(pane);
    MakeCard(*pane);
    pane->SetPadding(6.0f);
    auto heading = CreateLabel("dbaTreeHeading", 0, 0, 0, 22, "CONNECTIONS");
    heading->SetFontSize(10.5f);
    heading->SetFontWeight(FontWeight::Bold);
    heading->SetTextColor(StatusColor());
    heading->SetMargin(2.0f, 6.0f, 0.0f, 6.0f);
    heading->layoutItem.SetFlexShrink(0);
    pane->AddChild(heading);
    tree_ = std::make_shared<UltraCanvasTreeView>("dbaTree");
    StyleTree(*tree_);
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

    // Icons alone: the pair reads as a pager beside the row range.
    prevButton_ = MakeActionButton("dbaPrev", "", "previous", "The page before");
    prevButton_->SetOnClick([this]() { if (pageIndex_ > 0) LoadPage(pageIndex_ - 1); });
    bar->AddChild(prevButton_);

    nextButton_ = MakeActionButton("dbaNext", "", "next", "The page after");
    nextButton_->SetOnClick([this]() { LoadPage(pageIndex_ + 1); });
    bar->AddChild(nextButton_);

    browseInfo_ = CreateLabel("dbaBrowseInfo", 0, 0, 220, 24, "");
    browseInfo_->layoutItem.SetFlexShrink(0);
    bar->AddChild(browseInfo_);

    browseNote_ = MakeBarStatus("dbaBrowseNote");
    bar->AddChild(browseNote_);

    importButton_ = MakeActionButton("dbaImport", "Import", "import",
                                     "Put the rows of a CSV file or a workbook into this table, all or none");
    importButton_->SetOnClick([this]() { ImportIntoTable(); });
    bar->AddChild(importButton_);

    exportButton_ = MakeActionButton("dbaExport", "Export", "export",
                                     "Write every row of this table or view to a file: CSV, TSV, JSON, SQL or a workbook");
    exportButton_->SetOnClick([this]() { ExportRows(ExportSource::Table); });
    bar->AddChild(exportButton_);
    bar->AddChild(MakeBarDivider("dbaBrowseDivider"));

    reloadButton_ = MakeActionButton("dbaReload", "Reload", "refresh", "Read this page and the row count again");
    reloadButton_->SetOnClick([this]() {
        if (!pager_) return;
        LoadPage(pageIndex_);
        CountRows();
    });
    bar->AddChild(reloadButton_);

    page->AddChild(bar);
    PinToolbar(bar);
    auto editBar = BuildEditBar();
    page->AddChild(editBar);
    PinToolbar(editBar);

    browseModel_ = std::make_shared<ResultSetModel>();
    browseModel_->SetPending(&edits_);
    browseView_ = MakeList("dbaBrowseGrid", browseModel_);
    // The order is the server's: a click on a header asks for the page again,
    // ORDER BY that column (then the key) - never a sort of the 200 rows shown.
    browseView_->onHeaderClicked = [this](int column) { SortBrowse(column); };
    browseView_->onItemDoubleClicked = [this](int row) { EditRow(row); };
    FillWith(page, browseView_);
    return page;
}

std::shared_ptr<UltraCanvasContainer> DbAdminWindow::BuildEditBar() {
    auto bar = MakeToolbar("dbaEditBar");

    addRowButton_ = MakeActionButton("dbaAddRow", "Add row", "add", "Stage a new row");
    addRowButton_->SetOnClick([this]() { AddRow(); });
    bar->AddChild(addRowButton_);

    editRowButton_ = MakeActionButton("dbaEditRow", "Edit row", "edit",
                                      "Stage changes to the selected row (or double-click it)");
    editRowButton_->SetOnClick([this]() { EditRow(SelectedBrowseRow()); });
    bar->AddChild(editRowButton_);

    deleteRowButton_ = MakeActionButton("dbaDeleteRow", "Delete row", "delete",
                                        "Stage the deletion of the selected row", ButtonRole::Danger);
    deleteRowButton_->SetOnClick([this]() { DeleteSelectedRow(); });
    bar->AddChild(deleteRowButton_);

    pendingLabel_ = MakeBarStatus("dbaPending");
    bar->AddChild(pendingLabel_);

    previewButton_ = MakeActionButton("dbaPreview", "Preview SQL", "preview", "The statements Commit will run");
    previewButton_->SetOnClick([this]() { PreviewChanges(); });
    bar->AddChild(previewButton_);

    revertButton_ = MakeActionButton("dbaRevert", "Revert", "revert", "Drop every staged change");
    revertButton_->SetOnClick([this]() { RevertChanges(); });
    bar->AddChild(revertButton_);

    commitButton_ = MakeActionButton("dbaCommit", "Commit", "apply", "Write every staged change, in one transaction",
                                     ButtonRole::Primary);
    commitButton_->SetOnClick([this]() { CommitChanges(); });
    bar->AddChild(commitButton_);
    return bar;
}

std::shared_ptr<UltraCanvasContainer> DbAdminWindow::BuildStructurePage() {
    auto page = MakeTabPage("dbaStructurePage");
    auto infoBar = MakeToolbar("dbaStructureInfoBar");
    structureInfo_ = MakeBarStatus("dbaStructureInfo");
    infoBar->AddChild(structureInfo_);

    renameTableButton_ = MakeActionButton("dbaRenameTable", "Rename table", "rename", "Stage a new name for the table");
    renameTableButton_->SetOnClick([this]() { RenameTable(); });
    infoBar->AddChild(renameTableButton_);
    infoBar->AddChild(MakeBarDivider("dbaStructureDivider"));

    structurePreviewButton_ = MakeActionButton("dbaStructurePreview", "Preview SQL", "preview",
                                               "The statements Apply will run");
    structurePreviewButton_->SetOnClick([this]() { PreviewStructure(); });
    infoBar->AddChild(structurePreviewButton_);

    structureRevertButton_ = MakeActionButton("dbaStructureRevert", "Revert", "revert",
                                              "Drop every staged structure change");
    structureRevertButton_->SetOnClick([this]() { RevertStructure(); });
    infoBar->AddChild(structureRevertButton_);

    structureApplyButton_ = MakeActionButton("dbaStructureApply", "Apply", "apply",
                                             "Change the table: every staged change, in one transaction",
                                             ButtonRole::Primary);
    structureApplyButton_->SetOnClick([this]() { ApplyStructure(); });
    infoBar->AddChild(structureApplyButton_);
    page->AddChild(infoBar);
    PinToolbar(infoBar);

    auto editBar = BuildStructureBar();
    page->AddChild(editBar);
    PinToolbar(editBar);

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

    // Both lists show the table as it is to be: the staged edits over it.
    columnModel_ = std::make_shared<ColumnSchemaModel>();
    columnModel_->SetPending(&structure_);
    columnView_ = MakeList("dbaColumns", columnModel_);
    columnView_->onItemDoubleClicked = [this](int row) { EditColumn(row); };
    FillWith(columnsPane, columnView_);

    constraintModel_ = std::make_shared<ConstraintModel>();
    constraintModel_->SetPending(&structure_);
    constraintView_ = MakeList("dbaConstraints", constraintModel_);
    FillWith(constraintsPane, constraintView_);

    definitionArea_ = std::make_shared<UltraCanvasTextArea>("dbaDefinition");
    definitionArea_->ApplyCodeStyle("SQL");
    definitionArea_->SetBorders(1.0f, UltraCanvasTheme::Current().roles.line, 6.0f);
    definitionArea_->SetReadOnly(true);
    definitionArea_->SetWordWrap(true);
    FillWith(definitionPane, definitionArea_);

    FillWith(page, split);
    return page;
}

std::shared_ptr<UltraCanvasContainer> DbAdminWindow::BuildStructureBar() {
    auto bar = MakeToolbar("dbaStructureBar");

    addColumnButton_ = MakeActionButton("dbaAddColumn", "Add column", "add", "Stage a new column");
    addColumnButton_->SetOnClick([this]() { AddColumn(); });
    bar->AddChild(addColumnButton_);

    editColumnButton_ = MakeActionButton("dbaEditColumn", "Edit column", "edit",
                                         "Stage a new name, type, NOT NULL or default for the selected column "
                                         "(or double-click it)");
    editColumnButton_->SetOnClick([this]() { EditColumn(SelectedColumn()); });
    bar->AddChild(editColumnButton_);

    dropColumnButton_ = MakeActionButton("dbaDropColumn", "Drop column", "delete",
                                         "Stage dropping the selected column, with its data; on a column "
                                         "already to be dropped, keep it after all",
                                         ButtonRole::Danger);
    dropColumnButton_->SetOnClick([this]() { DropSelectedColumn(); });
    bar->AddChild(dropColumnButton_);
    bar->AddChild(MakeBarDivider("dbaIndexDivider"));

    addIndexButton_ = MakeActionButton("dbaAddIndex", "Add index", "key",
                                       "Stage a new index, on the selected column to begin with");
    addIndexButton_->SetOnClick([this]() { AddIndex(); });
    bar->AddChild(addIndexButton_);

    dropIndexButton_ = MakeActionButton("dbaDropIndex", "Drop index", "delete",
                                        "Stage dropping the index selected in the list of keys and indexes",
                                        ButtonRole::Danger);
    dropIndexButton_->SetOnClick([this]() { DropSelectedIndex(); });
    bar->AddChild(dropIndexButton_);

    structurePending_ = MakeBarStatus("dbaStructurePending");
    bar->AddChild(structurePending_);
    return bar;
}

std::shared_ptr<UltraCanvasContainer> DbAdminWindow::BuildSqlPage() {
    auto page = MakeTabPage("dbaSqlPage");
    auto bar = MakeToolbar("dbaSqlBar");

    runButton_ = MakeActionButton("dbaRun", "Run", "run",
                                  "Run the selected text, or the whole script (Ctrl+Enter)", ButtonRole::Primary);
    runButton_->SetOnClick([this]() { RunSql(); });
    bar->AddChild(runButton_);

    stopButton_ = MakeActionButton("dbaStop", "Stop", "stop", "Cancel the running statement", ButtonRole::Danger);
    stopButton_->SetOnClick([this]() { StopSql(); });
    bar->AddChild(stopButton_);
    bar->AddChild(MakeBarDivider("dbaSqlFileDivider"));

    importSqlButton_ = MakeActionButton("dbaImportSql", "Import SQL", "import",
                                        "Run a SQL file (.sql, or .sql.gz) on the connection picked in the tree");
    importSqlButton_->SetOnClick([this]() { ImportSql(); });
    bar->AddChild(importSqlButton_);

    exportRowsButton_ = MakeActionButton("dbaExportRows", "Export rows", "export",
                                         "Write the rows shown below to a file: CSV, TSV, JSON, SQL or a workbook");
    exportRowsButton_->SetOnClick([this]() { ExportRows(ExportSource::Result); });
    bar->AddChild(exportRowsButton_);
    bar->AddChild(MakeBarDivider("dbaSqlHistoryDivider"));

    historyDropdown_ = CreateDropdown("dbaHistory", 0, 0, 220, kControlHeight);
    historyDropdown_->layoutItem.SetFlexShrink(0);
    historyDropdown_->SetFollowTheme(true);
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
    sqlEditor_->SetBorders(1.0f, UltraCanvasTheme::Current().roles.line, 6.0f);
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
    statusLabel_->SetTextColor(error ? ErrorColor() : StatusColor());
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
    if (spec.production) data.textColor = ErrorColor();
    data.leftIcon = TreeNodeIcon(IconPath(spec.production ? "node-connection-production" : "node-connection"));
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
    // The placeholder is there for the expand button alone: hidden, while
    // the node's own loading bar says it is being read.
    node->data.loading = false;
    const std::string placeholder = nodeId + "/" + kPlaceholderText;
    TreeNodeData hidden(placeholder, kPlaceholderText);
    hidden.visible = false;
    tree_->AddNode(nodeId, hidden);
    NodeRef ref = nodes_[nodeId];
    ref.kind = NodeRef::Kind::Placeholder;
    nodes_[placeholder] = ref;
}

void DbAdminWindow::LoadChildren(const std::string& nodeId) {
    const NodeRef* ref = RefOf(nodeId);
    if (!ref || loading_.count(nodeId)) return;
    const ConnectionSpec* spec = store_.Find(ref->connectionId);
    if (!spec) return;
    if (TreeNode* node = tree_->FindNode(nodeId)) {
        node->data.loading = true;
        // A failed read's message goes while it tries again.
        if (TreeNode* placeholder = tree_->FindNode(nodeId + "/" + kPlaceholderText)) placeholder->data.visible = false;
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
        if (TreeNode* node = tree_->FindNode(nodeId)) node->data.loading = false;
    }
}

void DbAdminWindow::ShowLoadError(const std::string& nodeId, const UltraDbResult& result) {
    // The placeholder stays, saying what went wrong; collapsing and expanding
    // the node tries again.
    if (TreeNode* placeholder = tree_->FindNode(nodeId + "/" + kPlaceholderText)) {
        placeholder->data.text = "Could not read: " + CellText(UltraDbValue(result.message), 80);
        placeholder->data.tooltip = result.message;
        placeholder->data.textColor = ErrorColor();
        placeholder->data.visible = true;
    }
    SetStatus(result.message, true);
    tree_->RequestRedraw();
}

void DbAdminWindow::ApplySchemas(const std::string& nodeId, const UltraDbResult& result,
                                 const std::vector<std::string>& schemas) {
    loading_.erase(nodeId);
    const NodeRef* ref = RefOf(nodeId);
    TreeNode* node = ref ? tree_->FindNode(nodeId) : nullptr;
    if (!node) return;   // removed meanwhile
    node->data.loading = false;
    if (!result) { ShowLoadError(nodeId, result); return; }
    const NodeRef parent = *ref;
    const std::string placeholder = nodeId + "/" + kPlaceholderText;
    tree_->RemoveNode(placeholder);
    nodes_.erase(placeholder);
    std::string only;
    for (const auto& schema : schemas) {
        const std::string id = nodeId + "/" + std::to_string(++nextNodeId_);
        TreeNodeData data(id, schema);
        data.leftIcon = TreeNodeIcon(IconPath("node-schema"));
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
        TreeNodeData none(id, "No schemas");
        none.textColor = StatusColor();
        tree_->AddNode(nodeId, none);
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
    TreeNode* node = ref ? tree_->FindNode(nodeId) : nullptr;
    if (!node) return;
    node->data.loading = false;
    if (!result) { ShowLoadError(nodeId, result); return; }
    const NodeRef parent = *ref;
    const std::string placeholder = nodeId + "/" + kPlaceholderText;
    tree_->RemoveNode(placeholder);
    nodes_.erase(placeholder);
    for (const auto& object : objects) {
        const std::string id = nodeId + "/" + std::to_string(++nextNodeId_);
        // The icon tells a view or a sequence from a table; the tooltip names it.
        TreeNodeData data(id, object.name);
        data.leftIcon = TreeNodeIcon(ObjectIcon(object.kind));
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
        TreeNodeData none(id, "Empty");
        none.textColor = StatusColor();
        tree_->AddNode(nodeId, none);
    }
    tree_->RequestRedraw();
}

void DbAdminWindow::OnNodeSelected(TreeNode* node) {
    if (!node) return;
    const NodeRef* ref = RefOf(node->data.nodeId);
    if (!ref || ref->kind == NodeRef::Kind::Placeholder) return;
    selectedNode_ = node->data.nodeId;
    UpdateSqlBar();
    if (ref->kind != NodeRef::Kind::Object) return;
    const NodeRef object = *ref;
    ConfirmDiscard("open " + object.schema + "." + object.name, [this, object]() { OpenObject(object); });
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
    structure_ = StructureEdit();
    source_ = SqliteTableSource();
    columnModel_->Replace(UltraDbTableSchema());
    constraintModel_->Replace(UltraDbTableSchema());
    definitionArea_->SetText("", false);
    structureInfo_->SetText(message);
    browseNote_->SetText(message);
    browseNote_->SetTextColor(StatusColor());
    edits_ = EditBuffer();
    UpdateBrowseBar();
    UpdateEditBar();
    UpdateStructureBar();
}

void DbAdminWindow::OpenObject(const NodeRef& ref) {
    const ConnectionSpec* spec = store_.Find(ref.connectionId);
    if (!spec) return;
    ClearObject("Reading " + ref.schema + "." + ref.name + "…");
    objectConnectionId_ = spec->id;
    objectRef_ = ref;
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
    // A SQLite table's own definition comes with it: a rebuild works from it,
    // and Apply checks it has not changed since.
    const bool sqliteTable = spec->IsSqlite() && ref.objectKind == UltraDbObjectKind::Table;
    Background([this, connection, schema, name, generation, sqliteTable]() -> std::function<void()> {
        UltraDbTableSchema table;
        const UltraDbResult result = UltraDb_DescribeTable(connection, schema, name, table);
        SqliteTableSource source;
        if (result && sqliteTable) ReadSqliteTableSource(connection, schema, name, source);
        return [this, generation, result, table, source]() { ApplyTable(generation, result, table, source); };
    });
}

void DbAdminWindow::ApplyTable(uint64_t generation, const UltraDbResult& result, const UltraDbTableSchema& table,
                               const SqliteTableSource& source) {
    if (generation != objectGen_) return;
    if (!result) {
        const std::string label = objectLabel_;
        ClearObject(label + ": " + OneLine(result.message));
        browseNote_->SetTextColor(ErrorColor());
        SetStatus(result.message, true);
        return;
    }
    const ConnectionSpec* spec = store_.Find(objectConnectionId_);
    structure_ = StructureEdit(table, spec && spec->IsSqlite() ? SqlEngine::Sqlite : SqlEngine::Postgres);
    source_ = source;
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
    edits_ = EditBuffer(table);
    UpdateEditBar();
    UpdateStructureBar();
    LoadPage(0);
    CountRows();
}

void DbAdminWindow::ApplySequence(uint64_t generation, const UltraDbResult& result, const UltraDbResultSet& rows) {
    if (generation != objectGen_) return;
    if (!result) {
        const std::string label = objectLabel_;
        ClearObject(label + ": " + OneLine(result.message));
        browseNote_->SetTextColor(ErrorColor());
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
        browseNote_->SetTextColor(ErrorColor());
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
    browseNote_->SetTextColor(StatusColor());
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
    importButton_->SetDisabled(!ImportBlockReason().empty());
    UpdateExportButtons();
    importButton_->SetTooltip(ImportBlockReason().empty()
                                  ? std::string("Put the rows of a CSV file or a workbook into this table, all or none")
                                  : ImportBlockReason());
}

// ===== EDITING =====

std::string DbAdminWindow::EditBlockReason() const {
    if (!pager_) return "Open a table to edit its rows.";
    const std::string reason = edits_.ReadOnlyReason();
    if (!reason.empty()) return reason;
    const ConnectionSpec* spec = store_.Find(objectConnectionId_);
    if (spec && spec->readOnly) return "The connection is read only.";
    if (committing_) return "Committing…";
    if (applying_) return "Changing the table's structure…";
    return std::string();
}

int DbAdminWindow::SelectedBrowseRow() const {
    IListSelection* selection = browseView_ ? browseView_->GetSelection() : nullptr;
    if (!selection) return -1;
    const std::vector<int> rows = selection->GetSelectedRows();
    return rows.empty() ? -1 : rows.front();
}

void DbAdminWindow::AfterEdit() {
    browseModel_->Refresh();
    UpdateEditBar();
}

void DbAdminWindow::AddRow() {
    const std::string blocked = EditBlockReason();
    if (!blocked.empty()) { SetStatus(blocked); return; }
    const uint64_t generation = objectGen_;
    ShowRowDialog(window_.get(), edits_.Table(), nullptr, nullptr,
                  [this, generation](const std::map<std::string, CellValue>& values) -> std::string {
                      if (generation != objectGen_) return "Another table was opened meanwhile; nothing was staged.";
                      std::string error;
                      if (!edits_.InsertRow(values, error)) return error;
                      AfterEdit();
                      return std::string();
                  });
}

void DbAdminWindow::EditRow(int row) {
    const std::string blocked = EditBlockReason();
    if (!blocked.empty()) { SetStatus(blocked); return; }
    if (row < 0 || static_cast<size_t>(row) >= browseModel_->Rows().Size()) {
        SetStatus("Select a row to edit (or double-click it).");
        return;
    }
    const UltraDbRow original = browseModel_->Rows().Row(static_cast<size_t>(row));
    if (edits_.PendingDelete(original)) {
        SetStatus("This row is to be deleted; Revert to keep it.");
        return;
    }
    const uint64_t generation = objectGen_;
    ShowRowDialog(window_.get(), edits_.Table(), &original, &edits_,
                  [this, generation, original](const std::map<std::string, CellValue>& values) -> std::string {
                      if (generation != objectGen_) return "Another table was opened meanwhile; nothing was staged.";
                      std::string error;
                      if (!edits_.UpdateRow(original, values, error)) return error;
                      AfterEdit();
                      return std::string();
                  });
}

void DbAdminWindow::DeleteSelectedRow() {
    const std::string blocked = EditBlockReason();
    if (!blocked.empty()) { SetStatus(blocked); return; }
    const int row = SelectedBrowseRow();
    if (row < 0 || static_cast<size_t>(row) >= browseModel_->Rows().Size()) {
        SetStatus("Select the row to delete.");
        return;
    }
    std::string error;
    if (!edits_.DeleteRow(browseModel_->Rows().Row(static_cast<size_t>(row)), error)) {
        SetStatus(error, true);
        return;
    }
    AfterEdit();
}

void DbAdminWindow::PreviewChanges() {
    if (edits_.Empty() || !pager_) return;
    const ConnectionSpec* spec = store_.Find(objectConnectionId_);
    if (!spec) return;
    const std::string connection = RegisteredName(*spec);
    const EditBuffer snapshot = edits_;
    const std::string label = objectLabel_;
    // Quoting asks the connection: on the worker, like every query.
    Background([this, snapshot, connection, label]() -> std::function<void()> {
        std::vector<GeneratedStatement> statements;
        const UltraDbResult result = snapshot.Statements(connection, statements);
        return [this, result, statements, label]() {
            if (!result) { SetStatus(result.message, true); return; }
            std::string sql;
            for (const auto& statement : statements) sql += statement.preview + ";\n";
            ShowSqlPreview(window_.get(), "Staged changes to " + label,
                           "Commit runs these in one transaction. Values are bound as parameters, and a value "
                           "you typed is sent as text for the database to convert to the column's type.",
                           sql);
        };
    });
}

void DbAdminWindow::CommitChanges() {
    if (edits_.Empty() || committing_) return;
    const std::string blocked = EditBlockReason();
    if (!blocked.empty()) { SetStatus(blocked); return; }
    const ConnectionSpec* spec = store_.Find(objectConnectionId_);
    const size_t deletes = edits_.Count(RowChange::Kind::Delete);
    const bool production = spec && spec->production;
    if (deletes == 0 && !production) { RunCommit(); return; }
    // A deletion, or anything on a production database, is said out loud first.
    std::string message;
    if (production) message += "**" + spec->name + " is a production connection.**\n\n";
    message += "Commit " + std::to_string(edits_.Count()) + (edits_.Count() == 1 ? " change" : " changes") +
               " to " + objectLabel_ + "?";
    if (deletes > 0)
        message += " This deletes " + std::to_string(deletes) + (deletes == 1 ? " row." : " rows.");
    UltraCanvasDialogManager::ShowConfirmation(message, "Commit changes",
                                               [this](bool confirmed) { if (confirmed) RunCommit(); },
                                               window_.get());
}

void DbAdminWindow::RunCommit() {
    const ConnectionSpec* spec = store_.Find(objectConnectionId_);
    if (!spec || edits_.Empty() || committing_) return;
    committing_ = true;
    UpdateEditBar();
    SetStatus("Committing " + std::to_string(edits_.Count()) + " changes to " + objectLabel_ + "…");
    const std::string connection = RegisteredName(*spec);
    const EditBuffer snapshot = edits_;
    const uint64_t generation = objectGen_;
    // On the job queue, after any page still loading: the whole transaction
    // runs as one job, so no other query of the window lands inside it.
    Background([this, snapshot, connection, generation]() -> std::function<void()> {
        size_t failed = 0;
        const UltraDbResult result = snapshot.Commit(connection, failed);
        std::string failedSql;
        if (!result) {
            std::vector<GeneratedStatement> statements;
            if (snapshot.Statements(connection, statements) && failed < statements.size())
                failedSql = statements[failed].preview;
        }
        return [this, generation, result, failed, failedSql]() { ApplyCommit(generation, result, failed, failedSql); };
    });
}

void DbAdminWindow::ApplyCommit(uint64_t generation, const UltraDbResult& result, size_t failed,
                                const std::string& failedSql) {
    committing_ = false;
    if (generation != objectGen_) {
        // Only reachable if the table was closed some other way meanwhile.
        SetStatus(result ? std::string("The changes were committed.") : "Commit failed: " + OneLine(result.message),
                  !result);
        UpdateEditBar();
        return;
    }
    if (!result) {
        SetStatus("Nothing was changed: statement " + std::to_string(failed + 1) + " failed - " +
                  OneLine(result.message), true);
        if (!failedSql.empty()) statusLabel_->SetTooltip(failedSql + "\n\n" + result.message);
        UpdateEditBar();
        return;
    }
    const size_t count = edits_.Count();
    edits_.Revert();
    SetStatus(std::to_string(count) + (count == 1 ? " change" : " changes") + " committed to " + objectLabel_ + ".");
    AfterEdit();
    LoadPage(pageIndex_);
    CountRows();
}

void DbAdminWindow::RevertChanges() {
    if (edits_.Empty() || committing_) return;
    ConfirmRevert(edits_.Count(), "row change", [this]() {
        edits_.Revert();
        AfterEdit();
        SetStatus("Staged row changes dropped.");
    });
}

void DbAdminWindow::ConfirmRevert(size_t count, const std::string& kind, std::function<void()> drop) {
    UltraCanvasDialogManager::ShowConfirmation(
        "Drop " + std::to_string(count) + " staged " + kind + (count == 1 ? "" : "s") + " to " + objectLabel_ +
            (count == 1 ? "? It has not been written." : "? None of them has been written."),
        "Revert",
        [drop = std::move(drop)](bool confirmed) { if (confirmed) drop(); },
        window_.get());
}

void DbAdminWindow::ConfirmDiscard(const std::string& what, std::function<void()> then) {
    if (edits_.Empty() && structure_.Empty()) { then(); return; }
    const size_t count = edits_.Count() + structure_.Count();
    UltraCanvasDialogManager::ShowConfirmation(
        "Drop " + std::to_string(count) + (count == 1 ? " staged change" : " staged changes") + " to " +
            objectLabel_ + (what.empty() ? std::string() : " and " + what) +
            (count == 1 ? "? It has not been written." : "? None of them has been written."),
        "Uncommitted changes",
        [this, then](bool confirmed) {
            if (!confirmed) return;
            edits_.Revert();
            structure_.Revert();
            AfterEdit();
            AfterStructureEdit();
            then();
        },
        window_.get());
}

void DbAdminWindow::UpdateEditBar() {
    if (!pendingLabel_) return;
    const std::string blocked = EditBlockReason();
    const bool editable = blocked.empty();
    addRowButton_->SetDisabled(!editable);
    editRowButton_->SetDisabled(!editable);
    deleteRowButton_->SetDisabled(!editable);
    const bool any = !edits_.Empty();
    previewButton_->SetDisabled(!any || committing_);
    revertButton_->SetDisabled(!any || committing_);
    commitButton_->SetDisabled(!any || !editable);
    std::string text;
    if (any) {
        std::vector<std::string> parts;
        auto part = [&](RowChange::Kind kind, const char* one, const char* many) {
            const size_t n = edits_.Count(kind);
            if (n > 0) parts.push_back(std::to_string(n) + " " + (n == 1 ? one : many));
        };
        part(RowChange::Kind::Update, "row edited", "rows edited");
        part(RowChange::Kind::Insert, "new row", "new rows");
        part(RowChange::Kind::Delete, "row to delete", "rows to delete");
        for (size_t i = 0; i < parts.size(); ++i) text += (i == 0 ? "" : ", ") + parts[i];
        text += committing_ ? " - committing…" : " - not yet written";
    } else {
        text = editable ? std::string("Double-click a row to edit it.") : blocked;
    }
    pendingLabel_->SetText(text);
    pendingLabel_->SetTooltip(text);
    pendingLabel_->SetTextColor(any ? ErrorColor() : StatusColor());
    UpdateStructureBar();          // a commit holds structure edits too
    UpdateBrowseBar();             // and an import
}

// ===== STRUCTURE EDITING =====

std::string DbAdminWindow::StructureBlockReason() const {
    if (structure_.Table().name.empty()) return "Open a table to change its structure.";
    const std::string reason = structure_.ReadOnlyReason();
    if (!reason.empty()) return reason;
    const ConnectionSpec* spec = store_.Find(objectConnectionId_);
    if (spec && spec->readOnly) return "The connection is read only.";
    if (applying_) return "Applying…";
    if (committing_) return "Rows are being committed…";
    return std::string();
}

int DbAdminWindow::SelectedColumn() const {
    IListSelection* selection = columnView_ ? columnView_->GetSelection() : nullptr;
    if (!selection) return -1;
    const std::vector<int> rows = selection->GetSelectedRows();
    return rows.empty() ? -1 : rows.front();
}

void DbAdminWindow::AfterStructureEdit() {
    columnModel_->Refresh();
    constraintModel_->Refresh();
    UpdateStructureBar();
}

void DbAdminWindow::AddColumn() {
    const std::string blocked = StructureBlockReason();
    if (!blocked.empty()) { SetStatus(blocked); return; }
    const uint64_t generation = objectGen_;
    ShowColumnDialog(window_.get(), structure_, -1, [this, generation](const ColumnDraft& draft) -> std::string {
        if (generation != objectGen_) return "Another table was opened meanwhile; nothing was staged.";
        std::string error;
        if (!structure_.AddColumn(draft, error)) return error;
        AfterStructureEdit();
        return std::string();
    });
}

void DbAdminWindow::EditColumn(int row) {
    const std::string blocked = StructureBlockReason();
    if (!blocked.empty()) { SetStatus(blocked); return; }
    if (row < 0 || static_cast<size_t>(row) >= structure_.Columns().size()) {
        SetStatus("Select a column to change (or double-click it).");
        return;
    }
    if (structure_.Columns()[static_cast<size_t>(row)].dropped) {
        SetStatus("This column is to be dropped; Drop column again keeps it.");
        return;
    }
    const uint64_t generation = objectGen_;
    const size_t at = static_cast<size_t>(row);
    ShowColumnDialog(window_.get(), structure_, row, [this, generation, at](const ColumnDraft& draft) -> std::string {
        if (generation != objectGen_) return "Another table was opened meanwhile; nothing was staged.";
        std::string error;
        if (!structure_.UpdateColumn(at, draft, error)) return error;
        AfterStructureEdit();
        return std::string();
    });
}

void DbAdminWindow::DropSelectedColumn() {
    const std::string blocked = StructureBlockReason();
    if (!blocked.empty()) { SetStatus(blocked); return; }
    const int row = SelectedColumn();
    if (row < 0 || static_cast<size_t>(row) >= structure_.Columns().size()) {
        SetStatus("Select the column to drop.");
        return;
    }
    const size_t at = static_cast<size_t>(row);
    const ColumnDraft draft = structure_.Columns()[at];
    std::string error;
    const bool restoring = draft.dropped;
    const bool done = restoring ? structure_.RestoreColumn(at, error) : structure_.DropColumn(at, error);
    if (!done) { SetStatus(error, true); return; }
    SetStatus(restoring ? draft.name + " is kept."
              : draft.origin.empty() ? "The new column " + draft.name + " is no longer staged."
                                     : draft.name + " is to be dropped, with its data, when you apply.");
    AfterStructureEdit();
}

void DbAdminWindow::AddIndex() {
    const std::string blocked = StructureBlockReason();
    if (!blocked.empty()) { SetStatus(blocked); return; }
    const int row = SelectedColumn();
    std::string first;
    if (row >= 0 && static_cast<size_t>(row) < structure_.Columns().size() &&
        !structure_.Columns()[static_cast<size_t>(row)].dropped)
        first = structure_.Columns()[static_cast<size_t>(row)].name;
    const uint64_t generation = objectGen_;
    ShowIndexDialog(window_.get(), structure_, first, [this, generation](const IndexDraft& draft) -> std::string {
        if (generation != objectGen_) return "Another table was opened meanwhile; nothing was staged.";
        std::string error;
        if (!structure_.AddIndex(draft, error)) return error;
        AfterStructureEdit();
        return std::string();
    });
}

void DbAdminWindow::DropSelectedIndex() {
    const std::string blocked = StructureBlockReason();
    if (!blocked.empty()) { SetStatus(blocked); return; }
    IListSelection* selection = constraintView_->GetSelection();
    const std::vector<int> rows = selection ? selection->GetSelectedRows() : std::vector<int>();
    const std::string name = rows.empty() ? std::string() : constraintModel_->IndexAt(rows.front());
    if (name.empty()) {
        SetStatus("Select an index in the list of keys and indexes.");
        return;
    }
    std::string error;
    if (!structure_.DropIndex(name, error)) { SetStatus(error, true); return; }
    AfterStructureEdit();
}

void DbAdminWindow::RenameTable() {
    const std::string blocked = StructureBlockReason();
    if (!blocked.empty()) { SetStatus(blocked); return; }
    const uint64_t generation = objectGen_;
    const std::string current = structure_.NewName().empty() ? structure_.Table().name : structure_.NewName();
    UltraCanvasDialogManager::ShowInputDialog(
        "New name for " + structure_.Table().name + ":", "Rename table", current, InputType::Text,
        [this, generation](DialogResult result, const std::string& name) {
            if (result != DialogResult::OK || generation != objectGen_) return;
            std::string error;
            if (!structure_.RenameTable(name, error)) { SetStatus(error, true); return; }
            AfterStructureEdit();
        },
        window_.get());
}

void DbAdminWindow::PreviewStructure() {
    if (structure_.Empty()) return;
    const ConnectionSpec* spec = store_.Find(objectConnectionId_);
    if (!spec) return;
    const std::string connection = RegisteredName(*spec);
    const StructureEdit snapshot = structure_;
    const std::string label = objectLabel_;
    Background([this, snapshot, connection, label]() -> std::function<void()> {
        SqliteTableSource source;
        UltraDbResult result = UltraDbResult::Ok();
        if (snapshot.Engine() == SqlEngine::Sqlite)
            result = ReadSqliteTableSource(connection, snapshot.Table().schema, snapshot.Table().name, source);
        std::vector<DdlStep> steps;
        if (result) result = snapshot.Plan(connection, source, steps);
        const std::string sql = StructureEdit::PlanText(steps);
        const bool sqlite = snapshot.Engine() == SqlEngine::Sqlite;
        return [this, result, sql, label, sqlite]() {
            if (!result) { SetStatus(result.message, true); return; }
            ShowSqlPreview(window_.get(), "Structure changes to " + label,
                           sqlite ? "Apply runs these in one transaction. SQLite reads foreign_keys only outside "
                                    "one, so a rebuild switches it off before and on after."
                                  : "Apply runs these in one transaction: if one fails, the table stays as it was.",
                           sql);
        };
    });
}

void DbAdminWindow::ApplyStructure() {
    if (structure_.Empty() || applying_) return;
    const std::string blocked = StructureBlockReason();
    if (!blocked.empty()) { SetStatus(blocked); return; }
    if (!edits_.Empty()) {
        SetStatus("Commit or revert the staged rows in Browse first: they were read with the structure as it is.");
        return;
    }
    const ConnectionSpec* spec = store_.Find(objectConnectionId_);
    const bool production = spec && spec->production;
    const bool destructive = structure_.Destructive();
    const bool rebuild = structure_.NeedsRebuild();
    if (!production && !destructive && !rebuild) { RunApplyStructure(); return; }
    std::string message;
    if (production) message += "**" + spec->name + " is a production connection.**\n\n";
    message += "Apply " + std::to_string(structure_.Count()) +
               (structure_.Count() == 1 ? " structure change" : " structure changes") + " to " + objectLabel_ + "?";
    if (destructive) message += " What is dropped is gone, data and all.";
    if (rebuild) {
        message += " SQLite rebuilds the table: every row is copied into a new one, which takes a while on a "
                   "large table and holds the database meanwhile.";
    }
    UltraCanvasDialogManager::ShowConfirmation(message, "Apply structure changes",
                                               [this](bool confirmed) { if (confirmed) RunApplyStructure(); },
                                               window_.get());
}

void DbAdminWindow::RunApplyStructure() {
    const ConnectionSpec* spec = store_.Find(objectConnectionId_);
    if (!spec || structure_.Empty() || applying_) return;
    applying_ = true;
    UpdateStructureBar();
    UpdateEditBar();
    SetStatus("Changing " + objectLabel_ + "…");
    const std::string connection = RegisteredName(*spec);
    const StructureEdit snapshot = structure_;
    const SqliteTableSource loaded = source_;
    const uint64_t generation = objectGen_;
    const std::string finalName = snapshot.NewName().empty() ? snapshot.Table().name : snapshot.NewName();
    const size_t count = snapshot.Count();
    // One job, so no page load of the window lands between its statements.
    Background([this, snapshot, loaded, connection, generation, finalName, count]() -> std::function<void()> {
        SqliteTableSource source;
        UltraDbResult result = UltraDbResult::Ok();
        if (snapshot.Engine() == SqlEngine::Sqlite) {
            result = ReadSqliteTableSource(connection, snapshot.Table().schema, snapshot.Table().name, source);
            if (result && !loaded.createSql.empty() && !source.SameDefinition(loaded)) {
                result = UltraDbResult::Error(UltraDbResultCode::Misuse,
                                              "the table was changed since it was read (by another program, or in "
                                              "the SQL tab). Open it again to see it as it is.");
            }
        }
        std::vector<DdlStep> steps;
        if (result) result = snapshot.Plan(connection, source, steps);
        std::string failedSql;
        if (result) {
            size_t failed = 0;
            result = StructureEdit::Apply(connection, steps, failed);
            if (!result && failed < steps.size()) failedSql = steps[failed].preview;
        }
        return [this, generation, result, failedSql, finalName, count]() {
            FinishApplyStructure(generation, result, failedSql, finalName, count);
        };
    });
}

void DbAdminWindow::FinishApplyStructure(uint64_t generation, const UltraDbResult& result, const std::string& failedSql,
                                         const std::string& finalName, size_t count) {
    applying_ = false;
    if (!result) {
        SetStatus("Nothing was changed: " + OneLine(result.message), true);
        if (!failedSql.empty()) statusLabel_->SetTooltip(failedSql + "\n\n" + result.message);
        UpdateStructureBar();
        UpdateEditBar();
        return;
    }
    const std::string message = std::to_string(count) + (count == 1 ? " change" : " changes") + " applied to " +
                                objectLabel_ + (finalName != objectRef_.name ? ", now " + finalName : "") + ".";
    if (generation != objectGen_) { SetStatus(message); UpdateStructureBar(); UpdateEditBar(); return; }
    NodeRef ref = objectRef_;
    if (finalName != ref.name) {
        // The tree lists the old name: read the schema's objects again.
        std::string schemaNode;
        for (const auto& [id, node] : nodes_) {
            if (node.kind == NodeRef::Kind::Schema && node.connectionId == ref.connectionId && node.schema == ref.schema)
                schemaNode = id;
        }
        if (!schemaNode.empty()) {
            ResetNode(schemaNode);
            if (TreeNode* tree = tree_->FindNode(schemaNode)) {
                tree_->ExpandNode(tree);
                tree_->SelectNode(tree);
                selectedNode_ = schemaNode;
            }
            LoadChildren(schemaNode);
        }
        ref.name = finalName;
    }
    // Read it again, as it now is; the edit starts afresh from that.
    OpenObject(ref);
    SetStatus(message);
}

void DbAdminWindow::RevertStructure() {
    if (structure_.Empty() || applying_) return;
    ConfirmRevert(structure_.Count(), "structure change", [this]() {
        structure_.Revert();
        AfterStructureEdit();
        SetStatus("Staged structure changes dropped.");
    });
}

void DbAdminWindow::UpdateStructureBar() {
    if (!structurePending_) return;
    const std::string blocked = StructureBlockReason();
    const bool editable = blocked.empty();
    for (const auto& button : {addColumnButton_, editColumnButton_, dropColumnButton_, addIndexButton_,
                               dropIndexButton_, renameTableButton_})
        button->SetDisabled(!editable);
    const bool any = !structure_.Empty();
    structurePreviewButton_->SetDisabled(!any || applying_);
    structureRevertButton_->SetDisabled(!any || applying_);
    structureApplyButton_->SetDisabled(!any || !editable);
    std::string text;
    if (any) {
        std::vector<std::string> parts;
        auto part = [&](size_t n, const char* one, const char* many) {
            if (n > 0) parts.push_back(std::to_string(n) + " " + (n == 1 ? one : many));
        };
        part(structure_.Count(StructureEdit::ColumnState::Changed), "column changed", "columns changed");
        part(structure_.Count(StructureEdit::ColumnState::New), "new column", "new columns");
        part(structure_.Count(StructureEdit::ColumnState::Dropped), "column to drop", "columns to drop");
        part(structure_.NewIndexes().size(), "new index", "new indexes");
        part(structure_.DroppedIndexes().size(), "index to drop", "indexes to drop");
        if (!structure_.NewName().empty()) parts.push_back("renamed to " + structure_.NewName());
        for (size_t i = 0; i < parts.size(); ++i) text += (i == 0 ? "" : ", ") + parts[i];
        text += applying_ ? " - applying…" : " - not applied";
    } else {
        text = editable ? std::string("Double-click a column to change it.") : blocked;
    }
    structurePending_->SetText(text);
    structurePending_->SetTooltip(structure_.NeedsRebuild()
                                      ? text + "\n\nSQLite changes a column's type, NOT NULL or default only by "
                                               "rebuilding the table; Preview SQL shows every step."
                                      : text);
    structurePending_->SetTextColor(any ? ErrorColor() : StatusColor());
}

// ===== IMPORT =====

std::string DbAdminWindow::ImportBlockReason() const {
    if (!pager_) return "Open a table to import rows into it.";
    if (pager_->Table().kind != UltraDbObjectKind::Table)
        return std::string("Rows are imported into a table, not a ") + KindWord(pager_->Table().kind) + ".";
    const ConnectionSpec* spec = store_.Find(objectConnectionId_);
    if (spec && spec->readOnly) return "The connection is read only.";
    if (importing_) return "Importing…";
    if (committing_) return "Committing…";
    if (applying_) return "Changing the table's structure…";
    return std::string();
}

void DbAdminWindow::ImportIntoTable() {
    const std::string blocked = ImportBlockReason();
    if (!blocked.empty()) { SetStatus(blocked); return; }
    // The types are the framework's: every spreadsheet format this build reads.
    FileDialogOptions options;
    options.SetTitle("Import rows into " + objectLabel_).SetParentWindow(window_.get());
    UltraCanvasFileLoader::AddLoadTypes(options, "Spreadsheets and CSV", {MediaFormatCategory::Spreadsheet});
    options.AddFilter("All files", std::vector<std::string>{"*"});
    const uint64_t generation = objectGen_;
    UltraCanvasFileLoader::OpenFileDialog(options, [this, generation](DialogResult result, const std::string& path) {
        if (result != DialogResult::OK || path.empty() || generation != objectGen_) return;
        ReadImportFile(path);
    });
}

void DbAdminWindow::ReadImportFile(const std::string& path) {
    const uint64_t generation = objectGen_;
    const std::string name = PathToUtf8(PathFromUtf8(path).filename());
    std::string extension = PathToUtf8(PathFromUtf8(path).extension());
    for (auto& c : extension)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (extension == ".ods" || extension == ".xlsx" || extension == ".xls") {
        // A workbook: the framework's spreadsheet reader, on this thread.
        std::vector<ImportGrid> grids;
        std::string error;
        if (!GridsFromWorkbook(path, grids, error)) { SetStatus(name + " cannot be imported: " + error, true); return; }
        ShowImportMapping(generation, std::move(grids));
        return;
    }
    // Delimited text: the framework's Text Import dialog sets how it is read.
    ShowCSVImportDialog(
        path,
        [this, generation, path, name](const CSVImportOptions& options) {
            SetStatus("Reading " + name + "…");
            Background([this, generation, path, name, options]() -> std::function<void()> {
                const FileBytesResult file = UltraCanvasFileLoader::LoadFile(path);
                std::vector<ImportGrid> grids;
                std::string error = file.success ? std::string() : file.error;
                if (file.success) {
                    const std::string text =
                        CSVDecodeToUtf8(std::string(file.bytes.begin(), file.bytes.end()), options.encoding);
                    grids.push_back(GridFromCsv(text, options, name));
                }
                return [this, generation, grids, error, name]() mutable {
                    if (!error.empty()) { SetStatus(name + " cannot be read: " + error, true); return; }
                    SetStatus(std::string());
                    ShowImportMapping(generation, std::move(grids));
                };
            });
        },
        window_.get());
}

void DbAdminWindow::ShowImportMapping(uint64_t generation, std::vector<ImportGrid> grids) {
    if (generation != objectGen_ || !pager_) return;
    if (grids.empty() || grids.front().rows.empty()) { SetStatus("The file holds no rows."); return; }
    const ConnectionSpec* spec = store_.Find(objectConnectionId_);
    ShowRowImportDialog(window_.get(), pager_->Table(), std::move(grids), spec && spec->production,
                        [this, generation](const ImportGrid& grid, const ImportMapping& mapping) -> std::string {
                            if (generation != objectGen_) return "Another table was opened meanwhile.";
                            const std::string blocked = ImportBlockReason();
                            if (!blocked.empty()) return blocked;
                            RunImport(grid, mapping);
                            return std::string();
                        });
}

void DbAdminWindow::RunImport(const ImportGrid& grid, const ImportMapping& mapping) {
    const ConnectionSpec* spec = store_.Find(objectConnectionId_);
    if (!spec || !pager_ || importing_) return;
    auto run = [this, grid, mapping]() {
        const ConnectionSpec* spec = store_.Find(objectConnectionId_);
        if (!spec || !pager_ || importing_) return;
        importing_ = true;
        UpdateBrowseBar();
        UpdateEditBar();
        const size_t rows = DataRows(grid, mapping);
        SetStatus("Importing " + Grouped(static_cast<int64_t>(rows)) + " rows into " + objectLabel_ + "…");
        const std::string connection = RegisteredName(*spec);
        const UltraDbTableSchema table = pager_->Table();
        const uint64_t generation = objectGen_;
        // One job, so no page load of the window lands inside the transaction.
        Background([this, connection, table, grid, mapping, generation]() -> std::function<void()> {
            size_t imported = 0, failedRow = 0;
            const UltraDbResult result = ImportRows(connection, table, grid, mapping, imported, failedRow);
            const std::string file = grid.name;
            return [this, generation, result, imported, failedRow, file]() {
                FinishImport(generation, result, imported, failedRow, file);
            };
        });
    };
    if (!spec->production) { run(); return; }
    UltraCanvasDialogManager::ShowConfirmation(
        "**" + spec->name + " is a production connection.**\n\nImport " +
            Grouped(static_cast<int64_t>(DataRows(grid, mapping))) + " rows into " + objectLabel_ + "?",
        "Import rows", [run](bool confirmed) { if (confirmed) run(); }, window_.get());
}

void DbAdminWindow::FinishImport(uint64_t generation, const UltraDbResult& result, size_t imported,
                                 size_t failedRow, const std::string& file) {
    importing_ = false;
    if (!result) {
        SetStatus("Nothing was imported: " + (failedRow > 0 ? "row " + std::to_string(failedRow) + " of " + file + " - "
                                                            : std::string()) +
                      OneLine(result.message),
                  true);
    } else {
        SetStatus(Grouped(static_cast<int64_t>(imported)) + (imported == 1 ? " row" : " rows") + " imported into " +
                  objectLabel_ + " from " + file + ".");
    }
    UpdateEditBar();
    if (result && generation == objectGen_) {
        LoadPage(pageIndex_);
        CountRows();
    }
}

void DbAdminWindow::ImportSql() {
    const ConnectionSpec* spec = SelectedConnection();
    if (!spec) {
        sqlInfo_->SetText("Pick a connection (or anything in it) in the tree first.");
        sqlInfo_->SetTextColor(ErrorColor());
        return;
    }
    if (RunnerFor(*spec)->IsRunning()) {
        sqlInfo_->SetText("A script is still running on this connection: Stop it, or wait.");
        sqlInfo_->SetTextColor(ErrorColor());
        return;
    }
    // A SQL script is no media format, so FileLoader's inventory has no type
    // for it: the dialog names it; FileLoader reads it (and unpacks a .gz).
    FileDialogOptions options;
    options.SetTitle("Import SQL into " + spec->name).SetParentWindow(window_.get());
    options.AddFilter("SQL scripts (.sql, .sql.gz)", std::vector<std::string>{"sql", "gz"});
    options.AddFilter("All files", std::vector<std::string>{"*"});
    const std::string id = spec->id;
    UltraCanvasFileLoader::OpenFileDialog(options, [this, id](DialogResult result, const std::string& path) {
        if (result != DialogResult::OK || path.empty()) return;
        const std::string name = PathToUtf8(PathFromUtf8(path).filename());
        sqlInfo_->SetText("Reading " + name + "…");
        sqlInfo_->SetTextColor(StatusColor());
        Background([this, id, path, name]() -> std::function<void()> {
            const FileBytesResult file = UltraCanvasFileLoader::LoadFile(path);
            std::string script, error = file.success ? std::string() : file.error;
            if (file.success) ScriptFromBytes(file.bytes, script, error);
            ScriptSummary summary;
            if (error.empty()) summary = SummarizeScript(script);
            std::string size = HumanSize(static_cast<int64_t>(file.bytes.size()));
            if (!file.decompressedFrom.empty()) size += ", unpacked from " + file.decompressedFrom;
            return [this, id, name, script, error, summary, size]() {
                const ConnectionSpec* spec = store_.Find(id);
                if (!spec) return;
                if (!error.empty()) {
                    sqlInfo_->SetText(name + " cannot be imported: " + error);
                    sqlInfo_->SetTextColor(ErrorColor());
                    return;
                }
                UpdateSqlBar();
                SqlImportInfo info{name, size, summary, spec->name, spec->production};
                ShowSqlImportDialog(window_.get(), info, [this, id, name, script](bool oneTransaction) {
                    const ConnectionSpec* spec = store_.Find(id);
                    if (!spec) return;
                    RunOptions options;
                    options.oneTransaction = oneTransaction;
                    options.remember = false;          // a file is not put back in the editor
                    options.label = name;
                    std::weak_ptr<char> alive = alive_;
                    const bool started = RunnerFor(*spec)->Start(
                        script,
                        [this, alive, id](const RunOutcome& outcome) {
                            auto* app = UltraCanvasApplicationBase::GetCurrent();
                            if (!app || alive.expired()) return;
                            app->PostToUIThread([this, alive, id, outcome]() {
                                if (!alive.expired()) ApplyOutcome(id, outcome);
                            });
                        },
                        options);
                    if (!started) {
                        sqlInfo_->SetText("A script is still running on this connection: Stop it, or wait.");
                        sqlInfo_->SetTextColor(ErrorColor());
                        return;
                    }
                    sqlConnectionId_ = id;
                    tabs_->SetActiveTab(kSqlTab);
                    UpdateSqlBar();
                    sqlInfo_->SetText("Importing " + name + " into " + spec->name + "…");
                });
            };
        });
    });
}

// ===== EXPORT =====

void DbAdminWindow::UpdateExportButtons() {
    if (!exportButton_ || !exportRowsButton_) return;
    exportButton_->SetDisabled(!pager_ || exporting_);
    exportRowsButton_->SetDisabled(exporting_ || sqlResultModel_->Rows().Columns().empty());
}

void DbAdminWindow::ExportRows(ExportSource source) {
    if (exporting_) return;
    std::string name;
    if (source == ExportSource::Table) {
        if (!pager_) { SetStatus("Open a table or view to export it."); return; }
        name = pager_->Table().name;
    } else {
        if (sqlResultModel_->Rows().Columns().empty()) { SetStatus("Run a query, and pick a statement that returned rows."); return; }
        name = "query result";
    }
    // The types are the framework's spreadsheet writers - workbooks, CSV,
    // TSV - and the two this app writes itself: JSON and SQL.
    FileDialogOptions options;
    options.SetTitle(source == ExportSource::Table ? "Export " + objectLabel_ : std::string("Export the rows shown"))
        .SetDefaultFileName(name + ".csv")
        .SetParentWindow(window_.get());
    UltraCanvasFileLoader::AddSaveTypes(options, MediaFormatCategory::Spreadsheet);
    options.AddFilter("JSON (an object per row)", std::vector<std::string>{"json"});
    options.AddFilter("SQL INSERT statements", std::vector<std::string>{"sql"});
    const uint64_t generation = objectGen_;
    UltraCanvasFileLoader::SaveFileDialog(options, [this, source, generation](DialogResult result, const std::string& path) {
        if (result != DialogResult::OK || path.empty()) return;
        if (source == ExportSource::Table && generation != objectGen_) return;
        StartExport(source, path);
    });
}

void DbAdminWindow::StartExport(ExportSource source, const std::string& path) {
    std::string extension = PathToUtf8(PathFromUtf8(path).extension());
    for (auto& c : extension)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    ExportOptions options;
    if (extension == ".json") {
        options.format = ExportFormat::Json;
        RunExport(source, path, options);
        return;
    }
    if (extension == ".sql") {
        options.format = ExportFormat::Sql;
        RunExport(source, path, options);
        return;
    }
    if (extension == ".xlsx" || extension == ".ods") {
        RunExport(source, path, options);           // the workbook path: see RunExport
        return;
    }
    // Delimited text: the framework's Text Export dialog sets how it is written,
    // previewed on the rows at hand.
    const UltraDbResultSet& sample = source == ExportSource::Table ? browseModel_->Rows() : sqlResultModel_->Rows();
    auto preview = std::make_shared<UltraCanvasSpreadsheet>("dbaExportPreview", 0, 0, 10, 10);
    if (SpreadsheetSheet* sheet = preview->GetSheet(0)) {
        for (size_t c = 0; c < sample.Columns().size(); ++c)
            sheet->SetCellValue(0, static_cast<int>(c), sample.Columns()[c]);
        for (size_t r = 0; r < sample.Size() && r < 50; ++r)
            for (size_t c = 0; c < sample.Row(r).Size(); ++c)
                sheet->SetCellValue(static_cast<int>(r) + 1, static_cast<int>(c), CsvField(sample.Row(r)[c]));
    }
    CSVExportOptions initial;
    if (extension == ".tsv") initial.fieldSeparator = '\t';
    ShowCSVExportDialog(
        preview.get(), initial,
        [this, source, path, preview](const CSVExportOptions& csv) {
            ExportOptions chosen;
            chosen.format = ExportFormat::Csv;
            chosen.csv = csv;
            RunExport(source, path, chosen);
        },
        window_.get());
}

void DbAdminWindow::RunExport(ExportSource source, const std::string& path, ExportOptions options) {
    const ConnectionSpec* spec = source == ExportSource::Table ? store_.Find(objectConnectionId_)
                                                               : store_.Find(sqlConnectionId_);
    if (!spec || exporting_) return;
    if (source == ExportSource::Table && !pager_) return;
    const std::string connection = RegisteredName(*spec);
    std::string extension = PathToUtf8(PathFromUtf8(path).extension());
    for (auto& c : extension)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    const bool workbook = extension == ".xlsx" || extension == ".ods";
    options.engine = spec->IsSqlite() ? SqlEngine::Sqlite : SqlEngine::Postgres;
    const std::string name = PathToUtf8(PathFromUtf8(path).filename());
    exporting_ = true;
    UpdateExportButtons();
    SetStatus("Exporting to " + name + "…");

    std::weak_ptr<char> alive = alive_;
    auto progress = [alive, this, name](size_t rows) {
        auto* app = UltraCanvasApplicationBase::GetCurrent();
        if (!app || alive.expired()) return;
        app->PostToUIThread([alive, this, name, rows]() {
            if (!alive.expired() && exporting_)
                SetStatus("Exporting to " + name + "… " + Grouped(static_cast<int64_t>(rows)) + " rows");
        });
    };
    if (source == ExportSource::Result) {
        const UltraDbResultSet rows = sqlResultModel_->Rows();
        if (workbook) {
            std::string error;
            const bool ok = WriteWorkbook(path, "query result", rows, error);
            FinishExport(ok ? UltraDbResult::Ok() : UltraDbResult::Error(UltraDbResultCode::Internal, name + ": " + error),
                         rows.Size(), path, false);
            return;
        }
        if (options.format == ExportFormat::Sql) {
            UltraDb_QuoteIdentifier(connection, "query_result", options.table);
            options.preamble = "-- The rows of a query: put the name of the table they go into in place of "
                               "query_result.\n";
        }
        Background([this, connection, rows, path, options]() -> std::function<void()> {
            size_t written = 0;
            const UltraDbResult result = ExportResult(connection, rows, path, options, written);
            return [this, result, written, path]() { FinishExport(result, written, path, false); };
        });
        return;
    }

    const UltraDbTableSchema table = pager_->Table();
    const std::shared_ptr<std::atomic<bool>> stop = exportStop_;
    const bool sqlite = spec->IsSqlite();
    // One job on the queue, through a cursor: a page at a time, never the whole table.
    Background([this, connection, table, path, options, workbook, stop, progress, sqlite]() mutable -> std::function<void()> {
        std::string sql;
        UltraDbResult result = TableQuery(connection, table, sql);
        if (result && workbook) {
            UltraDbResultSet rows;
            bool truncated = false;
            result = ReadRows(connection, sql, static_cast<size_t>(SpreadsheetLimits::MaxRows - 1), rows, truncated);
            return [this, result, rows, path, truncated, table]() {
                if (!result) { FinishExport(result, 0, path, false); return; }
                std::string error;
                const bool ok = WriteWorkbook(path, table.name, rows, error);
                FinishExport(ok ? result : UltraDbResult::Error(UltraDbResultCode::Internal, error), rows.Size(), path,
                             truncated);
            };
        }
        if (result && options.format == ExportFormat::Sql) {
            result = UltraDb_QuoteQualifiedName(connection, table.schema, table.name, options.table);
            if (result && sqlite) result = SqlitePreamble(connection, table, options.preamble);
            else if (result)
                options.preamble = "-- The rows of " + table.schema + "." + table.name +
                                   " only: its definition is pg_dump --schema-only's to write.\n";
        }
        size_t written = 0;
        if (result) result = ExportQuery(connection, sql, path, options, written, stop.get(), progress);
        return [this, result, written, path]() { FinishExport(result, written, path, false); };
    });
}

void DbAdminWindow::FinishExport(const UltraDbResult& result, size_t rows, const std::string& path, bool truncated) {
    exporting_ = false;
    UpdateExportButtons();
    const std::string name = PathToUtf8(PathFromUtf8(path).filename());
    if (!result) {
        SetStatus("Not exported: " + OneLine(result.message), true);
        return;
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(PathFromUtf8(path), ec);
    SetStatus(Grouped(static_cast<int64_t>(rows)) + (rows == 1 ? " row" : " rows") + " exported to " + name +
                  (ec ? std::string() : " (" + HumanSize(static_cast<int64_t>(size)) + ")") +
                  (truncated ? " - a workbook holds no more rows; export CSV for all of them." : "."),
              truncated);
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
        sqlInfo_->SetTextColor(ErrorColor());
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
        sqlInfo_->SetTextColor(ErrorColor());
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
    if (!outcome.label.empty()) {
        // An imported file: say what of it stayed.
        const std::string stayed = outcome.rolledBack ? "nothing of it stayed" : "what ran before stays";
        if (outcome.cancelled) {
            info = outcome.label + ": stopped - " + stayed + ".";
            bad = true;
        } else if (!outcome.result.success) {
            info = outcome.label + ": statement " + std::to_string(n) + " failed - " + stayed + ": " +
                   OneLine(outcome.result.message);
            bad = true;
        } else {
            info = outcome.label + ": " + std::to_string(n) + (n == 1 ? " statement" : " statements") + " on " +
                   where + (outcome.transaction ? ", as one transaction," : "") + " in " + Seconds(outcome.seconds) +
                   ". Refresh shows new tables.";
        }
    } else if (outcome.cancelled) {
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
    sqlInfo_->SetTextColor(bad ? ErrorColor() : StatusColor());
}

void DbAdminWindow::ShowStatementRows(int row) {
    sqlResultProxy_->ClearSort();
    sqlResultView_->ClearSortIndicator();
    const UltraDbStatementResult* statement = statementModel_->At(row);
    if (statement && statement->returnsRows) sqlResultModel_->Replace(statement->rows);
    else sqlResultModel_->Clear();
    UpdateExportButtons();
}

void DbAdminWindow::UpdateSqlBar() {
    if (!runButton_) return;
    const ConnectionSpec* spec = SelectedConnection();
    auto running = runners_.find(sqlConnectionId_);
    const bool busy = running != runners_.end() && running->second->IsRunning();
    runButton_->SetDisabled(busy || !spec);
    stopButton_->SetDisabled(!busy);
    importSqlButton_->SetDisabled(busy || !spec);
    const bool selectedAny = spec != nullptr;
    editButton_->SetDisabled(!selectedAny);
    removeButton_->SetDisabled(!selectedAny);
    if (busy) {
        const ConnectionSpec* on = store_.Find(sqlConnectionId_);
        sqlInfo_->SetText("Running on " + (on ? on->name : std::string("the connection")) + "…");
        sqlInfo_->SetTextColor(StatusColor());
    } else if (spec) {
        sqlInfo_->SetText(std::string("Runs on ") + spec->name + (spec->production ? " - PRODUCTION" : "") +
                          (spec->readOnly ? " (read only)" : ""));
        sqlInfo_->SetTextColor(spec->production ? ErrorColor() : StatusColor());
    } else {
        sqlInfo_->SetText("Pick a connection in the tree; the script runs there.");
        sqlInfo_->SetTextColor(StatusColor());
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

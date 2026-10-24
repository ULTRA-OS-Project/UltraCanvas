// Apps/UltraDBAdmin/ui/DbAdminWindow.h
// UltraDBAdmin's window. A toolbar (Add, Edit, Remove a connection, Refresh),
// the tree of connections -> schemas -> tables, views and sequences on the
// left - its right-click menu carries each object's operations: open, copy
// its name, export, copy the table, rename, empty, drop, a new table, a
// schema dumped as SQL, a search of every table, SQLite's maintenance - and
// on the right a row of the objects open, one tab each (each keeps its page,
// sort, filters and staged changes while another is shown), the tabs below,
// and the SQL log under them: every statement the app ran.
//   Browse    - the selected table a page at a time (TablePager), sorted on
//               the server by a click on a column header, Previous / Next,
//               narrowed by filters (a column, a comparison, a value: bound,
//               one per column); and edited: a row changed, added or deleted
//               is staged (EditBuffer), shown in the grid and as SQL, and
//               written by Commit in one transaction, or dropped by Revert.
//               A double-click edits a cell in place; a blob is viewed (the
//               picture, or a hex dump), saved to a file or loaded from one;
//               selected rows are copied as CSV, JSON or INSERT statements
//               (the right-click menu, Ctrl+C for CSV) - as the console's
//               rows are;
//   Structure - its columns, its keys, indexes, foreign keys and triggers,
//               and a view's definition; and edited: a column added,
//               changed, renamed or dropped, an index added or dropped, the
//               table renamed is staged (StructureEdit), shown in the lists
//               and as DDL, and written by Apply in one transaction - on
//               SQLite with the table rebuilt where SQLite's ALTER cannot;
//   SQL       - a script editor with Run (Ctrl+Enter: the selection or the
//               whole script), Run statement (Ctrl+Shift+Enter: the one under
//               the caret), Explain, Stop, the history and saved snippets,
//               what each statement did, and the rows of the one selected;
//               completion of tables, columns and keywords as they are typed
//               or on Ctrl+Space (DbAdminCompletion);
//   Search    - a text in every table and view of a schema, a count per
//               column; a hit opens the table filtered on that column;
//   Relations - the schema's tables and the foreign keys between them as an
//               ER diagram (crow's foot or Chen); a table double-clicked
//               opens it;
//   Routines  - its triggers, and on PostgreSQL its functions, procedures
//               and aggregates, each with its statement: edited in the SQL
//               tab and run again, dropped, or a new one from a template;
// A table's menu also compares its rows with another table's - on any
// connection - and can make the other the same (DbAdminCompare).
//   Server    - PostgreSQL's sessions (a query cancelled, a session ended),
//               its settings and the sizes of its databases and tables, and
//               a monitor: charts of its sessions, statements, rows, cache
//               and traffic, read every 2 seconds while they are shown;
//               SQLite's file: its pages, journal mode, encoding, versions.
// Search, Relations and Routines work on the schema the tree's selection is
// in, Server on its connection; the last three read it when they are shown.
// And imports: Browse's Import... puts the rows of a CSV file or a workbook
// into the table shown, mapped column by column, all or none; the SQL tab's
// Import SQL... runs a SQL file on the console's session, as one transaction
// unless the file manages its own. And exports: Browse's Export... writes the
// whole table, the SQL tab's Export rows... the rows shown, as CSV / TSV,
// JSON, SQL INSERTs or a workbook.
// Nothing is painted by hand: UltraCanvasTreeView, UltraCanvasListView over
// the models in DbAdminModels, UltraCanvasTextArea.
//
// No database call is made on the UI thread. The tree, Browse and Structure
// queue theirs on one worker (JobQueue), on the connection's own session;
// the SQL console runs on its own thread (SqlRunner) on a second session
// (ConsoleName), so a long script holds up neither. Results come back through
// PostToUIThread, guarded by a token that dies with the window. A result for
// something no longer shown - another table was picked meanwhile - is
// recognised by its generation number and dropped.
// Version: 0.5.0 - the Server tab's monitor charts; completion in the SQL editor; data compare;
//                  connections through a relay, and the browser build that has nothing else
// Version: 0.4.0 - the Relations, Routines and Server tabs
// Version: 0.3.0 - tabs of open objects, the tree's operations, Search, the SQL log,
//                 Run statement, Explain, snippets, auto-commit, the typed row form
// Version: 0.2.0 - filters, editing in the cell, blobs, rows copied as text
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminConnections.h"
#include "DbAdminDump.h"
#include "DbAdminEditBuffer.h"
#include "DbAdminExport.h"
#include "DbAdminImport.h"
#include "DbAdminJobQueue.h"
#include "DbAdminModels.h"
#include "DbAdminOperations.h"
#include "DbAdminRelations.h"
#include "DbAdminRoutines.h"
#include "DbAdminSearch.h"
#include "DbAdminServer.h"
#include "DbAdminCompare.h"
#include "DbAdminCompletion.h"
#include "DbAdminMonitor.h"
#include "DbAdminSnippets.h"
#include "DbAdminSqlLog.h"
#include "DbAdminSqlRunner.h"
#include "DbAdminStructureEdit.h"
#include "DbAdminTablePager.h"
#include "DbAdminTheme.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListSortFilterProxy.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTimer.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasWindow.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace UltraCanvas {
class UltraCanvasERDiagram;
class UltraCanvasEngineLineChart;
}

namespace UltraDBAdmin {

class DbAdminWindow {
public:
    ~DbAdminWindow();

    // `dataDirectory` holds connections.json and the vault; empty keeps the
    // list in memory only (and says so).
    bool Initialize(const std::string& dataDirectory);
    void Show();

private:
    // A tree row, by its node id.
    struct NodeRef {
        enum class Kind { Connection, Schema, Object, Placeholder } kind = Kind::Connection;
        std::string connectionId;
        std::string schema;
        std::string name;
        UltraDbObjectKind objectKind = UltraDbObjectKind::Table;
    };

    // ===== CONSTRUCTION =====
    void LayoutForSize(float width, float height);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildToolbar();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildTreePane();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildBrowsePage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildStructurePage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildSqlPage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildSearchPage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildLogPanel();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildRelationsPage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildRoutinesPage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildServerPage();

    // ===== BACKGROUND =====
    // Runs `work` on the job queue; the function it returns runs on the UI
    // thread afterwards, unless the window has gone. `work` must not touch
    // the window: it captures what it needs by value.
    void Background(std::function<std::function<void()>()> work);
    void SetStatus(const std::string& text, bool error = false);

    // ===== CONNECTIONS AND THE TREE =====
    void RebuildTree();
    void AddConnectionNode(const ConnectionSpec& spec);
    // Replace the placeholder under `nodeId` with the real children.
    void LoadChildren(const std::string& nodeId);
    void ApplySchemas(const std::string& nodeId, const UltraDbResult& result,
                      const std::vector<std::string>& schemas);
    void ApplyObjects(const std::string& nodeId, const UltraDbResult& result,
                      const std::vector<UltraDbObjectSummary>& objects);
    void ShowLoadError(const std::string& nodeId, const UltraDbResult& result);
    // An SSH bastion's host key not known yet: shown, and kept with the
    // connection once the user trusts it; then `nodeId` is read again.
    void TrustHostKey(const std::string& nodeId, const std::string& fingerprint);
    void ResetNode(const std::string& nodeId);   // forget the children; load again on expand
    void OnNodeSelected(UltraCanvas::TreeNode* node);
    const NodeRef* RefOf(const std::string& nodeId) const;
    const ConnectionSpec* SelectedConnection() const;
    void AddConnection();
    // An UltraDBAdmin relay's connections (UltraDatabaseRelay.h): its
    // address and the user's token asked for, then every connection it
    // offers added to the tree - or updated, when already there. The browser
    // build starts here.
    void ConnectRelay();
    // The relay's connections for `token` - a user's token or a ticket of its
    // sign-in - added to the tree; `done` hears "" or why not.
    void ConnectWithToken(const std::string& url, const std::string& token, std::function<void(const std::string&)> done);
    // The relay's own sign-in (Apps/UltraDBAdmin/relay/RelaySignIn.h): in the
    // browser the page goes there and comes back with a ticket; on a desktop
    // the system browser goes there and the ticket comes to a loopback
    // address of the app's own.
    void SignInToRelay(const std::string& url, std::function<void(const std::string&)> done);
    void AddRelayConnections(const std::string& url, const std::string& token,
                             const std::vector<UltraDbRelayConnection>& offered);
    void EditConnection();
    void RemoveConnection();
    void RefreshSelected();
    std::string SaveConnection(const ConnectionSpec& spec, const ConnectionStore::SecretChanges& secrets);

    // ===== OPEN OBJECTS =====
    // An object open in a tab of its own. The tab shown is the window's
    // state below (pager_, edits_, structure_, ...); the others keep theirs
    // here until they are shown again.
    struct ObjectTab {
        NodeRef ref;
        std::string label;
        bool loaded = false;                  // read: what follows holds
        std::shared_ptr<const TablePager> pager;
        size_t pageIndex = 0;
        EditBuffer edits;
        StructureEdit structure;
        SqliteTableSource source;
        UltraDbTableSchema table;
        std::string definition, info;
    };
    static constexpr size_t kMaxTabs = 10;
    // Opens `ref` in its tab - shown, if it has one, else a new one.
    void OpenFromTree(const NodeRef& ref);
    int FindTab(const NodeRef& ref) const;
    void StashActiveTab();
    void ActivateTab(int index);               // unless a commit or an Apply is under way
    void ShowTab(int index);                   // without that check
    void CloseTab(int index);                  // asks first when it has staged changes
    void DropTab(int index);                   // without asking
    void CloseTabsOf(const std::string& connectionId);
    // The strip is built anew after the event that changed it: a chip's own
    // click is what often changes it, and the chip must outlive its handler.
    void RebuildObjectStrip();
    void BuildObjectStrip();
    size_t StagedChanges(const ObjectTab& tab) const;
    size_t StagedEverywhere() const;           // every tab's, the shown one's included
    bool TabSwitchBlocked();                   // a commit, an Apply, an import under way: says so

    // ===== BROWSE AND STRUCTURE =====
    void OpenObject(const NodeRef& ref);
    void ApplyTable(uint64_t generation, const UltraDbResult& result, const UltraDbTableSchema& table,
                    const SqliteTableSource& source);
    void LoadPage(size_t page);
    void ApplyPage(uint64_t generation, size_t page, const UltraDbResult& result, UltraDbResultSet rows);
    void ApplyCount(uint64_t generation, const UltraDbResult& result, int64_t rows);
    void ApplySequence(uint64_t generation, const UltraDbResult& result, const UltraDbResultSet& rows);
    // The tab's table back in Browse and Structure, as it was left.
    void RestoreObject(const ObjectTab& tab);
    void CountRows();
    void SortBrowse(int column);
    void UpdateBrowseBar();
    void ClearObject(const std::string& message);

    // ===== FILTERS =====
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildFilterBar();
    void FillFilterColumns();               // the open table's columns, in the dropdown
    void ShowFilterOf(int column);          // the column's filter, if it has one, in the fields
    void ApplyFilter();                     // the fields' filter, for their column
    void RemoveFilter();                    // the chosen column's
    void ClearFilters();
    // Browse with `pager` from now on: page 0 again, counted again.
    void ReplacePager(std::shared_ptr<TablePager> pager);
    void UpdateFilterBar();

    // ===== IN THE CELL =====
    void BeginCellEdit(int row, int column);
    void PlaceCellEditor();                 // over the cell, as the grid scrolls; ends it out of view
    void FinishCellEdit(bool keep);         // keep: stage what was typed
    void SetCellNull(int row, int column);
    const UltraDbColumnSchema* BrowseColumn(int column) const;   // null: not a column of the table
    bool IsBlobColumn(int column) const;

    // ===== BLOBS =====
    // The cell's bytes - staged ones first - in the blob dialog.
    void ViewBlob(int row, int column, bool console);
    void SaveBlob(const std::vector<uint8_t>& bytes, const std::string& name);
    void LoadBlob(int row, int column);     // a file's bytes, staged for the cell

    // ===== COPY =====
    enum class RowsSource { Browse, Console };
    void ShowBrowseMenu(int row, const UltraCanvas::UCEvent& event);
    void ShowResultMenu(int row, const UltraCanvas::UCEvent& event);
    void CopyRows(RowsSource source, ExportFormat format);

    // ===== EDITING =====
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildEditBar();
    // Why rows cannot be edited now, or "": no table, a view or a table
    // without a key, a read-only connection, a commit under way.
    std::string EditBlockReason() const;
    int SelectedBrowseRow() const;          // -1: none
    void AddRow();
    void EditRow(int row);
    void DeleteSelectedRow();
    void AfterEdit();                       // redraw the grid, recount the bar
    void AfterStage();                      // a change staged: AfterEdit, and with auto-commit, Commit
    bool AutoCommit() const;
    void PreviewChanges();
    void CommitChanges();
    void RunCommit();
    void ApplyCommit(uint64_t generation, const UltraDbResult& result, size_t failed,
                     const std::string& failedSql);
    void RevertChanges();
    void UpdateEditBar();

    // ===== STRUCTURE EDITING =====
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildStructureBar();
    // Why the structure cannot be edited now, or "": no table, a view, a
    // read-only connection, an Apply or a Commit under way.
    std::string StructureBlockReason() const;
    int SelectedColumn() const;             // -1: none
    void AddColumn();
    void EditColumn(int row);
    void DropSelectedColumn();              // or restore it, if it is to be dropped
    void AddIndex();
    void DropSelectedIndex();
    void RenameTable();
    void AfterStructureEdit();              // redraw the lists, recount the bar
    void PreviewStructure();
    void ApplyStructure();
    void RunApplyStructure();
    void FinishApplyStructure(uint64_t generation, const UltraDbResult& result, const std::string& failedSql,
                              const std::string& finalName, size_t count);
    void RevertStructure();
    void UpdateStructureBar();
    // Asks before `drop` throws away `count` staged changes of one kind
    // ("row change", "structure change").
    void ConfirmRevert(size_t count, const std::string& kind, std::function<void()> drop);

    // ===== IMPORT =====
    // Why rows cannot be imported into what Browse shows, or "".
    std::string ImportBlockReason() const;
    void ImportIntoTable();                 // pick a file, read it, map it, import
    void ReadImportFile(const std::string& path);
    void ShowImportMapping(uint64_t generation, std::vector<ImportGrid> grids);
    void RunImport(const ImportGrid& grid, const ImportMapping& mapping);
    void FinishImport(uint64_t generation, const UltraDbResult& result, size_t imported, size_t failedRow,
                      const std::string& file);
    void ImportSql();                       // pick a SQL file, read it, ask, run it on the console

    // ===== EXPORT =====
    // Browse: the whole table, read through a cursor; SQL: the rows shown.
    enum class ExportSource { Table, Result };
    void ExportRows(ExportSource source);
    void StartExport(ExportSource source, const std::string& path);
    void RunExport(ExportSource source, const std::string& path, ExportOptions options);
    void FinishExport(const UltraDbResult& result, size_t rows, const std::string& path, bool truncated);
    void UpdateExportButtons();

    // ===== TREE MENU AND OPERATIONS =====
    void ShowTreeMenu(UltraCanvas::TreeNode* node, const UltraCanvas::UCEvent& event);
    std::vector<UltraCanvas::MenuItemData> MaintenanceItems(const NodeRef& ref, const std::string& schema,
                                                            const std::string& table);
    // Why `spec` takes no change now, or "": read only.
    std::string WriteBlockReason(const ConnectionSpec& spec) const;
    // Runs `plan` on the connection's session, on the job queue; `done` on
    // the UI thread after.
    void RunOperation(const std::string& connectionId, const std::string& what, const OperationPlan& plan,
                      std::function<void(const UltraDbResult&, const UltraDbResultSet&)> done);
    // The plan shown first, then run. A production connection is said so.
    void ConfirmOperation(const std::string& connectionId, const std::string& title, const std::string& intro,
                          const OperationPlan& plan, const std::string& action, ButtonRole role,
                          std::function<void(const UltraDbResult&, const UltraDbResultSet&)> done);
    // The table read on the worker, then `then` on the UI thread.
    void ReadTableThen(const NodeRef& ref,
                       std::function<void(const UltraDbTableSchema&, const SqliteTableSource&)> then);
    void ReloadSchemaNode(const std::string& connectionId, const std::string& schema);
    void NewTable(const NodeRef& ref);
    void CopyTable(const NodeRef& ref);
    // The table's rows beside another table's (DbAdminCompare), and the
    // other made the same on request.
    void CompareData(const NodeRef& ref);
    void RenameObject(const NodeRef& ref);
    void EmptyTable(const NodeRef& ref);
    void DropObject(const NodeRef& ref);
    void RunMaintenance(const NodeRef& ref, const std::string& schema, const std::string& table, Maintenance task);
    void JournalMode(const NodeRef& ref, const std::string& schema, const std::string& mode);
    void DumpSchema(const NodeRef& ref);
    void ExportFromTree(const NodeRef& ref);
    void CopyName(const NodeRef& ref);

    // ===== SEARCH =====
    void StartSearch();
    void SearchNext(uint64_t generation);
    void StopSearch(const std::string& why);
    void OpenSearchHit(int row);
    void UpdateSearchBar();
    // The schema the tree's selection is in - SQLite's connection is its
    // "main" - which Search, Relations and Routines work on. False: none
    // (nothing picked, or a server's connection without a schema).
    bool SchemaScope(std::string& connectionId, std::string& schema) const;

    // ===== RELATIONS, ROUTINES, SERVER (DbAdminWindowSchema.cpp) =====
    // A tab was shown, or the tree's selection changed under it: Relations,
    // Routines and Server read theirs again when it is for elsewhere now.
    void OnPageShown(int tab);
    // Server's connection: the tree's selection, whatever is picked; the
    // schema its sizes list, when one is (SQLite: "main").
    bool ServerScope(std::string& connectionId, std::string& schema) const;
    void ReadRelations();
    void DrawRelations();                   // what was read, in the notation chosen
    void OpenRelationTable(const std::string& table);
    void UpdateRelationsBar();
    void ReadRoutines();
    void ShowRoutine(int row);              // its statement, under the list
    void EditRoutine(int row);              // its statement in the SQL tab, to change and run
    void ShowNewRoutineMenu();
    void NewRoutine(RoutineInfo::Kind kind);
    void DropRoutine();
    void UpdateRoutinesBar();
    int SelectedRoutine() const;            // -1: none
    void ReadServer();
    void StopServerSession(bool terminate); // the selected session's query, or the session
    void UpdateServerBar();
    ServerView ChosenServerView() const;
    void ReadMonitor(bool restart);         // one reading of the status, on a worker
    void ShowMonitor();                     // the charts, from monitor_

    // ===== SQL LOG =====
    void RefreshLog();                      // what is new, from log_ (a timer calls it)
    void RefillLog();                       // all of it again: the filter changed
    void CopyLog();
    std::string ConnectionLabel(const std::string& registered) const;

    // ===== SQL =====
    SqlRunner* RunnerFor(const ConnectionSpec& spec);
    enum class RunScope { Script, Statement, Explain };
    void RunSql(RunScope scope = RunScope::Script);
    // The completion list for the word at the caret: as it is typed (from
    // two letters, or after a '.'), or asked for with Ctrl+Space.
    void Complete(bool asked);
    // The catalog completion reads for the tree's connection and schema;
    // nullptr while it is being read (on the job queue).
    const CompletionCatalog* CompletionCatalogFor(const ConnectionSpec& spec, bool asked);
    // The caret's place in the editor's text, as a byte offset.
    size_t CaretOffset() const;
    void ShowSnippetsMenu();
    void SaveSnippet();
    void StopSql();
    void ApplyOutcome(const std::string& connectionId, const RunOutcome& outcome);
    void ShowStatementRows(int row);
    void UpdateSqlBar();
    void FillHistory();

    ConnectionStore store_;
    std::string storeNote_;          // why the list is in memory only, if it is
    std::string relayUrl_;           // the relay connected to last
    SnippetStore snippets_;

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> page_;
    std::shared_ptr<UltraCanvas::UltraCanvasSplitPane> split_;
    std::shared_ptr<UltraCanvas::UltraCanvasTabbedContainer> tabs_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> editButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> removeButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> statusLabel_;

    // Open objects: the strip of their tabs above the four tabs.
    std::vector<ObjectTab> openTabs_;
    int activeTab_ = -1;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> objectStrip_;
    std::string definitionText_, structureInfoText_;   // the shown object's, for its tab
    // Asked for while the object was being read: done once it is.
    bool exportWhenOpen_ = false;
    bool filterWhenOpen_ = false;
    ColumnFilter openFilter_;
    NodeRef whenOpenRef_;                 // the object they are for
    bool stripPending_ = false;
    bool IsObject(const NodeRef& a, const NodeRef& b) const {
        return a.connectionId == b.connectionId && a.schema == b.schema && a.name == b.name &&
               a.objectKind == b.objectKind;
    }

    std::shared_ptr<UltraCanvas::UltraCanvasTreeView> tree_;
    std::map<std::string, NodeRef> nodes_;
    std::set<std::string> loading_;      // nodes whose children are being read
    std::string selectedNode_;
    uint64_t nextNodeId_ = 0;

    // Browse
    std::shared_ptr<UltraCanvas::UltraCanvasButton> prevButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> nextButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> reloadButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> importButton_;
    bool importing_ = false;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> exportButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> exportRowsButton_;
    bool exporting_ = false;
    // Set when the window closes: a long export stops at its next page.
    std::shared_ptr<std::atomic<bool>> exportStop_ = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> browseInfo_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> browseNote_;
    std::shared_ptr<ResultSetModel> browseModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> browseView_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> browsePage_;
    uint64_t countGen_ = 0;              // the row count asked for last; an older answer is dropped

    // Filters: the column, the comparison, the value, and what is set.
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> filterColumn_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> filterOp_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> filterValue_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> filterApplyButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> filterRemoveButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> filterClearButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> filterSummary_;

    // The editor laid over a cell while it is edited, and what it edits.
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> cellEditor_;
    bool cellEditing_ = false;
    UltraDbRow cellRow_;                 // the row as read: its key addresses it
    std::string cellColumn_;
    uint64_t cellGen_ = 0;
    int cellViewRow_ = -1, cellViewColumn_ = -1;
    UltraCanvas::Rect2Di cellRect_;                   // where the editor was laid, in the grid
    UltraCanvas::TimerId cellTimer_ = 0;

    // The right-click menu shown last; kept until the next one.
    std::shared_ptr<UltraCanvas::UltraCanvasMenu> menu_;

    // Editing: the staged changes of the table Browse shows.
    EditBuffer edits_;
    bool committing_ = false;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> addRowButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> editRowButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> deleteRowButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> pendingLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> previewButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> revertButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> commitButton_;

    // Structure, and its staged edits. `source_` is the SQLite table's own
    // definition as it was read with the table: Apply reads it again and
    // refuses if it changed meanwhile.
    StructureEdit structure_;
    SqliteTableSource source_;
    bool applying_ = false;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> addColumnButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> editColumnButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> dropColumnButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> addIndexButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> dropIndexButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> renameTableButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> structurePending_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> structurePreviewButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> structureRevertButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> structureApplyButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> structureInfo_;
    std::shared_ptr<ColumnSchemaModel> columnModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> columnView_;
    std::shared_ptr<ConstraintModel> constraintModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> constraintView_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> definitionArea_;

    // The object Browse and Structure show. `objectGen_` rises with every
    // change of object, `viewGen_` with that and every change of sort, so a
    // late answer is told from a fresh one; a page answer must also be for
    // the page now asked for.
    std::string objectConnectionId_;    // the connection's id; empty: none
    NodeRef objectRef_;                 // the tree row it was opened from
    std::string objectLabel_;
    std::shared_ptr<const TablePager> pager_;
    size_t pageIndex_ = 0;
    size_t pageRows_ = 0;
    int64_t rowCount_ = -1;              // -1: not counted (yet)
    bool pageLoading_ = false;
    uint64_t objectGen_ = 0;
    uint64_t viewGen_ = 0;

    // SQL
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> sqlEditor_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> runButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> runStatementButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> explainButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> snippetsButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> stopButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> importSqlButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> historyDropdown_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> sqlInfo_;
    std::shared_ptr<StatementModel> statementModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> statementView_;
    int shownStatement_ = -1;            // the statement whose rows sqlResultModel_ holds
    std::shared_ptr<ResultSetModel> sqlResultModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListSortFilterProxy> sqlResultProxy_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> sqlResultView_;
    std::map<std::string, std::unique_ptr<SqlRunner>> runners_;   // by connection id
    std::vector<std::string> historyShown_;   // the dropdown's scripts, in its order
    std::string sqlConnectionId_;        // the console's connection: the tree's, when Run was pressed
    // Completion: a catalog per connection and default schema ("id/schema"),
    // read the first time it is needed, forgotten by Refresh and after a run.
    std::map<std::string, std::shared_ptr<const CompletionCatalog>> completionCatalogs_;
    std::set<std::string> completionLoading_;
    bool completionTyped_ = false;       // the editor's last change was a key typed

    // Search
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> searchInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> searchMode_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> searchButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> searchStopButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> searchInfo_;
    std::shared_ptr<SearchHitModel> searchModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> searchView_;
    bool searching_ = false;
    uint64_t searchGen_ = 0;             // rises with every search started or stopped
    std::string searchConnectionId_, searchSchema_, searchText_;
    SearchMode searchModeValue_ = SearchMode::Contains;
    std::vector<UltraDbObjectSummary> searchTargets_;
    size_t searchIndex_ = 0;
    std::vector<std::string> searchFailures_;
    std::string searchSummary_;          // how the last search ended

    // Relations: the tables read and their links, and what they are of.
    std::shared_ptr<UltraCanvas::UltraCanvasERDiagram> relationsDiagram_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> relationsReadButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> relationsFitButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> relationsNotation_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox> relationsTypes_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> relationsInfo_;
    std::vector<UltraDbTableSchema> relationsTables_;
    std::vector<RelationLink> relationsLinks_;
    std::string relationsConnectionId_, relationsSchema_;   // asked for last; "": nothing
    std::string relationsSummary_;
    bool relationsLoading_ = false;
    uint64_t relationsGen_ = 0;

    // Routines
    std::shared_ptr<RoutineModel> routineModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> routineView_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> routineText_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> routinesReadButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> routineNewButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> routineEditButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> routineDropButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> routinesInfo_;
    std::string routinesConnectionId_, routinesSchema_;     // asked for last; "": nothing
    std::string routinesSummary_;
    bool routinesLoading_ = false;
    uint64_t routinesGen_ = 0;

    // Server
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> serverViewPick_;
    std::vector<ServerView> serverViews_;    // the dropdown's, in its order
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> serverFilter_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> serverReadButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> serverCancelButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> serverEndButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> serverInfo_;
    std::shared_ptr<ResultSetModel> serverModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListSortFilterProxy> serverProxy_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> serverGrid_;
    std::string serverConnectionId_, serverSchema_;         // asked for last; "": nothing
    ServerView serverShown_ = ServerView::FileInfo;
    std::string serverSummary_;
    bool serverLoading_ = false;
    uint64_t serverGen_ = 0;
    // The monitor: a card per chart, made when the first reading names them.
    struct MonitorCard {
        std::string key;
        std::shared_ptr<UltraCanvas::UltraCanvasLabel> value;
        std::shared_ptr<UltraCanvas::UltraCanvasEngineLineChart> chart;
    };
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> serverMonitorBox_;
    std::vector<MonitorCard> monitorCards_;
    ServerMonitor monitor_;
    std::string monitorConnectionId_;    // whose readings monitor_ holds
    bool monitorReading_ = false;
    uint64_t monitorGen_ = 0;
    UltraCanvas::TimerId monitorTimer_ = 0;

    // SQL log
    std::shared_ptr<SqlLog> log_ = std::make_shared<SqlLog>(2000);
    int logObserver_ = 0;
    UltraCanvas::TimerId logTimer_ = 0;
    uint64_t shownSeq_ = 0;              // the newest entry the list has looked at
    std::shared_ptr<LogModel> logModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> logView_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> logInfo_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox> logSchemaReads_;
    std::shared_ptr<UltraCanvas::UltraCanvasSplitPane> rightSplit_;

    JobQueue jobs_;
    // Lives exactly as long as the window; a posted result checks it first.
    std::shared_ptr<char> alive_ = std::make_shared<char>(0);
};

} // namespace UltraDBAdmin

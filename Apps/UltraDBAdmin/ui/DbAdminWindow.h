// Apps/UltraDBAdmin/ui/DbAdminWindow.h
// UltraDBAdmin's window. A toolbar (Add, Edit, Remove a connection, Refresh),
// the tree of connections -> schemas -> tables, views and sequences on the
// left, and three tabs on the right:
//   Browse    - the selected table a page at a time (TablePager), sorted on
//               the server by a click on a column header, Previous / Next;
//               and edited: a row changed, added or deleted is staged
//               (EditBuffer), shown in the grid and as SQL, and written by
//               Commit in one transaction, or dropped by Revert;
//   Structure - its columns, its keys, indexes, foreign keys and triggers,
//               and a view's definition; and edited: a column added,
//               changed, renamed or dropped, an index added or dropped, the
//               table renamed is staged (StructureEdit), shown in the lists
//               and as DDL, and written by Apply in one transaction - on
//               SQLite with the table rebuilt where SQLite's ALTER cannot;
//   SQL       - a script editor with Run (Ctrl+Enter) and Stop, what each
//               statement did, and the rows of the one selected.
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
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminConnections.h"
#include "DbAdminEditBuffer.h"
#include "DbAdminExport.h"
#include "DbAdminImport.h"
#include "DbAdminJobQueue.h"
#include "DbAdminModels.h"
#include "DbAdminSqlRunner.h"
#include "DbAdminStructureEdit.h"
#include "DbAdminTablePager.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListSortFilterProxy.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasTextArea.h"
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
    void ResetNode(const std::string& nodeId);   // forget the children; load again on expand
    void OnNodeSelected(UltraCanvas::TreeNode* node);
    const NodeRef* RefOf(const std::string& nodeId) const;
    const ConnectionSpec* SelectedConnection() const;
    void AddConnection();
    void EditConnection();
    void RemoveConnection();
    void RefreshSelected();
    std::string SaveConnection(const ConnectionSpec& spec, const std::string& password,
                               bool clearPassword);

    // ===== BROWSE AND STRUCTURE =====
    void OpenObject(const NodeRef& ref);
    void ApplyTable(uint64_t generation, const UltraDbResult& result, const UltraDbTableSchema& table,
                    const SqliteTableSource& source);
    void LoadPage(size_t page);
    void ApplyPage(uint64_t generation, size_t page, const UltraDbResult& result, UltraDbResultSet rows);
    void ApplyCount(uint64_t generation, const UltraDbResult& result, int64_t rows);
    void ApplySequence(uint64_t generation, const UltraDbResult& result, const UltraDbResultSet& rows);
    void CountRows();
    void SortBrowse(int column);
    void UpdateBrowseBar();
    void ClearObject(const std::string& message);

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
    void PreviewChanges();
    void CommitChanges();
    void RunCommit();
    void ApplyCommit(uint64_t generation, const UltraDbResult& result, size_t failed,
                     const std::string& failedSql);
    void RevertChanges();
    void UpdateEditBar();
    // Runs `then` at once when nothing is staged - rows or structure - else
    // after the user agrees to drop the staged changes ("... and `what`?";
    // empty: just drop them).
    void ConfirmDiscard(const std::string& what, std::function<void()> then);

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

    // ===== SQL =====
    SqlRunner* RunnerFor(const ConnectionSpec& spec);
    void RunSql();
    void StopSql();
    void ApplyOutcome(const std::string& connectionId, const RunOutcome& outcome);
    void ShowStatementRows(int row);
    void UpdateSqlBar();
    void FillHistory();

    ConnectionStore store_;
    std::string storeNote_;          // why the list is in memory only, if it is

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> page_;
    std::shared_ptr<UltraCanvas::UltraCanvasSplitPane> split_;
    std::shared_ptr<UltraCanvas::UltraCanvasTabbedContainer> tabs_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> editButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> removeButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> statusLabel_;

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
    std::shared_ptr<UltraCanvas::UltraCanvasButton> stopButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> importSqlButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> historyDropdown_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> sqlInfo_;
    std::shared_ptr<StatementModel> statementModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> statementView_;
    std::shared_ptr<ResultSetModel> sqlResultModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListSortFilterProxy> sqlResultProxy_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> sqlResultView_;
    std::map<std::string, std::unique_ptr<SqlRunner>> runners_;   // by connection id
    std::vector<std::string> historyShown_;   // the dropdown's scripts, in its order
    std::string sqlConnectionId_;        // the console's connection: the tree's, when Run was pressed

    JobQueue jobs_;
    // Lives exactly as long as the window; a posted result checks it first.
    std::shared_ptr<char> alive_ = std::make_shared<char>(0);
};

} // namespace UltraDBAdmin

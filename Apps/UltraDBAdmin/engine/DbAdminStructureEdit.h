// Apps/UltraDBAdmin/engine/DbAdminStructureEdit.h
// Editing a table's structure, staged as Browse stages rows: a column added,
// changed, renamed or dropped, an index added or dropped, the table renamed -
// each a pending change, shown in Structure and as the DDL it becomes, and
// nothing reaches the database until Apply runs it all in one transaction
// (Docs/Research/UltraDBAdminDesignProposal.md, 5.3 Structure). Revert drops
// it.
//
// The DDL, per engine:
//   PostgreSQL - ALTER TABLE for everything: a type change converts with
//                USING CAST(column AS type); DDL is transactional, so a
//                failure anywhere leaves the table as it was.
//   SQLite     - ALTER TABLE where SQLite has it (RENAME COLUMN, DROP
//                COLUMN, ADD COLUMN, RENAME TO), and for a change of a
//                column's type, NOT NULL or default the documented rebuild
//                (DbAdminSqliteTable.h), with foreign keys off around the
//                transaction and checked before it commits.
// The statements run in a fixed order, so no step sees a name another has
// not made yet: drop indexes; rebuild or alter definitions under the old
// names; rename columns; drop columns; add columns; create indexes; rename
// the table.
//
// Every name goes through UltraDb_QuoteIdentifier. A type and a default are
// SQL the user writes - a name cannot be bound, nor can a type - and each is
// checked to be one fragment (CheckSqlFragment) before it is written in.
//
// A plain value class, like EditBuffer: the window keeps one for the table
// Structure shows and hands a copy to the worker.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminSqliteTable.h"

#include <UltraDatabase/UltraDatabase.h>

#include <cstddef>
#include <string>
#include <vector>

namespace UltraDBAdmin {

enum class SqlEngine { Sqlite, Postgres };

// A column as it is to be.
struct ColumnDraft {
    std::string origin;            // its name as read; "" for a new column
    std::string name;
    std::string type;              // as the engine spells it; "" (SQLite only): no type
    bool notNull = false;
    bool hasDefault = false;
    std::string defaultExpression; // SQL, as written after DEFAULT
    bool dropped = false;
};

struct IndexDraft {
    std::string name;
    std::vector<std::string> columns;   // the columns' names as they are to be, in key order
    bool unique = false;
};

// One statement of the plan.
struct DdlStep {
    enum class Phase { Before, Inside, After };   // around the transaction, or in it
    Phase phase = Phase::Inside;
    std::string sql;
    UltraDbParams params;
    std::string preview;           // the statement with its values written in
    std::string note;              // shown above it in the preview: why it is there
    bool check = false;            // a query that must return no rows
    std::string checkFailure;      // what rows mean, for the error
};

class StructureEdit {
public:
    enum class ColumnState { Unchanged, Changed, New, Dropped };

    StructureEdit() = default;
    StructureEdit(UltraDbTableSchema table, SqlEngine engine);

    const UltraDbTableSchema& Table() const { return table_; }
    SqlEngine Engine() const { return engine_; }

    // Why the structure cannot be edited here, or "" when it can.
    std::string ReadOnlyReason() const;

    // The table's columns as they are to be, in table order, then the new
    // ones; a column to be dropped stays listed (ColumnState::Dropped).
    const std::vector<ColumnDraft>& Columns() const { return columns_; }
    ColumnState StateOf(size_t at) const;
    const UltraDbColumnSchema* Original(size_t at) const;   // null for a new column

    const std::vector<IndexDraft>& NewIndexes() const { return newIndexes_; }
    const std::vector<std::string>& DroppedIndexes() const { return droppedIndexes_; }
    bool IndexDropped(const std::string& name) const;
    const std::string& NewName() const { return newName_; }     // "": not renamed

    // Stage a change. False, with `error`, when refused: a name already
    // taken, a type or default that is not one SQL fragment, a computed
    // column's definition, an index on a column SQLite cannot drop it with.
    bool AddColumn(const ColumnDraft& draft, std::string& error);
    bool UpdateColumn(size_t at, const ColumnDraft& draft, std::string& error);
    bool DropColumn(size_t at, std::string& error);     // a new column is just removed
    bool RestoreColumn(size_t at, std::string& error);  // undo a drop
    bool AddIndex(const IndexDraft& draft, std::string& error);
    bool DropIndex(const std::string& name, std::string& error);   // a new index is just removed
    bool RenameTable(const std::string& name, std::string& error); // "" or the same name: not renamed
    void Revert();

    bool Empty() const { return Count() == 0; }
    size_t Count() const;                 // changes, one per column, index and the rename
    size_t Count(ColumnState state) const;
    // SQLite has to rebuild the table: a column's type, NOT NULL or default changes.
    bool NeedsRebuild() const;
    // Drops a column or an index: said out loud before Apply.
    bool Destructive() const;

    // The statements, in the order they run. `source` is the table's own
    // definition, which a SQLite rebuild works from (ReadSqliteTableSource);
    // PostgreSQL ignores it. Quoting asks the connection: call it on a worker.
    UltraDbResult Plan(const std::string& connection, const SqliteTableSource& source,
                       std::vector<DdlStep>& out) const;

    // The plan as SQL to read, with BEGIN and COMMIT where the transaction is.
    static std::string PlanText(const std::vector<DdlStep>& steps);

    // Run a plan: its Before steps, the transaction, its After steps (which
    // run whatever happened, so foreign keys are switched back on). On
    // failure nothing has changed in the database; `failed` is the index of
    // the step that failed.
    static UltraDbResult Apply(const std::string& connection, const std::vector<DdlStep>& steps, size_t& failed);

private:
    bool SameName(const std::string& a, const std::string& b) const;
    bool CheckColumn(const ColumnDraft& draft, const ColumnDraft* replacing, std::string& error) const;
    bool DefinitionChanged(size_t at) const;
    std::string ColumnDefinition(const ColumnDraft& draft, const std::string& quotedName) const;

    UltraDbTableSchema table_;
    SqlEngine engine_ = SqlEngine::Sqlite;
    std::vector<ColumnDraft> columns_;
    std::vector<IndexDraft> newIndexes_;
    std::vector<std::string> droppedIndexes_;
    std::string newName_;
};

// The draft of a column as it was read.
ColumnDraft DraftOf(const UltraDbColumnSchema& column);

} // namespace UltraDBAdmin

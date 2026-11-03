// Apps/UltraDBAdmin/engine/DbAdminOperations.h
// What is done to a whole object rather than to its rows or columns - the
// Operations of Docs/Research/UltraDBAdminDesignProposal.md (5.2): create a
// table, copy one (its structure, or structure and rows), empty it, drop a
// table, view or sequence - and SQLite's maintenance: VACUUM, ANALYZE,
// PRAGMA integrity_check / quick_check / optimize and the journal mode.
// PostgreSQL's maintenance is VACUUM (ANALYZE) and ANALYZE; MySQL's CHECK,
// ANALYZE and OPTIMIZE TABLE, on a table.
//
// MySQL commits every statement that defines something as it runs - CREATE,
// DROP, RENAME, TRUNCATE - whatever transaction it is in, so a plan of them
// is not one transaction there, and its preview says so.
//
// Each operation is a plan first - the statements, shown as SQL before
// anything runs - then RunPlan. Names are quoted (QuotedName: SQLite and
// PostgreSQL both read "name"); a type and a default typed into the new-table
// form are checked to be one SQL fragment (CheckSqlFragment) before they are
// written in; a journal mode comes from a fixed list. Nothing the user typed
// becomes a statement of its own.
//
// A copy keeps what each engine can carry: on SQLite the table's own CREATE
// TABLE under the new name (CHECK, foreign keys, COLLATE, WITHOUT ROWID and
// all) and its indexes, renamed; on PostgreSQL CREATE TABLE ... (LIKE ...
// INCLUDING ALL) - defaults, constraints, indexes, identity, generated
// columns, comments - and an identity column's counter moved past the rows
// copied. Triggers are not copied on either: a trigger names its table in
// its body, and a copy firing the original's triggers is a surprise.
// Version: 0.2.0 - MySQL / MariaDB
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminSqliteTable.h"
#include "DbAdminStructureEdit.h"   // SqlEngine

#include <UltraDatabase/UltraDatabase.h>

#include <string>
#include <vector>

namespace UltraDBAdmin {

struct OperationPlan {
    std::vector<std::string> statements;
    bool transaction = true;       // all of them or none; VACUUM cannot run inside one
    std::vector<std::string> notes;   // said above the SQL in the preview
};

// The plan as SQL to read: its notes as comments, BEGIN / COMMIT around it
// when it is one transaction.
std::string PlanText(const OperationPlan& plan);

// Runs the plan on `connection`; `rows` (may be null) receives the last
// statement's rows - integrity_check's findings, the journal mode. `failed`
// is the index of the statement that failed. In one transaction a failure
// leaves nothing changed.
UltraDbResult RunPlan(const std::string& connection, const OperationPlan& plan, UltraDbResultSet* rows,
                      size_t& failed);

// "main"."orders" (`shop`.`orders` on MySQL) - schema and name quoted as
// `engine` quotes them; an empty schema leaves it out.
std::string QualifiedName(SqlEngine engine, const std::string& schema, const std::string& name);

// ===== Empty, drop =====

// Every row of the table goes. SQLite: DELETE FROM (and, with
// `restartNumbering`, the AUTOINCREMENT counter in sqlite_sequence);
// PostgreSQL: TRUNCATE TABLE (RESTART IDENTITY). A PostgreSQL table other
// tables refer to is refused by the server; CASCADE is never written.
OperationPlan EmptyTablePlan(SqlEngine engine, const std::string& schema, const std::string& table,
                             bool restartNumbering);

// DROP TABLE / VIEW / MATERIALIZED VIEW / FOREIGN TABLE / SEQUENCE. Never
// CASCADE: what depends on it makes the server refuse, and says what.
OperationPlan DropObjectPlan(SqlEngine engine, const std::string& schema, const std::string& name,
                             UltraDbObjectKind kind);

// ALTER ... RENAME TO: a table on SQLite (which renames nothing else), any
// object on PostgreSQL. False, with `error`, for an empty or unchanged name
// or a SQLite view.
bool RenameObjectPlan(SqlEngine engine, const std::string& schema, const std::string& name, UltraDbObjectKind kind,
                      const std::string& newName, OperationPlan& out, std::string& error);

// ===== Copy =====

// A copy of `table` as `newName` in the same schema, with its rows when
// `withData`. `source` is the SQLite table's own definition
// (ReadSqliteTableSource); the server engines ignore it. MySQL copies with
// CREATE TABLE ... LIKE, which carries the indexes and AUTO_INCREMENT but no
// foreign key. False, with `error`, for a
// name that is empty or the table's own, or a definition the SQLite parser
// does not read completely.
bool CopyTablePlan(SqlEngine engine, const UltraDbTableSchema& table, const SqliteTableSource& source,
                   const std::string& newName, bool withData, OperationPlan& out, std::string& error);

// A SQLite CREATE INDEX statement for another table: its index named
// `newIndex` (quoted, schema-qualified as the index is) on `newTable`
// (quoted, unqualified - SQLite puts an index in its table's schema).
bool RewriteCreateIndex(const std::string& sql, const std::string& newIndex, const std::string& newTable,
                        std::string& out, std::string& error);

// ===== Create =====

struct NewColumn {
    std::string name;
    std::string type;              // "" (SQLite only): no type
    bool notNull = false;
    bool primaryKey = false;       // part of the primary key
    bool autoIncrement = false;    // SQLite: INTEGER PRIMARY KEY AUTOINCREMENT; PostgreSQL: an identity column
    bool hasDefault = false;
    std::string defaultExpression;
};

// CREATE TABLE schema.name (columns, PRIMARY KEY (...)). Refused, with
// `error`: no name, no column, a column without a name or named twice, a
// type or default that is not one fragment, AUTOINCREMENT on a column that
// is not the one key column (SQLite: of type INTEGER), a PostgreSQL column
// without a type.
bool CreateTablePlan(SqlEngine engine, const std::string& schema, const std::string& name,
                     const std::vector<NewColumn>& columns, OperationPlan& out, std::string& error);

// ===== Maintenance =====

enum class Maintenance {
    Vacuum,           // SQLite: the whole file; PostgreSQL: VACUUM (ANALYZE), of a table or all
    Analyze,          // MySQL: ANALYZE TABLE
    IntegrityCheck,   // SQLite: PRAGMA integrity_check; MySQL: CHECK TABLE
    QuickCheck,       // SQLite: PRAGMA quick_check; MySQL: CHECK TABLE ... QUICK
    Optimize          // SQLite: PRAGMA optimize; MySQL: OPTIMIZE TABLE
};

// What `engine` offers, in menu order, on a table (`table`) or on a whole
// schema: on a SQLite table only ANALYZE (the checks and VACUUM are the
// file's); MySQL's statements each name a table, so it offers none on a
// schema.
std::vector<Maintenance> MaintenanceTasks(SqlEngine engine, bool table);
const char* MaintenanceText(Maintenance task);   // "Vacuum", "Check integrity", ...
// What it does, for the menu's tooltip and the confirmation.
const char* MaintenanceAbout(Maintenance task, SqlEngine engine);

// `table` "" for the whole schema (SQLite: the attached database).
OperationPlan MaintenancePlan(SqlEngine engine, Maintenance task, const std::string& schema,
                              const std::string& table);

// SQLite's journal modes, as PRAGMA journal_mode takes them.
const std::vector<std::string>& JournalModes();
// PRAGMA schema.journal_mode, read (`mode` "") or set; false for a mode not in
// JournalModes().
bool JournalModePlan(const std::string& schema, const std::string& mode, OperationPlan& out, std::string& error);

} // namespace UltraDBAdmin

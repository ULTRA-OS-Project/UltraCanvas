// Apps/UltraDBAdmin/engine/DbAdminSqliteTable.h
// What SQLite's limited ALTER TABLE needs: a table's own CREATE TABLE
// statement, read and rewritten. SQLite renames, adds and drops a column in
// place, but changes a column's type, NOT NULL or default only by the
// documented rebuild (sqlite.org/lang_altertable.html, "other kinds of table
// schema changes"): create the new table, copy the rows, drop the old one,
// rename the new one, recreate the indexes and triggers. The new table is the
// old statement with only the changed column definitions rewritten, so its
// CHECK constraints, foreign keys, COLLATE clauses, AUTOINCREMENT and table
// options (WITHOUT ROWID, STRICT) are kept exactly as written - never
// regenerated from what introspection reports, which knows nothing of CHECK.
//
// Also the check every type name and default expression typed into a form
// passes before it is written into DDL: one fragment of SQL, no `;`, no
// comment, quotes closed and parentheses balanced, so a slip cannot end the
// statement and start another.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraDatabase/UltraDatabase.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace UltraDBAdmin {

struct SqlToken {
    enum class Kind { Word, QuotedName, String, Number, Blob, Punct, Comment };
    Kind kind = Kind::Word;
    size_t begin = 0;     // byte offsets into the statement
    size_t end = 0;
    std::string text;     // the source text, as written
};

// The tokens of SQL text, whitespace dropped, comments kept (Kind::Comment).
// Quoting follows SQLite, which PostgreSQL shares for everything a form
// sends: "name", `name`, [name], 'text' with doubled quotes - and
// PostgreSQL's dollar quotes, $$...$$ and $tag$...$tag$, read as one string.
// False, with `error`, for an unterminated quote or comment.
bool TokenizeSql(const std::string& sql, std::vector<SqlToken>& out, std::string& error);

// A type name or default expression typed into a form, checked to be one
// fragment (see above). `what` names it in the error: "The type".
bool CheckSqlFragment(const std::string& text, const std::string& what, std::string& error);

// A name token's name: "a""b" -> a"b, [x] -> x, `x` -> x, 'x' -> x.
std::string UnquoteName(const SqlToken& token);

// SQLite compares names without regard to ASCII case, quoted or not.
bool SameSqliteName(const std::string& a, const std::string& b);

// "DEFAULT 0", "DEFAULT 'x'", "DEFAULT CURRENT_TIMESTAMP" - and an
// expression in parentheses, as SQLite requires: "DEFAULT (datetime('now'))".
// PRAGMA table_info reports an expression default without its parentheses,
// so this is also what puts them back.
std::string SqliteDefaultClause(const std::string& expression);

// One column definition of a CREATE TABLE.
struct SqliteColumnDef {
    struct Constraint {
        std::string kind;     // PRIMARY, NOT NULL, NULL, UNIQUE, CHECK, DEFAULT, COLLATE, REFERENCES, GENERATED
        size_t begin = 0;     // with its CONSTRAINT name, if it has one
        size_t end = 0;
    };
    std::string name;
    size_t begin = 0, end = 0;          // the whole definition
    size_t nameEnd = 0;
    size_t typeBegin = 0, typeEnd = 0;  // equal: no type
    std::vector<Constraint> constraints;
};

struct SqliteCreateTable {
    size_t headEnd = 0;                 // "CREATE TABLE [IF NOT EXISTS] name" ends here
    std::vector<SqliteColumnDef> columns;
    bool withoutRowid = false;
    bool autoincrement = false;
};

// False, with `error`, for anything that is not a plain CREATE TABLE ( ... )
// this parser reads completely - a virtual table, CREATE TABLE ... AS SELECT,
// a column constraint it does not know. Refusing is the point: a rebuild from
// a half-read definition would lose what was not read.
bool ParseCreateTable(const std::string& sql, SqliteCreateTable& out, std::string& error);

// How one column's definition is to change; the parts not `set` stay as written.
struct ColumnRewrite {
    std::string column;
    bool setType = false;
    std::string type;
    bool setNotNull = false;
    bool notNull = false;
    bool setDefault = false;
    bool hasDefault = false;
    std::string defaultExpression;
};

// The CREATE TABLE statement `sql` with its head replaced by
// "CREATE TABLE <newTable>" and the named columns rewritten; every other
// byte is kept.
bool RewriteCreateTable(const std::string& sql, const std::string& newTable,
                        const std::vector<ColumnRewrite>& changes, std::string& out, std::string& error);

// What a rebuild must put back, read from the schema it is in.
struct SqliteTableSource {
    std::string createSql;
    std::vector<std::pair<std::string, std::string>> indexes;   // name, CREATE INDEX (not the automatic ones)
    std::vector<std::string> triggers;                          // CREATE TRIGGER
    std::vector<std::string> referencedBy;   // tables of the schema with a foreign key to this one
    bool foreignKeys = false;                // PRAGMA foreign_keys, on this session
    bool legacyAlterTable = false;           // PRAGMA legacy_alter_table, likewise
    int64_t sequence = -1;                   // AUTOINCREMENT's sqlite_sequence value; -1: none

    bool operator==(const SqliteTableSource& other) const {
        return createSql == other.createSql && indexes == other.indexes && triggers == other.triggers &&
               referencedBy == other.referencedBy && foreignKeys == other.foreignKeys &&
               legacyAlterTable == other.legacyAlterTable && sequence == other.sequence;
    }
    bool operator!=(const SqliteTableSource& other) const { return !(*this == other); }
    // The same table, as far as a rebuild goes: its definition, indexes,
    // triggers and who refers to it - not the counter a new row moves on.
    bool SameDefinition(const SqliteTableSource& other) const {
        return createSql == other.createSql && indexes == other.indexes && triggers == other.triggers &&
               referencedBy == other.referencedBy;
    }
};

// Reads it on `connection` - the session the rebuild will run on, since both
// pragmas belong to a session. Call it where a query may run (a worker).
UltraDbResult ReadSqliteTableSource(const std::string& connection, const std::string& schema,
                                    const std::string& table, SqliteTableSource& out);

} // namespace UltraDBAdmin

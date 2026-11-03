// Apps/UltraDBAdmin/engine/DbAdminOperations.cpp
// Version: 0.2.0 - MySQL / MariaDB: DDL outside transactions, TRUNCATE / DELETE, RENAME TABLE,
//                 CREATE TABLE ... LIKE, AUTO_INCREMENT, CHECK / ANALYZE / OPTIMIZE TABLE
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminOperations.h"

#include "DbAdminExport.h"   // QuotedName

#include <algorithm>
#include <set>

namespace UltraDBAdmin {
namespace {

std::string Lower(std::string text) {
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

std::string Upper(std::string text) {
    for (auto& c : text)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return text;
}

// A SQL string literal: in single quotes, a quote inside doubled.
std::string StringLiteral(const std::string& text) {
    std::string out = "'";
    for (const char c : text) {
        if (c == '\'') out += '\'';
        out += c;
    }
    return out + "'";
}

bool IsWord(const SqlToken& token, const char* word) {
    return token.kind == SqlToken::Kind::Word && Upper(token.text) == word;
}

bool IsNameToken(const SqlToken& token) {
    return token.kind == SqlToken::Kind::Word || token.kind == SqlToken::Kind::QuotedName ||
           token.kind == SqlToken::Kind::String;
}

bool IsDot(const SqlToken& token) { return token.kind == SqlToken::Kind::Punct && token.text == "."; }

// The columns a copy carries rows in: all but those the database computes.
std::string WritableColumns(SqlEngine engine, const UltraDbTableSchema& table) {
    std::string names;
    for (const auto& column : table.columns) {
        if (column.generated) continue;
        names += (names.empty() ? "" : ", ") + QuotedName(engine, column.name);
    }
    return names;
}

// The copy's name for one of the table's indexes: the table's name in it
// replaced by the copy's ("idx_orders_day" -> "idx_orders_copy_day"), else
// the copy's name in front.
std::string CopiedIndexName(const std::string& index, const std::string& table, const std::string& copy) {
    const size_t at = Lower(index).find(Lower(table));
    if (at != std::string::npos && !table.empty()) return index.substr(0, at) + copy + index.substr(at + table.size());
    return copy + "_" + index;
}

bool IsIntegerType(const std::string& type) {
    static const std::set<std::string> integers = {"smallint", "integer", "int", "bigint", "int2", "int4", "int8"};
    return integers.count(Lower(type)) > 0;
}

// MySQL's integer types, with a display width or UNSIGNED after the word:
// "int", "bigint unsigned", "int(11)".
bool IsMysqlIntegerType(const std::string& type) {
    static const std::set<std::string> integers = {"tinyint", "smallint", "mediumint", "int", "integer", "bigint"};
    std::string word;
    for (const char c : Lower(type)) {
        if (!(c >= 'a' && c <= 'z')) break;
        word += c;
    }
    return integers.count(word) > 0;
}

// MySQL commits before and after every statement that defines something,
// whatever transaction it is in: a plan of such statements runs them one by
// one, and says so.
void NoTransaction(OperationPlan& plan, const char* what) {
    plan.transaction = false;
    plan.notes.push_back(std::string("MySQL commits ") + what + " as it runs: there is no undoing it with a rollback.");
}

} // namespace

std::string QualifiedName(SqlEngine engine, const std::string& schema, const std::string& name) {
    return schema.empty() ? QuotedName(engine, name) : QuotedName(engine, schema) + "." + QuotedName(engine, name);
}

std::string PlanText(const OperationPlan& plan) {
    std::string text;
    for (const auto& note : plan.notes) text += "-- " + note + "\n";
    if (!plan.notes.empty()) text += "\n";
    if (plan.transaction) text += "BEGIN;\n";
    for (const auto& statement : plan.statements) text += statement + ";\n";
    if (plan.transaction) text += "COMMIT;\n";
    return text;
}

UltraDbResult RunPlan(const std::string& connection, const OperationPlan& plan, UltraDbResultSet* rows,
                      size_t& failed) {
    failed = 0;
    UltraDbResultSet last;
    if (!plan.transaction) {
        for (size_t i = 0; i < plan.statements.size(); ++i) {
            last.Clear();
            const UltraDbResult result = UltraDb_Query(connection, plan.statements[i], {}, last);
            if (!result) { failed = i; return result; }
        }
        if (rows) *rows = std::move(last);
        return UltraDbResult::Ok();
    }
    UltraDbResult error;
    const UltraDbHandle tx = UltraDb_Begin(connection, &error);
    if (tx == UltraDbInvalidHandle) return error;
    for (size_t i = 0; i < plan.statements.size(); ++i) {
        last.Clear();
        const UltraDbResult result = UltraDb_QueryInTx(tx, plan.statements[i], {}, last);
        if (!result) {
            failed = i;
            UltraDb_Rollback(tx);
            return result;
        }
    }
    const UltraDbResult committed = UltraDb_Commit(tx);
    if (!committed) { failed = plan.statements.size(); return committed; }
    if (rows) *rows = std::move(last);
    return UltraDbResult::Ok();
}

// ===== Empty, drop =====

OperationPlan EmptyTablePlan(SqlEngine engine, const std::string& schema, const std::string& table,
                             bool restartNumbering) {
    OperationPlan plan;
    const std::string target = QualifiedName(engine, schema, table);
    if (engine == SqlEngine::MySql) {
        // TRUNCATE starts AUTO_INCREMENT again and cannot help it; DELETE
        // keeps the counter.
        if (restartNumbering) {
            plan.statements.push_back("TRUNCATE TABLE " + target);
            NoTransaction(plan, "TRUNCATE");
            plan.notes.push_back("TRUNCATE runs no triggers and starts AUTO_INCREMENT again at 1. A table other "
                                 "tables refer to is refused.");
        } else {
            plan.statements.push_back("DELETE FROM " + target);
            plan.notes.push_back("DELETE runs the table's DELETE triggers and foreign key actions for every row; "
                                 "AUTO_INCREMENT goes on where it was.");
        }
        return plan;
    }
    if (engine == SqlEngine::Sqlite) {
        plan.statements.push_back("DELETE FROM " + target);
        if (restartNumbering) {
            plan.statements.push_back("DELETE FROM " + QualifiedName(engine, schema, "sqlite_sequence") +
                                      " WHERE name = " + StringLiteral(table));
            plan.notes.push_back("The AUTOINCREMENT counter starts again at 1.");
        }
        plan.notes.push_back("DELETE runs the table's ON DELETE triggers and foreign key actions for every row.");
    } else {
        plan.statements.push_back("TRUNCATE TABLE " + target + (restartNumbering ? " RESTART IDENTITY" : ""));
        plan.notes.push_back("TRUNCATE runs no row triggers. A table other tables refer to is refused: CASCADE "
                             "is never written here.");
    }
    return plan;
}

OperationPlan DropObjectPlan(SqlEngine engine, const std::string& schema, const std::string& name,
                             UltraDbObjectKind kind) {
    OperationPlan plan;
    const char* what = "TABLE";
    switch (kind) {
        case UltraDbObjectKind::Table:            what = "TABLE"; break;
        case UltraDbObjectKind::View:             what = "VIEW"; break;
        case UltraDbObjectKind::MaterializedView: what = "MATERIALIZED VIEW"; break;
        case UltraDbObjectKind::ForeignTable:     what = "FOREIGN TABLE"; break;
        case UltraDbObjectKind::Sequence:         what = "SEQUENCE"; break;
    }
    plan.statements.push_back(std::string("DROP ") + what + " " + QualifiedName(engine, schema, name));
    if (engine == SqlEngine::MySql) NoTransaction(plan, "a DROP");
    if (kind == UltraDbObjectKind::Table)
        plan.notes.push_back(engine == SqlEngine::Sqlite
                                 ? "The table goes with its rows, indexes and triggers."
                                 : "The table goes with its rows, indexes and triggers. Without CASCADE, a view "
                                   "or foreign key that depends on it makes the server refuse.");
    return plan;
}

bool RenameObjectPlan(SqlEngine engine, const std::string& schema, const std::string& name, UltraDbObjectKind kind,
                      const std::string& newName, OperationPlan& out, std::string& error) {
    out = OperationPlan();
    if (newName.empty()) { error = "The new name is empty."; return false; }
    if (newName == name) { error = "That is its name already."; return false; }
    const char* what = "TABLE";
    switch (kind) {
        case UltraDbObjectKind::Table:            what = "TABLE"; break;
        case UltraDbObjectKind::View:             what = "VIEW"; break;
        case UltraDbObjectKind::MaterializedView: what = "MATERIALIZED VIEW"; break;
        case UltraDbObjectKind::ForeignTable:     what = "FOREIGN TABLE"; break;
        case UltraDbObjectKind::Sequence:         what = "SEQUENCE"; break;
    }
    if (engine == SqlEngine::Sqlite && kind != UltraDbObjectKind::Table) {
        error = "SQLite renames only a table; drop the view and create it again under the new name.";
        return false;
    }
    if (engine == SqlEngine::MySql) {
        // RENAME TABLE renames a view too; a sequence (MariaDB) is a table there.
        out.statements.push_back("RENAME TABLE " + QualifiedName(engine, schema, name) + " TO " +
                                 QualifiedName(engine, schema, newName));
        NoTransaction(out, "a RENAME");
        out.notes.push_back("A view or trigger that names the table by its old name stops working.");
        return true;
    }
    out.statements.push_back(std::string("ALTER ") + what + " " + QualifiedName(engine, schema, name) + " RENAME TO " +
                             QuotedName(engine, newName));
    if (engine == SqlEngine::Sqlite)
        out.notes.push_back("SQLite rewrites the views, triggers and foreign keys that name the table.");
    return true;
}

// ===== Copy =====

bool RewriteCreateIndex(const std::string& sql, const std::string& newIndex, const std::string& newTable,
                        std::string& out, std::string& error) {
    std::vector<SqlToken> all, tokens;
    if (!TokenizeSql(sql, all, error)) return false;
    for (auto& token : all)
        if (token.kind != SqlToken::Kind::Comment) tokens.push_back(std::move(token));
    size_t i = 0;
    if (i >= tokens.size() || !IsWord(tokens[i], "CREATE")) { error = "not a CREATE INDEX statement"; return false; }
    ++i;
    if (i < tokens.size() && IsWord(tokens[i], "UNIQUE")) ++i;
    if (i >= tokens.size() || !IsWord(tokens[i], "INDEX")) { error = "not a CREATE INDEX statement"; return false; }
    ++i;
    if (i + 2 < tokens.size() && IsWord(tokens[i], "IF") && IsWord(tokens[i + 1], "NOT") && IsWord(tokens[i + 2], "EXISTS"))
        i += 3;
    if (i >= tokens.size() || !IsNameToken(tokens[i])) { error = "the index has no name"; return false; }
    const size_t nameBegin = tokens[i].begin;
    size_t nameEnd = tokens[i].end;
    ++i;
    if (i + 1 < tokens.size() && IsDot(tokens[i]) && IsNameToken(tokens[i + 1])) {
        nameEnd = tokens[i + 1].end;
        i += 2;
    }
    if (i >= tokens.size() || !IsWord(tokens[i], "ON")) { error = "the index names no table"; return false; }
    ++i;
    if (i >= tokens.size() || !IsNameToken(tokens[i])) { error = "the index names no table"; return false; }
    const size_t tableBegin = tokens[i].begin, tableEnd = tokens[i].end;
    out = sql.substr(0, nameBegin) + newIndex + sql.substr(nameEnd, tableBegin - nameEnd) + newTable +
          sql.substr(tableEnd);
    return true;
}

bool CopyTablePlan(SqlEngine engine, const UltraDbTableSchema& table, const SqliteTableSource& source,
                   const std::string& newName, bool withData, OperationPlan& out, std::string& error) {
    out = OperationPlan();
    if (newName.empty()) { error = "The copy needs a name."; return false; }
    if (Lower(newName) == Lower(table.name)) { error = "The copy needs a name of its own."; return false; }
    if (table.kind != UltraDbObjectKind::Table) { error = "Only a table is copied here."; return false; }
    const std::string original = QualifiedName(engine, table.schema, table.name);
    const std::string copy = QualifiedName(engine, table.schema, newName);
    const std::string columns = WritableColumns(engine, table);

    if (engine == SqlEngine::MySql) {
        // LIKE copies the columns, their defaults, the indexes and
        // AUTO_INCREMENT; InnoDB moves the counter past the rows inserted.
        out.statements.push_back("CREATE TABLE " + copy + " LIKE " + original);
        if (withData && !columns.empty())
            out.statements.push_back("INSERT INTO " + copy + " (" + columns + ") SELECT " + columns + " FROM " + original);
        NoTransaction(out, "CREATE TABLE");
        if (!table.foreignKeys.empty())
            out.notes.push_back("CREATE TABLE ... LIKE carries no foreign keys: the copy refers to no other table.");
        if (!table.triggers.empty())
            out.notes.push_back("The table's triggers are not copied: each is bound to the original table.");
        return true;
    }

    if (engine == SqlEngine::Sqlite) {
        if (source.createSql.empty()) { error = "The table's definition was not read."; return false; }
        std::string create;
        if (!RewriteCreateTable(source.createSql, copy, {}, create, error)) {
            error = "The table's definition cannot be copied: " + error + ".";
            return false;
        }
        out.statements.push_back(create);
        for (const auto& [name, sql] : source.indexes) {
            std::string index;
            if (!RewriteCreateIndex(sql, QualifiedName(engine, table.schema, CopiedIndexName(name, table.name, newName)),
                                    QuotedName(engine, newName), index, error)) {
                error = "The index " + name + " cannot be copied: " + error + ".";
                return false;
            }
            out.statements.push_back(index);
        }
        if (withData && !columns.empty())
            out.statements.push_back("INSERT INTO " + copy + " (" + columns + ") SELECT " + columns + " FROM " + original);
        if (!source.triggers.empty())
            out.notes.push_back("The table's triggers are not copied: each names the original table in its body.");
        return true;
    }

    out.statements.push_back("CREATE TABLE " + copy + " (LIKE " + original + " INCLUDING ALL)");
    if (withData && !columns.empty()) {
        out.statements.push_back("INSERT INTO " + copy + " (" + columns + ") OVERRIDING SYSTEM VALUE SELECT " + columns +
                                 " FROM " + original);
        // An identity column's counter goes past the rows copied; a serial's
        // default still draws from the original's sequence (LIKE copies the
        // default, not the sequence), so pg_get_serial_sequence finds none
        // for it and setval is not called.
        for (const auto& column : table.columns) {
            if (!column.autoIncrement || column.generated) continue;
            out.statements.push_back("SELECT setval(pg_get_serial_sequence(" + StringLiteral(copy) + ", " +
                                     StringLiteral(column.name) + "), COALESCE(MAX(" + QuotedName(engine, column.name) +
                                     "), 0) + 1, false) FROM " + copy);
        }
    }
    for (const auto& column : table.columns) {
        if (column.autoIncrement && column.hasDefault && column.defaultExpression.rfind("nextval(", 0) == 0) {
            out.notes.push_back(column.name + " is a serial: the copy's default still draws numbers from the "
                                "original's sequence.");
        }
    }
    if (!table.triggers.empty())
        out.notes.push_back("The table's triggers are not copied: each is bound to the original table.");
    return true;
}

// ===== Create =====

bool CreateTablePlan(SqlEngine engine, const std::string& schema, const std::string& name,
                     const std::vector<NewColumn>& columns, OperationPlan& out, std::string& error) {
    out = OperationPlan();
    if (name.empty()) { error = "The table needs a name."; return false; }
    if (columns.empty()) { error = "A table needs at least one column."; return false; }
    std::set<std::string> seen;
    std::vector<std::string> key;
    const NewColumn* counted = nullptr;
    for (const auto& column : columns) {
        if (column.name.empty()) { error = "Every column needs a name."; return false; }
        if (!seen.insert(Lower(column.name)).second) { error = "Two columns are named " + column.name + "."; return false; }
        if (column.type.empty() && engine != SqlEngine::Sqlite) {
            error = column.name + " needs a type.";
            return false;
        }
        if (!column.type.empty() && !CheckSqlFragment(column.type, "The type of " + column.name, error)) return false;
        if (column.hasDefault &&
            !CheckSqlFragment(column.defaultExpression, "The default of " + column.name, error))
            return false;
        if (column.hasDefault && column.defaultExpression.empty()) {
            error = "The default of " + column.name + " is empty.";
            return false;
        }
        if (column.primaryKey) key.push_back(column.name);
        if (column.autoIncrement) {
            if (counted) { error = "Only one column is numbered by the database."; return false; }
            counted = &column;
        }
    }
    if (counted) {
        if (!counted->primaryKey || key.size() != 1) {
            error = counted->name + " is numbered by the database, so it must be the table's one key column.";
            return false;
        }
        if (counted->hasDefault) {
            error = counted->name + " is numbered by the database: it takes no default.";
            return false;
        }
        if (engine == SqlEngine::Sqlite && Upper(counted->type) != "INTEGER") {
            error = "SQLite numbers only an INTEGER PRIMARY KEY: give " + counted->name + " the type INTEGER.";
            return false;
        }
        if (engine == SqlEngine::Postgres && !IsIntegerType(counted->type)) {
            error = "An identity column is smallint, integer or bigint: " + counted->name + " is " + counted->type + ".";
            return false;
        }
        if (engine == SqlEngine::MySql && !IsMysqlIntegerType(counted->type)) {
            error = "AUTO_INCREMENT numbers an integer column: " + counted->name + " is " + counted->type + ".";
            return false;
        }
    }

    const bool inlineKey = counted && engine == SqlEngine::Sqlite;   // INTEGER PRIMARY KEY AUTOINCREMENT
    std::string sql = "CREATE TABLE " + QualifiedName(engine, schema, name) + " (";
    for (size_t i = 0; i < columns.size(); ++i) {
        const NewColumn& column = columns[i];
        std::string definition = "\n    " + QuotedName(engine, column.name);
        if (!column.type.empty()) definition += " " + column.type;
        if (&column == counted) {
            definition += engine == SqlEngine::Sqlite   ? " PRIMARY KEY AUTOINCREMENT"
                          : engine == SqlEngine::MySql ? " AUTO_INCREMENT"
                                                       : " GENERATED BY DEFAULT AS IDENTITY";
        }
        if (column.notNull) definition += " NOT NULL";
        if (column.hasDefault) {
            definition += engine == SqlEngine::Sqlite ? " " + SqliteDefaultClause(column.defaultExpression)
                                                      : " DEFAULT " + column.defaultExpression;
        }
        sql += definition + (i + 1 < columns.size() || (!key.empty() && !inlineKey) ? "," : "");
    }
    if (!key.empty() && !inlineKey) {
        std::string names;
        for (const auto& column : key) names += (names.empty() ? "" : ", ") + QuotedName(engine, column);
        sql += "\n    PRIMARY KEY (" + names + ")";
    }
    sql += "\n)";
    out.statements.push_back(sql);
    if (engine == SqlEngine::MySql) out.transaction = false;   // one statement; MySQL commits it as it runs
    if (key.empty())
        out.notes.push_back("No primary key: Browse shows its rows but cannot edit them, as it addresses a row by its key.");
    return true;
}

// ===== Maintenance =====

std::vector<Maintenance> MaintenanceTasks(SqlEngine engine, bool table) {
    // A SQLite table's maintenance is ANALYZE; the checks and VACUUM are the file's.
    if (engine == SqlEngine::Sqlite)
        return table ? std::vector<Maintenance>{Maintenance::Analyze}
                     : std::vector<Maintenance>{Maintenance::IntegrityCheck, Maintenance::QuickCheck,
                                                Maintenance::Analyze, Maintenance::Optimize, Maintenance::Vacuum};
    // MySQL's maintenance statements name their tables: offered on one.
    if (engine == SqlEngine::MySql)
        return table ? std::vector<Maintenance>{Maintenance::IntegrityCheck, Maintenance::QuickCheck,
                                                Maintenance::Analyze, Maintenance::Optimize}
                     : std::vector<Maintenance>{};
    return {Maintenance::Analyze, Maintenance::Vacuum};
}

const char* MaintenanceText(Maintenance task) {
    switch (task) {
        case Maintenance::Vacuum:         return "Vacuum";
        case Maintenance::Analyze:        return "Analyze";
        case Maintenance::IntegrityCheck: return "Check integrity";
        case Maintenance::QuickCheck:     return "Quick check";
        case Maintenance::Optimize:       return "Optimize";
    }
    return "";
}

const char* MaintenanceAbout(Maintenance task, SqlEngine engine) {
    switch (task) {
        case Maintenance::Vacuum:
            return engine == SqlEngine::Sqlite
                       ? "Rebuilds the database file, giving back the space deleted rows left; it needs as much "
                         "free disk again as the file takes, and holds the database meanwhile"
                       : "Reclaims the space dead rows take and refreshes the planner's statistics (VACUUM (ANALYZE))";
        case Maintenance::Analyze:
            return "Gathers the statistics the query planner chooses indexes by";
        case Maintenance::IntegrityCheck:
            return engine == SqlEngine::MySql
                       ? "Reads the table and its indexes and reports what is damaged (CHECK TABLE); \"OK\" when nothing is"
                       : "Reads every page, row and index of the database and reports what is damaged; \"ok\" when nothing is";
        case Maintenance::QuickCheck:
            return engine == SqlEngine::MySql
                       ? "CHECK TABLE ... QUICK: the rows are not scanned for wrong links"
                       : "The integrity check without checking that indexes match their tables: much faster";
        case Maintenance::Optimize:
            return engine == SqlEngine::MySql
                       ? "Rebuilds the table and its indexes, giving back the space deleted rows left (OPTIMIZE TABLE)"
                       : "Lets SQLite analyze what it finds worth analyzing (PRAGMA optimize)";
    }
    return "";
}

OperationPlan MaintenancePlan(SqlEngine engine, Maintenance task, const std::string& schema,
                              const std::string& table) {
    OperationPlan plan;
    plan.transaction = false;    // VACUUM cannot run in a transaction, and the rest need none
    if (engine == SqlEngine::MySql) {
        if (table.empty()) return plan;   // each names its table
        const std::string target = QualifiedName(engine, schema, table);
        switch (task) {
            case Maintenance::IntegrityCheck: plan.statements.push_back("CHECK TABLE " + target); break;
            case Maintenance::QuickCheck:     plan.statements.push_back("CHECK TABLE " + target + " QUICK"); break;
            case Maintenance::Analyze:        plan.statements.push_back("ANALYZE TABLE " + target); break;
            case Maintenance::Optimize:       plan.statements.push_back("OPTIMIZE TABLE " + target); break;
            case Maintenance::Vacuum:         break;   // SQLite's and PostgreSQL's
        }
        return plan;
    }
    if (engine == SqlEngine::Sqlite) {
        const std::string qs = QuotedName(engine, schema.empty() ? std::string("main") : schema);
        switch (task) {
            case Maintenance::Vacuum:         plan.statements.push_back("VACUUM " + qs); break;
            case Maintenance::Analyze:
                plan.statements.push_back("ANALYZE " + (table.empty() ? qs : qs + "." + QuotedName(engine, table)));
                break;
            case Maintenance::IntegrityCheck: plan.statements.push_back("PRAGMA " + qs + ".integrity_check"); break;
            case Maintenance::QuickCheck:     plan.statements.push_back("PRAGMA " + qs + ".quick_check"); break;
            case Maintenance::Optimize:       plan.statements.push_back("PRAGMA " + qs + ".optimize"); break;
        }
        return plan;
    }
    const std::string target = table.empty() ? std::string() : " " + QualifiedName(engine, schema, table);
    switch (task) {
        case Maintenance::Vacuum:  plan.statements.push_back("VACUUM (ANALYZE)" + target); break;
        case Maintenance::Analyze: plan.statements.push_back("ANALYZE" + target); break;
        default: break;   // SQLite's alone
    }
    return plan;
}

const std::vector<std::string>& JournalModes() {
    static const std::vector<std::string> modes = {"DELETE", "TRUNCATE", "PERSIST", "MEMORY", "WAL", "OFF"};
    return modes;
}

bool JournalModePlan(const std::string& schema, const std::string& mode, OperationPlan& out, std::string& error) {
    out = OperationPlan();
    out.transaction = false;     // the mode cannot change inside a transaction
    const std::string qs = QuotedName(SqlEngine::Sqlite, schema.empty() ? std::string("main") : schema);
    if (mode.empty()) {
        out.statements.push_back("PRAGMA " + qs + ".journal_mode");
        return true;
    }
    const auto& modes = JournalModes();
    if (std::find(modes.begin(), modes.end(), Upper(mode)) == modes.end()) {
        error = "SQLite has no journal mode " + mode + ".";
        return false;
    }
    out.statements.push_back("PRAGMA " + qs + ".journal_mode = " + Upper(mode));
    if (Upper(mode) == "OFF" || Upper(mode) == "MEMORY")
        out.notes.push_back("With the journal " + Lower(mode) + ", a crash during a write can damage the database.");
    return true;
}

} // namespace UltraDBAdmin

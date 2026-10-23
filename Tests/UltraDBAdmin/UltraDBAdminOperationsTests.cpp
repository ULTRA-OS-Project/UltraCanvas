// Tests/UltraDBAdmin/UltraDBAdminOperationsTests.cpp
// UltraDBAdmin's whole-object work and console helpers, without a window:
// the SQL log (every statement, from UltraDatabase's observer, values never
// logged), the operations - a table created, copied with its indexes and
// rows, emptied with its counter reset, dropped, SQLite's maintenance and
// journal mode - the search across every table (counts per column, LIKE
// escaped, ILIKE on PostgreSQL, blobs left out), the schema dump read back
// into an empty database exactly (SQLite; PostgreSQL with identity, serial
// and generated columns, CHECK, UNIQUE, foreign keys, comments, views), the
// saved snippets, the statement under the caret and EXPLAIN, the typed row
// form's checks and value functions, and auto-commit kept with a connection.
// SQLite in temporary folders; PostgreSQL where the tests of UltraDatabase
// find one (ULTRAFIBU_TEST_PG_*; REQUIRED makes a missing server a failure).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminConnections.h"
#include "DbAdminDump.h"
#include "DbAdminEditBuffer.h"
#include "DbAdminExport.h"
#include "DbAdminOperations.h"
#include "DbAdminSearch.h"
#include "DbAdminSnippets.h"
#include "DbAdminSqlLog.h"
#include "DbAdminSqlScript.h"
#include "DbAdminSqliteTable.h"
#include "DbAdminValueTypes.h"

#include "UltraCanvasPathUtf8.h"
#include <UltraDatabase/UltraDatabase.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace UltraDBAdmin;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  [FAIL] %s\n", what.c_str());
    }
}

void Section(const char* name) { std::printf("%s\n", name); }

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("ultradbadmin-ops-" + ConnectionStore::NewId());
        fs::create_directories(path);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    std::string File(const std::string& name) const { return UltraCanvas::PathToUtf8(path / name); }
};

bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

std::string ReadFile(const std::string& file) {
    std::ifstream in(UltraCanvas::PathFromUtf8(file), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

UltraDbResultSet Rows(const std::string& c, const std::string& sql, const UltraDbParams& params = {}) {
    UltraDbResultSet rows;
    const UltraDbResult result = UltraDb_Query(c, sql, params, rows);
    if (!result) std::printf("    (query failed: %s - %s)\n", sql.c_str(), result.message.c_str());
    return rows;
}

int64_t Count(const std::string& c, const std::string& sql) {
    const UltraDbResultSet rows = Rows(c, sql);
    return rows.Size() == 1 ? rows.Row(0)[0].AsInt64() : -1;
}

std::string SqliteFile(const std::string& name, const std::string& file) {
    UltraDbConnectionConfig cfg;
    cfg.name = name;
    cfg.driver = "sqlite";
    cfg.database = file;
    UltraDb_CloseConnection(name);
    UltraDb_RegisterConnection(cfg);
    return name;
}

std::string PostgresConnection(const std::string& name) {
    const char* database = std::getenv("ULTRAFIBU_TEST_PG_DB");
    const bool required = std::getenv("ULTRAFIBU_TEST_PG_REQUIRED") != nullptr;
    if (!database || !*database) {
        Check(!required, "a PostgreSQL server is configured");
        std::printf("  [SKIP] no PostgreSQL configured (ULTRAFIBU_TEST_PG_DB)\n");
        return std::string();
    }
    auto env = [](const char* key, const char* fallback) {
        const char* v = std::getenv(key);
        return std::string(v && *v ? v : fallback);
    };
    UltraDbConnectionConfig cfg;
    cfg.name = name;
    cfg.driver = "postgresql";
    cfg.database = database;
    cfg.host = env("ULTRAFIBU_TEST_PG_HOST", "/var/run/postgresql");
    cfg.port = std::atoi(env("ULTRAFIBU_TEST_PG_PORT", "5432").c_str());
    cfg.user = env("ULTRAFIBU_TEST_PG_USER", "");
    cfg.tls = UltraDbTls::Disable;       // the local test server
    if (!UltraDb_RegisterConnection(cfg) || !UltraDb_OpenConnection(name)) {
        Check(!required, "the PostgreSQL server is reached");
        std::printf("  [SKIP] PostgreSQL not reached\n");
        UltraDb_CloseConnection(name);
        return std::string();
    }
    return name;
}

// A column of a new table; the default, when given, is SQL.
NewColumn Col(const std::string& name, const std::string& type, bool notNull = false, bool key = false,
              bool counter = false, const char* defaultExpression = nullptr) {
    NewColumn column;
    column.name = name;
    column.type = type;
    column.notNull = notNull;
    column.primaryKey = key;
    column.autoIncrement = counter;
    column.hasDefault = defaultExpression != nullptr;
    if (defaultExpression) column.defaultExpression = defaultExpression;
    return column;
}

bool Run(const std::string& c, const OperationPlan& plan, UltraDbResultSet* rows = nullptr) {
    size_t failed = 0;
    const UltraDbResult result = RunPlan(c, plan, rows, failed);
    if (!result) std::printf("    (plan failed at %zu: %s)\n", failed, result.message.c_str());
    return result.success;
}

bool RunScript(const std::string& c, const std::string& script) {
    std::vector<UltraDbStatementResult> results;
    const UltraDbResult result = UltraDb_ExecBatch(c, script, results);
    if (!result) {
        std::printf("    (script failed at statement %zu: %s)\n", results.size(), result.message.c_str());
        if (!results.empty()) std::printf("    %s\n", results.back().sql.substr(0, 200).c_str());
    }
    return result.success;
}

UltraDbTableSchema Describe(const std::string& c, const std::string& schema, const std::string& table) {
    UltraDbTableSchema out;
    UltraDb_DescribeTable(c, schema, table, out);
    return out;
}

// ===== SQL log =====

void TestSqlLog() {
    Section("SQL log");
    const std::string c = SqliteFile("dbadmin-log", ":memory:");
    auto log = std::make_shared<SqlLog>(20);
    const int id = SqlLog::Attach(log, [c](const std::string& name) { return name == c; });
    Check(id > 0, "the log is attached");
    UltraDb_Exec(c, "CREATE TABLE secrets(id INTEGER PRIMARY KEY, word TEXT)");
    UltraDb_Exec(c, "INSERT INTO secrets(word) VALUES (?), (?)", {"hunter2", "swordfish"});
    Rows(c, "SELECT * FROM secrets");
    UltraDb_Exec(c, "INSERT INTO nowhere VALUES (1)");
    const std::string other = SqliteFile("dbadmin-log-other", ":memory:");
    Rows(other, "SELECT 1");                  // not the app's: not logged
    std::vector<LogEntry> entries = log->Since(0);
    Check(entries.size() == 4, "four statements logged, the other connection's not (" + std::to_string(entries.size()) + ")");
    if (entries.size() == 4) {
        Check(entries[1].boundValues == 2 && entries[1].affected == 2, "the insert: two values bound, two rows");
        Check(entries[2].rows == 2 && LogOutcome(entries[2]) == "2 rows", "the select: two rows");
        Check(!entries[3].ok && LogOutcome(entries[3]) == "failed" && !entries[3].message.empty(), "the failure and why");
        Check(entries[0].seq + 3 == entries[3].seq, "numbered in order");
        Check(LogTime(entries[0]).size() == 12, "a time of day to the millisecond: " + LogTime(entries[0]));
        Check(!LogDuration(entries[2]).empty(), "a duration");
        const std::string text = LogText(entries, [](const std::string&) { return std::string("shop"); });
        Check(!Has(text, "hunter2") && !Has(text, "swordfish"), "no bound value in the text");
        Check(Has(text, "-- ") && Has(text, "shop") && Has(text, "INSERT INTO secrets(word) VALUES (?), (?);"),
              "the text: a comment line per statement, the statement after it");
        Check(Has(text, "2 values bound"), "the count of bound values");
    }
    const uint64_t last = log->LastSeq();
    Rows(c, "SELECT 2");
    Check(log->Since(last).size() == 1, "Since gives only what is new");
    for (int i = 0; i < 30; ++i) Rows(c, "SELECT " + std::to_string(i));
    Check(log->Size() <= 20 && log->Dropped() > 0, "the newest are kept, the dropped counted");
    UltraDb_RemoveStatementObserver(id);
    const size_t size = log->Size();
    Rows(c, "SELECT 3");
    Check(log->Size() == size, "nothing more once detached");
    log->Clear();
    Check(log->Size() == 0, "cleared");
}

// ===== Operations =====

void TestCreateTablePlan() {
    Section("Create table plans");
    OperationPlan plan;
    std::string error;
    Check(!CreateTablePlan(SqlEngine::Sqlite, "main", "", {Col("id", "INTEGER")}, plan, error), "no name refused");
    Check(!CreateTablePlan(SqlEngine::Sqlite, "main", "t", {}, plan, error), "no column refused");
    Check(!CreateTablePlan(SqlEngine::Sqlite, "main", "t", {Col("a", "TEXT"), Col("A", "TEXT")}, plan, error),
          "two columns of one name refused");
    NewColumn bad = Col("a", "TEXT); DROP TABLE x; --");
    Check(!CreateTablePlan(SqlEngine::Sqlite, "main", "t", {bad}, plan, error), "a type that is not one fragment refused");
    NewColumn counted = Col("id", "TEXT", false, true, true);
    Check(!CreateTablePlan(SqlEngine::Sqlite, "main", "t", {counted}, plan, error) && Has(error, "INTEGER"),
          "SQLite numbers only an INTEGER PRIMARY KEY: " + error);
    NewColumn notKey = Col("id", "INTEGER", false, false, true);
    Check(!CreateTablePlan(SqlEngine::Sqlite, "main", "t", {notKey}, plan, error), "a counter not the key refused");
    Check(!CreateTablePlan(SqlEngine::Postgres, "public", "t", {Col("a", "")}, plan, error), "PostgreSQL needs a type");

    NewColumn id = Col("id", "INTEGER", false, true, true);
    NewColumn name = Col("name", "TEXT", true, false, false, "'nobody'");
    NewColumn made = Col("made", "TEXT", false, false, false, "datetime('now')");
    Check(CreateTablePlan(SqlEngine::Sqlite, "main", "people", {id, name, made}, plan, error), "a SQLite plan: " + error);
    const std::string sql = plan.statements.empty() ? std::string() : plan.statements[0];
    Check(Has(sql, "CREATE TABLE \"main\".\"people\"") && Has(sql, "\"id\" INTEGER PRIMARY KEY AUTOINCREMENT") &&
              Has(sql, "\"name\" TEXT NOT NULL DEFAULT 'nobody'") && Has(sql, "DEFAULT (datetime('now'))") &&
              !Has(sql, "PRIMARY KEY (\"id\")"),
          "the SQLite statement: " + sql);
    NewColumn pid = Col("id", "bigint", false, true, true);
    Check(CreateTablePlan(SqlEngine::Postgres, "public", "people", {pid, Col("name", "text", true)}, plan, error),
          "a PostgreSQL plan: " + error);
    Check(plan.statements.size() == 1 && Has(plan.statements[0], "\"id\" bigint GENERATED BY DEFAULT AS IDENTITY") &&
              Has(plan.statements[0], "PRIMARY KEY (\"id\")"),
          "the PostgreSQL statement: " + (plan.statements.empty() ? std::string() : plan.statements[0]));
    Check(CreateTablePlan(SqlEngine::Sqlite, "main", "keyless", {Col("a", "TEXT")}, plan, error) && !plan.notes.empty(),
          "a table without a key is made, with a note saying Browse cannot edit it");
    Check(Has(PlanText(plan), "BEGIN;") && Has(PlanText(plan), "-- No primary key"), "the plan as text");
}

void TestOperationsSqlite() {
    Section("Operations (SQLite)");
    TempDir dir;
    const std::string c = SqliteFile("dbadmin-ops", dir.File("ops.db"));
    std::string error;
    OperationPlan plan;
    NewColumn id = Col("id", "INTEGER", false, true, true);
    NewColumn day = Col("day", "TEXT", true);
    NewColumn total = Col("total", "REAL", false, false, false, "0");
    Check(CreateTablePlan(SqlEngine::Sqlite, "main", "orders", {id, day, total}, plan, error) && Run(c, plan),
          "the table is created");
    UltraDb_Exec(c, "ALTER TABLE orders ADD COLUMN doubled REAL GENERATED ALWAYS AS (total * 2) VIRTUAL");
    UltraDb_Exec(c, "CREATE INDEX idx_orders_day ON orders(day)");
    UltraDb_Exec(c, "CREATE TRIGGER orders_touch AFTER UPDATE ON orders BEGIN SELECT 1; END");
    for (int i = 1; i <= 5; ++i)
        UltraDb_Exec(c, "INSERT INTO orders(day, total) VALUES (?, ?)", {"2026-01-0" + std::to_string(i), i * 1.5});
    UltraDb_Exec(c, "DELETE FROM orders WHERE id = 5");   // the counter stays at 5

    // Copy, with rows.
    const UltraDbTableSchema orders = Describe(c, "main", "orders");
    SqliteTableSource source;
    ReadSqliteTableSource(c, "main", "orders", source);
    Check(!CopyTablePlan(SqlEngine::Sqlite, orders, source, "ORDERS", true, plan, error), "the table's own name refused");
    Check(CopyTablePlan(SqlEngine::Sqlite, orders, source, "orders_copy", true, plan, error), "a copy plan: " + error);
    Check(std::any_of(plan.notes.begin(), plan.notes.end(), [](const std::string& n) { return Has(n, "triggers"); }),
          "the plan says the triggers stay behind");
    Check(Run(c, plan), "the copy runs");
    Check(Count(c, "SELECT count(*) FROM orders_copy") == 4, "its rows");
    Check(Count(c, "SELECT doubled FROM orders_copy WHERE id = 2") == 6, "its generated column computed again");
    Check(Count(c, "SELECT count(*) FROM sqlite_master WHERE type = 'index' AND name = 'idx_orders_copy_day' AND "
                   "tbl_name = 'orders_copy'") == 1,
          "its index, renamed");
    Check(Count(c, "SELECT count(*) FROM sqlite_master WHERE type = 'trigger' AND tbl_name = 'orders_copy'") == 0,
          "no trigger");
    UltraDb_Exec(c, "INSERT INTO orders_copy(day) VALUES ('2026-02-01')");
    Check(Count(c, "SELECT max(id) FROM orders_copy") == 5, "its counter goes on from the rows copied");
    Check(CopyTablePlan(SqlEngine::Sqlite, orders, source, "orders_shape", false, plan, error) && Run(c, plan) &&
              Count(c, "SELECT count(*) FROM orders_shape") == 0,
          "a copy of the structure alone has no rows");

    std::string index;
    Check(RewriteCreateIndex("CREATE UNIQUE INDEX IF NOT EXISTS \"main\".\"ix\" ON \"t\" (a) WHERE a > 0",
                             "\"main\".\"ix2\"", "\"t2\"", index, error) &&
              index == "CREATE UNIQUE INDEX IF NOT EXISTS \"main\".\"ix2\" ON \"t2\" (a) WHERE a > 0",
          "an index rewritten for another table: " + index);

    // Empty, with the counter reset.
    Check(Run(c, EmptyTablePlan(SqlEngine::Sqlite, "main", "orders", true)), "emptied");
    Check(Count(c, "SELECT count(*) FROM orders") == 0, "no row is left");
    UltraDb_Exec(c, "INSERT INTO orders(day) VALUES ('2026-03-01')");
    Check(Count(c, "SELECT id FROM orders") == 1, "numbering starts again at 1");

    // Maintenance.
    UltraDbResultSet rows;
    Check(Run(c, MaintenancePlan(SqlEngine::Sqlite, Maintenance::IntegrityCheck, "main", ""), &rows) &&
              rows.Size() == 1 && rows.Row(0)[0].AsString() == "ok",
          "integrity check: ok");
    Check(Run(c, MaintenancePlan(SqlEngine::Sqlite, Maintenance::QuickCheck, "main", ""), &rows), "quick check");
    Check(Run(c, MaintenancePlan(SqlEngine::Sqlite, Maintenance::Analyze, "main", "orders")), "analyze a table");
    Check(Run(c, MaintenancePlan(SqlEngine::Sqlite, Maintenance::Optimize, "main", "")), "optimize");
    Check(Run(c, MaintenancePlan(SqlEngine::Sqlite, Maintenance::Vacuum, "main", "")), "vacuum");
    Check(!MaintenancePlan(SqlEngine::Sqlite, Maintenance::Vacuum, "main", "").transaction, "vacuum is no transaction");
    Check(JournalModePlan("main", "", plan, error) && Run(c, plan, &rows) && rows.Size() == 1 &&
              rows.Row(0)[0].AsString() == "delete",
          "the journal mode read: delete");
    Check(JournalModePlan("main", "wal", plan, error) && Run(c, plan, &rows) && rows.Size() == 1 &&
              rows.Row(0)[0].AsString() == "wal",
          "set to WAL");
    Check(!JournalModePlan("main", "wal; DROP TABLE orders", plan, error), "a mode not on the list refused");
    Check(MaintenanceTasks(SqlEngine::Postgres).size() == 2, "PostgreSQL offers analyze and vacuum");

    // Rename.
    Check(!RenameObjectPlan(SqlEngine::Sqlite, "main", "orders_shape", UltraDbObjectKind::Table, "orders_shape", plan, error),
          "the same name refused");
    Check(!RenameObjectPlan(SqlEngine::Sqlite, "main", "v", UltraDbObjectKind::View, "w", plan, error),
          "SQLite renames no view");
    Check(RenameObjectPlan(SqlEngine::Sqlite, "main", "orders_shape", UltraDbObjectKind::Table, "orders_form", plan,
                           error) && Run(c, plan) &&
              Count(c, "SELECT count(*) FROM sqlite_master WHERE name = 'orders_form'") == 1,
          "renamed");
    Check(RenameObjectPlan(SqlEngine::Sqlite, "main", "orders_form", UltraDbObjectKind::Table, "orders_shape", plan,
                           error) && Run(c, plan),
          "and back");

    // Drop.
    Check(Run(c, DropObjectPlan(SqlEngine::Sqlite, "main", "orders_shape", UltraDbObjectKind::Table)), "dropped");
    Check(Count(c, "SELECT count(*) FROM sqlite_master WHERE name = 'orders_shape'") == 0, "it is gone");
    UltraDb_Exec(c, "CREATE VIEW recent AS SELECT * FROM orders");
    Check(Run(c, DropObjectPlan(SqlEngine::Sqlite, "main", "recent", UltraDbObjectKind::View)), "a view dropped");
    // A failing plan in a transaction changes nothing.
    OperationPlan failing;
    failing.statements = {"DELETE FROM orders_copy", "DELETE FROM nowhere"};
    size_t failed = 0;
    Check(!RunPlan(c, failing, nullptr, failed).success && failed == 1, "the second statement fails");
    Check(Count(c, "SELECT count(*) FROM orders_copy") == 5, "and the first is rolled back");
    UltraDb_CloseConnection(c);
}

void TestOperationsPostgres() {
    Section("Operations (PostgreSQL)");
    const std::string c = PostgresConnection("dbadmin-ops-pg");
    if (c.empty()) return;
    const std::string schema = "dbadmin_" + ConnectionStore::NewId();
    UltraDb_Exec(c, "CREATE SCHEMA " + QuotedName(schema));
    std::string error;
    OperationPlan plan;
    NewColumn id = Col("id", "integer", false, true, true);
    NewColumn label = Col("label", "text", true, false, false, "'none'");
    Check(CreateTablePlan(SqlEngine::Postgres, schema, "items", {id, label}, plan, error) && Run(c, plan),
          "created with an identity key");
    const std::string items = QualifiedName(schema, "items");
    UltraDb_Exec(c, "ALTER TABLE " + items + " ADD COLUMN shout text GENERATED ALWAYS AS (upper(label)) STORED");
    UltraDb_Exec(c, "CREATE INDEX ON " + items + " (label)");
    for (const char* text : {"a", "b", "c"}) UltraDb_Exec(c, "INSERT INTO " + items + " (label) VALUES (?)", {text});

    const UltraDbTableSchema table = Describe(c, schema, "items");
    Check(CopyTablePlan(SqlEngine::Postgres, table, SqliteTableSource(), "items_copy", true, plan, error) && Run(c, plan),
          "copied with rows: " + error);
    const std::string copy = QualifiedName(schema, "items_copy");
    Check(Count(c, "SELECT count(*) FROM " + copy) == 3, "its rows");
    UltraDb_Exec(c, "INSERT INTO " + copy + " (label) VALUES ('d')");
    Check(Count(c, "SELECT max(id) FROM " + copy) == 4, "its identity goes on past the rows copied");
    Check(Rows(c, "SELECT shout FROM " + copy + " WHERE label = 'd'").Row(0)[0].AsString() == "D",
          "its generated column computes");
    Check(Count(c, "SELECT count(*) FROM pg_indexes WHERE schemaname = '" + schema + "' AND tablename = 'items_copy'") == 2,
          "its key and its index");

    Check(Run(c, EmptyTablePlan(SqlEngine::Postgres, schema, "items_copy", true)), "truncated");
    UltraDb_Exec(c, "INSERT INTO " + copy + " (label) VALUES ('e')");
    Check(Count(c, "SELECT id FROM " + copy) == 1, "RESTART IDENTITY numbers from 1 again");
    Check(Run(c, MaintenancePlan(SqlEngine::Postgres, Maintenance::Analyze, schema, "items")), "analyzed");
    UltraDb_Exec(c, "CREATE VIEW " + QualifiedName(schema, "few") + " AS SELECT * FROM " + items);
    Check(RenameObjectPlan(SqlEngine::Postgres, schema, "few", UltraDbObjectKind::View, "some", plan, error) &&
              Run(c, plan) && Count(c, "SELECT count(*) FROM " + QualifiedName(schema, "some")) == 3 &&
              Run(c, DropObjectPlan(SqlEngine::Postgres, schema, "some", UltraDbObjectKind::View)),
          "a view renamed, and dropped");
    Check(Run(c, MaintenancePlan(SqlEngine::Postgres, Maintenance::Vacuum, schema, "items")), "vacuumed");

    // A referenced table cannot be truncated without CASCADE, which is never written.
    UltraDb_Exec(c, "CREATE TABLE " + QualifiedName(schema, "refs") + " (item integer REFERENCES " + items + " (id))");
    Check(!Run(c, EmptyTablePlan(SqlEngine::Postgres, schema, "items", false)), "a referenced table is not truncated");
    Check(Count(c, "SELECT count(*) FROM " + items) == 3, "and keeps its rows");
    Check(!Run(c, DropObjectPlan(SqlEngine::Postgres, schema, "items", UltraDbObjectKind::Table)),
          "nor dropped while referenced");
    Check(Run(c, DropObjectPlan(SqlEngine::Postgres, schema, "refs", UltraDbObjectKind::Table)) &&
              Run(c, DropObjectPlan(SqlEngine::Postgres, schema, "items", UltraDbObjectKind::Table)),
          "dropped once nothing refers to it");
    UltraDb_Exec(c, "DROP SCHEMA " + QuotedName(schema) + " CASCADE");
    UltraDb_CloseConnection(c);
}

// ===== Search =====

void TestSearch() {
    Section("Search (SQLite)");
    TempDir dir;
    const std::string c = SqliteFile("dbadmin-search", dir.File("search.db"));
    UltraDb_Exec(c, "CREATE TABLE customers(id INTEGER PRIMARY KEY, name TEXT, city TEXT, photo BLOB)");
    UltraDb_Exec(c, "CREATE TABLE notes(id INTEGER PRIMARY KEY, body TEXT)");
    UltraDb_Exec(c, "CREATE VIEW berliners AS SELECT * FROM customers WHERE city = 'Berlin'");
    UltraDb_Exec(c, "INSERT INTO customers(name, city, photo) VALUES ('Ada', 'Berlin', X'4265726C696E'), "
                    "('Bob', 'berlin', NULL), ('Cy', 'Bonn', NULL), ('100% Bob', 'Köln', NULL)");
    UltraDb_Exec(c, "INSERT INTO notes(body) VALUES ('Moved to Berlin'), ('a_b'), ('axb')");
    std::vector<UltraDbObjectSummary> targets;
    Check(SearchTargets(c, "main", targets).success && targets.size() == 3, "two tables and a view are searched");
    std::vector<SearchHit> hits;
    for (const auto& target : targets)
        SearchTable(c, SqlEngine::Sqlite, Describe(c, "main", target.name), "berlin", SearchMode::Contains, hits);
    auto hit = [&](const std::string& table, const std::string& column) -> int64_t {
        for (const auto& h : hits)
            if (h.table == table && h.column == column) return h.rows;
        return 0;
    };
    Check(hit("customers", "city") == 2, "both Berlins, in any case");
    Check(hit("customers", "photo") == 0, "not the blob, though its bytes spell Berlin");
    Check(hit("notes", "body") == 1, "in a text");
    Check(hit("berliners", "city") == 1, "in a view");
    Check(hit("customers", "name") == 0, "no hit where there is none");
    hits.clear();
    SearchTable(c, SqlEngine::Sqlite, Describe(c, "main", "notes"), "a_b", SearchMode::Contains, hits);
    Check(hits.size() == 1 && hits[0].rows == 1, "_ is matched as itself, not as any character");
    hits.clear();
    SearchTable(c, SqlEngine::Sqlite, Describe(c, "main", "customers"), "100%", SearchMode::Contains, hits);
    Check(hits.size() == 1 && hits[0].column == "name", "% is matched as itself");
    hits.clear();
    SearchTable(c, SqlEngine::Sqlite, Describe(c, "main", "customers"), "berlin", SearchMode::Exactly, hits);
    Check(hits.size() == 1 && hits[0].rows == 1, "exactly: case and all");
    std::string sql;
    UltraDbParams params;
    Check(SearchQuery(SqlEngine::Sqlite, Describe(c, "main", "customers"), "x'; DROP TABLE notes; --", SearchMode::Contains,
                      sql, params) &&
              !Has(sql, "DROP") && params.size() == 3,
          "the text is bound, once per column searched");
    UltraDb_CloseConnection(c);
}

void TestSearchPostgres() {
    Section("Search (PostgreSQL)");
    const std::string c = PostgresConnection("dbadmin-search-pg");
    if (c.empty()) return;
    const std::string schema = "dbadmin_" + ConnectionStore::NewId();
    UltraDb_Exec(c, "CREATE SCHEMA " + QuotedName(schema));
    const std::string t = QualifiedName(schema, "people");
    UltraDb_Exec(c, "CREATE TABLE " + t + " (id serial PRIMARY KEY, name text, born date, active boolean, pic bytea)");
    UltraDb_Exec(c, "INSERT INTO " + t + " (name, born, active) VALUES ('Ada', '1815-12-10', true), "
                    "('ADA', '1990-01-01', false), ('Bea', '1815-01-01', NULL)");
    std::vector<SearchHit> hits;
    Check(SearchTable(c, SqlEngine::Postgres, Describe(c, schema, "people"), "ada", SearchMode::Contains, hits).success,
          "searched");
    Check(hits.size() == 1 && hits[0].column == "name" && hits[0].rows == 2, "ILIKE: Ada and ADA");
    hits.clear();
    SearchTable(c, SqlEngine::Postgres, Describe(c, schema, "people"), "1815", SearchMode::Contains, hits);
    Check(hits.size() == 1 && hits[0].column == "born" && hits[0].rows == 2, "a date read as text");
    hits.clear();
    SearchTable(c, SqlEngine::Postgres, Describe(c, schema, "people"), "true", SearchMode::Exactly, hits);
    Check(hits.size() == 1 && hits[0].column == "active" && hits[0].rows == 1, "a boolean read as text");
    UltraDb_Exec(c, "DROP SCHEMA " + QuotedName(schema) + " CASCADE");
    UltraDb_CloseConnection(c);
}

// ===== Dump =====

void TestDumpSqlite() {
    Section("Schema dump (SQLite)");
    TempDir dir;
    const std::string c = SqliteFile("dbadmin-dump", dir.File("source.db"));
    UltraDb_Exec(c, "CREATE TABLE customers(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL CHECK (name <> ''))");
    UltraDb_Exec(c, "CREATE TABLE orders(id INTEGER PRIMARY KEY, customer INTEGER REFERENCES customers(id), "
                    "note TEXT, data BLOB, total REAL, twice REAL GENERATED ALWAYS AS (total * 2) VIRTUAL)");
    UltraDb_Exec(c, "CREATE INDEX orders_customer ON orders(customer)");
    UltraDb_Exec(c, "CREATE TRIGGER orders_note AFTER INSERT ON orders WHEN new.note IS NULL BEGIN "
                    "UPDATE orders SET note = 'none' WHERE id = new.id; END");
    UltraDb_Exec(c, "CREATE VIEW big AS SELECT * FROM orders WHERE total > 10");
    UltraDb_Exec(c, "INSERT INTO customers(name) VALUES ('Ada'), ('O''Brien'), ('Cy')");
    UltraDb_Exec(c, "DELETE FROM customers WHERE name = 'Cy'");
    UltraDb_Exec(c, "INSERT INTO orders(customer, note, data, total) VALUES (1, 'it''s; done -- yes', X'00FF10', 12.5), "
                    "(2, NULL, NULL, 3), (1, 'line\nbreak', NULL, NULL)");
    const std::string path = dir.File("dump.sql");
    size_t tables = 0, rows = 0;
    std::vector<std::string> progress;
    UltraDbResult result = DumpSchema(c, SqlEngine::Sqlite, "main", path, DumpOptions(), tables, rows, nullptr,
                                      [&](const std::string& table, size_t) { progress.push_back(table); });
    Check(result.success, "dumped: " + result.message);
    Check(tables == 2 && rows == 5, "two tables, five rows (" + std::to_string(tables) + ", " + std::to_string(rows) + ")");
    Check(!progress.empty(), "progress was reported");
    const std::string text = ReadFile(path);
    Check(Has(text, "PRAGMA foreign_keys=OFF;") && Has(text, "BEGIN TRANSACTION;") && Has(text, "COMMIT;"),
          "framed as sqlite3's .dump frames it");
    Check(Has(text, "CHECK (name <> '')"), "the table's own CREATE TABLE, CHECK and all");
    Check(Has(text, "X'00ff10'") || Has(text, "X'00FF10'"), "a blob as a literal");
    Check(!Has(text, "\"twice\") VALUES"), "the generated column is not written to");
    Check(Has(text, "INSERT INTO sqlite_sequence VALUES('customers', 3);"), "the AUTOINCREMENT counter");
    Check(text.find("CREATE INDEX") > text.find("INSERT INTO \"orders\""), "indexes after the rows");
    Check(text.find("CREATE TRIGGER") > text.find("INSERT INTO \"orders\""), "the trigger after the rows, so it does not fire");
    Check(fs::exists(UltraCanvas::PathFromUtf8(path)) && !fs::exists(UltraCanvas::PathFromUtf8(path + ".part")),
          "the file in place, no part left");

    // Read back into an empty database: the same rows, counters and objects.
    const std::string copy = SqliteFile("dbadmin-dump-copy", dir.File("copy.db"));
    Check(RunScript(copy, text), "the dump runs on an empty database");
    Check(Count(copy, "SELECT count(*) FROM orders") == 3 && Count(copy, "SELECT count(*) FROM customers") == 2, "its rows");
    Check(Rows(copy, "SELECT note FROM orders WHERE id = 1").Row(0)[0].AsString() == "it's; done -- yes",
          "a value with a quote, a ';' and '--' read back exactly");
    Check(Rows(copy, "SELECT note FROM orders WHERE id = 3").Row(0)[0].AsString() == "line\nbreak", "a line break kept");
    Check(Rows(copy, "SELECT data FROM orders WHERE id = 2").Row(0)[0].IsNull(), "NULL stays NULL");
    Check(Rows(copy, "SELECT note FROM orders WHERE id = 2").Row(0)[0].AsString() == "none",
          "a value the trigger set in the source, as it was (the trigger is made after the rows)");
    Check(Rows(copy, "SELECT data FROM orders WHERE id = 1").Row(0)[0].AsBlob() == std::vector<uint8_t>{0x00, 0xFF, 0x10},
          "the blob byte for byte");
    Check(Count(copy, "SELECT twice FROM orders WHERE id = 1") == 25, "the generated column computes again");
    Check(Count(copy, "SELECT seq FROM sqlite_sequence WHERE name = 'customers'") == 3, "the counter");
    Check(Count(copy, "SELECT count(*) FROM big") == 1, "the view");
    Check(Count(copy, "SELECT count(*) FROM sqlite_master WHERE type IN ('index', 'trigger') AND name IN "
                      "('orders_customer', 'orders_note')") == 2,
          "the index and the trigger");

    // Again with DROP first: it runs over the copy it made.
    DumpOptions drop;
    drop.dropFirst = true;
    Check(DumpSchema(c, SqlEngine::Sqlite, "main", path, drop, tables, rows).success, "dumped with DROP IF EXISTS");
    Check(RunScript(copy, ReadFile(path)) && Count(copy, "SELECT count(*) FROM orders") == 3,
          "it replaces what is there, rows not doubled");

    DumpOptions structure;
    structure.data = false;
    Check(DumpSchema(c, SqlEngine::Sqlite, "main", path, structure, tables, rows).success && rows == 0 &&
              !Has(ReadFile(path), "INSERT INTO") && Has(ReadFile(path), "CREATE TABLE"),
          "structure only: no rows");
    DumpOptions data;
    data.structure = false;
    Check(DumpSchema(c, SqlEngine::Sqlite, "main", path, data, tables, rows).success && rows == 5 &&
              !Has(ReadFile(path), "CREATE TABLE") && Has(ReadFile(path), "INSERT INTO"),
          "data only: no CREATE");
    DumpOptions none;
    none.structure = none.data = false;
    Check(!DumpSchema(c, SqlEngine::Sqlite, "main", dir.File("none.sql"), none, tables, rows).success, "a dump of nothing refused");

    // Stopped: the file that was there stays.
    std::ofstream(UltraCanvas::PathFromUtf8(path), std::ios::binary) << "keep me";
    std::atomic<bool> stop{true};
    result = DumpSchema(c, SqlEngine::Sqlite, "main", path, DumpOptions(), tables, rows, &stop);
    Check(!result.success && ReadFile(path) == "keep me" && !fs::exists(UltraCanvas::PathFromUtf8(path + ".part")),
          "a stopped dump leaves the file that was there");
    UltraDb_CloseConnection(c);
    UltraDb_CloseConnection(copy);
}

void TestDumpPostgres() {
    Section("Schema dump (PostgreSQL)");
    const std::string c = PostgresConnection("dbadmin-dump-pg");
    if (c.empty()) return;
    const std::string schema = "dbadmin_" + ConnectionStore::NewId();
    const std::string s = QuotedName(schema);
    UltraDb_Exec(c, "CREATE SCHEMA " + s);
    UltraDb_Exec(c, "CREATE TABLE " + s + ".customers (id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY, "
                    "name text NOT NULL UNIQUE, CONSTRAINT named CHECK (length(name) > 0))");
    UltraDb_Exec(c, "CREATE TABLE " + s + ".orders (id serial PRIMARY KEY, customer integer REFERENCES " + s +
                    ".customers(id) ON DELETE CASCADE, total numeric(10,2) DEFAULT 0, "
                    "net numeric(10,2) GENERATED ALWAYS AS (total / 1.19) STORED, made timestamptz DEFAULT now(), "
                    "payload jsonb, data bytea, \"Odd Name\" text)");
    UltraDb_Exec(c, "CREATE INDEX orders_made ON " + s + ".orders (made DESC)");
    UltraDb_Exec(c, "COMMENT ON TABLE " + s + ".orders IS 'Every order, it''s all here'");
    UltraDb_Exec(c, "COMMENT ON COLUMN " + s + ".orders.total IS 'gross'");
    UltraDb_Exec(c, "CREATE VIEW " + s + ".big AS SELECT id, total FROM " + s + ".orders WHERE total > 10");
    UltraDb_Exec(c, "INSERT INTO " + s + ".customers (name) VALUES ('Ada'), ('O''Brien')");
    UltraDb_Exec(c, "INSERT INTO " + s + ".orders (customer, total, payload, data, \"Odd Name\") VALUES "
                    "(1, 119.00, '{\"a\": [1, 2]}', '\\x00ff', 'x'), (2, 5.5, NULL, NULL, NULL)");

    PostgresDdl ddl;
    const UltraDbResult described = PostgresTableDdl(c, Describe(c, schema, "orders"), ddl);
    Check(described.success, "the definition is read: " + described.message);
    Check(Has(ddl.create, "\"id\" serial") && Has(ddl.create, "GENERATED ALWAYS AS ((total / 1.19)) STORED") &&
              Has(ddl.create, "DEFAULT now()") && Has(ddl.create, "PRIMARY KEY (id)"),
          "serial, generated, default, key: " + ddl.create);
    Check(ddl.foreignKeys.size() == 1 && Has(ddl.foreignKeys[0], "ON DELETE CASCADE"), "the foreign key, after");
    Check(ddl.indexes.size() == 1 && Has(ddl.indexes[0], "made DESC"), "the index no constraint owns");
    Check(ddl.comments.size() == 2, "the table's comment and the column's");
    Check(std::find(ddl.insertColumns.begin(), ddl.insertColumns.end(), "net") == ddl.insertColumns.end(),
          "the generated column is not written to");
    // A table's export as SQL: its definition first, its counter after the rows.
    {
        TempDir out;
        UltraDb_Exec(c, "CREATE TABLE " + s + ".tags (id serial PRIMARY KEY, tag text UNIQUE)");
        UltraDb_Exec(c, "INSERT INTO " + s + ".tags (tag) VALUES ('a'), ('b')");
        PostgresDdl tags;
        PostgresTableDdl(c, Describe(c, schema, "tags"), tags);
        ExportOptions options;
        options.format = ExportFormat::Sql;
        options.engine = SqlEngine::Postgres;
        options.table = QualifiedName(schema, "tags");
        options.preamble = tags.create + ";\n";
        for (const auto& counter : tags.counters) options.postamble += counter + ";\n";
        size_t written = 0;
        const std::string file = out.File("tags.sql");
        Check(ExportQuery(c, "SELECT * FROM " + s + ".tags ORDER BY id", file, options, written).success && written == 2,
              "the table's rows exported");
        const std::string text = ReadFile(file);
        Check(text.find("CREATE TABLE") < text.find("INSERT INTO") && text.find("setval") > text.find("INSERT INTO") &&
                  text.find("setval") < text.find("COMMIT;"),
              "its definition first, its counter after the rows, before COMMIT");
        UltraDb_Exec(c, "DROP TABLE " + s + ".tags");
        Check(RunScript(c, text), "the file rebuilds it");
        UltraDb_Exec(c, "INSERT INTO " + s + ".tags (tag) VALUES ('c')");
        Check(Count(c, "SELECT max(id) FROM " + s + ".tags") == 3, "and numbering goes on after its rows");
        UltraDb_Exec(c, "DROP TABLE " + s + ".tags");
    }
    PostgresDdl customers;
    PostgresTableDdl(c, Describe(c, schema, "customers"), customers);
    Check(customers.overridingSystemValue && Has(customers.create, "GENERATED ALWAYS AS IDENTITY") &&
              Has(customers.create, "CONSTRAINT \"named\" CHECK") && Has(customers.create, "UNIQUE (name)"),
          "identity ALWAYS, CHECK, UNIQUE: " + customers.create);

    TempDir dir;
    const std::string path = dir.File("pg.sql");
    size_t tables = 0, rows = 0;
    UltraDbResult result = DumpSchema(c, SqlEngine::Postgres, schema, path, DumpOptions(), tables, rows);
    Check(result.success && tables == 2 && rows == 4, "dumped: " + result.message);
    const std::string text = ReadFile(path);
    Check(Has(text, "OVERRIDING SYSTEM VALUE") && Has(text, "setval(pg_get_serial_sequence"), "identities carried over");
    // Read back into the same database, after the schema is gone.
    UltraDb_Exec(c, "DROP SCHEMA " + s + " CASCADE");
    Check(RunScript(c, text), "the dump runs on a database without the schema");
    Check(Count(c, "SELECT count(*) FROM " + s + ".orders") == 2, "its rows");
    Check(Rows(c, "SELECT net::text FROM " + s + ".orders WHERE customer = 1").Row(0)[0].AsString() == "100.00",
          "the generated column computes again");
    Check(Rows(c, "SELECT payload->>'a' FROM " + s + ".orders WHERE customer = 1").Row(0)[0].AsString() == "[1, 2]",
          "jsonb read back");
    Check(Rows(c, "SELECT encode(data, 'hex') FROM " + s + ".orders WHERE customer = 1").Row(0)[0].AsString() == "00ff",
          "bytea read back");
    UltraDb_Exec(c, "INSERT INTO " + s + ".customers (name) VALUES ('Cy')");
    Check(Count(c, "SELECT max(id) FROM " + s + ".customers") == 3, "the identity goes on after the rows");
    UltraDb_Exec(c, "INSERT INTO " + s + ".orders (customer) VALUES (3)");
    Check(Count(c, "SELECT max(id) FROM " + s + ".orders") == 3, "the serial goes on after the rows");
    Check(!UltraDb_Exec(c, "INSERT INTO " + s + ".customers (name) VALUES ('')").success, "the CHECK is back");
    Check(!UltraDb_Exec(c, "INSERT INTO " + s + ".orders (customer) VALUES (99)").success, "the foreign key is back");
    Check(Rows(c, "SELECT obj_description('" + schema + ".orders'::regclass, 'pg_class')").Row(0)[0].AsString() ==
              "Every order, it's all here",
          "the comment is back");
    Check(Count(c, "SELECT count(*) FROM " + s + ".big") == 1, "the view is back");
    // With DROP first, over what is there.
    DumpOptions drop;
    drop.dropFirst = true;
    Check(DumpSchema(c, SqlEngine::Postgres, schema, path, drop, tables, rows).success, "dumped with DROP IF EXISTS");
    Check(RunScript(c, ReadFile(path)) && Count(c, "SELECT count(*) FROM " + s + ".orders") == 3,
          "it replaces the tables in place");
    UltraDb_Exec(c, "DROP SCHEMA " + s + " CASCADE");
    UltraDb_CloseConnection(c);
}

// ===== Snippets, console =====

void TestSnippets() {
    Section("Saved snippets");
    TempDir dir;
    std::string error;
    {
        SnippetStore store;
        Check(store.Open(UltraCanvas::PathToUtf8(dir.path), error), "opened: an empty list");
        Check(!store.Put({"", "SELECT 1"}, error), "no name refused");
        Check(!store.Put({"empty", " ; "}, error), "no SQL refused");
        Check(store.Put({"Top customers", "SELECT * FROM customers LIMIT 10"}, error), "kept");
        Check(store.Put({"active", "SELECT 1"}, error), "another");
        Check(store.Put({"TOP CUSTOMERS", "SELECT 2"}, error), "the same name in another case replaces it");
        Check(store.All().size() == 2 && store.All()[0].name == "active", "by name, in no case");
        Check(store.Find("top customers") && store.Find("top customers")->sql == "SELECT 2", "found in any case");
    }
    SnippetStore again;
    Check(again.Open(UltraCanvas::PathToUtf8(dir.path), error) && again.All().size() == 2, "read back from the file");
    Check(again.Remove("Active", error) && again.All().size() == 1, "removed");
    SnippetStore memory;
    Check(memory.Open("", error) && memory.Put({"x", "SELECT 1"}, error), "kept in memory without a folder");
}

void TestStatementAt() {
    Section("The statement under the caret");
    const std::string script = "SELECT 1;\n\n-- the second\nSELECT ';' AS semi;\nUPDATE t SET a = 1";
    const std::vector<StatementSpan> spans = StatementSpans(script);
    Check(spans.size() == 3, "three statements");
    auto at = [&](size_t offset) {
        StatementSpan span;
        if (!StatementAt(script, offset, span)) return std::string();
        return script.substr(span.begin, span.end - span.begin);
    };
    Check(at(0) == "SELECT 1", "at the start: the first");
    Check(at(9) == "SELECT 1", "on its ';': still the first");
    Check(at(10) == "SELECT 1", "in the blank line below: the one above");
    Check(at(script.find("';'") + 1) == "SELECT ';' AS semi", "a ';' in a string does not split");
    Check(at(script.size()) == "UPDATE t SET a = 1", "at the end: the last, without a ';'");
    Check(spans.size() == 3 && spans[2].semicolon == spans[2].end, "the last has no ';'");
    StatementSpan none;
    Check(!StatementAt("  -- nothing\n", 0, none), "a script of comments has no statement");
    Check(StatementSpans("SELECT 'open").empty(), "a quote never closed: none");
    const std::string trigger = "CREATE TRIGGER x AFTER INSERT ON t BEGIN UPDATE t SET a = 1; END;\nSELECT 2";
    Check(StatementSpans(trigger).size() == 2, "a trigger's body is one statement");
    Check(ExplainSql(SqlEngine::Sqlite, "SELECT 1") == "EXPLAIN QUERY PLAN SELECT 1", "SQLite's plan");
    Check(ExplainSql(SqlEngine::Postgres, "SELECT 1") == "EXPLAIN SELECT 1", "PostgreSQL's plan, not ANALYZE");
    const std::string c = SqliteFile("dbadmin-explain", ":memory:");
    UltraDb_Exec(c, "CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT)");
    const UltraDbResultSet plan = Rows(c, ExplainSql(SqlEngine::Sqlite, "SELECT * FROM t WHERE a = 1"));
    Check(plan.Size() >= 1, "EXPLAIN QUERY PLAN returns the plan's rows");
    UltraDb_CloseConnection(c);
}

// ===== Row form =====

void TestValueTypes() {
    Section("The row form's types and value functions");
    auto column = [](const std::string& type, UltraDbType value = UltraDbType::Text) {
        UltraDbColumnSchema c;
        c.name = "c";
        c.declaredType = type;
        c.valueType = value;
        return c;
    };
    Check(KindOfColumn(column("boolean")) == ValueKind::Boolean, "boolean");
    Check(KindOfColumn(column("INTEGER")) == ValueKind::Integer && KindOfColumn(column("bigint")) == ValueKind::Integer,
          "integers");
    Check(KindOfColumn(column("numeric(10,2)")) == ValueKind::Decimal && KindOfColumn(column("REAL")) == ValueKind::Decimal,
          "decimals");
    Check(KindOfColumn(column("timestamp with time zone")) == ValueKind::Timestamp &&
              KindOfColumn(column("DATETIME")) == ValueKind::Timestamp,
          "timestamps");
    Check(KindOfColumn(column("date")) == ValueKind::Date && KindOfColumn(column("time without time zone")) == ValueKind::Time,
          "date, time");
    Check(KindOfColumn(column("uuid")) == ValueKind::Uuid && KindOfColumn(column("jsonb")) == ValueKind::Json, "uuid, json");
    Check(KindOfColumn(column("varchar(80)")) == ValueKind::Text && KindOfColumn(column("BLOB")) == ValueKind::Blob,
          "text, blob");
    Check(KindOfColumn(column("", UltraDbType::Int)) == ValueKind::Integer, "an untyped column by its values");
    std::string error;
    Check(CheckValue(ValueKind::Integer, "qty", "-12", error) && !CheckValue(ValueKind::Integer, "qty", "12.5", error),
          "whole numbers");
    Check(CheckValue(ValueKind::Decimal, "price", "12.5", error) && CheckValue(ValueKind::Decimal, "price", "1e6", error) &&
              !CheckValue(ValueKind::Decimal, "price", "12,5", error) && Has(error, "price"),
          "decimals with a dot: " + error);
    Check(CheckValue(ValueKind::Date, "d", "2026-01-31", error) && !CheckValue(ValueKind::Date, "d", "31.01.2026", error),
          "dates");
    Check(CheckValue(ValueKind::Timestamp, "t", "2026-01-31 12:30", error) &&
              CheckValue(ValueKind::Timestamp, "t", "2026-01-31T12:30:00+02", error) &&
              !CheckValue(ValueKind::Timestamp, "t", "noon", error),
          "timestamps");
    Check(CheckValue(ValueKind::Uuid, "u", "123e4567-e89b-12d3-a456-426614174000", error) &&
              !CheckValue(ValueKind::Uuid, "u", "123", error),
          "uuids");
    Check(CheckValue(ValueKind::Boolean, "b", "true", error) && !CheckValue(ValueKind::Boolean, "b", "maybe", error),
          "booleans");
    Check(CheckValue(ValueKind::Text, "x", "anything, at all", error), "text takes anything");
    Check(!ValueFunctions(SqlEngine::Postgres, ValueKind::Uuid).empty() &&
              ValueFunctions(SqlEngine::Postgres, ValueKind::Uuid)[0].sql == "gen_random_uuid()",
          "a UUID column is offered gen_random_uuid()");
    Check(ValueFunctions(SqlEngine::Sqlite, ValueKind::Integer).empty(), "a number column no function");
    Check(IsValueFunction("CURRENT_TIMESTAMP") && !IsValueFunction("1); DROP TABLE t; --"), "only the listed functions");

    // Staged and written: the function is in the statement, not bound.
    const std::string c = SqliteFile("dbadmin-functions", ":memory:");
    UltraDb_Exec(c, "CREATE TABLE stamps(id INTEGER PRIMARY KEY, at TEXT, note TEXT)");
    EditBuffer edits(Describe(c, "main", "stamps"));
    Check(edits.InsertRow({{"at", CellValue::FunctionValue("CURRENT_TIMESTAMP")}, {"note", CellValue::Of("hi")}}, error),
          "a new row with a function");
    Check(!edits.InsertRow({{"at", CellValue::FunctionValue("1); DROP TABLE stamps; --")}}, error), "an unlisted one refused");
    std::vector<GeneratedStatement> statements;
    Check(edits.Statements(c, statements).success && statements.size() == 1 &&
              Has(statements[0].sql, "VALUES (CURRENT_TIMESTAMP, ?)") && statements[0].params.size() == 1,
          "written in, the text bound: " + (statements.empty() ? std::string() : statements[0].sql));
    Check(statements.size() == 1 && Has(statements[0].preview, "CURRENT_TIMESTAMP"), "and in the preview");
    size_t failed = 0;
    Check(edits.Commit(c, failed).success, "committed");
    const UltraDbResultSet rows = Rows(c, "SELECT at FROM stamps");
    Check(rows.Size() == 1 && rows.Row(0)[0].AsString().size() == 19, "the database's time: " +
                                                                           (rows.Size() ? rows.Row(0)[0].AsString() : ""));
    EditBuffer update(Describe(c, "main", "stamps"));
    Check(update.UpdateRow(rows.Size() ? Rows(c, "SELECT * FROM stamps").Row(0) : UltraDbRow(),
                           {{"note", CellValue::FunctionValue("CURRENT_DATE")}}, error) &&
              update.Statements(c, statements).success && statements.size() == 1 &&
              Has(statements[0].sql, "SET \"note\" = CURRENT_DATE WHERE") && statements[0].params.size() == 1,
          "an update to a function: only the key bound");
    UltraDb_CloseConnection(c);
}

void TestAutoCommitKept() {
    Section("Auto-commit, kept with the connection");
    TempDir dir;
    std::string error;
    std::string id;
    {
        ConnectionStore store;
        Check(store.Open(UltraCanvas::PathToUtf8(dir.path), error), "opened");
        ConnectionSpec spec;
        spec.name = "quick";
        spec.database = dir.File("q.db");
        spec.autoCommit = true;
        id = store.Put(spec, "", false, error);
        Check(!id.empty(), "saved");
    }
    ConnectionStore again;
    Check(again.Open(UltraCanvas::PathToUtf8(dir.path), error) && again.Find(id) && again.Find(id)->autoCommit, "read back: auto-commit on");
    if (const ConnectionSpec* spec = again.Find(id)) {
        Check(ConnectionIdOf(RegisteredName(*spec)) == id && ConnectionIdOf(ConsoleName(*spec)) == id,
              "both sessions' names lead back to the connection");
    }
    Check(ConnectionIdOf("someone.else").empty(), "another program's connection is not the app's");
}

} // namespace

int main() {
    std::printf("UltraDBAdmin operations tests\n\n");
    TestSqlLog();
    TestCreateTablePlan();
    TestOperationsSqlite();
    TestOperationsPostgres();
    TestSearch();
    TestSearchPostgres();
    TestDumpSqlite();
    TestDumpPostgres();
    TestSnippets();
    TestStatementAt();
    TestValueTypes();
    TestAutoCommitKept();
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

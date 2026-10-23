// Tests/UltraDBAdmin/UltraDBAdminSchemaTests.cpp
// UltraDBAdmin's stage 4 engine, without a window: the Relations tab's
// tables and links (a one-to-many, an optional key, a one-to-one, a table
// that refers to itself, a key to a table not read, a limit), the Routines
// tab (triggers on both engines; functions, procedures, an aggregate and an
// overload on PostgreSQL - each listed, its definition run again as edited,
// dropped, and a new one made from its template) and the Server tab
// (SQLite's file; PostgreSQL's sessions with this one among them, a running
// query cancelled from another session, the settings filter with LIKE's
// characters typed, the sizes).
// SQLite in temporary folders; PostgreSQL where the tests of UltraDatabase
// find one (ULTRAFIBU_TEST_PG_*; REQUIRED makes a missing server a failure).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminConnections.h"
#include "DbAdminExport.h"
#include "DbAdminOperations.h"
#include "DbAdminRelations.h"
#include "DbAdminRoutines.h"
#include "DbAdminServer.h"

#include "UltraCanvasPathUtf8.h"
#include <UltraDatabase/UltraDatabase.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
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
        path = fs::temp_directory_path() / ("ultradbadmin-schema-" + ConnectionStore::NewId());
        fs::create_directories(path);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    std::string File(const std::string& name) const { return UltraCanvas::PathToUtf8(path / name); }
};

bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

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

bool Run(const std::string& c, const OperationPlan& plan) {
    size_t failed = 0;
    const UltraDbResult result = RunPlan(c, plan, nullptr, failed);
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

const RelationLink* LinkOf(const std::vector<RelationLink>& links, const std::string& child, const std::string& parent) {
    for (const auto& link : links)
        if (link.child == child && link.parent == parent) return &link;
    return nullptr;
}

const RoutineInfo* RoutineOf(const std::vector<RoutineInfo>& routines, const std::string& name,
                             const std::string& arguments = std::string()) {
    for (const auto& routine : routines)
        if (routine.name == name && (arguments.empty() || routine.arguments == arguments)) return &routine;
    return nullptr;
}

// The value of `column` in the row whose first cell is `key`; "" when none.
std::string Cell(const UltraDbResultSet& rows, const std::string& key, const char* column) {
    for (size_t i = 0; i < rows.Size(); ++i)
        if (rows.Row(i)[size_t(0)].AsString() == key)
            return rows.Row(i)[column].IsNull() ? std::string() : rows.Row(i)[column].AsString();
    return std::string();
}

// The tables both engines' relation tests make: customers; orders with a
// required key to customers; profiles keyed by its customer (one-to-one);
// employees with an optional key to themselves; a note referring to a table
// that is not there; and a view, not a table.
const char* kRelationsSchema =
    "CREATE TABLE customers(id INTEGER PRIMARY KEY, name TEXT NOT NULL);"
    "CREATE TABLE orders(id INTEGER PRIMARY KEY, customer_id INTEGER NOT NULL REFERENCES customers(id), total REAL);"
    "CREATE TABLE profiles(customer_id INTEGER PRIMARY KEY REFERENCES customers(id), bio TEXT);"
    "CREATE TABLE employees(id INTEGER PRIMARY KEY, manager_id INTEGER REFERENCES employees(id));"
    "CREATE TABLE codes(id INTEGER PRIMARY KEY, customer_id INTEGER NOT NULL UNIQUE REFERENCES customers(id));"
    "CREATE VIEW big_orders AS SELECT * FROM orders WHERE total > 100;";

void CheckRelations(const std::vector<RelationLink>& links, const char* engine) {
    const std::string e = std::string(" (") + engine + ")";
    const RelationLink* order = LinkOf(links, "orders", "customers");
    Check(order && order->columns == std::vector<std::string>{"customer_id"} &&
              order->refColumns == std::vector<std::string>{"id"},
          "orders -> customers on customer_id = id" + e);
    Check(order && !order->optional && !order->oneToOne && !order->selfReference,
          "a required one-to-many" + e);
    const RelationLink* profile = LinkOf(links, "profiles", "customers");
    Check(profile && profile->oneToOne && !profile->optional, "profiles: keyed by its customer, one-to-one" + e);
    const RelationLink* code = LinkOf(links, "codes", "customers");
    Check(code && code->oneToOne, "codes: a unique key column, one-to-one" + e);
    const RelationLink* manager = LinkOf(links, "employees", "employees");
    Check(manager && manager->selfReference && manager->optional && !manager->oneToOne,
          "employees -> employees: refers to itself, optional" + e);
    std::vector<std::string> ids;
    for (const auto& link : links) ids.push_back(link.id);
    std::sort(ids.begin(), ids.end());
    Check(std::adjacent_find(ids.begin(), ids.end()) == ids.end(), "the links' ids are unique" + e);
    for (const auto& link : links) Check(!link.name.empty(), "a link has a name: " + link.id + e);
}

// ===== Relations =====

void TestRelationsSqlite() {
    Section("Relations (SQLite)");
    TempDir dir;
    const std::string c = SqliteFile("dbadmin-rel", dir.File("rel.db"));
    Check(RunScript(c, std::string(kRelationsSchema) +
                           "CREATE TABLE notes(id INTEGER PRIMARY KEY, ghost_id INTEGER REFERENCES ghosts(id));"),
          "the tables made");
    std::vector<UltraDbTableSchema> tables;
    bool truncated = true;
    std::vector<std::string> failed;
    Check(ReadSchemaTables(c, "main", 100, tables, truncated, failed).success, "read");
    Check(tables.size() == 6 && !truncated && failed.empty(),
          "six tables, the view left out (" + std::to_string(tables.size()) + ")");
    size_t outside = 0;
    const std::vector<RelationLink> links = RelationLinks(tables, outside);
    Check(links.size() == 4 && outside == 1,
          "four links drawn, the one to a missing table counted (" + std::to_string(links.size()) + ", " +
              std::to_string(outside) + ")");
    CheckRelations(links, "SQLite");

    Check(ReadSchemaTables(c, "main", 2, tables, truncated, failed).success && tables.size() == 2 && truncated,
          "a limit: two tables, and more said to be there");
    Check(!ReadSchemaTables(c, "nowhere", 10, tables, truncated, failed).success || tables.empty(),
          "a schema that is not there gives no table");
    UltraDb_CloseConnection(c);
}

void TestRelationsPostgres() {
    Section("Relations (PostgreSQL)");
    const std::string c = PostgresConnection("dbadmin-rel-pg");
    if (c.empty()) return;
    const std::string schema = "dbadmin_" + ConnectionStore::NewId();
    const std::string other = schema + "_other";
    Check(RunScript(c, "CREATE SCHEMA " + QuotedName(schema) + "; CREATE SCHEMA " + QuotedName(other) + ";"
                       "CREATE TABLE " + QualifiedName(other, "regions") + "(id integer PRIMARY KEY);"
                       "SET search_path TO " + QuotedName(schema) + ";" + kRelationsSchema +
                       "CREATE TABLE lines(order_id integer REFERENCES orders(id), n integer,"
                       " region_id integer REFERENCES " + QualifiedName(other, "regions") + "(id),"
                       " PRIMARY KEY (order_id, n));"
                       "CREATE TABLE shipments(order_id integer, n integer, PRIMARY KEY (order_id, n),"
                       " FOREIGN KEY (order_id, n) REFERENCES lines(order_id, n));"
                       "SET search_path TO DEFAULT;"),
          "the tables made, one key into another schema");
    std::vector<UltraDbTableSchema> tables;
    bool truncated = true;
    std::vector<std::string> failed;
    Check(ReadSchemaTables(c, schema, 100, tables, truncated, failed).success && tables.size() == 7 && !truncated,
          "seven tables, the view left out (" + std::to_string(tables.size()) + ")");
    size_t outside = 0;
    const std::vector<RelationLink> links = RelationLinks(tables, outside);
    Check(outside == 1, "the key into the other schema counted, not drawn");
    CheckRelations(links, "PostgreSQL");
    const RelationLink* line = LinkOf(links, "lines", "orders");
    Check(line && !line->oneToOne, "lines: part of its key refers to orders, one-to-many");
    const RelationLink* shipment = LinkOf(links, "shipments", "lines");
    Check(shipment && shipment->columns.size() == 2 && shipment->oneToOne && !shipment->optional,
          "shipments: a two-column key that is its whole primary key, one-to-one");
    const RelationLink* order = LinkOf(links, "orders", "customers");
    Check(order && order->name == "orders_customer_id_fkey", "a PostgreSQL link is named after its constraint");
    UltraDb_Exec(c, "DROP SCHEMA " + QuotedName(schema) + " CASCADE");
    UltraDb_Exec(c, "DROP SCHEMA " + QuotedName(other) + " CASCADE");
    UltraDb_CloseConnection(c);
}

// ===== Routines =====

void TestRoutinesSqlite() {
    Section("Routines (SQLite)");
    TempDir dir;
    const std::string c = SqliteFile("dbadmin-routines", dir.File("r.db"));
    Check(RunScript(c, "CREATE TABLE items(id INTEGER PRIMARY KEY, label TEXT, changed INTEGER DEFAULT 0);"
                       "CREATE TABLE audit(item INTEGER, what TEXT);"
                       "CREATE TRIGGER items_audit AFTER INSERT ON items BEGIN"
                       " INSERT INTO audit VALUES (new.id, 'added'); END;"),
          "a table with a trigger");
    std::vector<RoutineInfo> routines;
    Check(ListRoutines(c, SqlEngine::Sqlite, "main", routines).success && routines.size() == 1, "one routine listed");
    const RoutineInfo* trigger = RoutineOf(routines, "items_audit");
    Check(trigger && trigger->kind == RoutineInfo::Kind::Trigger && trigger->table == "items" &&
              Has(trigger->definition, "CREATE TRIGGER items_audit"),
          "the trigger, its table and its statement");
    if (trigger) {
        std::string edited = EditableDefinition(SqlEngine::Sqlite, *trigger);
        Check(Has(edited, "BEGIN;") && Has(edited, "DROP TRIGGER \"main\".\"items_audit\";") && Has(edited, "COMMIT;"),
              "the definition to edit: dropped and made again, in one transaction");
        const size_t at = edited.find("'added'");
        if (at != std::string::npos) edited.replace(at, 7, "'new'");
        Check(RunScript(c, edited), "the edited definition runs");
        UltraDb_Exec(c, "INSERT INTO items(label) VALUES ('a')");
        Check(Count(c, "SELECT count(*) FROM audit WHERE what = 'new'") == 1, "the edited trigger is the one that fires");
        Check(ListRoutines(c, SqlEngine::Sqlite, "main", routines).success && routines.size() == 1,
              "still one trigger");
        const OperationPlan drop = DropRoutinePlan(SqlEngine::Sqlite, routines.front());
        Check(drop.statements.size() == 1 && drop.statements[0] == "DROP TRIGGER \"main\".\"items_audit\"",
              "the drop: " + (drop.statements.empty() ? std::string() : drop.statements[0]));
        Check(Run(c, drop) && ListRoutines(c, SqlEngine::Sqlite, "main", routines).success && routines.empty(),
              "dropped");
    }
    const std::string fresh = RoutineTemplate(SqlEngine::Sqlite, RoutineInfo::Kind::Trigger, "main", "items");
    Check(Has(fresh, "ON \"items\"") && RunScript(c, fresh), "a new trigger from its template runs");
    Check(ListRoutines(c, SqlEngine::Sqlite, "main", routines).success && RoutineOf(routines, "trigger_name"),
          "and is listed");
    UltraDb_CloseConnection(c);
}

void TestRoutinesPostgres() {
    Section("Routines (PostgreSQL)");
    const std::string c = PostgresConnection("dbadmin-routines-pg");
    if (c.empty()) return;
    const std::string schema = "dbadmin_" + ConnectionStore::NewId();
    const std::string s = QuotedName(schema);
    Check(RunScript(c, "CREATE SCHEMA " + s + ";"
                       "CREATE TABLE " + s + ".items(id serial PRIMARY KEY, label text, stamp text);"
                       "CREATE FUNCTION " + s + ".twice(n integer) RETURNS integer LANGUAGE sql IMMUTABLE"
                       " AS $$ SELECT n * 2 $$;"
                       "CREATE FUNCTION " + s + ".twice(t text) RETURNS text LANGUAGE sql IMMUTABLE"
                       " AS $$ SELECT t || t $$;"
                       "CREATE FUNCTION " + s + ".stamp() RETURNS trigger LANGUAGE plpgsql"
                       " AS $$ BEGIN NEW.stamp := 'one'; RETURN NEW; END $$;"
                       "CREATE TRIGGER items_stamp BEFORE INSERT ON " + s + ".items"
                       " FOR EACH ROW EXECUTE FUNCTION " + s + ".stamp();"
                       "CREATE PROCEDURE " + s + ".tidy(keep integer) LANGUAGE sql"
                       " AS $$ DELETE FROM " + s + ".items WHERE id > keep $$;"
                       "CREATE AGGREGATE " + s + ".longest(text) (SFUNC = pg_catalog.text_larger, STYPE = text);"),
          "a schema with a trigger, an overloaded function, a procedure and an aggregate");
    std::vector<RoutineInfo> routines;
    Check(ListRoutines(c, SqlEngine::Postgres, schema, routines).success && routines.size() == 6,
          "six routines (" + std::to_string(routines.size()) + ")");
    Check(!routines.empty() && routines.front().kind == RoutineInfo::Kind::Trigger, "the trigger first");
    const RoutineInfo* trigger = RoutineOf(routines, "items_stamp");
    Check(trigger && trigger->table == "items" && Has(trigger->definition, "CREATE TRIGGER items_stamp"),
          "the trigger, its table and its statement");
    const RoutineInfo* number = RoutineOf(routines, "twice", "n integer");
    const RoutineInfo* text = RoutineOf(routines, "twice", "t text");
    Check(number && text && number->kind == RoutineInfo::Kind::Function && number->returns == "integer" &&
              number->language == "sql",
          "both overloads, told apart by their arguments");
    const RoutineInfo* procedure = RoutineOf(routines, "tidy");
    Check(procedure && procedure->kind == RoutineInfo::Kind::Procedure && Has(procedure->definition, "PROCEDURE"),
          "the procedure");
    const RoutineInfo* aggregate = RoutineOf(routines, "longest");
    Check(aggregate && aggregate->kind == RoutineInfo::Kind::Aggregate && aggregate->definition.empty() &&
              EditableDefinition(SqlEngine::Postgres, *aggregate).empty(),
          "the aggregate, with no definition to edit");
    Check(RoutineOf(routines, "stamp") && RoutineOf(routines, "stamp")->returns == "trigger", "the trigger's function");

    if (trigger) {
        std::string edited = EditableDefinition(SqlEngine::Postgres, *trigger);
        Check(edited.rfind("CREATE OR REPLACE TRIGGER items_stamp", 0) == 0, "a trigger is replaced in place");
        const size_t at = edited.find("BEFORE INSERT");
        if (at != std::string::npos) edited.replace(at, 13, "BEFORE INSERT OR UPDATE");
        Check(RunScript(c, edited), "the edited trigger runs");
    }
    if (number) {
        std::string edited = EditableDefinition(SqlEngine::Postgres, *number);
        Check(Has(edited, "CREATE OR REPLACE FUNCTION") && edited.back() == '\n', "a function as PostgreSQL gives it");
        const size_t at = edited.find("n * 2");
        if (at != std::string::npos) edited.replace(at, 5, "n * 3");
        Check(RunScript(c, edited) && Count(c, "SELECT " + s + ".twice(5)") == 15, "the edited function runs");
    }
    if (text) {
        Check(Run(c, DropRoutinePlan(SqlEngine::Postgres, *text)) &&
                  Count(c, "SELECT " + s + ".twice(2)") == 6,
              "one overload dropped, the other kept");
    }
    if (aggregate) Check(Run(c, DropRoutinePlan(SqlEngine::Postgres, *aggregate)), "the aggregate dropped");
    if (procedure) Check(Run(c, DropRoutinePlan(SqlEngine::Postgres, *procedure)), "the procedure dropped");
    const RoutineInfo* stamp = RoutineOf(routines, "stamp");
    if (stamp) {
        const OperationPlan drop = DropRoutinePlan(SqlEngine::Postgres, *stamp);
        Check(!drop.notes.empty() && !Run(c, drop), "a function a trigger calls is not dropped: no CASCADE");
    }
    if (trigger) Check(Run(c, DropRoutinePlan(SqlEngine::Postgres, *trigger)), "the trigger dropped, by its table");
    if (stamp) Check(Run(c, DropRoutinePlan(SqlEngine::Postgres, *stamp)), "then its function");
    Check(ListRoutines(c, SqlEngine::Postgres, schema, routines).success && routines.size() == 1,
          "one routine left");

    Check(RunScript(c, RoutineTemplate(SqlEngine::Postgres, RoutineInfo::Kind::Trigger, schema, "items")),
          "a new trigger from its template runs");
    Check(RunScript(c, RoutineTemplate(SqlEngine::Postgres, RoutineInfo::Kind::Function, schema, "")),
          "a new function from its template runs");
    Check(RunScript(c, RoutineTemplate(SqlEngine::Postgres, RoutineInfo::Kind::Procedure, schema, "")),
          "a new procedure from its template runs");
    Check(ListRoutines(c, SqlEngine::Postgres, schema, routines).success && routines.size() == 5,
          "and they are listed (" + std::to_string(routines.size()) + ")");
    // An extension's functions are the extension's.
    if (RunScript(c, "CREATE EXTENSION IF NOT EXISTS pg_trgm SCHEMA " + s)) {
        Check(ListRoutines(c, SqlEngine::Postgres, schema, routines).success && routines.size() == 5,
              "an extension's functions are not listed");
        UltraDb_Exec(c, "DROP EXTENSION pg_trgm");
    } else {
        std::printf("  [SKIP] pg_trgm not installed: no extension to leave out\n");
    }
    UltraDb_Exec(c, "DROP SCHEMA " + s + " CASCADE");
    UltraDb_CloseConnection(c);
}

// ===== Server =====

void TestServerSqlite() {
    Section("Server (SQLite)");
    Check(ServerViews(SqlEngine::Sqlite) == std::vector<ServerView>{ServerView::FileInfo}, "SQLite has its file");
    Check(ServerViews(SqlEngine::Postgres).size() == 3, "PostgreSQL has sessions, settings and sizes");
    TempDir dir;
    const std::string file = dir.File("info.db");
    const std::string c = SqliteFile("dbadmin-server", file);
    Check(RunScript(c, "PRAGMA journal_mode = wal; PRAGMA user_version = 7;"
                       "CREATE TABLE t(x); INSERT INTO t VALUES (1), (2);"),
          "a file in WAL mode, user_version 7");
    UltraDbResultSet info;
    Check(ReadServerView(c, SqlEngine::Sqlite, ServerView::FileInfo, "main", "", info).success, "read");
    Check(info.Columns().size() == 3 && info.Size() >= 10, "a property per row, its value and what it is");
    Check(Cell(info, "Journal mode", "Value") == "wal", "the journal mode");
    Check(Cell(info, "User version", "Value") == "7", "user_version");
    Check(Cell(info, "File", "Value") == file, "the file");
    Check(Has(Cell(info, "Page size", "Value"), "bytes") && !Cell(info, "Size", "Value").empty(), "page size and size");
    Check(Cell(info, "Encoding", "Value") == "UTF-8" && Cell(info, "Auto-vacuum", "Value") == "none",
          "encoding and auto-vacuum");
    Check(!Cell(info, "SQLite", "Value").empty(), "the library's version");
    Check(!ReadServerView(c, SqlEngine::Sqlite, ServerView::FileInfo, "nowhere", "", info).success,
          "a schema not attached is a failure, not an empty table");
    UltraDb_CloseConnection(c);
}

void TestServerPostgres() {
    Section("Server (PostgreSQL)");
    const std::string c = PostgresConnection("dbadmin-server-pg");
    if (c.empty()) return;
    const std::string schema = "dbadmin_" + ConnectionStore::NewId();
    Check(RunScript(c, "CREATE SCHEMA " + QuotedName(schema) + "; CREATE TABLE " + QualifiedName(schema, "big") +
                       "(id integer PRIMARY KEY, t text); INSERT INTO " + QualifiedName(schema, "big") +
                       " SELECT g, repeat('x', 200) FROM generate_series(1, 2000) g;"),
          "a table with some rows");

    UltraDbResultSet rows;
    Check(ReadServerView(c, SqlEngine::Postgres, ServerView::Sessions, schema, "", rows).success && rows.Size() >= 1,
          "the sessions");
    bool self = false;
    for (const auto& row : rows) self = self || row["this_session"].AsBool();
    Check(self, "this session among them");
    const auto& columns = rows.Columns();
    Check(std::find(columns.begin(), columns.end(), SessionPidColumn()) != columns.end(), "each with its process id");

    Check(ReadServerView(c, SqlEngine::Postgres, ServerView::Settings, schema, "WORK_MEM", rows).success &&
              !Cell(rows, "work_mem", "setting").empty() && !Cell(rows, "maintenance_work_mem", "setting").empty(),
          "the settings filter, any case");
    Check(ReadServerView(c, SqlEngine::Postgres, ServerView::Settings, schema, "work%mem", rows).success &&
              rows.Size() == 0,
          "% typed in the filter is a %");
    Check(ReadServerView(c, SqlEngine::Postgres, ServerView::Settings, schema, "", rows).success && rows.Size() > 100,
          "no filter: every setting");

    Check(ReadServerView(c, SqlEngine::Postgres, ServerView::Sizes, schema, "", rows).success, "the sizes");
    size_t databases = 0;
    bool big = false;
    for (const auto& row : rows) {
        databases += row["kind"].AsString() == "database";
        big = big || (row["name"].AsString() == schema + ".big" && row["bytes"].AsInt64() > 200 * 2000);
    }
    Check(databases >= 1 && big, "the databases, then the schema's table with its size");

    Check(StopSessionSql(42, false) == "SELECT pg_cancel_backend(42)" &&
              StopSessionSql(42, true) == "SELECT pg_terminate_backend(42)",
          "what the confirmation shows");
    // A query that runs until it is cancelled, from a session of its own.
    const std::string sleeper = PostgresConnection("dbadmin-server-sleeper");
    if (!sleeper.empty()) {
        UltraDb_Exec(sleeper, "SET application_name = 'dbadmin-sleeper'");
        std::atomic<bool> done{false};
        UltraDbResult slept;
        const auto start = std::chrono::steady_clock::now();
        std::thread worker([&] {
            UltraDbResultSet out;
            slept = UltraDb_Query(sleeper, "SELECT pg_sleep(30)", {}, out);
            done = true;
        });
        int64_t pid = 0;
        for (int i = 0; i < 100 && pid == 0; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            ReadServerView(c, SqlEngine::Postgres, ServerView::Sessions, schema, "", rows);
            for (const auto& row : rows)
                if (row["application"].AsString() == "dbadmin-sleeper" && row["state"].AsString() == "active")
                    pid = row[SessionPidColumn()].AsInt64();
        }
        Check(pid > 0, "the running query is among the sessions, active");
        bool stopped = false;
        Check(pid > 0 && StopSession(c, pid, false, stopped).success && stopped, "its query cancelled");
        if (pid == 0) UltraDb_Exec(c, "SELECT pg_cancel_backend(pid) FROM pg_stat_activity WHERE application_name = 'dbadmin-sleeper'");
        worker.join();
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        Check(done && !slept.success && seconds < 20, "the query ended early, as cancelled");
        Check(Count(sleeper, "SELECT 1") == 1, "the session itself is still there");
        UltraDb_CloseConnection(sleeper);
    }
    bool stopped = true;
    Check(StopSession(c, 1, true, stopped).success && !stopped, "no such session: nothing stopped");
    UltraDb_Exec(c, "DROP SCHEMA " + QuotedName(schema) + " CASCADE");
    UltraDb_CloseConnection(c);
}

} // namespace

int main() {
    std::printf("UltraDBAdmin schema tests\n\n");
    TestRelationsSqlite();
    TestRelationsPostgres();
    TestRoutinesSqlite();
    TestRoutinesPostgres();
    TestServerSqlite();
    TestServerPostgres();
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

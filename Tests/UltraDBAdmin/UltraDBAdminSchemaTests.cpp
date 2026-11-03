// Tests/UltraDBAdmin/UltraDBAdminSchemaTests.cpp
// UltraDBAdmin's stage 4 engine, without a window: the Relations tab's
// tables and links (a one-to-many, an optional key, a one-to-one, a table
// that refers to itself, a key to a table not read, a limit), the Routines
// tab (triggers on both engines; functions, procedures, an aggregate and an
// overload on PostgreSQL - each listed, its definition run again as edited,
// dropped, and a new one made from its template) and the Server tab
// (SQLite's file; PostgreSQL's sessions with this one among them, a running
// query cancelled from another session, the settings filter with LIKE's
// characters typed, the sizes; the monitor's charts from two readings of a
// live server), and the SQL tab's completion: the catalog read from each
// engine, and what is offered where - tables after FROM, a statement's
// columns after SELECT, an alias's or a schema's after "name.", names quoted
// as the engine needs, nothing in a string or a comment. And data compare:
// two tables matched by key, on one engine and across two, the rows only on
// one side and the columns that differ, the SQL that makes them the same
// and, run, the second table equal to the first.
// SQLite in temporary folders; PostgreSQL where the tests of UltraDatabase
// find one (ULTRAFIBU_TEST_PG_*; REQUIRED makes a missing server a failure).
// Version: 0.2.0 - the monitor; completion; data compare
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminCompare.h"
#include "DbAdminCompletion.h"
#include "DbAdminConnections.h"
#include "DbAdminExport.h"
#include "DbAdminOperations.h"
#include "DbAdminRelations.h"
#include "DbAdminRoutines.h"
#include "DbAdminMonitor.h"
#include "DbAdminServer.h"
#include "DbAdminSqlScript.h"

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

// The MySQL / MariaDB server of Tests/UltraDatabase/test_mysql.h: its socket
// (or host), user and database from ULTRADB_TEST_MYSQL_*. "" when none is
// configured - a failure where ULTRADB_TEST_MYSQL_REQUIRED says one must be.
std::string MysqlConnection(const std::string& name) {
    const char* database = std::getenv("ULTRADB_TEST_MYSQL_DB");
    const bool required = std::getenv("ULTRADB_TEST_MYSQL_REQUIRED") != nullptr;
    if (!database || !*database) {
        Check(!required, "a MySQL server is configured");
        std::printf("  [SKIP] no MySQL configured (ULTRADB_TEST_MYSQL_DB)\n");
        return std::string();
    }
    auto env = [](const char* key, const char* fallback) {
        const char* v = std::getenv(key);
        return std::string(v && *v ? v : fallback);
    };
    UltraDbConnectionConfig cfg;
    cfg.name = name;
    cfg.driver = "mysql";
    cfg.database = database;
    cfg.host = env("ULTRADB_TEST_MYSQL_HOST", "/run/mysqld/mysqld.sock");
    cfg.port = std::atoi(env("ULTRADB_TEST_MYSQL_PORT", "0").c_str());
    cfg.user = env("ULTRADB_TEST_MYSQL_USER", "");
    cfg.tls = UltraDbTls::Disable;       // the local test server
    if (!UltraDb_RegisterConnection(cfg) || !UltraDb_OpenConnection(name)) {
        Check(!required, "the MySQL server is reached");
        std::printf("  [SKIP] MySQL not reached\n");
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

// What stopping a session answers: 1 stopped, 0 not, -1 failed.
int Stop(const std::string& c, int64_t id, bool terminate) {
    bool stopped = false;
    if (!UltraDb_StopSession(c, id, terminate, stopped)) return -1;
    return stopped ? 1 : 0;
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

    // customers above orders, profiles and codes; notes (its key to no
    // table read) and employees (a key to itself) on their own below.
    const auto layers = RelationLayers(tables, links);
    Check(layers.size() == 3, "layers: the parents, their children, the rest (" + std::to_string(layers.size()) + ")");
    if (layers.size() == 3) {
        Check(layers[0] == std::vector<std::string>{"customers"}, "customers on top");
        Check(layers[1].size() == 3 && std::count(layers[1].begin(), layers[1].end(), "orders") == 1,
              "orders, profiles and codes under it");
        Check(layers[2] == std::vector<std::string>{"employees", "notes"},
              "a table linked only to itself or to no table read: the last layer");
    }

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
    Check(RunScript(c, "CREATE SCHEMA " + QuotedName(SqlEngine::Postgres, schema) + "; CREATE SCHEMA " + QuotedName(SqlEngine::Postgres, other) + ";"
                       "CREATE TABLE " + QualifiedName(SqlEngine::Postgres, other, "regions") + "(id integer PRIMARY KEY);"
                       "SET search_path TO " + QuotedName(SqlEngine::Postgres, schema) + ";" + kRelationsSchema +
                       "CREATE TABLE lines(order_id integer REFERENCES orders(id), n integer,"
                       " region_id integer REFERENCES " + QualifiedName(SqlEngine::Postgres, other, "regions") + "(id),"
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
    const auto layers = RelationLayers(tables, links);
    Check(layers.size() == 5 && layers[2] == std::vector<std::string>{"lines"} &&
              layers[3] == std::vector<std::string>{"shipments"} &&
              layers[4] == std::vector<std::string>{"employees"},
          "each table a layer below the one it refers to: customers, orders, lines, shipments; employees last");
    UltraDb_Exec(c, "DROP SCHEMA " + QuotedName(SqlEngine::Postgres, schema) + " CASCADE");
    UltraDb_Exec(c, "DROP SCHEMA " + QuotedName(SqlEngine::Postgres, other) + " CASCADE");
    UltraDb_CloseConnection(c);
}

void TestRelationsMysql() {
    Section("Relations (MySQL / MariaDB)");
    const std::string c = MysqlConnection("dbadmin-rel-my");
    if (c.empty()) return;
    constexpr SqlEngine my = SqlEngine::MySql;
    const std::string schema = "dbadmin_" + ConnectionStore::NewId();
    const std::string other = schema + "_other";
    const std::string s = QuotedName(my, schema);
    // MySQL 8 reads a REFERENCES written on a column and ignores it: each key
    // is a FOREIGN KEY clause of its own.
    Check(RunScript(c, "CREATE DATABASE " + s + "; CREATE DATABASE " + QuotedName(my, other) + ";"
                       "CREATE TABLE " + QualifiedName(my, other, "regions") + "(id int PRIMARY KEY);"
                       "USE " + s + ";"
                       "CREATE TABLE customers(id int PRIMARY KEY, name text NOT NULL);"
                       "CREATE TABLE orders(id int PRIMARY KEY, customer_id int NOT NULL, total double,"
                       " CONSTRAINT orders_customer FOREIGN KEY (customer_id) REFERENCES customers(id));"
                       "CREATE TABLE profiles(customer_id int PRIMARY KEY, bio text,"
                       " FOREIGN KEY (customer_id) REFERENCES customers(id));"
                       "CREATE TABLE employees(id int PRIMARY KEY, manager_id int,"
                       " FOREIGN KEY (manager_id) REFERENCES employees(id));"
                       "CREATE TABLE codes(id int PRIMARY KEY, customer_id int NOT NULL UNIQUE,"
                       " FOREIGN KEY (customer_id) REFERENCES customers(id));"
                       "CREATE VIEW big_orders AS SELECT * FROM orders WHERE total > 100;"
                       "CREATE TABLE `lines`(order_id int, n int, region_id int, PRIMARY KEY (order_id, n),"
                       " FOREIGN KEY (order_id) REFERENCES orders(id),"
                       " FOREIGN KEY (region_id) REFERENCES " + QualifiedName(my, other, "regions") + "(id));"
                       "CREATE TABLE shipments(order_id int, n int, PRIMARY KEY (order_id, n),"
                       " FOREIGN KEY (order_id, n) REFERENCES `lines`(order_id, n));"),
          "the tables made, one key into another database");
    std::vector<UltraDbTableSchema> tables;
    bool truncated = true;
    std::vector<std::string> failed;
    Check(ReadSchemaTables(c, schema, 100, tables, truncated, failed).success && tables.size() == 7 && !truncated,
          "seven tables, the view left out (" + std::to_string(tables.size()) + ")");
    size_t outside = 0;
    const std::vector<RelationLink> links = RelationLinks(tables, outside);
    Check(outside == 1, "the key into the other database counted, not drawn");
    CheckRelations(links, "MySQL");
    const RelationLink* shipment = LinkOf(links, "shipments", "lines");
    Check(shipment && shipment->columns.size() == 2 && shipment->oneToOne,
          "shipments: a two-column key that is its whole primary key, one-to-one");
    const RelationLink* order = LinkOf(links, "orders", "customers");
    Check(order && order->name == "orders_customer", "a MySQL link is named after its constraint");
    UltraDb_Exec(c, "DROP DATABASE " + s);
    UltraDb_Exec(c, "DROP DATABASE " + QuotedName(my, other));
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
    const std::string s = QuotedName(SqlEngine::Postgres, schema);
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

void TestRoutinesMysql() {
    Section("Routines (MySQL / MariaDB)");
    const std::string c = MysqlConnection("dbadmin-routines-my");
    if (c.empty()) return;
    constexpr SqlEngine my = SqlEngine::MySql;
    const std::string schema = "dbadmin_" + ConnectionStore::NewId();
    const std::string s = QuotedName(my, schema);
    Check(RunScript(c, "CREATE DATABASE " + s + ";\n"
                       "CREATE TABLE " + s + ".items(id int AUTO_INCREMENT PRIMARY KEY, label text, stamp text);\n"
                       "CREATE FUNCTION " + s + ".twice(n INT) RETURNS INT DETERMINISTIC RETURN n * 2;\n"
                       "DELIMITER $$\n"
                       "CREATE TRIGGER " + s + ".items_stamp BEFORE INSERT ON " + s + ".items FOR EACH ROW\n"
                       "BEGIN SET NEW.stamp = 'one'; END$$\n"
                       "CREATE PROCEDURE " + s + ".tidy(IN keep INT)\n"
                       "BEGIN DELETE FROM " + s + ".items WHERE id > keep; END$$\n"
                       "DELIMITER ;\n"),
          "a database with a trigger, a function and a procedure");
    std::vector<RoutineInfo> routines;
    Check(ListRoutines(c, my, schema, routines).success && routines.size() == 3,
          "three routines (" + std::to_string(routines.size()) + ")");
    Check(!routines.empty() && routines.front().kind == RoutineInfo::Kind::Trigger, "the trigger first");
    const RoutineInfo* trigger = RoutineOf(routines, "items_stamp");
    Check(trigger && trigger->table == "items" && Has(trigger->definition, "CREATE") &&
              Has(trigger->definition, "items_stamp"),
          "the trigger, its table and its statement");
    const RoutineInfo* number = RoutineOf(routines, "twice");
    Check(number && number->kind == RoutineInfo::Kind::Function && Has(number->arguments, "n") &&
              Has(number->definition, "RETURN n * 2"),
          "the function, its arguments and its statement");
    const RoutineInfo* procedure = RoutineOf(routines, "tidy");
    Check(procedure && procedure->kind == RoutineInfo::Kind::Procedure && Has(procedure->definition, "PROCEDURE"),
          "the procedure");

    if (trigger) {
        std::string edited = EditableDefinition(my, *trigger);
        Check(Has(edited, "DELIMITER $$") && Has(edited, "DROP TRIGGER IF EXISTS") && !Has(edited, "DEFINER="),
              "a trigger to edit: dropped and made again between DELIMITER lines, no DEFINER");
        const size_t at = edited.find("'one'");
        if (at != std::string::npos) edited.replace(at, 5, "'two'");
        Check(SummarizeScript(my, edited).statements == 2, "two statements to the SQL tab");
        Check(RunScript(c, edited), "the edited trigger runs");
        UltraDb_Exec(c, "INSERT INTO " + s + ".items(label) VALUES ('a')");
        Check(Rows(c, "SELECT stamp FROM " + s + ".items").Row(0)[0].AsString() == "two",
              "the edited trigger is the one that fires");
    }
    if (number) {
        std::string edited = EditableDefinition(my, *number);
        const size_t at = edited.find("n * 2");
        if (at != std::string::npos) edited.replace(at, 5, "n * 3");
        Check(RunScript(c, edited) && Count(c, "SELECT " + s + ".twice(5)") == 15, "the edited function runs");
        const OperationPlan drop = DropRoutinePlan(my, *number);
        Check(drop.statements.size() == 1 && drop.statements[0] == "DROP FUNCTION " + s + ".`twice`" && Run(c, drop),
              "the function dropped, by its name alone: " + (drop.statements.empty() ? std::string() : drop.statements[0]));
    }
    if (procedure) Check(Run(c, DropRoutinePlan(my, *procedure)), "the procedure dropped");
    if (trigger) Check(Run(c, DropRoutinePlan(my, *trigger)), "the trigger dropped");
    Check(ListRoutines(c, my, schema, routines).success && routines.empty(), "none left");

    Check(RunScript(c, RoutineTemplate(my, RoutineInfo::Kind::Trigger, schema, "items")),
          "a new trigger from its template runs");
    Check(RunScript(c, RoutineTemplate(my, RoutineInfo::Kind::Function, schema, "")),
          "a new function from its template runs");
    Check(RunScript(c, RoutineTemplate(my, RoutineInfo::Kind::Procedure, schema, "")),
          "a new procedure from its template runs");
    Check(ListRoutines(c, my, schema, routines).success && routines.size() == 3,
          "and they are listed (" + std::to_string(routines.size()) + ")");
    UltraDb_Exec(c, "DROP DATABASE " + s);
    UltraDb_CloseConnection(c);
}

// ===== Server =====

// The monitor over a live server: its charts from two readings with rows
// written between them - the rows written per second above zero. `table`
// (qualified) has an integer id and a text t; `traffic` for MySQL, which
// counts its clients' bytes.
void CheckMonitor(const std::string& c, SqlEngine engine, const std::string& table, bool traffic) {
    UltraDbResultSet status;
    Check(ReadServerView(c, engine, ServerView::Monitor, "", "", status).success && status.Size() >= 7,
          "the server's status");
    ServerMonitor monitor;
    const auto start = std::chrono::steady_clock::now();
    monitor.Add(0.0, status);
    Check(UltraDb_Exec(c, "INSERT INTO " + table + " (id, t) VALUES (900001, 'a'), (900002, 'b'), (900003, 'c')") &&
              UltraDb_Exec(c, "DELETE FROM " + table + " WHERE id > 900000"),
          "rows written between the readings");
    // PostgreSQL's statistics arrive within a second or so.
    double written = 0;
    for (int i = 0; i < 30 && written <= 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ReadServerView(c, engine, ServerView::Monitor, "", "", status);
        ServerMonitor again = monitor;
        again.Add(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(), status);
        for (const auto& chart : again.Charts())
            if (chart.key == "rows_written" && !chart.points.empty()) written = chart.points.back().value;
        if (written > 0) monitor = again;
    }
    Check(written > 0, "rows written per second, above zero");
    std::vector<std::string> keys;
    for (const auto& chart : monitor.Charts()) keys.push_back(chart.key);
    const bool hasTraffic = std::find(keys.begin(), keys.end(), "received") != keys.end();
    Check(keys.size() >= 6 && keys.front() == "sessions" && hasTraffic == traffic,
          "a chart per measure the server keeps (" + std::to_string(keys.size()) + ")");
    for (const auto& chart : monitor.Charts())
        if (chart.key == "cache_hits")
            Check(!chart.points.empty() && chart.points.back().value >= 0 && chart.points.back().value <= 100,
                  "the cache's hit ratio, a percentage");
}

void TestServerSqlite() {
    Section("Server (SQLite)");
    Check(ServerViews(SqlEngine::Sqlite) == std::vector<ServerView>{ServerView::FileInfo}, "SQLite has its file");
    Check(ServerViews(SqlEngine::Postgres).size() == 4, "PostgreSQL has sessions, a monitor, settings and sizes");
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
    Check(RunScript(c, "CREATE SCHEMA " + QuotedName(SqlEngine::Postgres, schema) + "; CREATE TABLE " + QualifiedName(SqlEngine::Postgres, schema, "big") +
                       "(id integer PRIMARY KEY, t text); INSERT INTO " + QualifiedName(SqlEngine::Postgres, schema, "big") +
                       " SELECT g, repeat('x', 200) FROM generate_series(1, 2000) g;"),
          "a table with some rows");

    UltraDbResultSet rows;
    Check(ReadServerView(c, SqlEngine::Postgres, ServerView::Sessions, schema, "", rows).success && rows.Size() >= 1,
          "the sessions");
    bool self = false;
    for (const auto& row : rows) self = self || row["this_session"].AsBool();
    Check(self, "this session among them");
    const auto& columns = rows.Columns();
    Check(std::find(columns.begin(), columns.end(), SessionIdColumn()) != columns.end(), "each with its id");

    Check(ReadServerView(c, SqlEngine::Postgres, ServerView::Settings, schema, "WORK_MEM", rows).success &&
              !Cell(rows, "work_mem", "value").empty() && !Cell(rows, "maintenance_work_mem", "value").empty(),
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

    std::string cancel, terminate;
    Check(UltraDb_StopSessionSql(c, 42, false, cancel) && cancel == "SELECT pg_cancel_backend(42)" &&
              UltraDb_StopSessionSql(c, 42, true, terminate) && terminate == "SELECT pg_terminate_backend(42)",
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
                    pid = row[SessionIdColumn()].AsInt64();
        }
        Check(pid > 0, "the running query is among the sessions, active");
        Check(pid > 0 && Stop(c, pid, false) == 1, "its query cancelled");
        if (pid == 0) UltraDb_Exec(c, "SELECT pg_cancel_backend(pid) FROM pg_stat_activity WHERE application_name = 'dbadmin-sleeper'");
        worker.join();
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        Check(done && !slept.success && seconds < 20, "the query ended early, as cancelled");
        Check(Count(sleeper, "SELECT 1") == 1, "the session itself is still there");
        UltraDb_CloseConnection(sleeper);
    }
    Check(Stop(c, 1, true) == 0, "no such session: nothing stopped");
    CheckMonitor(c, SqlEngine::Postgres, QualifiedName(SqlEngine::Postgres, schema, "big"), false);
    UltraDb_Exec(c, "DROP SCHEMA " + QuotedName(SqlEngine::Postgres, schema) + " CASCADE");
    UltraDb_CloseConnection(c);
}

} // namespace

void TestServerMysql() {
    Section("Server (MySQL / MariaDB)");
    const std::string c = MysqlConnection("dbadmin-server-my");
    if (c.empty()) return;
    constexpr SqlEngine my = SqlEngine::MySql;
    Check(ServerViews(my) == std::vector<ServerView>{ServerView::Sessions, ServerView::Monitor, ServerView::Settings,
                                                     ServerView::Sizes},
          "a MySQL server's views");
    const std::string schema = "dbadmin_" + ConnectionStore::NewId();
    const std::string s = QuotedName(my, schema);
    Check(RunScript(c, "CREATE DATABASE " + s + "; CREATE TABLE " + s + ".big(id int PRIMARY KEY, t text) ENGINE=InnoDB;"
                       "INSERT INTO " + s + ".big VALUES (1, REPEAT('x', 200));"),
          "a table");

    UltraDbResultSet rows;
    Check(ReadServerView(c, my, ServerView::Sessions, schema, "", rows).success && rows.Size() >= 1, "the sessions");
    bool self = false;
    for (const auto& row : rows) self = self || row["this_session"].AsBool();
    Check(self, "this session among them");
    Check(ReadServerView(c, my, ServerView::Settings, schema, "MAX_CONN", rows).success &&
              !Cell(rows, "max_connections", "value").empty(),
          "the settings filter, any case");
    Check(ReadServerView(c, my, ServerView::Settings, schema, "max%conn", rows).success && rows.Size() == 0,
          "% typed in the filter is a %");
    Check(ReadServerView(c, my, ServerView::Sizes, schema, "", rows).success, "the sizes");
    bool database = false, big = false;
    for (const auto& row : rows) {
        database = database || row["kind"].AsString() == "database";
        big = big || row["name"].AsString() == schema + ".big";
    }
    Check(database && big, "the databases, then the database's table");
    std::string sql;
    Check(UltraDb_StopSessionSql(c, 42, false, sql) && sql == "KILL QUERY 42" &&
              UltraDb_StopSessionSql(c, 42, true, sql) && sql == "KILL 42",
          "what the confirmation shows");

    // A query that runs until it is stopped, from a session of its own.
    const std::string sleeper = MysqlConnection("dbadmin-server-my-sleeper");
    if (!sleeper.empty()) {
        std::atomic<bool> done{false};
        UltraDbResult slept;
        const auto start = std::chrono::steady_clock::now();
        std::thread worker([&] {
            UltraDbResultSet out;
            slept = UltraDb_Query(sleeper, "SELECT SLEEP(30) AS dbadmin_sleeper", {}, out);
            done = true;
        });
        int64_t id = 0;
        for (int i = 0; i < 100 && id == 0; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            ReadServerView(c, my, ServerView::Sessions, schema, "", rows);
            for (const auto& row : rows)
                if (Has(row["query"].AsString(), "dbadmin_sleeper") && row["state"].AsString() == "active" &&
                    !row["this_session"].AsBool())
                    id = row[SessionIdColumn()].AsInt64();
        }
        Check(id > 0, "the running query is among the sessions, active");
        Check(id > 0 && Stop(c, id, false) == 1, "its query stopped");
        worker.join();
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        Check(done && seconds < 20, "the query ended early, as stopped");
        Check(Count(sleeper, "SELECT 1") == 1, "the session itself is still there");
        UltraDb_CloseConnection(sleeper);
    }
    Check(Stop(c, 2000000000, true) == 0, "no such session: nothing stopped");
    CheckMonitor(c, my, s + ".big", true);
    UltraDb_Exec(c, "DROP DATABASE " + s);
    UltraDb_CloseConnection(c);
}

// ===== Completion =====

bool Offers(const std::vector<std::string>& list, const std::string& item) {
    return std::find(list.begin(), list.end(), item) != list.end();
}

// What is offered at the '|' of `script` (the bar taken out).
std::vector<std::string> At(SqlEngine engine, const std::string& script, const CompletionCatalog& catalog) {
    const size_t bar = script.find('|');
    std::string text = script;
    text.erase(bar, 1);
    return CompletionsAt(engine, text, bar, catalog);
}

void TestCompletionSqlite() {
    Section("Completion (SQLite)");
    TempDir dir;
    const std::string c = SqliteFile("dbadmin-complete", dir.File("complete.db"));
    Check(RunScript(c, "CREATE TABLE orders(id INTEGER PRIMARY KEY, customer_id INTEGER, total REAL);"
                       "CREATE TABLE customers(id INTEGER PRIMARY KEY, name TEXT, \"Order Note\" TEXT);"
                       "CREATE VIEW big_orders AS SELECT * FROM orders WHERE total > 100;"),
          "two tables and a view");
    CompletionCatalog catalog;
    Check(LoadCompletionCatalog(c, SqlEngine::Sqlite, "main", catalog).success, "the catalog");
    Check(catalog.schemas.count("main") && catalog.schemas["main"].size() == 3 &&
              catalog.schemas["main"]["customers"] == std::vector<std::string>({ "id", "name", "Order Note" }),
          "its tables, a view and their columns in order");
    const SqlEngine e = SqlEngine::Sqlite;
    auto list = At(e, "SELECT * FROM or|", catalog);
    Check(!list.empty() && list.front() == "orders", "after FROM: the table that begins so, first");
    Check(Offers(At(e, "SELECT * FROM |", catalog), "big_orders"), "a view among the tables");
    list = At(e, "SELECT cu| FROM orders", catalog);
    Check(Offers(list, "customer_id") && !Offers(list, "name"), "after SELECT: the columns of the table named after it");
    list = At(e, "SELECT o.| FROM orders o JOIN customers c ON c.id = o.customer_id", catalog);
    Check(Offers(list, "total") && !Offers(list, "name"), "an alias's columns");
    list = At(e, "SELECT c.| FROM orders o JOIN customers c ON c.id = o.customer_id", catalog);
    Check(Offers(list, "\"Order Note\"") && Offers(list, "name"), "a name with a space, quoted");
    list = At(e, "SELECT * FROM orders WHERE to|", catalog);
    Check(!list.empty() && list.front() == "total", "after WHERE: the columns");
    list = At(e, "sel|", catalog);
    Check(Offers(list, "select") && !Offers(list, "SELECT"), "a keyword in the case it is typed in");
    list = At(e, "SELECT * FROM orders WHERE cu|", catalog);
    Check(Offers(list, "customer_id") && !Offers(list, "vacuum") && !Offers(list, "VACUUM"),
          "a keyword only for the letters it begins with");
    list = At(e, "SELECT * FROM main.|", catalog);
    Check(Offers(list, "orders") && Offers(list, "customers"), "after schema. its tables");
    Check(At(e, "SELECT 'or|", catalog).empty(), "nothing inside a string");
    Check(At(e, "SELECT 1 -- or|", catalog).empty(), "nothing inside a comment");
    list = At(e, "SELECT 1; SELECT * FROM customers WHERE na|", catalog);
    Check(!list.empty() && list.front() == "name", "the statement the caret is in, not the one before");
    Check(CompletionPrefix("SELECT nam", 10) == "nam", "the word a suggestion replaces");
    UltraDb_CloseConnection(c);
}

void TestCompletionServer(const char* label, SqlEngine engine, const std::string& c, const std::string& schema) {
    Section(label);
    const std::string s = QuotedName(engine, schema);
    Check(RunScript(c, std::string(engine == SqlEngine::MySql ? "CREATE DATABASE " : "CREATE SCHEMA ") + s + ";"
                       "CREATE TABLE " + s + ".invoices(id integer PRIMARY KEY, amount numeric(10,2));"
                       "CREATE TABLE " + s + "." + QuotedName(engine, "MixedCase") + "(" + QuotedName(engine, "Id") +
                       " integer);"),
          "a schema with two tables");
    CompletionCatalog catalog;
    Check(LoadCompletionCatalog(c, engine, schema, catalog).success, "the catalog");
    Check(catalog.schemas.count(schema) && catalog.schemas[schema].count("invoices") &&
              catalog.schemas[schema]["invoices"] == std::vector<std::string>({ "id", "amount" }),
          "the schema's tables and columns");
    auto list = At(engine, "SELECT * FROM inv|", catalog);
    Check(!list.empty() && list.front() == "invoices", "after FROM, the default schema's table");
    list = At(engine, "SELECT * FROM |", catalog);
    Check(Offers(list, QuotedName(engine, "MixedCase")) == (engine == SqlEngine::Postgres) &&
              Offers(list, "MixedCase") == (engine != SqlEngine::Postgres),
          "an upper-case name quoted where the engine folds it");
    list = At(engine, "SELECT am| FROM invoices", catalog);
    Check(Offers(list, "amount"), "a column after SELECT");
    UltraDb_Exec(c, std::string(engine == SqlEngine::MySql ? "DROP DATABASE " : "DROP SCHEMA ") + s +
                        (engine == SqlEngine::MySql ? "" : " CASCADE"));
}

void TestCompletionPostgres() {
    const std::string c = PostgresConnection("dbadmin-complete-pg");
    if (c.empty()) return;
    TestCompletionServer("Completion (PostgreSQL)", SqlEngine::Postgres, c, "dbadmin_" + ConnectionStore::NewId());
    UltraDb_CloseConnection(c);
}

void TestCompletionMysql() {
    const std::string c = MysqlConnection("dbadmin-complete-my");
    if (c.empty()) return;
    TestCompletionServer("Completion (MySQL / MariaDB)", SqlEngine::MySql, c, "dbadmin_" + ConnectionStore::NewId());
    UltraDb_CloseConnection(c);
}

// ===== Data compare =====

// Rows of `table` on `c`, as one text: to see two tables hold the same.
const RowDifference* FindDifference(const CompareResult& r, RowDifference::Kind kind, const std::string& id) {
    for (const auto& d : r.differences) {
        const auto& values = kind == RowDifference::Kind::OnlyRight ? d.right : d.left;
        if (d.kind == kind && !values.empty() && CompareText(values[0]) == id) return &d;
    }
    return nullptr;
}

// `left` and `right` hold the same five products but for: 4 only left, 5
// only right, 2's price and 3's name changed; right has a column of its own.
void CheckCompare(const char* label, const CompareSide& left, const CompareSide& right) {
    CompareResult r;
    const UltraDbResult compared = CompareTables(left, right, CompareLimits(), r);
    Check(compared.success, std::string(label) + ": compared (" + compared.message + ")");
    Check(r.key == std::vector<std::string>({ "id" }) && r.columns.size() == 4, "matched by the key, four columns");
    Check(r.onlyRightColumns == std::vector<std::string>({ "note" }), "the right's own column, not compared");
    Check(r.leftRows == 4 && r.rightRows == 4, "four rows each side");
    Check(r.same == 1 && r.onlyLeft == 1 && r.onlyRight == 1 && r.changed == 2,
          "one the same, one only left, one only right, two changed");
    const RowDifference* price = FindDifference(r, RowDifference::Kind::Changed, "2");
    Check(price && price->changed.size() == 1 && r.columns[price->changed[0]] == "price",
          "2: its price alone differs (10.50 on one side is 10.5 on the other)");
    Check(FindDifference(r, RowDifference::Kind::OnlyLeft, "4") && FindDifference(r, RowDifference::Kind::OnlyRight, "5"),
          "4 only left, 5 only right");
    const UltraDbResultSet rows = DifferenceRows(r);
    bool arrow = false;
    for (const auto& row : rows) arrow = arrow || row["price"].AsString().rfind("10.5 -> 11", 0) == 0;
    Check(rows.Size() == 4 && rows.Columns().front() == "difference" && arrow,
          "as the grid shows them: a changed value as here -> there");
    Check(CompareSummary(r, "in a", "in b").find("1 the same, 2 changed, 1 only in a, 1 only in b") != std::string::npos,
          "the summary");
    OperationPlan plan;
    std::string error;
    Check(SyncPlan(right, r, plan, error) && plan.statements.size() == 4 && plan.transaction,
          "the SQL: an insert, two updates, a delete, as one transaction");
    size_t failed = 0;
    const UltraDbResult ran = RunPlan(right.connection, plan, nullptr, failed);
    Check(ran.success, "run on the right (" + ran.message + ")");
    CompareResult after;
    Check(CompareTables(left, right, CompareLimits(), after).success && after.Differences() == 0 && after.same == 4,
          "and then the two hold the same");
}

const char* kCompareLeft =
    "CREATE TABLE products(id integer PRIMARY KEY, name varchar(40), price numeric(10,2), active boolean);"
    "INSERT INTO products VALUES (1, 'apple', 1.25, TRUE), (2, 'pear', 10.50, TRUE), (3, 'plum', 3.00, FALSE),"
    " (4, 'fig', 7.00, TRUE);";
const char* kCompareRight =
    "CREATE TABLE products(id integer PRIMARY KEY, name varchar(40), price numeric(10,2), active boolean, note text);"
    "INSERT INTO products VALUES (1, 'apple', 1.25, TRUE, 'x'), (2, 'pear', 11.00, TRUE, NULL),"
    " (3, 'plum!', 3.0, FALSE, NULL), (5, 'kiwi', 2.00, TRUE, NULL);";

void TestCompareSqlite() {
    Section("Data compare (SQLite)");
    TempDir dir;
    const std::string a = SqliteFile("dbadmin-compare-a", dir.File("a.db"));
    const std::string b = SqliteFile("dbadmin-compare-b", dir.File("b.db"));
    Check(RunScript(a, kCompareLeft) && RunScript(b, kCompareRight), "two files");
    CheckCompare("SQLite", { a, SqlEngine::Sqlite, "main", "products" }, { b, SqlEngine::Sqlite, "main", "products" });
    CompareResult r;
    Check(RunScript(a, "CREATE TABLE nokey(x integer);") &&
              CompareTables({ a, SqlEngine::Sqlite, "main", "nokey" }, { b, SqlEngine::Sqlite, "main", "products" },
                            CompareLimits(), r).code == UltraDbResultCode::InvalidArgument,
          "a table without a key is refused");
    CompareLimits small;
    small.maxRows = 2;
    Check(!CompareTables({ a, SqlEngine::Sqlite, "main", "products" }, { b, SqlEngine::Sqlite, "main", "products" },
                         small, r).success,
          "a table over the limit is refused");
    Check(CompareText(UltraDbValue("10.50")) == CompareText(UltraDbValue(10.5)) &&
              CompareText(UltraDbValue("007")) != CompareText(UltraDbValue(int64_t(7))) &&
              CompareText(UltraDbValue(true)) == CompareText(UltraDbValue(int64_t(1))) &&
              CompareText(UltraDbValue()) != CompareText(UltraDbValue("")),
          "values read alike across engines, a code with leading zeros and NULL kept apart");
    UltraDb_CloseConnection(a);
    UltraDb_CloseConnection(b);
}

void TestCompareServer(const char* label, SqlEngine engine, const std::string& c) {
    Section(label);
    const std::string schema = "dbadmin_" + ConnectionStore::NewId();
    const std::string s = QuotedName(engine, schema);
    const bool mysql = engine == SqlEngine::MySql;
    Check(RunScript(c, std::string(mysql ? "CREATE DATABASE " : "CREATE SCHEMA ") + s + ";"), "a schema");
    std::string script = kCompareRight;
    // Into the new schema; MySQL's boolean is a tinyint, and TRUE / FALSE are 1 / 0 there.
    for (size_t at; (at = script.find("products")) != std::string::npos;) script.replace(at, 8, s + ".prod_r");
    Check(RunScript(c, script), "the right table on the server");
    TempDir dir;
    const std::string a = SqliteFile("dbadmin-compare-x", dir.File("x.db"));
    Check(RunScript(a, kCompareLeft), "the left table in a SQLite file");
    CheckCompare(label, { a, SqlEngine::Sqlite, "main", "products" }, { c, engine, schema, "prod_r" });
    UltraDb_Exec(c, std::string(mysql ? "DROP DATABASE " : "DROP SCHEMA ") + s + (mysql ? "" : " CASCADE"));
    UltraDb_CloseConnection(a);
}

void TestComparePostgres() {
    const std::string c = PostgresConnection("dbadmin-compare-pg");
    if (c.empty()) return;
    TestCompareServer("Data compare (SQLite against PostgreSQL)", SqlEngine::Postgres, c);
    UltraDb_CloseConnection(c);
}

void TestCompareMysql() {
    const std::string c = MysqlConnection("dbadmin-compare-my");
    if (c.empty()) return;
    TestCompareServer("Data compare (SQLite against MySQL / MariaDB)", SqlEngine::MySql, c);
    UltraDb_CloseConnection(c);
}

int main() {
    std::printf("UltraDBAdmin schema tests\n\n");
    TestRelationsSqlite();
    TestRelationsPostgres();
    TestRelationsMysql();
    TestRoutinesSqlite();
    TestRoutinesPostgres();
    TestRoutinesMysql();
    TestServerSqlite();
    TestServerPostgres();
    TestServerMysql();
    TestCompletionSqlite();
    TestCompletionPostgres();
    TestCompletionMysql();
    TestCompareSqlite();
    TestComparePostgres();
    TestCompareMysql();
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

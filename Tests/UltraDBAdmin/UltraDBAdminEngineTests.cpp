// Tests/UltraDBAdmin/UltraDBAdminEngineTests.cpp
// UltraDBAdmin's engine without a window: the connection store (its file
// never holds a password, its vault does), cell formatting, the Browse pager
// (pages, sorting, quoting), the SQL runner (a script on a worker thread,
// Stop, history), the job queue the window's queries run on, the staged
// row edits of Browse (the SQL they become, one transaction, all or nothing),
// and the staged structure edits of Structure: the CREATE TABLE parser, the
// DDL per engine, SQLite's table rebuild keeping rows, CHECKs, indexes,
// triggers, foreign keys and AUTOINCREMENT. SQLite in temporary folders; the
// structure edits also on PostgreSQL where the tests of UltraDatabase find
// one (ULTRAFIBU_TEST_PG_*, as CI sets them; REQUIRED makes a missing server
// a failure, not a skip). And importing: a SQL file read and summarised and
// run as one transaction, rows from a CSV file mapped onto a table and
// inserted all or none. And exporting: CSV, TSV, JSON and SQL written row by
// row through a cursor, read back exactly, a SQL file that rebuilds the table
// on SQLite and refills one on PostgreSQL, and a stopped export leaving the
// file that was there. And Browse's filters (bound values, LIKE escaped,
// ILIKE on PostgreSQL), a blob staged and written, rows copied as CSV, JSON
// and INSERTs, a blob's kind and hex dump, the table a SELECT reads.
// Version: 0.2.0 - filters, blobs, rows as text
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminBlob.h"
#include "DbAdminConnections.h"
#include "DbAdminEditBuffer.h"
#include "DbAdminExport.h"
#include "DbAdminFormat.h"
#include "DbAdminImport.h"
#include "DbAdminJobQueue.h"
#include "DbAdminSqlRunner.h"
#include "DbAdminSqlScript.h"
#include "DbAdminSqliteTable.h"
#include "DbAdminStructureEdit.h"
#include "DbAdminTablePager.h"

#include "UltraCanvasPathUtf8.h"
#include <UltraDatabase/UltraDatabase.h>
#include <UltraVault/UltraVault.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
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

// A temporary folder of the test's own, removed afterwards.
struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("ultradbadmin-test-" + ConnectionStore::NewId());
        fs::create_directories(path);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    std::string Utf8() const { return UltraCanvas::PathToUtf8(path); }
};

UltraDbResultSet Rows(const std::string& c, const std::string& sql);
bool Has(const std::string& text, const std::string& part);

std::string ReadFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// A SQLite file registered as a connection, with `rows` rows in items.
std::string MakeSqlite(ConnectionStore& store, const TempDir& dir, int rows) {
    ConnectionSpec spec;
    spec.name = "test";
    spec.driver = "sqlite";
    spec.database = UltraCanvas::PathToUtf8(dir.path / "test.db");
    std::string error;
    const std::string id = store.Put(spec, "", false, error);
    const ConnectionSpec* saved = store.Find(id);
    store.Register(*saved);
    const std::string c = RegisteredName(*saved);
    UltraDb_Exec(c, "CREATE TABLE items(id INTEGER PRIMARY KEY, label TEXT, weight REAL)");
    UltraDbHandle tx = UltraDb_Begin(c);
    for (int i = 1; i <= rows; ++i)
        UltraDb_ExecInTx(tx, "INSERT INTO items(id, label, weight) VALUES(?, ?, ?)",
                         { i, "item " + std::to_string(i), (rows - i) * 0.5 });
    UltraDb_Commit(tx);
    return c;
}

void TestConnectionStoreInMemory() {
    Section("Connections, in memory");
    ConnectionStore store;
    store.OpenInMemory();
    ConnectionSpec spec;
    spec.name = "Notes";
    spec.database = "/tmp/notes.db";
    std::string error;
    const std::string id = store.Put(spec, "", false, error);
    Check(id.size() == 17 && id[0] == 'c', "a new connection gets a generated id (" + id + ")");
    Check(store.All().size() == 1, "and is in the list");
    const ConnectionSpec* found = store.Find(id);
    Check(found && found->name == "Notes", "and can be found by its id");
    Check(Describe(*found) == "notes.db", "a SQLite connection is described by its file");

    ConnectionSpec pg;
    pg.name = "CRM";
    pg.driver = "postgresql";
    pg.host = "db.example.com";
    pg.port = 5432;
    pg.user = "alice";
    pg.database = "crm";
    Check(Describe(pg) == "alice@db.example.com:5432/crm", "a server connection by user@host:port/db");

    spec.id = id;
    spec.name = "Notes (renamed)";
    Check(store.Put(spec, "", false, error) == id, "putting the same id replaces");
    Check(store.All().size() == 1 && store.Find(id)->name == "Notes (renamed)", "rather than adding");
    store.Register(*store.Find(id));
    const ConnectionSpec& registered = *store.Find(id);
    Check(ConsoleName(registered) != RegisteredName(registered), "the console has a session of its own");
    Check(UltraDb_HasConnection(RegisteredName(registered)) && UltraDb_HasConnection(ConsoleName(registered)),
          "Register registers both sessions");
    const std::string browse = RegisteredName(registered);
    const std::string console = ConsoleName(registered);
    Check(store.Remove(id, error) && store.All().empty(), "and Remove takes it out");
    Check(!UltraDb_HasConnection(browse) && !UltraDb_HasConnection(console), "and closes both sessions");
    Check(ConnectionStore::NewId() != ConnectionStore::NewId(), "ids do not repeat");
}

void TestConnectionStoreOnDisk() {
    Section("Connections, on disk with a vault");
    TempDir dir;
    std::string error;
    {
        ConnectionStore store;
        if (!store.Open(dir.Utf8(), error)) {
            // Without libsodium UltraCrypt has no cipher and the vault stays shut.
            std::printf("  [SKIP] %s\n", error.c_str());
            return;
        }
        ConnectionSpec spec;
        spec.name = "Server";
        spec.driver = "postgresql";
        spec.host = "db.internal";
        spec.database = "accounts";
        spec.user = "bob";
        spec.production = true;
        spec.tls = UltraDbTls::Require;
        const std::string id = store.Put(spec, "s3cret-Pa55word", false, error);
        Check(!id.empty(), "a connection with a password is stored (" + error + ")");
        Check(store.Find(id)->hasPassword, "and knows it has one");
        UltraVault::SecretValue secret;
        Check(UltraVault::Get("dbadmin.connection." + id, secret).IsOk() &&
              std::string(secret.bytes.begin(), secret.bytes.end()) == "s3cret-Pa55word",
              "the password is in the vault under the connection's key");
    }
    const std::string json = ReadFile(dir.path / "connections.json");
    Check(json.find("Server") != std::string::npos, "the list is written to connections.json");
    Check(json.find("s3cret") == std::string::npos, "and the password is not in it");
    Check(!fs::exists(dir.path / "connections.json.tmp"), "no half-written file is left behind");

    ConnectionStore again;
    Check(again.Open(dir.Utf8(), error), "the store opens again");
    Check(again.All().size() == 1, "with the saved connection");
    const ConnectionSpec& back = again.All().front();
    Check(back.name == "Server" && back.host == "db.internal" && back.user == "bob" &&
          back.hasPassword && back.production && back.tls == UltraDbTls::Require,
          "every field read back as written");
}

void TestCellText() {
    Section("Cell text");
    Check(CellText(UltraDbValue()) == "NULL", "NULL reads NULL");
    Check(CellText(UltraDbValue("")) == "", "an empty string stays empty, not NULL");
    Check(CellText(UltraDbValue(true)) == "true", "a bool reads true / false");
    Check(CellText(UltraDbValue(int64_t(42))) == "42", "an integer as is");
    Check(CellText(UltraDbValue(2.5)) == "2.5", "a double dot-decimal, shortest form");
    Check(CellText(UltraDbValue(std::vector<uint8_t>(1536, 0))) == "<blob, 1.5 KB>", "a blob by its size");
    Check(CellText(UltraDbValue("first line\nsecond")) == "first line…", "text up to its first line break");
    Check(CellText(UltraDbValue("abcdef"), 3) == "abc…", "and at most maxChars characters");
    Check(CellText(UltraDbValue("ÄÖÜß"), 2) == "ÄÖ…", "counted in characters, not bytes");
    Check(HumanSize(0) == "0 B" && HumanSize(1023) == "1023 B", "sizes under a KB in bytes");
}

void TestPager() {
    Section("Browse pager");
    TempDir dir;
    ConnectionStore store;
    store.OpenInMemory();
    const std::string c = MakeSqlite(store, dir, 450);
    UltraDbTableSchema table;
    Check(UltraDb_DescribeTable(c, "main", "items", table).success, "the table is described");
    TablePager pager(c, table, 200);
    Check(pager.HasStableOrder(), "a table with a key pages in a stable order");

    int64_t count = 0;
    Check(pager.CountRows(count).success && count == 450, "it counts the rows");
    UltraDbResultSet page;
    Check(pager.LoadPage(0, page).success && page.Size() == 200, "page 0 holds 200 rows");
    Check(page.Row(0)["id"].AsInt64() == 1 && page.Row(199)["id"].AsInt64() == 200, "the first 200, by key");
    Check(pager.LoadPage(2, page).success && page.Size() == 50, "the last page holds the rest");
    Check(page.Row(0)["id"].AsInt64() == 401, "starting at 401");
    Check(pager.LoadPage(3, page).success && page.Size() == 0, "a page past the end is empty");

    pager.SetSort("weight", true);
    Check(pager.LoadPage(0, page).success && page.Row(0)["id"].AsInt64() == 450,
          "sorted by a column, ascending");
    pager.SetSort("label", false);
    std::string sql;
    pager.PageSql(sql);
    Check(sql == "SELECT * FROM \"main\".\"items\" ORDER BY \"label\" DESC, \"id\" ASC LIMIT ? OFFSET ?",
          "the page query quotes every name and breaks ties by the key: " + sql);

    // A name that needs quoting, and a table without a key.
    UltraDb_Exec(c, "CREATE TABLE \"odd \"\"name\"(v TEXT)");
    UltraDb_Exec(c, "INSERT INTO \"odd \"\"name\" VALUES('x')");
    UltraDbTableSchema odd;
    Check(UltraDb_DescribeTable(c, "main", "odd \"name", odd).success, "an odd name is described");
    TablePager oddPager(c, odd, 10);
    Check(!oddPager.HasStableOrder(), "a table without a key says its order is not stable");
    Check(oddPager.LoadPage(0, page).success && page.Size() == 1, "and still pages");
    UltraDb_CloseConnection(c);
}

// A PostgreSQL connection registered and open under `name`, or "" when none
// is configured or reached (a failure instead when one is required).
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

// A MySQL / MariaDB connection, as PostgresConnection: the server of
// Tests/UltraDatabase/test_mysql.h, from ULTRADB_TEST_MYSQL_*.
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

int64_t Counted(const TablePager& pager) {
    int64_t n = -1;
    if (!pager.CountRows(n).success) return -1;
    return n;
}

void TestPagerFilters() {
    Section("Browse filters (SQLite)");
    TempDir dir;
    ConnectionStore store;
    store.OpenInMemory();
    const std::string c = MakeSqlite(store, dir, 450);
    UltraDb_Exec(c, "INSERT INTO items(id, label, weight) VALUES (451, NULL, 1), (452, '50% off', 2), "
                    "(453, '50 off', 3), (454, 'a_b', 4), (455, 'axb', 5)");
    UltraDbTableSchema table;
    UltraDb_DescribeTable(c, "main", "items", table);
    TablePager pager(c, table, 200, SqlEngine::Sqlite);
    std::string error;

    Check(!pager.SetFilter({"nope", FilterOp::Equals, "1"}, error) && !error.empty(), "an unknown column is refused");
    Check(!pager.SetFilter({"label", FilterOp::Contains, ""}, error), "a comparison needs a value");
    Check(pager.SetFilter({"label", FilterOp::Contains, "ITEM 1"}, error), "a filter is set");
    Check(Counted(pager) == 111, "contains matches anywhere, in any case (item 1, 10-19, 100-199)");
    Check(pager.SetFilter({"label", FilterOp::StartsWith, "item 4"}, error) && pager.Filters().size() == 1,
          "a second filter for the column replaces the first");
    Check(Counted(pager) == 62, "starts with (item 4, 40-49, 400-450)");
    Check(pager.SetFilter({"weight", FilterOp::Greater, "200"}, error) && pager.Filters().size() == 2,
          "a filter on another column is added");
    Check(Counted(pager) == 11, "both must hold (item 4, 40-49); a number compares as a number");
    pager.RemoveFilter("label");
    Check(Counted(pager) == 49, "weight > 200: ids 1-49");
    UltraDbResultSet page;
    Check(pager.LoadPage(0, page).success && page.Size() == 49 && page.Row(0)["id"].AsInt64() == 1,
          "the page holds the filtered rows, in key order");

    pager.ClearFilters();
    pager.SetFilter({"label", FilterOp::Contains, "50%"}, error);
    Check(Counted(pager) == 1, "a % in the value is matched literally");
    pager.SetFilter({"label", FilterOp::Contains, "a_b"}, error);
    Check(Counted(pager) == 1, "so is an _");
    pager.SetFilter({"label", FilterOp::IsNull, ""}, error);
    Check(Counted(pager) == 1, "is NULL takes no value");
    pager.SetFilter({"label", FilterOp::IsNotNull, ""}, error);
    Check(Counted(pager) == 454, "is not NULL");
    pager.SetFilter({"id", FilterOp::Equals, "300"}, error);
    pager.SetFilter({"weight", FilterOp::LessOrEqual, "75"}, error);
    Check(Counted(pager) == 1, "= on a number column, with <=");

    std::string sql;
    pager.ClearFilters();
    pager.SetFilter({"label", FilterOp::Contains, "x"}, error);
    pager.PageSql(sql);
    Check(sql == "SELECT * FROM \"main\".\"items\" WHERE CAST(\"label\" AS TEXT) LIKE ? ESCAPE '\\' "
                 "ORDER BY \"id\" ASC LIMIT ? OFFSET ?",
          "the value is a bound parameter, never written in: " + sql);
    Check(FilterText({"total", FilterOp::GreaterOrEqual, "10"}) == "total ≥ '10'" &&
          FilterText({"note", FilterOp::IsNull, ""}) == "note is NULL",
          "a filter reads as a short phrase");
    UltraDb_CloseConnection(c);
}

void TestPagerFiltersPostgres() {
    Section("Browse filters, blobs (PostgreSQL)");
    const std::string c = PostgresConnection("dbadmin-filter-pg");
    if (c.empty()) return;
    const std::string schema = "dbadmin_" + ConnectionStore::NewId();
    UltraDb_Exec(c, "CREATE SCHEMA \"" + schema + "\"");
    const std::string items = "\"" + schema + "\".items";
    UltraDb_Exec(c, "CREATE TABLE " + items + " (id serial PRIMARY KEY, label text, qty integer, "
                    "seen date, data bytea)");
    UltraDb_Exec(c, "INSERT INTO " + items + " (label, qty, seen) VALUES ('Red Pen', 5, '2026-01-02'), "
                    "('blue pen', 12, '2026-03-04'), ('Ink', 40, NULL), ('100% cotton', 7, '2026-05-06')");
    UltraDbTableSchema table;
    Check(UltraDb_DescribeTable(c, schema, "items", table).success, "the table is described");
    TablePager pager(c, table, 200, SqlEngine::Postgres);
    std::string error;
    pager.SetFilter({"label", FilterOp::Contains, "PEN"}, error);
    Check(Counted(pager) == 2, "contains ignores case on PostgreSQL too (ILIKE)");
    pager.SetFilter({"label", FilterOp::Contains, "100%"}, error);
    Check(Counted(pager) == 1, "a % is matched literally");
    pager.ClearFilters();
    pager.SetFilter({"qty", FilterOp::GreaterOrEqual, "10"}, error);
    Check(Counted(pager) == 2, "an integer column compares with the typed value as a number");
    pager.SetFilter({"seen", FilterOp::Less, "2026-04-01"}, error);
    Check(Counted(pager) == 1, "a date compares as a date");
    pager.ClearFilters();
    pager.SetFilter({"seen", FilterOp::IsNull, ""}, error);
    UltraDbResultSet page;
    Check(pager.LoadPage(0, page).success && page.Size() == 1 && page.Row(0)["label"].AsString() == "Ink",
          "is NULL, and the page is filtered");
    pager.ClearFilters();
    pager.SetFilter({"qty", FilterOp::Equals, "many"}, error);
    int64_t none = 0;
    Check(!pager.CountRows(none).success, "a value the column cannot take is the database's error");

    // A blob staged into a bytea column and written.
    std::vector<uint8_t> bytes = {0x89, 'P', 'N', 'G', 0x00, 0xff, 0x10};
    UltraDbResultSet rows = Rows(c, "SELECT * FROM " + items + " WHERE id = 1");
    EditBuffer edits(table);
    Check(edits.UpdateRow(rows.Row(0), {{"data", CellValue::BlobValue(bytes)}}, error), "a blob is staged");
    size_t failed = 0;
    Check(edits.Commit(c, failed).success, "and committed");
    rows = Rows(c, "SELECT data FROM " + items + " WHERE id = 1");
    Check(rows.Size() == 1 && rows.Row(0)["data"].AsBlob() == bytes, "the bytea holds the bytes exactly");

    UltraDb_Exec(c, "DROP SCHEMA \"" + schema + "\" CASCADE");
    UltraDb_CloseConnection(c);
}

void TestPagerFiltersMysql() {
    Section("Browse filters, blobs (MySQL / MariaDB)");
    const std::string c = MysqlConnection("dbadmin-filter-my");
    if (c.empty()) return;
    const std::string schema = "dbadmin_" + ConnectionStore::NewId();
    UltraDb_Exec(c, "CREATE DATABASE `" + schema + "`");
    const std::string items = "`" + schema + "`.items";
    UltraDb_Exec(c, "CREATE TABLE " + items + " (id int AUTO_INCREMENT PRIMARY KEY, label varchar(40), qty int, "
                    "seen date, data blob)");
    UltraDb_Exec(c, "INSERT INTO " + items + " (label, qty, seen) VALUES ('Red Pen', 5, '2026-01-02'), "
                    "('blue pen', 12, '2026-03-04'), ('Ink', 40, NULL), ('100% cotton', 7, '2026-05-06'), "
                    "('a\\\\b!', 1, NULL)");
    UltraDbTableSchema table;
    Check(UltraDb_DescribeTable(c, schema, "items", table).success, "the table is described");
    TablePager pager(c, table, 200, SqlEngine::MySql);
    std::string error;
    pager.SetFilter({"label", FilterOp::Contains, "PEN"}, error);
    Check(Counted(pager) == 2, "contains ignores case, as the collation does");
    pager.SetFilter({"label", FilterOp::Contains, "100%"}, error);
    Check(Counted(pager) == 1, "a % is matched literally");
    pager.SetFilter({"label", FilterOp::Contains, "a\\b!"}, error);
    Check(Counted(pager) == 1, "a backslash and the ! that escapes are matched literally");
    pager.ClearFilters();
    pager.SetFilter({"qty", FilterOp::GreaterOrEqual, "10"}, error);
    Check(Counted(pager) == 2, "an integer column compares with the typed value as a number");
    pager.SetFilter({"seen", FilterOp::Less, "2026-04-01"}, error);
    Check(Counted(pager) == 1, "a date compares as a date");
    pager.ClearFilters();
    pager.SetFilter({"qty", FilterOp::Contains, "4"}, error);
    Check(Counted(pager) == 1, "a number searched as text");
    pager.ClearFilters();
    pager.SetFilter({"seen", FilterOp::IsNull, ""}, error);
    UltraDbResultSet page;
    Check(pager.LoadPage(0, page).success && page.Size() == 2 && page.Row(0)["label"].AsString() == "Ink",
          "is NULL, and the page is filtered");

    // A blob staged into a blob column and written.
    std::vector<uint8_t> bytes = {0x89, 'P', 'N', 'G', 0x00, 0xff, '\\', '\''};
    UltraDbResultSet rows = Rows(c, "SELECT * FROM " + items + " WHERE id = 1");
    EditBuffer edits(table);
    Check(edits.UpdateRow(rows.Row(0), {{"data", CellValue::BlobValue(bytes)}}, error), "a blob is staged");
    size_t failed = 0;
    Check(edits.Commit(c, failed).success, "and committed");
    rows = Rows(c, "SELECT data FROM " + items + " WHERE id = 1");
    Check(rows.Size() == 1 && rows.Row(0)["data"].AsBlob() == bytes, "the blob holds the bytes exactly");

    UltraDb_Exec(c, "DROP DATABASE `" + schema + "`");
    UltraDb_CloseConnection(c);
}

void TestBlobsAndRowsText() {
    Section("Blobs, rows as text");
    TempDir dir;
    ConnectionStore store;
    store.OpenInMemory();
    const std::string c = MakeSqlite(store, dir, 2);
    UltraDb_Exec(c, "CREATE TABLE files(id INTEGER PRIMARY KEY, name TEXT, data BLOB)");
    UltraDb_Exec(c, "INSERT INTO files VALUES (1, 'a', X'0102'), (2, 'b', NULL)");
    UltraDbTableSchema files;
    UltraDb_DescribeTable(c, "main", "files", files);
    UltraDbResultSet rows = Rows(c, "SELECT * FROM files ORDER BY id");

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 13};
    EditBuffer edits(files);
    std::string error;
    Check(edits.UpdateRow(rows.Row(0), {{"data", CellValue::BlobValue({0x01, 0x02})}}, error) && edits.Empty(),
          "the same bytes are no change");
    Check(edits.UpdateRow(rows.Row(1), {{"data", CellValue::BlobValue(png)}}, error) && edits.Count() == 1,
          "a file's bytes are staged for a cell");
    const CellValue* staged = edits.PendingCell(rows.Row(1), "data");
    Check(staged && staged->kind == CellValue::Kind::Blob && staged->bytes == png, "the grid sees the staged bytes");
    std::vector<GeneratedStatement> statements;
    edits.Statements(c, statements);
    Check(statements.size() == 1 && statements[0].params[0].Type() == UltraDbType::Blob &&
              Has(statements[0].preview, "X'89504e470d0a1a0a0000000d'"),
          "it binds as a blob and previews as X'..'");
    size_t failed = 0;
    Check(edits.Commit(c, failed).success, "it is written");
    UltraDbResultSet back = Rows(c, "SELECT data FROM files WHERE id = 2");
    Check(back.Row(0)["data"].AsBlob() == png, "and reads back exactly");

    // What a blob is.
    Check(SniffBlob(png).image && SniffBlob(png).extension == "png", "a PNG by its signature");
    Check(SniffBlob({0xff, 0xd8, 0xff, 0xe0}).extension == "jpg", "a JPEG");
    const std::string pdf = "%PDF-1.7\n";
    Check(SniffBlob(std::vector<uint8_t>(pdf.begin(), pdf.end())).extension == "pdf", "a PDF");
    const std::string svg = "<?xml version=\"1.0\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\"/>";
    Check(SniffBlob(std::vector<uint8_t>(svg.begin(), svg.end())).image, "an SVG is a picture");
    const std::string text = "Grüße\nline two";
    Check(SniffBlob(std::vector<uint8_t>(text.begin(), text.end())).text, "UTF-8 text is text");
    Check(!SniffBlob({0x00, 0x01, 0x02}).text && SniffBlob({0x00, 0x01}).extension == "bin",
          "anything else is binary data");
    const std::string dump = HexDump({'A', 'B', 0x00, 0xff});
    Check(dump == "00000000  41 42 00 ff                                       |AB..|\n",
          "a hex dump line: offset, bytes, characters: " + dump);
    Check(Has(HexDump(std::vector<uint8_t>(40, 7), 16), "24 B more"), "a long blob says how much is not shown");

    // Rows as text.
    std::vector<UltraDbRow> picked = {rows.Row(0), rows.Row(1)};
    const std::vector<std::string> columns = rows.Columns();
    Check(RowsText(columns, picked, ExportFormat::Csv, SqlEngine::Sqlite, "") == "id,name,data\n1,a,\\x0102\n2,b,\n",
          "CSV: a header, NULL empty: " + RowsText(columns, picked, ExportFormat::Csv, SqlEngine::Sqlite, ""));
    const std::string json = RowsText(columns, picked, ExportFormat::Json, SqlEngine::Sqlite, "");
    Check(json == "[\n{\"id\": 1, \"name\": \"a\", \"data\": \"\\\\x0102\"},\n{\"id\": 2, \"name\": \"b\", \"data\": null}\n]\n",
          "JSON: an array of objects: " + json);
    const std::string sql = RowsText(columns, picked, ExportFormat::Sql, SqlEngine::Sqlite, QuotedName(SqlEngine::Sqlite, "my \"files\""));
    Check(sql == "INSERT INTO \"my \"\"files\"\"\" (\"id\", \"name\", \"data\") VALUES (1, 'a', X'0102');\n"
                 "INSERT INTO \"my \"\"files\"\"\" (\"id\", \"name\", \"data\") VALUES (2, 'b', NULL);\n",
          "SQL: one INSERT per row, every name quoted: " + sql);
    UltraDb_CloseConnection(c);

    Check(StatementTable("SELECT a FROM orders WHERE x = 1") == "orders", "the table a SELECT reads");
    Check(StatementTable("select * from \"main\".\"line items\" o join x") == "\"main\".\"line items\"",
          "schema-qualified, as written");
    Check(StatementTable("SELECT (SELECT 1 FROM t), 2") == "" && StatementTable("SELECT * FROM (SELECT 1)") == "",
          "not one inside parentheses, nor a subquery");
}

void TestSqlRunner() {
    Section("SQL runner");
    TempDir dir;
    ConnectionStore store;
    store.OpenInMemory();
    const std::string c = MakeSqlite(store, dir, 10);

    std::mutex m;
    std::condition_variable cv;
    bool finished = false;
    RunOutcome last;
    auto done = [&](const RunOutcome& outcome) {
        std::lock_guard<std::mutex> lock(m);
        last = outcome;
        finished = true;
        cv.notify_all();
    };
    auto wait = [&] {
        std::unique_lock<std::mutex> lock(m);
        return cv.wait_for(lock, std::chrono::seconds(20), [&] { return finished; });
    };

    SqlRunner runner(c);
    Check(runner.Start("SELECT count(*) AS n FROM items; UPDATE items SET label = 'x' WHERE id = 1", done),
          "a script starts");
    Check(wait(), "and finishes");
    Check(last.result.success && last.statements.size() == 2, "with every statement reported");
    Check(last.statements[0].rows.Row(0)["n"].AsInt64() == 10, "rows for the SELECT");
    Check(last.statements[1].result.affectedRows == 1, "a count for the UPDATE");
    Check(!runner.IsRunning(), "and the runner is free again");

    // A long one, stopped.
    finished = false;
    Check(runner.Start("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 1000000000) "
                       "SELECT count(*) FROM n", done), "a long query starts");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    Check(runner.IsRunning(), "and is running");
    Check(!runner.Start("SELECT 1", done), "a second script waits for the first: refused");
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    runner.Cancel();
    Check(wait(), "Stop ends it");
    Check(last.cancelled && last.result.code == UltraDbResultCode::Cancelled, "reported as cancelled");
    Check(last.seconds < 10.0, "promptly");

    finished = false;
    runner.Start("SELECT 1", done);
    wait();
    finished = false;
    runner.Start("SELECT 1", done);
    wait();
    const std::vector<std::string> history = runner.History();
    Check(history.size() == 3, "history keeps each script once in a row (" + std::to_string(history.size()) + ")");
    Check(history.front() == "SELECT 1", "newest first");

    // An imported file, as one transaction: all of it, or none of it.
    RunOptions imported;
    imported.oneTransaction = true;
    imported.remember = false;
    imported.label = "fix.sql";
    finished = false;
    runner.Start("UPDATE items SET label = 'imported' WHERE id = 2; INSERT INTO items(id) VALUES (2)", done, imported);
    wait();
    Check(!last.result.success && last.rolledBack && last.statements.size() == 2 && last.label == "fix.sql",
          "a file whose second statement fails is rolled back, the failure being its own statement 2");
    Check(Rows(c, "SELECT label FROM items WHERE id = 2").Row(0)["label"].AsString() == "item 2",
          "so its first statement left nothing");
    finished = false;
    runner.Start("UPDATE items SET label = 'imported' WHERE id = 2; DELETE FROM items WHERE id = 3", done, imported);
    wait();
    Check(last.result.success && !last.rolledBack && last.transaction, "a file that runs is committed");
    Check(Rows(c, "SELECT label FROM items WHERE id = 2").Row(0)["label"].AsString() == "imported" &&
              Rows(c, "SELECT count(*) AS n FROM items WHERE id = 3").Row(0)["n"].AsInt64() == 0,
          "with every statement");
    Check(runner.History().size() == 3, "and an imported file is not in the history");
    UltraDb_CloseConnection(c);
}

void TestSqlScript() {
    Section("Reading a SQL file");
    std::string script, error;
    Check(ScriptFromBytes({0xEF, 0xBB, 0xBF, 'S', 'E', 'L', 'E', 'C', 'T', ' ', '1'}, script, error) &&
              script == "SELECT 1",
          "a byte-order mark is dropped");
    Check(!ScriptFromBytes({'a', 0, 'b'}, script, error), "a file with a NUL byte is not a script");

    ScriptSummary summary = SummarizeScript(SqlEngine::Sqlite,
        "-- a comment; with a semicolon\n"
        "CREATE TABLE t(a TEXT DEFAULT ';');\n"
        "CREATE TRIGGER tr AFTER INSERT ON t BEGIN UPDATE t SET a = 'x'; DELETE FROM t WHERE 0; END;\n"
        "INSERT INTO t VALUES ('a;b'); DROP TABLE old; drop view v; DELETE FROM t;");
    Check(summary.unsupported.empty() && summary.statements == 6,
          "statements are counted past quotes, comments and a trigger's body (" +
              std::to_string(summary.statements) + ")");
    Check(!summary.ownTransactions, "a trigger's BEGIN is not a transaction");
    Check(RiskyText(summary) == "2 DROP, 1 DELETE", "the risky ones, by their first word: " + RiskyText(summary));

    summary = SummarizeScript(SqlEngine::Sqlite, "PRAGMA foreign_keys=OFF;\nBEGIN TRANSACTION;\nCREATE TABLE t(a);\nCOMMIT;\n");
    Check(summary.ownTransactions && summary.statements == 4, "a SQLite .dump manages its own transaction");
    summary = SummarizeScript(SqlEngine::Postgres, "CREATE FUNCTION f() RETURNS int AS $body$ BEGIN RETURN 1; END; $body$ LANGUAGE plpgsql;\n"
                              "DO $$ BEGIN PERFORM 1; END $$;");
    Check(summary.unsupported.empty() && summary.statements == 2 && !summary.ownTransactions,
          "a dollar-quoted body is one string: its ';' and BEGIN are its own");
    summary = SummarizeScript(SqlEngine::Postgres, "SET client_encoding = 'UTF8';\nCOPY public.t (a) FROM stdin;\n1\n\\.\n");
    Check(!summary.unsupported.empty(), "pg_dump's COPY ... FROM stdin needs psql, and says so");
    summary = SummarizeScript(SqlEngine::Postgres, "\\connect shop\nSELECT 1;");
    Check(!summary.unsupported.empty(), "so does a backslash command");
    summary = SummarizeScript(SqlEngine::Sqlite, "SELECT 'open");
    Check(!summary.unsupported.empty(), "and a quote never closed");

    // MySQL / MariaDB, as the mysql client splits: a dump's DELIMITER lines,
    // # comments, backslash escapes and backticks.
    summary = SummarizeScript(SqlEngine::MySql,
                              "# a comment; with a semicolon\n"
                              "SET FOREIGN_KEY_CHECKS = 0;\n"
                              "CREATE TABLE `a;b` (s varchar(9) DEFAULT 'it\\'s;');\n"
                              "DELIMITER ;;\n"
                              "CREATE TRIGGER tr BEFORE INSERT ON `a;b` FOR EACH ROW BEGIN SET NEW.s = 'x'; END;;\n"
                              "DELIMITER ;\n"
                              "DROP TABLE old; delete from t; START TRANSACTION; COMMIT;");
    Check(summary.unsupported.empty() && summary.statements == 7,
          "MySQL: counted past backticks, escapes, # comments and DELIMITER (" + std::to_string(summary.statements) + ")");
    Check(summary.ownTransactions && RiskyText(summary) == "1 DROP, 1 DELETE",
          "MySQL: START TRANSACTION, and the risky ones: " + RiskyText(summary));
    summary = SummarizeScript(SqlEngine::MySql, "CREATE TRIGGER t BEFORE INSERT ON x FOR EACH ROW BEGIN SET @a = 1; END;");
    Check(summary.statements == 1 && !summary.ownTransactions, "MySQL: a trigger's BEGIN is its body, not a transaction");
}

void TestImport() {
    Section("Importing rows");
    Check(NormalizeNumber("1.234,56", ',', '.') == "1234.56", "a German number is read");
    Check(NormalizeNumber(" -1,234,567.5 ", '.', ',') == "-1234567.5", "a grouped one with a sign");
    Check(NormalizeNumber("1,5", '.', ',').empty(), "\"1,5\" is not 15: a group has three digits");
    Check(NormalizeNumber("12 kg", '.', ',').empty() && NormalizeNumber("2026-10-21", '.', ',').empty() &&
              NormalizeNumber("1e", '.', ',').empty() && NormalizeNumber("", '.', ',').empty(),
          "nor is a quantity, a date or half an exponent");
    Check(NormalizeNumber("01067", '.', ',') == "01067" && NormalizeNumber("2.5E-3", '.', ',') == "2.5e-3",
          "leading zeros and exponents are numbers");

    CSVImportOptions options;
    options.SetSingleSeparator(';');
    options.decimalSeparator = ',';
    options.thousandsSeparator = '.';
    const std::string csv = "Name;Weight;Zip;Note\n"
                            "apple;1.234,5;01067;\"red; sweet\"\n"
                            "\n"
                            "pear;0,75;10115;\n";
    ImportGrid grid = GridFromCsv(csv, options, "fruit.csv");
    Check(grid.rows.size() == 3 && grid.Width() == 4, "three rows, the empty line left out");
    Check(grid.rowNumbers == std::vector<size_t>({1, 2, 4}), "each row keeps its line in the file");
    Check(grid.rows[1][1].number == "1234.5" && grid.rows[1][3].text == "red; sweet", "numbers and quoted text");

    TempDir dir;
    ConnectionStore store;
    store.OpenInMemory();
    const std::string c = MakeSqlite(store, dir, 0);
    UltraDb_Exec(c, "CREATE TABLE fruit(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, weight REAL, "
                    "zip TEXT, note TEXT DEFAULT 'none')");
    UltraDbTableSchema fruit;
    UltraDb_DescribeTable(c, "main", "fruit", fruit);

    Check(GuessHeader(fruit, grid), "a first row of column names is taken for a header");
    ImportMapping mapping = AutoMapping(fruit, grid, true);
    Check(mapping.source == std::vector<int>({-1, 0, 1, 2, 3}), "columns are matched by name, in any case");
    Check(FileColumnNames(grid, true)[1] == "Weight" && FileColumnNames(grid, false)[1] == "Column 2",
          "the file's columns by their names, or by number");
    Check(CheckMapping(fruit, grid, mapping).empty() && DataRows(grid, mapping) == 2, "two rows to import");
    const UltraDbResultSet preview = ImportPreview(fruit, grid, mapping, 10);
    Check(preview.Size() == 2 && preview.Columns().size() == 4 && preview.Row(0)["weight"].AsString() == "1234.5" &&
              preview.Row(0)["zip"].AsString() == "01067" && preview.Row(1)["note"].IsNull(),
          "the preview: a number for REAL, the text as written for TEXT, an empty cell NULL");

    size_t imported = 0, failed = 0;
    UltraDbResult result = ImportRows(c, fruit, grid, mapping, imported, failed);
    Check(result.success && imported == 2, "the rows are imported (" + result.message + ")");
    const UltraDbResultSet rows = Rows(c, "SELECT name, weight, typeof(weight) AS t, zip, note FROM fruit ORDER BY id");
    Check(rows.Size() == 2 && rows.Row(0)["t"].AsString() == "real" && rows.Row(0)["weight"].AsDouble() == 1234.5 &&
              rows.Row(0)["zip"].AsString() == "01067" && rows.Row(1)["note"].IsNull(),
          "stored as the columns want them");

    // All or none: the same file again breaks UNIQUE on its first row.
    const std::string more = "Name;Weight\nplum;1\napple;2\n";
    grid = GridFromCsv(more, options, "more.csv");
    mapping = AutoMapping(fruit, grid, true);
    result = ImportRows(c, fruit, grid, mapping, imported, failed);
    Check(!result.success && failed == 3 && imported == 0, "a row the table refuses fails, by its line in the file");
    Check(Rows(c, "SELECT count(*) AS n FROM fruit").Row(0)["n"].AsInt64() == 2, "and nothing of the file stays");

    // No header: the file's columns in order, the numbered key left to the database.
    grid = GridFromCsv("kiwi;0,1;;green\n", options, "kiwi.csv");
    Check(!GuessHeader(fruit, grid), "a first row of data is not a header");
    mapping = AutoMapping(fruit, grid, false);
    Check(mapping.source == std::vector<int>({-1, 0, 1, 2, 3}), "a file one column short skips the numbered key");
    mapping.emptyIsNull = false;
    result = ImportRows(c, fruit, grid, mapping, imported, failed);
    Check(result.success && Rows(c, "SELECT zip FROM fruit WHERE name = 'kiwi'").Row(0)["zip"].AsString().empty(),
          "an empty cell can be empty text instead of NULL");

    // An exact numeric comes back as text; its declared type says it takes numbers.
    UltraDbColumnSchema exact;
    exact.declaredType = "numeric(10,2)";
    exact.valueType = UltraDbType::Text;
    UltraDbColumnSchema text;
    text.declaredType = "text";
    const ImportCell german = GridFromCsv("1.234,50\n", options, "x").rows[0][0];
    Check(ImportValue(german, exact, true).AsString() == "1234.50" && ImportValue(german, text, true).AsString() == "1.234,50",
          "numeric(10,2) gets the number, text the text as written");

    mapping.source = {-1, -1, 1, -1, -1};
    Check(Has(CheckMapping(fruit, grid, mapping), "name"), "a NOT NULL column without a default needs a column");
    mapping.source.assign(5, -1);
    Check(!CheckMapping(fruit, grid, mapping).empty(), "and so does an import of nothing");
    UltraDb_CloseConnection(c);
}

// The rows of `sql` on `c`, for the edit-buffer tests.
UltraDbResultSet Rows(const std::string& c, const std::string& sql) {
    UltraDbResultSet rows;
    UltraDb_Query(c, sql, rows);
    return rows;
}

void TestEditBuffer() {
    Section("Editing rows");
    TempDir dir;
    ConnectionStore store;
    store.OpenInMemory();
    const std::string c = MakeSqlite(store, dir, 3);
    UltraDb_Exec(c, "CREATE TABLE \"line items\"(\"order\" INTEGER, line INTEGER, sku TEXT NOT NULL, "
                    "qty INTEGER DEFAULT 1, PRIMARY KEY(\"order\", line))");
    UltraDb_Exec(c, "INSERT INTO \"line items\" VALUES (1, 1, 'a', 2), (1, 2, 'b', 3)");
    UltraDb_Exec(c, "CREATE TABLE nokey(v TEXT)");
    UltraDb_Exec(c, "CREATE VIEW v AS SELECT * FROM items");

    UltraDbTableSchema items, lines, nokey, view;
    UltraDb_DescribeTable(c, "main", "items", items);
    UltraDb_DescribeTable(c, "main", "line items", lines);
    UltraDb_DescribeTable(c, "main", "nokey", nokey);
    UltraDb_DescribeTable(c, "main", "v", view);
    Check(EditBuffer(items).ReadOnlyReason().empty(), "a table with a key is editable");
    Check(!EditBuffer(nokey).ReadOnlyReason().empty(), "a table without a key is not, and says why");
    Check(!EditBuffer(view).ReadOnlyReason().empty(), "nor is a view");

    // Stage: an edit, a new row, a deletion.
    EditBuffer buffer(lines);
    const UltraDbResultSet page = Rows(c, "SELECT * FROM \"line items\" ORDER BY \"order\", line");
    std::string error;
    Check(buffer.UpdateRow(page.Row(0), {{"qty", CellValue::Of("5")}, {"sku", CellValue::Of("it's")}}, error),
          "an edit is staged (" + error + ")");
    Check(buffer.UpdateRow(page.Row(0), {{"qty", CellValue::Of("2")}}, error) && buffer.Count() == 1,
          "editing a cell back to what was read drops that cell, not the change");
    Check(buffer.PendingCell(page.Row(0), "sku") && buffer.PendingCell(page.Row(0), "sku")->text == "it's" &&
          !buffer.PendingCell(page.Row(0), "qty"), "the grid can ask for a cell's staged value");
    Check(buffer.InsertRow({{"order", CellValue::Of("1")}, {"line", CellValue::Of("3")},
                            {"sku", CellValue::Of("c")}, {"qty", CellValue::DefaultValue()}}, error),
          "a new row is staged");
    Check(buffer.DeleteRow(page.Row(1), error) && buffer.PendingDelete(page.Row(1)), "a deletion is staged");
    Check(!buffer.UpdateRow(page.Row(1), {{"sku", CellValue::Of("x")}}, error),
          "a row to be deleted cannot also be edited");
    Check(buffer.Count() == 3 && buffer.Count(RowChange::Kind::Delete) == 1, "three changes pending");

    std::vector<GeneratedStatement> statements;
    Check(buffer.Statements(c, statements).success && statements.size() == 3, "they become three statements");
    if (statements.size() == 3) {
        Check(statements[0].sql == "UPDATE \"main\".\"line items\" SET \"sku\" = ? WHERE \"order\" = ? AND \"line\" = ?",
              "an update binds its values and addresses the row by its whole key: " + statements[0].sql);
        Check(statements[0].preview ==
                  "UPDATE \"main\".\"line items\" SET \"sku\" = 'it''s' WHERE \"order\" = 1 AND \"line\" = 1",
              "the preview writes the values in, quotes doubled: " + statements[0].preview);
        Check(statements[1].sql == "INSERT INTO \"main\".\"line items\" (\"line\", \"order\", \"sku\") VALUES (?, ?, ?)",
              "an insert leaves a DEFAULT column out: " + statements[1].sql);
        Check(statements[2].sql == "DELETE FROM \"main\".\"line items\" WHERE \"order\" = ? AND \"line\" = ?",
              "a delete by key: " + statements[2].sql);
    }

    size_t failed = 0;
    Check(buffer.Commit(c, failed).success, "the changes commit");
    const UltraDbResultSet after = Rows(c, "SELECT * FROM \"line items\" ORDER BY \"order\", line");
    Check(after.Size() == 2 && after.Row(0)["sku"].AsString() == "it's" && after.Row(1)["line"].AsInt64() == 3 &&
          after.Row(1)["qty"].AsInt64() == 1, "the edit, the new row with its default, and no deleted row");

    // All or nothing: a row that vanished fails the commit and undoes the rest.
    EditBuffer stale(items);
    const UltraDbResultSet itemRows = Rows(c, "SELECT * FROM items ORDER BY id");
    stale.UpdateRow(itemRows.Row(0), {{"label", CellValue::Of("changed")}}, error);
    stale.UpdateRow(itemRows.Row(1), {{"label", CellValue::NullValue()}}, error);
    UltraDb_Exec(c, "DELETE FROM items WHERE id = 2");          // meanwhile, elsewhere
    UltraDbResult result = stale.Commit(c, failed);
    Check(!result.success && failed == 1, "an update of a row that is gone fails, naming the statement");
    Check(Rows(c, "SELECT label FROM items WHERE id = 1").Row(0)["label"].AsString() == "item 1",
          "and the statement before it is rolled back");
    Check(stale.Count() == 2, "the buffer keeps its changes for another try");

    // New row with nothing given.
    EditBuffer bare(items);
    bare.InsertRow({}, error);
    Check(bare.Statements(c, statements).success && statements[0].sql == "INSERT INTO \"main\".\"items\" DEFAULT VALUES",
          "a new row with no values given uses DEFAULT VALUES");
    UltraDb_CloseConnection(c);
}

bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

size_t StepWith(const std::vector<DdlStep>& steps, const std::string& part) {
    for (size_t i = 0; i < steps.size(); ++i)
        if (Has(steps[i].sql, part)) return i;
    return steps.size();
}

size_t ColumnAt(const StructureEdit& edit, const std::string& name) {
    for (size_t i = 0; i < edit.Columns().size(); ++i)
        if (edit.Columns()[i].name == name) return i;
    return edit.Columns().size();
}

void TestSqliteTable() {
    Section("Reading a CREATE TABLE");
    std::string error;
    Check(CheckSqlFragment("numeric(10, 2)", "The type", error), "a type with its precision is one fragment");
    Check(CheckSqlFragment("'it''s'", "The default", error), "a quoted default with a doubled quote is one");
    Check(!CheckSqlFragment("int; DROP TABLE x", "The type", error) && Has(error, ";"),
          "a semicolon is refused: " + error);
    Check(!CheckSqlFragment("text -- note", "The type", error), "so is a comment");
    Check(!CheckSqlFragment("(1", "The default", error), "and an open parenthesis");
    Check(!CheckSqlFragment("'open", "The default", error), "and an open quote");

    Check(SqliteDefaultClause("0") == "DEFAULT 0", "a number is a default as it stands");
    Check(SqliteDefaultClause("-1") == "DEFAULT -1", "so is a signed one");
    Check(SqliteDefaultClause("'x'") == "DEFAULT 'x'", "and a string");
    Check(SqliteDefaultClause("CURRENT_TIMESTAMP") == "DEFAULT CURRENT_TIMESTAMP", "and CURRENT_TIMESTAMP");
    Check(SqliteDefaultClause("datetime('now')") == "DEFAULT (datetime('now'))",
          "an expression gets the parentheses table_info leaves off");
    Check(SqliteDefaultClause("(1 + 2)") == "DEFAULT (1 + 2)", "and is not wrapped twice");

    const std::string sql =
        "CREATE TABLE \"Order Items\" (\n"
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "  \"unit price\" NUMERIC(10, 2) NOT NULL DEFAULT 0 CHECK (\"unit price\" >= 0),\n"
        "  note TEXT COLLATE NOCASE, -- a comment\n"
        "  parent INTEGER REFERENCES \"Order Items\"(id) ON DELETE SET NULL NOT DEFERRABLE,\n"
        "  total REAL GENERATED ALWAYS AS (\"unit price\" * 2) VIRTUAL,\n"
        "  CONSTRAINT u UNIQUE (note), CHECK (id > 0))";
    SqliteCreateTable parsed;
    Check(ParseCreateTable(sql, parsed, error), "a definition with every kind of clause is read (" + error + ")");
    Check(parsed.columns.size() == 5 && parsed.autoincrement && !parsed.withoutRowid,
          "five columns, AUTOINCREMENT, a rowid table");
    if (parsed.columns.size() == 5) {
        const auto& price = parsed.columns[1];
        Check(price.name == "unit price" && price.constraints.size() == 3 && price.constraints[0].kind == "NOT NULL" &&
              price.constraints[1].kind == "DEFAULT" && price.constraints[2].kind == "CHECK",
              "a quoted column name and its constraints");
        Check(parsed.columns[3].constraints.size() == 1 && parsed.columns[3].constraints[0].kind == "REFERENCES",
              "SET NULL and NOT DEFERRABLE stay inside REFERENCES");
        Check(parsed.columns[4].constraints.size() == 1 && parsed.columns[4].constraints[0].kind == "GENERATED",
              "a generated column");
    }

    ColumnRewrite rewrite;
    rewrite.column = "UNIT PRICE";               // SQLite's names know no case
    rewrite.setType = true;
    rewrite.type = "REAL";
    rewrite.setNotNull = true;
    rewrite.notNull = false;
    rewrite.setDefault = true;
    rewrite.hasDefault = true;
    rewrite.defaultExpression = "abs(-1.5)";
    std::string rewritten;
    Check(RewriteCreateTable(sql, "\"main\".\"_new\"", {rewrite}, rewritten, error), "the definition is rewritten");
    Check(rewritten.rfind("CREATE TABLE \"main\".\"_new\" (\n  id INTEGER", 0) == 0, "under the new name: " + rewritten);
    Check(Has(rewritten, "\"unit price\" REAL CHECK (\"unit price\" >= 0) DEFAULT (abs(-1.5)),\n"),
          "only the changed column: its CHECK kept, NOT NULL gone, the new default: " + rewritten);
    Check(Has(rewritten, "note TEXT COLLATE NOCASE, -- a comment\n") && Has(rewritten, "CHECK (id > 0))"),
          "every other byte as it was");

    Check(!ParseCreateTable("CREATE VIRTUAL TABLE t USING fts5(a)", parsed, error), "a virtual table is refused");
    Check(!ParseCreateTable("CREATE TABLE t AS SELECT 1 AS a", parsed, error), "and CREATE TABLE AS SELECT");
    Check(!ParseCreateTable("CREATE TABLE t (a INTEGER PRIMARY KEY FROBNICATE)", parsed, error) && Has(error, "FROBNICATE"),
          "and a clause it does not know, by name: " + error);
    Check(ParseCreateTable("CREATE TABLE t (a TEXT PRIMARY KEY, b) WITHOUT ROWID", parsed, error) &&
              parsed.withoutRowid && parsed.columns.size() == 2,
          "WITHOUT ROWID, and an untyped column");
}

// Apply a structure edit, reporting what failed.
bool ApplyEdit(const std::string& c, const StructureEdit& edit, std::string& message) {
    SqliteTableSource source;
    if (edit.Engine() == SqlEngine::Sqlite) {
        const UltraDbResult read = ReadSqliteTableSource(c, edit.Table().schema, edit.Table().name, source);
        if (!read) { message = read.message; return false; }
    }
    std::vector<DdlStep> steps;
    UltraDbResult result = edit.Plan(c, source, steps);
    if (!result) { message = result.message; return false; }
    size_t failed = 0;
    result = StructureEdit::Apply(c, steps, failed);
    if (!result) { message = result.message + " (in: " + (failed < steps.size() ? steps[failed].sql : "") + ")"; return false; }
    return true;
}

void TestStructureEditSqlite() {
    Section("Editing structure (SQLite)");
    TempDir dir;
    ConnectionStore store;
    store.OpenInMemory();
    const std::string c = MakeSqlite(store, dir, 0);
    UltraDb_Exec(c, "PRAGMA foreign_keys = ON");
    UltraDb_Exec(c, "CREATE TABLE parent(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL "
                    "CHECK (length(name) < 20), price TEXT DEFAULT '0', note TEXT)");
    UltraDb_Exec(c, "CREATE TABLE child(id INTEGER PRIMARY KEY, pid INTEGER REFERENCES parent(id))");
    UltraDb_Exec(c, "CREATE INDEX parent_name ON parent(name)");
    UltraDb_Exec(c, "CREATE TABLE log(what TEXT)");
    UltraDb_Exec(c, "CREATE TRIGGER parent_log AFTER INSERT ON parent BEGIN INSERT INTO log VALUES (new.name); END");
    UltraDb_Exec(c, "CREATE VIEW priced AS SELECT name, price FROM parent");
    UltraDb_Exec(c, "INSERT INTO parent(name, price, note) VALUES ('a', '1.5', 'x'), ('b', '2', NULL), ('c', '3', NULL)");
    UltraDb_Exec(c, "DELETE FROM parent WHERE id = 3");      // AUTOINCREMENT remembers 3
    UltraDb_Exec(c, "INSERT INTO child(pid) VALUES (1), (2)");

    UltraDbTableSchema parent, view;
    UltraDb_DescribeTable(c, "main", "parent", parent);
    UltraDb_DescribeTable(c, "main", "priced", view);
    Check(!StructureEdit(view, SqlEngine::Sqlite).ReadOnlyReason().empty(), "a view's structure is not edited here");

    StructureEdit edit(parent, SqlEngine::Sqlite);
    std::string error;
    const size_t price = ColumnAt(edit, "price");
    ColumnDraft draft = edit.Columns()[price];
    draft.type = "REAL";
    draft.notNull = true;
    draft.defaultExpression = "0";
    Check(edit.UpdateColumn(price, draft, error) && edit.NeedsRebuild(), "a type change asks for a rebuild");
    draft = edit.Columns()[ColumnAt(edit, "note")];
    draft.name = "remark";
    Check(edit.UpdateColumn(ColumnAt(edit, "note"), draft, error) && edit.Columns()[3].origin == "note",
          "a rename keeps the column's origin");
    ColumnDraft qty;
    qty.name = "qty";
    qty.type = "INTEGER";
    qty.notNull = true;
    Check(!edit.AddColumn(qty, error), "SQLite adds a NOT NULL column only with a default: " + error);
    qty.hasDefault = true;
    qty.defaultExpression = "1";
    Check(edit.AddColumn(qty, error), "with one it is staged");
    ColumnDraft clash = qty;
    clash.name = "NAME";
    Check(!edit.AddColumn(clash, error), "a name taken, in another case, is refused: " + error);
    ColumnDraft bad = qty;
    bad.name = "bad";
    bad.type = "int); DROP TABLE child; --";
    Check(!edit.AddColumn(bad, error), "a type that is not one fragment is refused: " + error);
    Check(!edit.DropColumn(ColumnAt(edit, "id"), error), "a key column is not dropped: " + error);
    Check(!edit.DropColumn(ColumnAt(edit, "name"), error) && Has(error, "parent_name"),
          "nor an indexed one, naming the index: " + error);
    Check(edit.AddIndex({"parent_price", {"price"}, false}, error), "a new index is staged");
    Check(!edit.AddIndex({"PARENT_NAME", {"qty"}, false}, error), "an index name taken is refused");
    Check(!edit.AddIndex({"none", {}, false}, error), "an index on nothing is refused");
    Check(edit.Count() == 4 && !edit.Destructive(), "four changes, none of them a drop");

    SqliteTableSource source;
    Check(ReadSqliteTableSource(c, "main", "parent", source).success && source.foreignKeys &&
              source.sequence == 3 && source.indexes.size() == 1 && source.triggers.size() == 1 &&
              source.referencedBy == std::vector<std::string>{"child"},
          "the source: foreign keys on, the sequence, the index, the trigger, who refers to it");
    std::vector<DdlStep> steps;
    Check(edit.Plan(c, source, steps).success, "the plan is made");
    Check(!steps.empty() && steps.front().phase == DdlStep::Phase::Before &&
              steps.front().sql == "PRAGMA foreign_keys = OFF" && steps.back().phase == DdlStep::Phase::After &&
              steps.back().sql == "PRAGMA foreign_keys = ON",
          "foreign keys go off before the transaction and on after it");
    const size_t create = StepWith(steps, "CREATE TABLE \"main\".\"_dbadmin_new_parent\"");
    const size_t rename = StepWith(steps, "RENAME COLUMN \"note\" TO \"remark\"");
    const size_t add = StepWith(steps, "ADD COLUMN \"qty\" INTEGER NOT NULL DEFAULT 1");
    const size_t index = StepWith(steps, "CREATE INDEX \"main\".\"parent_price\" ON \"parent\" (\"price\")");
    Check(create < rename && rename < add && add < index && index < steps.size(),
          "rebuild, then rename, then add, then the new index");
    Check(create < steps.size() && Has(steps[create].sql, "price REAL DEFAULT 0 NOT NULL") == false &&
              Has(steps[create].sql, "price REAL NOT NULL DEFAULT 0") &&
              Has(steps[create].sql, "CHECK (length(name) < 20)"),
          "the new definition rewrites price alone: " + (create < steps.size() ? steps[create].sql : ""));
    Check(StepWith(steps, "INSERT INTO \"main\".\"_dbadmin_new_parent\" (\"id\", \"name\", \"price\", \"note\") SELECT") <
              steps.size(),
          "the rows are copied by name, the rowid alias with them");
    Check(StepWith(steps, "foreign_key_check(\"child\")") < steps.size(), "the children are checked before COMMIT");
    const std::string text = StructureEdit::PlanText(steps);
    Check(Has(text, "PRAGMA foreign_keys = OFF;\nBEGIN;\n") && Has(text, "COMMIT;\n") &&
              text.size() > text.rfind("COMMIT;") && Has(text.substr(text.rfind("COMMIT;")), "PRAGMA foreign_keys = ON;\n"),
          "the preview shows where the transaction is");

    std::string message;
    Check(ApplyEdit(c, edit, message), "the edit is applied (" + message + ")");
    const UltraDbResultSet types = Rows(c, "SELECT DISTINCT typeof(price) AS t FROM parent");
    Check(types.Size() == 1 && types.Row(0)["t"].AsString() == "real", "the prices are REAL now");
    Check(Rows(c, "SELECT count(*) AS n FROM child").Row(0)["n"].AsInt64() == 2, "the child rows are all there");
    Check(Rows(c, "PRAGMA foreign_keys").Row(0)[0].AsInt64() == 1, "foreign keys are on again");
    Check(Rows(c, "SELECT seq FROM sqlite_sequence WHERE name = 'parent'").Row(0)["seq"].AsInt64() == 3,
          "AUTOINCREMENT goes on after 3, not after the highest row left");
    Check(!UltraDb_Exec(c, "INSERT INTO parent(name) VALUES ('a name of far more than twenty characters')").success,
          "the CHECK constraint is still there");
    UltraDb_Exec(c, "INSERT INTO parent(name) VALUES ('d')");
    const UltraDbResultSet d = Rows(c, "SELECT id, price, remark, qty FROM parent WHERE name = 'd'");
    Check(d.Size() == 1 && d.Row(0)["id"].AsInt64() == 4 && d.Row(0)["price"].AsDouble() == 0.0 &&
              d.Row(0)["qty"].AsInt64() == 1,
          "a new row: number 4, the new default, the new column");
    Check(Rows(c, "SELECT count(*) AS n FROM log").Row(0)["n"].AsInt64() == 4, "the trigger still fires");
    Check(Rows(c, "SELECT count(*) AS n FROM sqlite_master WHERE type = 'index' AND name IN "
                  "('parent_name', 'parent_price')").Row(0)["n"].AsInt64() == 2,
          "the old index is back, the new one is there");
    Check(Rows(c, "SELECT count(*) AS n FROM priced").Row(0)["n"].AsInt64() == 3, "the view still reads the table");
    Check(Rows(c, "SELECT count(*) AS n FROM sqlite_master WHERE name LIKE '_dbadmin%'").Row(0)["n"].AsInt64() == 0,
          "no temporary table is left");

    // A rebuild the data refuses changes nothing.
    UltraDb_DescribeTable(c, "main", "parent", parent);
    SqliteTableSource before;
    ReadSqliteTableSource(c, "main", "parent", before);
    StructureEdit strict(parent, SqlEngine::Sqlite);
    draft = strict.Columns()[ColumnAt(strict, "remark")];
    draft.notNull = true;                        // two rows hold NULL
    strict.UpdateColumn(ColumnAt(strict, "remark"), draft, error);
    Check(!ApplyEdit(c, strict, message) && Has(message, "NOT NULL"), "NOT NULL over NULLs fails: " + message);
    SqliteTableSource unchanged;
    ReadSqliteTableSource(c, "main", "parent", unchanged);
    Check(unchanged == before, "and the table is exactly as it was");
    Check(Rows(c, "PRAGMA foreign_keys").Row(0)[0].AsInt64() == 1, "with foreign keys on again");

    // Drops, and the table's own name.
    UltraDb_Exec(c, "CREATE INDEX parent_qty ON parent(qty)");
    UltraDb_DescribeTable(c, "main", "parent", parent);
    StructureEdit drops(parent, SqlEngine::Sqlite);
    Check(!drops.DropColumn(ColumnAt(drops, "qty"), error), "an indexed column waits for its index");
    Check(drops.DropIndex("parent_qty", error) && drops.DropColumn(ColumnAt(drops, "qty"), error),
          "once its index goes, the column can go (" + error + ")");
    Check(drops.DropColumn(ColumnAt(drops, "remark"), error) && drops.Destructive(), "a drop is destructive");
    Check(drops.RestoreColumn(ColumnAt(drops, "remark"), error) && drops.Count() == 2, "and can be taken back");
    Check(drops.RenameTable("Parent Things", error), "the rename is staged");
    Check(ApplyEdit(c, drops, message), "drops and the rename are applied (" + message + ")");
    UltraDbTableSchema renamed;
    Check(UltraDb_DescribeTable(c, "main", "Parent Things", renamed).success && renamed.columns.size() == 4,
          "the table has its new name and a column fewer");
    Check(Has(Rows(c, "SELECT sql FROM sqlite_master WHERE name = 'child'").Row(0)["sql"].AsString(), "Parent Things"),
          "and the child's foreign key follows it");

    // A table with no rowid alias keeps its rowids through a rebuild.
    UltraDb_Exec(c, "CREATE TABLE plain(a TEXT, b TEXT)");
    UltraDb_Exec(c, "INSERT INTO plain VALUES ('1', 'x'), ('2', 'y'), ('3', 'z')");
    UltraDb_Exec(c, "DELETE FROM plain WHERE a = '1'");
    UltraDbTableSchema plain;
    UltraDb_DescribeTable(c, "main", "plain", plain);
    StructureEdit retype(plain, SqlEngine::Sqlite);
    draft = retype.Columns()[0];
    draft.type = "INTEGER";
    retype.UpdateColumn(0, draft, error);
    Check(ApplyEdit(c, retype, message), "a table without a key is rebuilt (" + message + ")");
    const UltraDbResultSet rowids = Rows(c, "SELECT rowid AS r, typeof(a) AS t FROM plain ORDER BY rowid");
    Check(rowids.Size() == 2 && rowids.Row(0)["r"].AsInt64() == 2 && rowids.Row(1)["r"].AsInt64() == 3 &&
              rowids.Row(0)["t"].AsString() == "integer",
          "with the rowids it had");
    UltraDb_CloseConnection(c);
}

// PostgreSQL, where a server is configured (see the header).
void TestStructureEditPostgres() {
    Section("Editing structure (PostgreSQL)");
    const char* database = std::getenv("ULTRAFIBU_TEST_PG_DB");
    const bool required = std::getenv("ULTRAFIBU_TEST_PG_REQUIRED") != nullptr;
    if (!database || !*database) {
        Check(!required, "a PostgreSQL server is configured");
        std::printf("  [SKIP] no PostgreSQL configured (ULTRAFIBU_TEST_PG_DB)\n");
        return;
    }
    auto env = [](const char* name, const char* fallback) {
        const char* v = std::getenv(name);
        return std::string(v && *v ? v : fallback);
    };
    UltraDbConnectionConfig cfg;
    cfg.name = "dbadmin-structure-pg";
    cfg.driver = "postgresql";
    cfg.database = database;
    cfg.host = env("ULTRAFIBU_TEST_PG_HOST", "/var/run/postgresql");
    cfg.port = std::atoi(env("ULTRAFIBU_TEST_PG_PORT", "5432").c_str());
    cfg.user = env("ULTRAFIBU_TEST_PG_USER", "");
    cfg.tls = UltraDbTls::Disable;       // the local test server
    const std::string c = cfg.name;
    if (!UltraDb_RegisterConnection(cfg) || !UltraDb_OpenConnection(c)) {
        Check(!required, "the PostgreSQL server is reached");
        std::printf("  [SKIP] PostgreSQL not reached\n");
        UltraDb_CloseConnection(c);
        return;
    }
    // A schema of its own, so a parallel run cannot meet it.
    const std::string schema = "dbadmin_" + ConnectionStore::NewId();
    UltraDb_Exec(c, "CREATE SCHEMA \"" + schema + "\"");
    const std::string items = "\"" + schema + "\".items";
    UltraDb_Exec(c, "CREATE TABLE " + items + " (id serial PRIMARY KEY, label varchar(20) NOT NULL, "
                    "qty text DEFAULT '1', note text)");
    UltraDb_Exec(c, "INSERT INTO " + items + " (label, qty) VALUES ('a', '5'), ('b', '7')");

    UltraDbTableSchema table;
    Check(UltraDb_DescribeTable(c, schema, "items", table).success, "the table is described");
    StructureEdit edit(table, SqlEngine::Postgres);
    std::string error;
    ColumnDraft draft = edit.Columns()[ColumnAt(edit, "qty")];
    draft.type = "integer";
    draft.defaultExpression = "1";
    Check(edit.UpdateColumn(ColumnAt(edit, "qty"), draft, error), "a type change is staged");
    draft = edit.Columns()[ColumnAt(edit, "label")];
    draft.notNull = false;
    edit.UpdateColumn(ColumnAt(edit, "label"), draft, error);
    draft = edit.Columns()[ColumnAt(edit, "note")];
    draft.name = "Remark";
    edit.UpdateColumn(ColumnAt(edit, "note"), draft, error);
    ColumnDraft added;
    added.name = "added";
    Check(!edit.AddColumn(added, error), "PostgreSQL needs a type: " + error);
    added.type = "timestamptz";
    added.hasDefault = true;
    added.defaultExpression = "now()";
    Check(edit.AddColumn(added, error), "a new column with an expression default");
    edit.AddIndex({"items_added", {"added"}, false}, error);
    Check(!edit.NeedsRebuild(), "PostgreSQL never rebuilds");

    std::vector<DdlStep> steps;
    Check(edit.Plan(c, SqliteTableSource(), steps).success, "the plan is made");
    const std::string alter = "ALTER TABLE \"" + schema + "\".\"items\"";
    const size_t dropDefault = StepWith(steps, alter + " ALTER COLUMN \"qty\" DROP DEFAULT");
    const size_t type = StepWith(steps, alter + " ALTER COLUMN \"qty\" TYPE integer USING CAST(\"qty\" AS integer)");
    const size_t setDefault = StepWith(steps, alter + " ALTER COLUMN \"qty\" SET DEFAULT 1");
    Check(dropDefault < type && type < setDefault && setDefault < steps.size(),
          "the old default goes, the type converts, the new default comes");
    Check(StepWith(steps, alter + " ALTER COLUMN \"label\" DROP NOT NULL") < steps.size() &&
              StepWith(steps, alter + " RENAME COLUMN \"note\" TO \"Remark\"") < steps.size() &&
              StepWith(steps, alter + " ADD COLUMN \"added\" timestamptz DEFAULT now()") < steps.size() &&
              StepWith(steps, "CREATE INDEX \"items_added\" ON " + std::string("\"") + schema + "\".\"items\"") <
                  steps.size(),
          "NOT NULL, the rename, the new column and its index");
    Check(std::all_of(steps.begin(), steps.end(), [](const DdlStep& s) { return s.phase == DdlStep::Phase::Inside; }),
          "all of it in the transaction");
    std::string message;
    Check(ApplyEdit(c, edit, message), "the edit is applied (" + message + ")");
    const UltraDbResultSet qty = Rows(c, "SELECT sum(qty) AS s, pg_typeof(min(qty))::text AS t FROM " + items);
    Check(qty.Size() == 1 && qty.Row(0)["s"].AsInt64() == 12 && qty.Row(0)["t"].AsString() == "integer",
          "the text became integers");
    Check(UltraDb_Exec(c, "INSERT INTO " + items + " (label) VALUES (NULL)").success, "label takes NULL now");
    Check(Rows(c, "SELECT count(*) AS n FROM " + items + " WHERE \"Remark\" IS NULL AND added IS NOT NULL")
                  .Row(0)["n"].AsInt64() == 3,
          "the renamed column and the new one, filled by its default");

    // A type change keeps an unchanged default, set again after the conversion.
    UltraDb_Exec(c, "ALTER TABLE " + items + " ADD COLUMN code text DEFAULT 'x'");
    UltraDb_DescribeTable(c, schema, "items", table);
    StructureEdit retype(table, SqlEngine::Postgres);
    draft = retype.Columns()[ColumnAt(retype, "code")];
    draft.type = "varchar(10)";
    retype.UpdateColumn(ColumnAt(retype, "code"), draft, error);
    Check(retype.Plan(c, SqliteTableSource(), steps).success &&
              StepWith(steps, "\"code\" DROP DEFAULT") < StepWith(steps, "\"code\" TYPE varchar(10)") &&
              StepWith(steps, "\"code\" TYPE varchar(10)") < StepWith(steps, "\"code\" SET DEFAULT 'x'::text") &&
              StepWith(steps, "\"code\" SET DEFAULT 'x'::text") < steps.size(),
          "a default is dropped before a type change and set again after it");
    Check(ApplyEdit(c, retype, message), "and the change applies (" + message + ")");
    UltraDb_Exec(c, "INSERT INTO " + items + " (label) VALUES ('c')");
    Check(Rows(c, "SELECT code FROM " + items + " WHERE label = 'c'").Row(0)["code"].AsString() == "x",
          "with the default still there");
    UltraDb_Exec(c, "ALTER TABLE " + items + " DROP COLUMN code");

    // A conversion that fails leaves the table as it was.
    UltraDb_DescribeTable(c, schema, "items", table);
    StructureEdit failing(table, SqlEngine::Postgres);
    draft = failing.Columns()[ColumnAt(failing, "label")];
    draft.type = "integer";                         // 'a' is no integer
    failing.UpdateColumn(ColumnAt(failing, "label"), draft, error);
    failing.RenameTable("things", error);
    Check(!ApplyEdit(c, failing, message), "a cast the data refuses fails: " + message);
    UltraDbTableSchema after;
    Check(UltraDb_DescribeTable(c, schema, "items", after).success && after.columns.size() == 5 &&
              after.columns[1].declaredType == table.columns[1].declaredType,
          "nothing changed, the rename included");

    UltraDb_Exec(c, "DROP SCHEMA \"" + schema + "\" CASCADE");
    UltraDb_CloseConnection(c);
}

void TestStructureEditMysql() {
    Section("Editing structure (MySQL / MariaDB)");
    const std::string c = MysqlConnection("dbadmin-structure-my");
    if (c.empty()) return;
    const std::string schema = "dbadmin_" + ConnectionStore::NewId();
    UltraDb_Exec(c, "CREATE DATABASE `" + schema + "`");
    const std::string items = "`" + schema + "`.items";
    UltraDb_Exec(c, "CREATE TABLE " + items + " (id int AUTO_INCREMENT PRIMARY KEY, label varchar(20) NOT NULL, "
                    "qty varchar(10) DEFAULT '1', note text COMMENT 'the remark') ENGINE=InnoDB");
    UltraDb_Exec(c, "CREATE INDEX by_label ON " + items + " (label)");
    UltraDb_Exec(c, "INSERT INTO " + items + " (label, qty) VALUES ('a', '5'), ('b', '7')");

    UltraDbTableSchema table;
    Check(UltraDb_DescribeTable(c, schema, "items", table).success, "the table is described");
    StructureEdit edit(table, SqlEngine::MySql);
    std::string error;
    ColumnDraft draft = edit.Columns()[ColumnAt(edit, "qty")];
    draft.type = "int";
    draft.defaultExpression = "1";
    Check(edit.UpdateColumn(ColumnAt(edit, "qty"), draft, error), "a type change is staged");
    draft = edit.Columns()[ColumnAt(edit, "label")];
    draft.notNull = false;
    edit.UpdateColumn(ColumnAt(edit, "label"), draft, error);
    draft = edit.Columns()[ColumnAt(edit, "note")];
    draft.name = "Remark";
    draft.type = "varchar(200)";
    edit.UpdateColumn(ColumnAt(edit, "note"), draft, error);
    draft = edit.Columns()[ColumnAt(edit, "id")];
    draft.type = "bigint";
    edit.UpdateColumn(ColumnAt(edit, "id"), draft, error);
    ColumnDraft added;
    added.name = "added";
    Check(!edit.AddColumn(added, error), "MySQL needs a type: " + error);
    added.type = "datetime";
    added.hasDefault = true;
    added.defaultExpression = "CURRENT_TIMESTAMP";
    Check(edit.AddColumn(added, error), "a new column with an expression default");
    edit.AddIndex({"items_added", {"added"}, false}, error);
    Check(edit.DropIndex("by_label", error), "an index to drop: " + error);
    Check(!edit.NeedsRebuild(), "MySQL never rebuilds");

    std::vector<DdlStep> steps;
    Check(edit.Plan(c, SqliteTableSource(), steps).success, "the plan is made");
    const std::string target = "`" + schema + "`.`items`";
    const std::string alter = "ALTER TABLE " + target;
    Check(StepWith(steps, "DROP INDEX `by_label` ON " + target) == 0, "the index dropped first, named with its table");
    Check(StepWith(steps, alter + " MODIFY COLUMN `qty` int DEFAULT 1") < steps.size() &&
              StepWith(steps, alter + " MODIFY COLUMN `label` varchar(20)") < steps.size() &&
              StepWith(steps, alter + " MODIFY COLUMN `id` bigint NOT NULL AUTO_INCREMENT") < steps.size() &&
              StepWith(steps, alter + " MODIFY COLUMN `note` varchar(200) COMMENT 'the remark'") < steps.size(),
          "MODIFY states each column whole: AUTO_INCREMENT and the comment said again");
    Check(StepWith(steps, alter + " RENAME COLUMN `note` TO `Remark`") < steps.size() &&
              StepWith(steps, alter + " ADD COLUMN `added` datetime DEFAULT CURRENT_TIMESTAMP") < steps.size() &&
              StepWith(steps, "CREATE INDEX `items_added` ON " + target) < steps.size(),
          "the rename, the new column and its index");
    Check(std::all_of(steps.begin(), steps.end(), [](const DdlStep& s) { return s.phase == DdlStep::Phase::Before; }),
          "each on its own, outside a transaction MySQL would commit anyway");
    Check(!steps.empty() && Has(steps.front().note, "commits each statement"), "and the preview says so");
    std::string message;
    Check(ApplyEdit(c, edit, message), "the edit is applied (" + message + ")");
    const UltraDbResultSet qty = Rows(c, "SELECT CAST(sum(qty) AS SIGNED) AS s FROM " + items);
    Check(qty.Size() == 1 && qty.Row(0)["s"].AsInt64() == 12, "the text became integers");
    UltraDbTableSchema after;
    UltraDb_DescribeTable(c, schema, "items", after);
    bool idCounts = false, remark = false;
    for (const auto& column : after.columns) {
        if (column.name == "id") idCounts = column.autoIncrement && column.declaredType.rfind("bigint", 0) == 0;
        if (column.name == "Remark") remark = column.comment == "the remark";
    }
    Check(idCounts, "the key is a bigint and still AUTO_INCREMENT");
    Check(remark, "the renamed column kept its comment");
    Check(UltraDb_Exec(c, "INSERT INTO " + items + " (label) VALUES (NULL)").success, "label takes NULL now");
    Check(Rows(c, "SELECT count(*) AS n FROM " + items + " WHERE `Remark` IS NULL AND added IS NOT NULL")
                  .Row(0)["n"].AsInt64() == 3,
          "the renamed column and the new one, filled by its default");

    // A conversion that fails stops there: what follows it is not run.
    UltraDb_DescribeTable(c, schema, "items", table);
    StructureEdit failing(table, SqlEngine::MySql);
    draft = failing.Columns()[ColumnAt(failing, "label")];
    draft.type = "int";                             // 'a' is no integer
    failing.UpdateColumn(ColumnAt(failing, "label"), draft, error);
    failing.RenameTable("things", error);
    Check(!ApplyEdit(c, failing, message), "a conversion the data refuses fails: " + message);
    Check(UltraDb_DescribeTable(c, schema, "items", after).success && after.columns.size() == 5 &&
              after.columns[1].declaredType == table.columns[1].declaredType,
          "nothing changed, the rename included");

    UltraDb_Exec(c, "DROP DATABASE `" + schema + "`");
    UltraDb_CloseConnection(c);
}

void TestExport() {
    Section("Exporting rows");
    Check(SqlLiteral(UltraDbValue(std::string("it's")), SqlEngine::Sqlite) == "'it''s'" &&
              SqlLiteral(UltraDbValue::Null(), SqlEngine::Postgres) == "NULL" &&
              SqlLiteral(UltraDbValue(std::vector<uint8_t>{0x00, 0xff}), SqlEngine::Sqlite) == "X'00ff'" &&
              SqlLiteral(UltraDbValue(std::vector<uint8_t>{0x00, 0xff}), SqlEngine::Postgres) == "decode('00ff', 'hex')",
          "SQL literals per engine: quotes doubled, a blob as hex");
    Check(JsonValue(UltraDbValue(std::string("a\"b\n\x01"))) == "\"a\\\"b\\n\\u0001\"" &&
              JsonValue(UltraDbValue(int64_t(3))) == "3" && JsonValue(UltraDbValue::Null()) == "null",
          "JSON values: escaped strings, bare numbers, null");
    Check(CsvField(UltraDbValue(std::vector<uint8_t>{0xab})) == "\\xab" && CsvField(UltraDbValue::Null()).empty(),
          "a CSV field: a blob as \\x hex, NULL empty");
    const UltraDbValue exact(std::string("1234.50")), zip(std::string("01067"));
    Check(WorkbookValue(exact, true).kind == WorkbookCell::Kind::Number && WorkbookValue(exact, true).number == 1234.5 &&
              WorkbookValue(exact, false).kind == WorkbookCell::Kind::Text &&
              WorkbookValue(zip, true).kind == WorkbookCell::Kind::Text &&
              WorkbookValue(UltraDbValue::Null(), true).kind == WorkbookCell::Kind::Empty &&
              WorkbookValue(UltraDbValue(int64_t(7)), false).kind == WorkbookCell::Kind::Number,
          "a workbook cell: an exact numeric a number in a number column, \"01067\" text, NULL empty");

    TempDir dir;
    ConnectionStore store;
    store.OpenInMemory();
    const std::string c = MakeSqlite(store, dir, 0);
    UltraDb_Exec(c, "CREATE TABLE goods(id INTEGER PRIMARY KEY, name TEXT NOT NULL, price REAL, data BLOB)");
    UltraDb_Exec(c, "CREATE INDEX goods_name ON goods(name)");
    UltraDb_Exec(c, "INSERT INTO goods VALUES (2, 'Äpfel, \"rot\"', 1.5, X'00ff'), (1, 'two\nlines', NULL, NULL)");
    UltraDbTableSchema goods;
    UltraDb_DescribeTable(c, "main", "goods", goods);
    std::string sql;
    Check(TableQuery(c, goods, sql).success && sql == "SELECT * FROM \"main\".\"goods\" ORDER BY \"id\"",
          "the whole table, in key order: " + sql);

    const std::string csvPath = UltraCanvas::PathToUtf8(dir.path / "goods.csv");
    ExportOptions options;
    size_t written = 0;
    UltraDbResult result = ExportQuery(c, sql, csvPath, options, written);
    Check(result.success && written == 2, "a table is exported to CSV (" + result.message + ")");
    Check(ReadFile(dir.path / "goods.csv") ==
              "id,name,price,data\n1,\"two\nlines\",,\n2,\"Äpfel, \"\"rot\"\"\",1.5,\\x00ff\n",
          "with the framework's quoting, in key order: " + ReadFile(dir.path / "goods.csv"));
    Check(!fs::exists(dir.path / "goods.csv.part"), "and nothing left beside it");

    options.csv.fieldSeparator = '\t';
    options.csv.lineEnding = UltraCanvas::CSVExportOptions::LineEnding::CRLF;
    options.csv.writeBOM = true;
    options.header = false;
    const std::string tsvPath = UltraCanvas::PathToUtf8(dir.path / "goods.tsv");
    ExportQuery(c, sql, tsvPath, options, written);
    const std::string tsv = ReadFile(dir.path / "goods.tsv");
    Check(tsv.rfind("\xEF\xBB\xBF" "1\t\"two\nlines\"\t\t\r\n", 0) == 0, "TSV: a tab, CRLF, the BOM once, no header");

    options = ExportOptions();
    options.format = ExportFormat::Json;
    const std::string jsonPath = UltraCanvas::PathToUtf8(dir.path / "goods.json");
    ExportQuery(c, sql, jsonPath, options, written);
    Check(ReadFile(dir.path / "goods.json") ==
              "[\n{\"id\": 1, \"name\": \"two\\nlines\", \"price\": null, \"data\": null},\n"
              "{\"id\": 2, \"name\": \"Äpfel, \\\"rot\\\"\", \"price\": 1.5, \"data\": \"\\\\x00ff\"}\n]\n",
          "JSON: an object per row: " + ReadFile(dir.path / "goods.json"));

    // SQL: the table's own definition, its index, the rows - and it rebuilds the table.
    options = ExportOptions();
    options.format = ExportFormat::Sql;
    options.engine = SqlEngine::Sqlite;
    UltraDb_QuoteQualifiedName(c, "", "goods", options.table);
    Check(SqlitePreamble(c, goods, options.preamble).success && Has(options.preamble, "CREATE INDEX goods_name"),
          "a SQLite table's preamble is its CREATE TABLE and its indexes");
    const std::string sqlPath = UltraCanvas::PathToUtf8(dir.path / "goods.sql");
    result = ExportQuery(c, sql, sqlPath, options, written);
    const std::string script = ReadFile(dir.path / "goods.sql");
    Check(result.success && Has(script, "BEGIN;\nINSERT INTO \"goods\" (\"id\", \"name\", \"price\", \"data\") VALUES (1, 'two\nlines', NULL, NULL);"),
          "INSERTs between BEGIN and COMMIT");
    ConnectionSpec copySpec;
    copySpec.name = "copy";
    copySpec.database = UltraCanvas::PathToUtf8(dir.path / "copy.db");
    std::string error;
    const ConnectionSpec* copy = store.Find(store.Put(copySpec, "", false, error));
    store.Register(*copy);
    const std::string cc = RegisteredName(*copy);
    std::vector<UltraDbStatementResult> statements;
    result = UltraDb_ExecBatch(cc, script, statements);
    const UltraDbResultSet copied = Rows(cc, "SELECT id, name, price, hex(data) AS data FROM goods ORDER BY id");
    Check(result.success && copied.Size() == 2 && copied.Row(1)["name"].AsString() == "Äpfel, \"rot\"" &&
              copied.Row(1)["data"].AsString() == "00FF" && copied.Row(0)["price"].IsNull() &&
              Rows(cc, "SELECT count(*) AS n FROM sqlite_master WHERE name = 'goods_name'").Row(0)["n"].AsInt64() == 1,
          "running the file rebuilds the table, its index and its rows (" + result.message + ")");
    UltraDb_CloseConnection(cc);

    // Stopped: the file that was there stays.
    {
        std::ofstream keep(dir.path / "goods.csv", std::ios::binary | std::ios::trunc);
        keep << "the old file";
    }
    std::atomic<bool> stop{true};
    result = ExportQuery(c, sql, csvPath, ExportOptions(), written, &stop);
    Check(result.code == UltraDbResultCode::Cancelled && ReadFile(dir.path / "goods.csv") == "the old file" &&
              !fs::exists(dir.path / "goods.csv.part"),
          "a stopped export leaves the file that was there, and no part of its own");

    UltraDbResultSet some;
    bool truncated = false;
    Check(ReadRows(c, sql, 1, some, truncated).success && some.Size() == 1 && truncated && some.Columns().size() == 4,
          "rows for a workbook stop at the limit, and say so");
    UltraDbResultSet console = Rows(c, "SELECT name FROM goods WHERE id = 2");
    result = ExportResult(c, console, UltraCanvas::PathToUtf8(dir.path / "one.csv"), ExportOptions(), written);
    Check(result.success && ReadFile(dir.path / "one.csv") == "name\n\"Äpfel, \"\"rot\"\"\"\n",
          "a console result is exported from memory");
    UltraDb_CloseConnection(c);

    // PostgreSQL: its literals refill a copy of the table exactly.
    const char* database = std::getenv("ULTRAFIBU_TEST_PG_DB");
    if (!database || !*database) {
        Check(std::getenv("ULTRAFIBU_TEST_PG_REQUIRED") == nullptr, "a PostgreSQL server is configured");
        return;
    }
    auto env = [](const char* name, const char* fallback) {
        const char* v = std::getenv(name);
        return std::string(v && *v ? v : fallback);
    };
    UltraDbConnectionConfig cfg;
    cfg.name = "dbadmin-export-pg";
    cfg.driver = "postgresql";
    cfg.database = database;
    cfg.host = env("ULTRAFIBU_TEST_PG_HOST", "/var/run/postgresql");
    cfg.port = std::atoi(env("ULTRAFIBU_TEST_PG_PORT", "5432").c_str());
    cfg.user = env("ULTRAFIBU_TEST_PG_USER", "");
    cfg.tls = UltraDbTls::Disable;
    const std::string pg = cfg.name;
    if (!UltraDb_RegisterConnection(cfg) || !UltraDb_OpenConnection(pg)) {
        Check(std::getenv("ULTRAFIBU_TEST_PG_REQUIRED") == nullptr, "the PostgreSQL server is reached");
        return;
    }
    const std::string schema = "dbadmin_" + ConnectionStore::NewId();
    UltraDb_Exec(pg, "CREATE SCHEMA \"" + schema + "\"");
    const std::string columns = "(id integer PRIMARY KEY, amount numeric(14,2), ok boolean, at timestamptz, "
                                "note text, data bytea)";
    UltraDb_Exec(pg, "CREATE TABLE \"" + schema + "\".src " + columns);
    UltraDb_Exec(pg, "CREATE TABLE \"" + schema + "\".dst " + columns);
    UltraDb_Exec(pg, "INSERT INTO \"" + schema + "\".src VALUES (1, 12345678901.25, true, '2026-10-21 08:30:00+00', "
                     "'it''s \\ back', '\\x00ff'), (2, NULL, false, NULL, NULL, NULL)");
    Check(Rows(pg, "SELECT count(*) AS n FROM \"" + schema + "\".src").Row(0)["n"].AsInt64() == 2,
          "the PostgreSQL source holds its two rows");
    UltraDbTableSchema src;
    UltraDb_DescribeTable(pg, schema, "src", src);
    TableQuery(pg, src, sql);
    options = ExportOptions();
    options.format = ExportFormat::Sql;
    options.engine = SqlEngine::Postgres;
    UltraDb_QuoteQualifiedName(pg, schema, "dst", options.table);
    const std::string pgPath = UltraCanvas::PathToUtf8(dir.path / "pg.sql");
    result = ExportQuery(pg, sql, pgPath, options, written);
    Check(result.success && written == 2, "a PostgreSQL table is exported to SQL (" + std::to_string(written) + " rows; " + result.message + ")");
    statements.clear();
    result = UltraDb_ExecBatch(pg, ReadFile(dir.path / "pg.sql"), statements);
    Check(result.success, "the file runs (" + result.message + ")");
    const UltraDbResultSet differ = Rows(pg, "SELECT count(*) AS n FROM ((SELECT * FROM \"" + schema +
                                                 "\".src EXCEPT SELECT * FROM \"" + schema + "\".dst) UNION ALL (SELECT * FROM \"" +
                                                 schema + "\".dst EXCEPT SELECT * FROM \"" + schema + "\".src)) AS d");
    Check(differ.Size() == 1 && differ.Row(0)["n"].AsInt64() == 0,
          "and the copy holds exactly the rows: exact numeric, boolean, time, quotes, backslash, bytea");
    UltraDb_Exec(pg, "DROP SCHEMA \"" + schema + "\" CASCADE");
    UltraDb_CloseConnection(pg);
}

void TestJobQueue() {
    Section("Job queue");
    std::mutex m;
    std::condition_variable cv;
    std::vector<int> order;
    {
        JobQueue queue;
        bool release = false;
        // The first job holds the worker until released, so the rest wait.
        queue.Post([&] {
            std::unique_lock<std::mutex> lock(m);
            cv.wait(lock, [&] { return release; });
            order.push_back(0);
        });
        for (int i = 1; i <= 3; ++i)
            queue.Post([&, i] { std::lock_guard<std::mutex> lock(m); order.push_back(i); });
        Check(queue.Outstanding() == 4, "jobs wait while one runs (" + std::to_string(queue.Outstanding()) + ")");
        {
            std::lock_guard<std::mutex> lock(m);
            release = true;
        }
        cv.notify_all();
        for (int i = 0; i < 200 && queue.Outstanding() > 0; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Check(queue.Outstanding() == 0, "and then all run");
    }
    Check(order == std::vector<int>({ 0, 1, 2, 3 }), "in the order they were posted");

    // Stop: the running job finishes, the waiting ones are dropped.
    std::atomic<int> ran{0};
    JobQueue queue;
    queue.Post([&] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); ++ran; });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    queue.Post([&] { ++ran; });
    queue.Stop();
    Check(ran == 1, "Stop waits for the running job and drops the rest");
    queue.Post([&] { ++ran; });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    Check(ran == 1 && queue.Outstanding() == 0, "a job posted after Stop is ignored");
}

} // namespace

int main() {
    std::printf("UltraDBAdmin engine tests\n\n");
    TestConnectionStoreInMemory();
    TestConnectionStoreOnDisk();
    TestCellText();
    TestPager();
    TestSqlRunner();
    TestJobQueue();
    TestEditBuffer();
    TestSqliteTable();
    TestStructureEditSqlite();
    TestStructureEditPostgres();
    TestStructureEditMysql();
    TestSqlScript();
    TestImport();
    TestExport();
    TestPagerFilters();
    TestPagerFiltersPostgres();
    TestPagerFiltersMysql();
    TestBlobsAndRowsText();
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

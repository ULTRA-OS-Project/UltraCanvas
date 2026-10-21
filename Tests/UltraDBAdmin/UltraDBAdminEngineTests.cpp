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
// a failure, not a skip).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminConnections.h"
#include "DbAdminEditBuffer.h"
#include "DbAdminFormat.h"
#include "DbAdminJobQueue.h"
#include "DbAdminSqlRunner.h"
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
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

// Tests/UltraDBAdmin/UltraDBAdminEngineTests.cpp
// UltraDBAdmin's engine without a window: the connection store (its file
// never holds a password, its vault does), cell formatting, the Browse pager
// (pages, sorting, quoting), the SQL runner (a script on a worker thread,
// Stop, history), the job queue the window's queries run on, and the staged
// row edits of Browse (the SQL they become, one transaction, all or nothing).
// SQLite throughout, in temporary folders only.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminConnections.h"
#include "DbAdminEditBuffer.h"
#include "DbAdminFormat.h"
#include "DbAdminJobQueue.h"
#include "DbAdminSqlRunner.h"
#include "DbAdminTablePager.h"

#include "UltraCanvasPathUtf8.h"
#include <UltraDatabase/UltraDatabase.h>
#include <UltraVault/UltraVault.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
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
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

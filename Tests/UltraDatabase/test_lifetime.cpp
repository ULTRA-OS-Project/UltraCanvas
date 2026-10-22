// Tests/UltraDatabase/test_lifetime.cpp
// A connection outlives every call that is using it. Re-registering a name
// (UltraDb_RegisterConnection) or closing it (UltraDb_CloseConnection) on one
// thread while another thread's statement runs on it must neither free the
// connection under that statement nor fail it: the running call finishes on
// the connection it started on, and the next call opens the new one. The
// same holds for a cursor, a prepared statement and a transaction whose
// handle is closed, or whose name is replaced, while a call on it runs - and
// a cancel by name, which must still reach a statement that began on the
// name before it changed hands.
//
// Each slow call goes back to its connection after the slow part (a second
// statement), because that is where a freed connection is touched again;
// run under -fsanitize=address for the SQLite cases. On PostgreSQL a freed
// connection is a closed socket and the call fails outright.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "test_pg.h"

#include <UltraDatabase/UltraDatabase.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace {

// About a second of work for SQLite: long enough that the other thread acts
// while it runs, short enough for a test.
const std::string kSlowCount =
    "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 3000000) "
    "SELECT count(*) AS n FROM n";
const int64_t kSlowCountResult = 3000000;
// The slow count, then a statement that needs the connection again.
const std::string kSlowThenMore = kSlowCount + "; SELECT 'after' AS step";

std::string Register(const std::string& name) {
    UltraDbConnectionConfig cfg;
    cfg.name = name;
    cfg.database = ":memory:";
    UltraDb_CloseConnection(name);
    REQUIRE(UltraDb_RegisterConnection(cfg).success);
    return name;
}

void Pause(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

} // namespace

TEST(query_survives_reregister_and_close) {
    const std::string c = Register("lifetime-query");
    REQUIRE(UltraDb_Exec(c, "CREATE TABLE marker(v INTEGER)").success);

    UltraDbResult result;
    UltraDbResultSet rows;
    std::atomic<bool> done{false};
    std::thread worker([&] { result = UltraDb_Query(c, kSlowThenMore, rows); done = true; });
    Pause(150);
    REQUIRE(!done);   // the test means something only while the count runs
    // While the count runs: the name now means a new, empty database.
    UltraDbConnectionConfig fresh;
    fresh.name = c;
    fresh.database = ":memory:";
    REQUIRE(UltraDb_RegisterConnection(fresh).success);
    UltraDbResultSet probe;
    REQUIRE(!UltraDb_Query(c, "SELECT v FROM marker", probe).success);   // the new connection
    worker.join();
    REQUIRE(result.success);
    REQUIRE(rows.Size() == 1 && rows.Row(0)["step"].AsString() == "after");

    done = false;
    std::thread again([&] { result = UltraDb_Query(c, kSlowThenMore, rows); done = true; });
    Pause(150);
    REQUIRE(!done);
    REQUIRE(UltraDb_CloseConnection(c).success);
    REQUIRE(UltraDb_Query(c, "SELECT 1", probe).code == UltraDbResultCode::ConnectionNotFound);
    again.join();
    REQUIRE(result.success);
    REQUIRE(rows.Row(0)["step"].AsString() == "after");
}

TEST(script_survives_close) {
    const std::string c = Register("lifetime-batch");
    std::vector<UltraDbStatementResult> statements;
    UltraDbResult result;
    std::atomic<bool> done{false};
    std::thread worker([&] { result = UltraDb_ExecBatch(c, kSlowThenMore, statements); done = true; });
    Pause(150);
    REQUIRE(!done);
    REQUIRE(UltraDb_CloseConnection(c).success);
    worker.join();
    REQUIRE(result.success);
    REQUIRE(statements.size() == 2);
    REQUIRE(statements[0].rows.Row(0)["n"].AsInt64() == kSlowCountResult);
}

TEST(cursor_fetch_survives_close_of_cursor_and_connection) {
    const std::string c = Register("lifetime-cursor");
    UltraDbResult error;
    UltraDbHandle cursor = UltraDb_OpenCursor(c, kSlowCount, {}, &error);
    REQUIRE(cursor != UltraDbInvalidHandle);

    UltraDbResult result;
    UltraDbResultSet page;
    bool exhausted = false;
    std::atomic<bool> done{false};
    // The count is computed by the first fetch: it runs for a second.
    std::thread worker([&] { result = UltraDb_FetchRows(cursor, 10, page, exhausted); done = true; });
    Pause(150);
    REQUIRE(!done);
    REQUIRE(UltraDb_CloseCursor(cursor).success);
    REQUIRE(UltraDb_CloseConnection(c).success);
    worker.join();
    REQUIRE(result.success);
    REQUIRE(page.Size() == 1 && page.Row(0)["n"].AsInt64() == kSlowCountResult);
    // The handle is gone for any later call.
    REQUIRE(UltraDb_FetchRows(cursor, 10, page, exhausted).code == UltraDbResultCode::InvalidArgument);
}

TEST(prepared_statement_survives_finalize_and_close) {
    const std::string c = Register("lifetime-prepared");
    UltraDbResult error;
    UltraDbHandle statement = UltraDb_Prepare(c, kSlowCount, &error);
    REQUIRE(statement != UltraDbInvalidHandle);

    UltraDbResult result;
    UltraDbResultSet rows;
    std::atomic<bool> done{false};
    std::thread worker([&] { result = UltraDb_QueryPrepared(statement, {}, rows); done = true; });
    Pause(150);
    REQUIRE(!done);
    REQUIRE(UltraDb_Finalize(statement).success);
    REQUIRE(UltraDb_CloseConnection(c).success);
    worker.join();
    REQUIRE(result.success);
    REQUIRE(rows.Row(0)["n"].AsInt64() == kSlowCountResult);
    REQUIRE(UltraDb_QueryPrepared(statement, {}, rows).code == UltraDbResultCode::InvalidArgument);
}

TEST(transaction_survives_reregister_and_close) {
    const std::string c = Register("lifetime-tx");
    REQUIRE(UltraDb_Exec(c, "CREATE TABLE t(v INTEGER)").success);
    UltraDbResult error;
    UltraDbHandle tx = UltraDb_Begin(c, &error);
    REQUIRE(tx != UltraDbInvalidHandle);

    UltraDbResult result;
    std::atomic<bool> done{false};
    std::thread worker([&] { result = UltraDb_ExecInTx(tx, kSlowThenMore); done = true; });
    Pause(150);
    REQUIRE(!done);
    UltraDbConnectionConfig fresh;
    fresh.name = c;
    fresh.database = ":memory:";
    REQUIRE(UltraDb_RegisterConnection(fresh).success);
    REQUIRE(UltraDb_CloseConnection(c).success);
    worker.join();
    REQUIRE(result.success);
    // The transaction is still on the connection it began on, and finishes there.
    REQUIRE(UltraDb_ExecInTx(tx, "INSERT INTO t(v) VALUES(1)").success);
    UltraDbResultSet rows;
    REQUIRE(UltraDb_QueryInTx(tx, "SELECT count(*) AS n FROM t", {}, rows).success);
    REQUIRE(rows.Row(0)["n"].AsInt64() == 1);
    REQUIRE(UltraDb_Commit(tx).success);
}

TEST(pg_query_survives_close_and_reregister) {
    const std::string c = "lifetime-pg";
    if (!ultradb_test::OpenPgTestConnection(c)) return;
    UltraDbResult result;
    UltraDbResultSet rows;
    std::atomic<bool> done{false};
    std::thread worker([&] {
        result = UltraDb_Query(c, "SELECT pg_sleep(1), 7 AS n", rows);
        done = true;
    });
    Pause(250);
    REQUIRE(!done);
    // Closes the name and registers it again, as a settings change does.
    REQUIRE(ultradb_test::OpenPgTestConnection(c));
    UltraDbResultSet probe;
    REQUIRE(UltraDb_Query(c, "SELECT 1 AS one", probe).success);   // the new session, meanwhile
    worker.join();
    REQUIRE(result.success);
    REQUIRE(rows.Size() == 1 && rows.Row(0)["n"].AsInt64() == 7);
    UltraDb_CloseConnection(c);
}

namespace {

// Minutes of work, unless it is cancelled.
const std::string kCancellable =
    "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 1000000000) "
    "SELECT count(*) AS n FROM n";

// Runs `kCancellable` on `c`, does `change` to the name while it runs, then
// cancels by name. The statement must stop, although the name no longer
// leads to the connection it runs on.
void CancelAfter(const std::string& c, const std::function<void()>& change) {
    UltraDbResult result;
    UltraDbResultSet rows;
    std::atomic<bool> done{false};
    const auto start = std::chrono::steady_clock::now();
    std::thread worker([&] { result = UltraDb_Query(c, kCancellable, rows); done = true; });
    Pause(150);
    REQUIRE(!done);
    change();
    REQUIRE(UltraDb_CancelQuery(c).success);
    worker.join();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    REQUIRE(result.code == UltraDbResultCode::Cancelled);
    REQUIRE(seconds < 10.0);
}

} // namespace

TEST(cancel_reaches_a_statement_on_a_replaced_connection) {
    const std::string c = Register("lifetime-cancel-replaced");
    CancelAfter(c, [&] {
        UltraDbConnectionConfig fresh;
        fresh.name = c;
        fresh.database = ":memory:";
        REQUIRE(UltraDb_RegisterConnection(fresh).success);
    });
    UltraDbResultSet rows;
    REQUIRE(UltraDb_Query(c, "SELECT 1 AS one", rows).success);   // the new one is untouched
    UltraDb_CloseConnection(c);
}

TEST(cancel_reaches_a_statement_on_a_closed_connection) {
    const std::string c = Register("lifetime-cancel-closed");
    CancelAfter(c, [&] { REQUIRE(UltraDb_CloseConnection(c).success); });
    // Nothing runs under the name any more: unknown again.
    REQUIRE(UltraDb_CancelQuery(c).code == UltraDbResultCode::ConnectionNotFound);
}

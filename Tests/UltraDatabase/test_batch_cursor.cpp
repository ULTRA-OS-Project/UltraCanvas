// Tests/UltraDatabase/test_batch_cursor.cpp
// Stage 1 of UltraDBAdmin (Docs/Research/UltraDBAdminDesignProposal.md, G3 G4
// G6): every statement of a script with its own result, a forward-only cursor
// read a page at a time, and cancelling a running statement from another
// thread - against SQLite always and a live PostgreSQL server where one is
// configured (test_pg.h). Also the PostgreSQL statement splitter, which needs
// no server.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "test_pg.h"

#include <UltraDatabase/UltraDatabase.h>

// Not a public header: the splitter and rewriter are driver-internal.
#include "../../UltraCanvas/core/UltraDatabase/UltraDatabaseInternal.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string SqliteConn(const std::string& tag) {
    UltraDbConnectionConfig cfg;
    cfg.name = "batch-" + tag;
    cfg.database = ":memory:";
    UltraDb_CloseConnection(cfg.name);
    REQUIRE(UltraDb_RegisterConnection(cfg).success);
    return cfg.name;
}

using Clock = std::chrono::steady_clock;

double SecondsSince(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// Runs `sql` on `connection` in a thread, cancels it after `delayMs`, and
// returns how the query ended and how long it took.
UltraDbResult RunAndCancel(const std::string& connection, const std::string& sql,
                           int delayMs, double& seconds) {
    UltraDbResult outcome;
    std::atomic<bool> started{false};
    const Clock::time_point start = Clock::now();
    std::thread worker([&] {
        started = true;
        UltraDbResultSet rs;
        outcome = UltraDb_Query(connection, sql, rs);
    });
    while (!started) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    UltraDbResult cancelled = UltraDb_CancelQuery(connection);
    worker.join();
    seconds = SecondsSince(start);
    REQUIRE(cancelled.success);
    return outcome;
}

} // namespace

// ===== SCRIPTS (SQLite) =====

TEST(sqlite_exec_batch_reports_every_statement) {
    const std::string c = SqliteConn("all");
    std::vector<UltraDbStatementResult> out;
    UltraDbResult r = UltraDb_ExecBatch(c,
        "CREATE TABLE t(a INTEGER);\n"
        "  INSERT INTO t VALUES(1);\n"
        "INSERT INTO t VALUES(2), (3);\n"
        "SELECT a FROM t ORDER BY a;\n"
        "-- a trailing comment is not a statement\n", out);
    REQUIRE(r.success);
    REQUIRE_EQ(out.size(), (size_t)4);
    REQUIRE_EQ(out[0].sql, std::string("CREATE TABLE t(a INTEGER)"));
    REQUIRE_EQ(out[1].sql, std::string("INSERT INTO t VALUES(1)"));
    REQUIRE(!out[1].returnsRows);
    REQUIRE_EQ(out[2].result.affectedRows, (int64_t)2);
    REQUIRE(out[3].returnsRows);
    REQUIRE_EQ(out[3].rows.Size(), (size_t)3);
    REQUIRE_EQ(out[3].rows.Row(2)["a"].AsInt64(), (int64_t)3);
    UltraDb_CloseConnection(c);
}

// sqlite3_changes() keeps the count of the last INSERT, UPDATE or DELETE
// until the next one, so a statement that changes no rows - DDL, a SELECT, an
// UPDATE that matched nothing - must not report the count of the one before.
TEST(sqlite_exec_batch_counts_only_what_each_statement_changed) {
    const std::string c = SqliteConn("changes");
    std::vector<UltraDbStatementResult> out;
    UltraDbResult r = UltraDb_ExecBatch(c,
        "CREATE TABLE t(a INTEGER);\n"
        "CREATE TABLE log(a INTEGER);\n"
        "CREATE TRIGGER tr AFTER INSERT ON t BEGIN INSERT INTO log VALUES (new.a); END;\n"
        "INSERT INTO t VALUES(1), (2);\n"
        "CREATE VIEW v AS SELECT a FROM t;\n"
        "SELECT a FROM t;\n"
        "UPDATE t SET a = 5 WHERE a > 10;\n"
        "DROP VIEW v;\n"
        "DELETE FROM t WHERE a = 1;\n", out);
    REQUIRE(r.success);
    REQUIRE_EQ(out.size(), (size_t)9);
    REQUIRE_EQ(out[3].result.affectedRows, (int64_t)2);   // its own rows, not the trigger's
    REQUIRE_EQ(out[3].result.lastInsertId, (int64_t)2);
    REQUIRE_EQ(out[4].result.affectedRows, (int64_t)0);   // CREATE VIEW
    REQUIRE_EQ(out[4].result.lastInsertId, (int64_t)0);
    REQUIRE_EQ(out[5].result.affectedRows, (int64_t)0);   // SELECT
    REQUIRE_EQ(out[6].result.affectedRows, (int64_t)0);   // an UPDATE that matched nothing
    REQUIRE_EQ(out[7].result.affectedRows, (int64_t)0);   // DROP VIEW
    REQUIRE_EQ(out[8].result.affectedRows, (int64_t)1);
    // UltraDb_Exec reports the same way.
    REQUIRE(UltraDb_Exec(c, "INSERT INTO t VALUES (7)").affectedRows == 1);
    REQUIRE(UltraDb_Exec(c, "CREATE INDEX t_a ON t(a)").affectedRows == 0);
    UltraDb_CloseConnection(c);
}

TEST(sqlite_exec_batch_stops_at_the_first_failure) {
    const std::string c = SqliteConn("stop");
    REQUIRE(UltraDb_Exec(c, "CREATE TABLE t(a INTEGER)").success);
    std::vector<UltraDbStatementResult> out;
    UltraDbResult r = UltraDb_ExecBatch(c,
        "INSERT INTO t VALUES(1); SELECT nosuch FROM t; INSERT INTO t VALUES(2);", out);
    REQUIRE(!r.success);
    REQUIRE_EQ(out.size(), (size_t)2);              // the third never ran
    REQUIRE(out[0].result.success);
    REQUIRE(!out[1].result.success);
    REQUIRE_EQ(out[1].sql, std::string("SELECT nosuch FROM t; INSERT INTO t VALUES(2)"));
    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query(c, "SELECT count(*) AS n FROM t", rs).success);
    REQUIRE_EQ(rs.Row(0)["n"].AsInt64(), (int64_t)1);
    UltraDb_CloseConnection(c);
}

// ===== CURSORS (SQLite) =====

TEST(sqlite_cursor_pages_through_a_result) {
    const std::string c = SqliteConn("cursor");
    REQUIRE(UltraDb_Exec(c,
        "CREATE TABLE t(a INTEGER); "
        "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 25) "
        "INSERT INTO t SELECT i FROM n;").success);
    REQUIRE(UltraDb_Exec(c, "CREATE TABLE log(x INTEGER)").success);

    UltraDbResult err;
    UltraDbHandle cur = UltraDb_OpenCursor(c, "SELECT a FROM t WHERE a > ? ORDER BY a", { 0 }, &err);
    REQUIRE(cur != UltraDbInvalidHandle);

    UltraDbResultSet page;
    bool exhausted = true;
    REQUIRE(UltraDb_FetchRows(cur, 10, page, exhausted).success);
    REQUIRE_EQ(page.Size(), (size_t)10);
    REQUIRE(!exhausted);
    REQUIRE_EQ(page.Row(0)["a"].AsInt64(), (int64_t)1);
    REQUIRE_EQ(page.ColumnInfo().size(), (size_t)1);

    // The connection stays usable between pages.
    REQUIRE(UltraDb_Exec(c, "INSERT INTO log VALUES(?)", { 1 }).success);

    REQUIRE(UltraDb_FetchRows(cur, 10, page, exhausted).success);
    REQUIRE_EQ(page.Row(0)["a"].AsInt64(), (int64_t)11);
    REQUIRE(!exhausted);
    REQUIRE(UltraDb_FetchRows(cur, 10, page, exhausted).success);
    REQUIRE_EQ(page.Size(), (size_t)5);
    REQUIRE(exhausted);
    REQUIRE(UltraDb_FetchRows(cur, 10, page, exhausted).success);
    REQUIRE_EQ(page.Size(), (size_t)0);
    REQUIRE(exhausted);
    REQUIRE_EQ(page.Columns().size(), (size_t)1);   // still says what it would hold

    REQUIRE(UltraDb_CloseCursor(cur).success);
    REQUIRE(UltraDb_FetchRows(cur, 10, page, exhausted).code == UltraDbResultCode::InvalidArgument);
    REQUIRE(UltraDb_CloseCursor(cur).code == UltraDbResultCode::InvalidArgument);
    UltraDb_CloseConnection(c);
}

TEST(sqlite_cursor_refuses_what_it_cannot_page) {
    const std::string c = SqliteConn("cursor-bad");
    UltraDbResult err;
    REQUIRE(UltraDb_OpenCursor(c, "SELECT 1; SELECT 2", {}, &err) == UltraDbInvalidHandle);
    REQUIRE(err.code == UltraDbResultCode::InvalidArgument);
    REQUIRE(UltraDb_OpenCursor(c, "SELECT nosuch FROM nowhere", {}, &err) == UltraDbInvalidHandle);
    REQUIRE(!err.success);
    REQUIRE(UltraDb_OpenCursor("no-such-connection", "SELECT 1", {}, &err) == UltraDbInvalidHandle);
    REQUIRE(err.code == UltraDbResultCode::ConnectionNotFound);

    UltraDbHandle cur = UltraDb_OpenCursor(c, "SELECT 1 AS one", {}, &err);
    REQUIRE(cur != UltraDbInvalidHandle);
    UltraDbResultSet page;
    bool exhausted = false;
    REQUIRE(UltraDb_FetchRows(cur, 0, page, exhausted).code == UltraDbResultCode::InvalidArgument);
    UltraDb_CloseCursor(cur);
    UltraDb_CloseConnection(c);
}

// ===== CANCELLING (SQLite) =====

TEST(sqlite_cancel_stops_a_running_query) {
    const std::string c = SqliteConn("cancel");
    REQUIRE(UltraDb_OpenConnection(c).success);
    double seconds = 0;
    // Counts to a billion - minutes, unless it is stopped.
    UltraDbResult r = RunAndCancel(c,
        "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 1000000000) "
        "SELECT count(*) FROM n", 200, seconds);
    REQUIRE(!r.success);
    REQUIRE(r.code == UltraDbResultCode::Cancelled);
    REQUIRE(seconds < 10.0);
    // And the connection works after it.
    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query(c, "SELECT 1 AS one", rs).success);
    UltraDb_CloseConnection(c);
}

TEST(cancel_with_nothing_to_cancel) {
    REQUIRE(UltraDb_CancelQuery("no-such-connection").code == UltraDbResultCode::ConnectionNotFound);
    const std::string c = SqliteConn("cancel-idle");
    REQUIRE(UltraDb_CancelQuery(c).success);        // never opened: nothing runs
    REQUIRE(UltraDb_OpenConnection(c).success);
    REQUIRE(UltraDb_CancelQuery(c).success);        // open, idle
    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query(c, "SELECT 1 AS one", rs).success);   // and nothing was left pending
    UltraDb_CloseConnection(c);
}

// ===== POSTGRESQL STATEMENT SPLITTING (no server) =====

namespace {
std::vector<std::string> Split(const std::string& sql) {
    return ultradb_internal::PostgresAnweisungen(sql);
}
} // namespace

TEST(pg_split_plain_statements) {
    auto s = Split("SELECT 1;  SELECT 2 ;\n\nSELECT 3");
    REQUIRE_EQ(s.size(), (size_t)3);
    REQUIRE_EQ(s[0], std::string("SELECT 1"));
    REQUIRE_EQ(s[1], std::string("SELECT 2"));
    REQUIRE_EQ(s[2], std::string("SELECT 3"));
    REQUIRE_EQ(Split("").size(), (size_t)0);
    REQUIRE_EQ(Split(" ;; -- only a comment\n /* and another */ ;").size(), (size_t)0);
}

TEST(pg_split_ignores_semicolons_in_text_and_comments) {
    auto s = Split("SELECT 'a;b', \"c;d\", E'it\\'s; here' -- x;y\n; /* p; /* nested; */ q; */ SELECT 2");
    REQUIRE_EQ(s.size(), (size_t)2);
    REQUIRE(s[0].find("E'it\\'s; here'") != std::string::npos);
    REQUIRE(s[1].find("SELECT 2") != std::string::npos);
}

TEST(pg_split_keeps_function_bodies_whole) {
    auto s = Split(
        "CREATE FUNCTION f() RETURNS int LANGUAGE plpgsql AS $$ BEGIN RETURN 1; END $$;"
        "CREATE FUNCTION g() RETURNS int LANGUAGE sql BEGIN ATOMIC SELECT 1; SELECT CASE WHEN true THEN 2 END; END;"
        "SELECT f(), g()");
    REQUIRE_EQ(s.size(), (size_t)3);
    REQUIRE(s[0].find("RETURN 1; END $$") != std::string::npos);
    REQUIRE(s[1].find("SELECT 1; SELECT CASE") != std::string::npos);
    REQUIRE_EQ(s[2], std::string("SELECT f(), g()"));
    // Outside a CREATE, BEGIN is a transaction and END its commit.
    auto t = Split("BEGIN; UPDATE x SET a = CASE WHEN b THEN 1 END; END;");
    REQUIRE_EQ(t.size(), (size_t)3);
}

TEST(pg_placeholders_leave_escape_strings_alone) {
    // E'...' takes \' as a quote; the rewriter used to end the string there
    // and number the ? that follows inside it.
    REQUIRE_EQ(ultradb_internal::PostgresPlatzhalter("SELECT E'it\\'s ?', ?"),
               std::string("SELECT E'it\\'s ?', $1"));
    // An identifier ending in e before a literal is not an escape string.
    REQUIRE_EQ(ultradb_internal::PostgresPlatzhalter("SELECT type'x', ?"),
               std::string("SELECT type'x', $1"));
}

// ===== POSTGRESQL (live server) =====

TEST(pg_exec_batch_reports_every_statement) {
    const std::string c = "batch-pg";
    if (!ultradb_test::OpenPgTestConnection(c)) return;
    REQUIRE(UltraDb_Exec(c, "SET client_min_messages = warning").success);
    // The function first: a BEGIN ATOMIC body depends on the table it reads,
    // and PostgreSQL refuses to drop the table while it stands.
    const char* cleanup = "DROP FUNCTION IF EXISTS ultradb_batch_f(); DROP TABLE IF EXISTS ultradb_batch_t";
    REQUIRE(UltraDb_Exec(c, cleanup).success);
    std::vector<UltraDbStatementResult> out;
    UltraDbResult r = UltraDb_ExecBatch(c,
        "DROP TABLE IF EXISTS ultradb_batch_t;"
        "CREATE TABLE ultradb_batch_t(a integer, s text);"
        "INSERT INTO ultradb_batch_t VALUES (1, 'x;y'), (2, E'it\\'s');"
        "CREATE OR REPLACE FUNCTION ultradb_batch_f() RETURNS int LANGUAGE sql"
        "  BEGIN ATOMIC SELECT 1; SELECT count(*)::int FROM ultradb_batch_t; END;"
        "SELECT a, s, ultradb_batch_f() AS n FROM ultradb_batch_t ORDER BY a;"
        "SELECT nosuch;"
        "DROP TABLE ultradb_batch_t;", out);
    REQUIRE(!r.success);
    REQUIRE_EQ(out.size(), (size_t)6);              // the last DROP never ran
    REQUIRE_EQ(out[2].result.affectedRows, (int64_t)2);
    REQUIRE(out[4].returnsRows);
    REQUIRE_EQ(out[4].rows.Size(), (size_t)2);
    REQUIRE_EQ(out[4].rows.Row(0)["s"].AsString(), std::string("x;y"));
    REQUIRE_EQ(out[4].rows.Row(1)["s"].AsString(), std::string("it's"));
    REQUIRE_EQ(out[4].rows.Row(0)["n"].AsInt64(), (int64_t)2);
    REQUIRE_EQ(out[4].rows.ColumnInfo()[0].sourceTable, std::string("ultradb_batch_t"));
    REQUIRE(!out[5].result.success);
    REQUIRE(UltraDb_Exec(c, cleanup).success);
    UltraDb_CloseConnection(c);
}

TEST(pg_cursor_pages_outside_and_inside_a_transaction) {
    const std::string c = "cursor-pg";
    if (!ultradb_test::OpenPgTestConnection(c)) return;

    // Outside a transaction: WITH HOLD, and the connection is free between pages.
    UltraDbResult err;
    UltraDbHandle cur = UltraDb_OpenCursor(c,
        "SELECT i FROM generate_series(1, ?::int) AS g(i) ORDER BY i", { 25 }, &err);
    if (cur == UltraDbInvalidHandle) std::printf("         %s\n", err.message.c_str());
    REQUIRE(cur != UltraDbInvalidHandle);
    UltraDbResultSet page;
    bool exhausted = true;
    REQUIRE(UltraDb_FetchRows(cur, 10, page, exhausted).success);
    REQUIRE_EQ(page.Size(), (size_t)10);
    REQUIRE(!exhausted);
    REQUIRE_EQ(page.ColumnInfo()[0].declaredType, std::string("integer"));
    UltraDbResultSet other;
    REQUIRE(UltraDb_Query(c, "SELECT 42 AS answer", other).success);
    REQUIRE(UltraDb_FetchRows(cur, 10, page, exhausted).success);
    REQUIRE_EQ(page.Row(0)["i"].AsInt64(), (int64_t)11);
    REQUIRE(UltraDb_FetchRows(cur, 10, page, exhausted).success);
    REQUIRE_EQ(page.Size(), (size_t)5);
    REQUIRE(exhausted);
    REQUIRE(UltraDb_CloseCursor(cur).success);

    // Inside one: WITHOUT HOLD; it ends with the transaction, and closing it
    // afterwards is harmless.
    UltraDbHandle tx = UltraDb_Begin(c, &err);
    REQUIRE(tx != UltraDbInvalidHandle);
    cur = UltraDb_OpenCursor(c, "SELECT i FROM generate_series(1, 5) AS g(i)", {}, &err);
    REQUIRE(cur != UltraDbInvalidHandle);
    REQUIRE(UltraDb_FetchRows(cur, 2, page, exhausted).success);
    REQUIRE_EQ(page.Size(), (size_t)2);
    REQUIRE(UltraDb_Commit(tx).success);
    REQUIRE(!UltraDb_FetchRows(cur, 2, page, exhausted).success);   // gone with the transaction
    REQUIRE(UltraDb_CloseCursor(cur).success);
    REQUIRE(UltraDb_Query(c, "SELECT 1 AS one", other).success);
    UltraDb_CloseConnection(c);
}

TEST(pg_cancel_stops_a_running_query) {
    const std::string c = "cancel-pg";
    if (!ultradb_test::OpenPgTestConnection(c)) return;
    double seconds = 0;
    UltraDbResult r = RunAndCancel(c, "SELECT pg_sleep(30)", 300, seconds);
    REQUIRE(!r.success);
    REQUIRE(r.code == UltraDbResultCode::Cancelled);
    REQUIRE_EQ(r.sqlState, std::string("57014"));
    REQUIRE(seconds < 15.0);
    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query(c, "SELECT 1 AS one", rs).success);
    UltraDb_CloseConnection(c);
}

// Tests/UltraDatabase/test_ultradatabase.cpp
// Stage 1 coverage: connection registry, parameterized queries, value
// coercion, prepared statements, transactions, migrations and error mapping.
// Each test uses its own in-memory database (a distinct connection name), so
// tests are independent.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraDatabase/UltraDatabase.h>

#include <string>
#include <vector>

namespace {

// Register a fresh in-memory connection under a unique name and return it.
std::string FreshConn(const std::string& tag) {
    UltraDbConnectionConfig cfg;
    cfg.name = "test-" + tag;
    cfg.driver = "sqlite";
    cfg.database = ":memory:";
    UltraDb_CloseConnection(cfg.name);  // in case a prior run left it
    UltraDbResult r = UltraDb_RegisterConnection(cfg);
    REQUIRE(r.success);
    return cfg.name;
}

} // namespace

TEST(version_and_drivers) {
    REQUIRE(!UltraDatabase_GetVersion().empty());
    auto drivers = UltraDatabase_GetSupportedDrivers();
    bool hasSqlite = false;
    for (auto& d : drivers) if (d == "sqlite") hasSqlite = true;
    REQUIRE(hasSqlite);
}

TEST(register_and_info) {
    std::string c = FreshConn("info");
    REQUIRE(UltraDb_HasConnection(c));

    UltraDbConnectionInfo info;
    REQUIRE(UltraDb_GetConnectionInfo(c, info).success);
    REQUIRE_EQ(info.driver, std::string("sqlite"));
    REQUIRE(!info.open);  // not opened until first use

    REQUIRE(UltraDb_OpenConnection(c).success);
    REQUIRE(UltraDb_GetConnectionInfo(c, info).success);
    REQUIRE(info.open);
}

TEST(unknown_connection_errors) {
    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query("does-not-exist", "SELECT 1", rs);
    REQUIRE(!r.success);
    REQUIRE(r.code == UltraDbResultCode::ConnectionNotFound);
}

TEST(create_insert_select) {
    std::string c = FreshConn("crud");
    REQUIRE(UltraDb_Exec(c,
        "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT, score REAL)").success);

    UltraDbResult ins = UltraDb_Exec(c,
        "INSERT INTO t(name, score) VALUES(?, ?)", { "alice", 9.5 });
    REQUIRE(ins.success);
    REQUIRE_EQ(ins.affectedRows, (int64_t)1);
    REQUIRE_EQ(ins.lastInsertId, (int64_t)1);

    UltraDb_Exec(c, "INSERT INTO t(name, score) VALUES(?, ?)", { "bob", 3.0 });

    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query(c,
        "SELECT id, name, score FROM t WHERE score > ? ORDER BY id",
        { 5.0 }, rs).success);
    REQUIRE_EQ(rs.Size(), (size_t)1);
    REQUIRE_EQ(rs.Row(0)["name"].AsString(), std::string("alice"));
    REQUIRE_EQ(rs.Row(0)["id"].AsInt64(), (int64_t)1);
    REQUIRE(rs.Row(0)["score"].AsDouble() > 9.4);
}

TEST(parameter_binding_is_safe) {
    std::string c = FreshConn("inject");
    UltraDb_Exec(c, "CREATE TABLE users(name TEXT)");
    UltraDb_Exec(c, "INSERT INTO users(name) VALUES(?)", { "real" });

    // A classic injection string must be treated as a literal value, matching
    // nothing, and must NOT drop the table.
    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query(c, "SELECT name FROM users WHERE name = ?",
                          { "'; DROP TABLE users;--" }, rs).success);
    REQUIRE_EQ(rs.Size(), (size_t)0);

    UltraDbResultSet all;
    REQUIRE(UltraDb_Query(c, "SELECT name FROM users", all).success);
    REQUIRE_EQ(all.Size(), (size_t)1);  // table intact
}

TEST(value_coercion_and_null) {
    std::string c = FreshConn("coerce");
    UltraDb_Exec(c, "CREATE TABLE v(i INTEGER, t TEXT, r REAL, n TEXT)");
    UltraDb_Exec(c, "INSERT INTO v(i, t, r, n) VALUES(?, ?, ?, ?)",
                 { 42, "123", 2.5, nullptr });

    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query(c, "SELECT i, t, r, n FROM v", rs).success);
    const auto& row = rs.Row(0);
    REQUIRE_EQ(row["i"].AsInt64(), (int64_t)42);
    REQUIRE_EQ(row["i"].AsString(), std::string("42"));
    REQUIRE_EQ(row["t"].AsInt64(), (int64_t)123);   // text -> int coercion
    REQUIRE(row["r"].AsDouble() > 2.4);
    REQUIRE(row["n"].IsNull());
    REQUIRE_EQ(row["missing"].IsNull(), true);       // unknown column -> null
}

TEST(blob_round_trip) {
    std::string c = FreshConn("blob");
    UltraDb_Exec(c, "CREATE TABLE b(data BLOB)");
    std::vector<uint8_t> bytes = {0, 1, 2, 255, 254, 0, 42};
    REQUIRE(UltraDb_Exec(c, "INSERT INTO b(data) VALUES(?)", { bytes }).success);

    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query(c, "SELECT data FROM b", rs).success);
    auto got = rs.Row(0)["data"].AsBlob();
    REQUIRE_EQ(got.size(), bytes.size());
    REQUIRE(got == bytes);
}

TEST(prepared_statement_reuse) {
    std::string c = FreshConn("prep");
    UltraDb_Exec(c, "CREATE TABLE p(n INTEGER)");

    UltraDbResult err;
    UltraDbHandle stmt = UltraDb_Prepare(c, "INSERT INTO p(n) VALUES(?)", &err);
    REQUIRE(stmt != UltraDbInvalidHandle);
    for (int i = 1; i <= 5; ++i)
        REQUIRE(UltraDb_ExecPrepared(stmt, { i }).success);
    REQUIRE(UltraDb_Finalize(stmt).success);

    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query(c, "SELECT COUNT(*) AS c, SUM(n) AS s FROM p", rs).success);
    REQUIRE_EQ(rs.Row(0)["c"].AsInt64(), (int64_t)5);
    REQUIRE_EQ(rs.Row(0)["s"].AsInt64(), (int64_t)15);
}

TEST(transaction_commit) {
    std::string c = FreshConn("txok");
    UltraDb_Exec(c, "CREATE TABLE tx(n INTEGER)");

    UltraDbHandle tx = UltraDb_Begin(c);
    REQUIRE(tx != UltraDbInvalidHandle);
    REQUIRE(UltraDb_ExecInTx(tx, "INSERT INTO tx(n) VALUES(?)", { 1 }).success);
    REQUIRE(UltraDb_ExecInTx(tx, "INSERT INTO tx(n) VALUES(?)", { 2 }).success);
    REQUIRE(UltraDb_Commit(tx).success);

    UltraDbResultSet rs;
    UltraDb_Query(c, "SELECT COUNT(*) AS c FROM tx", rs);
    REQUIRE_EQ(rs.Row(0)["c"].AsInt64(), (int64_t)2);
}

TEST(transaction_rollback) {
    std::string c = FreshConn("txroll");
    UltraDb_Exec(c, "CREATE TABLE tx(n INTEGER)");
    UltraDb_Exec(c, "INSERT INTO tx(n) VALUES(100)");

    UltraDbHandle tx = UltraDb_Begin(c);
    REQUIRE(UltraDb_ExecInTx(tx, "INSERT INTO tx(n) VALUES(?)", { 200 }).success);
    REQUIRE(UltraDb_Rollback(tx).success);

    UltraDbResultSet rs;
    UltraDb_Query(c, "SELECT COUNT(*) AS c FROM tx", rs);
    REQUIRE_EQ(rs.Row(0)["c"].AsInt64(), (int64_t)1);  // only the pre-tx row
}

TEST(constraint_violation_maps) {
    std::string c = FreshConn("constraint");
    UltraDb_Exec(c, "CREATE TABLE u(id INTEGER PRIMARY KEY)");
    REQUIRE(UltraDb_Exec(c, "INSERT INTO u(id) VALUES(1)").success);
    UltraDbResult dup = UltraDb_Exec(c, "INSERT INTO u(id) VALUES(1)");
    REQUIRE(!dup.success);
    REQUIRE(dup.code == UltraDbResultCode::ConstraintViolation);
}

TEST(batch_exec_multi_statement) {
    std::string c = FreshConn("batch");
    REQUIRE(UltraDb_Exec(c,
        "CREATE TABLE a(x INTEGER);"
        "CREATE TABLE b(y INTEGER);"
        "INSERT INTO a(x) VALUES(1);"
        "INSERT INTO b(y) VALUES(2);").success);

    UltraDbResultSet ra, rb;
    UltraDb_Query(c, "SELECT x FROM a", ra);
    UltraDb_Query(c, "SELECT y FROM b", rb);
    REQUIRE_EQ(ra.Row(0)["x"].AsInt64(), (int64_t)1);
    REQUIRE_EQ(rb.Row(0)["y"].AsInt64(), (int64_t)2);
}

TEST(migrations_apply_and_are_idempotent) {
    std::string c = FreshConn("migrate");

    std::vector<UltraDbMigration> steps = {
        { 1, "create messages", "CREATE TABLE messages(uid INTEGER PRIMARY KEY, subject TEXT)" },
        { 2, "add flags",       "ALTER TABLE messages ADD COLUMN flags INTEGER DEFAULT 0" },
        { 3, "seed",            "INSERT INTO messages(uid, subject) VALUES(1, 'hi')" },
    };

    REQUIRE(UltraDb_Migrate(c, steps).success);

    int version = 0;
    REQUIRE(UltraDb_GetSchemaVersion(c, version).success);
    REQUIRE_EQ(version, 3);

    // The schema and seed row exist.
    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query(c, "SELECT uid, subject, flags FROM messages", rs).success);
    REQUIRE_EQ(rs.Size(), (size_t)1);
    REQUIRE_EQ(rs.Row(0)["subject"].AsString(), std::string("hi"));

    // Re-running the same migration set is a no-op (no duplicate seed row).
    REQUIRE(UltraDb_Migrate(c, steps).success);
    UltraDbResultSet rs2;
    UltraDb_Query(c, "SELECT COUNT(*) AS c FROM messages", rs2);
    REQUIRE_EQ(rs2.Row(0)["c"].AsInt64(), (int64_t)1);

    // A new higher-version step applies on top.
    steps.push_back({ 4, "seed2", "INSERT INTO messages(uid, subject) VALUES(2, 'yo')" });
    REQUIRE(UltraDb_Migrate(c, steps).success);
    UltraDb_GetSchemaVersion(c, version);
    REQUIRE_EQ(version, 4);
    UltraDbResultSet rs3;
    UltraDb_Query(c, "SELECT COUNT(*) AS c FROM messages", rs3);
    REQUIRE_EQ(rs3.Row(0)["c"].AsInt64(), (int64_t)2);
}

TEST(close_connection) {
    std::string c = FreshConn("close");
    REQUIRE(UltraDb_HasConnection(c));
    REQUIRE(UltraDb_CloseConnection(c).success);
    REQUIRE(!UltraDb_HasConnection(c));
    REQUIRE(UltraDb_CloseConnection(c).code == UltraDbResultCode::ConnectionNotFound);
}

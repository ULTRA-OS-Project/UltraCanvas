// Tests/UltraDatabase/test_server.cpp
// Server administration (UltraDatabaseServer.h): SQLite has no server and
// says so; PostgreSQL and MySQL / MariaDB list their sessions with the asking
// one among them, filter their settings in any case with % and _ typed taken
// literally, report the sizes of their databases and of a schema's tables,
// and stop a running query from another session - not the asking session
// itself, and a session that is not there is "not stopped", not an error.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "test_mysql.h"
#include "test_pg.h"

#include <UltraDatabase/UltraDatabase.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace {

// `value` in the row whose `key` column is `name`; "" when there is none.
std::string Find(const UltraDbResultSet& rows, const char* key, const std::string& name, const char* column) {
    for (const auto& row : rows)
        if (row[key].AsString() == name) return row[column].AsString();
    return std::string();
}

// Sessions, settings, sizes and a stopped query on `c`, with `sleeper` a
// second connection to the same server and `sleepSql` a query that runs until
// it is stopped. `table` is a table in `schema`, `setting` a setting's name
// and `fragment` part of it in another case.
void CheckServer(const std::string& c, const std::string& sleeper, const std::string& schema,
                 const std::string& table, const std::string& setting, const std::string& fragment,
                 const std::string& sleepSql) {
    UltraDbResultSet rows;
    REQUIRE(UltraDb_ServerSessions(c, rows).success);
    const auto& columns = rows.Columns();
    for (const char* name : { "id", "user", "database", "application", "client", "state", "running", "waiting",
                              "query", "this_session" })
        REQUIRE(std::find(columns.begin(), columns.end(), name) != columns.end());
    int64_t self = 0;
    for (const auto& row : rows)
        if (row["this_session"].AsBool()) self = row["id"].AsInt64();
    REQUIRE(self != 0);

    REQUIRE(UltraDb_ServerSettings(c, fragment, rows).success);
    REQUIRE(!Find(rows, "name", setting, "value").empty());
    REQUIRE(UltraDb_ServerSettings(c, "zz%zz", rows).success);
    REQUIRE_EQ(rows.Size(), (size_t)0);          // % typed is a %
    REQUIRE(UltraDb_ServerSettings(c, "", rows).success);
    REQUIRE(rows.Size() > 50);

    REQUIRE(UltraDb_ServerSizes(c, schema, rows).success);
    bool database = false;
    for (const auto& row : rows) database = database || row["kind"].AsString() == "database";
    REQUIRE(database);
    REQUIRE(!Find(rows, "name", schema + "." + table, "bytes").empty());

    std::string sql;
    REQUIRE(UltraDb_StopSessionSql(c, 42, false, sql).success);
    REQUIRE(!sql.empty());
    bool stopped = true;
    REQUIRE(UltraDb_StopSession(c, self, false, stopped).code == UltraDbResultCode::InvalidArgument);
    REQUIRE(UltraDb_StopSession(c, 2000000000, true, stopped).success);
    REQUIRE(!stopped);

    // A query that runs until stopped, on its own session, found in the list
    // and stopped from here.
    std::atomic<bool> done{false};
    UltraDbResult slept;
    std::thread worker([&] {
        UltraDbResultSet out;
        slept = UltraDb_Query(sleeper, sleepSql, {}, out);
        done = true;
    });
    int64_t target = 0;
    for (int i = 0; i < 100 && target == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        REQUIRE(UltraDb_ServerSessions(c, rows).success);
        for (const auto& row : rows)
            if (row["query"].AsString() == sleepSql && row["state"].AsString() == "active") target = row["id"].AsInt64();
    }
    REQUIRE(target != 0);
    REQUIRE(UltraDb_StopSession(c, target, false, stopped).success);
    REQUIRE(stopped);
    worker.join();
    REQUIRE(done);
    UltraDbResultSet one;
    REQUIRE(UltraDb_Query(sleeper, "SELECT 1 AS one", {}, one).success);   // the session stayed
}

} // namespace

TEST(server_sqlite_has_none) {
    UltraDbConnectionConfig cfg;
    cfg.name = "server-sqlite";
    cfg.driver = "sqlite";
    cfg.database = ":memory:";
    UltraDb_CloseConnection(cfg.name);
    REQUIRE(UltraDb_RegisterConnection(cfg).success);
    UltraDbResultSet rows;
    REQUIRE(UltraDb_ServerSessions(cfg.name, rows).code == UltraDbResultCode::Unsupported);
    REQUIRE(UltraDb_ServerSettings(cfg.name, "", rows).code == UltraDbResultCode::Unsupported);
    bool stopped = true;
    REQUIRE(UltraDb_StopSession(cfg.name, 1, false, stopped).code == UltraDbResultCode::Unsupported);
    REQUIRE(!stopped);
    REQUIRE(UltraDb_ServerSizes("no-such-connection", "", rows).code == UltraDbResultCode::ConnectionNotFound);
    UltraDb_CloseConnection(cfg.name);
}

TEST(server_postgres) {
    const std::string c = "server-pg", sleeper = "server-pg-sleeper";
    if (!ultradb_test::OpenPgTestConnection(c) || !ultradb_test::OpenPgTestConnection(sleeper)) return;
    REQUIRE(UltraDb_Exec(c, "DROP TABLE IF EXISTS ultradb_server_t").success);
    REQUIRE(UltraDb_Exec(c, "CREATE TABLE ultradb_server_t(id integer PRIMARY KEY)").success);
    CheckServer(c, sleeper, "public", "ultradb_server_t", "work_mem", "WORK_MEM", "SELECT pg_sleep(30)");
    REQUIRE(UltraDb_Exec(c, "DROP TABLE ultradb_server_t").success);
    UltraDb_CloseConnection(sleeper);
    UltraDb_CloseConnection(c);
}

TEST(server_mysql) {
    const std::string c = "server-my", sleeper = "server-my-sleeper";
    if (!ultradb_test::OpenMysqlTestConnection(c) || !ultradb_test::OpenMysqlTestConnection(sleeper)) return;
    const std::string db = ultradb_test::MysqlEnv("ULTRADB_TEST_MYSQL_DB", "");
    REQUIRE(UltraDb_Exec(c, "DROP TABLE IF EXISTS ultradb_server_t").success);
    REQUIRE(UltraDb_Exec(c, "CREATE TABLE ultradb_server_t(id INT PRIMARY KEY) ENGINE=InnoDB").success);
    CheckServer(c, sleeper, db, "ultradb_server_t", "max_connections", "MAX_CONN", "SELECT SLEEP(30)");
    REQUIRE(UltraDb_Exec(c, "DROP TABLE ultradb_server_t").success);
    UltraDb_CloseConnection(sleeper);
    UltraDb_CloseConnection(c);
}

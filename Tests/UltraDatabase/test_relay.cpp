// Tests/UltraDatabase/test_relay.cpp
// The relay (UltraDatabaseRelay.h), with a transport of the test's own that
// hands each request straight to an UltraDbRelayServer in the same process:
//   - the wire format keeps every value's type (a double that is whole, NaN,
//     the largest integer, a blob, text with quotes and line breaks);
//   - a "relay" connection works as the engine behind it does - queries with
//     bound values, scripts, a transaction, cursors a page at a time,
//     introspection - and a SQLite file serves as that engine here;
//   - the server offers only its connections and only to tokens it knows:
//     an unknown token, a connection not granted, a read-only grant that
//     refuses to write, the lists each user is shown;
//   - its limits: too many rows at once, too many requests, too many
//     sessions; and the audit log, with who ran each statement;
//   - the statement time-out: a query, a script and a cursor's page that run
//     too long stopped, saying why, and the connection usable after;
//   - a sign-in of the program's own (SetSignIn): offered, and its tickets
//     known beside the tokens;
//   - on PostgreSQL, a query stopped through the relay while it runs.
// Version: 0.2.0 - the time-out, the program's own sign-in
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "test_pg.h"

#include <UltraDatabase/UltraDatabase.h>

#include "../../UltraCanvas/core/UltraDatabase/UltraDatabaseRelayJson.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

// A server in this process, and a transport that calls it.
struct InProcess {
    UltraDbRelayServer server;
    std::mutex m;
    std::vector<UltraDbRelayAuditEntry> audit;
};

InProcess* g_relay = nullptr;

class DirectTransport : public IUltraDbRelayTransport {
public:
    explicit DirectTransport(UltraDbRelayServer& server) : server_(server), session_(server.OpenSession("test")) {}
    ~DirectTransport() override { server_.CloseSession(session_); }
    UltraDbResult Call(int64_t id, const std::string& request, std::string& response) override {
        response = server_.Handle(session_, request);
        // Every answer begins with its request's id.
        if (UltraDb_RelayAnswerId(response) != id)
            return UltraDbResult::Error(UltraDbResultCode::Internal, "an answer for another request");
        return UltraDbResult::Ok();
    }

private:
    UltraDbRelayServer& server_;
    uint64_t session_;
};

void UseRelay(InProcess& relay) {
    g_relay = &relay;
    UltraDb_SetRelayConnector([](const std::string& url, UltraDbResult& error) -> std::unique_ptr<IUltraDbRelayTransport> {
        if (url != "inproc://relay" || !g_relay) {
            error = UltraDbResult::Error(UltraDbResultCode::ConnectionFailed, "no relay at " + url);
            return nullptr;
        }
        return std::make_unique<DirectTransport>(g_relay->server);
    });
}

std::string TempFile(const char* name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    return path.string();
}

UltraDbConnectionConfig Through(const std::string& name, const std::string& offered, const std::string& token) {
    UltraDbConnectionConfig cfg;
    cfg.name = name;
    cfg.driver = "relay";
    cfg.host = "inproc://relay";
    cfg.database = offered;
    cfg.options["relay_token"] = token;
    UltraDb_CloseConnection(name);
    return cfg;
}

// "shop" (a SQLite file) offered to anna, who may write, and to ben, who
// may read; "other" offered to nobody.
void Offer(InProcess& relay, const std::string& file) {
    UltraDbConnectionConfig shop;
    shop.driver = "sqlite";
    shop.database = file;
    relay.server.AddConnection("shop", shop);
    UltraDbConnectionConfig other = shop;
    relay.server.AddConnection("other", other);
    UltraDbRelayUser anna;
    anna.name = "anna";
    anna.tokenSha256 = UltraDb_RelayTokenHash("anna-token");
    anna.access["shop"] = UltraDbRelayAccess::Write;
    relay.server.AddUser(anna);
    UltraDbRelayUser ben;
    ben.name = "ben";
    ben.tokenSha256 = UltraDb_RelayTokenHash("ben-token");
    ben.access["shop"] = UltraDbRelayAccess::Read;
    ben.access["other"] = UltraDbRelayAccess::Denied;
    relay.server.AddUser(ben);
    relay.server.SetAudit([&relay](const UltraDbRelayAuditEntry& entry) {
        std::lock_guard<std::mutex> lock(relay.m);
        relay.audit.push_back(entry);
    });
}

} // namespace

TEST(relay_wire_keeps_types) {
    using ultradb_relay::Json;
    const std::vector<UltraDbValue> values = {
        UltraDbValue(), UltraDbValue(true), UltraDbValue(std::numeric_limits<int64_t>::max()), UltraDbValue(2.0),
        UltraDbValue(0.1), UltraDbValue(std::nan("")), UltraDbValue(std::string("say \"hi\"\nthen \xC3\xA4")),
        UltraDbValue(std::vector<uint8_t>{ 0, 1, 254, 255 }),
    };
    Json list = ultradb_relay::FromParams(values);
    Json back;
    std::string error;
    REQUIRE(Json::Parse(list.Dump(), back, error));
    const UltraDbParams read = ultradb_relay::ToParams(&back);
    REQUIRE_EQ(read.size(), values.size());
    REQUIRE(read[0].IsNull());
    REQUIRE(read[1].Type() == UltraDbType::Boolean && read[1].AsBool());
    REQUIRE(read[2].Type() == UltraDbType::Int && read[2].AsInt64() == std::numeric_limits<int64_t>::max());
    REQUIRE(read[3].Type() == UltraDbType::Double && read[3].AsDouble() == 2.0);   // whole, still a double
    REQUIRE(read[4].AsDouble() == 0.1);
    REQUIRE(read[5].Type() == UltraDbType::Double && std::isnan(read[5].AsDouble()));
    REQUIRE_EQ(read[6].AsString(), values[6].AsString());
    REQUIRE(read[7].Type() == UltraDbType::Blob && read[7].AsBlob() == values[7].AsBlob());
    REQUIRE(!Json::Parse("{\"a\":", back, error));
    REQUIRE(Json::Parse("\"\\u00e4\\ud83d\\ude00\"", back, error) && back.text == "\xC3\xA4\xF0\x9F\x98\x80");
}

TEST(relay_runs_what_the_engine_runs) {
    InProcess relay;
    const std::string file = TempFile("ultradb-relay-shop.db");
    Offer(relay, file);
    UseRelay(relay);

    const UltraDbConnectionConfig cfg = Through("via-relay", "shop", "anna-token");
    REQUIRE(UltraDb_RegisterConnection(cfg).success);
    REQUIRE(UltraDb_OpenConnection("via-relay").success);
    REQUIRE(UltraDb_Exec("via-relay", "CREATE TABLE items(id INTEGER PRIMARY KEY, name TEXT, price REAL, photo BLOB)").success);
    for (int i = 1; i <= 25; ++i)
        REQUIRE(UltraDb_Exec("via-relay", "INSERT INTO items(name, price, photo) VALUES (?, ?, ?)",
                             { UltraDbValue("item " + std::to_string(i)), UltraDbValue(i * 1.5),
                               UltraDbValue(std::vector<uint8_t>{ static_cast<uint8_t>(i), 0 }) })
                    .success);
    UltraDbResultSet rows;
    REQUIRE(UltraDb_Query("via-relay", "SELECT id, name, price, photo FROM items WHERE id = ?", { UltraDbValue(4) }, rows)
                .success);
    REQUIRE_EQ(rows.Size(), (size_t)1);
    REQUIRE_EQ(rows.Row(0)["name"].AsString(), std::string("item 4"));
    REQUIRE(rows.Row(0)["price"].AsDouble() == 6.0);
    REQUIRE(rows.Row(0)["photo"].AsBlob() == std::vector<uint8_t>({ 4, 0 }));

    // A script, statement by statement.
    std::vector<UltraDbStatementResult> statements;
    REQUIRE(UltraDb_ExecBatch("via-relay", "UPDATE items SET price = 0 WHERE id = 1; SELECT count(*) AS n FROM items",
                              statements).success);
    REQUIRE_EQ(statements.size(), (size_t)2);
    REQUIRE(statements[1].returnsRows && statements[1].rows.Row(0)["n"].AsInt64() == 25);

    // A transaction, rolled back.
    UltraDbResult error;
    const UltraDbHandle tx = UltraDb_Begin("via-relay", &error);
    REQUIRE(tx != UltraDbInvalidHandle);
    REQUIRE(UltraDb_ExecInTx(tx, "DELETE FROM items").success);
    REQUIRE(UltraDb_Rollback(tx).success);
    REQUIRE(UltraDb_Query("via-relay", "SELECT count(*) AS n FROM items", rows).success && rows.Row(0)["n"].AsInt64() == 25);

    // A cursor, a page at a time.
    const UltraDbHandle cursor = UltraDb_OpenCursor("via-relay", "SELECT id FROM items ORDER BY id", {}, &error);
    REQUIRE(cursor != UltraDbInvalidHandle);
    size_t read = 0;
    bool exhausted = false;
    while (!exhausted) {
        UltraDbResultSet page;
        REQUIRE(UltraDb_FetchRows(cursor, 10, page, exhausted).success);
        read += page.Size();
    }
    REQUIRE_EQ(read, (size_t)25);
    REQUIRE(UltraDb_CloseCursor(cursor).success);

    // Introspection.
    std::vector<std::string> schemas;
    REQUIRE(UltraDb_ListSchemas("via-relay", schemas).success && !schemas.empty());
    std::vector<UltraDbObjectSummary> objects;
    REQUIRE(UltraDb_ListObjects("via-relay", "main", objects).success && objects.size() == 1);
    UltraDbTableSchema table;
    REQUIRE(UltraDb_DescribeTable("via-relay", "main", "items", table).success);
    REQUIRE(table.columns.size() == 4 && table.primaryKey == std::vector<std::string>({ "id" }));
    std::string quoted;
    REQUIRE(UltraDb_QuoteIdentifier("via-relay", "a\"b", quoted).success && quoted == "\"a\"\"b\"");

    // A failure comes back as the engine said it.
    const UltraDbResult bad = UltraDb_Exec("via-relay", "SELECT nonsense FROM nowhere");
    REQUIRE(!bad.success && bad.message.find("nowhere") != std::string::npos);
    // SQLite has no server to administer, through a relay neither.
    REQUIRE(UltraDb_ServerStatus("via-relay", rows).code == UltraDbResultCode::Unsupported);

    // Every statement in the audit log, with its user.
    {
        std::lock_guard<std::mutex> lock(relay.m);
        bool created = false;
        for (const auto& entry : relay.audit)
            created = created || (entry.user == "anna" && entry.connection == "shop" &&
                                  entry.sql.find("CREATE TABLE items") != std::string::npos);
        REQUIRE(created);
    }
    UltraDb_CloseConnection("via-relay");
    UltraDb_SetRelayConnector(nullptr);
    g_relay = nullptr;
    std::filesystem::remove(file);
}

TEST(relay_offers_only_what_it_grants) {
    InProcess relay;
    const std::string file = TempFile("ultradb-relay-grants.db");
    Offer(relay, file);
    UseRelay(relay);

    std::vector<UltraDbRelayConnection> offered;
    REQUIRE(UltraDb_RelayConnections("inproc://relay", "anna-token", offered).success);
    REQUIRE(offered.size() == 1 && offered[0].name == "shop" && offered[0].engine == "sqlite" && !offered[0].readOnly);
    REQUIRE(UltraDb_RelayConnections("inproc://relay", "ben-token", offered).success);
    REQUIRE(offered.size() == 1 && offered[0].readOnly);
    REQUIRE(UltraDb_RelayConnections("inproc://relay", "nobody", offered).code == UltraDbResultCode::CredentialsUnavailable);
    REQUIRE(UltraDb_RelayConnections("inproc://elsewhere", "anna-token", offered).code == UltraDbResultCode::ConnectionFailed);

    // anna makes the table; ben reads it and cannot write.
    REQUIRE(UltraDb_RegisterConnection(Through("relay-anna", "shop", "anna-token")).success);
    REQUIRE(UltraDb_Exec("relay-anna", "CREATE TABLE t(x INTEGER)").success);
    REQUIRE(UltraDb_RegisterConnection(Through("relay-ben", "shop", "ben-token")).success);
    UltraDbResultSet rows;
    REQUIRE(UltraDb_Query("relay-ben", "SELECT count(*) AS n FROM t", rows).success);
    REQUIRE(!UltraDb_Exec("relay-ben", "INSERT INTO t VALUES (1)").success);
    // A connection not granted is not there, as one that does not exist.
    REQUIRE(UltraDb_RegisterConnection(Through("relay-other", "other", "ben-token")).success);
    REQUIRE(UltraDb_OpenConnection("relay-other").code == UltraDbResultCode::ConnectionNotFound);
    REQUIRE(UltraDb_RegisterConnection(Through("relay-none", "nothing", "anna-token")).success);
    REQUIRE(UltraDb_OpenConnection("relay-none").code == UltraDbResultCode::ConnectionNotFound);

    // A request before the token, and one the server does not know.
    const uint64_t session = relay.server.OpenSession("raw");
    std::string answer = relay.server.Handle(session, "{\"id\":7,\"op\":\"open\",\"connection\":\"shop\"}");
    REQUIRE(answer.find("\"ok\":false") != std::string::npos && answer.find("CredentialsUnavailable") != std::string::npos);
    answer = relay.server.Handle(session, "not json");
    REQUIRE(answer.find("InvalidArgument") != std::string::npos);
    relay.server.CloseSession(session);
    for (const char* name : { "relay-anna", "relay-ben", "relay-other", "relay-none" }) UltraDb_CloseConnection(name);
    UltraDb_SetRelayConnector(nullptr);
    g_relay = nullptr;
    std::filesystem::remove(file);
}

TEST(relay_keeps_its_limits) {
    InProcess relay;
    const std::string file = TempFile("ultradb-relay-limits.db");
    Offer(relay, file);
    UltraDbRelayLimits limits;
    limits.rowsPerResult = 5;
    limits.rowsPerPage = 3;
    limits.requestsPerMinute = 40;
    limits.sessionsPerUser = 2;
    relay.server.SetLimits(limits);
    UseRelay(relay);

    REQUIRE(UltraDb_RegisterConnection(Through("relay-limits", "shop", "anna-token")).success);
    REQUIRE(UltraDb_Exec("relay-limits", "CREATE TABLE t(x INTEGER)").success);
    REQUIRE(UltraDb_Exec("relay-limits", "INSERT INTO t VALUES (1),(2),(3),(4),(5),(6),(7)").success);
    UltraDbResultSet rows;
    const UltraDbResult many = UltraDb_Query("relay-limits", "SELECT x FROM t", rows);
    REQUIRE(!many.success && many.message.find("cursor") != std::string::npos);   // 7 rows: more than 5
    UltraDbResult error;
    const UltraDbHandle cursor = UltraDb_OpenCursor("relay-limits", "SELECT x FROM t", {}, &error);
    REQUIRE(cursor != UltraDbInvalidHandle);
    bool exhausted = false;
    REQUIRE(UltraDb_FetchRows(cursor, 100, rows, exhausted).success && rows.Size() == 3);   // a page of 3 at most
    UltraDb_CloseCursor(cursor);

    // A third session for anna is one too many.
    std::vector<UltraDbRelayConnection> offered;
    REQUIRE(UltraDb_RegisterConnection(Through("relay-limits-2", "shop", "anna-token")).success);
    REQUIRE(UltraDb_OpenConnection("relay-limits-2").success);
    REQUIRE(UltraDb_RelayConnections("inproc://relay", "anna-token", offered).code == UltraDbResultCode::Busy);

    // Requests past the minute's share are refused.
    UltraDbResult last;
    for (int i = 0; i < 60 && last.success; ++i) last = UltraDb_Exec("relay-limits", "SELECT 1");
    REQUIRE(last.code == UltraDbResultCode::Busy);
    UltraDb_CloseConnection("relay-limits");
    UltraDb_CloseConnection("relay-limits-2");
    UltraDb_SetRelayConnector(nullptr);
    g_relay = nullptr;
    std::filesystem::remove(file);
}

TEST(relay_stops_what_runs_too_long) {
    InProcess relay;
    const std::string file = TempFile("ultradb-relay-timeout.db");
    Offer(relay, file);
    UltraDbRelayLimits limits;
    limits.statementTimeoutSeconds = 1;
    relay.server.SetLimits(limits);
    UseRelay(relay);
    REQUIRE(UltraDb_RegisterConnection(Through("relay-slow", "shop", "anna-token")).success);
    // Counts for ever: only the time-out ends it.
    const std::string forever = "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM c) SELECT count(*) FROM c";
    UltraDbResultSet rows;
    auto start = std::chrono::steady_clock::now();
    const UltraDbResult query = UltraDb_Query("relay-slow", forever, rows);
    REQUIRE(!query.success && query.code == UltraDbResultCode::Cancelled);
    REQUIRE(query.message.find("1 s") != std::string::npos);
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::seconds(10));
    // A script: the statement that ran too long says so.
    std::vector<UltraDbStatementResult> statements;
    const UltraDbResult script = UltraDb_ExecBatch("relay-slow", "SELECT 1; " + forever, statements);
    REQUIRE(!script.success && statements.size() == 2 && statements[0].result.success);
    REQUIRE(statements[1].result.code == UltraDbResultCode::Cancelled &&
            statements[1].result.message.find("1 s") != std::string::npos);
    // A cursor whose first page never comes.
    UltraDbResult error;
    const UltraDbHandle cursor = UltraDb_OpenCursor("relay-slow", forever, {}, &error);
    if (cursor != UltraDbInvalidHandle) {
        bool exhausted = false;
        const UltraDbResult page = UltraDb_FetchRows(cursor, 10, rows, exhausted);
        REQUIRE(!page.success && page.code == UltraDbResultCode::Cancelled);
        UltraDb_CloseCursor(cursor);
    } else {
        REQUIRE(error.code == UltraDbResultCode::Cancelled);
    }
    // What is quick still runs, on the same connection.
    REQUIRE(UltraDb_Query("relay-slow", "SELECT 42 AS n", rows).success && rows.Size() == 1);
    // Without a time-out nothing is stopped by the relay (a quick check only).
    limits.statementTimeoutSeconds = 0;
    relay.server.SetLimits(limits);
    REQUIRE(UltraDb_Query("relay-slow", "SELECT 1", rows).success);
    UltraDb_CloseConnection("relay-slow");
    UltraDb_SetRelayConnector(nullptr);
    g_relay = nullptr;
    std::filesystem::remove(file);
}

TEST(relay_knows_its_own_sign_in) {
    InProcess relay;
    const std::string file = TempFile("ultradb-relay-signin.db");
    Offer(relay, file);
    UseRelay(relay);
    std::string label = "?";
    REQUIRE(UltraDb_RelaySignInOffer("inproc://relay", label).success && label.empty());
    // The program's sign-in: a ticket it handed out stands for carla.
    relay.server.SetSignIn("Example SSO", [](const std::string& token, UltraDbRelayUser& user) {
        if (token != "ticket-for-carla") return false;
        user.name = "carla";
        user.access["shop"] = UltraDbRelayAccess::Read;
        return true;
    });
    REQUIRE(UltraDb_RelaySignInOffer("inproc://relay", label).success && label == "Example SSO");
    std::vector<UltraDbRelayConnection> offered;
    REQUIRE(UltraDb_RelayConnections("inproc://relay", "ticket-for-carla", offered).success);
    REQUIRE(offered.size() == 1 && offered[0].name == "shop" && offered[0].readOnly);
    REQUIRE(UltraDb_RelayConnections("inproc://relay", "ticket-for-nobody", offered).code ==
            UltraDbResultCode::CredentialsUnavailable);
    // The users' own tokens still count, and come first.
    REQUIRE(UltraDb_RelayConnections("inproc://relay", "anna-token", offered).success && !offered[0].readOnly);
    // A user with no token of its own is not reached by an empty one.
    REQUIRE(UltraDb_RelayConnections("inproc://relay", "", offered).code == UltraDbResultCode::CredentialsUnavailable);
    UltraDb_SetRelayConnector(nullptr);
    g_relay = nullptr;
    std::filesystem::remove(file);
}

TEST(relay_cancels_on_postgres) {
    // The server's connection is the tests' PostgreSQL.
    if (!ultradb_test::OpenPgTestConnection("relay-pg-probe")) return;
    UltraDb_CloseConnection("relay-pg-probe");
    InProcess relay;
    UltraDbConnectionConfig pg;
    pg.driver = "postgresql";
    pg.database = ultradb_test::PgEnv("ULTRAFIBU_TEST_PG_DB", "");
    pg.host = ultradb_test::PgEnv("ULTRAFIBU_TEST_PG_HOST", "/var/run/postgresql");
    pg.port = std::atoi(ultradb_test::PgEnv("ULTRAFIBU_TEST_PG_PORT", "5432"));
    pg.user = ultradb_test::PgEnv("ULTRAFIBU_TEST_PG_USER", "");
    pg.tls = UltraDbTls::Disable;
    relay.server.AddConnection("pg", pg);
    UltraDbRelayUser anna;
    anna.name = "anna";
    anna.tokenSha256 = UltraDb_RelayTokenHash("anna-token");
    anna.access["pg"] = UltraDbRelayAccess::Write;
    relay.server.AddUser(anna);
    UseRelay(relay);

    REQUIRE(UltraDb_RegisterConnection(Through("relay-pg", "pg", "anna-token")).success);
    UltraDbResultSet rows;
    REQUIRE(UltraDb_Query("relay-pg", "SELECT 41 + 1 AS n", rows).success && rows.Row(0)["n"].AsInt64() == 42);
    REQUIRE(UltraDb_ServerStatus("relay-pg", rows).success && rows.Size() >= 7);
    std::string quoted;
    REQUIRE(UltraDb_QuoteIdentifier("relay-pg", "Mixed", quoted).success && quoted == "\"Mixed\"");
    // A query that runs until it is stopped, stopped through the relay.
    std::atomic<bool> done{ false };
    UltraDbResult slept;
    const auto start = std::chrono::steady_clock::now();
    std::thread worker([&] {
        UltraDbResultSet out;
        slept = UltraDb_Query("relay-pg", "SELECT pg_sleep(20)", {}, out);
        done = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    REQUIRE(UltraDb_CancelQuery("relay-pg").success);
    worker.join();
    REQUIRE(done && !slept.success);
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::seconds(10));
    UltraDb_CloseConnection("relay-pg");
    UltraDb_SetRelayConnector(nullptr);
    g_relay = nullptr;
}

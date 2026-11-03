// Tests/UltraDatabase/test_observer.cpp
// Statement observers (UltraDb_AddStatementObserver): every statement through
// the public calls is reported once it has ended - its kind, its text, how
// many values were bound (never the values), its rows, whether it ran in a
// transaction, its time and its outcome - and nothing more once the observer
// is removed. An observer may run statements itself.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraDatabase/UltraDatabase.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

using Kind = UltraDbStatementEvent::Kind;

std::string ObservedConn(const std::string& tag) {
    UltraDbConnectionConfig cfg;
    cfg.name = "observer-" + tag;
    cfg.database = ":memory:";
    UltraDb_CloseConnection(cfg.name);
    REQUIRE(UltraDb_RegisterConnection(cfg).success);
    return cfg.name;
}

// The events of one connection, as reported; other tests' connections are
// left out, so a test sees only what it ran.
struct Recorder {
    std::string connection;
    std::mutex mutex;
    std::vector<UltraDbStatementEvent> events;

    std::vector<UltraDbStatementEvent> Take() {
        std::lock_guard<std::mutex> lk(mutex);
        std::vector<UltraDbStatementEvent> out;
        out.swap(events);
        return out;
    }
};

int Watch(const std::shared_ptr<Recorder>& recorder) {
    return UltraDb_AddStatementObserver([recorder](const UltraDbStatementEvent& event) {
        if (event.connection != recorder->connection) return;
        std::lock_guard<std::mutex> lk(recorder->mutex);
        recorder->events.push_back(event);
    });
}

} // namespace

TEST(observer_reports_queries_execs_and_their_outcome) {
    auto recorder = std::make_shared<Recorder>();
    recorder->connection = ObservedConn("basic");
    const std::string& c = recorder->connection;
    const int id = Watch(recorder);
    REQUIRE(id > 0);

    REQUIRE(UltraDb_Exec(c, "CREATE TABLE t(id INTEGER PRIMARY KEY, secret TEXT)").success);
    REQUIRE(UltraDb_Exec(c, "INSERT INTO t(secret) VALUES (?), (?)", {"hunter2", "swordfish"}).success);
    UltraDbResultSet rows;
    REQUIRE(UltraDb_Query(c, "SELECT * FROM t WHERE id > ?", {0}, rows).success);
    REQUIRE(!UltraDb_Exec(c, "INSERT INTO nowhere VALUES (1)").success);

    const auto events = recorder->Take();
    REQUIRE_EQ(events.size(), size_t(4));
    REQUIRE(events[0].kind == Kind::Exec);
    REQUIRE_EQ(events[0].sql, std::string("CREATE TABLE t(id INTEGER PRIMARY KEY, secret TEXT)"));
    REQUIRE(events[1].kind == Kind::Exec);
    REQUIRE_EQ(events[1].boundValues, size_t(2));
    REQUIRE_EQ(events[1].result.affectedRows, int64_t(2));
    // The values themselves are nowhere in the event.
    REQUIRE(events[1].sql.find("hunter2") == std::string::npos);
    REQUIRE(events[2].kind == Kind::Query);
    REQUIRE_EQ(events[2].rows, size_t(2));
    REQUIRE(events[2].seconds >= 0.0);
    REQUIRE(!events[2].inTransaction);
    REQUIRE(!events[3].result.success);
    REQUIRE(!events[3].result.message.empty());

    UltraDb_RemoveStatementObserver(id);
    REQUIRE(UltraDb_Exec(c, "DELETE FROM t").success);
    REQUIRE(recorder->Take().empty());
}

TEST(observer_reports_a_transaction_a_script_a_cursor_and_introspection) {
    auto recorder = std::make_shared<Recorder>();
    recorder->connection = ObservedConn("kinds");
    const std::string& c = recorder->connection;
    const int id = Watch(recorder);

    UltraDbResult error;
    UltraDbHandle tx = UltraDb_Begin(c, &error);
    REQUIRE(tx != UltraDbInvalidHandle);
    REQUIRE(UltraDb_ExecInTx(tx, "CREATE TABLE n(v INTEGER)").success);
    REQUIRE(UltraDb_ExecInTx(tx, "INSERT INTO n VALUES (1), (2), (3)").success);
    REQUIRE(UltraDb_Commit(tx).success);
    auto events = recorder->Take();
    REQUIRE_EQ(events.size(), size_t(4));
    REQUIRE(events[0].kind == Kind::Transaction);
    REQUIRE(events[1].kind == Kind::Exec);
    REQUIRE(events[1].inTransaction);
    REQUIRE(events[3].kind == Kind::Transaction);
    REQUIRE_EQ(events[3].sql, std::string("COMMIT"));

    std::vector<UltraDbStatementResult> results;
    REQUIRE(UltraDb_ExecBatch(c, "SELECT v FROM n; UPDATE n SET v = v + 1", results).success);
    events = recorder->Take();
    REQUIRE_EQ(events.size(), size_t(2));
    REQUIRE(events[0].kind == Kind::Script);
    REQUIRE_EQ(events[0].rows, size_t(3));
    REQUIRE(events[0].seconds < 0.0);   // a script's statement has no time of its own
    REQUIRE_EQ(events[1].result.affectedRows, int64_t(3));

    UltraDbHandle cursor = UltraDb_OpenCursor(c, "SELECT v FROM n WHERE v > ?", {1});
    REQUIRE(cursor != UltraDbInvalidHandle);
    UltraDbResultSet page;
    bool exhausted = false;
    REQUIRE(UltraDb_FetchRows(cursor, 10, page, exhausted).success);
    REQUIRE(UltraDb_CloseCursor(cursor).success);
    events = recorder->Take();
    REQUIRE_EQ(events.size(), size_t(1));       // the open, not its pages
    REQUIRE(events[0].kind == Kind::Cursor);
    REQUIRE_EQ(events[0].boundValues, size_t(1));

    UltraDbTableSchema table;
    REQUIRE(UltraDb_DescribeTable(c, "main", "n", table).success);
    events = recorder->Take();
    REQUIRE(!events.empty());
    REQUIRE(events.back().kind == Kind::Introspection);
    REQUIRE(events.back().sql.find("main.n") != std::string::npos);

    UltraDbHandle prepared = UltraDb_Prepare(c, "SELECT v FROM n WHERE v = ?");
    REQUIRE(prepared != UltraDbInvalidHandle);
    REQUIRE(UltraDb_QueryPrepared(prepared, {2}, page).success);
    UltraDb_Finalize(prepared);
    events = recorder->Take();
    REQUIRE_EQ(events.size(), size_t(1));
    REQUIRE(events[0].kind == Kind::Query);
    REQUIRE_EQ(events[0].sql, std::string("SELECT v FROM n WHERE v = ?"));
    REQUIRE_EQ(events[0].rows, size_t(1));
    UltraDb_RemoveStatementObserver(id);
}

TEST(observer_may_run_statements_itself) {
    auto recorder = std::make_shared<Recorder>();
    recorder->connection = ObservedConn("reentrant");
    const std::string c = recorder->connection;
    REQUIRE(UltraDb_Exec(c, "CREATE TABLE log(sql TEXT)").success);
    // Writes what it saw into the database it watches - once, so the write
    // it reports for itself does not write again.
    auto writes = std::make_shared<int>(0);
    const int id = UltraDb_AddStatementObserver([c, writes](const UltraDbStatementEvent& event) {
        if (event.connection != c || event.sql.rfind("INSERT INTO log", 0) == 0) return;
        ++*writes;
        UltraDb_Exec(c, "INSERT INTO log VALUES (?)", {event.sql});
    });
    UltraDbResultSet rows;
    REQUIRE(UltraDb_Query(c, "SELECT 1 AS one", rows).success);
    UltraDb_RemoveStatementObserver(id);
    REQUIRE(UltraDb_Query(c, "SELECT sql FROM log", rows).success);
    REQUIRE_EQ(rows.Size(), size_t(1));
    REQUIRE_EQ(rows.Row(0)["sql"].AsString(), std::string("SELECT 1 AS one"));
    REQUIRE_EQ(*writes, 1);
}

TEST(observer_empty_function_is_refused) {
    REQUIRE_EQ(UltraDb_AddStatementObserver(UltraDbStatementObserver()), 0);
    UltraDb_RemoveStatementObserver(0);         // nothing to remove: no harm
}

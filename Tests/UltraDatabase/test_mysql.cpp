// Tests/UltraDatabase/test_mysql.cpp
// The MySQL / MariaDB driver: the script splitter (no server), and against a
// live server (test_mysql.h) its values both ways - bound to a prepared
// statement and read over the text protocol - the result columns' origins,
// affected rows and insert ids, transactions, scripts with DELIMITER and
// compound bodies, cursors outside and inside a transaction, cancelling a
// running query, the error codes a caller reacts to, a read-only connection,
// and the schema introspection (columns and their defaults, keys, indexes,
// foreign keys, triggers, views).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "test_mysql.h"

#include <UltraDatabase/UltraDatabase.h>

// The splitter is internal; its tests reach it the way the PostgreSQL ones do.
#include "../../UltraCanvas/core/UltraDatabase/UltraDatabaseInternal.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace {

std::vector<std::string> Split(const std::string& sql) { return ultradb_internal::MysqlStatements(sql); }

const UltraDbColumnSchema* ColumnOf(const UltraDbTableSchema& t, const std::string& name) {
    for (const auto& c : t.columns)
        if (c.name == name) return &c;
    return nullptr;
}

} // namespace

// ===== MYSQL STATEMENT SPLITTING (no server) =====

TEST(mysql_split_plain_statements) {
    auto s = Split("SELECT 1;  SELECT 2 ;\n\nSELECT 3");
    REQUIRE_EQ(s.size(), (size_t)3);
    REQUIRE_EQ(s[0], std::string("SELECT 1"));
    REQUIRE_EQ(s[2], std::string("SELECT 3"));
    REQUIRE_EQ(Split(" ;; -- only a comment\n # and another\n /* and a third */ ;").size(), (size_t)0);
}

TEST(mysql_split_ignores_semicolons_in_text_and_comments) {
    auto s = Split("SELECT 'a;b', \"c;d\", 'it\\'s; here', `e;f` -- x;y\n# p;q\n; /* r; */ SELECT 2");
    REQUIRE_EQ(s.size(), (size_t)2);
    REQUIRE(s[0].find("'it\\'s; here'") != std::string::npos);
    REQUIRE(s[0].find("`e;f`") != std::string::npos);
    REQUIRE_EQ(s[1], std::string("SELECT 2"));      // the comment before it is not part of it
    // Where each is in the script, for an editor.
    const std::string script = "-- one\nSELECT 1;\nDELIMITER $$\nCREATE TRIGGER t BEFORE INSERT ON x FOR EACH ROW BEGIN SET @a = 1; END$$\nDELIMITER ;\nSELECT 3";
    const auto spans = UltraDb_MysqlScriptStatements(script);
    REQUIRE_EQ(spans.size(), (size_t)3);
    REQUIRE_EQ(script.substr(spans[0].begin, spans[0].end - spans[0].begin), std::string("SELECT 1"));
    REQUIRE_EQ(script.substr(spans[0].end, spans[0].terminatorEnd - spans[0].end), std::string(";"));
    REQUIRE(script.compare(spans[1].begin, 14, "CREATE TRIGGER") == 0);
    REQUIRE_EQ(script.substr(spans[1].end, spans[1].terminatorEnd - spans[1].end), std::string("$$"));
    REQUIRE_EQ(script.substr(spans[2].begin), std::string("SELECT 3"));
    REQUIRE_EQ(spans[2].terminatorEnd, script.size());
    // "--" is a comment only before a space: SELECT 5--1 is arithmetic.
    auto t = Split("SELECT 5--1; SELECT 2");
    REQUIRE_EQ(t.size(), (size_t)2);
    REQUIRE_EQ(t[0], std::string("SELECT 5--1"));
}

TEST(mysql_split_follows_delimiter) {
    auto s = Split(
        "DELIMITER $$\n"
        "CREATE TRIGGER t BEFORE INSERT ON x FOR EACH ROW BEGIN\n"
        "  SET NEW.a = 1;\n"
        "  IF NEW.b IS NULL THEN SET NEW.b = 2; END IF;\n"
        "END$$\n"
        "DELIMITER ;\n"
        "INSERT INTO x VALUES (1); SELECT 1");
    REQUIRE_EQ(s.size(), (size_t)3);
    REQUIRE(s[0].rfind("CREATE TRIGGER", 0) == 0);
    REQUIRE(s[0].find("END IF;\nEND") != std::string::npos);
    REQUIRE_EQ(s[1], std::string("INSERT INTO x VALUES (1)"));
    REQUIRE_EQ(s[2], std::string("SELECT 1"));
}

TEST(mysql_split_keeps_compound_bodies_whole_without_delimiter) {
    auto s = Split(
        "CREATE PROCEDURE p(n INT) BEGIN\n"
        "  DECLARE i INT DEFAULT 0;\n"
        "  WHILE i < n DO SET i = i + 1; END WHILE;\n"
        "  SELECT CASE WHEN i > 1 THEN 'many' ELSE 'one' END;\n"
        "  CASE i WHEN 1 THEN SELECT 1; ELSE SELECT 2; END CASE;\n"
        "END;\n"
        "CALL p(3)");
    REQUIRE_EQ(s.size(), (size_t)2);
    REQUIRE(s[0].find("END CASE;\nEND") != std::string::npos);
    REQUIRE_EQ(s[1], std::string("CALL p(3)"));
    // Outside a CREATE, BEGIN starts a transaction.
    REQUIRE_EQ(Split("BEGIN; UPDATE x SET a = CASE WHEN b THEN 1 END; COMMIT;").size(), (size_t)3);
}

// ===== MYSQL (live server) =====

TEST(mysql_values_round_trip) {
    const std::string c = "my-values";
    if (!ultradb_test::OpenMysqlTestConnection(c)) return;
    REQUIRE(UltraDb_Exec(c, "DROP TABLE IF EXISTS ultradb_my_values").success);
    REQUIRE(UltraDb_Exec(c,
        "CREATE TABLE ultradb_my_values(id INT AUTO_INCREMENT PRIMARY KEY, i BIGINT, u BIGINT UNSIGNED,"
        " d DOUBLE, n DECIMAL(20,4), s VARCHAR(100), b VARBINARY(20), bits BIT(8), t TINYINT,"
        " day DATE, at DATETIME(3), nothing INT)").success);
    const std::vector<uint8_t> bytes = { 0x00, 0xFF, 0x27, 0x5C, 0x00 };
    UltraDbResult r = UltraDb_Exec(c,
        "INSERT INTO ultradb_my_values(i, u, d, n, s, b, bits, t, day, at, nothing)"
        " VALUES (?, ?, ?, ?, ?, ?, b'10100101', ?, ?, ?, ?)",
        { int64_t(-9007199254740993), UltraDbValue("18446744073709551615"), 3.14, UltraDbValue("12345678901234.5678"),
          UltraDbValue("Grüße 'quoted' \\ and; ? no placeholder"), UltraDbValue(bytes), true,
          UltraDbValue("2026-10-24"), UltraDbValue("2026-10-24 12:34:56.789"), UltraDbValue() });
    REQUIRE(r.success);
    REQUIRE_EQ(r.affectedRows, (int64_t)1);
    REQUIRE_EQ(r.lastInsertId, (int64_t)1);

    // Read back twice: as a prepared statement (binary protocol) and as text.
    for (int pass = 0; pass < 2; ++pass) {
        UltraDbResultSet rows;
        r = pass == 0 ? UltraDb_Query(c, "SELECT * FROM ultradb_my_values WHERE id = ?", { 1 }, rows)
                      : UltraDb_Query(c, "SELECT * FROM ultradb_my_values WHERE id = 1", {}, rows);
        REQUIRE(r.success);
        REQUIRE_EQ(rows.Size(), (size_t)1);
        const UltraDbRow& row = rows.Row(0);
        REQUIRE(row["i"].Type() == UltraDbType::Int);
        REQUIRE_EQ(row["i"].AsInt64(), (int64_t)-9007199254740993);
        REQUIRE(row["u"].Type() == UltraDbType::Text);                  // beyond int64: exact text
        REQUIRE_EQ(row["u"].AsString(), std::string("18446744073709551615"));
        REQUIRE(row["d"].Type() == UltraDbType::Double);
        REQUIRE(row["d"].AsDouble() == 3.14);
        REQUIRE(row["n"].Type() == UltraDbType::Text);
        REQUIRE_EQ(row["n"].AsString(), std::string("12345678901234.5678"));
        REQUIRE_EQ(row["s"].AsString(), std::string("Grüße 'quoted' \\ and; ? no placeholder"));
        REQUIRE(row["b"].Type() == UltraDbType::Blob);
        REQUIRE(row["b"].AsBlob() == bytes);
        REQUIRE(row["bits"].Type() == UltraDbType::Int);
        REQUIRE_EQ(row["bits"].AsInt64(), (int64_t)0xA5);
        REQUIRE_EQ(row["t"].AsInt64(), (int64_t)1);
        REQUIRE_EQ(row["day"].AsString(), std::string("2026-10-24"));
        REQUIRE_EQ(row["at"].AsString(), std::string("2026-10-24 12:34:56.789"));
        REQUIRE(row["nothing"].IsNull());
        // Where each column came from, so a grid can edit the row.
        const auto& info = rows.ColumnInfo();
        REQUIRE_EQ(info.size(), rows.Columns().size());
        REQUIRE_EQ(info[0].sourceTable, std::string("ultradb_my_values"));
        REQUIRE_EQ(info[0].sourceColumn, std::string("id"));
        REQUIRE_EQ(info[0].declaredType, std::string("int"));
    }
    UltraDbResultSet expr;
    REQUIRE(UltraDb_Query(c, "SELECT 1 + 1 AS two", {}, expr).success);
    REQUIRE(expr.ColumnInfo()[0].sourceTable.empty());
    REQUIRE_EQ(expr.Row(0)["two"].AsInt64(), (int64_t)2);
    REQUIRE(UltraDb_Exec(c, "DROP TABLE ultradb_my_values").success);
    UltraDb_CloseConnection(c);
}

TEST(mysql_affected_rows_and_errors) {
    const std::string c = "my-errors";
    if (!ultradb_test::OpenMysqlTestConnection(c)) return;
    REQUIRE(UltraDb_Exec(c, "DROP TABLE IF EXISTS ultradb_my_child, ultradb_my_parent").success);
    REQUIRE(UltraDb_Exec(c, "CREATE TABLE ultradb_my_parent(id INT PRIMARY KEY, name VARCHAR(20) NOT NULL UNIQUE)"
                            " ENGINE=InnoDB").success);
    REQUIRE(UltraDb_Exec(c, "CREATE TABLE ultradb_my_child(id INT PRIMARY KEY, parent INT NOT NULL,"
                            " FOREIGN KEY (parent) REFERENCES ultradb_my_parent(id)) ENGINE=InnoDB").success);
    REQUIRE(UltraDb_Exec(c, "INSERT INTO ultradb_my_parent VALUES (?, ?), (?, ?)", { 1, "a", 2, "b" }).success);
    UltraDbResult r = UltraDb_Exec(c, "UPDATE ultradb_my_parent SET name = CONCAT(name, '!') WHERE id > ?", { 0 });
    REQUIRE(r.success);
    REQUIRE_EQ(r.affectedRows, (int64_t)2);
    r = UltraDb_Exec(c, "INSERT INTO ultradb_my_parent VALUES (?, ?)", { 3, "a!" });
    REQUIRE(r.code == UltraDbResultCode::ConstraintViolation);
    REQUIRE_EQ(r.sqlState, std::string("23000"));
    r = UltraDb_Exec(c, "INSERT INTO ultradb_my_child VALUES (?, ?)", { 1, 99 });
    REQUIRE(r.code == UltraDbResultCode::ConstraintViolation);
    r = UltraDb_Exec(c, "INSERT INTO ultradb_my_parent VALUES (?, ?)", { 4, UltraDbValue() });
    REQUIRE(r.code == UltraDbResultCode::ConstraintViolation);
    r = UltraDb_Exec(c, "SELEC 1");
    REQUIRE(r.code == UltraDbResultCode::QueryFailed);
    REQUIRE(!r.message.empty());
    r = UltraDb_Exec(c, "INSERT INTO ultradb_my_parent VALUES (?, ?)", { 5 });
    REQUIRE(r.code == UltraDbResultCode::InvalidArgument);   // one value short
    REQUIRE(UltraDb_Exec(c, "DROP TABLE ultradb_my_child, ultradb_my_parent").success);
    UltraDb_CloseConnection(c);
}

TEST(mysql_transactions_and_prepared_statements) {
    const std::string c = "my-tx";
    if (!ultradb_test::OpenMysqlTestConnection(c)) return;
    REQUIRE(UltraDb_Exec(c, "DROP TABLE IF EXISTS ultradb_my_tx").success);
    REQUIRE(UltraDb_Exec(c, "CREATE TABLE ultradb_my_tx(id INT PRIMARY KEY, v INT) ENGINE=InnoDB").success);
    UltraDbResult error;
    UltraDbHandle tx = UltraDb_Begin(c, &error);
    REQUIRE(tx != UltraDbInvalidHandle);
    REQUIRE(UltraDb_ExecInTx(tx, "INSERT INTO ultradb_my_tx VALUES (?, ?)", { 1, 10 }).success);
    REQUIRE(UltraDb_Rollback(tx).success);
    UltraDbResultSet rows;
    REQUIRE(UltraDb_Query(c, "SELECT COUNT(*) AS n FROM ultradb_my_tx", {}, rows).success);
    REQUIRE_EQ(rows.Row(0)["n"].AsInt64(), (int64_t)0);
    tx = UltraDb_Begin(c, &error);
    REQUIRE(tx != UltraDbInvalidHandle);
    REQUIRE(UltraDb_ExecInTx(tx, "INSERT INTO ultradb_my_tx VALUES (?, ?)", { 1, 10 }).success);
    REQUIRE(UltraDb_QueryInTx(tx, "SELECT v FROM ultradb_my_tx WHERE id = ?" + UltraDb_RowLockSuffix(c), { 1 }, rows).success);
    REQUIRE_EQ(rows.Row(0)["v"].AsInt64(), (int64_t)10);
    REQUIRE(UltraDb_Commit(tx).success);

    // One prepared statement, run several times.
    UltraDbHandle stmt = UltraDb_Prepare(c, "INSERT INTO ultradb_my_tx VALUES (?, ?)", &error);
    REQUIRE(stmt != UltraDbInvalidHandle);
    for (int i = 2; i <= 4; ++i) REQUIRE(UltraDb_ExecPrepared(stmt, { i, i * 10 }).success);
    REQUIRE(UltraDb_Finalize(stmt).success);
    REQUIRE(UltraDb_Query(c, "SELECT SUM(v) AS s FROM ultradb_my_tx", {}, rows).success);
    REQUIRE_EQ(rows.Row(0)["s"].AsString(), std::string("100"));   // SUM of INT is DECIMAL: exact text
    REQUIRE(UltraDb_Exec(c, "DROP TABLE ultradb_my_tx").success);
    UltraDb_CloseConnection(c);
}

TEST(mysql_exec_batch_with_delimiter_and_bodies) {
    const std::string c = "my-batch";
    if (!ultradb_test::OpenMysqlTestConnection(c)) return;
    REQUIRE(UltraDb_Exec(c, "DROP TABLE IF EXISTS ultradb_my_batch; DROP PROCEDURE IF EXISTS ultradb_my_count").success);
    std::vector<UltraDbStatementResult> out;
    UltraDbResult r = UltraDb_ExecBatch(c,
        "CREATE TABLE ultradb_my_batch(a INT, s VARCHAR(20), stamp INT);\n"
        "DELIMITER //\n"
        "CREATE TRIGGER ultradb_my_batch_stamp BEFORE INSERT ON ultradb_my_batch FOR EACH ROW BEGIN\n"
        "  IF NEW.stamp IS NULL THEN SET NEW.stamp = 7; END IF;\n"
        "END//\n"
        "DELIMITER ;\n"
        "CREATE PROCEDURE ultradb_my_count(OUT n INT) BEGIN SELECT COUNT(*) INTO n FROM ultradb_my_batch; END;\n"
        "INSERT INTO ultradb_my_batch(a, s) VALUES (1, 'x;y'), (2, 'it\\'s');\n"
        "SELECT a, s, stamp FROM ultradb_my_batch ORDER BY a;\n"
        "SELECT nosuch;\n"
        "DROP TABLE ultradb_my_batch;", out);
    REQUIRE(!r.success);
    REQUIRE_EQ(out.size(), (size_t)6);                 // the last DROP never ran
    REQUIRE_EQ(out[3].result.affectedRows, (int64_t)2);
    REQUIRE(out[4].returnsRows);
    REQUIRE_EQ(out[4].rows.Size(), (size_t)2);
    REQUIRE_EQ(out[4].rows.Row(1)["s"].AsString(), std::string("it's"));
    REQUIRE_EQ(out[4].rows.Row(0)["stamp"].AsInt64(), (int64_t)7);   // the trigger's body ran whole
    REQUIRE(!out[5].result.success);
    UltraDbResultSet rows;
    REQUIRE(UltraDb_Query(c, "CALL ultradb_my_count(@n)", {}, rows).success);
    REQUIRE(UltraDb_Query(c, "SELECT @n AS n", {}, rows).success);
    REQUIRE_EQ(rows.Row(0)["n"].AsInt64(), (int64_t)2);
    REQUIRE(UltraDb_Exec(c, "DROP TABLE ultradb_my_batch; DROP PROCEDURE ultradb_my_count").success);
    UltraDb_CloseConnection(c);
}

TEST(mysql_cursors_stream_and_see_their_transaction) {
    const std::string c = "my-cursor";
    if (!ultradb_test::OpenMysqlTestConnection(c)) return;
    REQUIRE(UltraDb_Exec(c, "DROP TABLE IF EXISTS ultradb_my_cursor").success);
    REQUIRE(UltraDb_Exec(c, "CREATE TABLE ultradb_my_cursor(n INT PRIMARY KEY) ENGINE=InnoDB").success);
    std::string values;
    for (int n = 1; n <= 2500; ++n) values += (n > 1 ? ",(" : "(") + std::to_string(n) + ")";
    REQUIRE(UltraDb_Exec(c, "INSERT INTO ultradb_my_cursor VALUES " + values).success);
    UltraDbResult error;
    UltraDbHandle cur = UltraDb_OpenCursor(c, "SELECT n FROM ultradb_my_cursor WHERE n > ? ORDER BY n", { 500 }, &error);
    REQUIRE(cur != UltraDbInvalidHandle);
    size_t total = 0;
    bool exhausted = false;
    int64_t last = 0;
    while (!exhausted) {
        UltraDbResultSet page;
        REQUIRE(UltraDb_FetchRows(cur, 700, page, exhausted).success);
        REQUIRE_EQ(page.Columns().size(), (size_t)1);
        total += page.Size();
        if (!page.Empty()) last = page.Row(page.Size() - 1)["n"].AsInt64();
        // The connection is free between pages.
        UltraDbResultSet other;
        REQUIRE(UltraDb_Query(c, "SELECT 1 AS one", {}, other).success);
    }
    REQUIRE_EQ(total, (size_t)2000);
    REQUIRE_EQ(last, (int64_t)2500);
    REQUIRE(UltraDb_CloseCursor(cur).success);

    // Inside a transaction the cursor sees what the transaction wrote.
    UltraDbHandle tx = UltraDb_Begin(c, &error);
    REQUIRE(tx != UltraDbInvalidHandle);
    REQUIRE(UltraDb_ExecInTx(tx, "INSERT INTO ultradb_my_cursor VALUES (?)", { 9999 }).success);
    cur = UltraDb_OpenCursor(c, "SELECT n FROM ultradb_my_cursor WHERE n = 9999", {}, &error);
    REQUIRE(cur != UltraDbInvalidHandle);
    UltraDbResultSet page;
    REQUIRE(UltraDb_FetchRows(cur, 10, page, exhausted).success);
    REQUIRE_EQ(page.Size(), (size_t)1);
    REQUIRE(exhausted);
    REQUIRE(UltraDb_CloseCursor(cur).success);
    REQUIRE(UltraDb_Rollback(tx).success);
    REQUIRE(UltraDb_Exec(c, "DROP TABLE ultradb_my_cursor").success);
    UltraDb_CloseConnection(c);
}

TEST(mysql_cancel_stops_a_running_query) {
    const std::string c = "my-cancel";
    if (!ultradb_test::OpenMysqlTestConnection(c)) return;
    std::atomic<bool> done{false};
    UltraDbResult slept;
    const auto start = std::chrono::steady_clock::now();
    std::thread worker([&] {
        UltraDbResultSet rows;
        slept = UltraDb_Query(c, "SELECT SLEEP(30) AS s FROM DUAL WHERE ? = 1", { 1 }, rows);
        // MySQL ends an interrupted SLEEP with a row of 1, MariaDB with an error.
        if (slept.success && rows.Size() == 1 && rows.Row(0)["s"].AsInt64() == 1)
            slept = UltraDbResult::Error(UltraDbResultCode::Cancelled, "interrupted");
        done = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    REQUIRE(UltraDb_CancelQuery(c).success);
    worker.join();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    REQUIRE(done);
    REQUIRE(seconds < 20);
    if (slept.code != UltraDbResultCode::Cancelled)
        std::printf("         not cancelled: %d %s\n", static_cast<int>(slept.code), slept.message.c_str());
    REQUIRE(slept.code == UltraDbResultCode::Cancelled);
    // The connection is still there.
    UltraDbResultSet rows;
    REQUIRE(UltraDb_Query(c, "SELECT 1 AS one", {}, rows).success);
    UltraDb_CloseConnection(c);
}

TEST(mysql_read_only_connection_refuses_writes) {
    const std::string c = "my-ro";
    const std::string rw = "my-rw";
    if (!ultradb_test::OpenMysqlTestConnection(rw)) return;
    REQUIRE(UltraDb_Exec(rw, "DROP TABLE IF EXISTS ultradb_my_ro").success);
    REQUIRE(UltraDb_Exec(rw, "CREATE TABLE ultradb_my_ro(a INT) ENGINE=InnoDB").success);
    if (!ultradb_test::OpenMysqlTestConnection(c, true)) return;
    REQUIRE(!UltraDb_Exec(c, "INSERT INTO ultradb_my_ro VALUES (?)", { 1 }).success);
    UltraDbResultSet rows;
    REQUIRE(UltraDb_Query(c, "SELECT COUNT(*) AS n FROM ultradb_my_ro", {}, rows).success);
    REQUIRE(UltraDb_Exec(rw, "DROP TABLE ultradb_my_ro").success);
    UltraDb_CloseConnection(c);
    UltraDb_CloseConnection(rw);
}

TEST(mysql_introspection) {
    const std::string c = "my-schema";
    if (!ultradb_test::OpenMysqlTestConnection(c)) return;
    const std::string db = ultradb_test::MysqlEnv("ULTRADB_TEST_MYSQL_DB", "");
    REQUIRE(UltraDb_Exec(c, "DROP VIEW IF EXISTS ultradb_my_v; DROP TABLE IF EXISTS ultradb_my_line, ultradb_my_order").success);
    REQUIRE(UltraDb_Exec(c,
        "CREATE TABLE ultradb_my_order(id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT 'the number',"
        " code VARCHAR(20) NOT NULL DEFAULT 'none', placed DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        " total DECIMAL(10,2) DEFAULT 0, note TEXT, doubled INT AS (id * 2) VIRTUAL,"
        " UNIQUE KEY uq_code (code)) ENGINE=InnoDB COMMENT='orders'").success);
    REQUIRE(UltraDb_Exec(c,
        "CREATE TABLE ultradb_my_line(order_id INT UNSIGNED NOT NULL, n INT NOT NULL, qty INT,"
        " PRIMARY KEY (order_id, n), KEY ix_qty (qty),"
        " CONSTRAINT fk_line_order FOREIGN KEY (order_id) REFERENCES ultradb_my_order(id) ON DELETE CASCADE)"
        " ENGINE=InnoDB").success);
    REQUIRE(UltraDb_Exec(c, "CREATE TRIGGER ultradb_my_line_qty BEFORE INSERT ON ultradb_my_line FOR EACH ROW"
                            " SET NEW.qty = IFNULL(NEW.qty, 1)").success);
    REQUIRE(UltraDb_Exec(c, "CREATE VIEW ultradb_my_v AS SELECT id, code FROM ultradb_my_order").success);

    std::vector<std::string> schemas;
    REQUIRE(UltraDb_ListSchemas(c, schemas).success);
    REQUIRE(std::find(schemas.begin(), schemas.end(), db) != schemas.end());
    REQUIRE(std::find(schemas.begin(), schemas.end(), "mysql") == schemas.end());

    std::vector<UltraDbObjectSummary> objects;
    REQUIRE(UltraDb_ListObjects(c, db, objects).success);
    bool table = false, view = false;
    for (const auto& o : objects) {
        if (o.name == "ultradb_my_order") table = o.kind == UltraDbObjectKind::Table;
        if (o.name == "ultradb_my_v") view = o.kind == UltraDbObjectKind::View;
    }
    REQUIRE(table && view);
    REQUIRE(UltraDb_ListObjects(c, "ultradb_no_such_db", objects).code == UltraDbResultCode::NotFound);

    UltraDbTableSchema order;
    REQUIRE(UltraDb_DescribeTable(c, db, "ultradb_my_order", order).success);
    REQUIRE_EQ(order.comment, std::string("orders"));
    REQUIRE(order.primaryKey == std::vector<std::string>{"id"});
    const UltraDbColumnSchema* id = ColumnOf(order, "id");
    REQUIRE(id && id->autoIncrement && !id->nullable && !id->hasDefault && id->comment == "the number");
    REQUIRE(id->declaredType.find("unsigned") != std::string::npos);
    const UltraDbColumnSchema* code = ColumnOf(order, "code");
    REQUIRE(code && code->hasDefault && code->defaultExpression == "'none'" && !code->nullable);
    const UltraDbColumnSchema* placed = ColumnOf(order, "placed");
    REQUIRE(placed && placed->hasDefault);
    std::string lowered = placed->defaultExpression;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::tolower);
    REQUIRE(lowered.rfind("current_timestamp", 0) == 0);
    const UltraDbColumnSchema* total = ColumnOf(order, "total");
    REQUIRE(total && total->hasDefault && total->valueType == UltraDbType::Text);
    const UltraDbColumnSchema* note = ColumnOf(order, "note");
    REQUIRE(note && note->nullable && !note->hasDefault);
    const UltraDbColumnSchema* doubled = ColumnOf(order, "doubled");
    REQUIRE(doubled && doubled->generated);
    bool unique = false;
    for (const auto& index : order.indexes)
        if (index.name == "uq_code") unique = index.unique && index.columns == std::vector<std::string>{"code"};
    REQUIRE(unique);

    UltraDbTableSchema line;
    REQUIRE(UltraDb_DescribeTable(c, db, "ultradb_my_line", line).success);
    REQUIRE((line.primaryKey == std::vector<std::string>{"order_id", "n"}));
    REQUIRE_EQ(line.foreignKeys.size(), (size_t)1);
    REQUIRE_EQ(line.foreignKeys[0].name, std::string("fk_line_order"));
    REQUIRE_EQ(line.foreignKeys[0].refTable, std::string("ultradb_my_order"));
    REQUIRE_EQ(line.foreignKeys[0].onDelete, std::string("CASCADE"));
    REQUIRE((line.foreignKeys[0].refColumns == std::vector<std::string>{"id"}));
    REQUIRE_EQ(line.triggers.size(), (size_t)1);
    REQUIRE(line.triggers[0].definition.rfind("CREATE TRIGGER `ultradb_my_line_qty` BEFORE INSERT ON `ultradb_my_line`", 0) == 0);

    UltraDbTableSchema v;
    REQUIRE(UltraDb_DescribeTable(c, db, "ultradb_my_v", v).success);
    REQUIRE(v.kind == UltraDbObjectKind::View);
    REQUIRE(!v.viewDefinition.empty());
    REQUIRE(UltraDb_DescribeTable(c, db, "ultradb_no_such_table", v).code == UltraDbResultCode::NotFound);

    std::string quoted;
    REQUIRE(UltraDb_QuoteQualifiedName(c, db, "odd`name", quoted).success);
    REQUIRE_EQ(quoted, "`" + db + "`.`odd``name`");
    REQUIRE(UltraDb_Exec(c, "DROP VIEW ultradb_my_v; DROP TABLE ultradb_my_line, ultradb_my_order").success);
    UltraDb_CloseConnection(c);
}

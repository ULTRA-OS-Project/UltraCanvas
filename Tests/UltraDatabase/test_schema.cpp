// Tests/UltraDatabase/test_schema.cpp
// Stage 0 of UltraDBAdmin (Docs/Research/UltraDBAdminDesignProposal.md): the
// connection list, identifier quoting, schema introspection and result-column
// metadata - against SQLite always, and against a live PostgreSQL server
// where one is configured (see test_pg.h; CI configures one). Also that a
// PostgreSQL numeric comes back exact.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "test_pg.h"

#include <UltraDatabase/UltraDatabase.h>

// Not a public header: the affinity mapping is driver-internal.
#include "../../UltraCanvas/core/UltraDatabase/UltraDatabaseInternal.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string SqliteConn(const std::string& tag) {
    UltraDbConnectionConfig cfg;
    cfg.name = "schema-" + tag;
    cfg.database = ":memory:";
    UltraDb_CloseConnection(cfg.name);
    REQUIRE(UltraDb_RegisterConnection(cfg).success);
    return cfg.name;
}

std::string Quote(const std::string& connection, const std::string& name) {
    std::string out;
    REQUIRE(UltraDb_QuoteIdentifier(connection, name, out).success);
    return out;
}

const UltraDbColumnSchema* FindColumn(const UltraDbTableSchema& t, const std::string& name) {
    for (const auto& c : t.columns) if (c.name == name) return &c;
    return nullptr;
}

const UltraDbIndexSchema* FindIndex(const UltraDbTableSchema& t, const std::string& name) {
    for (const auto& i : t.indexes) if (i.name == name) return &i;
    return nullptr;
}

bool Has(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

} // namespace

// ===== CONNECTIONS AND QUOTING =====

TEST(list_connections_is_sorted_and_current) {
    const std::string b = SqliteConn("list-b");
    const std::string a = SqliteConn("list-a");
    std::vector<std::string> names = UltraDb_ListConnections();
    REQUIRE(Has(names, a));
    REQUIRE(Has(names, b));
    REQUIRE(std::is_sorted(names.begin(), names.end()));
    UltraDb_CloseConnection(a);
    REQUIRE(!Has(UltraDb_ListConnections(), a));
    UltraDb_CloseConnection(b);
}

TEST(quote_identifier_doubles_quotes_and_refuses_the_unquotable) {
    const std::string c = SqliteConn("quote");
    REQUIRE_EQ(Quote(c, "plain"), std::string("\"plain\""));
    REQUIRE_EQ(Quote(c, "Order Items"), std::string("\"Order Items\""));
    REQUIRE_EQ(Quote(c, "we\"ird"), std::string("\"we\"\"ird\""));

    std::string out = "left over";
    UltraDbResult empty = UltraDb_QuoteIdentifier(c, "", out);
    REQUIRE(empty.code == UltraDbResultCode::InvalidArgument);
    REQUIRE(out.empty());   // never something a caller could splice by mistake
    UltraDbResult nul = UltraDb_QuoteIdentifier(c, std::string("a\0b", 3), out);
    REQUIRE(nul.code == UltraDbResultCode::InvalidArgument);
    REQUIRE(UltraDb_QuoteIdentifier("no-such-connection", "x", out).code ==
            UltraDbResultCode::ConnectionNotFound);

    REQUIRE(UltraDb_QuoteQualifiedName(c, "main", "Order Items", out).success);
    REQUIRE_EQ(out, std::string("\"main\".\"Order Items\""));
    REQUIRE(UltraDb_QuoteQualifiedName(c, "", "t", out).success);
    REQUIRE_EQ(out, std::string("\"t\""));
    UltraDb_CloseConnection(c);
}

TEST(quoted_names_work_in_real_statements) {
    // The point of quoting: a name that is a keyword, has a space or a quote
    // in it can still be created, written and read - and an injection
    // attempt in a name stays a name.
    const std::string c = SqliteConn("quote-use");
    const std::string table = Quote(c, "Order \"Items\"; DROP TABLE x; --");
    const std::string column = Quote(c, "select");
    REQUIRE(UltraDb_Exec(c, "CREATE TABLE " + table + "(" + column + " TEXT)").success);
    REQUIRE(UltraDb_Exec(c, "INSERT INTO " + table + "(" + column + ") VALUES(?)", { "v" }).success);
    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query(c, "SELECT " + column + " FROM " + table, rs).success);
    REQUIRE_EQ(rs.Size(), (size_t)1);
    REQUIRE_EQ(rs.Row(0)["select"].AsString(), std::string("v"));
    UltraDb_CloseConnection(c);
}

// ===== SQLITE INTROSPECTION =====

TEST(sqlite_affinity_follows_sqlite_rules) {
    using ultradb_internal::SqliteValueTypeForDeclaredType;
    REQUIRE(SqliteValueTypeForDeclaredType("INTEGER") == UltraDbType::Int);
    REQUIRE(SqliteValueTypeForDeclaredType("bigint") == UltraDbType::Int);
    REQUIRE(SqliteValueTypeForDeclaredType("VARCHAR(80)") == UltraDbType::Text);
    REQUIRE(SqliteValueTypeForDeclaredType("BLOB") == UltraDbType::Blob);
    REQUIRE(SqliteValueTypeForDeclaredType("DOUBLE PRECISION") == UltraDbType::Double);
    REQUIRE(SqliteValueTypeForDeclaredType("BOOLEAN") == UltraDbType::Bool);
    REQUIRE(SqliteValueTypeForDeclaredType("DATETIME") == UltraDbType::Text);
    REQUIRE(SqliteValueTypeForDeclaredType("DECIMAL(10,2)") == UltraDbType::Double);
    REQUIRE(SqliteValueTypeForDeclaredType("") == UltraDbType::Text);
}

namespace {

const char* kSqliteShop =
    "CREATE TABLE customers("
    "  id INTEGER PRIMARY KEY,"
    "  email TEXT NOT NULL UNIQUE,"
    "  name TEXT NOT NULL DEFAULT 'anonymous',"
    "  name_upper TEXT GENERATED ALWAYS AS (upper(name)) VIRTUAL);"
    "CREATE TABLE orders("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  customer_id INTEGER NOT NULL REFERENCES customers ON DELETE CASCADE,"
    "  total REAL,"
    "  placed DATETIME);"
    "CREATE TABLE order_lines("
    "  order_id INTEGER, line INTEGER, sku TEXT,"
    "  PRIMARY KEY(order_id, line),"
    "  FOREIGN KEY(order_id) REFERENCES orders(id) ON UPDATE RESTRICT);"
    "CREATE INDEX orders_by_customer ON orders(customer_id, placed);"
    "CREATE TRIGGER orders_stamp AFTER INSERT ON orders BEGIN"
    "  UPDATE orders SET placed = CURRENT_TIMESTAMP WHERE id = NEW.id; END;"
    "CREATE VIEW big_orders AS SELECT id, total FROM orders WHERE total > 100;";

} // namespace

TEST(sqlite_lists_schemas_and_objects) {
    const std::string c = SqliteConn("objects");
    REQUIRE(UltraDb_Exec(c, kSqliteShop).success);

    std::vector<std::string> schemas;
    REQUIRE(UltraDb_ListSchemas(c, schemas).success);
    REQUIRE(Has(schemas, "main"));

    std::vector<UltraDbObjectSummary> objects;
    REQUIRE(UltraDb_ListObjects(c, "main", objects).success);
    std::vector<std::string> names;
    for (const auto& o : objects) names.push_back(o.name);
    // Sorted, with the view, and without AUTOINCREMENT's sqlite_sequence.
    REQUIRE_EQ(names.size(), (size_t)4);
    REQUIRE_EQ(names[0], std::string("big_orders"));
    REQUIRE_EQ(names[1], std::string("customers"));
    REQUIRE(objects[0].kind == UltraDbObjectKind::View);
    REQUIRE(objects[1].kind == UltraDbObjectKind::Table);
    REQUIRE(!Has(names, "sqlite_sequence"));

    // An empty schema means main; an unknown one is NotFound, not an error
    // about sqlite_master.
    REQUIRE(UltraDb_ListObjects(c, "", objects).success);
    REQUIRE_EQ(objects.size(), (size_t)4);
    REQUIRE(UltraDb_ListObjects(c, "nosuch", objects).code == UltraDbResultCode::NotFound);
    UltraDb_CloseConnection(c);
}

TEST(sqlite_attached_schema_with_a_space_in_its_name) {
    // The schema in front of sqlite_master is the one name the SQLite
    // introspection writes into SQL text; it has to arrive quoted.
    const std::string c = SqliteConn("attached");
    REQUIRE(UltraDb_Exec(c, "ATTACH ':memory:' AS \"other db\"").success);
    REQUIRE(UltraDb_Exec(c, "CREATE TABLE \"other db\".notes(id INTEGER PRIMARY KEY, body TEXT)").success);
    std::vector<std::string> schemas;
    REQUIRE(UltraDb_ListSchemas(c, schemas).success);
    REQUIRE(Has(schemas, "other db"));
    std::vector<UltraDbObjectSummary> objects;
    REQUIRE(UltraDb_ListObjects(c, "other db", objects).success);
    REQUIRE_EQ(objects.size(), (size_t)1);
    REQUIRE_EQ(objects[0].name, std::string("notes"));
    REQUIRE_EQ(objects[0].schema, std::string("other db"));
    UltraDbTableSchema t;
    REQUIRE(UltraDb_DescribeTable(c, "other db", "notes", t).success);
    REQUIRE_EQ(t.columns.size(), (size_t)2);
    REQUIRE(UltraDb_DescribeTable(c, "main", "notes", t).code == UltraDbResultCode::NotFound);
    UltraDb_CloseConnection(c);
}

TEST(sqlite_describes_columns_keys_and_defaults) {
    const std::string c = SqliteConn("describe");
    REQUIRE(UltraDb_Exec(c, kSqliteShop).success);

    UltraDbTableSchema t;
    // SQLite names are case-insensitive; the stored spelling comes back.
    REQUIRE(UltraDb_DescribeTable(c, "main", "CUSTOMERS", t).success);
    REQUIRE_EQ(t.name, std::string("customers"));
    REQUIRE(t.kind == UltraDbObjectKind::Table);
    REQUIRE_EQ(t.columns.size(), (size_t)4);

    const UltraDbColumnSchema* id = FindColumn(t, "id");
    REQUIRE(id != nullptr);
    REQUIRE(id->autoIncrement);          // the rowid alias
    REQUIRE(!id->nullable);
    REQUIRE(id->valueType == UltraDbType::Int);
    REQUIRE_EQ(t.primaryKey.size(), (size_t)1);
    REQUIRE_EQ(t.primaryKey[0], std::string("id"));

    const UltraDbColumnSchema* name = FindColumn(t, "name");
    REQUIRE(name != nullptr);
    REQUIRE(!name->nullable);
    REQUIRE(name->hasDefault);
    REQUIRE_EQ(name->defaultExpression, std::string("'anonymous'"));

    const UltraDbColumnSchema* upper = FindColumn(t, "name_upper");
    REQUIRE(upper != nullptr);
    REQUIRE(upper->generated);

    // UNIQUE makes an automatic index, reported as unique and not primary.
    bool uniqueEmail = false;
    for (const auto& i : t.indexes)
        if (i.unique && !i.primary && i.columns.size() == 1 && i.columns[0] == "email")
            uniqueEmail = true;
    REQUIRE(uniqueEmail);
    UltraDb_CloseConnection(c);
}

TEST(sqlite_describes_foreign_keys_indexes_and_triggers) {
    const std::string c = SqliteConn("fk");
    REQUIRE(UltraDb_Exec(c, kSqliteShop).success);

    UltraDbTableSchema orders;
    REQUIRE(UltraDb_DescribeTable(c, "main", "orders", orders).success);
    REQUIRE_EQ(orders.foreignKeys.size(), (size_t)1);
    const UltraDbForeignKeySchema& fk = orders.foreignKeys[0];
    REQUIRE_EQ(fk.refTable, std::string("customers"));
    REQUIRE_EQ(fk.columns[0], std::string("customer_id"));
    // REFERENCES customers with no column means its primary key.
    REQUIRE_EQ(fk.refColumns.size(), (size_t)1);
    REQUIRE_EQ(fk.refColumns[0], std::string("id"));
    REQUIRE_EQ(fk.onDelete, std::string("CASCADE"));
    REQUIRE_EQ(fk.onUpdate, std::string("NO ACTION"));

    const UltraDbIndexSchema* byCustomer = FindIndex(orders, "orders_by_customer");
    REQUIRE(byCustomer != nullptr);
    REQUIRE(!byCustomer->unique);
    REQUIRE_EQ(byCustomer->columns.size(), (size_t)2);
    REQUIRE_EQ(byCustomer->columns[1], std::string("placed"));

    REQUIRE_EQ(orders.triggers.size(), (size_t)1);
    REQUIRE_EQ(orders.triggers[0].name, std::string("orders_stamp"));
    REQUIRE(orders.triggers[0].definition.find("CREATE TRIGGER") == 0);

    UltraDbTableSchema lines;
    REQUIRE(UltraDb_DescribeTable(c, "main", "order_lines", lines).success);
    REQUIRE_EQ(lines.primaryKey.size(), (size_t)2);
    REQUIRE_EQ(lines.primaryKey[1], std::string("line"));
    REQUIRE(!FindColumn(lines, "order_id")->autoIncrement);   // a composite key is no rowid
    REQUIRE_EQ(lines.foreignKeys[0].onUpdate, std::string("RESTRICT"));
    bool primaryIndex = false;
    for (const auto& i : lines.indexes) if (i.primary) primaryIndex = true;
    REQUIRE(primaryIndex);
    UltraDb_CloseConnection(c);
}

TEST(sqlite_describes_views_and_reports_missing_objects) {
    const std::string c = SqliteConn("view");
    REQUIRE(UltraDb_Exec(c, kSqliteShop).success);
    UltraDbTableSchema v;
    REQUIRE(UltraDb_DescribeTable(c, "main", "big_orders", v).success);
    REQUIRE(v.kind == UltraDbObjectKind::View);
    REQUIRE(v.viewDefinition.find("CREATE VIEW") == 0);
    REQUIRE_EQ(v.columns.size(), (size_t)2);

    UltraDbTableSchema none;
    REQUIRE(UltraDb_DescribeTable(c, "main", "nope", none).code == UltraDbResultCode::NotFound);
    REQUIRE(UltraDb_DescribeTable(c, "nosuch", "orders", none).code == UltraDbResultCode::NotFound);
    REQUIRE(UltraDb_DescribeTable(c, "main", "", none).code == UltraDbResultCode::InvalidArgument);
    UltraDb_CloseConnection(c);
}

TEST(sqlite_result_columns_carry_type_and_source) {
    const std::string c = SqliteConn("colinfo");
    REQUIRE(UltraDb_Exec(c, kSqliteShop).success);
    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query(c, "SELECT c.email AS mail, 1 + 1 AS two FROM customers c", rs).success);
    const auto& info = rs.ColumnInfo();
    REQUIRE_EQ(info.size(), (size_t)2);
    REQUIRE_EQ(info[0].name, std::string("mail"));
    REQUIRE_EQ(info[0].declaredType, std::string("TEXT"));
    REQUIRE(info[1].declaredType.empty());      // an expression has no type
    REQUIRE(info[1].sourceTable.empty());
    // Present where libsqlite3 has column metadata (Debian, Homebrew, MSYS2).
    if (!info[0].sourceTable.empty()) {
        REQUIRE_EQ(info[0].sourceSchema, std::string("main"));
        REQUIRE_EQ(info[0].sourceTable, std::string("customers"));
        REQUIRE_EQ(info[0].sourceColumn, std::string("email"));
    } else {
        std::printf("         [note] libsqlite3 without column metadata: no source table\n");
    }
    UltraDb_CloseConnection(c);
}

// ===== POSTGRESQL INTROSPECTION (live server) =====

namespace {

const char* kPgSchema = "ultradb test";   // a space, so every query must quote or bind it

const char* kPgShop =
    "CREATE SCHEMA \"ultradb test\";"
    "CREATE TABLE \"ultradb test\".parent("
    "  id serial PRIMARY KEY,"
    "  code varchar(20) NOT NULL UNIQUE,"
    "  amount numeric(12,2) NOT NULL DEFAULT 0,"
    "  note text COLLATE \"C\","
    "  created_at timestamptz DEFAULT now());"
    "COMMENT ON TABLE \"ultradb test\".parent IS 'the parents';"
    "COMMENT ON COLUMN \"ultradb test\".parent.code IS 'business key';"
    "CREATE INDEX parent_code_lower ON \"ultradb test\".parent (lower(code));"
    "CREATE TABLE \"ultradb test\".child("
    "  id bigint GENERATED ALWAYS AS IDENTITY,"
    "  parent_id integer NOT NULL,"
    "  qty integer NOT NULL,"
    "  price numeric(10,2) NOT NULL,"
    "  total numeric GENERATED ALWAYS AS (qty * price) STORED,"
    "  PRIMARY KEY (id),"
    "  CONSTRAINT child_parent FOREIGN KEY (parent_id)"
    "    REFERENCES \"ultradb test\".parent(id) ON DELETE CASCADE ON UPDATE SET NULL);"
    "CREATE FUNCTION \"ultradb test\".touch() RETURNS trigger LANGUAGE plpgsql"
    "  AS $$ BEGIN RETURN NEW; END $$;"
    "CREATE TRIGGER child_touch BEFORE INSERT ON \"ultradb test\".child"
    "  FOR EACH ROW EXECUTE FUNCTION \"ultradb test\".touch();"
    "CREATE VIEW \"ultradb test\".big AS SELECT id, amount FROM \"ultradb test\".parent WHERE amount > 100;"
    "CREATE MATERIALIZED VIEW \"ultradb test\".totals AS SELECT parent_id, sum(total) AS s"
    "  FROM \"ultradb test\".child GROUP BY parent_id;"
    "CREATE SEQUENCE \"ultradb test\".tickets;";

bool PgShop(const std::string& c) {
    if (!ultradb_test::OpenPgTestConnection(c)) return false;
    // DROP ... CASCADE narrates every object it drops; the log has no use for it.
    REQUIRE(UltraDb_Exec(c, "SET client_min_messages = warning").success);
    REQUIRE(UltraDb_Exec(c, "DROP SCHEMA IF EXISTS \"ultradb test\" CASCADE").success);
    UltraDbResult made = UltraDb_Exec(c, kPgShop);
    if (!made) std::printf("         %s\n", made.message.c_str());
    REQUIRE(made.success);
    return true;
}

void PgDrop(const std::string& c) {
    UltraDb_Exec(c, "DROP SCHEMA IF EXISTS \"ultradb test\" CASCADE");
    UltraDb_CloseConnection(c);
}

} // namespace

TEST(pg_lists_schemas_and_objects) {
    const std::string c = "schema-pg-objects";
    if (!PgShop(c)) return;
    std::vector<std::string> schemas;
    REQUIRE(UltraDb_ListSchemas(c, schemas).success);
    REQUIRE(Has(schemas, "public"));
    REQUIRE(Has(schemas, kPgSchema));
    REQUIRE(!Has(schemas, "pg_catalog"));
    REQUIRE(!Has(schemas, "information_schema"));

    std::vector<UltraDbObjectSummary> objects;
    REQUIRE(UltraDb_ListObjects(c, kPgSchema, objects).success);
    // big, child, child_id_seq, parent, parent_id_seq, tickets, totals
    auto kindOf = [&](const std::string& name) {
        for (const auto& o : objects) if (o.name == name) return static_cast<int>(o.kind);
        return -1;
    };
    REQUIRE_EQ(kindOf("parent"), static_cast<int>(UltraDbObjectKind::Table));
    REQUIRE_EQ(kindOf("big"), static_cast<int>(UltraDbObjectKind::View));
    REQUIRE_EQ(kindOf("totals"), static_cast<int>(UltraDbObjectKind::MaterializedView));
    REQUIRE_EQ(kindOf("tickets"), static_cast<int>(UltraDbObjectKind::Sequence));
    REQUIRE(std::is_sorted(objects.begin(), objects.end(),
                           [](const UltraDbObjectSummary& a, const UltraDbObjectSummary& b) {
                               return a.name < b.name; }));
    REQUIRE(UltraDb_ListObjects(c, "no such schema", objects).code == UltraDbResultCode::NotFound);
    PgDrop(c);
}

TEST(pg_describes_columns_types_defaults_and_comments) {
    const std::string c = "schema-pg-describe";
    if (!PgShop(c)) return;
    UltraDbTableSchema t;
    REQUIRE(UltraDb_DescribeTable(c, kPgSchema, "parent", t).success);
    REQUIRE_EQ(t.comment, std::string("the parents"));
    REQUIRE(t.sizeBytes > 0);
    REQUIRE_EQ(t.primaryKey.size(), (size_t)1);
    REQUIRE_EQ(t.primaryKey[0], std::string("id"));

    const UltraDbColumnSchema* id = FindColumn(t, "id");
    REQUIRE(id->autoIncrement);                         // serial: nextval default
    REQUIRE(id->valueType == UltraDbType::Int);
    const UltraDbColumnSchema* code = FindColumn(t, "code");
    REQUIRE_EQ(code->declaredType, std::string("character varying(20)"));
    REQUIRE(!code->nullable);
    REQUIRE_EQ(code->comment, std::string("business key"));
    const UltraDbColumnSchema* amount = FindColumn(t, "amount");
    REQUIRE_EQ(amount->declaredType, std::string("numeric(12,2)"));
    REQUIRE(amount->valueType == UltraDbType::Text);    // exact, not a double
    REQUIRE(amount->hasDefault);
    REQUIRE_EQ(amount->defaultExpression, std::string("0"));
    REQUIRE_EQ(FindColumn(t, "note")->collation, std::string("C"));
    REQUIRE(FindColumn(t, "code")->collation.empty());  // the type's default is not news
    REQUIRE(FindColumn(t, "created_at")->nullable);

    // The expression index prints its expression; UNIQUE made its own.
    const UltraDbIndexSchema* lower = FindIndex(t, "parent_code_lower");
    REQUIRE(lower != nullptr);
    REQUIRE_EQ(lower->columns.size(), (size_t)1);
    REQUIRE(lower->columns[0].find("lower") != std::string::npos);
    const UltraDbIndexSchema* pkey = FindIndex(t, "parent_pkey");
    REQUIRE(pkey != nullptr && pkey->primary && pkey->unique);
    PgDrop(c);
}

TEST(pg_describes_identity_generated_keys_triggers_views) {
    const std::string c = "schema-pg-keys";
    if (!PgShop(c)) return;
    UltraDbTableSchema child;
    REQUIRE(UltraDb_DescribeTable(c, kPgSchema, "child", child).success);
    REQUIRE(FindColumn(child, "id")->autoIncrement);    // identity
    REQUIRE(!FindColumn(child, "id")->hasDefault);
    REQUIRE(FindColumn(child, "total")->generated);
    REQUIRE(!FindColumn(child, "total")->hasDefault);

    REQUIRE_EQ(child.foreignKeys.size(), (size_t)1);
    const UltraDbForeignKeySchema& fk = child.foreignKeys[0];
    REQUIRE_EQ(fk.name, std::string("child_parent"));
    REQUIRE_EQ(fk.refSchema, std::string(kPgSchema));
    REQUIRE_EQ(fk.refTable, std::string("parent"));
    REQUIRE_EQ(fk.columns[0], std::string("parent_id"));
    REQUIRE_EQ(fk.refColumns[0], std::string("id"));
    REQUIRE_EQ(fk.onDelete, std::string("CASCADE"));
    REQUIRE_EQ(fk.onUpdate, std::string("SET NULL"));

    REQUIRE_EQ(child.triggers.size(), (size_t)1);   // the FK's own triggers are internal
    REQUIRE_EQ(child.triggers[0].name, std::string("child_touch"));

    UltraDbTableSchema view;
    REQUIRE(UltraDb_DescribeTable(c, kPgSchema, "big", view).success);
    REQUIRE(view.kind == UltraDbObjectKind::View);
    REQUIRE(view.viewDefinition.find("amount > ") != std::string::npos);
    REQUIRE_EQ(view.approxRows, (int64_t)-1);

    UltraDbTableSchema none;
    REQUIRE(UltraDb_DescribeTable(c, kPgSchema, "Parent", none).code ==
            UltraDbResultCode::NotFound);   // PostgreSQL folds only unquoted names
    PgDrop(c);
}

TEST(pg_numeric_is_exact) {
    const std::string c = "schema-pg-numeric";
    if (!ultradb_test::OpenPgTestConnection(c)) return;
    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query(c, "SELECT 12345678901234.5678::numeric AS n, 2.50::numeric(5,2) AS m", rs).success);
    REQUIRE(rs.Row(0)["n"].Type() == UltraDbType::Text);
    REQUIRE_EQ(rs.Row(0)["n"].AsString(), std::string("12345678901234.5678"));
    REQUIRE_EQ(rs.Row(0)["m"].AsString(), std::string("2.50"));
    REQUIRE_EQ(rs.Row(0)["m"].AsDouble(), 2.5);       // still coerces for a caller that wants it
    UltraDb_CloseConnection(c);
}

TEST(pg_result_columns_carry_type_and_source_and_follow_ddl) {
    const std::string c = "schema-pg-colinfo";
    if (!PgShop(c)) return;
    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query(c,
        "SELECT p.code, p.amount AS money, 1 + 1 AS two FROM \"ultradb test\".parent p", rs).success);
    const auto& info = rs.ColumnInfo();
    REQUIRE_EQ(info.size(), (size_t)3);
    REQUIRE_EQ(info[0].declaredType, std::string("character varying(20)"));
    REQUIRE_EQ(info[0].sourceSchema, std::string(kPgSchema));
    REQUIRE_EQ(info[0].sourceTable, std::string("parent"));
    REQUIRE_EQ(info[0].sourceColumn, std::string("code"));
    REQUIRE_EQ(info[1].name, std::string("money"));
    REQUIRE_EQ(info[1].declaredType, std::string("numeric(12,2)"));
    REQUIRE_EQ(info[1].sourceColumn, std::string("amount"));
    REQUIRE_EQ(info[2].declaredType, std::string("integer"));
    REQUIRE(info[2].sourceTable.empty());

    // A rename on this connection must not leave the cached old name behind.
    REQUIRE(UltraDb_Exec(c, "ALTER TABLE \"ultradb test\".parent RENAME COLUMN code TO sku").success);
    REQUIRE(UltraDb_Query(c, "SELECT p.sku FROM \"ultradb test\".parent p", rs).success);
    REQUIRE_EQ(rs.ColumnInfo()[0].sourceColumn, std::string("sku"));

    // The same through a prepared statement.
    UltraDbHandle stmt = UltraDb_Prepare(c, "SELECT amount FROM \"ultradb test\".parent WHERE id = ?");
    REQUIRE(stmt != UltraDbInvalidHandle);
    REQUIRE(UltraDb_QueryPrepared(stmt, { 1 }, rs).success);
    REQUIRE_EQ(rs.ColumnInfo()[0].sourceTable, std::string("parent"));
    UltraDb_Finalize(stmt);
    PgDrop(c);
}

TEST(pg_column_lookup_inside_a_transaction_leaves_it_usable) {
    // The metadata lookup runs on the caller's connection. Inside a
    // transaction it must not disturb it: the rows written before and after a
    // query on a table the cache has never seen must both commit.
    const std::string c = "schema-pg-tx";
    if (!PgShop(c)) return;
    UltraDbResult err;
    UltraDbHandle tx = UltraDb_Begin(c, &err);
    REQUIRE(tx != UltraDbInvalidHandle);
    REQUIRE(UltraDb_ExecInTx(tx, "INSERT INTO \"ultradb test\".parent(code) VALUES(?)", { "a" }).success);
    UltraDbResultSet rs;
    REQUIRE(UltraDb_QueryInTx(tx, "SELECT code, amount FROM \"ultradb test\".parent", {}, rs).success);
    REQUIRE_EQ(rs.ColumnInfo()[0].sourceTable, std::string("parent"));
    REQUIRE(UltraDb_ExecInTx(tx, "INSERT INTO \"ultradb test\".parent(code) VALUES(?)", { "b" }).success);
    REQUIRE(UltraDb_Commit(tx).success);
    REQUIRE(UltraDb_Query(c, "SELECT count(*) AS n FROM \"ultradb test\".parent", rs).success);
    REQUIRE_EQ(rs.Row(0)["n"].AsInt64(), (int64_t)2);
    PgDrop(c);
}

TEST(pg_quoted_names_round_trip) {
    const std::string c = "schema-pg-quote";
    if (!PgShop(c)) return;
    std::string table;
    REQUIRE(UltraDb_QuoteQualifiedName(c, kPgSchema, "we\"ird name", table).success);
    REQUIRE_EQ(table, std::string("\"ultradb test\".\"we\"\"ird name\""));
    REQUIRE(UltraDb_Exec(c, "CREATE TABLE " + table + "(v integer)").success);
    REQUIRE(UltraDb_Exec(c, "INSERT INTO " + table + "(v) VALUES(?)", { 7 }).success);
    UltraDbTableSchema t;
    REQUIRE(UltraDb_DescribeTable(c, kPgSchema, "we\"ird name", t).success);
    REQUIRE_EQ(t.columns.size(), (size_t)1);
    PgDrop(c);
}

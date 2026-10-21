// core/UltraDatabase/UltraDatabaseSqliteSchema.cpp
// Schema introspection for the SQLite driver: what UltraDb_ListSchemas,
// UltraDb_ListObjects and UltraDb_DescribeTable answer for a SQLite database.
//
// Written as plain SQL over the connection interface - sqlite_master and the
// table-valued pragma functions (pragma_table_xinfo, pragma_foreign_key_list,
// pragma_index_list, pragma_index_info), which take the table and schema as
// bound arguments. A pragma statement (`PRAGMA table_info(x)`) cannot bind its
// argument, so it would need the name spliced into the text; these do not.
// The one place a name has to be in the text - the schema in front of
// sqlite_master - goes through QuoteIdentifier, after the schema has been
// checked against the connection's own list.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraDatabaseInternal.h"

#include "UltraDatabase/UltraDatabaseSchema.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>

namespace ultradb_internal {

namespace {

std::string Upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool Contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

UltraDbResult Query(IUltraDbConnection& conn, const std::string& sql,
                    const UltraDbParams& params, UltraDbResultSet& out) {
    return conn.ExecuteDirect(sql, params, out);
}

// "main" when the caller did not name one - SQLite's own default.
std::string SchemaOrMain(const std::string& schema) {
    return schema.empty() ? std::string("main") : schema;
}

// The schema exists (SQLite's database list), so its quoted name can go in
// front of sqlite_master. NotFound otherwise, rather than SQLite's
// "no such table: x.sqlite_master", which names the wrong thing.
UltraDbResult CheckSchema(IUltraDbConnection& conn, const std::string& schema) {
    std::vector<std::string> schemas;
    UltraDbResult listed = SqliteListSchemas(conn, schemas);
    if (!listed) return listed;
    if (std::find(schemas.begin(), schemas.end(), schema) == schemas.end())
        return UltraDbResult::Error(UltraDbResultCode::NotFound,
                                    "no schema named '" + schema + "'", "sqlite");
    return UltraDbResult::Ok();
}

std::string MasterTable(IUltraDbConnection& conn, const std::string& schema) {
    return conn.QuoteIdentifier(schema) + ".sqlite_master";
}

// The columns of a table's primary key, in key order.
UltraDbResult PrimaryKeyColumns(IUltraDbConnection& conn, const std::string& schema,
                                const std::string& table, std::vector<std::string>& out) {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult r = Query(conn,
        "SELECT name FROM pragma_table_xinfo(?, ?) WHERE pk > 0 ORDER BY pk",
        { table, schema }, rs);
    if (!r) return r;
    for (const auto& row : rs) out.push_back(row["name"].AsString());
    return UltraDbResult::Ok();
}

} // namespace

UltraDbType SqliteValueTypeForDeclaredType(const std::string& declaredType) {
    // SQLite's own affinity rules (datatype3.html, section 3.1), in its order,
    // with two refinements a reader of the value wants: BOOLEAN reads back as
    // a bool, and DATE / TIME columns hold text by convention.
    const std::string t = Upper(declaredType);
    if (t.empty()) return UltraDbType::Text;               // untyped: whatever was stored
    if (Contains(t, "INT")) return UltraDbType::Int;
    if (Contains(t, "CHAR") || Contains(t, "CLOB") || Contains(t, "TEXT")) return UltraDbType::Text;
    if (Contains(t, "BLOB")) return UltraDbType::Blob;
    if (Contains(t, "REAL") || Contains(t, "FLOA") || Contains(t, "DOUB")) return UltraDbType::Double;
    if (Contains(t, "BOOL")) return UltraDbType::Bool;
    if (Contains(t, "DATE") || Contains(t, "TIME")) return UltraDbType::Text;
    return UltraDbType::Double;                            // NUMERIC affinity
}

UltraDbResult SqliteListSchemas(IUltraDbConnection& conn, std::vector<std::string>& out) {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult r = Query(conn, "SELECT name FROM pragma_database_list ORDER BY seq", {}, rs);
    if (!r) return r;
    for (const auto& row : rs) out.push_back(row["name"].AsString());
    return UltraDbResult::Ok();
}

UltraDbResult SqliteListObjects(IUltraDbConnection& conn, const std::string& schemaName,
                                std::vector<UltraDbObjectSummary>& out) {
    out.clear();
    const std::string schema = SchemaOrMain(schemaName);
    UltraDbResult exists = CheckSchema(conn, schema);
    if (!exists) return exists;

    UltraDbResultSet rs;
    UltraDbResult r = Query(conn,
        "SELECT type, name FROM " + MasterTable(conn, schema) +
        " WHERE type IN ('table', 'view') AND name NOT LIKE ? ESCAPE '\\'"
        " ORDER BY name",
        { "sqlite\\_%" }, rs);
    if (!r) return r;
    for (const auto& row : rs) {
        UltraDbObjectSummary o;
        o.schema = schema;
        o.name   = row["name"].AsString();
        o.kind   = row["type"].AsString() == "view" ? UltraDbObjectKind::View
                                                    : UltraDbObjectKind::Table;
        out.push_back(std::move(o));
    }
    return UltraDbResult::Ok();
}

UltraDbResult SqliteDescribeTable(IUltraDbConnection& conn, const std::string& schemaName,
                                  const std::string& tableName, UltraDbTableSchema& out) {
    out = UltraDbTableSchema{};
    const std::string schema = SchemaOrMain(schemaName);
    UltraDbResult exists = CheckSchema(conn, schema);
    if (!exists) return exists;

    // The object itself. Names are case-insensitive in SQLite, so "Customers"
    // finds customers - and the stored spelling is what is reported back.
    UltraDbResultSet obj;
    UltraDbResult r = Query(conn,
        "SELECT type, name, sql FROM " + MasterTable(conn, schema) +
        " WHERE type IN ('table', 'view') AND name = ? COLLATE NOCASE",
        { tableName }, obj);
    if (!r) return r;
    if (obj.Empty())
        return UltraDbResult::Error(UltraDbResultCode::NotFound,
                                    "no table or view named '" + tableName + "' in '" +
                                    schema + "'", "sqlite");
    out.schema = schema;
    out.name   = obj.Row(0)["name"].AsString();
    const bool isView = obj.Row(0)["type"].AsString() == "view";
    out.kind = isView ? UltraDbObjectKind::View : UltraDbObjectKind::Table;
    if (isView) out.viewDefinition = obj.Row(0)["sql"].AsString();
    const std::string table = out.name;

    // Indexes first: whether the primary key has an index of its own is what
    // tells a rowid alias (INTEGER PRIMARY KEY, auto-assigned) from any other
    // integer key.
    bool primaryKeyHasIndex = false;
    if (!isView) {
        UltraDbResultSet list;
        r = Query(conn, "SELECT name, \"unique\", origin FROM pragma_index_list(?, ?) ORDER BY name",
                  { table, schema }, list);
        if (!r) return r;
        for (const auto& row : list) {
            UltraDbIndexSchema idx;
            idx.name    = row["name"].AsString();
            idx.unique  = row["unique"].AsInt64() != 0;
            idx.primary = row["origin"].AsString() == "pk";
            if (idx.primary) primaryKeyHasIndex = true;
            UltraDbResultSet cols;
            r = Query(conn, "SELECT cid, name FROM pragma_index_info(?, ?) ORDER BY seqno",
                      { idx.name, schema }, cols);
            if (!r) return r;
            for (const auto& c : cols)
                // cid -2 is an expression; SQLite gives no text for it here.
                idx.columns.push_back(c["cid"].AsInt64() == -2 ? std::string("(expression)")
                                                               : c["name"].AsString());
            out.indexes.push_back(std::move(idx));
        }
    }

    // Columns. pragma_table_xinfo rather than table_info: it also lists
    // generated columns, which a grid must show and must not write.
    UltraDbResultSet cols;
    r = Query(conn,
        "SELECT name, type, \"notnull\", dflt_value, pk, hidden FROM pragma_table_xinfo(?, ?)"
        " ORDER BY cid",
        { table, schema }, cols);
    if (!r) return r;
    std::map<int64_t, std::string> keyByPosition;
    for (const auto& row : cols) {
        const int64_t hidden = row["hidden"].AsInt64();
        if (hidden == 1) continue;              // a virtual table's hidden column
        UltraDbColumnSchema c;
        c.name         = row["name"].AsString();
        c.declaredType = row["type"].AsString();
        c.valueType    = SqliteValueTypeForDeclaredType(c.declaredType);
        c.nullable     = row["notnull"].AsInt64() == 0;
        c.generated    = hidden == 2 || hidden == 3;
        c.hasDefault   = !row["dflt_value"].IsNull();
        if (c.hasDefault) c.defaultExpression = row["dflt_value"].AsString();
        const int64_t pk = row["pk"].AsInt64();
        if (pk > 0) keyByPosition[pk] = c.name;
        out.columns.push_back(std::move(c));
    }
    for (const auto& kv : keyByPosition) out.primaryKey.push_back(kv.second);

    // A single INTEGER key without an index of its own is the rowid: never
    // NULL, and assigned by SQLite when an insert leaves it out.
    if (!isView && out.primaryKey.size() == 1 && !primaryKeyHasIndex) {
        for (auto& c : out.columns) {
            if (c.name == out.primaryKey[0] && Upper(c.declaredType) == "INTEGER") {
                c.autoIncrement = true;
                c.nullable = false;
            }
        }
    }

    if (!isView) {
        // Foreign keys: one row per column of each key, grouped by id.
        UltraDbResultSet fks;
        r = Query(conn,
            "SELECT id, \"table\", \"from\", \"to\", on_update, on_delete"
            " FROM pragma_foreign_key_list(?, ?) ORDER BY id, seq",
            { table, schema }, fks);
        if (!r) return r;
        int64_t currentId = -1;
        for (const auto& row : fks) {
            if (row["id"].AsInt64() != currentId) {
                currentId = row["id"].AsInt64();
                UltraDbForeignKeySchema fk;
                fk.refSchema = schema;           // SQLite keys stay in one database
                fk.refTable  = row["table"].AsString();
                fk.onUpdate  = row["on_update"].AsString();
                fk.onDelete  = row["on_delete"].AsString();
                out.foreignKeys.push_back(std::move(fk));
            }
            UltraDbForeignKeySchema& fk = out.foreignKeys.back();
            fk.columns.push_back(row["from"].AsString());
            // A key written REFERENCES parent - with no column - points at the
            // parent's primary key; SQLite then reports "to" as NULL.
            if (!row["to"].IsNull()) fk.refColumns.push_back(row["to"].AsString());
        }
        for (auto& fk : out.foreignKeys) {
            if (!fk.refColumns.empty()) continue;
            r = PrimaryKeyColumns(conn, schema, fk.refTable, fk.refColumns);
            if (!r) return r;
        }

        // No size: SQLite keeps none, and the dbstat table that could count
        // one walks every page of the table - minutes on a large file, for a
        // number nobody asked for. sizeBytes stays -1 (unknown).
    }

    UltraDbResultSet triggers;
    r = Query(conn,
        "SELECT name, sql FROM " + MasterTable(conn, schema) +
        " WHERE type = 'trigger' AND tbl_name = ? COLLATE NOCASE ORDER BY name",
        { table }, triggers);
    if (!r) return r;
    for (const auto& row : triggers)
        out.triggers.push_back({ row["name"].AsString(), row["sql"].AsString() });

    return UltraDbResult::Ok();
}

} // namespace ultradb_internal

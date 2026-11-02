// core/UltraDatabase/UltraDatabaseMysqlSchema.cpp
// Schema introspection for the MySQL / MariaDB driver: what
// UltraDb_ListSchemas, UltraDb_ListObjects and UltraDb_DescribeTable answer
// for a MySQL or MariaDB server.
//
// A MySQL "schema" is a database: CREATE SCHEMA and CREATE DATABASE are the
// same statement, and one connection reaches every database its user may see.
// So the schemas listed are the databases, apart from the server's own
// (mysql, information_schema, performance_schema, sys).
//
// Read from information_schema, which both servers fill alike - except for a
// column's default: MariaDB (10.2.7 on) reports it as SQL, a string quoted
// and "NULL" for none, while MySQL reports a string's bare text, an SQL NULL
// for none and marks an expression with DEFAULT_GENERATED. Both are turned
// into what the model wants, the default as it would be written in the DDL.
//
// Plain SQL over the connection interface, every name bound, so this file
// compiles - and stays compiled - in a build without libmariadb.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraDatabaseInternal.h"

#include "UltraDatabase/UltraDatabaseSchema.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace ultradb_internal {

namespace {

const char* const kDriver = "mysql";

std::string Lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// The connection's default database when the caller names none.
UltraDbResult SchemaOrCurrent(IUltraDbConnection& conn, const std::string& schema, std::string& out) {
    if (!schema.empty()) { out = schema; return UltraDbResult::Ok(); }
    UltraDbResultSet rs;
    UltraDbResult r = conn.ExecuteDirect("SELECT DATABASE() AS db", {}, rs);
    if (!r) return r;
    if (rs.Empty() || rs.Row(0)["db"].IsNull())
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    "no database named, and the connection has none selected", kDriver);
    out = rs.Row(0)["db"].AsString();
    return UltraDbResult::Ok();
}

bool IsMariaDb(IUltraDbConnection& conn) {
    UltraDbResultSet rs;
    if (!conn.ExecuteDirect("SELECT VERSION() AS v", {}, rs) || rs.Empty()) return false;
    return Lower(rs.Row(0)["v"].AsString()).find("mariadb") != std::string::npos;
}

// A column's default as it would be written in its DDL; false when it has
// none. `extra` is information_schema's EXTRA, `type` the DATA_TYPE.
bool DefaultExpression(const UltraDbValue& value, bool mariaDb, const std::string& extra,
                       const std::string& type, std::string& out) {
    if (value.IsNull()) return false;
    const std::string text = value.AsString();
    if (mariaDb) {
        // SQL already: 'text', 42, current_timestamp(), NULL.
        if (text == "NULL") return false;
        out = text;
        return true;
    }
    // MySQL: an expression is marked; numbers and bits are written bare.
    if (Lower(extra).find("default_generated") != std::string::npos) {
        out = text;
        return true;
    }
    static const char* const bare[] = { "tinyint", "smallint", "mediumint", "int", "bigint", "decimal",
                                        "float", "double", "bit", "year" };
    const std::string t = Lower(type);
    for (const char* b : bare)
        if (t == b) { out = text; return true; }
    out = "'";
    for (char c : text) {
        if (c == '\'' || c == '\\') out.push_back(c);
        out.push_back(c);
    }
    out.push_back('\'');
    return true;
}

} // namespace

UltraDbType MysqlValueTypeForDataType(const std::string& dataType, const std::string& columnType) {
    const std::string t = Lower(dataType);
    if (t == "tinyint" || t == "smallint" || t == "mediumint" || t == "int" || t == "integer" ||
        t == "bigint" || t == "year" || t == "bit") {
        // An unsigned BIGINT can exceed what an Int holds; the driver hands
        // such a value over as text, so the column says so.
        if (t == "bigint" && Lower(columnType).find("unsigned") != std::string::npos) return UltraDbType::Text;
        return UltraDbType::Int;
    }
    if (t == "float" || t == "double" || t == "real") return UltraDbType::Double;
    if (t == "binary" || t == "varbinary" || t == "tinyblob" || t == "blob" || t == "mediumblob" ||
        t == "longblob")
        return UltraDbType::Blob;
    // decimal stays text: it is exact, and a double is not.
    return UltraDbType::Text;
}

UltraDbResult MysqlListSchemas(IUltraDbConnection& conn, std::vector<std::string>& out) {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult r = conn.ExecuteDirect(
        "SELECT SCHEMA_NAME AS name FROM information_schema.SCHEMATA"
        " WHERE SCHEMA_NAME NOT IN ('mysql', 'information_schema', 'performance_schema', 'sys')"
        " ORDER BY SCHEMA_NAME",
        {}, rs);
    if (!r) return r;
    for (const auto& row : rs) out.push_back(row["name"].AsString());
    return UltraDbResult::Ok();
}

UltraDbResult MysqlListObjects(IUltraDbConnection& conn, const std::string& schemaName,
                               std::vector<UltraDbObjectSummary>& out) {
    out.clear();
    std::string schema;
    UltraDbResult r = SchemaOrCurrent(conn, schemaName, schema);
    if (!r) return r;

    UltraDbResultSet ns;
    r = conn.ExecuteDirect("SELECT SCHEMA_NAME FROM information_schema.SCHEMATA WHERE SCHEMA_NAME = ?",
                           { schema }, ns);
    if (!r) return r;
    if (ns.Empty())
        return UltraDbResult::Error(UltraDbResultCode::NotFound,
                                    "no database named '" + schema + "'", kDriver);

    UltraDbResultSet rs;
    r = conn.ExecuteDirect(
        "SELECT TABLE_NAME AS name, TABLE_TYPE AS type FROM information_schema.TABLES"
        " WHERE TABLE_SCHEMA = ? ORDER BY TABLE_NAME",
        { schema }, rs);
    if (!r) return r;
    for (const auto& row : rs) {
        UltraDbObjectSummary o;
        o.schema = schema;
        o.name = row["name"].AsString();
        const std::string type = row["type"].AsString();
        o.kind = type == "VIEW" ? UltraDbObjectKind::View
               : type == "SEQUENCE" ? UltraDbObjectKind::Sequence    // MariaDB 10.3+
               : UltraDbObjectKind::Table;                             // BASE TABLE, SYSTEM VERSIONED
        out.push_back(std::move(o));
    }
    return UltraDbResult::Ok();
}

UltraDbResult MysqlDescribeTable(IUltraDbConnection& conn, const std::string& schemaName,
                                 const std::string& table, UltraDbTableSchema& out) {
    out = UltraDbTableSchema{};
    std::string schema;
    UltraDbResult r = SchemaOrCurrent(conn, schemaName, schema);
    if (!r) return r;

    UltraDbResultSet rel;
    r = conn.ExecuteDirect(
        "SELECT TABLE_NAME AS name, TABLE_TYPE AS type, TABLE_ROWS AS est_rows,"
        "       DATA_LENGTH + INDEX_LENGTH AS bytes, TABLE_COMMENT AS comment,"
        "       TABLE_COLLATION AS collation"
        " FROM information_schema.TABLES WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ?",
        { schema, table }, rel);
    if (!r) return r;
    if (rel.Empty())
        return UltraDbResult::Error(UltraDbResultCode::NotFound,
                                    "no table or view named '" + table + "' in '" + schema + "'", kDriver);
    const UltraDbRow& t = rel.Row(0);
    out.schema = schema;
    out.name = t["name"].AsString();
    const std::string type = t["type"].AsString();
    out.kind = type == "VIEW" ? UltraDbObjectKind::View
             : type == "SEQUENCE" ? UltraDbObjectKind::Sequence
             : UltraDbObjectKind::Table;
    const std::string tableCollation = t["collation"].AsString();
    if (out.kind == UltraDbObjectKind::Table) {
        // An estimate for InnoDB, exact for MyISAM; the server's word either way.
        if (!t["est_rows"].IsNull()) out.approxRows = t["est_rows"].AsInt64();
        if (!t["bytes"].IsNull()) out.sizeBytes = t["bytes"].AsInt64();
        // InnoDB writes its free space into a table's comment; that is not the user's.
        const std::string comment = t["comment"].AsString();
        if (comment.rfind("InnoDB free:", 0) != 0) out.comment = comment;
    }
    if (out.kind == UltraDbObjectKind::View) {
        UltraDbResultSet view;
        r = conn.ExecuteDirect(
            "SELECT VIEW_DEFINITION AS def FROM information_schema.VIEWS"
            " WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ?",
            { schema, table }, view);
        if (!r) return r;
        if (!view.Empty()) out.viewDefinition = view.Row(0)["def"].AsString();
    }

    const bool mariaDb = IsMariaDb(conn);
    UltraDbResultSet cols;
    r = conn.ExecuteDirect(
        "SELECT COLUMN_NAME AS name, COLUMN_TYPE AS column_type, DATA_TYPE AS data_type,"
        "       IS_NULLABLE AS nullable, COLUMN_DEFAULT AS dflt, EXTRA AS extra,"
        "       COLLATION_NAME AS collation, COLUMN_COMMENT AS comment"
        " FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ?"
        " ORDER BY ORDINAL_POSITION",
        { schema, table }, cols);
    if (!r) return r;
    for (const auto& row : cols) {
        UltraDbColumnSchema c;
        c.name = row["name"].AsString();
        c.declaredType = row["column_type"].AsString();
        c.valueType = MysqlValueTypeForDataType(row["data_type"].AsString(), c.declaredType);
        c.nullable = row["nullable"].AsString() == "YES";
        const std::string extra = Lower(row["extra"].AsString());
        c.autoIncrement = extra.find("auto_increment") != std::string::npos;
        // VIRTUAL GENERATED / STORED GENERATED (MySQL), VIRTUAL / PERSISTENT /
        // STORED GENERATED (MariaDB).
        c.generated = extra.find("generated") != std::string::npos && extra.find("default_generated") == std::string::npos;
        if (!c.generated && !c.autoIncrement)
            c.hasDefault = DefaultExpression(row["dflt"], mariaDb, extra, row["data_type"].AsString(),
                                             c.defaultExpression);
        const std::string collation = row["collation"].AsString();
        if (!collation.empty() && collation != tableCollation) c.collation = collation;
        c.comment = row["comment"].AsString();
        out.columns.push_back(std::move(c));
    }

    if (out.kind == UltraDbObjectKind::Table) {
        // Key columns in order. An expression index (MySQL 8.0.13+) has no
        // COLUMN_NAME; MariaDB has no such indexes, nor the EXPRESSION column,
        // so the name is all that is asked for.
        UltraDbResultSet idx;
        r = conn.ExecuteDirect(
            "SELECT INDEX_NAME AS name, NON_UNIQUE AS non_unique, COLUMN_NAME AS col"
            " FROM information_schema.STATISTICS WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ?"
            " ORDER BY INDEX_NAME = 'PRIMARY' DESC, INDEX_NAME, SEQ_IN_INDEX",
            { schema, table }, idx);
        if (!r) return r;
        for (const auto& row : idx) {
            const std::string name = row["name"].AsString();
            if (out.indexes.empty() || out.indexes.back().name != name) {
                UltraDbIndexSchema index;
                index.name = name;
                index.unique = row["non_unique"].AsInt64() == 0;
                index.primary = name == "PRIMARY";
                out.indexes.push_back(std::move(index));
            }
            UltraDbIndexSchema& index = out.indexes.back();
            index.columns.push_back(row["col"].IsNull() ? std::string("(expression)") : row["col"].AsString());
            if (index.primary) out.primaryKey.push_back(index.columns.back());
        }

        UltraDbResultSet fks;
        r = conn.ExecuteDirect(
            "SELECT k.CONSTRAINT_NAME AS name, k.COLUMN_NAME AS col,"
            "       k.REFERENCED_TABLE_SCHEMA AS refschema, k.REFERENCED_TABLE_NAME AS reftable,"
            "       k.REFERENCED_COLUMN_NAME AS refcol,"
            "       c.UPDATE_RULE AS onupdate, c.DELETE_RULE AS ondelete"
            " FROM information_schema.KEY_COLUMN_USAGE k"
            " JOIN information_schema.REFERENTIAL_CONSTRAINTS c"
            "   ON c.CONSTRAINT_SCHEMA = k.CONSTRAINT_SCHEMA AND c.CONSTRAINT_NAME = k.CONSTRAINT_NAME"
            "  AND c.TABLE_NAME = k.TABLE_NAME"
            " WHERE k.TABLE_SCHEMA = ? AND k.TABLE_NAME = ? AND k.REFERENCED_TABLE_NAME IS NOT NULL"
            " ORDER BY k.CONSTRAINT_NAME, k.ORDINAL_POSITION",
            { schema, table }, fks);
        if (!r) return r;
        for (const auto& row : fks) {
            const std::string name = row["name"].AsString();
            if (out.foreignKeys.empty() || out.foreignKeys.back().name != name) {
                UltraDbForeignKeySchema fk;
                fk.name = name;
                fk.refSchema = row["refschema"].AsString();
                fk.refTable = row["reftable"].AsString();
                fk.onUpdate = row["onupdate"].AsString();
                fk.onDelete = row["ondelete"].AsString();
                out.foreignKeys.push_back(std::move(fk));
            }
            out.foreignKeys.back().columns.push_back(row["col"].AsString());
            out.foreignKeys.back().refColumns.push_back(row["refcol"].AsString());
        }

        // The statement a trigger was made with, rebuilt from its parts: the
        // server keeps the body, and the rest is fixed syntax.
        UltraDbResultSet trg;
        r = conn.ExecuteDirect(
            "SELECT TRIGGER_NAME AS name, ACTION_TIMING AS timing, EVENT_MANIPULATION AS event,"
            "       ACTION_STATEMENT AS body"
            " FROM information_schema.TRIGGERS"
            " WHERE EVENT_OBJECT_SCHEMA = ? AND EVENT_OBJECT_TABLE = ?"
            " ORDER BY ACTION_ORDER, TRIGGER_NAME",
            { schema, table }, trg);
        if (!r) return r;
        for (const auto& row : trg) {
            UltraDbTriggerSchema tr;
            tr.name = row["name"].AsString();
            tr.definition = "CREATE TRIGGER " + conn.QuoteIdentifier(tr.name) + " " +
                            row["timing"].AsString() + " " + row["event"].AsString() + " ON " +
                            conn.QuoteIdentifier(out.name) + " FOR EACH ROW " + row["body"].AsString();
            out.triggers.push_back(std::move(tr));
        }
    }
    return UltraDbResult::Ok();
}

} // namespace ultradb_internal

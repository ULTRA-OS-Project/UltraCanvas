// core/UltraDatabase/UltraDatabasePostgresSchema.cpp
// Schema introspection for the PostgreSQL driver: what UltraDb_ListSchemas,
// UltraDb_ListObjects and UltraDb_DescribeTable answer for a PostgreSQL
// database.
//
// Read from pg_catalog rather than information_schema. The standard views
// are portable but slow on a large catalog, leave out what PostgreSQL has
// beyond the standard (materialized views, identity vs serial, comments,
// sizes, expression indexes), and only show objects the user holds a
// privilege on - an admin tool must see the table before it can be granted.
// The functions PostgreSQL itself uses to print its catalog
// (format_type, pg_get_expr, pg_get_viewdef, pg_get_triggerdef,
// pg_get_indexdef) spell types and expressions exactly as psql does.
//
// Plain SQL over the connection interface, every name bound as a parameter,
// so this file compiles - and stays compiled - in a build without libpq.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraDatabaseInternal.h"

#include "UltraDatabase/UltraDatabaseSchema.h"

#include <string>
#include <vector>

namespace ultradb_internal {

namespace {

const char* const kDriver = "postgresql";

// "public" when the caller did not name a schema - where an unqualified
// CREATE TABLE puts it on a default installation.
std::string SchemaOrPublic(const std::string& schema) {
    return schema.empty() ? std::string("public") : schema;
}

UltraDbObjectKind KindFromRelkind(const std::string& relkind) {
    if (relkind == "v") return UltraDbObjectKind::View;
    if (relkind == "m") return UltraDbObjectKind::MaterializedView;
    if (relkind == "f") return UltraDbObjectKind::ForeignTable;
    if (relkind == "S") return UltraDbObjectKind::Sequence;
    return UltraDbObjectKind::Table;   // 'r' and a partitioned table, 'p'
}

// pg_constraint's one-letter referential actions, as SQL spells them.
std::string ActionName(const std::string& code) {
    if (code == "r") return "RESTRICT";
    if (code == "c") return "CASCADE";
    if (code == "n") return "SET NULL";
    if (code == "d") return "SET DEFAULT";
    return "NO ACTION";               // 'a'
}

} // namespace

UltraDbType PostgresValueTypeForOid(unsigned int typeOid) {
    // Kept in step with the driver's own mapping (WertAusFeld in
    // UltraDatabasePostgresDriver.cpp). numeric is Text on purpose: it is
    // exact, and a double is not.
    switch (typeOid) {
        case 16:  return UltraDbType::Boolean;     // bool
        case 17:  return UltraDbType::Blob;     // bytea
        case 20:                                // int8
        case 21:                                // int2
        case 23:  return UltraDbType::Int;      // int4
        case 700:                               // float4
        case 701: return UltraDbType::Double;   // float8
        default:  return UltraDbType::Text;
    }
}

UltraDbResult PostgresListSchemas(IUltraDbConnection& conn, std::vector<std::string>& out) {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult r = conn.ExecuteDirect(
        "SELECT nspname FROM pg_catalog.pg_namespace"
        " WHERE nspname NOT LIKE ? ESCAPE '\\' AND nspname <> 'information_schema'"
        " ORDER BY nspname",
        { "pg\\_%" }, rs);
    if (!r) return r;
    for (const auto& row : rs) out.push_back(row["nspname"].AsString());
    return UltraDbResult::Ok();
}

UltraDbResult PostgresListObjects(IUltraDbConnection& conn, const std::string& schemaName,
                                  std::vector<UltraDbObjectSummary>& out) {
    out.clear();
    const std::string schema = SchemaOrPublic(schemaName);

    UltraDbResultSet ns;
    UltraDbResult r = conn.ExecuteDirect(
        "SELECT oid FROM pg_catalog.pg_namespace WHERE nspname = ?", { schema }, ns);
    if (!r) return r;
    if (ns.Empty())
        return UltraDbResult::Error(UltraDbResultCode::NotFound,
                                    "no schema named '" + schema + "'", kDriver);

    UltraDbResultSet rs;
    r = conn.ExecuteDirect(
        "SELECT c.relname, c.relkind::text AS relkind"
        " FROM pg_catalog.pg_class c"
        " JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace"
        " WHERE n.nspname = ? AND c.relkind IN ('r', 'p', 'v', 'm', 'f', 'S')"
        "   AND NOT c.relispartition"
        " ORDER BY c.relname",
        { schema }, rs);
    if (!r) return r;
    for (const auto& row : rs) {
        UltraDbObjectSummary o;
        o.schema = schema;
        o.name   = row["relname"].AsString();
        o.kind   = KindFromRelkind(row["relkind"].AsString());
        out.push_back(std::move(o));
    }
    return UltraDbResult::Ok();
}

UltraDbResult PostgresDescribeTable(IUltraDbConnection& conn, const std::string& schemaName,
                                    const std::string& table, UltraDbTableSchema& out) {
    out = UltraDbTableSchema{};
    const std::string schema = SchemaOrPublic(schemaName);

    // The relation. Its oid keys every later query, so a name is looked up
    // once and the rest cannot drift onto a different object.
    UltraDbResultSet rel;
    UltraDbResult r = conn.ExecuteDirect(
        "SELECT c.oid::bigint AS oid, c.relname, c.relkind::text AS relkind,"
        "       c.reltuples::bigint AS reltuples,"
        "       CASE WHEN c.relkind IN ('r', 'p', 'm')"
        "            THEN pg_catalog.pg_total_relation_size(c.oid) END AS bytes,"
        "       CASE WHEN c.relkind IN ('v', 'm')"
        "            THEN pg_catalog.pg_get_viewdef(c.oid, true) END AS viewdef,"
        "       pg_catalog.obj_description(c.oid, 'pg_class') AS comment"
        " FROM pg_catalog.pg_class c"
        " JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace"
        " WHERE n.nspname = ? AND c.relname = ? AND c.relkind IN ('r', 'p', 'v', 'm', 'f')",
        { schema, table }, rel);
    if (!r) return r;
    if (rel.Empty())
        return UltraDbResult::Error(UltraDbResultCode::NotFound,
                                    "no table or view named '" + table + "' in '" +
                                    schema + "'", kDriver);
    const UltraDbRow& t = rel.Row(0);
    const int64_t oid = t["oid"].AsInt64();
    out.schema = schema;
    out.name   = t["relname"].AsString();
    out.kind   = KindFromRelkind(t["relkind"].AsString());
    out.viewDefinition = t["viewdef"].AsString();
    out.comment = t["comment"].AsString();
    const std::string relkind = t["relkind"].AsString();
    // reltuples is -1 until the table is first analysed (PostgreSQL 14+), and
    // means nothing for a view.
    if (relkind == "r" || relkind == "p" || relkind == "m") {
        const int64_t estimate = t["reltuples"].AsInt64();
        if (estimate >= 0) out.approxRows = estimate;
    }
    if (!t["bytes"].IsNull()) out.sizeBytes = t["bytes"].AsInt64();

    // Columns. The collation is reported only where it differs from the
    // type's own, which is what makes it worth showing.
    UltraDbResultSet cols;
    r = conn.ExecuteDirect(
        "SELECT a.attname,"
        "       pg_catalog.format_type(a.atttypid, a.atttypmod) AS type,"
        "       a.atttypid::bigint AS typeoid,"
        "       NOT a.attnotnull AS nullable,"
        "       pg_catalog.pg_get_expr(d.adbin, d.adrelid) AS dflt,"
        "       a.attidentity::text AS identity,"
        "       a.attgenerated::text AS generated,"
        "       CASE WHEN a.attcollation <> ty.typcollation THEN co.collname END AS collation,"
        "       pg_catalog.col_description(a.attrelid, a.attnum) AS comment"
        " FROM pg_catalog.pg_attribute a"
        " JOIN pg_catalog.pg_type ty ON ty.oid = a.atttypid"
        " LEFT JOIN pg_catalog.pg_attrdef d ON d.adrelid = a.attrelid AND d.adnum = a.attnum"
        " LEFT JOIN pg_catalog.pg_collation co ON co.oid = a.attcollation"
        " WHERE a.attrelid = ?::oid AND a.attnum > 0 AND NOT a.attisdropped"
        " ORDER BY a.attnum",
        { oid }, cols);
    if (!r) return r;
    for (const auto& row : cols) {
        UltraDbColumnSchema c;
        c.name         = row["attname"].AsString();
        c.declaredType = row["type"].AsString();
        c.valueType    = PostgresValueTypeForOid(static_cast<unsigned int>(row["typeoid"].AsInt64()));
        c.nullable     = row["nullable"].AsBool();
        const std::string identity  = row["identity"].AsString();
        const std::string generated = row["generated"].AsString();
        c.generated    = !generated.empty();
        // A generated column's expression is in adbin too; it is not a default.
        c.hasDefault   = !row["dflt"].IsNull() && !c.generated;
        if (c.hasDefault) c.defaultExpression = row["dflt"].AsString();
        c.autoIncrement = !identity.empty() ||
                          (c.hasDefault && c.defaultExpression.rfind("nextval(", 0) == 0);
        c.collation    = row["collation"].AsString();
        c.comment      = row["comment"].AsString();
        out.columns.push_back(std::move(c));
    }

    // Indexes, key columns only (INCLUDE columns are carried, not keyed). An
    // expression column has attnum 0, and pg_get_indexdef prints it.
    UltraDbResultSet idx;
    r = conn.ExecuteDirect(
        "SELECT ic.relname AS name, i.indisunique AS is_unique, i.indisprimary AS is_primary,"
        "       k.ord, a.attname,"
        "       pg_catalog.pg_get_indexdef(i.indexrelid, k.ord::int, true) AS expr"
        " FROM pg_catalog.pg_index i"
        " JOIN pg_catalog.pg_class ic ON ic.oid = i.indexrelid"
        " CROSS JOIN LATERAL unnest(i.indkey::int2[]) WITH ORDINALITY AS k(attnum, ord)"
        " LEFT JOIN pg_catalog.pg_attribute a"
        "        ON a.attrelid = i.indrelid AND a.attnum = k.attnum AND k.attnum > 0"
        " WHERE i.indrelid = ?::oid AND k.ord <= i.indnkeyatts"
        " ORDER BY ic.relname, k.ord",
        { oid }, idx);
    if (!r) return r;
    for (const auto& row : idx) {
        const std::string name = row["name"].AsString();
        if (out.indexes.empty() || out.indexes.back().name != name) {
            UltraDbIndexSchema index;
            index.name    = name;
            index.unique  = row["is_unique"].AsBool();
            index.primary = row["is_primary"].AsBool();
            out.indexes.push_back(std::move(index));
        }
        UltraDbIndexSchema& index = out.indexes.back();
        index.columns.push_back(row["attname"].IsNull() ? row["expr"].AsString()
                                                        : row["attname"].AsString());
        if (index.primary) out.primaryKey.push_back(index.columns.back());
    }

    // Foreign keys: conkey and confkey pair up position by position.
    UltraDbResultSet fks;
    r = conn.ExecuteDirect(
        "SELECT con.conname, k.ord, a.attname AS col, rn.nspname AS refschema,"
        "       rc.relname AS reftable, ra.attname AS refcol,"
        "       con.confupdtype::text AS onupdate, con.confdeltype::text AS ondelete"
        " FROM pg_catalog.pg_constraint con"
        " JOIN pg_catalog.pg_class rc ON rc.oid = con.confrelid"
        " JOIN pg_catalog.pg_namespace rn ON rn.oid = rc.relnamespace"
        " CROSS JOIN LATERAL unnest(con.conkey, con.confkey) WITH ORDINALITY AS k(attnum, refattnum, ord)"
        " JOIN pg_catalog.pg_attribute a ON a.attrelid = con.conrelid AND a.attnum = k.attnum"
        " JOIN pg_catalog.pg_attribute ra ON ra.attrelid = con.confrelid AND ra.attnum = k.refattnum"
        " WHERE con.conrelid = ?::oid AND con.contype = 'f'"
        " ORDER BY con.conname, k.ord",
        { oid }, fks);
    if (!r) return r;
    for (const auto& row : fks) {
        const std::string name = row["conname"].AsString();
        if (out.foreignKeys.empty() || out.foreignKeys.back().name != name) {
            UltraDbForeignKeySchema fk;
            fk.name      = name;
            fk.refSchema = row["refschema"].AsString();
            fk.refTable  = row["reftable"].AsString();
            fk.onUpdate  = ActionName(row["onupdate"].AsString());
            fk.onDelete  = ActionName(row["ondelete"].AsString());
            out.foreignKeys.push_back(std::move(fk));
        }
        out.foreignKeys.back().columns.push_back(row["col"].AsString());
        out.foreignKeys.back().refColumns.push_back(row["refcol"].AsString());
    }

    // Triggers the user wrote; the ones PostgreSQL makes for foreign keys are
    // internal and would only confuse.
    UltraDbResultSet trg;
    r = conn.ExecuteDirect(
        "SELECT tgname, pg_catalog.pg_get_triggerdef(oid, true) AS def"
        " FROM pg_catalog.pg_trigger WHERE tgrelid = ?::oid AND NOT tgisinternal"
        " ORDER BY tgname",
        { oid }, trg);
    if (!r) return r;
    for (const auto& row : trg)
        out.triggers.push_back({ row["tgname"].AsString(), row["def"].AsString() });

    return UltraDbResult::Ok();
}

} // namespace ultradb_internal

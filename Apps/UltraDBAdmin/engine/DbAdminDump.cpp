// Apps/UltraDBAdmin/engine/DbAdminDump.cpp
// Version: 0.2.0 - MySQL / MariaDB: SHOW CREATE TABLE, rows, triggers between DELIMITER lines, views
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminDump.h"

#include "DbAdminExport.h"       // QuotedName, SqlLiteral
#include "DbAdminOperations.h"   // QualifiedName
#include "DbAdminRoutines.h"     // WithoutDefiner
#include "DbAdminSqliteTable.h"

#include "UltraCanvasPathUtf8.h"

#include <cstdio>
#include <filesystem>

namespace UltraDBAdmin {
namespace {

std::string Lower(std::string text) {
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

std::string StringLiteral(const std::string& text) {
    std::string out = "'";
    for (const char c : text) {
        if (c == '\'') out += '\'';
        out += c;
    }
    return out + "'";
}

// pg_get_viewdef ends its SELECT with ';' and a newline.
std::string WithoutTrailingSemicolon(std::string sql) {
    while (!sql.empty() && (sql.back() == ';' || sql.back() == ' ' || sql.back() == '\n' || sql.back() == '\r' ||
                            sql.back() == '\t'))
        sql.pop_back();
    return sql;
}

// A file written beside its name and moved there when complete - as
// ExportWriter writes - so a dump that fails or is stopped leaves the file
// that was there.
class PartFile {
public:
    ~PartFile() {
        if (!file_) return;
        std::fclose(file_);
        std::error_code ec;
        std::filesystem::remove(UltraCanvas::PathFromUtf8(partial_), ec);
    }
    bool Open(const std::string& path) {
        path_ = path;
        partial_ = path + ".part";
        file_ = UltraCanvas::OpenFileUtf8(partial_, "wb");
        return file_ != nullptr;
    }
    bool Put(const std::string& text) {
        return text.empty() || std::fwrite(text.data(), 1, text.size(), file_) == text.size();
    }
    bool Finish(std::string& error) {
        const bool closed = std::fclose(file_) == 0;
        file_ = nullptr;
        std::error_code ec;
        if (!closed) {
            std::filesystem::remove(UltraCanvas::PathFromUtf8(partial_), ec);
            error = "the file could not be completed";
            return false;
        }
        std::filesystem::rename(UltraCanvas::PathFromUtf8(partial_), UltraCanvas::PathFromUtf8(path_), ec);
        if (ec) {
            std::filesystem::remove(UltraCanvas::PathFromUtf8(partial_), ec);
            error = "it could not be put in place: " + ec.message();
            return false;
        }
        return true;
    }

private:
    std::string path_, partial_;
    std::FILE* file_ = nullptr;
};

UltraDbResult FileError(const std::string& path, const std::string& error) {
    return UltraDbResult::Error(UltraDbResultCode::Internal,
                                UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(path).filename()) + ": " + error);
}

UltraDbResult Cancelled() {
    return UltraDbResult::Error(UltraDbResultCode::Cancelled, "the dump was stopped; no file was written");
}

// One table's rows as INSERT statements, a cursor page at a time.
struct RowSource {
    std::string target;                 // as the INSERTs name the table
    std::vector<std::string> columns;   // the columns written, in order
    std::string query;                  // what reads them
    bool overriding = false;            // PostgreSQL: OVERRIDING SYSTEM VALUE
};

UltraDbResult WriteRows(const std::string& connection, SqlEngine engine, const RowSource& source,
                        const DumpOptions& options, PartFile& file, const std::string& path, size_t& rows,
                        const std::atomic<bool>* cancel,
                        const std::function<void(const std::string&, size_t)>& progress, const std::string& label) {
    if (source.columns.empty()) return UltraDbResult::Ok();
    std::string names;
    for (const auto& column : source.columns) names += (names.empty() ? "" : ", ") + QuotedName(engine, column);
    const std::string head = "INSERT INTO " + source.target + " (" + names + ")" +
                             (source.overriding ? " OVERRIDING SYSTEM VALUE" : "") + " VALUES (";
    UltraDbResult error;
    const UltraDbHandle cursor = UltraDb_OpenCursor(connection, source.query, {}, &error);
    if (cursor == UltraDbInvalidHandle) return error;
    bool exhausted = false;
    UltraDbResult result = UltraDbResult::Ok();
    while (!exhausted) {
        if (cancel && cancel->load()) { result = Cancelled(); break; }
        UltraDbResultSet page;
        result = UltraDb_FetchRows(cursor, options.pageRows, page, exhausted);
        if (!result) break;
        std::string text;
        for (size_t r = 0; r < page.Size(); ++r) {
            const UltraDbRow& row = page.Row(r);
            text += head;
            for (size_t i = 0; i < row.Size(); ++i) text += (i == 0 ? "" : ", ") + SqlLiteral(row[i], engine);
            text += ");\n";
        }
        if (!file.Put(text)) { result = FileError(path, "the file could not be written (is the disk full?)"); break; }
        rows += page.Size();
        if (progress) progress(label, rows);
    }
    UltraDb_CloseCursor(cursor);
    return result;
}

std::string SelectColumns(SqlEngine engine, const std::vector<std::string>& columns, const std::string& target,
                          const std::vector<std::string>& key) {
    std::string names;
    for (const auto& column : columns) names += (names.empty() ? "" : ", ") + QuotedName(engine, column);
    std::string sql = "SELECT " + names + " FROM " + target;
    std::string order;
    for (const auto& column : key) order += (order.empty() ? "" : ", ") + QuotedName(engine, column);
    if (!order.empty()) sql += " ORDER BY " + order;
    return sql;
}

std::vector<std::string> WritableColumns(const UltraDbTableSchema& table) {
    std::vector<std::string> names;
    for (const auto& column : table.columns)
        if (!column.generated) names.push_back(column.name);
    return names;
}

// ----- SQLite -----

UltraDbResult DumpSqlite(const std::string& connection, const std::string& schema,
                         const std::vector<UltraDbObjectSummary>& objects, const DumpOptions& options, PartFile& file,
                         const std::string& path, size_t& tables, size_t& rows, const std::atomic<bool>* cancel,
                         const std::function<void(const std::string&, size_t)>& progress) {
    std::string after;          // indexes, triggers, views: once every table and row is in
    std::string text = "-- SQLite dump of " + schema + ", written by UltraDBAdmin.\n"
                       "PRAGMA foreign_keys=OFF;\nBEGIN TRANSACTION;\n";
    bool anySequence = false;
    for (const auto& object : objects) {
        if (object.kind == UltraDbObjectKind::View) {
            if (!options.structure) continue;
            UltraDbTableSchema view;
            const UltraDbResult described = UltraDb_DescribeTable(connection, schema, object.name, view);
            if (!described) return described;
            if (options.dropFirst) text += "DROP VIEW IF EXISTS " + QuotedName(SqlEngine::Sqlite, object.name) + ";\n";
            after += WithoutTrailingSemicolon(view.viewDefinition) + ";\n";
            continue;
        }
        if (object.kind != UltraDbObjectKind::Table) continue;
        UltraDbTableSchema table;
        UltraDbResult result = UltraDb_DescribeTable(connection, schema, object.name, table);
        if (!result) return result;
        SqliteTableSource source;
        result = ReadSqliteTableSource(connection, schema, object.name, source);
        if (!result) return result;
        if (source.sequence >= 0) anySequence = true;
        if (options.structure) {
            if (options.dropFirst) text += "DROP TABLE IF EXISTS " + QuotedName(SqlEngine::Sqlite, object.name) + ";\n";
            text += source.createSql + ";\n";
            for (const auto& [name, sql] : source.indexes) after += sql + ";\n";
            for (const auto& trigger : source.triggers) after += trigger + ";\n";
        }
        if (!file.Put(text)) return FileError(path, "the file could not be written (is the disk full?)");
        text.clear();
        ++tables;
        if (options.data) {
            RowSource rowSource;
            rowSource.target = QuotedName(SqlEngine::Sqlite, object.name);
            rowSource.columns = WritableColumns(table);
            rowSource.query = SelectColumns(SqlEngine::Sqlite, rowSource.columns, QualifiedName(SqlEngine::Sqlite, schema, object.name), table.primaryKey);
            size_t tableRows = 0;
            result = WriteRows(connection, SqlEngine::Sqlite, rowSource, options, file, path, tableRows, cancel,
                               progress, object.name);
            rows += tableRows;
            if (!result) return result;
        }
    }
    // The AUTOINCREMENT counters, as sqlite3's .dump writes them.
    if (options.data && anySequence) {
        UltraDbResultSet counters;
        const UltraDbResult result =
            UltraDb_Query(connection, "SELECT name, seq FROM " + QualifiedName(SqlEngine::Sqlite, schema, "sqlite_sequence"), {}, counters);
        if (result) {
            text += "DELETE FROM sqlite_sequence;\n";
            for (size_t i = 0; i < counters.Size(); ++i)
                text += "INSERT INTO sqlite_sequence VALUES(" + SqlLiteral(counters.Row(i)["name"], SqlEngine::Sqlite) +
                        ", " + SqlLiteral(counters.Row(i)["seq"], SqlEngine::Sqlite) + ");\n";
        }
    }
    text += after + "COMMIT;\n";
    if (!file.Put(text)) return FileError(path, "the file could not be written (is the disk full?)");
    return UltraDbResult::Ok();
}

// ----- PostgreSQL -----

UltraDbResult DumpPostgres(const std::string& connection, const std::string& schema,
                           const std::vector<UltraDbObjectSummary>& objects, const DumpOptions& options, PartFile& file,
                           const std::string& path, size_t& tables, size_t& rows, const std::atomic<bool>* cancel,
                           const std::function<void(const std::string&, size_t)>& progress) {
    // Every definition is read first: the drops go before any table is made,
    // so a foreign key between two of them never holds a DROP up.
    struct Table {
        UltraDbTableSchema schema;
        PostgresDdl ddl;
    };
    std::vector<Table> tableList;
    std::string drops, views, viewDrops;
    std::vector<std::string> skipped;
    for (const auto& object : objects) {
        const std::string target = QualifiedName(SqlEngine::Postgres, schema, object.name);
        if (object.kind == UltraDbObjectKind::View || object.kind == UltraDbObjectKind::MaterializedView) {
            if (!options.structure) continue;
            UltraDbTableSchema view;
            const UltraDbResult described = UltraDb_DescribeTable(connection, schema, object.name, view);
            if (!described) return described;
            const bool materialized = object.kind == UltraDbObjectKind::MaterializedView;
            const std::string word = materialized ? "MATERIALIZED VIEW " : "VIEW ";
            viewDrops = "DROP " + word + "IF EXISTS " + target + ";\n" + viewDrops;
            views += "CREATE " + word + target + " AS\n" + WithoutTrailingSemicolon(view.viewDefinition) +
                     (materialized ? "\nWITH DATA;\n" : ";\n");
            continue;
        }
        if (object.kind != UltraDbObjectKind::Table) {
            skipped.push_back(object.name);
            continue;
        }
        Table table;
        UltraDbResult result = UltraDb_DescribeTable(connection, schema, object.name, table.schema);
        if (!result) return result;
        result = PostgresTableDdl(connection, table.schema, table.ddl);
        if (!result) return result;
        tableList.push_back(std::move(table));
    }

    std::string text = "-- PostgreSQL dump of the schema " + schema + ", written by UltraDBAdmin.\n"
                       "-- Tables, rows, constraints, indexes, comments and views (in name order). Functions,\n"
                       "-- triggers, types, extensions and privileges are pg_dump's to write.\n"
                       "BEGIN;\n";
    if (options.structure) {
        text += "CREATE SCHEMA IF NOT EXISTS " + QuotedName(SqlEngine::Postgres, schema) + ";\n";
        if (options.dropFirst) {
            text += viewDrops;
            for (const auto& table : tableList)
                for (const auto& constraint : table.schema.foreignKeys)
                    if (!constraint.name.empty())
                        text += "ALTER TABLE IF EXISTS " + QualifiedName(SqlEngine::Postgres, schema, table.schema.name) +
                                " DROP CONSTRAINT IF EXISTS " + QuotedName(SqlEngine::Postgres, constraint.name) + ";\n";
            for (const auto& table : tableList)
                text += "DROP TABLE IF EXISTS " + QualifiedName(SqlEngine::Postgres, schema, table.schema.name) + ";\n";
        }
    }
    std::string after, foreignKeys;
    for (const auto& table : tableList) {
        const std::string target = QualifiedName(SqlEngine::Postgres, schema, table.schema.name);
        if (options.structure) {
            for (const auto& note : table.ddl.notes) text += "-- " + note + "\n";
            text += table.ddl.create + ";\n";
            for (const auto& index : table.ddl.indexes) after += index + ";\n";
            for (const auto& comment : table.ddl.comments) after += comment + ";\n";
            for (const auto& constraint : table.ddl.foreignKeys) foreignKeys += constraint + ";\n";
        }
        if (!file.Put(text)) return FileError(path, "the file could not be written (is the disk full?)");
        text.clear();
        ++tables;
        if (!options.data) continue;
        RowSource source;
        source.target = target;
        source.columns = table.ddl.insertColumns;
        source.overriding = table.ddl.overridingSystemValue;
        source.query = SelectColumns(SqlEngine::Postgres, source.columns, target, table.schema.primaryKey);
        size_t tableRows = 0;
        const UltraDbResult result = WriteRows(connection, SqlEngine::Postgres, source, options, file, path, tableRows,
                                               cancel, progress, table.schema.name);
        rows += tableRows;
        if (!result) return result;
        for (const auto& counter : table.ddl.counters) text += counter + ";\n";
    }
    if (!skipped.empty()) {
        std::string names;
        for (const auto& name : skipped) names += (names.empty() ? "" : ", ") + name;
        text += "-- Not written (sequences of their own, foreign tables): " + names + "\n";
    }
    text += after + foreignKeys + views + "COMMIT;\n";
    if (!file.Put(text)) return FileError(path, "the file could not be written (is the disk full?)");
    return UltraDbResult::Ok();
}

// ----- MySQL / MariaDB -----

// The text of a SHOW CREATE ... answer's `column`, the DEFINER clause left out
// so the file loads under any account.
UltraDbResult ShowCreate(const std::string& connection, const std::string& what, const std::string& target,
                         const char* column, std::string& out) {
    UltraDbResultSet rows;
    const UltraDbResult result = UltraDb_Query(connection, "SHOW CREATE " + what + " " + target, {}, rows);
    if (!result) return result;
    if (rows.Size() != 1 || rows.Row(0)[column].IsNull())
        return UltraDbResult::Error(UltraDbResultCode::QueryFailed, "SHOW CREATE " + what + " " + target +
                                                                         " gave no definition (is it this user's?)");
    out = WithoutDefiner(rows.Row(0)[column].AsString());
    return UltraDbResult::Ok();
}

// `sql` without `quotedSchema` + "." before the names in it: SHOW CREATE
// VIEW names every table with its database, which would tie the file to it.
std::string WithoutSchema(std::string sql, const std::string& quotedSchema) {
    const std::string prefix = quotedSchema + ".";
    for (size_t at = sql.find(prefix); at != std::string::npos; at = sql.find(prefix, at)) sql.erase(at, prefix.size());
    return sql;
}

UltraDbResult DumpMysql(const std::string& connection, const std::string& schema,
                        const std::vector<UltraDbObjectSummary>& objects, const DumpOptions& options, PartFile& file,
                        const std::string& path, size_t& tables, size_t& rows, const std::atomic<bool>* cancel,
                        const std::function<void(const std::string&, size_t)>& progress) {
    constexpr SqlEngine engine = SqlEngine::MySql;
    // Names are written without the database, as mysqldump writes them: the
    // file loads into whichever database is in use.
    std::string text = "-- MySQL / MariaDB dump of the database " + schema + ", written by UltraDBAdmin.\n"
                       "-- Tables, rows, indexes, foreign keys, triggers and views (in name order).\n"
                       "-- Procedures, functions and events are mysqldump --routines --events' to write.\n"
                       "SET NAMES utf8mb4;\n"
                       "SET FOREIGN_KEY_CHECKS = 0;\n";
    std::string triggers, views, viewDrops;
    for (const auto& object : objects) {
        const std::string target = QualifiedName(engine, schema, object.name);
        const std::string name = QuotedName(engine, object.name);
        if (object.kind == UltraDbObjectKind::View) {
            if (!options.structure) continue;
            std::string create;
            const UltraDbResult result = ShowCreate(connection, "VIEW", target, "Create View", create);
            if (!result) return result;
            if (options.dropFirst) viewDrops += "DROP VIEW IF EXISTS " + name + ";\n";
            views += WithoutSchema(WithoutTrailingSemicolon(create), QuotedName(engine, schema)) + ";\n";
            continue;
        }
        if (object.kind != UltraDbObjectKind::Table) continue;
        UltraDbTableSchema table;
        UltraDbResult result = UltraDb_DescribeTable(connection, schema, object.name, table);
        if (!result) return result;
        if (options.structure) {
            std::string create;
            result = ShowCreate(connection, "TABLE", target, "Create Table", create);
            if (!result) return result;
            if (options.dropFirst) text += "DROP TABLE IF EXISTS " + name + ";\n";
            text += create + ";\n";
            // A trigger's body holds ';': each goes between DELIMITER lines,
            // which the SQL tab's import and the mysql client both read. Its
            // definition as the introspection rebuilds it names the trigger
            // and its table without the database, as the file needs.
            for (const auto& trigger : table.triggers) {
                if (options.dropFirst) triggers += "DROP TRIGGER IF EXISTS " + QuotedName(engine, trigger.name) + ";\n";
                triggers += "DELIMITER ;;\n" + WithoutTrailingSemicolon(trigger.definition) + ";;\nDELIMITER ;\n";
            }
        }
        if (!file.Put(text)) return FileError(path, "the file could not be written (is the disk full?)");
        text.clear();
        ++tables;
        if (!options.data) continue;
        RowSource source;
        source.target = name;
        source.columns = WritableColumns(table);
        source.query = SelectColumns(engine, source.columns, target, table.primaryKey);
        size_t tableRows = 0;
        result = WriteRows(connection, engine, source, options, file, path, tableRows, cancel, progress, object.name);
        rows += tableRows;
        if (!result) return result;
    }
    // Views last, once every table they read is there; one view reading
    // another loads when its name comes later.
    text += triggers + viewDrops + views + "SET FOREIGN_KEY_CHECKS = 1;\n";
    if (!file.Put(text)) return FileError(path, "the file could not be written (is the disk full?)");
    return UltraDbResult::Ok();
}

} // namespace

UltraDbResult PostgresTableDdl(const std::string& connection, const UltraDbTableSchema& table, PostgresDdl& out) {
    out = PostgresDdl();
    const std::string target = QualifiedName(SqlEngine::Postgres, table.schema, table.name);
    const UltraDbParams self{UltraDbValue(target)};

    UltraDbResultSet columns;
    UltraDbResult result = UltraDb_Query(
        connection,
        "SELECT a.attname AS name, format_type(a.atttypid, a.atttypmod) AS type, a.attnotnull AS notnull,"
        "       a.attidentity::text AS identity, a.attgenerated::text AS generated,"
        "       pg_get_expr(d.adbin, d.adrelid) AS expr,"
        "       CASE WHEN a.attcollation <> t.typcollation THEN quote_ident(co.collname) END AS collation,"
        "       col_description(a.attrelid, a.attnum) AS comment,"
        "       pg_get_serial_sequence(?, a.attname) AS serial"
        "  FROM pg_catalog.pg_attribute a"
        "  JOIN pg_catalog.pg_type t ON t.oid = a.atttypid"
        "  LEFT JOIN pg_catalog.pg_attrdef d ON d.adrelid = a.attrelid AND d.adnum = a.attnum"
        "  LEFT JOIN pg_catalog.pg_collation co ON co.oid = a.attcollation"
        " WHERE a.attrelid = CAST(? AS regclass) AND a.attnum > 0 AND NOT a.attisdropped"
        " ORDER BY a.attnum",
        {UltraDbValue(target), UltraDbValue(target)}, columns);
    if (!result) return result;

    std::vector<std::string> definitions;
    for (size_t i = 0; i < columns.Size(); ++i) {
        const UltraDbRow& row = columns.Row(i);
        const std::string name = row["name"].AsString();
        std::string type = row["type"].AsString();
        const std::string identity = row["identity"].AsString();
        const std::string generated = row["generated"].AsString();
        const std::string expr = row["expr"].IsNull() ? std::string() : row["expr"].AsString();
        const bool serial = !row["serial"].IsNull() && identity.empty() && expr.rfind("nextval(", 0) == 0;
        std::string definition = "    " + QuotedName(SqlEngine::Postgres, name) + " ";
        if (serial) {
            // A serial makes its own sequence, as the original's was made.
            const std::string lower = Lower(type);
            type = lower == "bigint" ? "bigserial" : lower == "smallint" ? "smallserial" : "serial";
        }
        definition += type;
        if (!row["collation"].IsNull()) definition += " COLLATE " + row["collation"].AsString();
        if (identity == "a") {
            definition += " GENERATED ALWAYS AS IDENTITY";
            out.overridingSystemValue = true;
        } else if (identity == "d") {
            definition += " GENERATED BY DEFAULT AS IDENTITY";
        } else if (generated == "s") {
            definition += " GENERATED ALWAYS AS (" + expr + ") STORED";
        } else if (!expr.empty() && !serial) {
            definition += " DEFAULT " + expr;
        }
        if (row["notnull"].AsBool() && !serial && identity.empty()) definition += " NOT NULL";
        definitions.push_back(definition);
        if (generated.empty()) out.insertColumns.push_back(name);
        if (!identity.empty() || serial) {
            out.counters.push_back("SELECT setval(pg_get_serial_sequence(" + StringLiteral(target) + ", " +
                                   StringLiteral(name) + "), COALESCE(MAX(" + QuotedName(SqlEngine::Postgres, name) + "), 0) + 1, false) FROM " +
                                   target);
        }
        if (!row["comment"].IsNull())
            out.comments.push_back("COMMENT ON COLUMN " + target + "." + QuotedName(SqlEngine::Postgres, name) + " IS " +
                                   StringLiteral(row["comment"].AsString()));
    }

    // Constraints: the key, unique, CHECK and exclusion ones in the table;
    // foreign keys once every table exists. NOT NULL is the column's.
    UltraDbResultSet constraints;
    result = UltraDb_Query(connection,
                           "SELECT conname AS name, contype::text AS type, pg_get_constraintdef(oid) AS def"
                           "  FROM pg_catalog.pg_constraint WHERE conrelid = CAST(? AS regclass)"
                           " ORDER BY CASE contype WHEN 'p' THEN 0 WHEN 'u' THEN 1 ELSE 2 END, conname",
                           self, constraints);
    if (!result) return result;
    for (size_t i = 0; i < constraints.Size(); ++i) {
        const UltraDbRow& row = constraints.Row(i);
        const std::string type = row["type"].AsString();
        const std::string clause = "CONSTRAINT " + QuotedName(SqlEngine::Postgres, row["name"].AsString()) + " " + row["def"].AsString();
        if (type == "p" || type == "u" || type == "c" || type == "x") definitions.push_back("    " + clause);
        else if (type == "f") out.foreignKeys.push_back("ALTER TABLE " + target + " ADD " + clause);
    }

    std::string create = "CREATE TABLE " + target + " (\n";
    for (size_t i = 0; i < definitions.size(); ++i) create += definitions[i] + (i + 1 < definitions.size() ? ",\n" : "\n");
    out.create = create + ")";

    // The indexes no constraint owns.
    UltraDbResultSet indexes;
    result = UltraDb_Query(connection,
                           "SELECT pg_get_indexdef(i.indexrelid) AS def FROM pg_catalog.pg_index i"
                           " WHERE i.indrelid = CAST(? AS regclass) AND NOT EXISTS ("
                           "   SELECT 1 FROM pg_catalog.pg_constraint c"
                           "    WHERE c.conindid = i.indexrelid AND c.conrelid = i.indrelid)"
                           " ORDER BY i.indexrelid",
                           self, indexes);
    if (!result) return result;
    for (size_t i = 0; i < indexes.Size(); ++i) out.indexes.push_back(indexes.Row(i)["def"].AsString());

    UltraDbResultSet comment;
    result = UltraDb_Query(connection,
                           "SELECT obj_description(CAST(? AS regclass), 'pg_class') AS comment", self, comment);
    if (!result) return result;
    if (comment.Size() == 1 && !comment.Row(0)["comment"].IsNull())
        out.comments.insert(out.comments.begin(),
                            "COMMENT ON TABLE " + target + " IS " + StringLiteral(comment.Row(0)["comment"].AsString()));
    if (!table.triggers.empty())
        out.notes.push_back(table.name + " has " + std::to_string(table.triggers.size()) +
                            (table.triggers.size() == 1 ? " trigger" : " triggers") +
                            ", not written: a trigger calls a function, which is pg_dump's to write.");
    return UltraDbResult::Ok();
}

UltraDbResult DumpSchema(const std::string& connection, SqlEngine engine, const std::string& schema,
                         const std::string& path, const DumpOptions& options, size_t& tables, size_t& rows,
                         const std::atomic<bool>* cancel,
                         const std::function<void(const std::string& table, size_t rows)>& progress) {
    tables = 0;
    rows = 0;
    if (!options.structure && !options.data)
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument, "a dump of neither structure nor rows is empty");
    std::vector<UltraDbObjectSummary> objects;
    UltraDbResult result = UltraDb_ListObjects(connection, schema, objects);
    if (!result) return result;
    PartFile file;
    if (!file.Open(path)) return FileError(path, "it cannot be written there");
    result = engine == SqlEngine::Sqlite
                 ? DumpSqlite(connection, schema, objects, options, file, path, tables, rows, cancel, progress)
             : engine == SqlEngine::MySql
                 ? DumpMysql(connection, schema, objects, options, file, path, tables, rows, cancel, progress)
                 : DumpPostgres(connection, schema, objects, options, file, path, tables, rows, cancel, progress);
    if (!result) return result;            // the part file goes with `file`
    std::string error;
    if (!file.Finish(error)) return FileError(path, error);
    return UltraDbResult::Ok();
}

} // namespace UltraDBAdmin

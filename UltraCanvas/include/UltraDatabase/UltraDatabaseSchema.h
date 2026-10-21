// include/UltraDatabase/UltraDatabaseSchema.h
// What a database contains, asked of the engine: its schemas, the tables and
// views in each, and for one table its columns, primary key, foreign keys,
// indexes and triggers - one engine-neutral model, filled by each driver from
// its own catalog (SQLite's sqlite_master and table-valued pragmas,
// PostgreSQL's pg_catalog). Also the one sanctioned way to put a table or
// column NAME into SQL text: values are always bound, but a name cannot be,
// and every hand-written quoter is a place for an injection to start.
//
// Used by the UltraDBAdmin application and by anything that has to look at a
// schema it did not write (the ER diagram's live import, a migration diff).
// A driver that cannot introspect answers Unsupported.
//
// doc-check: example
//   #include <UltraDatabase/UltraDatabase.h>
//
//   UltraDbConnectionConfig cfg;
//   cfg.name = "app"; cfg.driver = "sqlite"; cfg.database = ":memory:";
//   UltraDb_RegisterConnection(cfg);
//   UltraDb_Exec("app", "CREATE TABLE customers(id INTEGER PRIMARY KEY, name TEXT NOT NULL)");
//
//   std::vector<UltraDbObjectSummary> objects;
//   UltraDb_ListObjects("app", "main", objects);
//
//   UltraDbTableSchema table;
//   if (UltraDb_DescribeTable("app", "main", "customers", table)) {
//       std::string name;
//       UltraDb_QuoteQualifiedName("app", table.schema, table.name, name);
//       UltraDbResultSet rows;
//       UltraDb_Query("app", "SELECT * FROM " + name + " LIMIT ?", { 50 }, rows);
//   }
//
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDatabaseCore.h"
#include "UltraDatabaseValue.h"

#include <cstdint>
#include <string>
#include <vector>

// The kinds of object a schema lists. SQLite has only tables and views;
// PostgreSQL has all of them.
enum class UltraDbObjectKind {
    Table = 0,
    View,
    MaterializedView,
    ForeignTable,
    Sequence
};

// One entry of UltraDb_ListObjects.
struct UltraDbObjectSummary {
    std::string       schema;
    std::string       name;
    UltraDbObjectKind kind = UltraDbObjectKind::Table;
};

struct UltraDbColumnSchema {
    std::string name;
    std::string declaredType;      // as the engine spells it: "varchar(80)", "INTEGER", "" (SQLite, untyped)
    UltraDbType valueType = UltraDbType::Text;  // how its values come back
    bool        nullable = true;
    bool        autoIncrement = false;  // identity / serial / SQLite's INTEGER PRIMARY KEY
    bool        generated = false;      // computed by the engine; not writable
    bool        hasDefault = false;
    std::string defaultExpression;      // as written in the DDL; meaningful when hasDefault
    std::string collation;              // only when it differs from the type's default
    std::string comment;
};

// Referential actions, spelled as SQL spells them: "NO ACTION", "RESTRICT",
// "CASCADE", "SET NULL", "SET DEFAULT".
struct UltraDbForeignKeySchema {
    std::string              name;        // constraint name ("" where SQLite has none)
    std::vector<std::string> columns;     // in this table
    std::string              refSchema;
    std::string              refTable;
    std::vector<std::string> refColumns;  // same order as `columns`
    std::string              onUpdate = "NO ACTION";
    std::string              onDelete = "NO ACTION";
};

struct UltraDbIndexSchema {
    std::string              name;
    std::vector<std::string> columns;     // key columns in order; an expression index lists the expression text
    bool                     unique = false;
    bool                     primary = false;
};

struct UltraDbTriggerSchema {
    std::string name;
    std::string definition;               // the CREATE TRIGGER statement
};

struct UltraDbTableSchema {
    std::string       schema;
    std::string       name;
    UltraDbObjectKind kind = UltraDbObjectKind::Table;
    std::vector<UltraDbColumnSchema>     columns;     // in table order
    std::vector<std::string>             primaryKey;  // column names in key order; empty if none
    std::vector<UltraDbForeignKeySchema> foreignKeys;
    std::vector<UltraDbIndexSchema>      indexes;
    std::vector<UltraDbTriggerSchema>    triggers;
    std::string       viewDefinition;     // the SELECT, for a view or materialized view
    std::string       comment;
    int64_t           approxRows = -1;    // the engine's estimate; -1 when it keeps none
    int64_t           sizeBytes = -1;     // on disk, with indexes; -1 when unknown
};

// The schemas of a connection, in the engine's order: for SQLite the attached
// databases ("main", "temp", any ATTACHed name), for PostgreSQL the namespaces
// apart from pg_catalog, information_schema and the other system ones.
UltraDbResult UltraDb_ListSchemas(const std::string& connection,
                                  std::vector<std::string>& out);

// The tables, views, materialized views, foreign tables and sequences of one
// schema, sorted by name. SQLite's own sqlite_* tables are left out.
UltraDbResult UltraDb_ListObjects(const std::string& connection,
                                  const std::string& schema,
                                  std::vector<UltraDbObjectSummary>& out);

// Everything about one table or view. NotFound when there is no such object.
UltraDbResult UltraDb_DescribeTable(const std::string& connection,
                                    const std::string& schema,
                                    const std::string& table,
                                    UltraDbTableSchema& out);

// A table, column or schema name quoted for this connection's engine, so it
// can be written into SQL text: `Order Items` becomes "Order Items", and a
// quote inside a name is doubled. InvalidArgument for an empty name or one
// containing a NUL byte, which no engine can quote - check the result rather
// than splicing an empty `out` into a statement.
UltraDbResult UltraDb_QuoteIdentifier(const std::string& connection,
                                      const std::string& name,
                                      std::string& out);

// schema.name, each part quoted; an empty schema gives just the quoted name.
UltraDbResult UltraDb_QuoteQualifiedName(const std::string& connection,
                                         const std::string& schema,
                                         const std::string& name,
                                         std::string& out);

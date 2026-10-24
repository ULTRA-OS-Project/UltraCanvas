// Apps/UltraDBAdmin/engine/DbAdminDump.h
// A whole schema - every table, its rows, its indexes, views and triggers - as
// one SQL file that rebuilds it (Docs/Research/UltraDBAdminDesignProposal.md,
// 5.2 Export: "table / database ... -> SQL dump", with structure only, data
// only and DROP IF EXISTS). Rows are read through a cursor a page at a time,
// so a database of millions of rows is never held.
//
//   SQLite      - as sqlite3's .dump writes it: PRAGMA foreign_keys=OFF,
//                 BEGIN TRANSACTION, each table's own CREATE TABLE and rows,
//                 sqlite_sequence's counters, then the indexes, triggers and
//                 views as SQLite keeps them, COMMIT.
//   PostgreSQL  - BEGIN; each table's CREATE TABLE (PostgresTableDdl: its
//                 columns, types, defaults, identity and generated columns,
//                 its primary key, unique and CHECK constraints) and rows,
//                 identity and serial counters moved past them, then the
//                 other indexes and the foreign keys (after every table
//                 exists, so the order of tables does not matter), comments,
//                 views; COMMIT. Triggers and the functions they call, types
//                 and extensions are pg_dump's to write, and the file says so.
//
// Read back by the SQL tab's Import SQL, or by sqlite3 / psql.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminStructureEdit.h"   // SqlEngine

#include <UltraDatabase/UltraDatabase.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace UltraDBAdmin {

struct DumpOptions {
    bool structure = true;         // CREATE statements
    bool data = true;              // INSERT statements
    bool dropFirst = false;        // DROP ... IF EXISTS before each CREATE
    size_t pageRows = 1000;        // rows per cursor page
};

// A PostgreSQL table's definition, from its catalog - what a dump writes and
// what an export of the table writes before its rows.
struct PostgresDdl {
    std::string create;                     // CREATE TABLE with columns and PRIMARY KEY / UNIQUE / CHECK
    std::vector<std::string> indexes;       // CREATE INDEX for the indexes no constraint owns
    std::vector<std::string> foreignKeys;   // ALTER TABLE ... ADD CONSTRAINT ... FOREIGN KEY
    std::vector<std::string> counters;      // SELECT setval(...): identity and serial counters past the rows
    std::vector<std::string> comments;      // COMMENT ON TABLE / COLUMN
    std::vector<std::string> notes;         // what the definition cannot carry, as comments for the file
    std::vector<std::string> insertColumns; // the columns rows are written to: all but the generated ones
    bool overridingSystemValue = false;     // an identity column is GENERATED ALWAYS: rows need OVERRIDING SYSTEM VALUE
};

UltraDbResult PostgresTableDdl(const std::string& connection, const UltraDbTableSchema& table, PostgresDdl& out);

// Writes `schema` of `connection` to `path` (beside it, moved there when
// complete). `cancel`, when set, stops it between pages, leaving no file.
// `progress` hears the table being written and the rows so far. Call it on a
// worker.
UltraDbResult DumpSchema(const std::string& connection, SqlEngine engine, const std::string& schema,
                         const std::string& path, const DumpOptions& options, size_t& tables, size_t& rows,
                         const std::atomic<bool>* cancel = nullptr,
                         const std::function<void(const std::string& table, size_t rows)>& progress = nullptr);

} // namespace UltraDBAdmin

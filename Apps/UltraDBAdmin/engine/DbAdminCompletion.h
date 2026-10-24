// Apps/UltraDBAdmin/engine/DbAdminCompletion.h
// Schema-aware completion for the SQL tab (Docs/Research/
// UltraDBAdminDesignProposal.md, stage 5): what the word at the caret may be,
// from the connection's schemas, tables and columns and where the caret is.
//   after FROM, JOIN, INTO, UPDATE, TABLE   the tables (and the schemas)
//   after SELECT, WHERE, ON, BY, SET, ...   the columns of the tables the
//                                           statement names, their aliases,
//                                           and the keywords
//   after "name."                           a schema's tables, or a table's -
//                                           or an alias's - columns
// A name is offered as it must be written: quoted where the engine would
// read it otherwise (PostgreSQL folds an unquoted name to lower case).
// Nothing is offered inside a string or a comment.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminEngine.h"

#include <UltraDatabase/UltraDatabase.h>

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace UltraDBAdmin {

// What completion knows of a connection: its schemas, each with its tables
// and views and their columns, and the schema an unqualified name is in.
struct CompletionCatalog {
    std::string defaultSchema;
    std::map<std::string, std::map<std::string, std::vector<std::string>>> schemas;   // schema -> table -> columns
};

// The catalog of `connection`: every schema but the system's, at most
// `maxColumns` columns in all (a server with thousands of tables still
// answers quickly). `defaultSchema` is where an unqualified name is looked
// for: "public", the MySQL database, "main". Call it on a worker.
UltraDbResult LoadCompletionCatalog(const std::string& connection, SqlEngine engine, const std::string& defaultSchema,
                                    CompletionCatalog& out, size_t maxColumns = 20000);

// The suggestions for the word that ends at byte `offset` of `script`, best
// first: those that begin as typed, then the names - not the keywords - that
// contain it, at most `limit`. Empty inside a string or a comment, and when
// nothing fits.
std::vector<std::string> CompletionsAt(SqlEngine engine, const std::string& script, size_t offset,
                                       const CompletionCatalog& catalog, size_t limit = 60);

// The word that ends at `offset` - what a suggestion replaces.
std::string CompletionPrefix(const std::string& script, size_t offset);

} // namespace UltraDBAdmin

// core/UltraDatabase/UltraDatabaseInternal.h
// Internal shared declarations for the UltraDatabase core: the connection
// manager (registry + handle tables) and the built-in SQLite driver
// accessor. Not a public header.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDatabase/UltraDatabaseCore.h"
#include "UltraDatabase/UltraDatabasePlugins.h"

namespace ultradb_internal {

// Returns the process-wide built-in SQLite driver singleton and guarantees it
// is registered exactly once. Called by the manager before resolving drivers.
IUltraDbDriverPlugin* BuiltinSqliteDriver();

// The PostgreSQL driver, when the build found libpq. Returns nullptr
// otherwise, so a caller can tell "not built" from "failed to connect".
IUltraDbDriverPlugin* BuiltinPostgresDriver();

// Rewrite UltraDatabase's `?` placeholders into PostgreSQL's `$1, $2, ...`,
// leaving a `?` inside a string literal, a quoted identifier, a dollar-quoted
// body or a comment untouched. Declared here so the suite can test it even in
// a build without libpq - see UltraDatabasePostgresSql.cpp.
std::string PostgresPlatzhalter(const std::string& sql);

// Expand a leading "~" in a filesystem path to $HOME (used by the SQLite
// driver for database paths).
std::string ExpandUserPath(const std::string& path);

} // namespace ultradb_internal

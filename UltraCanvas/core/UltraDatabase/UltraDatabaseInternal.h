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

// A number in a database's text form - a PostgreSQL float8 or numeric column,
// a Text value coerced with AsDouble - read with '.' as the decimal point
// whatever LC_NUMERIC says. strtod follows the locale, and the Linux backend
// calls setlocale(LC_ALL, "") for XIM, so in any GUI program on a
// comma-decimal desktop it read "3.14" as 3. Also takes PostgreSQL's spellings
// of the special values ("NaN", "Infinity", "-Infinity"). Leading whitespace
// and trailing text are ignored, as strtod ignores them. Returns false, and
// leaves `out` alone, when the text does not start with a number. Defined in
// UltraDatabaseValue.cpp, so it is compiled and tested without libpq.
bool ParseDoubleClassic(const std::string& text, double& out);

// The other direction: a double as a parameter on the wire, dot-decimal and
// with enough digits to read back as the same double. std::to_string, which
// the PostgreSQL driver used before, both follows the locale ("3,140000",
// which the server rejects) and keeps six decimals (1e-9 became "0.000000").
std::string FormatDoubleClassic(double value);

} // namespace ultradb_internal

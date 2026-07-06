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

// Expand a leading "~" in a filesystem path to $HOME (used by the SQLite
// driver for database paths).
std::string ExpandUserPath(const std::string& path);

} // namespace ultradb_internal

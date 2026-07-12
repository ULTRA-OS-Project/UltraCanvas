// include/UltraDatabase/UltraDatabase.h
// Umbrella include for the UltraDatabase module — the unified data-access
// layer for ULTRA OS. Pull this in to get the whole Stage 1 surface:
// named connections, parameterized queries, prepared statements,
// transactions and versioned migrations, backed by the bundled SQLite
// driver (with PostgreSQL / MySQL / ... as pluggable drivers).
//
//   #include <UltraDatabase/UltraDatabase.h>
//
//   UltraDbConnectionConfig cfg;
//   cfg.name = "app"; cfg.driver = "sqlite"; cfg.database = ":memory:";
//   UltraDb_RegisterConnection(cfg);
//   UltraDb_Exec("app", "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT)");
//   UltraDb_Exec("app", "INSERT INTO t(name) VALUES(?)", { "hello" });
//   UltraDbResultSet rs;
//   UltraDb_Query("app", "SELECT name FROM t WHERE id = ?", { 1 }, rs);
//
// See Docs/Modules/UltraDatabase/README.md for the full concept.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDatabaseCore.h"
#include "UltraDatabaseValue.h"
#include "UltraDatabaseConnection.h"
#include "UltraDatabaseQuery.h"
#include "UltraDatabaseTransaction.h"
#include "UltraDatabaseMigrate.h"
#include "UltraDatabasePlugins.h"

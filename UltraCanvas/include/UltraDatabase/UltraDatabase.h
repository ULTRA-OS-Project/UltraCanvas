// include/UltraDatabase/UltraDatabase.h
// Umbrella include for the UltraDatabase module — the unified data-access
// layer for ULTRA OS. Pull this in to get the whole Stage 1 surface:
// named connections, parameterized queries, prepared statements,
// transactions, versioned migrations, schema introspection and server
// administration, backed by the bundled SQLite driver, with PostgreSQL and
// MySQL / MariaDB drivers where the build found their client libraries.
//
// doc-check: example
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
// Version: 0.3.0 - UltraDatabaseServer.h
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDatabaseCore.h"
#include "UltraDatabaseValue.h"
#include "UltraDatabaseConnection.h"
#include "UltraDatabaseQuery.h"
#include "UltraDatabaseTransaction.h"
#include "UltraDatabaseMigrate.h"
#include "UltraDatabaseSchema.h"
#include "UltraDatabaseServer.h"
#include "UltraDatabasePlugins.h"

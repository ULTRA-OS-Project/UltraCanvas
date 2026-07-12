// include/UltraDatabase/UltraDatabaseMigrate.h
// Versioned schema migrations. An app declares an ordered list of steps and
// calls UltraDb_Migrate on launch; steps whose version exceeds the recorded
// schema version are applied, each inside a transaction, and the version is
// advanced. This lets an app bring its schema up to date regardless of engine.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDatabaseCore.h"

#include <string>
#include <vector>

// One migration step. `version` must be strictly increasing across the list
// and >= 1. `sql` may contain several statements separated by ';'.
struct UltraDbMigration {
    int         version = 0;
    std::string name;
    std::string sql;
};

// Apply every step whose version is greater than the connection's current
// schema version, in ascending order, each in its own transaction. On the
// first failing step, that step is rolled back and migration stops with an
// error (steps already applied remain committed). The applied version is
// tracked in an `ultradb_schema_version` table created on demand.
UltraDbResult UltraDb_Migrate(const std::string& connection,
                              const std::vector<UltraDbMigration>& steps);

// Current schema version (0 if no migration has ever run).
UltraDbResult UltraDb_GetSchemaVersion(const std::string& connection,
                                       int& outVersion);

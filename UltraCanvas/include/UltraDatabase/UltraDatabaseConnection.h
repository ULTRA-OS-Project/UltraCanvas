// include/UltraDatabase/UltraDatabaseConnection.h
// The connection registry: apps describe a database once, give it a name, and
// refer to it by that name everywhere else. Registration does not open a
// socket/file; the connection is opened lazily on first use (Stage 1 keeps a
// single physical connection per name — pooling arrives in Stage 2).
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDatabaseCore.h"

// Register (or replace) a named connection. Validates the config and that the
// requested driver exists; returns AlreadyExists behaviour by replacing an
// existing entry of the same name after closing it.
UltraDbResult UltraDb_RegisterConnection(const UltraDbConnectionConfig& config);

// True if a connection with this name is registered.
bool UltraDb_HasConnection(const std::string& name);

// Close and remove a named connection (drops its physical handle). Safe to
// call on an unknown name (returns ConnectionNotFound).
UltraDbResult UltraDb_CloseConnection(const std::string& name);

// Force the physical connection open now (otherwise it opens on first query).
// Useful in setup flows that want to surface connection errors immediately.
UltraDbResult UltraDb_OpenConnection(const std::string& name);

struct UltraDbConnectionInfo {
    std::string name;
    std::string driver;
    std::string database;
    bool        open = false;      // physical connection currently open
    bool        readOnly = false;
};

UltraDbResult UltraDb_GetConnectionInfo(const std::string& name,
                                        UltraDbConnectionInfo& out);

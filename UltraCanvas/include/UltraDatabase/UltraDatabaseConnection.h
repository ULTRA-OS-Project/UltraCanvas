// include/UltraDatabase/UltraDatabaseConnection.h
// The connection registry: apps describe a database once, give it a name, and
// refer to it by that name everywhere else. Registration does not open a
// socket/file; the connection is opened lazily on first use (Stage 1 keeps a
// single physical connection per name — pooling arrives in Stage 2).
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDatabaseCore.h"

#include <string>
#include <vector>

// Register (or replace) a named connection. Validates the config and that the
// requested driver exists. An existing entry of the same name is replaced:
// calls that start afterwards open the new connection, while a call already
// running on the old one - on any thread - and any open statement, cursor or
// transaction handle keep it until they end, and it closes after the last.
UltraDbResult UltraDb_RegisterConnection(const UltraDbConnectionConfig& config);

// True if a connection with this name is registered.
bool UltraDb_HasConnection(const std::string& name);

// The names of every registered connection, sorted. What a tool that shows
// "the databases this program knows about" lists.
std::vector<std::string> UltraDb_ListConnections();

// Close and remove a named connection (drops its physical handle). Safe to
// call on an unknown name (returns ConnectionNotFound). Safe while another
// thread's call runs on the connection: that call finishes, and the physical
// handle closes once it and every open statement, cursor and transaction
// handle on it have ended.
UltraDbResult UltraDb_CloseConnection(const std::string& name);

// Force the physical connection open now (otherwise it opens on first query).
// Useful in setup flows that want to surface connection errors immediately.
UltraDbResult UltraDb_OpenConnection(const std::string& name);

// The clause this connection's engine needs to lock a row being read for
// update ("" or " FOR UPDATE"). Callers that read a counter and write it back
// append this to the SELECT; see IUltraDbConnection::RowLockSuffix.
std::string UltraDb_RowLockSuffix(const std::string& connection);

struct UltraDbConnectionInfo {
    std::string name;
    std::string driver;
    std::string database;
    bool        open = false;      // physical connection currently open
    bool        readOnly = false;
};

UltraDbResult UltraDb_GetConnectionInfo(const std::string& name,
                                        UltraDbConnectionInfo& out);

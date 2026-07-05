// include/UltraDatabase/UltraDatabasePlugins.h
// Driver-plugin contract. A driver teaches UltraDatabase how to talk to one
// engine family. The built-in SQLite driver implements these same interfaces;
// PostgreSQL / MySQL / ... arrive as additional drivers (Stage 2+ ships them
// as loadable DSOs) without changing the core or any caller.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDatabaseCore.h"
#include "UltraDatabaseValue.h"

#include <memory>
#include <string>
#include <vector>

// A compiled statement bound to a physical connection. Execute() binds the
// parameters afresh, runs the statement, and appends any returned rows to
// `out` (cleared first). Reusable across many executions.
class IUltraDbStatement {
public:
    virtual ~IUltraDbStatement() = default;
    virtual UltraDbResult Execute(const UltraDbParams& params,
                                  UltraDbResultSet& out) = 0;
};

// A live physical connection to one database.
class IUltraDbConnection {
public:
    virtual ~IUltraDbConnection() = default;

    virtual std::string DriverName() const = 0;

    // Compile a single statement. On failure returns nullptr and sets `error`.
    virtual std::unique_ptr<IUltraDbStatement> Prepare(const std::string& sql,
                                                       UltraDbResult& error) = 0;

    // Prepare + execute one statement (or, when `params` is empty, a batch of
    // ';'-separated statements). Rows go to `out` (cleared first).
    virtual UltraDbResult ExecuteDirect(const std::string& sql,
                                        const UltraDbParams& params,
                                        UltraDbResultSet& out) = 0;
};

// A driver: a factory that opens connections for one or more driver ids.
class IUltraDbDriverPlugin {
public:
    virtual ~IUltraDbDriverPlugin() = default;

    virtual std::string GetName() const = 0;

    // Driver ids this plugin answers to, e.g. {"sqlite", "sqlite3"}.
    virtual std::vector<std::string> GetDriverIds() const = 0;

    // Open a new physical connection for the given config. On failure returns
    // nullptr and sets `error`.
    virtual std::unique_ptr<IUltraDbConnection> Open(
        const UltraDbConnectionConfig& config, UltraDbResult& error) = 0;
};

// Register a driver. Ownership stays with the caller (drivers are typically
// static singletons). Returns false if any of its ids is already registered.
bool UltraDatabase_RegisterDriver(IUltraDbDriverPlugin* driver);

// Look up a registered driver by id, or nullptr.
IUltraDbDriverPlugin* UltraDatabase_GetDriver(const std::string& driverId);

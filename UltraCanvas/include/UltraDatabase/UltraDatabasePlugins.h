// include/UltraDatabase/UltraDatabasePlugins.h
// Driver-plugin contract. A driver teaches UltraDatabase how to talk to one
// engine family. The built-in SQLite driver implements these same interfaces;
// PostgreSQL / MySQL / ... arrive as additional drivers (Stage 2+ ships them
// as loadable DSOs) without changing the core or any caller.
// Version: 0.4.0 - server administration (UltraDatabaseServer.h)
// Version: 0.3.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDatabaseCore.h"
#include "UltraDatabaseValue.h"
#include "UltraDatabaseSchema.h"

#include <cstdint>
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

// An open forward-only cursor over one query's rows (UltraDb_OpenCursor).
// Fetch appends up to `maxRows` rows to `out` (cleared first, columns always
// set) and sets `exhausted` once the last row has been read. Destroying the
// cursor releases it on the engine.
class IUltraDbCursor {
public:
    virtual ~IUltraDbCursor() = default;
    virtual UltraDbResult Fetch(size_t maxRows, UltraDbResultSet& out, bool& exhausted) = 0;
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

    // How this engine starts a write transaction.
    //
    // Genuinely driver-specific rather than a detail: SQLite needs
    // "BEGIN IMMEDIATE" to take the write lock at once and avoid a deadlock
    // when a reader tries to upgrade, while PostgreSQL has no such statement
    // and rejects it outright. The core used to hardcode SQLite's spelling,
    // which meant no other engine could ever start a transaction. The default
    // is the standard form, so a driver only overrides it when it needs to.
    virtual std::string BeginTransactionSql() const { return "BEGIN"; }

    // What to append to a SELECT whose row is about to be updated, so a
    // concurrent transaction cannot read the same value.
    //
    // This is the difference between a counter that allocates unique numbers
    // and one that hands the same number to two clients. SQLite needs nothing
    // because BEGIN IMMEDIATE already serialises writers; PostgreSQL at READ
    // COMMITTED will happily let both transactions read the old value, so it
    // needs an explicit row lock. Empty by default, because a driver that
    // serialises writers wants no clause at all - and because SQLite rejects
    // "FOR UPDATE" outright.
    virtual std::string RowLockSuffix() const { return std::string(); }

    // A name quoted for this engine's SQL text. The default is the SQL
    // standard - double quotes, a quote inside doubled - which SQLite and
    // PostgreSQL take; MySQL overrides it with backticks. The core has
    // already refused an empty name and a NUL byte before this is called.
    virtual std::string QuoteIdentifier(const std::string& name) const {
        std::string out;
        out.reserve(name.size() + 2);
        out.push_back('"');
        for (char c : name) {
            if (c == '"') out.push_back('"');
            out.push_back(c);
        }
        out.push_back('"');
        return out;
    }

    // Every statement of a script with its own outcome (UltraDb_ExecBatch),
    // stopping after the first that fails. The default runs the script as
    // ExecuteDirect's batch does and reports it as one statement, so a driver
    // that cannot split still answers.
    virtual UltraDbResult ExecuteBatch(const std::string& sql,
                                       std::vector<UltraDbStatementResult>& out) {
        out.clear();
        UltraDbStatementResult one;
        one.sql = sql;
        one.result = ExecuteDirect(sql, {}, one.rows);
        one.returnsRows = !one.rows.Columns().empty();
        UltraDbResult r = one.result;
        out.push_back(std::move(one));
        return r;
    }

    // Ask the engine to stop the statement now running on this connection,
    // from any thread (UltraDb_CancelQuery). Must not wait for the connection
    // to be free - the running statement holds it. The statement then fails
    // with Cancelled; with nothing running, nothing happens.
    virtual UltraDbResult Cancel() {
        return UltraDbResult::Error(UltraDbResultCode::Unsupported,
                                    "this driver cannot cancel a query", DriverName());
    }

    // A forward-only cursor over `sql`'s rows, read a page at a time
    // (UltraDb_OpenCursor), so a result too large to hold is never held.
    virtual std::unique_ptr<IUltraDbCursor> OpenCursor(const std::string& sql,
                                                       const UltraDbParams& params,
                                                       UltraDbResult& error) {
        (void)sql; (void)params;
        error = UltraDbResult::Error(UltraDbResultCode::Unsupported,
                                     "this driver has no cursors", DriverName());
        return nullptr;
    }

    // Introspection, behind UltraDb_ListSchemas / ListObjects / DescribeTable
    // (UltraDatabaseSchema.h). A driver that cannot answer keeps these
    // defaults and the caller is told Unsupported.
    virtual UltraDbResult ListSchemas(std::vector<std::string>& out) {
        out.clear();
        return UltraDbResult::Error(UltraDbResultCode::Unsupported,
                                    "this driver cannot list schemas", DriverName());
    }
    virtual UltraDbResult ListObjects(const std::string& schema,
                                      std::vector<UltraDbObjectSummary>& out) {
        (void)schema;
        out.clear();
        return UltraDbResult::Error(UltraDbResultCode::Unsupported,
                                    "this driver cannot list objects", DriverName());
    }
    virtual UltraDbResult DescribeTable(const std::string& schema, const std::string& table,
                                        UltraDbTableSchema& out) {
        (void)schema; (void)table;
        out = UltraDbTableSchema{};
        return UltraDbResult::Error(UltraDbResultCode::Unsupported,
                                    "this driver cannot describe tables", DriverName());
    }

    // Server administration, behind UltraDb_ServerSessions and the rest
    // (UltraDatabaseServer.h, which fixes the result columns). An embedded
    // engine has no server and keeps these defaults.
    virtual UltraDbResult ServerSessions(UltraDbResultSet& out) {
        out.Clear();
        return NoServer();
    }
    virtual UltraDbResult ServerSettings(const std::string& filter, UltraDbResultSet& out) {
        (void)filter;
        out.Clear();
        return NoServer();
    }
    virtual UltraDbResult ServerSizes(const std::string& schema, UltraDbResultSet& out) {
        (void)schema;
        out.Clear();
        return NoServer();
    }
    virtual UltraDbResult StopSessionSql(int64_t id, bool endSession, std::string& sql) {
        (void)id; (void)endSession;
        sql.clear();
        return NoServer();
    }
    virtual UltraDbResult StopSession(int64_t id, bool endSession, bool& stopped) {
        (void)id; (void)endSession;
        stopped = false;
        return NoServer();
    }

private:
    UltraDbResult NoServer() const {
        return UltraDbResult::Error(UltraDbResultCode::Unsupported,
                                    "this engine has no server to administer", DriverName());
    }
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

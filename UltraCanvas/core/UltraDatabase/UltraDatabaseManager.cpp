// core/UltraDatabase/UltraDatabaseManager.cpp
// The UltraDatabase manager: driver registry, named-connection registry with
// lazy opening, and the public UltraDb_* entrypoints for queries, prepared
// statements, transactions and migrations.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraDatabaseInternal.h"

#include "UltraDatabase/UltraDatabaseConnection.h"
#include "UltraDatabase/UltraDatabaseQuery.h"
#include "UltraDatabase/UltraDatabaseTransaction.h"
#include "UltraDatabase/UltraDatabaseMigrate.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>

namespace {

// ---- Driver registry -------------------------------------------------------

std::mutex& DriverMutex() { static std::mutex m; return m; }
std::map<std::string, IUltraDbDriverPlugin*>& DriverRegistry() {
    static std::map<std::string, IUltraDbDriverPlugin*> r;
    return r;
}

// Ensure the built-in SQLite driver has registered itself.
void EnsureBuiltins() { (void)ultradb_internal::BuiltinSqliteDriver(); }

// ---- Connection registry ---------------------------------------------------

struct ConnEntry {
    UltraDbConnectionConfig             config;
    std::unique_ptr<IUltraDbConnection> conn;   // opened lazily
    std::mutex                          openMtx; // guards lazy open of `conn`
};

std::mutex& RegistryMutex() { static std::mutex m; return m; }
std::map<std::string, std::shared_ptr<ConnEntry>>& Registry() {
    static std::map<std::string, std::shared_ptr<ConnEntry>> r;
    return r;
}

std::shared_ptr<ConnEntry> FindEntry(const std::string& name) {
    std::lock_guard<std::mutex> lk(RegistryMutex());
    auto it = Registry().find(name);
    return it == Registry().end() ? nullptr : it->second;
}

// Open (if needed) and return the physical connection for an entry.
IUltraDbConnection* OpenEntry(const std::shared_ptr<ConnEntry>& e, UltraDbResult& err) {
    std::lock_guard<std::mutex> lk(e->openMtx);
    if (e->conn) return e->conn.get();

    EnsureBuiltins();
    IUltraDbDriverPlugin* drv = UltraDatabase_GetDriver(e->config.driver);
    if (!drv) {
        err = UltraDbResult::Error(UltraDbResultCode::DriverNotFound,
                                   "no driver registered for '" + e->config.driver + "'");
        return nullptr;
    }
    e->conn = drv->Open(e->config, err);
    return e->conn.get();
}

// Resolve name -> open connection, mapping the two failure modes.
IUltraDbConnection* Resolve(const std::string& name, UltraDbResult& err) {
    auto e = FindEntry(name);
    if (!e) {
        err = UltraDbResult::Error(UltraDbResultCode::ConnectionNotFound,
                                   "no connection named '" + name + "'");
        return nullptr;
    }
    return OpenEntry(e, err);
}

// ---- Handle tables ---------------------------------------------------------

std::atomic<uint64_t> g_nextHandle{1};
std::mutex& HandleMutex() { static std::mutex m; return m; }

struct PreparedEntry {
    std::shared_ptr<ConnEntry>          keepAlive;
    std::unique_ptr<IUltraDbStatement>  stmt;
};
std::map<UltraDbHandle, PreparedEntry>& PreparedTable() {
    static std::map<UltraDbHandle, PreparedEntry> t;
    return t;
}

struct TxEntry {
    std::shared_ptr<ConnEntry> keepAlive;
    IUltraDbConnection*        conn = nullptr;
    bool                       finished = false;
};
std::map<UltraDbHandle, TxEntry>& TxTable() {
    static std::map<UltraDbHandle, TxEntry> t;
    return t;
}

} // namespace

// ============================================================================
// Driver registry (public, declared in UltraDatabasePlugins.h)
// ============================================================================

bool UltraDatabase_RegisterDriver(IUltraDbDriverPlugin* driver) {
    if (!driver) return false;
    std::lock_guard<std::mutex> lk(DriverMutex());
    auto ids = driver->GetDriverIds();
    for (const auto& id : ids)
        if (DriverRegistry().count(id)) return false;
    for (const auto& id : ids)
        DriverRegistry()[id] = driver;
    return true;
}

IUltraDbDriverPlugin* UltraDatabase_GetDriver(const std::string& driverId) {
    std::lock_guard<std::mutex> lk(DriverMutex());
    auto it = DriverRegistry().find(driverId);
    return it == DriverRegistry().end() ? nullptr : it->second;
}

// ============================================================================
// Core (public, declared in UltraDatabaseCore.h)
// ============================================================================

std::string UltraDatabase_GetVersion() { return "0.1.0"; }

std::vector<std::string> UltraDatabase_GetSupportedDrivers() {
    EnsureBuiltins();
    std::lock_guard<std::mutex> lk(DriverMutex());
    std::vector<std::string> ids;
    for (const auto& kv : DriverRegistry()) ids.push_back(kv.first);
    return ids;
}

// ============================================================================
// Connections (public, declared in UltraDatabaseConnection.h)
// ============================================================================

UltraDbResult UltraDb_RegisterConnection(const UltraDbConnectionConfig& config) {
    if (config.name.empty())
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    "connection name must not be empty");
    if (config.driver.empty())
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    "connection driver must not be empty");
    EnsureBuiltins();
    if (!UltraDatabase_GetDriver(config.driver))
        return UltraDbResult::Error(UltraDbResultCode::DriverNotFound,
                                    "no driver registered for '" + config.driver + "'");

    auto entry = std::make_shared<ConnEntry>();
    entry->config = config;

    std::lock_guard<std::mutex> lk(RegistryMutex());
    Registry()[config.name] = std::move(entry);  // replaces any prior entry
    return UltraDbResult::Ok();
}

bool UltraDb_HasConnection(const std::string& name) {
    return FindEntry(name) != nullptr;
}

UltraDbResult UltraDb_CloseConnection(const std::string& name) {
    std::lock_guard<std::mutex> lk(RegistryMutex());
    auto it = Registry().find(name);
    if (it == Registry().end())
        return UltraDbResult::Error(UltraDbResultCode::ConnectionNotFound,
                                    "no connection named '" + name + "'");
    Registry().erase(it);  // shared_ptr drop closes the physical connection
    return UltraDbResult::Ok();
}

UltraDbResult UltraDb_OpenConnection(const std::string& name) {
    UltraDbResult err;
    IUltraDbConnection* conn = Resolve(name, err);
    return conn ? UltraDbResult::Ok() : err;
}

UltraDbResult UltraDb_GetConnectionInfo(const std::string& name,
                                        UltraDbConnectionInfo& out) {
    auto e = FindEntry(name);
    if (!e)
        return UltraDbResult::Error(UltraDbResultCode::ConnectionNotFound,
                                    "no connection named '" + name + "'");
    out.name     = e->config.name;
    out.driver   = e->config.driver;
    out.database = e->config.database;
    out.readOnly = e->config.readOnly;
    {
        std::lock_guard<std::mutex> lk(e->openMtx);
        out.open = e->conn != nullptr;
    }
    return UltraDbResult::Ok();
}

// ============================================================================
// Queries (public, declared in UltraDatabaseQuery.h)
// ============================================================================

UltraDbResult UltraDb_Query(const std::string& connection, const std::string& sql,
                            const UltraDbParams& params, UltraDbResultSet& out) {
    UltraDbResult err;
    IUltraDbConnection* conn = Resolve(connection, err);
    if (!conn) return err;
    return conn->ExecuteDirect(sql, params, out);
}

UltraDbResult UltraDb_Query(const std::string& connection, const std::string& sql,
                            UltraDbResultSet& out) {
    return UltraDb_Query(connection, sql, UltraDbParams{}, out);
}

UltraDbResult UltraDb_Exec(const std::string& connection, const std::string& sql,
                           const UltraDbParams& params) {
    UltraDbResult err;
    IUltraDbConnection* conn = Resolve(connection, err);
    if (!conn) return err;
    UltraDbResultSet discard;
    return conn->ExecuteDirect(sql, params, discard);
}

UltraDbHandle UltraDb_Prepare(const std::string& connection, const std::string& sql,
                              UltraDbResult* error) {
    UltraDbResult err;
    auto e = FindEntry(connection);
    if (!e) {
        err = UltraDbResult::Error(UltraDbResultCode::ConnectionNotFound,
                                   "no connection named '" + connection + "'");
        if (error) *error = err;
        return UltraDbInvalidHandle;
    }
    IUltraDbConnection* conn = OpenEntry(e, err);
    if (!conn) { if (error) *error = err; return UltraDbInvalidHandle; }

    std::unique_ptr<IUltraDbStatement> stmt = conn->Prepare(sql, err);
    if (!stmt) { if (error) *error = err; return UltraDbInvalidHandle; }

    UltraDbHandle h = g_nextHandle.fetch_add(1);
    {
        std::lock_guard<std::mutex> lk(HandleMutex());
        PreparedTable()[h] = PreparedEntry{e, std::move(stmt)};
    }
    if (error) *error = UltraDbResult::Ok();
    return h;
}

static UltraDbResult ExecutePreparedImpl(UltraDbHandle statement,
                                         const UltraDbParams& params,
                                         UltraDbResultSet& out) {
    IUltraDbStatement* stmt = nullptr;
    {
        std::lock_guard<std::mutex> lk(HandleMutex());
        auto it = PreparedTable().find(statement);
        if (it == PreparedTable().end())
            return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                        "invalid prepared-statement handle");
        stmt = it->second.stmt.get();
    }
    return stmt->Execute(params, out);
}

UltraDbResult UltraDb_ExecPrepared(UltraDbHandle statement, const UltraDbParams& params) {
    UltraDbResultSet discard;
    return ExecutePreparedImpl(statement, params, discard);
}

UltraDbResult UltraDb_QueryPrepared(UltraDbHandle statement, const UltraDbParams& params,
                                    UltraDbResultSet& out) {
    return ExecutePreparedImpl(statement, params, out);
}

UltraDbResult UltraDb_Finalize(UltraDbHandle statement) {
    std::lock_guard<std::mutex> lk(HandleMutex());
    auto it = PreparedTable().find(statement);
    if (it == PreparedTable().end())
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    "invalid prepared-statement handle");
    PreparedTable().erase(it);
    return UltraDbResult::Ok();
}

// ============================================================================
// Transactions (public, declared in UltraDatabaseTransaction.h)
// ============================================================================

UltraDbHandle UltraDb_Begin(const std::string& connection, UltraDbResult* error) {
    UltraDbResult err;
    auto e = FindEntry(connection);
    if (!e) {
        err = UltraDbResult::Error(UltraDbResultCode::ConnectionNotFound,
                                   "no connection named '" + connection + "'");
        if (error) *error = err;
        return UltraDbInvalidHandle;
    }
    IUltraDbConnection* conn = OpenEntry(e, err);
    if (!conn) { if (error) *error = err; return UltraDbInvalidHandle; }

    UltraDbResultSet discard;
    UltraDbResult begin = conn->ExecuteDirect("BEGIN IMMEDIATE", {}, discard);
    if (!begin) { if (error) *error = begin; return UltraDbInvalidHandle; }

    UltraDbHandle h = g_nextHandle.fetch_add(1);
    {
        std::lock_guard<std::mutex> lk(HandleMutex());
        TxTable()[h] = TxEntry{e, conn, false};
    }
    if (error) *error = UltraDbResult::Ok();
    return h;
}

static IUltraDbConnection* TxConn(UltraDbHandle tx, UltraDbResult& err) {
    std::lock_guard<std::mutex> lk(HandleMutex());
    auto it = TxTable().find(tx);
    if (it == TxTable().end() || it->second.finished) {
        err = UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                   "invalid or finished transaction handle");
        return nullptr;
    }
    return it->second.conn;
}

UltraDbResult UltraDb_ExecInTx(UltraDbHandle transaction, const std::string& sql,
                               const UltraDbParams& params) {
    UltraDbResult err;
    IUltraDbConnection* conn = TxConn(transaction, err);
    if (!conn) return err;
    UltraDbResultSet discard;
    return conn->ExecuteDirect(sql, params, discard);
}

UltraDbResult UltraDb_QueryInTx(UltraDbHandle transaction, const std::string& sql,
                                const UltraDbParams& params, UltraDbResultSet& out) {
    UltraDbResult err;
    IUltraDbConnection* conn = TxConn(transaction, err);
    if (!conn) return err;
    return conn->ExecuteDirect(sql, params, out);
}

static UltraDbResult FinishTx(UltraDbHandle transaction, const char* verb) {
    IUltraDbConnection* conn = nullptr;
    {
        std::lock_guard<std::mutex> lk(HandleMutex());
        auto it = TxTable().find(transaction);
        if (it == TxTable().end() || it->second.finished)
            return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                        "invalid or finished transaction handle");
        conn = it->second.conn;
        it->second.finished = true;
    }
    UltraDbResultSet discard;
    UltraDbResult r = conn->ExecuteDirect(verb, {}, discard);
    {
        std::lock_guard<std::mutex> lk(HandleMutex());
        TxTable().erase(transaction);
    }
    return r;
}

UltraDbResult UltraDb_Commit(UltraDbHandle transaction) {
    return FinishTx(transaction, "COMMIT");
}

UltraDbResult UltraDb_Rollback(UltraDbHandle transaction) {
    return FinishTx(transaction, "ROLLBACK");
}

// ============================================================================
// Migrations (public, declared in UltraDatabaseMigrate.h)
// ============================================================================

namespace {

UltraDbResult EnsureVersionTable(const std::string& connection) {
    return UltraDb_Exec(connection,
        "CREATE TABLE IF NOT EXISTS ultradb_schema_version("
        "version INTEGER NOT NULL, name TEXT, applied_at TEXT)");
}

} // namespace

UltraDbResult UltraDb_GetSchemaVersion(const std::string& connection, int& outVersion) {
    outVersion = 0;
    UltraDbResult ensure = EnsureVersionTable(connection);
    if (!ensure) return ensure;

    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection,
        "SELECT COALESCE(MAX(version), 0) AS v FROM ultradb_schema_version", rs);
    if (!q) return q;
    if (!rs.Empty())
        outVersion = rs.Row(0)[static_cast<size_t>(0)].AsInt();
    return UltraDbResult::Ok();
}

UltraDbResult UltraDb_Migrate(const std::string& connection,
                              const std::vector<UltraDbMigration>& steps) {
    int current = 0;
    UltraDbResult v = UltraDb_GetSchemaVersion(connection, current);
    if (!v) return v;

    // Apply in ascending version order.
    std::vector<UltraDbMigration> ordered = steps;
    std::sort(ordered.begin(), ordered.end(),
              [](const UltraDbMigration& a, const UltraDbMigration& b) {
                  return a.version < b.version;
              });

    for (const auto& step : ordered) {
        if (step.version <= current) continue;
        if (step.version <= 0)
            return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                        "migration version must be >= 1");

        UltraDbResult beginErr;
        UltraDbHandle tx = UltraDb_Begin(connection, &beginErr);
        if (tx == UltraDbInvalidHandle) return beginErr;

        UltraDbResult stepRes = UltraDb_ExecInTx(tx, step.sql);
        if (!stepRes) {
            UltraDb_Rollback(tx);
            stepRes.message = "migration to version " + std::to_string(step.version) +
                              " ('" + step.name + "') failed: " + stepRes.message;
            return stepRes;
        }

        UltraDbResult rec = UltraDb_ExecInTx(tx,
            "INSERT INTO ultradb_schema_version(version, name, applied_at) "
            "VALUES(?, ?, datetime('now'))",
            { step.version, step.name });
        if (!rec) { UltraDb_Rollback(tx); return rec; }

        UltraDbResult commit = UltraDb_Commit(tx);
        if (!commit) return commit;

        current = step.version;
    }
    return UltraDbResult::Ok();
}

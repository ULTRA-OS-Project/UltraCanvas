// core/UltraDatabase/UltraDatabaseManager.cpp
// The UltraDatabase manager: driver registry, named-connection registry with
// lazy opening, and the public UltraDb_* entrypoints for queries, prepared
// statements, transactions and migrations.
// Version: 0.6.0 - SSH tunnels (UltraDatabaseTunnel.h): opened with a connection, closed with it;
//                  the server's status for a monitor (UltraDb_ServerStatus)
// Version: 0.5.0 - the MySQL / MariaDB driver, server administration (UltraDatabaseServer.h)
// Version: 0.4.0 - statement observers (UltraDb_AddStatementObserver)
// Version: 0.3.3 - Retired() is never destroyed: a close from an atexit handler wrote into it after its teardown
// Version: 0.3.2 - the SQLite driver reports a statement's own changes only
// Version: 0.3.1
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraDatabaseInternal.h"

#include "UltraDatabase/UltraDatabaseConnection.h"
#include "UltraDatabase/UltraDatabaseQuery.h"
#include "UltraDatabase/UltraDatabaseTransaction.h"
#include "UltraDatabase/UltraDatabaseMigrate.h"
#include "UltraDatabase/UltraDatabaseSchema.h"
#include "UltraDatabase/UltraDatabaseTunnel.h"

#if defined(ULTRADATABASE_HAS_VAULT)
  #include "UltraVault/UltraVault.h"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
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

// Ensure the built-in drivers have registered themselves.
//
// The call is what keeps each driver's translation unit in the link: with a
// static library the linker drops an object nothing references, and a driver
// that registers itself from a static initializer would then silently not
// exist. That failure looks exactly like "no driver registered for
// 'postgresql'" at run time, which is a long way from the cause.
void EnsureBuiltins() {
    (void)ultradb_internal::BuiltinSqliteDriver();
    (void)ultradb_internal::BuiltinPostgresDriver();
    (void)ultradb_internal::BuiltinMysqlDriver();
    (void)ultradb_internal::BuiltinRelayDriver();
}

// ---- Connection registry ---------------------------------------------------

struct ConnEntry {
    UltraDbConnectionConfig             config;
    // Declared before `conn`, so destroyed after it: the driver closes its
    // sessions while the tunnel they run through is still there.
    std::unique_ptr<IUltraDbTunnel>     tunnel;  // an SSH tunnel's, when the config asks for one
    std::unique_ptr<IUltraDbConnection> conn;   // opened lazily
    std::mutex                          openMtx; // guards lazy open of `conn`
};

std::mutex& RegistryMutex() { static std::mutex m; return m; }
std::map<std::string, std::shared_ptr<ConnEntry>>& Registry() {
    static std::map<std::string, std::shared_ptr<ConnEntry>> r;
    return r;
}

// Entries a name had before it was re-registered or closed, while calls that
// started on them still keep them alive (see Session). Weak: a retired
// connection closes with its last call, as before. Kept so a cancel by name
// (UltraDb_CancelQuery) still reaches a statement that began under the name
// before it changed hands. Guarded by RegistryMutex.
//
// Never destroyed, like the handle tables below: it is first built by the
// first close, and UltraMessage's atexit handler - registered before that -
// closes its journal after the static would already be gone. Destroyed, the
// close then wrote into freed memory, and the process aborted in free() as
// it exited (ChatCube, on every quit after its store had closed).
std::map<std::string, std::vector<std::weak_ptr<ConnEntry>>>& Retired() {
    static auto* r = new std::map<std::string, std::vector<std::weak_ptr<ConnEntry>>>;
    return *r;
}

// Under RegistryMutex: remember `entry` as retired under `name`, forgetting
// those no call keeps any more.
void Retire(const std::string& name, const std::shared_ptr<ConnEntry>& entry) {
    auto& list = Retired()[name];
    list.erase(std::remove_if(list.begin(), list.end(),
                              [](const std::weak_ptr<ConnEntry>& w) { return w.expired(); }),
               list.end());
    list.push_back(entry);
}

std::shared_ptr<ConnEntry> FindEntry(const std::string& name) {
    std::lock_guard<std::mutex> lk(RegistryMutex());
    auto it = Registry().find(name);
    return it == Registry().end() ? nullptr : it->second;
}

// ---- SSH tunnels (UltraDatabaseTunnel.h) -----------------------------------

std::mutex& TunnelMutex() { static std::mutex m; return m; }
UltraDbTunnelOpener& TunnelOpener() {
    static UltraDbTunnelOpener opener;
    return opener;
}

std::string Option(const UltraDbConnectionConfig& config, const char* key) {
    auto it = config.options.find(key);
    return it == config.options.end() ? std::string() : it->second;
}

void Wipe(std::string& s) {
    volatile char* p = s.empty() ? nullptr : &s[0];
    for (size_t i = 0; i < s.size(); ++i) p[i] = 0;
    s.clear();
}

void Wipe(std::vector<uint8_t>& bytes) {
    volatile uint8_t* p = bytes.data();
    for (size_t i = 0; i < bytes.size(); ++i) p[i] = 0;
    bytes.clear();
}

// The secret an option names ("vault:<key>"), read from UltraVault; nothing
// for an empty option. `what` names it in an error.
bool ReadSecret(const std::string& reference, const char* what, std::vector<uint8_t>& out, UltraDbResult& err) {
    out.clear();
    if (reference.empty()) return true;
    std::string key = reference;
    if (key.rfind("vault:", 0) == 0) key = key.substr(6);
#if defined(ULTRADATABASE_HAS_VAULT)
    if (!UltraVault::IsAvailable()) {
        err = UltraDbResult::Error(UltraDbResultCode::CredentialsUnavailable,
                                   std::string("the ") + what + " cannot be read: UltraVault is not open");
        return false;
    }
    UltraVault::SecretValue secret;
    const UltraVault::Result read = UltraVault::Get(key, secret);
    if (!read) {
        err = UltraDbResult::Error(UltraDbResultCode::CredentialsUnavailable,
                                   std::string("the ") + what + " '" + key + "' " +
                                       (read.code == UltraVault::ResultCode::NotFound ? "is not in UltraVault"
                                                                                      : "could not be read: " + read.message));
        return false;
    }
    out.assign(secret.bytes.begin(), secret.bytes.end());
    Wipe(secret.bytes);
    return true;
#else
    err = UltraDbResult::Error(UltraDbResultCode::CredentialsUnavailable,
                               std::string("the ") + what + " is in UltraVault, which is not built in");
    return false;
#endif
}

bool LooksLikePrivateKey(const std::vector<uint8_t>& secret) {
    static const std::string begin = "-----BEGIN ";
    return secret.size() > begin.size() && std::equal(begin.begin(), begin.end(), secret.begin());
}

// Open `e`'s tunnel and fill `driverConfig` - `e->config` without its ssh_*
// options, pointed at the tunnel by the driver.
bool OpenTunnel(ConnEntry& e, IUltraDbDriverPlugin& drv, UltraDbConnectionConfig& driverConfig, UltraDbResult& err) {
    const UltraDbConnectionConfig& config = e.config;
    UltraDbTunnelOpener opener;
    {
        std::lock_guard<std::mutex> lk(TunnelMutex());
        opener = TunnelOpener();
    }
    if (!opener) {
        err = UltraDbResult::Error(UltraDbResultCode::Unsupported,
                                   "the connection asks for an SSH tunnel and none can be opened here "
                                   "(no UltraDb_SetTunnelOpener)");
        return false;
    }
    UltraDbTunnelRequest request;
    request.targetPort = config.port > 0 ? config.port : drv.DefaultPort();
    if (request.targetPort <= 0) {
        err = UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                   "a " + config.driver + " database has no server to reach through an SSH tunnel");
        return false;
    }
    if (!config.host.empty() && config.host[0] == '/') {
        err = UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                   "an SSH tunnel reaches a host and port, not the socket " + config.host);
        return false;
    }
    request.sshHost = Option(config, "ssh_host");
    const std::string sshPort = Option(config, "ssh_port");
    request.sshPort = sshPort.empty() ? 22 : std::atoi(sshPort.c_str());
    if (request.sshPort <= 0 || request.sshPort > 65535) {
        err = UltraDbResult::Error(UltraDbResultCode::InvalidArgument, "ssh_port is not a port: " + sshPort);
        return false;
    }
    request.sshUser = Option(config, "ssh_user");
    request.hostKeyFingerprint = Option(config, "ssh_host_key");
    // "" is the bastion itself, as the bastion reaches it.
    request.targetHost = config.host.empty() ? "127.0.0.1" : config.host;

    std::vector<uint8_t> secret, passphrase;
    if (!ReadSecret(Option(config, "ssh_credentials"), "SSH password or key", secret, err) ||
        !ReadSecret(Option(config, "ssh_key_passphrase"), "SSH key's passphrase", passphrase, err)) {
        Wipe(secret);
        return false;
    }
    if (LooksLikePrivateKey(secret)) request.privateKey = std::move(secret);
    else request.password.assign(secret.begin(), secret.end());
    request.keyPassphrase.assign(passphrase.begin(), passphrase.end());
    Wipe(secret);
    Wipe(passphrase);

    err = UltraDbResult::Ok();
    e.tunnel = opener(request, err);
    Wipe(request.password);
    Wipe(request.privateKey);
    Wipe(request.keyPassphrase);
    if (!e.tunnel) {
        if (err.success)
            err = UltraDbResult::Error(UltraDbResultCode::ConnectionFailed, "the SSH tunnel did not open");
        return false;
    }

    driverConfig = config;
    for (auto it = driverConfig.options.begin(); it != driverConfig.options.end();)
        it = it->first.rfind("ssh_", 0) == 0 ? driverConfig.options.erase(it) : std::next(it);
    err = drv.ThroughTunnel(driverConfig, e.tunnel->LocalPort());
    if (!err) {
        e.tunnel.reset();
        return false;
    }
    return true;
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
    if (!UltraDb_UsesTunnel(e->config)) {
        e->conn = drv->Open(e->config, err);
        return e->conn.get();
    }
    UltraDbConnectionConfig driverConfig;
    if (!OpenTunnel(*e, *drv, driverConfig, err)) return nullptr;
    e->conn = drv->Open(driverConfig, err);
    if (!e->conn) {
        e->tunnel.reset();
        if (err.message.find("SSH tunnel") == std::string::npos)
            err.message += " (through the SSH tunnel via " + Option(e->config, "ssh_host") + ")";
    }
    return e->conn.get();
}

// A connection held for the length of one call. `entry` keeps the registry
// entry - and the connection it owns - alive until the call returns, even
// when another thread re-registers or closes the name meanwhile: those drop
// the registry's reference, never a caller's. The name then means the new
// connection for every call that starts afterwards.
struct Session {
    std::shared_ptr<ConnEntry> entry;
    IUltraDbConnection*        conn = nullptr;

    explicit operator bool() const { return conn != nullptr; }
    IUltraDbConnection* operator->() const { return conn; }
};

// Resolve name -> open connection, mapping the two failure modes.
Session Resolve(const std::string& name, UltraDbResult& err) {
    Session session;
    session.entry = FindEntry(name);
    if (!session.entry) {
        err = UltraDbResult::Error(UltraDbResultCode::ConnectionNotFound,
                                   "no connection named '" + name + "'");
        return session;
    }
    session.conn = OpenEntry(session.entry, err);
    return session;
}

// ---- Handle tables ---------------------------------------------------------
// Never destroyed: UltraMessage broker sessions (detached threads, stopped
// by an atexit handler registered before these are first built) can still
// be journaling through them while statics are torn down.

std::atomic<uint64_t> g_nextHandle{1};
std::mutex& HandleMutex() { static std::mutex* m = new std::mutex; return *m; }

// A handle's object is shared with the calls using it, so closing the handle
// on one thread (Finalize, CloseCursor) while a call runs on another frees it
// when that call returns, not under it. keepAlive is declared first so it is
// destroyed last: a statement or cursor goes before its connection.
struct PreparedEntry {
    std::shared_ptr<ConnEntry>          keepAlive;
    std::shared_ptr<IUltraDbStatement>  stmt;
    std::string                         sql;      // for the observers
};
std::map<UltraDbHandle, PreparedEntry>& PreparedTable() {
    static auto* t = new std::map<UltraDbHandle, PreparedEntry>;
    return *t;
}

struct CursorEntry {
    std::shared_ptr<ConnEntry>       keepAlive;
    std::shared_ptr<IUltraDbCursor>  cursor;
};
std::map<UltraDbHandle, CursorEntry>& CursorTable() {
    static auto* t = new std::map<UltraDbHandle, CursorEntry>;
    return *t;
}

struct TxEntry {
    std::shared_ptr<ConnEntry> keepAlive;
    IUltraDbConnection*        conn = nullptr;
    bool                       finished = false;
};
std::map<UltraDbHandle, TxEntry>& TxTable() {
    static auto* t = new std::map<UltraDbHandle, TxEntry>;
    return *t;
}

// ---- Statement observers ---------------------------------------------------
// Never destroyed, for the reason the handle tables are not. A report copies
// the list under the lock and calls it outside, so an observer may run
// statements of its own, and may add or remove observers.

std::mutex& ObserverMutex() { static std::mutex* m = new std::mutex; return *m; }
std::map<int, std::shared_ptr<const UltraDbStatementObserver>>& Observers() {
    static auto* o = new std::map<int, std::shared_ptr<const UltraDbStatementObserver>>;
    return *o;
}
std::atomic<int> g_observers{0};   // how many there are: none, and nothing is timed
int g_nextObserver = 0;            // under ObserverMutex

using Kind = UltraDbStatementEvent::Kind;

// Times one call from its construction, and reports it - when anyone was
// watching as it began.
class Watch {
public:
    Watch() : on_(g_observers.load(std::memory_order_relaxed) > 0) {
        if (on_) start_ = std::chrono::steady_clock::now();
    }
    explicit operator bool() const { return on_; }

    // `untimed`: the statement did not run as a call of its own (one of a
    // script's), so it has no time apart; it reports -1.
    void Report(Kind kind, const std::string& connection, const std::string& sql, size_t bound, size_t rows,
                bool inTransaction, const UltraDbResult& result, bool untimed = false) const {
        if (!on_) return;
        UltraDbStatementEvent event;
        event.kind = kind;
        event.connection = connection;
        event.sql = sql;
        event.boundValues = bound;
        event.rows = rows;
        event.inTransaction = inTransaction;
        event.seconds = untimed ? -1.0
                                : std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
        event.result = result;
        std::vector<std::shared_ptr<const UltraDbStatementObserver>> observers;
        {
            std::lock_guard<std::mutex> lk(ObserverMutex());
            for (const auto& kv : Observers()) observers.push_back(kv.second);
        }
        // An observer that throws must not turn a statement that ran into
        // one that failed.
        for (const auto& observer : observers) {
            try { (*observer)(event); } catch (...) {}
        }
    }

private:
    bool on_ = false;
    std::chrono::steady_clock::time_point start_;
};

} // namespace

int UltraDb_AddStatementObserver(UltraDbStatementObserver observer) {
    if (!observer) return 0;
    std::lock_guard<std::mutex> lk(ObserverMutex());
    const int id = ++g_nextObserver;
    Observers()[id] = std::make_shared<const UltraDbStatementObserver>(std::move(observer));
    g_observers.store(static_cast<int>(Observers().size()));
    return id;
}

void UltraDb_RemoveStatementObserver(int id) {
    std::lock_guard<std::mutex> lk(ObserverMutex());
    Observers().erase(id);
    g_observers.store(static_cast<int>(Observers().size()));
}

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

std::string UltraDatabase_GetVersion() { return "0.5.0"; }

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

void UltraDb_SetTunnelOpener(UltraDbTunnelOpener opener) {
    std::lock_guard<std::mutex> lk(TunnelMutex());
    TunnelOpener() = std::move(opener);
}

bool UltraDb_UsesTunnel(const UltraDbConnectionConfig& config) {
    return !Option(config, "ssh_host").empty();
}

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

    std::shared_ptr<ConnEntry> replaced;   // closed after the lock, as UltraDb_CloseConnection does
    std::lock_guard<std::mutex> lk(RegistryMutex());
    auto& slot = Registry()[config.name];
    if (slot) Retire(config.name, slot);
    replaced = std::move(slot);
    slot = std::move(entry);  // replaces any prior entry
    return UltraDbResult::Ok();
}

bool UltraDb_HasConnection(const std::string& name) {
    return FindEntry(name) != nullptr;
}

std::vector<std::string> UltraDb_ListConnections() {
    std::lock_guard<std::mutex> lk(RegistryMutex());
    std::vector<std::string> names;
    names.reserve(Registry().size());
    for (const auto& kv : Registry()) names.push_back(kv.first);   // std::map: already sorted
    return names;
}

UltraDbResult UltraDb_CloseConnection(const std::string& name) {
    // Taken out under the lock, closed after it: closing can take a round
    // trip (a server's goodbye, a relay's), which must not hold up every
    // other connection - nor call back into the registry, as a relay served
    // in the same process does.
    std::shared_ptr<ConnEntry> closing;
    {
        std::lock_guard<std::mutex> lk(RegistryMutex());
        auto it = Registry().find(name);
        if (it == Registry().end())
            return UltraDbResult::Error(UltraDbResultCode::ConnectionNotFound,
                                        "no connection named '" + name + "'");
        Retire(name, it->second);
        closing = std::move(it->second);
        Registry().erase(it);
    }
    closing.reset();  // the last call on it closes the physical connection
    return UltraDbResult::Ok();
}

UltraDbResult UltraDb_OpenConnection(const std::string& name) {
    UltraDbResult err;
    Session conn = Resolve(name, err);
    return conn ? UltraDbResult::Ok() : err;
}

std::string UltraDb_RowLockSuffix(const std::string& connection) {
    UltraDbResult err;
    Session conn = Resolve(connection, err);
    return conn ? conn->RowLockSuffix() : std::string();
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
    const Watch watch;
    UltraDbResult err;
    Session conn = Resolve(connection, err);
    if (!conn) {
        watch.Report(Kind::Query, connection, sql, params.size(), 0, false, err);
        return err;
    }
    UltraDbResult result = conn->ExecuteDirect(sql, params, out);
    watch.Report(Kind::Query, connection, sql, params.size(), out.Size(), false, result);
    return result;
}

UltraDbResult UltraDb_Query(const std::string& connection, const std::string& sql,
                            UltraDbResultSet& out) {
    return UltraDb_Query(connection, sql, UltraDbParams{}, out);
}

UltraDbResult UltraDb_Exec(const std::string& connection, const std::string& sql,
                           const UltraDbParams& params) {
    const Watch watch;
    UltraDbResult err;
    Session conn = Resolve(connection, err);
    if (!conn) {
        watch.Report(Kind::Exec, connection, sql, params.size(), 0, false, err);
        return err;
    }
    UltraDbResultSet discard;
    UltraDbResult result = conn->ExecuteDirect(sql, params, discard);
    watch.Report(Kind::Exec, connection, sql, params.size(), 0, false, result);
    return result;
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
        PreparedTable()[h] = PreparedEntry{e, std::shared_ptr<IUltraDbStatement>(std::move(stmt)), sql};
    }
    if (error) *error = UltraDbResult::Ok();
    return h;
}

static UltraDbResult ExecutePreparedImpl(UltraDbHandle statement,
                                         const UltraDbParams& params,
                                         UltraDbResultSet& out, Kind kind) {
    const Watch watch;
    PreparedEntry held;   // this call's share: see PreparedEntry
    {
        std::lock_guard<std::mutex> lk(HandleMutex());
        auto it = PreparedTable().find(statement);
        if (it == PreparedTable().end())
            return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                        "invalid prepared-statement handle");
        held = it->second;
    }
    UltraDbResult result = held.stmt->Execute(params, out);
    if (watch)
        watch.Report(kind, held.keepAlive->config.name, held.sql, params.size(),
                     kind == Kind::Query ? out.Size() : 0, false, result);
    return result;
}

UltraDbResult UltraDb_ExecPrepared(UltraDbHandle statement, const UltraDbParams& params) {
    UltraDbResultSet discard;
    return ExecutePreparedImpl(statement, params, discard, Kind::Exec);
}

UltraDbResult UltraDb_QueryPrepared(UltraDbHandle statement, const UltraDbParams& params,
                                    UltraDbResultSet& out) {
    return ExecutePreparedImpl(statement, params, out, Kind::Query);
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
    // Ask the driver how its engine starts a transaction; see
    // IUltraDbConnection::BeginTransactionSql.
    const Watch watch;
    const std::string beginSql = conn->BeginTransactionSql();
    UltraDbResult begin = conn->ExecuteDirect(beginSql, {}, discard);
    watch.Report(Kind::Transaction, connection, beginSql, 0, 0, true, begin);
    if (!begin) { if (error) *error = begin; return UltraDbInvalidHandle; }

    UltraDbHandle h = g_nextHandle.fetch_add(1);
    {
        std::lock_guard<std::mutex> lk(HandleMutex());
        TxTable()[h] = TxEntry{e, conn, false};
    }
    if (error) *error = UltraDbResult::Ok();
    return h;
}

// The transaction's connection for one call, held like Resolve's: the
// transaction's own reference can go (Commit on another thread) while the
// call runs.
static Session TxConn(UltraDbHandle tx, UltraDbResult& err) {
    std::lock_guard<std::mutex> lk(HandleMutex());
    auto it = TxTable().find(tx);
    if (it == TxTable().end() || it->second.finished) {
        err = UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                   "invalid or finished transaction handle");
        return Session{};
    }
    return Session{it->second.keepAlive, it->second.conn};
}

UltraDbResult UltraDb_ExecInTx(UltraDbHandle transaction, const std::string& sql,
                               const UltraDbParams& params) {
    const Watch watch;
    UltraDbResult err;
    Session conn = TxConn(transaction, err);
    if (!conn) return err;
    UltraDbResultSet discard;
    UltraDbResult result = conn->ExecuteDirect(sql, params, discard);
    if (watch) watch.Report(Kind::Exec, conn.entry->config.name, sql, params.size(), 0, true, result);
    return result;
}

UltraDbResult UltraDb_QueryInTx(UltraDbHandle transaction, const std::string& sql,
                                const UltraDbParams& params, UltraDbResultSet& out) {
    const Watch watch;
    UltraDbResult err;
    Session conn = TxConn(transaction, err);
    if (!conn) return err;
    UltraDbResult result = conn->ExecuteDirect(sql, params, out);
    if (watch) watch.Report(Kind::Query, conn.entry->config.name, sql, params.size(), out.Size(), true, result);
    return result;
}

static UltraDbResult FinishTx(UltraDbHandle transaction, const char* verb) {
    Session conn;
    {
        std::lock_guard<std::mutex> lk(HandleMutex());
        auto it = TxTable().find(transaction);
        if (it == TxTable().end() || it->second.finished)
            return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                        "invalid or finished transaction handle");
        conn = Session{it->second.keepAlive, it->second.conn};
        it->second.finished = true;
    }
    const Watch watch;
    UltraDbResultSet discard;
    UltraDbResult r = conn->ExecuteDirect(verb, {}, discard);
    {
        std::lock_guard<std::mutex> lk(HandleMutex());
        TxTable().erase(transaction);
    }
    if (watch) watch.Report(Kind::Transaction, conn.entry->config.name, verb, 0, 0, true, r);
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

        // CURRENT_TIMESTAMP rather than SQLite's datetime('now'): the latter
        // is not a function PostgreSQL has, so the migration bookkeeping -
        // not the migration itself - was what stopped any other engine from
        // ever being usable. This is standard SQL and both engines take it.
        UltraDbResult rec = UltraDb_ExecInTx(tx,
            "INSERT INTO ultradb_schema_version(version, name, applied_at) "
            "VALUES(?, ?, CURRENT_TIMESTAMP)",
            { step.version, step.name });
        if (!rec) { UltraDb_Rollback(tx); return rec; }

        UltraDbResult commit = UltraDb_Commit(tx);
        if (!commit) return commit;

        current = step.version;
    }
    return UltraDbResult::Ok();
}

// ============================================================================
// Schema introspection and name quoting (public, declared in
// UltraDatabaseSchema.h)
// ============================================================================

UltraDbResult UltraDb_ListSchemas(const std::string& connection,
                                  std::vector<std::string>& out) {
    out.clear();
    const Watch watch;
    UltraDbResult err;
    Session conn = Resolve(connection, err);
    UltraDbResult result = conn ? conn->ListSchemas(out) : err;
    watch.Report(Kind::Introspection, connection, "-- list the schemas", 0, out.size(), false, result);
    return result;
}

UltraDbResult UltraDb_ListObjects(const std::string& connection, const std::string& schema,
                                  std::vector<UltraDbObjectSummary>& out) {
    out.clear();
    const Watch watch;
    UltraDbResult err;
    Session conn = Resolve(connection, err);
    UltraDbResult result = conn ? conn->ListObjects(schema, out) : err;
    watch.Report(Kind::Introspection, connection, "-- list the objects of " + schema, 0, out.size(), false, result);
    return result;
}

UltraDbResult UltraDb_DescribeTable(const std::string& connection, const std::string& schema,
                                    const std::string& table, UltraDbTableSchema& out) {
    out = UltraDbTableSchema{};
    if (table.empty())
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    "table name must not be empty");
    const Watch watch;
    UltraDbResult err;
    Session conn = Resolve(connection, err);
    UltraDbResult result = conn ? conn->DescribeTable(schema, table, out) : err;
    watch.Report(Kind::Introspection, connection, "-- describe " + (schema.empty() ? table : schema + "." + table), 0,
                 0, false, result);
    return result;
}

// ===== SERVER ADMINISTRATION =====

UltraDbResult UltraDb_ServerSessions(const std::string& connection, UltraDbResultSet& out) {
    out.Clear();
    const Watch watch;
    UltraDbResult err;
    Session conn = Resolve(connection, err);
    UltraDbResult result = conn ? conn->ServerSessions(out) : err;
    watch.Report(Kind::Introspection, connection, "-- the server's sessions", 0, out.Size(), false, result);
    return result;
}

UltraDbResult UltraDb_ServerSettings(const std::string& connection, const std::string& filter,
                                     UltraDbResultSet& out) {
    out.Clear();
    const Watch watch;
    UltraDbResult err;
    Session conn = Resolve(connection, err);
    UltraDbResult result = conn ? conn->ServerSettings(filter, out) : err;
    watch.Report(Kind::Introspection, connection, "-- the server's settings", filter.empty() ? 0 : 1, out.Size(),
                 false, result);
    return result;
}

UltraDbResult UltraDb_ServerSizes(const std::string& connection, const std::string& schema,
                                  UltraDbResultSet& out) {
    out.Clear();
    const Watch watch;
    UltraDbResult err;
    Session conn = Resolve(connection, err);
    UltraDbResult result = conn ? conn->ServerSizes(schema, out) : err;
    watch.Report(Kind::Introspection, connection, "-- the sizes of the databases" + (schema.empty() ? "" : " and " + schema),
                 0, out.Size(), false, result);
    return result;
}

UltraDbResult UltraDb_ServerStatus(const std::string& connection, UltraDbResultSet& out) {
    out.Clear();
    const Watch watch;
    UltraDbResult err;
    Session conn = Resolve(connection, err);
    UltraDbResult result = conn ? conn->ServerStatus(out) : err;
    watch.Report(Kind::Introspection, connection, "-- the server's status", 0, out.Size(), false, result);
    return result;
}

UltraDbResult UltraDb_StopSessionSql(const std::string& connection, int64_t id, bool endSession,
                                     std::string& sql) {
    sql.clear();
    UltraDbResult err;
    Session conn = Resolve(connection, err);
    return conn ? conn->StopSessionSql(id, endSession, sql) : err;
}

UltraDbResult UltraDb_StopSession(const std::string& connection, int64_t id, bool endSession,
                                  bool& stopped) {
    stopped = false;
    const Watch watch;
    UltraDbResult err;
    Session conn = Resolve(connection, err);
    std::string sql;
    UltraDbResult result = conn ? conn->StopSessionSql(id, endSession, sql) : err;
    if (result) result = conn->StopSession(id, endSession, stopped);
    watch.Report(Kind::Exec, connection, sql, 0, 0, false, result);
    return result;
}

namespace {

// The checks every engine shares, made once here so no driver can forget
// them: an empty name quotes to "" - a syntax error at best - and a NUL byte
// ends the statement early in every C client library.
UltraDbResult CheckIdentifier(const std::string& name) {
    if (name.empty())
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    "an identifier must not be empty");
    if (name.find('\0') != std::string::npos)
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    "an identifier must not contain a NUL byte");
    return UltraDbResult::Ok();
}

} // namespace

UltraDbResult UltraDb_QuoteIdentifier(const std::string& connection, const std::string& name,
                                      std::string& out) {
    out.clear();
    UltraDbResult valid = CheckIdentifier(name);
    if (!valid) return valid;
    UltraDbResult err;
    Session conn = Resolve(connection, err);
    if (!conn) return err;
    out = conn->QuoteIdentifier(name);
    return UltraDbResult::Ok();
}

UltraDbResult UltraDb_QuoteQualifiedName(const std::string& connection, const std::string& schema,
                                         const std::string& name, std::string& out) {
    out.clear();
    UltraDbResult valid = CheckIdentifier(name);
    if (!valid) return valid;
    if (!schema.empty()) {
        valid = CheckIdentifier(schema);
        if (!valid) return valid;
    }
    UltraDbResult err;
    Session conn = Resolve(connection, err);
    if (!conn) return err;
    out = schema.empty() ? conn->QuoteIdentifier(name)
                         : conn->QuoteIdentifier(schema) + "." + conn->QuoteIdentifier(name);
    return UltraDbResult::Ok();
}

// ============================================================================
// Scripts, cancelling and cursors (public, declared in UltraDatabaseQuery.h)
// ============================================================================

UltraDbResult UltraDb_ExecBatch(const std::string& connection, const std::string& sql,
                                std::vector<UltraDbStatementResult>& out) {
    out.clear();
    const Watch watch;
    UltraDbResult err;
    Session conn = Resolve(connection, err);
    if (!conn) {
        watch.Report(Kind::Script, connection, sql, 0, 0, false, err);
        return err;
    }
    UltraDbResult result = conn->ExecuteBatch(sql, out);
    if (watch) {
        // One report per statement, each with its own outcome; the engine
        // runs the script as one call, so none has a time of its own.
        for (const auto& statement : out)
            watch.Report(Kind::Script, connection, statement.sql, 0, statement.returnsRows ? statement.rows.Size() : 0,
                         false, statement.result, true);
    }
    return result;
}

UltraDbResult UltraDb_CancelQuery(const std::string& connection) {
    // The name's current entry and every one it had before that a call still
    // runs on: a statement that began before the name was re-registered or
    // closed is still the name's statement to stop.
    std::vector<std::shared_ptr<ConnEntry>> entries;
    {
        std::lock_guard<std::mutex> lk(RegistryMutex());
        auto it = Registry().find(connection);
        if (it != Registry().end()) entries.push_back(it->second);
        auto retired = Retired().find(connection);
        if (retired != Retired().end()) {
            for (const auto& weak : retired->second)
                if (auto entry = weak.lock()) entries.push_back(std::move(entry));
        }
    }
    if (entries.empty())
        return UltraDbResult::Error(UltraDbResultCode::ConnectionNotFound,
                                    "no connection named '" + connection + "'");
    UltraDbResult result = UltraDbResult::Ok();
    for (const auto& e : entries) {
        // Not Resolve: a cancel must not open a connection, and must not wait
        // on anything the running statement holds. openMtx is held only while
        // a connection is being opened, never while a statement runs.
        IUltraDbConnection* conn = nullptr;
        {
            std::lock_guard<std::mutex> lk(e->openMtx);
            conn = e->conn.get();
        }
        if (!conn) continue;   // never opened: nothing is running
        UltraDbResult cancelled = conn->Cancel();
        if (!cancelled && result) result = cancelled;
    }
    return result;
}

UltraDbHandle UltraDb_OpenCursor(const std::string& connection, const std::string& sql,
                                 const UltraDbParams& params, UltraDbResult* error) {
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

    const Watch watch;
    std::unique_ptr<IUltraDbCursor> cursor = conn->OpenCursor(sql, params, err);
    watch.Report(Kind::Cursor, connection, sql, params.size(), 0, false, cursor ? UltraDbResult::Ok() : err);
    if (!cursor) { if (error) *error = err; return UltraDbInvalidHandle; }

    UltraDbHandle h = g_nextHandle.fetch_add(1);
    {
        std::lock_guard<std::mutex> lk(HandleMutex());
        CursorTable()[h] = CursorEntry{e, std::shared_ptr<IUltraDbCursor>(std::move(cursor))};
    }
    if (error) *error = UltraDbResult::Ok();
    return h;
}

UltraDbResult UltraDb_FetchRows(UltraDbHandle cursor, size_t maxRows,
                                UltraDbResultSet& out, bool& exhausted) {
    out.Clear();
    exhausted = false;
    CursorEntry held;   // this call's share: see PreparedEntry
    {
        std::lock_guard<std::mutex> lk(HandleMutex());
        auto it = CursorTable().find(cursor);
        if (it == CursorTable().end())
            return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                        "invalid cursor handle");
        held = it->second;
    }
    if (maxRows == 0)
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    "a page must hold at least one row");
    return held.cursor->Fetch(maxRows, out, exhausted);
}

UltraDbResult UltraDb_CloseCursor(UltraDbHandle cursor) {
    CursorEntry entry;
    {
        std::lock_guard<std::mutex> lk(HandleMutex());
        auto it = CursorTable().find(cursor);
        if (it == CursorTable().end())
            return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                        "invalid cursor handle");
        entry = std::move(it->second);
        CursorTable().erase(it);
    }
    // Released outside the handle lock: closing a server-side cursor is a
    // round trip, and other handles should not wait for it. A fetch still
    // running holds its own share and closes it when it returns.
    entry.cursor.reset();
    return UltraDbResult::Ok();
}

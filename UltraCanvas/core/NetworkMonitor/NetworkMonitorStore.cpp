// core/NetworkMonitor/NetworkMonitorStore.cpp
// The activity store over UltraDatabase. Parameter binding only - hostnames
// and process command lines are attacker-influenced text and never reach
// the SQL as text. One transaction per snapshot; a per-store mutex so the
// window's history view and the recording thread cannot interleave on the
// single SQLite connection.
//
// Without UltraDatabase in the build (ULTRACANVAS_HAS_DATABASE undefined)
// this file compiles to stubs that report NotSupported.
//
// Version: 0.3.0
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework / ULTRA OS
#include "NetworkMonitor/NetworkMonitorStore.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#ifdef ULTRACANVAS_HAS_DATABASE
#include "UltraDatabase/UltraDatabase.h"
#endif

namespace UltraCanvas {

// ===== RecordedFlow =====

bool RecordedFlow::IsLoopback() const {
    auto loopback = [](const std::string& address) {
        return address.rfind("127.", 0) == 0 || address == "::1" ||
               address.rfind("::ffff:127.", 0) == 0;
    };
    return loopback(localAddress) || loopback(remoteAddress);
}

std::string RecordedFlow::LocalEndpoint() const {
    return NetworkMonitor_FormatEndpoint(localAddress, localPort);
}

std::string RecordedFlow::RemoteEndpoint() const {
    return NetworkMonitor_FormatEndpoint(remoteAddress, remotePort);
}

int64_t NetworkMonitor_Now() {
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

#ifndef ULTRACANVAS_HAS_DATABASE

// ===== NO DATABASE IN THIS BUILD =====

namespace {
NetworkMonitorResult NoStore() {
    return NetworkMonitorResult::Error(NetworkMonitorResultCode::NotSupported,
                                       "This build has no UltraDatabase, so no activity store.");
}
} // namespace

bool NetworkMonitor_StoreAvailable() { return false; }
NetworkMonitorResult NetworkMonitor_OpenStore(const NetworkMonitorStoreOptions&, NetworkMonitorStoreHandle& out) {
    out = NetworkMonitorInvalidStore;
    return NoStore();
}
NetworkMonitorResult NetworkMonitor_CloseStore(NetworkMonitorStoreHandle) { return NoStore(); }
NetworkMonitorResult NetworkMonitor_RecordSnapshot(NetworkMonitorStoreHandle, const std::vector<NetworkConnection>&, int64_t) { return NoStore(); }
NetworkMonitorResult NetworkMonitor_QueryFlows(NetworkMonitorStoreHandle, const ActivityQuery&, std::vector<RecordedFlow>& out) { out.clear(); return NoStore(); }
NetworkMonitorResult NetworkMonitor_QueryDailyTotals(NetworkMonitorStoreHandle, const ActivityQuery&, std::vector<DailyProcessTotal>& out) { out.clear(); return NoStore(); }
NetworkMonitorResult NetworkMonitor_RollUp(NetworkMonitorStoreHandle, int64_t, int64_t* rolledUp) { if (rolledUp) *rolledUp = 0; return NoStore(); }
NetworkMonitorResult NetworkMonitor_ApplyRetention(NetworkMonitorStoreHandle, int64_t) { return NoStore(); }
NetworkMonitorResult NetworkMonitor_Purge(NetworkMonitorStoreHandle) { return NoStore(); }
NetworkMonitorResult NetworkMonitor_StoreStats(NetworkMonitorStoreHandle, NetworkMonitorStoreStats& out) { out = NetworkMonitorStoreStats(); return NoStore(); }
NetworkMonitorResult NetworkMonitor_ExportFlowsCsv(NetworkMonitorStoreHandle, const ActivityQuery&, const std::string&, int64_t* rows) { if (rows) *rows = 0; return NoStore(); }

#else

namespace {

// ===== SCHEMA =====
// Version 1. A flow row is one connection across consecutive snapshots;
// processes are deduplicated so a busy browser is one row, not thousands.
constexpr int kSchemaVersion = 1;
const char* const kSchemaV1 =
    "CREATE TABLE IF NOT EXISTS processes ("
    "  id INTEGER PRIMARY KEY,"
    "  pid INTEGER NOT NULL,"
    "  name TEXT NOT NULL,"
    "  executable TEXT NOT NULL,"
    "  user TEXT NOT NULL,"
    "  UNIQUE(pid, name, executable, user));"
    "CREATE TABLE IF NOT EXISTS flows ("
    "  id INTEGER PRIMARY KEY,"
    "  transport INTEGER NOT NULL,"
    "  family INTEGER NOT NULL,"
    "  local_address TEXT NOT NULL,"
    "  local_port INTEGER NOT NULL,"
    "  remote_address TEXT NOT NULL,"
    "  remote_port INTEGER NOT NULL,"
    "  state INTEGER NOT NULL,"
    "  inode INTEGER NOT NULL DEFAULT 0,"
    "  process_id INTEGER REFERENCES processes(id),"
    "  first_seen INTEGER NOT NULL,"
    "  last_seen INTEGER NOT NULL,"
    "  snapshots INTEGER NOT NULL DEFAULT 1,"
    "  bytes_sent INTEGER,"
    "  bytes_received INTEGER);"
    "CREATE INDEX IF NOT EXISTS flows_last_seen ON flows(last_seen);"
    "CREATE INDEX IF NOT EXISTS flows_key ON flows(transport, family, local_address, local_port,"
    "  remote_address, remote_port, inode, process_id);"
    "CREATE TABLE IF NOT EXISTS daily_totals ("
    "  day INTEGER NOT NULL,"
    "  process_name TEXT NOT NULL,"
    "  executable TEXT NOT NULL,"
    "  remote_address TEXT NOT NULL,"
    "  flows INTEGER NOT NULL,"
    "  counted_flows INTEGER NOT NULL,"
    "  bytes_sent INTEGER NOT NULL,"
    "  bytes_received INTEGER NOT NULL,"
    "  PRIMARY KEY(day, process_name, executable, remote_address));"
    "CREATE TABLE IF NOT EXISTS snapshots ("
    "  id INTEGER PRIMARY KEY,"
    "  observed_at INTEGER NOT NULL,"
    "  connections INTEGER NOT NULL);";

struct StoreState {
    std::string connection;
    NetworkMonitorStoreOptions options;
    std::mutex mutex;
    // (pid|name|exe|user) -> processes.id, so a snapshot's hundred sockets of
    // one browser cost one lookup, not a hundred.
    std::unordered_map<std::string, int64_t> processIds;
};

std::mutex g_registryMutex;
std::map<NetworkMonitorStoreHandle, std::shared_ptr<StoreState>> g_stores;
NetworkMonitorStoreHandle g_nextHandle = 1;

std::shared_ptr<StoreState> Find(NetworkMonitorStoreHandle handle) {
    std::lock_guard<std::mutex> lock(g_registryMutex);
    auto found = g_stores.find(handle);
    return found == g_stores.end() ? nullptr : found->second;
}

NetworkMonitorResult BadHandle() {
    return NetworkMonitorResult::Error(NetworkMonitorResultCode::InvalidArgument,
                                       "Not an open activity store.");
}

NetworkMonitorResult Storage(const std::string& what, const UltraDbResult& result) {
    return NetworkMonitorResult::Error(NetworkMonitorResultCode::StorageError,
                                       what + ": " + result.message);
}

UltraDbValue OptionalBytes(const std::optional<uint64_t>& bytes) {
    return bytes ? UltraDbValue(static_cast<int64_t>(*bytes)) : UltraDbValue::Null();
}

std::optional<uint64_t> BytesFrom(const UltraDbValue& value) {
    if (value.IsNull()) return std::nullopt;
    return static_cast<uint64_t>(value.AsInt64());
}

// The processes row for this identity, inserted if new. NULL for none.
UltraDbValue ProcessRowFor(StoreState& state, UltraDbHandle tx,
                           const std::optional<ProcessIdentity>& process,
                           NetworkMonitorResult& failure) {
    if (!process) return UltraDbValue::Null();
    const std::string key = std::to_string(process->pid) + '|' + process->displayName + '|' +
                            process->executablePath + '|' + process->userName;
    auto cached = state.processIds.find(key);
    if (cached != state.processIds.end()) return UltraDbValue(cached->second);

    const UltraDbParams identity = { process->pid, process->displayName,
                                     process->executablePath, process->userName };
    UltraDbResult inserted = UltraDb_ExecInTx(
        tx, "INSERT OR IGNORE INTO processes(pid, name, executable, user) VALUES(?, ?, ?, ?)",
        identity);
    if (!inserted) { failure = Storage("recording the process", inserted); return UltraDbValue::Null(); }
    UltraDbResultSet rows;
    UltraDbResult found = UltraDb_QueryInTx(
        tx, "SELECT id FROM processes WHERE pid = ? AND name = ? AND executable = ? AND user = ?",
        identity, rows);
    if (!found || rows.Empty()) { failure = Storage("finding the process", found); return UltraDbValue::Null(); }
    const int64_t id = rows.Row(0)[0].AsInt64();
    state.processIds.emplace(key, id);
    return UltraDbValue(id);
}

std::string FlowSelect() {
    return "SELECT f.id, f.transport, f.family, f.local_address, f.local_port, f.remote_address,"
           " f.remote_port, f.state, f.first_seen, f.last_seen, f.snapshots, f.bytes_sent,"
           " f.bytes_received, p.pid, p.name, p.executable, p.user"
           " FROM flows f LEFT JOIN processes p ON p.id = f.process_id WHERE 1 = 1";
}

// The WHERE clauses a query adds, and their parameters, in order.
void AppendFlowFilters(const ActivityQuery& query, std::string& sql, UltraDbParams& params) {
    if (query.since) { sql += " AND f.last_seen >= ?"; params.push_back(*query.since); }
    if (query.until) { sql += " AND f.first_seen <= ?"; params.push_back(*query.until); }
    if (query.pid) { sql += " AND p.pid = ?"; params.push_back(*query.pid); }
    if (!query.processName.empty()) { sql += " AND p.name = ?"; params.push_back(query.processName); }
    if (!query.text.empty()) {
        const std::string pattern = "%" + query.text + "%";
        sql += " AND (f.local_address LIKE ? OR f.remote_address LIKE ? OR p.name LIKE ? OR p.executable LIKE ?)";
        for (int i = 0; i < 4; ++i) params.push_back(pattern);
    }
    if (!query.includeListening) {
        sql += " AND f.state <> ? AND f.state <> ?";
        params.push_back(static_cast<int>(NetworkConnectionState::Listening));
        params.push_back(static_cast<int>(NetworkConnectionState::Unconnected));
    }
}

RecordedFlow FlowFrom(const UltraDbRow& row) {
    RecordedFlow flow;
    flow.id = row[0].AsInt64();
    flow.transport = static_cast<NetworkTransport>(row[1].AsInt());
    flow.family = static_cast<NetworkAddressFamily>(row[2].AsInt());
    flow.localAddress = row[3].AsString();
    flow.localPort = static_cast<uint16_t>(row[4].AsInt());
    flow.remoteAddress = row[5].AsString();
    flow.remotePort = static_cast<uint16_t>(row[6].AsInt());
    flow.lastState = static_cast<NetworkConnectionState>(row[7].AsInt());
    flow.firstSeen = row[8].AsInt64();
    flow.lastSeen = row[9].AsInt64();
    flow.snapshots = row[10].AsInt();
    flow.bytesSent = BytesFrom(row[11]);
    flow.bytesReceived = BytesFrom(row[12]);
    if (!row[13].IsNull()) {
        ProcessIdentity process;
        process.pid = row[13].AsU32();
        process.displayName = row[14].AsString();
        process.executablePath = row[15].AsString();
        process.userName = row[16].AsString();
        flow.process = process;
    }
    return flow;
}

std::string CsvField(const std::string& text) {
    if (text.find_first_of(",\"\r\n") == std::string::npos) return text;
    std::string quoted = "\"";
    for (char c : text) { if (c == '"') quoted += '"'; quoted += c; }
    return quoted + "\"";
}

std::string IsoUtc(int64_t seconds) {
    const std::time_t when = static_cast<std::time_t>(seconds);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &when);
#else
    gmtime_r(&when, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

} // namespace

bool NetworkMonitor_StoreAvailable() { return true; }

NetworkMonitorResult NetworkMonitor_OpenStore(const NetworkMonitorStoreOptions& options,
                                              NetworkMonitorStoreHandle& out) {
    out = NetworkMonitorInvalidStore;
    if (options.path.empty()) {
        return NetworkMonitorResult::Error(NetworkMonitorResultCode::InvalidArgument,
                                           "The store needs a path, or \":memory:\".");
    }
    if (options.retentionDays < 1) {
        return NetworkMonitorResult::Error(NetworkMonitorResultCode::InvalidArgument,
                                           "retentionDays must be at least 1.");
    }
    auto state = std::make_shared<StoreState>();
    state->options = options;
    NetworkMonitorStoreHandle handle;
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        handle = g_nextHandle++;
    }
    state->connection = "networkmonitor-store-" + std::to_string(handle);

    UltraDbConnectionConfig config;
    config.name = state->connection;
    config.driver = "sqlite";
    config.database = options.path;
    if (UltraDbResult registered = UltraDb_RegisterConnection(config); !registered) {
        return Storage("opening the activity store", registered);
    }
    int existing = 0;
    if (UltraDb_GetSchemaVersion(state->connection, existing) && existing > kSchemaVersion) {
        UltraDb_CloseConnection(state->connection);
        return NetworkMonitorResult::Error(NetworkMonitorResultCode::StorageError,
            "The activity store has schema version " + std::to_string(existing) +
            "; this build knows only " + std::to_string(kSchemaVersion) + ".");
    }
    const std::vector<UltraDbMigration> steps = { { 1, "NetworkMonitor activity store", kSchemaV1 } };
    if (UltraDbResult migrated = UltraDb_Migrate(state->connection, steps); !migrated) {
        UltraDb_CloseConnection(state->connection);
        return Storage("creating the activity store's schema", migrated);
    }
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        g_stores.emplace(handle, state);
    }
    out = handle;
    return NetworkMonitorResult::Ok();
}

NetworkMonitorResult NetworkMonitor_CloseStore(NetworkMonitorStoreHandle store) {
    std::shared_ptr<StoreState> state;
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        auto found = g_stores.find(store);
        if (found == g_stores.end()) return BadHandle();
        state = found->second;
        g_stores.erase(found);
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    UltraDb_CloseConnection(state->connection);
    return NetworkMonitorResult::Ok();
}

NetworkMonitorResult NetworkMonitor_RecordSnapshot(NetworkMonitorStoreHandle store,
                                                   const std::vector<NetworkConnection>& connections,
                                                   int64_t observedAt) {
    auto state = Find(store);
    if (!state) return BadHandle();
    if (observedAt == 0) observedAt = NetworkMonitor_Now();
    std::lock_guard<std::mutex> lock(state->mutex);

    UltraDbResult error;
    const UltraDbHandle tx = UltraDb_Begin(state->connection, &error);
    if (tx == UltraDbInvalidHandle) return Storage("starting the snapshot", error);

    NetworkMonitorResult failure = NetworkMonitorResult::Ok();
    UltraDbResult step = UltraDb_ExecInTx(
        tx, "INSERT INTO snapshots(observed_at, connections) VALUES(?, ?)",
        { observedAt, static_cast<int64_t>(connections.size()) });
    if (!step) failure = Storage("recording the snapshot", step);

    for (const auto& c : connections) {
        if (!failure) break;
        const UltraDbValue processRow = ProcessRowFor(*state, tx, c.process, failure);
        if (!failure) break;

        // Continue the most recent flow with this key if it was seen
        // recently enough; otherwise this is a new conversation.
        UltraDbResultSet recent;
        step = UltraDb_QueryInTx(
            tx,
            "SELECT id, last_seen FROM flows WHERE transport = ? AND family = ? AND local_address = ?"
            " AND local_port = ? AND remote_address = ? AND remote_port = ? AND inode = ?"
            " AND process_id IS ? ORDER BY last_seen DESC LIMIT 1",
            { static_cast<int>(c.transport), static_cast<int>(c.family), c.localAddress,
              static_cast<int>(c.localPort), c.remoteAddress, static_cast<int>(c.remotePort),
              static_cast<int64_t>(c.socketInode), processRow },
            recent);
        if (!step) { failure = Storage("looking up the flow", step); break; }

        if (!recent.Empty() && observedAt - recent.Row(0)[1].AsInt64() <= kFlowContinuationSeconds) {
            step = UltraDb_ExecInTx(
                tx,
                "UPDATE flows SET last_seen = ?, state = ?, snapshots = snapshots + 1,"
                " bytes_sent = ?, bytes_received = ? WHERE id = ?",
                { observedAt, static_cast<int>(c.state), OptionalBytes(c.bytesSent),
                  OptionalBytes(c.bytesReceived), recent.Row(0)[0].AsInt64() });
        } else {
            step = UltraDb_ExecInTx(
                tx,
                "INSERT INTO flows(transport, family, local_address, local_port, remote_address,"
                " remote_port, state, inode, process_id, first_seen, last_seen, snapshots,"
                " bytes_sent, bytes_received) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1, ?, ?)",
                { static_cast<int>(c.transport), static_cast<int>(c.family), c.localAddress,
                  static_cast<int>(c.localPort), c.remoteAddress, static_cast<int>(c.remotePort),
                  static_cast<int>(c.state), static_cast<int64_t>(c.socketInode), processRow,
                  observedAt, observedAt, OptionalBytes(c.bytesSent), OptionalBytes(c.bytesReceived) });
        }
        if (!step) { failure = Storage("recording the flow", step); break; }
    }

    if (!failure) {
        UltraDb_Rollback(tx);
        return failure;
    }
    if (UltraDbResult committed = UltraDb_Commit(tx); !committed) {
        return Storage("committing the snapshot", committed);
    }
    return NetworkMonitorResult::Ok();
}

NetworkMonitorResult NetworkMonitor_QueryFlows(NetworkMonitorStoreHandle store,
                                               const ActivityQuery& query,
                                               std::vector<RecordedFlow>& out) {
    out.clear();
    auto state = Find(store);
    if (!state) return BadHandle();
    std::lock_guard<std::mutex> lock(state->mutex);

    std::string sql = FlowSelect();
    UltraDbParams params;
    AppendFlowFilters(query, sql, params);
    sql += " ORDER BY f.last_seen DESC, f.id DESC";
    // The loopback filter is applied after the fact, so the limit is too.
    if (query.includeLoopback && query.limit > 0) {
        sql += " LIMIT ?";
        params.push_back(query.limit);
    }
    UltraDbResultSet rows;
    if (UltraDbResult result = UltraDb_Query(state->connection, sql, params, rows); !result) {
        return Storage("reading the flows", result);
    }
    for (const auto& row : rows) {
        RecordedFlow flow = FlowFrom(row);
        if (!query.includeLoopback && flow.IsLoopback()) continue;
        out.push_back(std::move(flow));
        if (query.limit > 0 && static_cast<int>(out.size()) >= query.limit) break;
    }
    return NetworkMonitorResult::Ok();
}

NetworkMonitorResult NetworkMonitor_QueryDailyTotals(NetworkMonitorStoreHandle store,
                                                     const ActivityQuery& query,
                                                     std::vector<DailyProcessTotal>& out) {
    out.clear();
    auto state = Find(store);
    if (!state) return BadHandle();
    std::lock_guard<std::mutex> lock(state->mutex);

    std::string sql = "SELECT day, process_name, executable, remote_address, flows, counted_flows,"
                      " bytes_sent, bytes_received FROM daily_totals WHERE 1 = 1";
    UltraDbParams params;
    if (query.since) { sql += " AND day >= ?"; params.push_back(*query.since - (*query.since % 86400)); }
    if (query.until) { sql += " AND day <= ?"; params.push_back(*query.until); }
    if (!query.processName.empty()) { sql += " AND process_name = ?"; params.push_back(query.processName); }
    if (!query.text.empty()) {
        const std::string pattern = "%" + query.text + "%";
        sql += " AND (process_name LIKE ? OR executable LIKE ? OR remote_address LIKE ?)";
        for (int i = 0; i < 3; ++i) params.push_back(pattern);
    }
    sql += " ORDER BY day DESC, flows DESC";
    if (query.limit > 0) { sql += " LIMIT ?"; params.push_back(query.limit); }

    UltraDbResultSet rows;
    if (UltraDbResult result = UltraDb_Query(state->connection, sql, params, rows); !result) {
        return Storage("reading the daily totals", result);
    }
    for (const auto& row : rows) {
        DailyProcessTotal total;
        total.day = row[0].AsInt64();
        total.processName = row[1].AsString();
        total.executablePath = row[2].AsString();
        total.remoteAddress = row[3].AsString();
        total.flows = row[4].AsInt();
        total.countedFlows = row[5].AsInt();
        total.bytesSent = static_cast<uint64_t>(row[6].AsInt64());
        total.bytesReceived = static_cast<uint64_t>(row[7].AsInt64());
        out.push_back(std::move(total));
    }
    return NetworkMonitorResult::Ok();
}

NetworkMonitorResult NetworkMonitor_RollUp(NetworkMonitorStoreHandle store, int64_t olderThan,
                                           int64_t* rolledUp) {
    if (rolledUp) *rolledUp = 0;
    auto state = Find(store);
    if (!state) return BadHandle();
    std::lock_guard<std::mutex> lock(state->mutex);

    UltraDbResult error;
    const UltraDbHandle tx = UltraDb_Begin(state->connection, &error);
    if (tx == UltraDbInvalidHandle) return Storage("starting the roll-up", error);

    // A flow is counted on the UTC day it began. Byte totals sum only the
    // flows that carried counters, and say how many did.
    UltraDbResult step = UltraDb_ExecInTx(
        tx,
        "INSERT INTO daily_totals(day, process_name, executable, remote_address, flows,"
        " counted_flows, bytes_sent, bytes_received)"
        " SELECT (f.first_seen / 86400) * 86400, COALESCE(p.name, '(unattributed)'),"
        "  COALESCE(p.executable, ''), f.remote_address, COUNT(*),"
        "  SUM(CASE WHEN f.bytes_sent IS NOT NULL THEN 1 ELSE 0 END),"
        "  COALESCE(SUM(f.bytes_sent), 0), COALESCE(SUM(f.bytes_received), 0)"
        " FROM flows f LEFT JOIN processes p ON p.id = f.process_id"
        " WHERE f.last_seen < ? GROUP BY 1, 2, 3, 4"
        " ON CONFLICT(day, process_name, executable, remote_address) DO UPDATE SET"
        "  flows = flows + excluded.flows, counted_flows = counted_flows + excluded.counted_flows,"
        "  bytes_sent = bytes_sent + excluded.bytes_sent,"
        "  bytes_received = bytes_received + excluded.bytes_received",
        { olderThan });
    if (!step) { UltraDb_Rollback(tx); return Storage("rolling flows up", step); }

    step = UltraDb_ExecInTx(tx, "DELETE FROM flows WHERE last_seen < ?", { olderThan });
    if (!step) { UltraDb_Rollback(tx); return Storage("dropping rolled-up flows", step); }
    const int64_t dropped = step.affectedRows;

    step = UltraDb_ExecInTx(
        tx, "DELETE FROM processes WHERE id NOT IN (SELECT DISTINCT process_id FROM flows"
            " WHERE process_id IS NOT NULL)");
    if (!step) { UltraDb_Rollback(tx); return Storage("dropping unreferenced processes", step); }

    if (UltraDbResult committed = UltraDb_Commit(tx); !committed) {
        return Storage("committing the roll-up", committed);
    }
    state->processIds.clear();
    if (rolledUp) *rolledUp = dropped;
    return NetworkMonitorResult::Ok();
}

NetworkMonitorResult NetworkMonitor_ApplyRetention(NetworkMonitorStoreHandle store, int64_t now) {
    auto state = Find(store);
    if (!state) return BadHandle();
    if (now == 0) now = NetworkMonitor_Now();
    const int64_t window = static_cast<int64_t>(state->options.retentionDays) * 86400;
    if (NetworkMonitorResult rolled = NetworkMonitor_RollUp(store, now - window); !rolled) return rolled;

    std::lock_guard<std::mutex> lock(state->mutex);
    UltraDbResult step = UltraDb_Exec(state->connection, "DELETE FROM daily_totals WHERE day < ?",
                                      { now - 12 * window });
    if (!step) return Storage("dropping old daily totals", step);
    step = UltraDb_Exec(state->connection, "DELETE FROM snapshots WHERE observed_at < ?",
                        { now - window });
    if (!step) return Storage("dropping old snapshot records", step);
    return NetworkMonitorResult::Ok();
}

NetworkMonitorResult NetworkMonitor_Purge(NetworkMonitorStoreHandle store) {
    auto state = Find(store);
    if (!state) return BadHandle();
    std::lock_guard<std::mutex> lock(state->mutex);
    UltraDbResult step = UltraDb_Exec(state->connection,
        "DELETE FROM flows; DELETE FROM daily_totals; DELETE FROM snapshots; DELETE FROM processes;");
    if (!step) return Storage("purging the activity store", step);
    state->processIds.clear();
    return NetworkMonitorResult::Ok();
}

NetworkMonitorResult NetworkMonitor_StoreStats(NetworkMonitorStoreHandle store,
                                               NetworkMonitorStoreStats& out) {
    out = NetworkMonitorStoreStats();
    auto state = Find(store);
    if (!state) return BadHandle();
    std::lock_guard<std::mutex> lock(state->mutex);
    UltraDbResultSet rows;
    UltraDbResult step = UltraDb_Query(state->connection,
        "SELECT COUNT(*), COALESCE(MIN(first_seen), 0), COALESCE(MAX(last_seen), 0) FROM flows", rows);
    if (!step || rows.Empty()) return Storage("counting flows", step);
    out.flows = rows.Row(0)[0].AsInt64();
    out.oldestFlow = rows.Row(0)[1].AsInt64();
    out.newestFlow = rows.Row(0)[2].AsInt64();
    step = UltraDb_Query(state->connection, "SELECT COUNT(*) FROM daily_totals", rows);
    if (!step || rows.Empty()) return Storage("counting daily totals", step);
    out.dailyTotals = rows.Row(0)[0].AsInt64();
    step = UltraDb_Query(state->connection, "SELECT COUNT(*) FROM snapshots", rows);
    if (!step || rows.Empty()) return Storage("counting snapshots", step);
    out.snapshots = rows.Row(0)[0].AsInt64();
    return NetworkMonitorResult::Ok();
}

NetworkMonitorResult NetworkMonitor_ExportFlowsCsv(NetworkMonitorStoreHandle store,
                                                   const ActivityQuery& query,
                                                   const std::string& path,
                                                   int64_t* rowsWritten) {
    if (rowsWritten) *rowsWritten = 0;
    std::vector<RecordedFlow> flows;
    if (NetworkMonitorResult read = NetworkMonitor_QueryFlows(store, query, flows); !read) return read;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return NetworkMonitorResult::Error(NetworkMonitorResultCode::IoError,
                                           "Could not write " + path);
    }
    file << "first_seen,last_seen,snapshots,transport,family,local,remote,state,"
            "application,pid,executable,user,bytes_sent,bytes_received\r\n";
    for (const auto& f : flows) {
        file << IsoUtc(f.firstSeen) << ',' << IsoUtc(f.lastSeen) << ',' << f.snapshots << ','
             << NetworkMonitor_TransportName(f.transport) << ','
             << (f.family == NetworkAddressFamily::IPv6 ? "IPv6" : "IPv4") << ','
             << CsvField(f.LocalEndpoint()) << ',' << CsvField(f.RemoteEndpoint()) << ','
             << NetworkMonitor_StateName(f.lastState) << ','
             << CsvField(f.process ? f.process->displayName : std::string("(unattributed)")) << ','
             << (f.process ? std::to_string(f.process->pid) : std::string()) << ','
             << CsvField(f.process ? f.process->executablePath : std::string()) << ','
             << CsvField(f.process ? f.process->userName : std::string()) << ','
             << (f.bytesSent ? std::to_string(*f.bytesSent) : std::string()) << ','
             << (f.bytesReceived ? std::to_string(*f.bytesReceived) : std::string()) << "\r\n";
    }
    if (!file) {
        return NetworkMonitorResult::Error(NetworkMonitorResultCode::IoError,
                                           "Writing " + path + " failed part-way.");
    }
    if (rowsWritten) *rowsWritten = static_cast<int64_t>(flows.size());
    return NetworkMonitorResult::Ok();
}

#endif // ULTRACANVAS_HAS_DATABASE

} // namespace UltraCanvas

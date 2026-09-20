// include/NetworkMonitor/NetworkMonitorStore.h
// The activity store: snapshots recorded over time into an UltraDatabase
// (SQLite) file, so "what was this machine talking to last Tuesday?" has an
// answer. A connection seen in consecutive snapshots is one *flow* with a
// first-seen, a last-seen and its latest counters; flows older than the
// retention window are rolled up into per-day, per-process, per-peer totals
// and dropped, so the file stays small on a busy desktop.
//
// This database is a detailed record of a person's activity - more
// sensitive than most of what it observes. It never leaves the machine,
// retention is the caller's to set and defaults short, and ":memory:" keeps
// a session entirely off disk. At-rest encryption (UltraCrypt + UltraVault)
// is a later increment.
//
// Every function returns NetworkMonitorResult; where the build has no
// UltraDatabase, NetworkMonitor_StoreAvailable() is false and every call
// reports NotSupported.
//
// Version: 0.3.0
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "NetworkMonitor/NetworkMonitor.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {

using NetworkMonitorStoreHandle = uint64_t;
constexpr NetworkMonitorStoreHandle NetworkMonitorInvalidStore = 0;

struct NetworkMonitorStoreOptions {
    // A file path (a leading "~" is expanded by UltraDatabase), or
    // ":memory:" for a store that lives only as long as the handle.
    std::string path;
    // Flows whose last sighting is older than this are rolled up into daily
    // totals and deleted by NetworkMonitor_ApplyRetention. Daily totals are
    // kept twelve times as long.
    int retentionDays = 30;
};

// One connection as recorded across snapshots. The same 5-tuple seen again
// after more than kFlowContinuationSeconds without a sighting starts a new
// flow, so a reused ephemeral port is not glued to an earlier conversation.
constexpr int64_t kFlowContinuationSeconds = 120;

struct RecordedFlow {
    int64_t                id = 0;
    NetworkTransport       transport = NetworkTransport::Tcp;
    NetworkAddressFamily   family = NetworkAddressFamily::IPv4;
    std::string            localAddress;
    uint16_t               localPort = 0;
    std::string            remoteAddress;
    uint16_t               remotePort = 0;
    NetworkConnectionState lastState = NetworkConnectionState::Unknown;
    std::optional<ProcessIdentity> process;
    int64_t                firstSeen = 0;      // Unix seconds
    int64_t                lastSeen = 0;
    int                    snapshots = 0;      // how many snapshots saw it
    std::optional<uint64_t> bytesSent;         // latest counters, if any
    std::optional<uint64_t> bytesReceived;

    bool IsLoopback() const;
    std::string LocalEndpoint() const;
    std::string RemoteEndpoint() const;
};

// A day's worth of flows between one process and one peer, produced by the
// roll-up. `day` is the Unix second the UTC day began.
struct DailyProcessTotal {
    int64_t     day = 0;
    std::string processName;      // "(unattributed)" for the rest
    std::string executablePath;
    std::string remoteAddress;    // the listeners' wildcard included
    int         flows = 0;
    int         countedFlows = 0; // flows that carried byte counters
    uint64_t    bytesSent = 0;    // over the counted flows only
    uint64_t    bytesReceived = 0;
};

struct ActivityQuery {
    std::optional<int64_t>  since;         // last seen at or after (Unix seconds)
    std::optional<int64_t>  until;         // first seen at or before
    std::optional<uint32_t> pid;
    std::string             processName;   // exact match
    std::string             text;          // substring over addresses, name, executable
    bool                    includeListening = true;
    bool                    includeLoopback = true;
    int                     limit = 1000;
};

struct NetworkMonitorStoreStats {
    int64_t flows = 0;
    int64_t dailyTotals = 0;
    int64_t snapshots = 0;
    int64_t oldestFlow = 0;   // 0 when empty
    int64_t newestFlow = 0;
};

// Whether this build carries UltraDatabase, and therefore a store at all.
bool NetworkMonitor_StoreAvailable();

// Unix seconds now - the clock every recording function defaults to.
int64_t NetworkMonitor_Now();

NetworkMonitorResult NetworkMonitor_OpenStore(const NetworkMonitorStoreOptions& options,
                                              NetworkMonitorStoreHandle& out);
NetworkMonitorResult NetworkMonitor_CloseStore(NetworkMonitorStoreHandle store);

// Records one snapshot: every connection either extends the flow it
// continues or starts a new one. One transaction per call.
NetworkMonitorResult NetworkMonitor_RecordSnapshot(NetworkMonitorStoreHandle store,
                                                   const std::vector<NetworkConnection>& connections,
                                                   int64_t observedAt = 0);

NetworkMonitorResult NetworkMonitor_QueryFlows(NetworkMonitorStoreHandle store,
                                               const ActivityQuery& query,
                                               std::vector<RecordedFlow>& out);
NetworkMonitorResult NetworkMonitor_QueryDailyTotals(NetworkMonitorStoreHandle store,
                                                     const ActivityQuery& query,
                                                     std::vector<DailyProcessTotal>& out);

// Aggregates every flow last seen before `olderThan` into the daily totals
// and deletes it. `rolledUp`, when given, receives the number of flows.
NetworkMonitorResult NetworkMonitor_RollUp(NetworkMonitorStoreHandle store, int64_t olderThan,
                                           int64_t* rolledUp = nullptr);
// The retention policy in one call: roll up flows older than the window,
// drop daily totals and snapshot records older than twelve windows.
NetworkMonitorResult NetworkMonitor_ApplyRetention(NetworkMonitorStoreHandle store, int64_t now = 0);
// Deletes everything recorded. Irreversible; the caller confirms.
NetworkMonitorResult NetworkMonitor_Purge(NetworkMonitorStoreHandle store);

NetworkMonitorResult NetworkMonitor_StoreStats(NetworkMonitorStoreHandle store,
                                               NetworkMonitorStoreStats& out);

// Writes the flows a query selects as CSV (RFC 4180 quoting, UTC ISO-8601
// times, dot-decimal numbers) to `path`.
NetworkMonitorResult NetworkMonitor_ExportFlowsCsv(NetworkMonitorStoreHandle store,
                                                   const ActivityQuery& query,
                                                   const std::string& path,
                                                   int64_t* rowsWritten = nullptr);

} // namespace UltraCanvas

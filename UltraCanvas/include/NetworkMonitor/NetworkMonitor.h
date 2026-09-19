// include/NetworkMonitor/NetworkMonitor.h
// NetworkMonitor - system-wide view of the machine's network connections and
// the processes that own them. Public surface of the module: the data model,
// the capability report, and the NetworkMonitor_* functions.
//
// This is NOT part of UltraNet. UltraNet is a client library and only ever
// sees the traffic its own process makes; this module reads the operating
// system's socket table, the same one `ss -p`, `netstat -p` and `lsof -i`
// read, and so sees every process it is allowed to look at. It observes and
// records; it never blocks, filters or modifies traffic.
//
// Phase 1 (this file): a polled snapshot of the socket table with process
// attribution, and a per-process roll-up. Byte counters, connection events,
// name resolution and file-transfer correlation are later phases - see
// Docs/Modules/NetworkMonitor/README.md for what is built and what is not.
//
// Version: 0.1.0
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== RESULT =====
// Every blocking call returns one of these, the way UltraNet_* returns
// UltraNetResult: the code says why, not just whether.
enum class NetworkMonitorResultCode {
    Success,
    NotSupported,      // no backend on this platform / build
    PermissionDenied,  // the OS refused the socket table itself
    IoError,           // the table exists but could not be read
    Unknown
};

struct NetworkMonitorResult {
    NetworkMonitorResultCode code = NetworkMonitorResultCode::Unknown;
    bool success = false;
    std::string message;

    operator bool() const { return success; }

    static NetworkMonitorResult Ok() {
        NetworkMonitorResult r;
        r.code = NetworkMonitorResultCode::Success;
        r.success = true;
        return r;
    }
    static NetworkMonitorResult Error(NetworkMonitorResultCode c, const std::string& msg) {
        NetworkMonitorResult r;
        r.code = c;
        r.success = false;
        r.message = msg;
        return r;
    }
};

// ===== DATA MODEL =====
enum class NetworkTransport { Tcp, Udp, Other };
enum class NetworkAddressFamily { IPv4, IPv6 };

// The TCP state machine, plus the two states a UDP socket can be in
// (Unconnected, or Established when connect() has fixed its peer).
enum class NetworkConnectionState {
    Unknown,
    Listening,
    SynSent,
    SynReceived,
    Established,
    FinWait1,
    FinWait2,
    CloseWait,
    Closing,
    LastAck,
    TimeWait,
    Closed,
    Unconnected
};

struct ProcessIdentity {
    uint32_t    pid = 0;
    std::string executablePath;   // "/usr/lib/firefox/firefox"; empty when unreadable
    std::string displayName;      // "firefox" - the kernel's comm, or the exe's basename
    std::string userName;         // owner of the socket, from the table's UID
};

struct NetworkConnection {
    NetworkTransport       transport = NetworkTransport::Tcp;
    NetworkAddressFamily   family    = NetworkAddressFamily::IPv4;
    std::string            localAddress;
    uint16_t               localPort = 0;
    std::string            remoteAddress;     // "0.0.0.0" / "::" for a listener
    uint16_t               remotePort = 0;
    NetworkConnectionState state = NetworkConnectionState::Unknown;

    // Raw attribution keys. `ownerUid` is the socket's owner as the kernel
    // reports it; `socketInode` is the join key between the socket table and
    // a process's descriptors (Linux). 0 where a platform has no such key.
    std::optional<uint32_t> ownerUid;
    uint64_t                socketInode = 0;

    // Empty when the backend could not attribute the socket - because the
    // process belongs to another user and the monitor is not elevated, or
    // because the socket closed between the table read and the walk.
    std::optional<ProcessIdentity> process;

    // std::optional, as UltraCanvasHardwareInfo does: "0 bytes" and "not
    // reported" are different facts. Not filled by the Phase 1 backends.
    std::optional<uint64_t> bytesSent;
    std::optional<uint64_t> bytesReceived;

    bool IsListening() const { return state == NetworkConnectionState::Listening; }
    bool IsLoopback() const;
    // "1.2.3.4:443", "[::1]:80". Empty address formats as "*".
    std::string LocalEndpoint() const;
    std::string RemoteEndpoint() const;
};

// ===== CAPABILITIES =====
// What the backend can actually deliver on this machine at the current
// privilege level. A monitor that silently under-reports is a correctness bug,
// so a caller reads this and shows the difference ("own processes only").
struct NetworkMonitorCapabilities {
    bool socketTable        = false;  // a snapshot is possible at all
    bool processAttribution = false;  // sockets can be mapped to a PID
    bool allUsers           = false;  // false = only this user's processes are attributable
    bool connectionEvents   = false;  // event-rate collection (not in Phase 1)
    bool perConnectionBytes = false;  // byte counters (not in Phase 1)
    bool dnsWithProcess     = false;  // DNS queries with a PID (not in Phase 1)
    std::string backendName;          // "procfs", "none"
    // Human-readable lines for what is missing and why, in the order a
    // status line wants them.
    std::vector<std::string> notes;
};

// ===== OPTIONS =====
struct NetworkMonitorOptions {
    bool includeListening = true;   // LISTEN / unconnected UDP sockets
    bool includeLoopback  = true;   // 127.0.0.0/8 and ::1 endpoints
    bool resolveProcesses = true;   // map sockets to PIDs (the expensive part)
};

// ===== PER-PROCESS ROLL-UP =====
struct ProcessTrafficSummary {
    ProcessIdentity process;         // pid 0 and displayName "(unattributed)" for the rest
    bool     attributed       = false;
    int      connectionCount  = 0;
    int      establishedCount = 0;
    int      listeningCount   = 0;
    // Distinct remote addresses, listeners' wildcard excluded.
    std::vector<std::string> remoteAddresses;
    // Sums, present only when every connection in the group reported a value.
    std::optional<uint64_t> bytesSent;
    std::optional<uint64_t> bytesReceived;
};

// ===== PUBLIC FUNCTIONS =====

// Cheap; may be called before any snapshot. Never throws.
NetworkMonitorCapabilities NetworkMonitor_GetCapabilities();
bool                       NetworkMonitor_IsAvailable();

// One snapshot of the socket table, filtered by `options`. `out` is replaced.
NetworkMonitorResult NetworkMonitor_ListConnections(
    std::vector<NetworkConnection>& out,
    const NetworkMonitorOptions& options = NetworkMonitorOptions());

// Groups a snapshot by owning process. Pure - no I/O - so a test drives it
// from fixtures. Sorted by connection count, then name.
std::vector<ProcessTrafficSummary> NetworkMonitor_SummarizeByProcess(
    const std::vector<NetworkConnection>& connections);

// Names for display and for the command line. Never null.
const char* NetworkMonitor_TransportName(NetworkTransport transport);
const char* NetworkMonitor_StateName(NetworkConnectionState state);
// "1.2.3.4:443", "[fe80::1]:22"; an empty address is "*".
std::string NetworkMonitor_FormatEndpoint(const std::string& address, uint16_t port);

} // namespace UltraCanvas

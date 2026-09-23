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
// This file: a polled snapshot of the socket table with process attribution
// and byte counters where the backend has them, a per-process roll-up, and
// the domain name behind a peer address where a name source has seen it
// (NetworkMonitorNames.h), and connections reported as they open and close
// where an event source runs (NetworkMonitorEvents.h). File-transfer
// correlation is a later phase - see Docs/Modules/NetworkMonitor/README.md
// for what is built and what is not.
//
// Version: 0.8.0
// Last Modified: 2026-09-23
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
    InvalidArgument,   // a bad handle or option
    StorageError,      // the activity store refused (NetworkMonitorStore.h)
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

    // "firefox (27352)", or "(unattributed)" for an empty identity.
    std::string Label() const { return displayName + " (" + std::to_string(pid) + ")"; }
};

// A connection whose other end is on this machine: which side of it this
// socket is. The server is the side whose port a listener holds.
enum class LoopbackRole { None, Client, Server };

// Where a domain name came from. The first four are *observed*: a source saw
// the query that produced the address, so the name is the one the
// application asked for. ReverseDns and Inferred are *weak*: a PTR record
// or a guess, near-useless behind a CDN, and a UI shows them as such. The
// order is the precedence when two sources name one address.
enum class NameSource {
    None,
    DnsProxy,        // the local DNS proxy saw the query (NetworkMonitorNames.h)
    EtwDnsClient,    // the Windows DNS client's own events, with the PID
    PacketCapture,   // port-53 capture (not built yet)
    Sni,             // the TLS ClientHello's server name (not built yet)
    ReverseDns,      // a PTR lookup of the address - weak
    Inferred         // a guess from other evidence - weak
};

// One answered query as a name source reports it: the name asked for and
// the addresses it resolved to, with the process where the source knows it
// (only the Windows DNS client events do).
struct DnsObservation {
    std::string              queryName;      // "www.example.com", lower-case, no trailing dot
    std::vector<std::string> addresses;      // the A / AAAA answers, in text
    std::optional<ProcessIdentity> process;  // the asking process, where known
    NameSource               source = NameSource::None;
    int64_t                  observedAt = 0; // Unix seconds; 0 = now
    int                      ttlSeconds = 0; // the shortest answer TTL; 0 = unknown
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
    // reported" are different facts. Filled where the backend has counters.
    std::optional<uint64_t> bytesSent;
    std::optional<uint64_t> bytesReceived;

    // The domain name behind remoteAddress, when a name source has seen
    // one, and which source: empty / None until then. Filled by
    // NetworkMonitor_ListConnections from the name table
    // (NetworkMonitorNames.h) when the options ask for it.
    std::string remoteName;
    NameSource  nameSource = NameSource::None;

    // The loopback chain (NetworkMonitor_DecodeLoopback): when the other
    // end of this connection is a socket on this machine, the process on
    // that end and which side this is. A mail client talking to an
    // antivirus mail proxy on 127.0.0.1:12993 is the Client with the proxy
    // as localPeer; the proxy's accepted socket is the Server with the
    // client as localPeer. `forProcesses` is the inference the other way:
    // on a process's outbound connections, the loopback clients that
    // process serves - the applications its traffic is really for.
    LoopbackRole                   loopbackRole = LoopbackRole::None;
    std::optional<ProcessIdentity> localPeer;
    std::vector<std::string>       forProcesses;   // "thunderbird (4120)", distinct

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
    bool connectionEvents   = false;  // an event source is running (NetworkMonitorEvents.h)
    bool perConnectionBytes = false;  // byte counters in the snapshot
    bool dnsWithProcess     = false;  // a running name source reports the asking PID
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
    bool resolveNames     = true;   // fill remoteName from the name table
};

// ===== PER-PROCESS ROLL-UP =====
struct ProcessTrafficSummary {
    ProcessIdentity process;         // pid 0 and displayName "(unattributed)" for the rest
    bool     attributed       = false;
    int      connectionCount  = 0;
    int      establishedCount = 0;
    int      listeningCount   = 0;
    // Distinct remote addresses, listeners' wildcard excluded, and the
    // distinct names known for them.
    std::vector<std::string> remoteAddresses;
    std::vector<std::string> remoteNames;
    // The loopback chain at process level: the local processes this one
    // connects to (a proxy it goes through), and the local processes that
    // connect to this one (the applications it serves). "name (pid)" each.
    std::vector<std::string> viaProcesses;
    std::vector<std::string> servesProcesses;
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

// Decodes the loopback chains in a snapshot: pairs every connection whose
// peer is on this machine with its mirror (the socket on the other end),
// fills loopbackRole and localPeer on both, and, on the outbound
// connections of a process that serves loopback clients, forProcesses.
// Pure; NetworkMonitor_ListConnections applies it to every snapshot before
// the filters, so a connection kept without its loopback mirror still
// carries what the mirror told. A test drives it from fixtures.
void NetworkMonitor_DecodeLoopback(std::vector<NetworkConnection>& connections);
// "client", "server", "" for None. Never null.
const char* NetworkMonitor_LoopbackRoleName(LoopbackRole role);

// Names for display and for the command line. Never null.
const char* NetworkMonitor_TransportName(NetworkTransport transport);
const char* NetworkMonitor_StateName(NetworkConnectionState state);
// "DNS proxy", "reverse DNS", ...; "none" for None.
const char* NetworkMonitor_NameSourceName(NameSource source);
// True for the observed sources, false for the weak ones and None.
bool        NetworkMonitor_NameIsObserved(NameSource source);
// "1.2.3.4:443", "[fe80::1]:22"; an empty address is "*".
std::string NetworkMonitor_FormatEndpoint(const std::string& address, uint16_t port);

// A snapshot as CSV (RFC 4180 quoting, dot-decimal numbers), in the order
// given: the per-process roll-up one row per process, with its distinct
// peers, hosts and loopback chain semicolon-joined; the connections one
// row each with the process behind it and the loopback chain. `rowsWritten`,
// when given, receives the row count.
NetworkMonitorResult NetworkMonitor_ExportSummaryCsv(const std::vector<ProcessTrafficSummary>& summaries,
                                                     const std::string& path,
                                                     int64_t* rowsWritten = nullptr);
NetworkMonitorResult NetworkMonitor_ExportConnectionsCsv(const std::vector<NetworkConnection>& connections,
                                                         const std::string& path,
                                                         int64_t* rowsWritten = nullptr);

} // namespace UltraCanvas

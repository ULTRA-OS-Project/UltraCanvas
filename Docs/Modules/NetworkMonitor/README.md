# NetworkMonitor — System-Wide Network Activity

**Status:** Phase 1 and Phase 2 implemented (Linux, Windows, macOS; persistence; domain names; connection events); throughput charts and Phase 3 in the proposal.
**Version:** 0.5.0
**Author:** UltraCanvas Framework / ULTRA OS
**Last Modified:** 2026-09-23

NetworkMonitor reads the operating system's socket table — the one `ss -p`,
`netstat -p` and `lsof -i` read — and reports every connection the machine
holds together with the process that owns it. It is the module beneath
`Apps/UltraNetMonitor`, and the answer to "which application is talking to
whom?" for any application built on the framework.

**It is not part of UltraNet.** UltraNet is a client library and only ever
sees the traffic its own process makes; NetworkMonitor observes *other
processes'* sockets, which is an operating-system question, so it sits beside
UltraNet as a sibling with no dependency on it. It observes and records; it
never blocks, filters or modifies traffic.

The research behind it — what is and is not observable without a kernel
driver, why TLS interception was rejected, the platform-by-platform mechanism
table and the privacy posture — is the proposal,
`Docs/Modules/NetworkMonitor/NetworkMonitorProposal.md`. This document is the
reference for what is built.

## What is built

| | Linux | Windows | macOS | Other |
|---|---|---|---|---|
| Socket table (TCP/UDP, v4/v6) | ✅ netlink `sock_diag`, `/proc/net/*` fallback | ✅ `GetExtendedTcpTable` / `GetExtendedUdpTable` | ✅ libproc, per process | null backend |
| Process attribution | ✅ `/proc/<pid>/fd` → `socket:[inode]` | ✅ owner PID with the row | ✅ inherent (enumerated per process) | — |
| Owning user | ✅ from the table's UID | ✅ process token | ✅ `PROC_PIDTBSDINFO` | — |
| Byte counters | ✅ `tcp_info` (TCP only) | ✅ on closed events (ETW, elevated) | none — see below | — |
| Connection events | ✅ `nf_conntrack` (root, tracker active) + the differ | ✅ kernel network ETW (elevated) + the differ | the differ only | the differ |
| Domain names | ✅ local DNS proxy, reverse DNS | ✅ DNS client events (ETW, elevated), proxy, reverse DNS | ✅ local DNS proxy, reverse DNS | proxy, reverse DNS |

Each backend reports what it cannot see in `NetworkMonitorCapabilities`:
`perConnectionBytes` is true only on Linux with netlink; `allUsers` is root
on Linux and macOS, an elevated token on Windows; `dnsWithProcess` is true
while a name source that reports the asking process is running (the
Windows DNS client events); `connectionEvents` is true while an event
source runs; and the `notes` count the processes the last snapshot could
not inspect.

Where there is no backend, `NetworkMonitor_IsAvailable()` is false, the
capabilities read as all-false with `backendName == "none"`, and every
snapshot returns `NotSupported` — never an empty table that looks like a
quiet machine.

## Public surface

Header: `UltraCanvas/include/NetworkMonitor/NetworkMonitor.h`. CMake target:
`NetworkMonitor` (static, UI-free — links headless like UltraCrypt).

```cpp
#include "NetworkMonitor/NetworkMonitor.h"
using namespace UltraCanvas;

// What can this machine deliver, at this privilege?
const NetworkMonitorCapabilities caps = NetworkMonitor_GetCapabilities();
if (!caps.allUsers) { /* "only this user's processes can be attributed" */ }

// One snapshot.
std::vector<NetworkConnection> connections;
NetworkMonitorOptions options;
options.includeListening = false;          // established flows only
if (NetworkMonitorResult r = NetworkMonitor_ListConnections(connections, options); !r) {
    // r.code: NotSupported / PermissionDenied / IoError; r.message says why
}
for (const auto& c : connections) {
    // c.process is std::optional<ProcessIdentity>: empty when the socket
    // belongs to a process this monitor may not inspect.
    printf("%s %s -> %s %s %s\n",
           NetworkMonitor_TransportName(c.transport),
           c.LocalEndpoint().c_str(), c.RemoteEndpoint().c_str(),
           NetworkMonitor_StateName(c.state),
           c.process ? c.process->displayName.c_str() : "(unattributed)");
}

// Group it by process: connection / established / listening counts and the
// distinct peers, busiest first. Pure - no I/O.
for (const auto& app : NetworkMonitor_SummarizeByProcess(connections)) { /* … */ }
```

### Types

| Type | Holds |
|---|---|
| `NetworkConnection` | transport, address family, local/remote address and port, `state`, `ownerUid`, `socketInode`, `process` (optional), `bytesSent` / `bytesReceived` (optional), `remoteName` / `nameSource` (empty / `None` until a source has named the peer) |
| `ProcessIdentity` | `pid`, `executablePath`, `displayName` (the kernel's comm), `userName` |
| `NetworkConnectionState` | the TCP state machine, plus `Unconnected` for a UDP socket without a fixed peer |
| `NetworkMonitorCapabilities` | `socketTable`, `processAttribution`, `allUsers`, `connectionEvents`, `perConnectionBytes`, `dnsWithProcess`, `backendName`, `notes` |
| `NetworkMonitorOptions` | `includeListening`, `includeLoopback`, `resolveProcesses`, `resolveNames` |
| `ProcessTrafficSummary` | one process's counts, distinct `remoteAddresses` and `remoteNames`, byte totals present only when every connection reported them |
| `NameSource` | where a name came from: `DnsProxy`, `EtwDnsClient`, `PacketCapture`, `Sni` are *observed*; `ReverseDns`, `Inferred` are *weak* |
| `DnsObservation` | one answered query as a source reports it: `queryName`, `addresses`, `process` (where known), `source`, `observedAt`, `ttlSeconds` |
| `NetworkMonitorResult` | `code`, `success`, `message`; `operator bool` |

`std::optional` throughout, as in `UltraCanvasHardwareInfo`: "0 bytes" and
"not reported" are different facts, and conflating them is how a monitor
lies.

### Functions

| Function | Does |
|---|---|
| `NetworkMonitor_GetCapabilities()` | Cheap; safe before any snapshot. Re-read it after a snapshot: the notes carry per-snapshot counts ("12 processes could not be inspected") |
| `NetworkMonitor_IsAvailable()` | Whether this build has a backend at all |
| `NetworkMonitor_ListConnections(out, options)` | One snapshot, filtered by `options`; `out` is replaced |
| `NetworkMonitor_SummarizeByProcess(connections)` | Per-process roll-up, sorted by connection count then name |
| `NetworkMonitor_TransportName` / `NetworkMonitor_StateName` | Display names, the kernel's spelling (`ESTABLISHED`, `CLOSE_WAIT`, `UNCONN`) |
| `NetworkMonitor_FormatEndpoint(address, port)` | `1.2.3.4:443`, `[::1]:22`, `*:0` |
| `NetworkMonitor_NameSourceName` / `NetworkMonitor_NameIsObserved` | A source's display name, and whether it is observed or weak |

## Connection events

A polled table misses every connection shorter than its interval — a DNS
lookup, a beacon, an API call can open, transfer and close between two
reads. `NetworkMonitor/NetworkMonitorEvents.h` reports connections as they
open, are accepted and close, the same shape as the names:
`IConnectionEventSource` is the contract, `NetworkMonitor_RegisterEventSource`
runs a source, every `NetworkConnectionEvent` reaches the listeners
(`NetworkMonitor_AddEventListener`) and a bounded ring
(`NetworkMonitor_RecentEvents`). On the way through, the registry names the
peer from the name table and, for a source that knows only the tuple,
attributes the event from a socket table it refreshes a few times a
second — in either orientation, so a tuple whose source is the remote side
becomes an *Accepted* on the listener's process — and remembers the match
so the *Closed* that follows is attributed though the socket is gone.

```cpp
#include "NetworkMonitor/NetworkMonitorEvents.h"

// The platform's own events where it has any (null on macOS) ...
if (auto system = NetworkMonitor_CreateSystemEventSource()) {
    if (NetworkMonitorResult r = NetworkMonitor_RegisterEventSource(std::move(system)); !r) { /* root / elevation */ }
}
// ... and the differ everywhere.
SnapshotDiffOptions diff;
diff.intervalMs = 250;
NetworkMonitor_RegisterEventSource(NetworkMonitor_CreateSnapshotDiffEventSource(diff));

NetworkMonitor_AddEventListener([](const NetworkConnectionEvent& e) {
    // e.kind: Opened / Accepted / Closed; e.process where known;
    // e.bytesSent / bytesReceived on Closed where the source counts.
});
NetworkMonitor_StopEventSources();   // before exit
```

| Source | Reports | Process | Bytes | Needs | Misses |
|---|---|---|---|---|---|
| **Snapshot differ** (`NetworkMonitor_CreateSnapshotDiffEventSource`) | what appeared and went between two reads of the table | from the table | the table's counters on Closed | a backend | anything shorter than its interval — and says so |
| **nf_conntrack** (Linux, `NetworkMonitor_CreateSystemEventSource`) | every tracked connection's NEW and DESTROY, over `NETLINK_NETFILTER` | attributed by the registry | both directions on DESTROY, when `nf_conntrack_acct` is on | `CAP_NET_ADMIN`, and a firewall rule that has activated the tracker | nothing the tracker sees; loopback when no rule tracks it |
| **Kernel network ETW** (Windows) | TCP connect, accept, disconnect | in the event | sends and receives summed per connection, on Closed | an elevated token | UDP (no lifecycle) |

The tracker's caveat is real: the kernel registers the conntrack hooks only
once a rule asks for connection state, so on a machine with no firewall
the source binds and nothing arrives. Its `LastError()` says so; the
monitor never adds a rule. The message parser
(`NetworkMonitorConntrack.h`) is pure and tested from captured bytes on
every platform; the Windows source is compiled on CI and, like the DNS
client source, awaits its first elevated run. eBPF stays the proposal's
open question (§10.3): the tracker gives Linux events without vendoring
libbpf or shipping CO-RE objects, at the price of needing root and an
active tracker.

## Domain names

Sockets carry addresses, not names, and resolving `104.18.x.x` back to the
site the user visited needs a separate source. The proposal (§2.3) made
that a plug-in point: `NetworkMonitor/NetworkMonitorNames.h` is the
contract (`INameSource`), the registry that runs the sources, and the
**name table** every source feeds — address → the best name known for it,
with the `NameSource` it came from. An *observed* name (a source saw the
query that produced the address) always beats a *weak* one (a PTR record,
a guess), however old; between two of the same grade the newer wins.
Names outlive their DNS TTL — at least an hour — because a connection
outlives the answer that started it.

```cpp
#include "NetworkMonitor/NetworkMonitorNames.h"

// Reverse DNS for whatever the snapshots see, on its own thread; weak.
NetworkMonitor_RegisterNameSource(NetworkMonitor_CreateReverseDnsSource(ReverseDnsOptions()));

// The local DNS proxy: every query that passes through names its answers.
DnsProxyOptions proxy;
proxy.listenPort = 5353;                       // 53 needs privilege
if (NetworkMonitorResult r = NetworkMonitor_RegisterNameSource(
        NetworkMonitor_CreateDnsProxySource(proxy)); !r) { /* r.message: the port, the upstream */ }

// Windows, elevated: the DNS client's own events, with the asking PID.
if (auto system = NetworkMonitor_CreateSystemDnsSource()) NetworkMonitor_RegisterNameSource(std::move(system));

// From here every snapshot names its peers where it can:
NetworkMonitor_ListConnections(connections);   // c.remoteName, c.nameSource
NameRecord record;
if (NetworkMonitor_LookupName("93.184.216.34", record)) { /* record.name, record.source */ }

NetworkMonitor_StopNameSources();               // before exit: joins their threads
```

| Source | Sees | Gives the PID | Privilege | Blind to |
|---|---|---|---|---|
| **Local DNS proxy** (`NetworkMonitor_CreateDnsProxySource`) | every client of the system resolver, once the resolver points at 127.0.0.1 | no | port 53 needs root / admin; any other port needs the resolver pointed at it | a browser resolving over HTTPS on its own |
| **Reverse DNS** (`NetworkMonitor_CreateReverseDnsSource`) | any public address | no | none | a CDN: everything answers "cloudflare". Labelled weak |
| **Windows DNS client events** (`NetworkMonitor_CreateSystemDnsSource`) | every query through the Windows resolver | **yes** | elevated token | DNS over HTTPS inside the browser |

The proxy is a window, not a resolver: one thread, one `select()` loop,
each UDP query forwarded on a socket of its own and the answer relayed
back to the client and read on the way; TCP (a truncated answer makes a
resolver retry over TCP) relayed the same way. It never rewrites, caches
or filters, refuses an upstream that is itself, and reads the upstream
from `/etc/resolv.conf` or `GetNetworkParams` when none is given
(`NetworkMonitor_SystemResolver`). The wire format — `NetworkMonitorDns.h`
— is pure functions over bytes, every read bounds-checked, compression
pointers that do not go backwards refused, CNAME chains followed so the
address maps to the name the application asked for.

Reverse DNS never looks up loopback, link-local, multicast or (unless
`includePrivateRanges`) private addresses, remembers a missing PTR for ten
minutes, and drops queued addresses past `maxQueueLength` rather than
falling behind. `NetworkMonitor_WaitForNames(ms)` waits for its queue.

The Windows source is a real-time ETW session on
`Microsoft-Windows-DNS-Client` (event 3008, *DNS query completed*), whose
payload carries the name and the answers and whose header the PID. It
needs an elevated token and reports `PermissionDenied` without one. It is
compiled on the Windows CI rows; it has not yet been exercised at run
time — treat the first elevated run as its acceptance test.

An application that has a better source (a browser extension, an
enterprise resolver's log) implements `INameSource` and registers it; the
core, the store and the UI treat it like the built-in ones.
`NetworkMonitor_AddNameListener` hears every observation from any source —
the way UltraNetMonitor records them while recording.

## The activity store

`NetworkMonitor/NetworkMonitorStore.h`, over UltraDatabase's bundled SQLite.
It turns snapshots into **flows** — one row per connection across the
snapshots that saw it — and, past a retention window, into **daily totals**
per process and peer, so the file stays small however busy the machine.

```cpp
#include "NetworkMonitor/NetworkMonitorStore.h"

NetworkMonitorStoreOptions options;
options.path = "~/.local/share/UltraNetMonitor/activity.db";   // or ":memory:"
options.retentionDays = 30;
NetworkMonitorStoreHandle store;
if (!NetworkMonitor_OpenStore(options, store)) { /* r.message says why */ }

// Once a second, whatever the app already snapshots:
NetworkMonitor_RecordSnapshot(store, connections);      // one transaction

// "What was firefox talking to in the last day?"
ActivityQuery query;
query.since = NetworkMonitor_Now() - 24 * 3600;
query.processName = "firefox";
std::vector<RecordedFlow> flows;
NetworkMonitor_QueryFlows(store, query, flows);         // newest first

NetworkMonitor_ApplyRetention(store);                   // on stop, on close
NetworkMonitor_CloseStore(store);
```

| Function | Does |
|---|---|
| `NetworkMonitor_StoreAvailable()` | Whether this build has UltraDatabase; without it every call below reports `NotSupported` |
| `NetworkMonitor_OpenStore` / `CloseStore` | Registers a named SQLite connection and migrates the schema (versioned; a newer file is refused, not damaged) |
| `NetworkMonitor_RecordSnapshot` | Every connection either extends the flow it continues or starts a new one; processes are deduplicated and cached |
| `NetworkMonitor_QueryFlows` | Filters: `since`, `until`, `pid`, `processName`, `text` (substring over addresses, name, executable), `includeListening`, `includeLoopback`, `limit`; newest first |
| `NetworkMonitor_QueryDailyTotals` | The rolled-up totals, newest day first |
| `NetworkMonitor_RecordDnsObservation` / `QueryDnsObservations` | Every observation a name source reported, one row per address, with the asking process where known; the same filters, on `observedAt` |
| `NetworkMonitor_RecordConnectionEvent` / `QueryConnectionEvents` / `ExportEventsCsv` | Every connection event a source reported, with its millisecond, process, name and counters; the same filters |
| `NetworkMonitor_RollUp(olderThan)` | Aggregates flows last seen before the time into daily totals and deletes them; a second roll-up onto the same day accumulates |
| `NetworkMonitor_ApplyRetention` | Rolls up flows older than the window, drops DNS observations, connection events and snapshot records older than the window, and daily totals older than twelve windows |
| `NetworkMonitor_Purge` | Deletes everything; irreversible, so the caller confirms |
| `NetworkMonitor_StoreStats` | Counts and the oldest / newest flow |
| `NetworkMonitor_ExportFlowsCsv` | RFC 4180 quoting, UTC ISO-8601 times, dot-decimal numbers |

**Continuation.** A connection is the same flow across snapshots while its
sightings are at most `kFlowContinuationSeconds` (120) apart; the same
5-tuple seen again later starts a new flow, so a reused ephemeral port is
not glued to an earlier conversation. `TIME_WAIT` and the like extend the
flow they belong to.

**Counters.** A flow carries its *latest* counters (the kernel's are
cumulative). A daily total sums only the flows that had counters and says
how many did (`countedFlows`), so a day with no counters reads as zero
counted flows, not as zero bytes.

**Names.** A flow keeps the best name it was seen with: an observed name
replaces a weak one, never the reverse, and a sighting without a name
keeps the one recorded. The daily total remembers the last name its flows
carried. The text filter and the CSV (`remote_name`, `name_source`)
include it. Events are one row each with their millisecond. Schema
version 3; a file written by an earlier version migrates in place on open.

**Threads.** One mutex per store: a recording thread, the name sources'
threads and a reading thread never share the single SQLite connection at
once. One transaction per snapshot, one per observation.

**What it does not do yet.** Encrypt at rest — the proposal's plan is
UltraCrypt with the key in UltraVault, and it is the next store increment.
Until then the file is as private as the directory it sits in, which is
why the app keeps it in the per-user data directory, never a cache.

## Privilege

Three tiers; the module degrades across them and says which it is in:

1. **Unprivileged** — every socket is listed, but only this user's processes
   can be attributed (`allUsers == false`). Other users' sockets appear with
   `ownerUid` set and `process` empty, and the capabilities' notes count the
   processes whose descriptors could not be read.
2. **Root** — every process is attributable.
3. **Kernel** — needed only to *block*, which this module does not do.

## Layout

```
UltraCanvas/include/NetworkMonitor/
    NetworkMonitor.h              public surface
    NetworkMonitorBackend.h       INetworkMonitorBackend + the native define (internal)
    NetworkMonitorAddress.h       address bytes -> text, shared by every backend (internal, pure)
    NetworkMonitorProcfs.h        the /proc/net parser (internal, pure)
    NetworkMonitorNames.h         INameSource, the registry, the name table, the built-in sources
    NetworkMonitorDns.h           the DNS wire format (internal, pure)
    NetworkMonitorEvents.h        IConnectionEventSource, the registry, the ring, the differ
    NetworkMonitorConntrack.h     the conntrack netlink messages (internal, pure)
UltraCanvas/core/NetworkMonitor/
    NetworkMonitorCore.cpp        NetworkMonitor_* functions, filters, names, roll-up, null backend
    NetworkMonitorAddress.cpp     RFC 5952 IPv6 text, IPv4-mapped in mixed notation
    NetworkMonitorProcfs.cpp      table parsing - no I/O, so it is tested on every platform
    NetworkMonitorStore.cpp       the activity store over UltraDatabase (stubs without it)
    NetworkMonitorNames.cpp       the name table, the registry, reverse DNS, the system resolver
    NetworkMonitorDns.cpp         message parsing and building, bounds-checked
    NetworkMonitorDnsProxy.cpp    the local DNS proxy source (BSD sockets, Winsock shims)
    NetworkMonitorEvents.cpp      the event registry, attribution, the snapshot differ
    NetworkMonitorConntrack.cpp   conntrack message parsing, bounds-checked
UltraCanvas/OS/Linux/UltraCanvasLinuxNetworkMonitor.cpp
                                  netlink sock_diag, the /proc/net fallback, the /proc walk
UltraCanvas/OS/Linux/UltraCanvasLinuxNetworkMonitorEvents.cpp
                                  nf_conntrack over NETLINK_NETFILTER as an event source
UltraCanvas/OS/MSWindows/UltraCanvasWindowsNetworkMonitor.cpp
                                  IP Helper tables, QueryFullProcessImageNameW, the token user
UltraCanvas/OS/MSWindows/UltraCanvasWindowsNetworkMonitorDns.cpp
                                  the DNS client's ETW events as a name source
UltraCanvas/OS/MSWindows/UltraCanvasWindowsNetworkMonitorEvents.cpp
                                  the kernel network ETW events as an event source
UltraCanvas/OS/MacOS/UltraCanvasMacOSNetworkMonitor.cpp
                                  libproc: PROC_PIDLISTFDS / PROC_PIDFDSOCKETINFO per process
Tests/NetworkMonitorTests.cpp     target NetworkMonitorTests (ctest)
Apps/UltraNetMonitor/             the application
```

The backend contract follows `IFolderWatchBackend`: an interface implemented
under `OS/<Platform>/`, a `CreateNativeNetworkMonitorBackend()` that the core
file defines as null wherever `ULTRACANVAS_NETWORKMONITOR_NATIVE` is not set.
Adding a platform is one source under `OS/<Platform>/`, one line in the
define's `#if` (`NetworkMonitorBackend.h`) and one in the source list
(`UltraCanvas/CMakeLists.txt`) — the two must agree.

### Linux: netlink first, the file second, the walk always

The table is asked for through `NETLINK_SOCK_DIAG` (`inet_diag`), one dump
per (family, protocol): a single round trip instead of a text file the
kernel renders in O(n²), and for TCP the socket's `tcp_info`, whose
`tcpi_bytes_acked` (what the peer acknowledged — it counts the SYN as one
byte) and `tcpi_bytes_received` fill `bytesSent` / `bytesReceived`. UDP
carries no counters; a `TIME_WAIT` socket has no `tcp_info`. Where the
kernel refuses a dump — a seccomp profile without netlink, a kernel without
`udp_diag` — that table is read from `/proc/net/*` instead, per table, and
the capabilities say so (`backendName` is `procfs+sock_diag` or `procfs`).

Neither source reports the PID: `sock_diag` gives the inode and UID, the file
the same. Attribution is the walk of every `/proc/<pid>/fd/*` for symlinks
reading `socket:[<inode>]`, which is exactly what `ss -p` does and why
`ss -p` is slower than `ss`. Process identities (exe, comm) are cached by
PID between snapshots and evicted when the PID is gone.

Addresses in `/proc/net` are printed in host byte order — `0100007F` is
127.0.0.1 — which the parser assumes little-endian, true of every Linux
target this framework builds for.

### Windows: the PID comes with the row

`GetExtendedTcpTable` / `GetExtendedUdpTable` with the `*_OWNER_PID` classes
return each socket's owning PID directly, for IPv4 and IPv6, so there is no
join to perform. `QueryFullProcessImageNameW` (through a
`PROCESS_QUERY_LIMITED_INFORMATION` handle) gives the executable and the
process token the user; a process the monitor may not open keeps its PID
and gets a `pid N` name, and is counted in the notes. `allUsers` is whether
the monitor's own token is elevated. Byte counters and events are the ETW
work of a later increment.

### macOS: sockets are found through their owners

There is no system-wide table. `proc_listpids` lists every process,
`proc_pidinfo(PROC_PIDLISTFDS)` its descriptors, and
`proc_pidfdinfo(PROC_PIDFDSOCKETINFO)` the socket behind each one, with
family, endpoints and TCP state — the `lsof -i` method. Attribution is
therefore inherent, and so is the limit: a process the monitor may not
inspect (another user's, when not root) contributes **no sockets at all**,
not even unattributed ones. The capabilities say so. No byte counters: the
public socket info carries none worth trusting, and `nettop`'s come from a
private framework.

## Tests

`Tests/NetworkMonitorTests.cpp` (`ctest -R NetworkMonitorTests`) covers the
shared address formatter and the parser against fixture text (IPv4, IPv6
with zero-run collapsing, IPv4-mapped peers, malformed fields), the state
codes, the roll-up including the byte-total rule, endpoint formatting and the
loopback test, and — where a backend exists — opens a loopback listener,
connects to it, moves 64 KiB across, and asserts both ends appear in the
snapshot attributed to the test's own PID, with counters at least that large
where the backend collects them and unset (not zero) where it does not; then
that the option filters remove what they should. The socket code compiles
on Winsock too. Where there is no backend, it asserts the module says so.
The store is tested on an in-memory database: continuation across
snapshots and its cut-off, every query filter, the CSV export, the
roll-up's accumulation onto an existing day, retention and purge. Names:
the wire format from fixture bytes (a CNAME chain, a compression pointer,
a pointer loop, a cut message), the table's precedence and lifetime rules
and its listener, the reverse DNS source's address filters, the proxy end
to end over UDP and TCP against a fake resolver on loopback, and the
store's names, observations, CSV, roll-up and the version-1 migration.
Events: the conntrack parser against a captured NEW and DESTROY, the
registry's ring, listener, naming and both-orientation attribution against
sockets the test opens, the differ reporting opened, accepted and closed
for a loopback connection attributed to the test's PID, the platform
source starting where it can, and the store's events.

## Not built yet

- Throughput charts, at-rest encryption of the store and file-transfer
  correlation — the rest of Phase 2 and Phase 3, in the proposal's §9
  order. eBPF connection events on Linux, should the tracker's caveats
  (root, an active tracker) prove too narrow.
- The name sources that need packet capture — port-53 capture and the TLS
  SNI reader — and any source for a browser resolving over HTTPS on its
  own; `NameSource` already names them.
- Byte counters on Windows (ETW) and macOS (no public source).
- Any blocking or TLS interception, by decision (proposal §7).

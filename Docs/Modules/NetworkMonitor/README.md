# NetworkMonitor — System-Wide Network Activity

**Status:** Phase 1 and the platform half of Phase 2 implemented (Linux, Windows, macOS); the rest of Phase 2 and Phase 3 in the proposal.
**Version:** 0.2.0
**Author:** UltraCanvas Framework / ULTRA OS
**Last Modified:** 2026-09-19

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
| Byte counters | ✅ `tcp_info` (TCP only) | later (ETW) | none — see below | — |
| Connection events | later (eBPF) | later (ETW) | none | — |
| Domain names | later | later | later | — |

Each backend reports what it cannot see in `NetworkMonitorCapabilities`:
`perConnectionBytes` is true only on Linux with netlink; `allUsers` is root
on Linux and macOS, an elevated token on Windows; and the `notes` count the
processes the last snapshot could not inspect.

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
| `NetworkConnection` | transport, address family, local/remote address and port, `state`, `ownerUid`, `socketInode`, `process` (optional), `bytesSent` / `bytesReceived` (optional, unset in Phase 1) |
| `ProcessIdentity` | `pid`, `executablePath`, `displayName` (the kernel's comm), `userName` |
| `NetworkConnectionState` | the TCP state machine, plus `Unconnected` for a UDP socket without a fixed peer |
| `NetworkMonitorCapabilities` | `socketTable`, `processAttribution`, `allUsers`, `connectionEvents`, `perConnectionBytes`, `dnsWithProcess`, `backendName`, `notes` |
| `NetworkMonitorOptions` | `includeListening`, `includeLoopback`, `resolveProcesses` |
| `ProcessTrafficSummary` | one process's counts, distinct `remoteAddresses`, byte totals present only when every connection reported them |
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
UltraCanvas/core/NetworkMonitor/
    NetworkMonitorCore.cpp        NetworkMonitor_* functions, filters, roll-up, null backend
    NetworkMonitorAddress.cpp     RFC 5952 IPv6 text, IPv4-mapped in mixed notation
    NetworkMonitorProcfs.cpp      table parsing - no I/O, so it is tested on every platform
UltraCanvas/OS/Linux/UltraCanvasLinuxNetworkMonitor.cpp
                                  netlink sock_diag, the /proc/net fallback, the /proc walk
UltraCanvas/OS/MSWindows/UltraCanvasWindowsNetworkMonitor.cpp
                                  IP Helper tables, QueryFullProcessImageNameW, the token user
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

## Not built yet

- Connection events (ETW on Windows, eBPF on Linux), domain names,
  persistence, throughput charts and file-transfer correlation — the rest of
  Phase 2 and Phase 3, in the proposal's §9 order.
- Byte counters on Windows (ETW) and macOS (no public source).
- Any blocking or TLS interception, by decision (proposal §7).

# NetworkMonitor — System-Wide Network Activity

**Status:** Phase 1 implemented (Linux); Phase 2 and 3 in the proposal.
**Version:** 0.1.0
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
| Socket table (TCP/UDP, v4/v6) | ✅ `/proc/net/{tcp,tcp6,udp,udp6}` | Phase 2 | Phase 2 | null backend |
| Process attribution | ✅ `/proc/<pid>/fd` → `socket:[inode]` | Phase 2 | Phase 2 | — |
| Owning user | ✅ from the table's UID | Phase 2 | Phase 2 | — |
| Byte counters | Phase 2 (`inet_diag`) | Phase 2 (ETW) | open question | — |
| Connection events | Phase 3 (eBPF) | Phase 2 (ETW) | none | — |
| Domain names | Phase 2 | Phase 2 | Phase 2 | — |

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
    NetworkMonitorProcfs.h        the /proc/net parser (internal, pure)
UltraCanvas/core/NetworkMonitor/
    NetworkMonitorCore.cpp        NetworkMonitor_* functions, filters, roll-up, null backend
    NetworkMonitorProcfs.cpp      table parsing - no I/O, so it is tested on every platform
UltraCanvas/OS/Linux/UltraCanvasLinuxNetworkMonitor.cpp
                                  the reads and the /proc walk
Tests/NetworkMonitorTests.cpp     target NetworkMonitorTests (ctest)
Apps/UltraNetMonitor/             the application
```

The backend contract follows `IFolderWatchBackend`: an interface implemented
under `OS/<Platform>/`, a `CreateNativeNetworkMonitorBackend()` that the core
file defines as null wherever `ULTRACANVAS_NETWORKMONITOR_NATIVE` is not set.
Adding a platform is one source under `OS/<Platform>/`, one line in the
define's `#if` (`NetworkMonitorBackend.h`) and one in the source list
(`UltraCanvas/CMakeLists.txt`) — the two must agree.

### How the Linux backend attributes

`/proc/net/tcp` gives each socket's inode; `/proc/<pid>/fd/*` are symlinks
reading `socket:[<inode>]`. One walk of every process's descriptors builds
inode → PID, which is exactly what `ss -p` does and why `ss -p` is slower than
`ss`. Netlink `sock_diag` would replace the table *read* (and bring byte
counters, Phase 2) but not the walk: it reports the inode and UID, not the
PID. Process identities (exe, comm) are cached by PID between snapshots and
evicted when the PID is gone.

Addresses in `/proc/net` are printed in host byte order — `0100007F` is
127.0.0.1 — which the parser assumes little-endian, true of every Linux
target this framework builds for.

## Tests

`Tests/NetworkMonitorTests.cpp` (`ctest -R NetworkMonitorTests`) covers the
parser against fixture text (IPv4, IPv6 with zero-run collapsing, IPv4-mapped
peers, malformed fields), the state codes, the roll-up including the
byte-total rule, endpoint formatting and the loopback test, and — where a
backend exists — opens a loopback listener and asserts it appears in the
snapshot attributed to the test's own PID, then that both filters remove it.
Where there is no backend, it asserts the module says so.

## Not in Phase 1

- Byte counters, connection events, domain names, file-transfer correlation
  and persistence — see the proposal's §9 for the order.
- Windows and macOS backends.
- Any blocking or TLS interception, by decision (proposal §7).

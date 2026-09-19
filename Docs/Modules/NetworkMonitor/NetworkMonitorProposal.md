# NetworkMonitor — System-Wide Internet Activity Monitoring

**Status:** Proposal — nothing implemented. This document is the feasibility
study and the architecture; no code, no registry entry in
[`Masterfile_modules.md`](../../../Masterfile_modules.md) until Phase 1 lands.
**Author:** UltraCanvas Framework / ULTRA OS
**Last Modified:** 2026-09-19

The question this document answers: *can we build an internet activity monitor
that records which connections and domains a machine reaches, what was
downloaded or uploaded, and which application did it?*

**Short answer: three of those four, yes, on all three desktop platforms,
without shipping a kernel driver. The fourth — the actual files crossing the
wire — is blocked by TLS and cannot be read from the network without becoming
a man-in-the-middle. This proposal declines to do that, and recovers most of
the value from the filesystem instead.**

| Signal | Feasible? | How |
|---|---|---|
| Connections (5-tuple, state, lifetime) | **Yes** | OS socket tables, unprivileged for own processes |
| Which app opened it | **Yes** | socket → PID → executable, all three platforms |
| Bytes up / down per app | **Yes** (macOS caveat, §3.3) | counters on the same APIs |
| Domain names | **Yes** | four possible sources, §2.3 — pick per platform |
| Which *file* was downloaded | **Partly** — not from the wire | filesystem watch + OS provenance metadata, §2.4 |
| Which *file* was uploaded | **Weakly** | byte volume + read correlation only, §2.4.3 |
| URLs and payload contents | **No**, by decision | would require TLS interception, §7.1 |

---

## 1. What this module is, and what it is not

**It is not part of UltraNet.** That distinction matters enough to state
first, because it is the natural wrong assumption. UltraNet is a *client*
library: it makes requests on behalf of the process it is linked into, and it
can only ever see its own traffic. A system-wide monitor observes *other
processes'* traffic, which is an operating-system question, not a networking
one. NetworkMonitor therefore sits beside UltraNet as a sibling module with no
dependency on it, and reads the same OS interfaces that `ss`, `netstat`,
`lsof` and `nettop` read.

There is precedent for the shape in the framework already: the hardware
module, `UltraCanvasHardwareInfo`, is a shared data model with one probing
backend per platform under `UltraCanvas/OS/<Platform>/`, and it already
enumerates network interfaces with byte counters
(`ListNetworkInterfaces()` → `NetworkInterfaceInfo`). NetworkMonitor is the
same idea one level down: per *connection* and per *process* instead of per
interface.

Scope boundary: this module **observes and records**. It does not block, filter
or modify traffic. Blocking is a different product with a much higher cost
(§7.2) and is deliberately out of scope for Phases 1–3.

---

## 2. The four questions

### 2.1 Connections, and which application opened them

Every desktop platform exposes its socket table to userspace, with the owning
process attached or derivable. No driver, no packet capture.

**Linux.** `/proc/net/tcp`, `/proc/net/tcp6`, `/proc/net/udp`, `/proc/net/udp6`
give the local and remote address:port, connection state, owning UID and the
socket's **inode**. The inode is the join key: walking `/proc/<pid>/fd/` and
resolving the symlinks that read `socket:[<inode>]` maps each socket to a PID,
and from there to `/proc/<pid>/exe`, `comm` and `cmdline`.

The faster form of the first half is netlink `NETLINK_SOCK_DIAG` with
`inet_diag`, which returns the whole table — including per-socket `tcp_info`
counters — in one round trip instead of parsing a text file that is O(n²) to
produce for large tables. **It does not remove the `/proc` walk**: sock_diag
reports the inode and UID but not the PID, so the fd scan is still required
for process attribution. This is exactly what `ss -p` does, and it is why
`ss -p` is noticeably slower than plain `ss`.

**Windows.** `GetExtendedTcpTable` and `GetExtendedUdpTable` with
`TCP_TABLE_OWNER_PID_ALL` / `UDP_TABLE_OWNER_PID` return the 5-tuple *and* the
owning PID directly — no second lookup. `QueryFullProcessImageName` turns the
PID into a path.

**macOS.** `libproc`: `proc_listpids` for the process set, then
`proc_pidinfo(pid, PROC_PIDLISTFDS, …)` for each process's descriptors and
`proc_pidfdinfo(pid, fd, PROC_PIDFDSOCKETINFO, …)` for the ones that are
sockets. The attribution is free because the enumeration is already per-PID.
`proc_pidpath` gives the executable. This is the `lsof` approach.

### 2.2 Polling versus events

Snapshotting the socket table at 1 Hz is cheap and portable, and it is what
Phase 1 should do. It has one real flaw: **short-lived connections are missed
entirely.** A DNS lookup, a tracking beacon or an API call can open, transfer
and close inside a single polling interval, and those are frequently the
connections a user most wants to see.

Event sources that close the gap, all requiring elevation:

- **Linux — eBPF.** Either a `cgroup/connect4`/`connect6` hook
  (`BPF_CGROUP_INET4_CONNECT`, attached to the root cgroup v2) or kprobes on
  `tcp_connect` / `inet_csk_accept`. Gives the connection at the moment of
  syscall, with PID and comm already in context. Needs `CAP_BPF` +
  `CAP_NET_ADMIN` (or root) and, for CO-RE portability, a kernel built with
  BTF. **Distro portability is the risk here** — this is why polling stays as
  the baseline that always works rather than being replaced.
- **Windows — ETW.** The `Microsoft-Windows-Kernel-Network` provider emits
  TCP/UDP connect, disconnect, send and receive events with PID and byte
  counts, in real time, via a real-time trace session. Requires
  administrator. No driver to write, sign or maintain — a significant
  advantage over the WFP route.
- **macOS — none without an entitlement.** There is no unprivileged event
  stream. Short-lived connections are simply missed unless the product ships a
  Network Extension (§7.2), which is a much larger commitment. Accept the gap
  on macOS in Phases 1–2.

### 2.3 Domain names

Sockets carry IP addresses, not names. Resolving `104.18.x.x` back to
"the site the user actually visited" needs a separate source, and there are
four, with materially different properties:

| Source | Privilege | Survives DoH/DoT? | Gives PID? | Notes |
|---|---|---|---|---|
| **ETW `Microsoft-Windows-DNS-Client`** (Windows) | Admin | Yes — it hooks the resolver, not the wire | **Yes** | Cleanest option on any platform. No packet capture at all |
| **Local DNS proxy** on `127.0.0.1:53`, system resolver repointed | Setup only | Yes for clients that use the system resolver | Source address only | Cross-platform; logs every query; survives restarts once configured |
| **Passive capture of port 53** | root / `CAP_NET_RAW` / Npcap / BPF device | **No** | No | Traditional, and the most fragile of the four |
| **TLS SNI from the ClientHello** | same as above | Yes (it is a different protocol) | No | Works even when DNS was encrypted — until ECH, below |

Two trends are actively eroding the packet-sniffing options and should shape
the design:

- **DoH/DoT.** Browsers increasingly resolve over HTTPS to their own resolver.
  Those queries never touch port 53 and never touch the system resolver, so
  passive capture *and* the local proxy both go blind. The ETW DNS provider
  also misses them, because the browser is not calling the Windows resolver.
  For such clients, SNI is the remaining name source.
- **Encrypted ClientHello.** ECH encrypts the SNI and, as deployment grows,
  will remove the last plaintext hostname on the wire. There is no
  observation-based replacement; what remains is the local-proxy route and
  per-application integration.

**Design consequence: the name source must be a plug-in point, not a
hard-wired collector.** Several may run at once, and each recorded name gets a
provenance tag (`DnsProxy`, `EtwDnsClient`, `PacketCapture`, `Sni`,
`ReverseDns`, `Inferred`) so the UI can distinguish "we saw this query" from
"we guessed". Reverse DNS and ASN/organisation lookup are the last-resort
fallbacks and are near-useless behind CDNs — everything resolves to
"Cloudflare" — so they are labelled as weak, never presented as fact.

### 2.4 Files downloaded and uploaded — the TLS wall

Effectively all interesting traffic is TLS. **Filenames, URLs, headers and
payloads are not visible on the wire**, and no amount of cleverness in a
passive observer changes that. The only way to read them is to terminate TLS —
install a private CA in the system trust store and proxy everything through
it. §7.1 explains why this proposal rejects that.

What follows recovers most of the practical value without decrypting anything.

#### 2.4.1 Watch the filesystem, not the wire

A file arriving on disk is observable, cheaply and unprivileged:

- **Linux** — inotify. Already wrapped in the framework as
  `UltraCanvasFolderWatcher` (`UltraCanvas/include/UltraCanvasFolderWatcher.h`,
  backend under `OS/Linux/UltraCanvasLinuxFolderWatcher.cpp`).
- **Windows** — `ReadDirectoryChangesW`, same wrapper.
- **macOS** — FSEvents, same wrapper.

**One gap to close first:** the existing watcher's callback is coalesced — it
signals "something in this folder changed" with no per-file detail, which suits
a file manager refreshing a view but not an audit log. Phase 2 either diffs a
directory listing on each signal (simple, adequate for a handful of download
folders, misses same-interval churn) or extends `IFolderWatchBackend` with an
event-detail variant. The second is the better answer and benefits the file
manager too; it should be proposed as its own change rather than smuggled in
here.

#### 2.4.2 The OS already records where downloads came from

This is the part that is easy to miss, and it gives the *actual source URL* for
free — no decryption, no correlation, no guessing:

| Platform | Where | Contents |
|---|---|---|
| Windows | NTFS alternate data stream `Zone.Identifier` | `[ZoneTransfer]` with `ZoneId`, `HostUrl`, `ReferrerUrl` |
| macOS | xattr `com.apple.metadata:kMDItemWhereFroms` | binary plist array of source URLs |
| Linux | xattr `user.xdg.origin.url`, `user.xdg.referrer.url` | freedesktop convention; Firefox and Chromium write it, `curl`/`wget` do not |

Coverage is browser-centric and not universal, but where it exists it is
authoritative in a way that no inference can match.

#### 2.4.3 Correlation, and being honest about it

Joining the two halves — "a 14 MB file appeared in `~/Downloads` at 10:04:31,
created by PID 2214" and "PID 2214 had an active flow to
`cdn.example.com` that transferred 14 MB between 10:04:12 and 10:04:31" —
produces the sentence a user actually wants: *Firefox downloaded report.pdf
from cdn.example.com.*

That join is an **inference**, and the module must label it as one. The record
carries a confidence and the evidence behind it (byte-count agreement within
tolerance, time-window overlap, same PID), so the UI can show a firm claim
when provenance metadata confirms it and a hedged one when only correlation
supports it. Silently presenting a guess as a fact is the failure mode to
design against.

**Uploads have no equivalent.** There is no provenance metadata for a file
leaving the machine. The honest ceiling is "application X sent 40 MB to
host Y", optionally sharpened by watching which files that process read in the
same window — which needs fanotify or auditd on Linux and ETW file I/O on
Windows, is noisy, and is expensive. Phase 3 at the earliest; report the byte
volume plainly until then rather than inventing a filename.

#### 2.4.4 Two cheap wins worth taking

- **Browser download history.** Browsers keep their own download records in
  SQLite, with URL, filename, size and timestamp already joined. UltraDatabase
  bundles SQLite, so reading those (read-only, on a copy, to avoid lock
  contention with a running browser) is a short path to high-quality data for
  the dominant download source.
- **Explicit proxy mode.** Configured as the system HTTP(S) proxy, the module
  sees `CONNECT` target hostnames and per-request byte counts for every
  well-behaved client — SNI-grade information without touching a raw socket,
  and without decrypting anything. Apps that ignore system proxy settings are
  simply not covered.

---

## 3. Platform capability summary

### 3.1 Linux

| Signal | Mechanism | Privilege |
|---|---|---|
| Socket table | `/proc/net/*`, or netlink `sock_diag`/`inet_diag` | own processes free; all processes root |
| Socket → PID | `/proc/<pid>/fd` symlink scan | same |
| Per-socket bytes | `inet_diag` `tcp_info` | same |
| Connection events | eBPF `cgroup/connect4`, or kprobe `tcp_connect` | `CAP_BPF` + `CAP_NET_ADMIN` |
| DNS | local proxy, or port-53 capture (`CAP_NET_RAW`) | proxy: none after setup |
| File arrival | inotify via `UltraCanvasFolderWatcher` | none |
| Download provenance | `user.xdg.origin.url` xattr | none |

### 3.2 Windows

| Signal | Mechanism | Privilege |
|---|---|---|
| Socket table + PID | `GetExtendedTcpTable` / `GetExtendedUdpTable` | none for the table; process paths may need elevation |
| Connection events + bytes | ETW `Microsoft-Windows-Kernel-Network` | Administrator |
| DNS with PID | ETW `Microsoft-Windows-DNS-Client` | Administrator |
| File arrival | `ReadDirectoryChangesW` via `UltraCanvasFolderWatcher` | none |
| Download provenance | `Zone.Identifier` ADS | none |

Windows is the strongest platform here: ETW supplies connection events, byte
counts and DNS-with-PID from documented providers, with no driver.

### 3.3 macOS

| Signal | Mechanism | Privilege |
|---|---|---|
| Socket table + PID | `libproc` `PROC_PIDLISTFDS` / `PROC_PIDFDSOCKETINFO` | own processes free; all processes root |
| Per-socket bytes | **open question** — see below | — |
| Connection events | none without a System Extension | — |
| DNS | local proxy | none after setup |
| File arrival | FSEvents via `UltraCanvasFolderWatcher` | none |
| Download provenance | `kMDItemWhereFroms` xattr | none |

**The macOS byte-counter question must be settled before Phase 2 is
scheduled.** `PROC_PIDFDSOCKETINFO` exposes socket state richly but its
per-socket traffic counters are incomplete; `nettop` gets its per-process
figures from the private `NetworkStatistics.framework`, which is not
acceptable in a shipped product. The likely outcome is that macOS reports
per-*interface* totals (already available via `UltraCanvasHardwareInfo`) plus
per-process connection counts, and defers per-process byte attribution to the
Network Extension route. Spike this early; do not design the UI assuming the
number exists everywhere.

---

## 4. Privilege model

Three tiers, and the product should degrade cleanly across them rather than
demanding the top one:

1. **Unprivileged.** Own processes' connections, all file-arrival events and
   all provenance metadata. Genuinely useful on a single-user desktop, and the
   right default on first run.
2. **Elevated (root / Administrator).** Every process's connections, event-rate
   collection, packet capture if used. This is the normal operating mode and
   the one the UI should guide users toward, with an explicit consent step.
3. **Kernel / system extension.** Required only for *blocking*. Out of scope,
   §7.2.

The collector must therefore report *what it could not see*, not silently
under-report. A connection table that omits other users' processes because the
daemon is unprivileged looks identical to a quiet machine, and that is a
correctness bug in a monitoring product. `NetworkMonitorCapabilities` (§5.4)
makes the difference explicit and the UI surfaces it.

---

## 5. Architecture

### 5.1 Layout

Following the module conventions in [AGENTS.md](../../../AGENTS.md) —
platform code only under `OS/<Platform>/`, shared logic in `core/`, backing
libraries fully wrapped:

```
UltraCanvas/include/NetworkMonitor/
    NetworkMonitor.h              public surface: types + NetworkMonitor_* functions
    NetworkMonitorBackend.h       INetworkMonitorBackend (not public)
UltraCanvas/core/NetworkMonitor/
    NetworkMonitorCore.cpp        session lifetime, capability negotiation
    NetworkMonitorCorrelator.cpp  DNS <-> connection <-> file-event joining
    NetworkMonitorStore.cpp       persistence over UltraDatabase
    NetworkMonitorNameSources.cpp name-source registry and provenance
UltraCanvas/OS/Linux/UltraCanvasLinuxNetworkMonitor.cpp
UltraCanvas/OS/MSWindows/UltraCanvasWindowsNetworkMonitor.cpp
UltraCanvas/OS/MacOS/UltraCanvasMacOSNetworkMonitor.mm
Apps/UltraNetMonitor/            the UI application
Tests/NetworkMonitorTests.cpp
```

`Docs/Modules/NetworkMonitor/README.md` joins this file when Phase 1 ships;
this proposal then stays as the research record, matching how the packet
diagram work is split between proposal and reference.

### 5.2 Backend interface

Mirrors the `IFolderWatchBackend` pattern already in the framework — an
interface implemented per platform, a `CreateNative…` factory that returns null
where no backend exists, and a public class the callers actually use:

```cpp
namespace UltraCanvas {

    // Implemented per platform under OS/<Platform>/. Not public surface.
    class INetworkMonitorBackend {
    public:
        virtual ~INetworkMonitorBackend() = default;

        // What this backend can actually deliver on this machine, at the
        // current privilege level. Cheap; may be called before Start().
        virtual NetworkMonitorCapabilities Capabilities() const = 0;

        // One snapshot of the socket table. Always available where the
        // backend exists at all - this is the portable baseline.
        virtual NetworkMonitorResult Snapshot(
            std::vector<NetworkConnection>& out) = 0;

        // Begin event-rate collection where Capabilities().connectionEvents
        // is set. Callbacks run on the backend's own thread. Returns a
        // failure result, not false, so the caller can report *why* (missing
        // privilege reads differently from an unsupported kernel).
        virtual NetworkMonitorResult StartEvents(
            std::function<void(const NetworkConnectionEvent&)> onConnection,
            std::function<void(const DnsObservation&)> onDns,
            std::function<void()> onFailed) = 0;

        // Stop and join. Safe twice, safe if StartEvents() failed. No
        // callback runs after this returns.
        virtual void StopEvents() = 0;
    };

    std::unique_ptr<INetworkMonitorBackend> CreateNativeNetworkMonitorBackend();
}
```

### 5.3 Data model

```cpp
enum class NetworkTransport { Tcp, Udp, Icmp, Other };
enum class NetworkConnectionState {
    Unknown, Listening, SynSent, Established, CloseWait, TimeWait, Closed
};
enum class NameSource {
    None, DnsProxy, EtwDnsClient, PacketCapture, Sni, ReverseDns, Inferred
};

struct ProcessIdentity {
    uint32_t    pid = 0;
    std::string executablePath;      // "/usr/lib/firefox/firefox"
    std::string displayName;         // "Firefox"
    std::string userName;            // owning user
    std::optional<std::string> signingIdentity;  // where the OS provides one
};

struct NetworkConnection {
    NetworkTransport       transport = NetworkTransport::Tcp;
    std::string            localAddress;
    uint16_t               localPort = 0;
    std::string            remoteAddress;
    uint16_t               remotePort = 0;
    NetworkConnectionState state = NetworkConnectionState::Unknown;

    std::optional<ProcessIdentity> process;   // empty when not attributable
    std::string                    remoteName;    // resolved host, may be empty
    NameSource                     nameSource = NameSource::None;

    std::optional<uint64_t> bytesSent;        // absent != zero
    std::optional<uint64_t> bytesReceived;
    std::chrono::system_clock::time_point firstSeen, lastSeen;
};

struct DnsObservation {
    std::string queryName;
    std::vector<std::string> addresses;
    std::optional<ProcessIdentity> process;   // only ETW and eBPF supply this
    NameSource source = NameSource::None;
    std::chrono::system_clock::time_point observedAt;
};

enum class TransferDirection { Download, Upload };
enum class TransferEvidence  { Provenance, BrowserHistory, Correlated, VolumeOnly };

struct FileTransferRecord {
    TransferDirection direction = TransferDirection::Download;
    std::string       filePath;
    uint64_t          sizeBytes = 0;
    std::string       sourceUrl;        // from provenance metadata, may be empty
    std::string       remoteName;       // correlated host, may be empty
    std::optional<ProcessIdentity> process;
    TransferEvidence  evidence = TransferEvidence::VolumeOnly;
    int               confidencePercent = 0;   // 100 only for Provenance
    std::chrono::system_clock::time_point observedAt;
};
```

`std::optional` throughout for the same reason `UltraCanvasHardwareInfo` uses
it: "0 bytes" and "not reported" are different facts, and conflating them is
how a monitor lies.

### 5.4 Public surface

Free functions with a module prefix and a result type from every blocking
call, consistent with `UltraNet_*`/`UltraNetResult` and `UltraDb_*`/
`UltraDbResult`:

```cpp
struct NetworkMonitorCapabilities {
    bool socketTable        = false;
    bool processAttribution = false;  // false when unprivileged and not own-process
    bool allUsers           = false;  // false = only this user's processes
    bool connectionEvents   = false;  // event-rate, not just polling
    bool perConnectionBytes = false;  // the macOS open question, S3.3
    bool dnsWithProcess     = false;
    std::string backendName;          // "procfs+netlink", "ETW", "libproc"
};

NetworkMonitorCapabilities NetworkMonitor_GetCapabilities();
bool                       NetworkMonitor_IsAvailable();

NetworkMonitorResult NetworkMonitor_ListConnections(std::vector<NetworkConnection>& out);
NetworkMonitorResult NetworkMonitor_ListByProcess(std::vector<ProcessTrafficSummary>& out);

NetworkMonitorHandle NetworkMonitor_StartSession(const NetworkMonitorOptions& options);
NetworkMonitorResult NetworkMonitor_StopSession(NetworkMonitorHandle session);

NetworkMonitorResult NetworkMonitor_QueryConnections(const ActivityQuery& query,
                                                     std::vector<NetworkConnection>& out);
NetworkMonitorResult NetworkMonitor_QueryTransfers(const ActivityQuery& query,
                                                   std::vector<FileTransferRecord>& out);

NetworkMonitorResult NetworkMonitor_RegisterNameSource(std::unique_ptr<INameSource> source);
NetworkMonitorResult NetworkMonitor_ExportRange(const ActivityQuery& query,
                                                const std::string& path,
                                                ActivityExportFormat format);
```

### 5.5 Storage

UltraDatabase, SQLite driver, parameter binding only — never string-built SQL,
per the module rules, and doubly so here because hostnames and process command
lines are attacker-influenced text.

Four tables: `connections`, `dns_observations`, `file_transfers`, `processes`
(deduplicated, referenced by the others). Volume is the design constraint —
a busy desktop can open tens of thousands of connections a day — so:

- write through a batching queue, not one INSERT per event;
- roll up connections older than a configurable window into per-process,
  per-host, per-day aggregates and drop the individual rows;
- default retention 30 days, user-configurable including "session only,
  nothing on disk";
- offer at-rest encryption of the store via UltraCrypt, with the key held in
  UltraVault. **This database is a detailed record of a person's browsing —
  it is more sensitive than most of what it observes**, and it must not be the
  weakest link in the system.

### 5.6 Correlation engine

Runs in `core/`, entirely platform-independent, and therefore the part most
worth unit-testing hard: it takes recorded event streams as input and emits
`FileTransferRecord`s, so `Tests/NetworkMonitorTests.cpp` can drive it from
fixtures with no network and no privileges. Rules, in confidence order:

1. Provenance metadata present → `TransferEvidence::Provenance`, confidence 100,
   URL taken verbatim.
2. Browser history match on path and size → `BrowserHistory`, confidence ~95.
3. Correlated: same PID, transfer window overlapping the file's creation, byte
   totals agreeing within tolerance → `Correlated`, confidence scaled by how
   many candidate hosts the PID was talking to at the time. **One candidate
   only should produce high confidence; five should produce low.**
4. Otherwise `VolumeOnly` — report the bytes and the host, no filename.

---

## 6. The application

`Apps/UltraNetMonitor/`, built on `UltraCanvasApplication` following
`Apps/Texter/main.cpp`, alongside UltraCleaner and UltraFiler. Its own
changelog at `Docs/UltraNetMonitor/CHANGELOG.md` per the versioning rules.

Built from existing elements — nothing hand-painted, per the UI reuse rule.
Check `Docs/UltraCanvas/UltraCanvasUIElements.md` before adding anything:

| View | Element |
|---|---|
| Live connection table | `UltraCanvasListView` — multi-column, virtualised, model-driven (`IListModel` + `ListColumnDef`), which matters at tens of thousands of rows |
| Sorting and filtering | `UltraCanvasListSortFilterProxy`, with `SetShowHeader(true)` for sort indicators |
| App → host → connection hierarchy | `UltraCanvasColumnsTreeView` |
| Throughput over time | `UltraCanvasLineChartElement` / `UltraCanvasAreaChartElement` |
| Live rate indicator | `UltraCanvasLevelMeter` is the closest existing element, but its API is audio-shaped (`SetLevel(peak, rms)`) — check the fit before committing to it; a narrow `UltraCanvasLineChartElement` in sparkline form may suit a byte-rate better |
| Per-app share of traffic | one of the circular charts, or `UltraCanvasSankeyDiagram` for app → host flow |

Note that `UltraCanvasTableView` does **not** exist — `UltraCanvasListView` is
the multi-column table. Related: the packet-diagram work explicitly rejected
coupling its elements to live UltraNet data
(`Docs/UltraCanvas/UltraCanvasPacketDiagramProposal.md` §12.6), and that
decision holds here — if packet-level display is ever wanted, it consumes
recorded data through the same model, not a live socket.

---

## 7. Deliberate non-goals

### 7.1 TLS interception

Reading URLs, headers and payloads requires installing a private CA in the
system trust store and proxying all traffic through a local MITM, the
mitmproxy model. It works. This proposal rejects it as the product's
foundation for three reasons:

1. **It breaks things, visibly.** Certificate pinning is standard in updaters,
   banking applications, messaging clients and most mobile-derived software.
   Those connections fail rather than degrade, and the monitor gets blamed.
2. **The CA private key becomes the most valuable target on the machine.** A
   monitoring tool that weakens the trust store has made the system less safe
   in exchange for visibility — a poor trade for the stated use case.
3. **§2.4 gets most of the value without it.** Provenance metadata gives the
   real URL for browser downloads; correlation covers most of the rest.

If a specific need for it emerges later — a developer's own HTTP debugging, a
lab machine — it belongs behind an explicit, per-session, loudly-labelled
opt-in that the user turns on for a defined window, never a background default
and never a first-run prompt.

### 7.2 Blocking

Observing and blocking differ by an order of magnitude in cost. Blocking
requires a WFP kernel-mode callout driver on Windows (written, signed,
maintained against kernel changes), NFQUEUE or eBPF on Linux, and on macOS a
`NEFilterDataProvider` System Extension — which needs the Network Extension
entitlement granted by Apple on request, plus notarization. The notarization
half of that pipeline already exists here
(`package_and_notarize-macos.sh`), but the entitlement does not, and it is an
approval process, not a build step. Treat blocking as a separate product
decision after the observer has shipped and proved useful.

---

## 8. Legal and privacy posture

On the owner's own machine this is ordinary system tooling of the same kind as
`ss`, Activity Monitor or Resource Monitor. The same binary deployed on other
people's machines is surveillance software, and that is a regulated activity:
wiretap and interception statutes, workplace-monitoring consent and disclosure
rules, and GDPR obligations where the machine's user is an employee or a
customer. The TLS-interception variant rejected in §7.1 is squarely in that
territory.

This is an architecture constraint, not a disclaimer to add at the end:

- **First run requires explicit, informed consent**, naming what is recorded
  and for how long — not a licence-agreement checkbox.
- **Recording is visibly on.** A monitor that can be hidden from the person
  being monitored is a different product; do not build the affordance.
- **Retention is user-controlled and defaults short** (§5.5), with one-click
  purge and export.
- **No network egress of recorded data, ever, in any phase.** The store is
  local. If remote collection is ever requested, it is a new proposal with its
  own review, not a configuration flag added to this one.
- Deployment intent should be settled **before** the architecture hardens:
  single-user desktop tool and managed-fleet agent are different products with
  different obligations, and retrofitting the second onto the first is how
  consent gets skipped.

---

## 9. Delivery

**Phase 1 — see the connections.** Linux `procfs`/`sock_diag` backend with
PID attribution, polling only, the shared data model, the capability
negotiation, in-memory storage, and `Apps/UltraNetMonitor` with the live
connection list and per-app rollup. Ships the whole vertical slice on one
platform, which is what makes the model honest.

**Phase 2 — names, history and the other platforms.** Windows
(`GetExtendedTcpTable` + ETW for events and DNS) and macOS (`libproc`, with the
byte-counter question resolved per §3.3); the name-source registry with the
local DNS proxy and SNI reader; UltraDatabase persistence with retention and
rollup; the throughput charts.

**Phase 3 — transfers.** Per-file folder watching (with the
`IFolderWatchBackend` detail extension proposed separately), provenance-metadata
readers for all three platforms, browser-history import, the correlation
engine and the transfers view. eBPF connection events on Linux where
available.

**Later, if warranted.** Upload attribution via fanotify/ETW file I/O; explicit
proxy mode; blocking, as its own proposal.

---

## 10. Open questions

1. **macOS per-process byte counters** (§3.3) — is there a supported API, or
   does macOS ship without that number until a Network Extension exists? Spike
   before Phase 2 is scheduled; it changes what the UI can promise.
2. **Daemon or in-process?** Elevated collection argues for a small privileged
   helper with an unprivileged UI over it. That is more moving parts and a new
   IPC surface, but it avoids running the whole GUI as root, which is
   unacceptable. Recommend the split helper, decided at the start of Phase 2 —
   retrofitting it later means rewriting the collector's boundary.
3. **eBPF distribution.** Vendoring libbpf and shipping CO-RE objects, versus
   requiring a distro package, versus skipping eBPF and accepting the
   short-connection gap on Linux as macOS already does.
4. **Module name.** `NetworkMonitor` reads clearly and avoids implying an
   UltraNet dependency (§1), but every other observability-adjacent sibling
   here carries the `Ultra` prefix. Settle before the registry entry is
   written, since renaming after that touches the public surface.

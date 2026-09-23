// Apps/UltraNetMonitor/main.cpp
// UltraNetMonitor - shows which processes hold which network connections,
// live, from the operating system's socket table (the NetworkMonitor
// module), and records them over time into an activity store. It observes;
// it never blocks or modifies traffic.
//
// Two ways in. Without arguments it opens the UltraCanvas window. With
// --list / --by-app / --names / --capabilities it prints one snapshot and
// exits, --events prints connection events as they come; with --record it
// writes snapshots, DNS observations and events to the store until
// stopped, and --history / --dns / --events-history / --totals /
// --store-stats read the store back, --purge empties it. All of that runs
// headless, which is what makes it usable over ssh and checkable in CI.
//
// Domain names come from the name sources started here in every mode:
// reverse DNS unless --no-rdns, the platform's own resolver events where
// it has any (Windows, elevated), and the local DNS proxy with --dns-proxy.
// Connection events come from the event sources: the snapshot differ
// unless --no-diff, and the platform's own (nf_conntrack as root on
// Linux, the kernel network ETW provider elevated on Windows).
// Version: 0.6.0
// Author: UltraCanvas Framework / ULTRA OS

// Before the window header: on Linux that one reaches X11, whose `None`
// macro would otherwise break HardwareQuery::None in this header.
#include "UltraCanvasHardwareInfo.h"

#include "ui/UltraNetMonitorPaths.h"
#include "ui/UltraNetMonitorWindow.h"

#include "NetworkMonitor/NetworkMonitor.h"
#include "NetworkMonitor/NetworkMonitorEvents.h"
#include "NetworkMonitor/NetworkMonitorNames.h"
#include "NetworkMonitor/NetworkMonitorStore.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasUtils.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#include <X11/Xlib.h>
#endif

// ULTRANETMONITOR_VERSION comes from the build alone: CMake reads the first line
// of Docs/UltraNetMonitor/CHANGELOG.md (cmake/UltraCanvasVersion.cmake) and passes it
// as a compile definition. No fallback here, so a build that lost it
// fails instead of reporting a wrong number.
#ifndef ULTRANETMONITOR_VERSION
#error "ULTRANETMONITOR_VERSION is not defined: build through CMake, which reads it from Docs/UltraNetMonitor/CHANGELOG.md"
#endif

using namespace UltraCanvas;

namespace {

std::atomic<bool> g_stopRecording{false};

// Ctrl-C and SIGTERM. Two flag stores, nothing else: a handler may not
// log, lock or exit. In the window, the framework's flag makes the next
// loop iteration request the exit on the main thread, main returns, and
// the window's destructor stops the worker, the name and event sources
// and the store before any static destructor runs. Headless, the
// recording or event loop finishes its round, applies retention and
// closes cleanly.
void SignalHandler(int) {
    UltraCanvasApplicationBase::RequestExitFromSignal();
    g_stopRecording = true;
}

void PrintUsage(const char* programName) {
    std::printf(
        "UltraNetMonitor - which processes hold which network connections\n"
        "Powered by the UltraCanvas framework\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "  (no options)        Open the UltraNetMonitor window\n"
        "  --list              Print every connection with its process and exit\n"
        "  --by-app            Print the per-process roll-up and exit\n"
        "  --capabilities      Print what this machine's backend can deliver\n"
        "  --names             Print every address the name sources have named\n"
        "  --events            Print connection events as they happen, until Ctrl-C\n"
        "                      (or --seconds)\n"
        "  --no-listen         With --list / --by-app / --history: leave out listeners\n"
        "  --no-loopback       With --list / --by-app / --history: leave out loopback\n"
        "  --resolve           With --list / --by-app / --names: wait up to 3 s for\n"
        "                      reverse DNS before printing\n"
        "  --csv <file>        With --list / --by-app: write the snapshot as CSV instead\n"
        "                      of printing it (the window's Export menu does the same)\n"
        "\n"
        "  --dns-proxy [<port>]\n"
        "                      Run the local DNS proxy on 127.0.0.1:<port> (default 53,\n"
        "                      which needs privilege) and learn names from the queries\n"
        "                      that pass through it; point the system resolver at it\n"
        "      --upstream <ip> The resolver to forward to (default: the system's)\n"
        "  --no-rdns           Do not look up names by reverse DNS\n"
        "  --no-diff           Do not diff snapshots for events (only the platform's own)\n"
        "  --diff-interval <ms>\n"
        "                      The differ's interval (default 250); shorter connections\n"
        "                      are missed by it, never by the platform's own source\n"
        "\n"
        "  --record [<db>]     Record a snapshot every --interval into the store\n"
        "                      until Ctrl-C (or --seconds), then apply retention\n"
        "      --seconds <n>   Stop after n seconds\n"
        "      --interval <ms> Snapshot interval (default 1000)\n"
        "  --history [<db>]    Print recorded flows, newest first\n"
        "      --since <hours> Window (default 24)\n"
        "      --app <name>    Only this application\n"
        "      --limit <n>     At most n flows (default 200)\n"
        "      --csv <file>    Write the flows to a CSV file instead\n"
        "  --dns [<db>]        Print recorded DNS observations (same filters)\n"
        "  --events-history [<db>]\n"
        "                      Print recorded connection events (same filters; --csv)\n"
        "  --totals [<db>]     Print the rolled-up daily totals per application and peer\n"
        "  --store-stats [<db>]\n"
        "  --purge [<db>] --yes\n"
        "                      Delete everything recorded (irreversible)\n"
        "  -v, --version       Show version information\n"
        "  -h, --help          Show this message\n"
        "\n"
        "<db> defaults to the per-user data directory (see --store-stats). Not\n"
        "elevated, only this user's processes can be attributed; on Linux and\n"
        "Windows other users' sockets are still listed, as (unattributed).\n"
        "Byte counters come from netlink sock_diag on Linux; elsewhere a dash.\n"
        "A host name with a trailing ? came from reverse DNS - a guess, not\n"
        "what the application asked for. Events from the snapshot differ miss\n"
        "connections shorter than its interval; nf_conntrack (Linux, root, a\n"
        "firewall rule active) and the kernel's ETW events (Windows, elevated)\n"
        "do not.\n",
        programName);
}

void PrintCapabilities(const NetworkMonitorCapabilities& caps) {
    std::printf("Backend: %s\n", caps.backendName.c_str());
    std::printf("  socket table:         %s\n", caps.socketTable ? "yes" : "no");
    std::printf("  process attribution:  %s\n", caps.processAttribution ? "yes" : "no");
    std::printf("  every user's process: %s\n", caps.allUsers ? "yes" : "no");
    std::printf("  connection events:    %s\n", caps.connectionEvents ? "yes" : "no");
    std::printf("  per-connection bytes: %s\n", caps.perConnectionBytes ? "yes" : "no");
    std::printf("  DNS with process:     %s\n", caps.dnsWithProcess ? "yes" : "no");
    std::printf("  activity store:       %s\n", NetworkMonitor_StoreAvailable() ? "yes" : "no (no UltraDatabase)");
    for (const auto& note : caps.notes) std::printf("  note: %s\n", note.c_str());
    std::vector<NameSourceStatus> sources;
    NetworkMonitor_ListNameSources(sources);
    std::printf("Name sources: %s\n", sources.empty() ? "none" : "");
    for (const auto& source : sources) {
        std::printf("  %s: %s%s%s\n", source.name.c_str(), source.running ? "running" : "stopped",
                    source.reportsProcess ? ", with the asking process" : "",
                    source.lastError.empty() ? "" : (" - " + source.lastError).c_str());
    }
    const std::string resolver = NetworkMonitor_SystemResolver();
    std::printf("  system resolver:      %s\n", resolver.empty() ? "(none found)" : resolver.c_str());
    std::vector<EventSourceStatus> eventSources;
    NetworkMonitor_ListEventSources(eventSources);
    std::printf("Event sources: %s\n", eventSources.empty() ? "none" : "");
    for (const auto& source : eventSources) {
        std::printf("  %s: %s%s%s%s\n", source.name.c_str(), source.running ? "running" : "stopped",
                    source.reportsProcess ? ", with the process" : "",
                    source.reportsBytes ? ", with byte counts" : "",
                    source.lastError.empty() ? "" : (" - " + source.lastError).c_str());
    }
}

// ===== NAME SOURCES =====

struct NameSettings {
    bool reverseDns = true;
    bool dnsProxy = false;
    uint16_t dnsProxyPort = 53;
    std::string upstream;
    bool snapshotDiff = true;
    int diffIntervalMs = 250;
};

// Starts the sources the settings ask for. What could not start is
// returned as notes (and printed when `verbose`); only a proxy the user
// asked for and did not get is a failure.
bool StartNameSources(const NameSettings& settings, bool verbose, std::vector<std::string>& notes) {
    bool ok = true;
    if (settings.reverseDns) {
        const NetworkMonitorResult started =
            NetworkMonitor_RegisterNameSource(NetworkMonitor_CreateReverseDnsSource(ReverseDnsOptions()));
        if (!started) notes.push_back("Reverse DNS: " + started.message);
    }
    if (auto system = NetworkMonitor_CreateSystemDnsSource()) {
        const std::string name = system->Name();
        const NetworkMonitorResult started = NetworkMonitor_RegisterNameSource(std::move(system));
        if (!started) notes.push_back(name + ": " + started.message);
    }
    if (settings.dnsProxy) {
        DnsProxyOptions options;
        options.listenPort = settings.dnsProxyPort;
        options.upstreamAddress = settings.upstream;
        const NetworkMonitorResult started =
            NetworkMonitor_RegisterNameSource(NetworkMonitor_CreateDnsProxySource(options));
        if (!started) {
            notes.push_back("DNS proxy: " + started.message);
            ok = false;
        } else if (verbose) {
            std::printf("DNS proxy listening on 127.0.0.1:%u; point the system resolver at it "
                        "to see every query.\n", static_cast<unsigned>(settings.dnsProxyPort));
        }
    }
    // Events: the platform's own source first, then the differ.
    if (auto system = NetworkMonitor_CreateSystemEventSource()) {
        const std::string name = system->Name();
        const NetworkMonitorResult started = NetworkMonitor_RegisterEventSource(std::move(system));
        if (!started) notes.push_back("Events from " + name + ": " + started.message);
    }
    if (settings.snapshotDiff) {
        SnapshotDiffOptions options;
        options.intervalMs = settings.diffIntervalMs;
        const NetworkMonitorResult started =
            NetworkMonitor_RegisterEventSource(NetworkMonitor_CreateSnapshotDiffEventSource(options));
        if (!started) notes.push_back("Events from the snapshot differ: " + started.message);
    }
    if (verbose) for (const auto& note : notes) std::printf("%s\n", note.c_str());
    return ok;
}

// Stops every source at exit, whichever way main returns.
struct NameSourcesScope {
    ~NameSourcesScope() {
        NetworkMonitor_StopEventSources();
        NetworkMonitor_StopNameSources();
    }
};

std::string HostColumn(const std::string& name, NameSource source) {
    if (name.empty()) return std::string();
    return NetworkMonitor_NameIsObserved(source) ? name : name + " ?";
}

std::string ByteText(const std::optional<uint64_t>& bytes) {
    return bytes ? UltraCanvasHardwareInfo::FormatBytes(*bytes) : std::string("-");
}

std::string LocalTime(int64_t seconds) {
    const std::time_t when = static_cast<std::time_t>(seconds);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &when);
#else
    localtime_r(&when, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M:%S", &local);
    return buffer;
}

std::string LocalDay(int64_t seconds) {
    const std::time_t when = static_cast<std::time_t>(seconds);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &when);
#else
    gmtime_r(&when, &utc);
#endif
    char buffer[16];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%d", &utc);
    return buffer;
}

void PrintAttributionNotes() {
    const NetworkMonitorCapabilities caps = NetworkMonitor_GetCapabilities();
    for (const auto& note : caps.notes) {
        if (note.find("could not be") != std::string::npos ||
            note.find("Not running as root") != std::string::npos ||
            note.find("Not elevated") != std::string::npos) {
            std::printf("%s\n", note.c_str());
        }
    }
}

int RunHeadless(bool byApp, const NetworkMonitorOptions& options, bool resolve, const std::string& csvPath) {
    std::vector<NetworkConnection> connections;
    NetworkMonitorResult result = NetworkMonitor_ListConnections(connections, options);
    if (result && resolve) {
        // The first snapshot queued the peers for reverse DNS; give it a
        // moment and read again so the names are in.
        NetworkMonitor_WaitForNames(3000);
        result = NetworkMonitor_ListConnections(connections, options);
    }
    if (!result) {
        std::printf("Could not read the socket table: %s\n", result.message.c_str());
        return EXIT_FAILURE;
    }
    if (!csvPath.empty()) {
        int64_t rows = 0;
        const NetworkMonitorResult written = byApp
            ? NetworkMonitor_ExportSummaryCsv(NetworkMonitor_SummarizeByProcess(connections), csvPath, &rows)
            : NetworkMonitor_ExportConnectionsCsv(connections, csvPath, &rows);
        if (!written) {
            std::printf("Export failed: %s\n", written.message.c_str());
            return EXIT_FAILURE;
        }
        std::printf("Wrote %lld %s to %s\n", static_cast<long long>(rows),
                    byApp ? "applications" : "connections", csvPath.c_str());
        return EXIT_SUCCESS;
    }

    if (byApp) {
        const auto groups = NetworkMonitor_SummarizeByProcess(connections);
        std::printf("%-24s %7s %6s %6s %6s %6s %9s %9s  %s\n",
                    "APPLICATION", "PID", "CONNS", "ESTAB", "LISTEN", "PEERS", "SENT", "RECV", "HOSTS");
        for (const auto& g : groups) {
            std::string hosts;
            for (std::size_t i = 0; i < g.remoteNames.size() && i < 3; ++i) {
                hosts += (i ? ", " : "") + g.remoteNames[i];
            }
            if (g.remoteNames.size() > 3) hosts += ", +" + std::to_string(g.remoteNames.size() - 3);
            std::printf("%-24.24s %7s %6d %6d %6d %6zu %9s %9s  %s\n",
                        g.process.displayName.c_str(),
                        g.attributed ? std::to_string(g.process.pid).c_str() : "-",
                        g.connectionCount, g.establishedCount, g.listeningCount,
                        g.remoteAddresses.size(),
                        ByteText(g.bytesSent).c_str(), ByteText(g.bytesReceived).c_str(), hosts.c_str());
        }
        std::printf("\n%zu connections in %zu applications\n", connections.size(), groups.size());
    } else {
        std::printf("%-6s %-42s %-42s %-11s %9s %9s %-20s %-12s %s\n",
                    "PROTO", "LOCAL", "REMOTE", "STATE", "SENT", "RECV", "APPLICATION", "USER", "HOST");
        for (const auto& c : connections) {
            const bool unbound = c.IsListening() || c.state == NetworkConnectionState::Unconnected;
            const std::string app = c.process
                ? c.process->displayName + " (" + std::to_string(c.process->pid) + ")"
                : std::string("(unattributed)");
            const std::string user = c.process ? c.process->userName
                : (c.ownerUid ? "uid " + std::to_string(*c.ownerUid) : std::string());
            std::printf("%-4s%-2s %-42s %-42s %-11s %9s %9s %-20.20s %-12.12s %s\n",
                        NetworkMonitor_TransportName(c.transport),
                        c.family == NetworkAddressFamily::IPv6 ? "6" : "",
                        c.LocalEndpoint().c_str(),
                        unbound ? "*" : c.RemoteEndpoint().c_str(),
                        NetworkMonitor_StateName(c.state),
                        ByteText(c.bytesSent).c_str(), ByteText(c.bytesReceived).c_str(),
                        app.c_str(), user.c_str(), HostColumn(c.remoteName, c.nameSource).c_str());
        }
        std::printf("\n%zu connections\n", connections.size());
    }
    PrintAttributionNotes();
    return EXIT_SUCCESS;
}

std::string EventLine(const NetworkConnectionEvent& e) {
    const std::time_t when = static_cast<std::time_t>(e.observedAtMs / 1000);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &when);
#else
    localtime_r(&when, &local);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%H:%M:%S", &local);
    char line[512];
    const std::string app = e.process
        ? e.process->displayName + " (" + std::to_string(e.process->pid) + ")"
        : std::string("(unattributed)");
    const std::string bytes = e.kind == NetworkEventKind::Closed
        ? ByteText(e.bytesSent) + " / " + ByteText(e.bytesReceived) : std::string();
    std::snprintf(line, sizeof line, "%s.%03d %-8s %-4s%-2s %-28s %-28s %-22.22s %-18s %s",
                  stamp, static_cast<int>(e.observedAtMs % 1000), NetworkMonitor_EventKindName(e.kind),
                  NetworkMonitor_TransportName(e.transport), e.family == NetworkAddressFamily::IPv6 ? "6" : "",
                  e.LocalEndpoint().c_str(), e.RemoteEndpoint().c_str(), app.c_str(),
                  HostColumn(e.remoteName, e.nameSource).c_str(), bytes.c_str());
    return line;
}

int RunEvents(int seconds) {
    std::vector<EventSourceStatus> sources;
    NetworkMonitor_ListEventSources(sources);
    if (sources.empty()) {
        std::printf("No event source is running (see --capabilities).\n");
        return EXIT_FAILURE;
    }
    for (const auto& source : sources) {
        std::printf("%s: %s%s\n", source.name.c_str(), source.running ? "running" : "stopped",
                    source.lastError.empty() ? "" : (" - " + source.lastError).c_str());
    }
    std::printf("%-12s %-8s %-6s %-28s %-28s %-22s %-18s %s\n",
                "TIME", "EVENT", "PROTO", "LOCAL", "REMOTE", "APPLICATION", "HOST", "SENT / RECV");
    std::mutex printMutex;
    const EventListenerId listener = NetworkMonitor_AddEventListener([&printMutex](const NetworkConnectionEvent& e) {
        std::lock_guard<std::mutex> lock(printMutex);
        std::printf("%s\n", EventLine(e).c_str());
        std::fflush(stdout);
    });
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    using clock = std::chrono::steady_clock;
    const auto started = clock::now();
    while (!g_stopRecording.load()) {
        if (seconds > 0 && clock::now() - started >= std::chrono::seconds(seconds)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    NetworkMonitor_StopEventSources();
    NetworkMonitor_RemoveEventListener(listener);
    return EXIT_SUCCESS;
}

int RunNames(const NetworkMonitorOptions& options, bool resolve) {
    // A snapshot first, so the peers of the moment are queued for the
    // on-demand sources; then the table as it stands.
    std::vector<NetworkConnection> connections;
    NetworkMonitor_ListConnections(connections, options);
    if (resolve) NetworkMonitor_WaitForNames(3000);
    std::vector<NameRecord> names;
    NetworkMonitor_ListNames(names);
    std::printf("%-40s %-40s %-20s %-19s %s\n", "NAME", "ADDRESS", "SOURCE", "OBSERVED", "ASKED BY");
    for (const auto& n : names) {
        const std::string source = std::string(NetworkMonitor_NameSourceName(n.source)) +
                                   (NetworkMonitor_NameIsObserved(n.source) ? "" : " (weak)");
        std::printf("%-40.40s %-40s %-20s %-19s %s\n", n.name.c_str(), n.address.c_str(), source.c_str(),
                    LocalTime(n.observedAt).c_str(),
                    n.process ? (n.process->displayName + " (" + std::to_string(n.process->pid) + ")").c_str() : "");
    }
    std::vector<NameSourceStatus> sources;
    NetworkMonitor_ListNameSources(sources);
    std::printf("\n%zu addresses named", names.size());
    for (const auto& source : sources) {
        std::printf(" · %s: %lld observations%s", source.name.c_str(), static_cast<long long>(source.observations),
                    source.lastError.empty() ? "" : (" (" + source.lastError + ")").c_str());
    }
    std::printf("\n");
    return EXIT_SUCCESS;
}

// ===== THE STORE FROM THE COMMAND LINE =====

struct StoreSession {
    NetworkMonitorStoreHandle handle = NetworkMonitorInvalidStore;
    std::string path;
    ~StoreSession() { if (handle != NetworkMonitorInvalidStore) NetworkMonitor_CloseStore(handle); }
};

bool OpenStore(const std::string& requestedPath, StoreSession& session) {
    if (!NetworkMonitor_StoreAvailable()) {
        std::printf("This build has no UltraDatabase, so there is no activity store.\n");
        return false;
    }
    session.path = requestedPath.empty() ? UltraNetMonitor::DefaultStorePath() : requestedPath;
    if (session.path.empty()) {
        std::printf("No writable per-user data directory; give a path.\n");
        return false;
    }
    NetworkMonitorStoreOptions options;
    options.path = session.path;
    const NetworkMonitorResult opened = NetworkMonitor_OpenStore(options, session.handle);
    if (!opened) {
        std::printf("Could not open %s: %s\n", session.path.c_str(), opened.message.c_str());
        return false;
    }
    return true;
}

int RunRecord(const std::string& path, int seconds, int intervalMs, const NetworkMonitorOptions& options) {
    StoreSession session;
    if (!OpenStore(path, session)) return EXIT_FAILURE;
    std::printf("Recording to %s every %d ms%s - Ctrl-C to stop.\n", session.path.c_str(), intervalMs,
                seconds > 0 ? (" for " + std::to_string(seconds) + " s").c_str() : "");
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    // Every DNS observation and every connection event the sources report
    // goes in too, from the source's thread; the store serialises the
    // writers itself.
    std::atomic<int64_t> observations{0};
    std::atomic<int64_t> events{0};
    const NetworkMonitorStoreHandle handle = session.handle;
    const NameListenerId listener = NetworkMonitor_AddNameListener(
        [handle, &observations](const DnsObservation& observation) {
            if (NetworkMonitor_RecordDnsObservation(handle, observation)) ++observations;
        });
    const EventListenerId eventListener = NetworkMonitor_AddEventListener(
        [handle, &events](const NetworkConnectionEvent& event) {
            if (NetworkMonitor_RecordConnectionEvent(handle, event)) ++events;
        });

    using clock = std::chrono::steady_clock;
    const auto started = clock::now();
    int64_t snapshots = 0;
    int failures = 0;
    while (!g_stopRecording.load()) {
        if (seconds > 0 && clock::now() - started >= std::chrono::seconds(seconds)) break;
        std::vector<NetworkConnection> connections;
        const NetworkMonitorResult read = NetworkMonitor_ListConnections(connections, options);
        if (!read) {
            std::printf("Could not read the socket table: %s\n", read.message.c_str());
            NetworkMonitor_StopEventSources();
            NetworkMonitor_RemoveNameListener(listener);
            NetworkMonitor_RemoveEventListener(eventListener);
            return EXIT_FAILURE;
        }
        const NetworkMonitorResult recorded = NetworkMonitor_RecordSnapshot(session.handle, connections);
        if (!recorded) {
            std::printf("Could not record: %s\n", recorded.message.c_str());
            if (++failures >= 3) {
                NetworkMonitor_StopEventSources();
                NetworkMonitor_RemoveNameListener(listener);
                NetworkMonitor_RemoveEventListener(eventListener);
                return EXIT_FAILURE;
            }
        } else {
            ++snapshots;
        }
        const auto next = clock::now() + std::chrono::milliseconds(intervalMs);
        while (!g_stopRecording.load() && clock::now() < next) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    // The sources stop before the listeners go, so nothing is
    // half-recorded into a store about to close.
    NetworkMonitor_StopEventSources();
    NetworkMonitor_StopNameSources();
    NetworkMonitor_RemoveNameListener(listener);
    NetworkMonitor_RemoveEventListener(eventListener);
    NetworkMonitor_ApplyRetention(session.handle);
    NetworkMonitorStoreStats stats;
    NetworkMonitor_StoreStats(session.handle, stats);
    std::printf("\nRecorded %lld snapshots, %lld DNS observations and %lld events. The store holds %lld flows, "
                "%lld daily totals, %lld observations and %lld events.\n",
                static_cast<long long>(snapshots), static_cast<long long>(observations.load()),
                static_cast<long long>(events.load()),
                static_cast<long long>(stats.flows), static_cast<long long>(stats.dailyTotals),
                static_cast<long long>(stats.dnsObservations), static_cast<long long>(stats.connectionEvents));
    return EXIT_SUCCESS;
}

int RunHistory(const std::string& path, const ActivityQuery& query, const std::string& csvPath) {
    StoreSession session;
    if (!OpenStore(path, session)) return EXIT_FAILURE;

    if (!csvPath.empty()) {
        int64_t rows = 0;
        const NetworkMonitorResult written = NetworkMonitor_ExportFlowsCsv(session.handle, query, csvPath, &rows);
        if (!written) {
            std::printf("Export failed: %s\n", written.message.c_str());
            return EXIT_FAILURE;
        }
        std::printf("Wrote %lld flows to %s\n", static_cast<long long>(rows), csvPath.c_str());
        return EXIT_SUCCESS;
    }

    std::vector<RecordedFlow> flows;
    const NetworkMonitorResult read = NetworkMonitor_QueryFlows(session.handle, query, flows);
    if (!read) {
        std::printf("Could not read the store: %s\n", read.message.c_str());
        return EXIT_FAILURE;
    }
    std::printf("%-19s %-19s %4s %-6s %-30s %-30s %-11s %9s %9s %-22s %s\n",
                "FIRST SEEN", "LAST SEEN", "SEEN", "PROTO", "LOCAL", "REMOTE", "STATE", "SENT", "RECV",
                "APPLICATION", "HOST");
    for (const auto& f : flows) {
        const bool unbound = f.lastState == NetworkConnectionState::Listening ||
                             f.lastState == NetworkConnectionState::Unconnected;
        const std::string app = f.process
            ? f.process->displayName + " (" + std::to_string(f.process->pid) + ")"
            : std::string("(unattributed)");
        std::printf("%-19s %-19s %4d %-4s%-2s %-30s %-30s %-11s %9s %9s %-22.22s %s\n",
                    LocalTime(f.firstSeen).c_str(), LocalTime(f.lastSeen).c_str(), f.snapshots,
                    NetworkMonitor_TransportName(f.transport),
                    f.family == NetworkAddressFamily::IPv6 ? "6" : "",
                    f.LocalEndpoint().c_str(), unbound ? "*" : f.RemoteEndpoint().c_str(),
                    NetworkMonitor_StateName(f.lastState),
                    ByteText(f.bytesSent).c_str(), ByteText(f.bytesReceived).c_str(), app.c_str(),
                    HostColumn(f.remoteName, f.nameSource).c_str());
    }
    std::printf("\n%zu flows from %s\n", flows.size(), session.path.c_str());
    return EXIT_SUCCESS;
}

int RunDns(const std::string& path, const ActivityQuery& query) {
    StoreSession session;
    if (!OpenStore(path, session)) return EXIT_FAILURE;
    std::vector<RecordedDnsObservation> observations;
    const NetworkMonitorResult read = NetworkMonitor_QueryDnsObservations(session.handle, query, observations);
    if (!read) {
        std::printf("Could not read the store: %s\n", read.message.c_str());
        return EXIT_FAILURE;
    }
    std::printf("%-19s %-40s %-40s %-20s %s\n", "OBSERVED", "NAME", "ADDRESS", "SOURCE", "ASKED BY");
    for (const auto& o : observations) {
        const std::string source = std::string(NetworkMonitor_NameSourceName(o.source)) +
                                   (NetworkMonitor_NameIsObserved(o.source) ? "" : " (weak)");
        std::printf("%-19s %-40.40s %-40s %-20s %s\n", LocalTime(o.observedAt).c_str(), o.queryName.c_str(),
                    o.address.c_str(), source.c_str(),
                    o.process ? (o.process->displayName + " (" + std::to_string(o.process->pid) + ")").c_str() : "");
    }
    std::printf("\n%zu DNS observations from %s\n", observations.size(), session.path.c_str());
    return EXIT_SUCCESS;
}

int RunEventsHistory(const std::string& path, const ActivityQuery& query, const std::string& csvPath) {
    StoreSession session;
    if (!OpenStore(path, session)) return EXIT_FAILURE;
    if (!csvPath.empty()) {
        int64_t rows = 0;
        const NetworkMonitorResult written = NetworkMonitor_ExportEventsCsv(session.handle, query, csvPath, &rows);
        if (!written) {
            std::printf("Export failed: %s\n", written.message.c_str());
            return EXIT_FAILURE;
        }
        std::printf("Wrote %lld events to %s\n", static_cast<long long>(rows), csvPath.c_str());
        return EXIT_SUCCESS;
    }
    std::vector<RecordedConnectionEvent> events;
    const NetworkMonitorResult read = NetworkMonitor_QueryConnectionEvents(session.handle, query, events);
    if (!read) {
        std::printf("Could not read the store: %s\n", read.message.c_str());
        return EXIT_FAILURE;
    }
    std::printf("%-12s %-8s %-6s %-28s %-28s %-22s %-18s %s\n",
                "TIME", "EVENT", "PROTO", "LOCAL", "REMOTE", "APPLICATION", "HOST", "SENT / RECV");
    for (const auto& r : events) std::printf("%s\n", EventLine(r.event).c_str());
    std::printf("\n%zu events from %s\n", events.size(), session.path.c_str());
    return EXIT_SUCCESS;
}

int RunTotals(const std::string& path, const ActivityQuery& query) {
    StoreSession session;
    if (!OpenStore(path, session)) return EXIT_FAILURE;
    std::vector<DailyProcessTotal> totals;
    const NetworkMonitorResult read = NetworkMonitor_QueryDailyTotals(session.handle, query, totals);
    if (!read) {
        std::printf("Could not read the store: %s\n", read.message.c_str());
        return EXIT_FAILURE;
    }
    std::printf("%-10s %-24s %-40s %6s %7s %9s %9s %s\n",
                "DAY (UTC)", "APPLICATION", "PEER", "FLOWS", "COUNTED", "SENT", "RECV", "HOST");
    for (const auto& t : totals) {
        std::printf("%-10s %-24.24s %-40.40s %6d %7d %9s %9s %s\n",
                    LocalDay(t.day).c_str(), t.processName.c_str(), t.remoteAddress.c_str(),
                    t.flows, t.countedFlows,
                    UltraCanvasHardwareInfo::FormatBytes(t.bytesSent).c_str(),
                    UltraCanvasHardwareInfo::FormatBytes(t.bytesReceived).c_str(), t.remoteName.c_str());
    }
    std::printf("\n%zu daily totals from %s (flows still within the retention window are not "
                "rolled up yet - see --history)\n", totals.size(), session.path.c_str());
    return EXIT_SUCCESS;
}

int RunStoreStats(const std::string& path) {
    StoreSession session;
    if (!OpenStore(path, session)) return EXIT_FAILURE;
    NetworkMonitorStoreStats stats;
    const NetworkMonitorResult read = NetworkMonitor_StoreStats(session.handle, stats);
    if (!read) {
        std::printf("Could not read the store: %s\n", read.message.c_str());
        return EXIT_FAILURE;
    }
    std::printf("Store:        %s\n", session.path.c_str());
    std::printf("Flows:        %lld\n", static_cast<long long>(stats.flows));
    std::printf("Daily totals: %lld\n", static_cast<long long>(stats.dailyTotals));
    std::printf("Snapshots:    %lld\n", static_cast<long long>(stats.snapshots));
    std::printf("DNS records:  %lld\n", static_cast<long long>(stats.dnsObservations));
    std::printf("Events:       %lld\n", static_cast<long long>(stats.connectionEvents));
    if (stats.flows > 0) {
        std::printf("Oldest flow:  %s\nNewest flow:  %s\n",
                    LocalTime(stats.oldestFlow).c_str(), LocalTime(stats.newestFlow).c_str());
    }
    return EXIT_SUCCESS;
}

int RunPurge(const std::string& path, bool confirmed) {
    if (!confirmed) {
        std::printf("--purge deletes everything recorded, irreversibly; add --yes to confirm.\n");
        return EXIT_FAILURE;
    }
    StoreSession session;
    if (!OpenStore(path, session)) return EXIT_FAILURE;
    const NetworkMonitorResult purged = NetworkMonitor_Purge(session.handle);
    if (!purged) {
        std::printf("Purge failed: %s\n", purged.message.c_str());
        return EXIT_FAILURE;
    }
    std::printf("Purged %s\n", session.path.c_str());
    return EXIT_SUCCESS;
}

// An option's optional path argument: the next word, when it is not itself
// an option.
std::string OptionalPath(int argc, char* argv[], int& i) {
    if (i + 1 < argc && argv[i + 1][0] != '-') return argv[++i];
    return std::string();
}

bool NeedsValue(int argc, int i, const char* option) {
    if (i + 1 < argc) return true;
    std::printf("%s needs a value\n", option);
    return false;
}

} // namespace

int main(int argc, char* argv[]) {
    enum class Mode { Window, List, ByApp, Names, Events, Record, History, Dns, EventsHistory, Totals, StoreStats, Purge };
    Mode mode = Mode::Window;
    bool capabilities = false;
    bool confirmed = false;
    bool resolve = false;
    NameSettings names;
    std::string storePath;
    std::string csvPath;
    int seconds = 0;
    int intervalMs = 1000;
    double sinceHours = 24;
    int limit = 200;
    NetworkMonitorOptions options;
    ActivityQuery query;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (arg == "--version" || arg == "-v") {
            std::printf("UltraNetMonitor %s\nUltraCanvas Framework %s\n",
                        ULTRANETMONITOR_VERSION, UltraCanvas::versionString);
            return EXIT_SUCCESS;
        } else if (arg == "--list") {
            mode = Mode::List;
        } else if (arg == "--by-app") {
            mode = Mode::ByApp;
        } else if (arg == "--capabilities") {
            capabilities = true;
        } else if (arg == "--names") {
            mode = Mode::Names;
        } else if (arg == "--events") {
            mode = Mode::Events;
        } else if (arg == "--events-history") {
            mode = Mode::EventsHistory;
            storePath = OptionalPath(argc, argv, i);
        } else if (arg == "--no-diff") {
            names.snapshotDiff = false;
        } else if (arg == "--diff-interval") {
            if (!NeedsValue(argc, i, "--diff-interval")) return EXIT_FAILURE;
            names.diffIntervalMs = std::max(50, std::atoi(argv[++i]));
        } else if (arg == "--resolve") {
            resolve = true;
        } else if (arg == "--no-rdns") {
            names.reverseDns = false;
        } else if (arg == "--dns-proxy") {
            names.dnsProxy = true;
            const std::string port = OptionalPath(argc, argv, i);
            if (!port.empty()) names.dnsProxyPort = static_cast<uint16_t>(std::atoi(port.c_str()));
        } else if (arg == "--upstream") {
            if (!NeedsValue(argc, i, "--upstream")) return EXIT_FAILURE;
            names.upstream = argv[++i];
        } else if (arg == "--dns") {
            mode = Mode::Dns;
            storePath = OptionalPath(argc, argv, i);
        } else if (arg == "--no-listen") {
            options.includeListening = false;
            query.includeListening = false;
        } else if (arg == "--no-loopback") {
            options.includeLoopback = false;
            query.includeLoopback = false;
        } else if (arg == "--record") {
            mode = Mode::Record;
            storePath = OptionalPath(argc, argv, i);
        } else if (arg == "--history") {
            mode = Mode::History;
            storePath = OptionalPath(argc, argv, i);
        } else if (arg == "--totals") {
            mode = Mode::Totals;
            storePath = OptionalPath(argc, argv, i);
        } else if (arg == "--store-stats") {
            mode = Mode::StoreStats;
            storePath = OptionalPath(argc, argv, i);
        } else if (arg == "--purge") {
            mode = Mode::Purge;
            storePath = OptionalPath(argc, argv, i);
        } else if (arg == "--yes") {
            confirmed = true;
        } else if (arg == "--seconds") {
            if (!NeedsValue(argc, i, "--seconds")) return EXIT_FAILURE;
            seconds = std::atoi(argv[++i]);
        } else if (arg == "--interval") {
            if (!NeedsValue(argc, i, "--interval")) return EXIT_FAILURE;
            intervalMs = std::max(100, std::atoi(argv[++i]));
        } else if (arg == "--since") {
            if (!NeedsValue(argc, i, "--since")) return EXIT_FAILURE;
            sinceHours = std::atof(argv[++i]);
        } else if (arg == "--app") {
            if (!NeedsValue(argc, i, "--app")) return EXIT_FAILURE;
            query.processName = argv[++i];
        } else if (arg == "--limit") {
            if (!NeedsValue(argc, i, "--limit")) return EXIT_FAILURE;
            limit = std::atoi(argv[++i]);
        } else if (arg == "--csv") {
            if (!NeedsValue(argc, i, "--csv")) return EXIT_FAILURE;
            csvPath = argv[++i];
        } else {
            std::printf("Unknown argument: %s\nUse --help for usage.\n", arg.c_str());
            return EXIT_FAILURE;
        }
    }

    // The name sources run in every mode; a store reader has no use for
    // them, so those modes skip the start.
    NameSourcesScope sourcesScope;
    std::vector<std::string> nameNotes;
    const bool readsOnly = mode == Mode::History || mode == Mode::Dns || mode == Mode::EventsHistory ||
                           mode == Mode::Totals || mode == Mode::StoreStats || mode == Mode::Purge;
    if (!readsOnly) {
        const bool verbose = mode != Mode::Window;
        if (!StartNameSources(names, verbose, nameNotes) && mode != Mode::Window) return EXIT_FAILURE;
    }

    if (capabilities) {
        // Some capabilities (byte counters, unreadable-process counts) are
        // only known once a snapshot has been taken, so take one first.
        if (NetworkMonitor_IsAvailable()) {
            std::vector<NetworkConnection> probe;
            NetworkMonitor_ListConnections(probe, options);
        }
        PrintCapabilities(NetworkMonitor_GetCapabilities());
        if (mode == Mode::Window) return EXIT_SUCCESS;
    }

    query.since = NetworkMonitor_Now() - static_cast<int64_t>(sinceHours * 3600.0);
    query.limit = limit;
    switch (mode) {
        case Mode::List:       return RunHeadless(false, options, resolve, csvPath);
        case Mode::ByApp:      return RunHeadless(true, options, resolve, csvPath);
        case Mode::Names:      return RunNames(options, resolve);
        case Mode::Events:     return RunEvents(seconds);
        case Mode::Record:     return RunRecord(storePath, seconds, intervalMs, options);
        case Mode::History:    return RunHistory(storePath, query, csvPath);
        case Mode::Dns:        return RunDns(storePath, query);
        case Mode::EventsHistory: return RunEventsHistory(storePath, query, csvPath);
        case Mode::Totals:     return RunTotals(storePath, query);
        case Mode::StoreStats: return RunStoreStats(storePath);
        case Mode::Purge:      return RunPurge(storePath, confirmed);
        case Mode::Window:     break;
    }

    UltraCanvasApplication app;

#ifdef __linux__
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    if (!XInitThreads()) {
        debugOutput << "Warning: X11 threading initialization failed" << std::endl;
    }
#endif

    try {
        if (!app.Initialize("UltraNetMonitor")) {
            debugOutput << "Failed to initialize the UltraCanvas application" << std::endl;
            return EXIT_FAILURE;
        }
        // One icon, everywhere the app is drawn: the window and the taskbar
        // entry that follows it read this file; the .ico embedded in the
        // Windows binary and the desktop entry's theme icon are rendered from
        // the same media/appicon/UltraNetMonitor.svg (see CMakeLists.txt).
        app.SetDefaultWindowIcon(
            NormalizePath(GetResourcesDir() + "media/appicon/UltraNetMonitor.png"));
        UltraCanvasDialogManager::SetUseNativeDialogs(true);

        UltraNetMonitor::UltraNetMonitorWindow window;
        if (!window.Initialize(nameNotes)) {
            debugOutput << "Failed to create the UltraNetMonitor window" << std::endl;
            return EXIT_FAILURE;
        }
        window.Show();
        app.Run();
    } catch (const std::exception& e) {
        debugOutput << "Unhandled exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

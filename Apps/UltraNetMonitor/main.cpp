// Apps/UltraNetMonitor/main.cpp
// UltraNetMonitor - shows which processes hold which network connections,
// live, from the operating system's socket table (the NetworkMonitor
// module), and records them over time into an activity store. It observes;
// it never blocks or modifies traffic.
//
// Two ways in. Without arguments it opens the UltraCanvas window. With
// --list / --by-app / --capabilities it prints one snapshot and exits; with
// --record it writes snapshots to the store until stopped, and --history /
// --totals / --store-stats read the store back, --purge empties it. All of
// that runs headless, which is what makes it usable over ssh and checkable
// in CI.
// Version: 0.3.0
// Author: UltraCanvas Framework / ULTRA OS

// Before the window header: on Linux that one reaches X11, whose `None`
// macro would otherwise break HardwareQuery::None in this header.
#include "UltraCanvasHardwareInfo.h"

#include "ui/UltraNetMonitorPaths.h"
#include "ui/UltraNetMonitorWindow.h"

#include "NetworkMonitor/NetworkMonitor.h"
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

UltraCanvasApplication* g_app = nullptr;
std::atomic<bool> g_stopRecording{false};

void SignalHandler(int) {
    if (g_app) {
        g_app->RequestExit();
        std::exit(EXIT_SUCCESS);
    }
    // Headless recording: finish the loop, apply retention, close cleanly.
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
        "  --no-listen         With --list / --by-app / --history: leave out listeners\n"
        "  --no-loopback       With --list / --by-app / --history: leave out loopback\n"
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
        "Byte counters come from netlink sock_diag on Linux; elsewhere a dash.\n",
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

int RunHeadless(bool byApp, const NetworkMonitorOptions& options) {
    std::vector<NetworkConnection> connections;
    const NetworkMonitorResult result = NetworkMonitor_ListConnections(connections, options);
    if (!result) {
        std::printf("Could not read the socket table: %s\n", result.message.c_str());
        return EXIT_FAILURE;
    }

    if (byApp) {
        const auto groups = NetworkMonitor_SummarizeByProcess(connections);
        std::printf("%-24s %7s %6s %6s %6s %6s %9s %9s\n",
                    "APPLICATION", "PID", "CONNS", "ESTAB", "LISTEN", "PEERS", "SENT", "RECV");
        for (const auto& g : groups) {
            std::printf("%-24.24s %7s %6d %6d %6d %6zu %9s %9s\n",
                        g.process.displayName.c_str(),
                        g.attributed ? std::to_string(g.process.pid).c_str() : "-",
                        g.connectionCount, g.establishedCount, g.listeningCount,
                        g.remoteAddresses.size(),
                        ByteText(g.bytesSent).c_str(), ByteText(g.bytesReceived).c_str());
        }
        std::printf("\n%zu connections in %zu applications\n", connections.size(), groups.size());
    } else {
        std::printf("%-6s %-42s %-42s %-11s %9s %9s %-20s %s\n",
                    "PROTO", "LOCAL", "REMOTE", "STATE", "SENT", "RECV", "APPLICATION", "USER");
        for (const auto& c : connections) {
            const bool unbound = c.IsListening() || c.state == NetworkConnectionState::Unconnected;
            const std::string app = c.process
                ? c.process->displayName + " (" + std::to_string(c.process->pid) + ")"
                : std::string("(unattributed)");
            const std::string user = c.process ? c.process->userName
                : (c.ownerUid ? "uid " + std::to_string(*c.ownerUid) : std::string());
            std::printf("%-4s%-2s %-42s %-42s %-11s %9s %9s %-20.20s %s\n",
                        NetworkMonitor_TransportName(c.transport),
                        c.family == NetworkAddressFamily::IPv6 ? "6" : "",
                        c.LocalEndpoint().c_str(),
                        unbound ? "*" : c.RemoteEndpoint().c_str(),
                        NetworkMonitor_StateName(c.state),
                        ByteText(c.bytesSent).c_str(), ByteText(c.bytesReceived).c_str(),
                        app.c_str(), user.c_str());
        }
        std::printf("\n%zu connections\n", connections.size());
    }
    PrintAttributionNotes();
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
            return EXIT_FAILURE;
        }
        const NetworkMonitorResult recorded = NetworkMonitor_RecordSnapshot(session.handle, connections);
        if (!recorded) {
            std::printf("Could not record: %s\n", recorded.message.c_str());
            if (++failures >= 3) return EXIT_FAILURE;
        } else {
            ++snapshots;
        }
        const auto next = clock::now() + std::chrono::milliseconds(intervalMs);
        while (!g_stopRecording.load() && clock::now() < next) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    NetworkMonitor_ApplyRetention(session.handle);
    NetworkMonitorStoreStats stats;
    NetworkMonitor_StoreStats(session.handle, stats);
    std::printf("\nRecorded %lld snapshots. The store holds %lld flows and %lld daily totals.\n",
                static_cast<long long>(snapshots), static_cast<long long>(stats.flows),
                static_cast<long long>(stats.dailyTotals));
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
    std::printf("%-19s %-19s %4s %-6s %-30s %-30s %-11s %9s %9s %s\n",
                "FIRST SEEN", "LAST SEEN", "SEEN", "PROTO", "LOCAL", "REMOTE", "STATE", "SENT", "RECV",
                "APPLICATION");
    for (const auto& f : flows) {
        const bool unbound = f.lastState == NetworkConnectionState::Listening ||
                             f.lastState == NetworkConnectionState::Unconnected;
        const std::string app = f.process
            ? f.process->displayName + " (" + std::to_string(f.process->pid) + ")"
            : std::string("(unattributed)");
        std::printf("%-19s %-19s %4d %-4s%-2s %-30s %-30s %-11s %9s %9s %s\n",
                    LocalTime(f.firstSeen).c_str(), LocalTime(f.lastSeen).c_str(), f.snapshots,
                    NetworkMonitor_TransportName(f.transport),
                    f.family == NetworkAddressFamily::IPv6 ? "6" : "",
                    f.LocalEndpoint().c_str(), unbound ? "*" : f.RemoteEndpoint().c_str(),
                    NetworkMonitor_StateName(f.lastState),
                    ByteText(f.bytesSent).c_str(), ByteText(f.bytesReceived).c_str(), app.c_str());
    }
    std::printf("\n%zu flows from %s\n", flows.size(), session.path.c_str());
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
    std::printf("%-10s %-24s %-40s %6s %7s %9s %9s\n",
                "DAY (UTC)", "APPLICATION", "PEER", "FLOWS", "COUNTED", "SENT", "RECV");
    for (const auto& t : totals) {
        std::printf("%-10s %-24.24s %-40.40s %6d %7d %9s %9s\n",
                    LocalDay(t.day).c_str(), t.processName.c_str(), t.remoteAddress.c_str(),
                    t.flows, t.countedFlows,
                    UltraCanvasHardwareInfo::FormatBytes(t.bytesSent).c_str(),
                    UltraCanvasHardwareInfo::FormatBytes(t.bytesReceived).c_str());
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
    enum class Mode { Window, List, ByApp, Record, History, Totals, StoreStats, Purge };
    Mode mode = Mode::Window;
    bool capabilities = false;
    bool confirmed = false;
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
        case Mode::List:       return RunHeadless(false, options);
        case Mode::ByApp:      return RunHeadless(true, options);
        case Mode::Record:     return RunRecord(storePath, seconds, intervalMs, options);
        case Mode::History:    return RunHistory(storePath, query, csvPath);
        case Mode::Totals:     return RunTotals(storePath, query);
        case Mode::StoreStats: return RunStoreStats(storePath);
        case Mode::Purge:      return RunPurge(storePath, confirmed);
        case Mode::Window:     break;
    }

    UltraCanvasApplication app;
    g_app = &app;

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
        if (!window.Initialize()) {
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

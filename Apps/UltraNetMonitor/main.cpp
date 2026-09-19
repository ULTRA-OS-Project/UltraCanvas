// Apps/UltraNetMonitor/main.cpp
// UltraNetMonitor - shows which processes hold which network connections,
// live, from the operating system's socket table (the NetworkMonitor
// module). It observes; it never blocks or modifies traffic.
//
// Two ways in. Without arguments it opens the UltraCanvas window. With
// --list / --by-app / --capabilities it prints one snapshot and exits, which
// is what makes it usable over ssh and checkable in CI.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/UltraNetMonitorWindow.h"

#include "NetworkMonitor/NetworkMonitor.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasUtils.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

#ifdef __linux__
#include <X11/Xlib.h>
#include <csignal>
#endif

// Defined by the build from the first line of Docs/UltraNetMonitor/CHANGELOG.md
// - see cmake/UltraCanvasVersion.cmake. The fallback only applies outside CMake.
#ifndef ULTRANETMONITOR_VERSION
#define ULTRANETMONITOR_VERSION "0.0-dev"
#endif

using namespace UltraCanvas;

namespace {

UltraCanvasApplication* g_app = nullptr;

#ifdef __linux__
void SignalHandler(int) {
    if (g_app) g_app->RequestExit();
    std::exit(EXIT_SUCCESS);
}
#endif

void PrintUsage(const char* programName) {
    std::printf(
        "UltraNetMonitor - which processes hold which network connections\n"
        "Powered by the UltraCanvas framework\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "  (no options)      Open the UltraNetMonitor window\n"
        "  --list            Print every connection with its process and exit\n"
        "  --by-app          Print the per-process roll-up and exit\n"
        "  --capabilities    Print what this machine's backend can deliver\n"
        "  --no-listen       With --list / --by-app: leave out listeners\n"
        "  --no-loopback     With --list / --by-app: leave out loopback\n"
        "  -v, --version     Show version information\n"
        "  -h, --help        Show this message\n"
        "\n"
        "Not running as root, only this user's processes can be attributed;\n"
        "other users' sockets are still listed, as (unattributed).\n",
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
    for (const auto& note : caps.notes) std::printf("  note: %s\n", note.c_str());
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
        std::printf("%-24s %7s %6s %6s %6s %6s\n",
                    "APPLICATION", "PID", "CONNS", "ESTAB", "LISTEN", "PEERS");
        for (const auto& g : groups) {
            std::printf("%-24.24s %7s %6d %6d %6d %6zu\n",
                        g.process.displayName.c_str(),
                        g.attributed ? std::to_string(g.process.pid).c_str() : "-",
                        g.connectionCount, g.establishedCount, g.listeningCount,
                        g.remoteAddresses.size());
        }
        std::printf("\n%zu connections in %zu applications\n", connections.size(), groups.size());
    } else {
        std::printf("%-6s %-42s %-42s %-11s %-20s %s\n",
                    "PROTO", "LOCAL", "REMOTE", "STATE", "APPLICATION", "USER");
        for (const auto& c : connections) {
            const bool unbound = c.IsListening() || c.state == NetworkConnectionState::Unconnected;
            const std::string app = c.process
                ? c.process->displayName + " (" + std::to_string(c.process->pid) + ")"
                : std::string("(unattributed)");
            const std::string user = c.process ? c.process->userName
                : (c.ownerUid ? "uid " + std::to_string(*c.ownerUid) : std::string());
            std::printf("%-4s%-2s %-42s %-42s %-11s %-20.20s %s\n",
                        NetworkMonitor_TransportName(c.transport),
                        c.family == NetworkAddressFamily::IPv6 ? "6" : "",
                        c.LocalEndpoint().c_str(),
                        unbound ? "*" : c.RemoteEndpoint().c_str(),
                        NetworkMonitor_StateName(c.state), app.c_str(), user.c_str());
        }
        std::printf("\n%zu connections\n", connections.size());
    }

    const NetworkMonitorCapabilities caps = NetworkMonitor_GetCapabilities();
    for (const auto& note : caps.notes) {
        if (note.find("could not be inspected") != std::string::npos ||
            note.find("Not running as root") != std::string::npos) {
            std::printf("%s\n", note.c_str());
        }
    }
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char* argv[]) {
    bool list = false;
    bool byApp = false;
    bool capabilities = false;
    NetworkMonitorOptions options;

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
            list = true;
        } else if (arg == "--by-app") {
            byApp = true;
        } else if (arg == "--capabilities") {
            capabilities = true;
        } else if (arg == "--no-listen") {
            options.includeListening = false;
        } else if (arg == "--no-loopback") {
            options.includeLoopback = false;
        } else {
            std::printf("Unknown argument: %s\nUse --help for usage.\n", arg.c_str());
            return EXIT_FAILURE;
        }
    }

    if (capabilities) {
        PrintCapabilities(NetworkMonitor_GetCapabilities());
        if (!list && !byApp) return EXIT_SUCCESS;
    }
    if (list || byApp) return RunHeadless(byApp, options);

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

// Tests/UltraNet/ApiStatus/ApiStatusMain.cpp
// Entry point for UltraNetApiStatus — walks the public UltraNet surface and
// prints, per function, whether it is WORKING here, merely IMPLEMENTED, or
// NOT IMPLEMENTED.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ApiStatus.h"
#include "ProbeServer.h"

#include <UltraNet/UltraNetCore.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace {

void PrintUsage(const char* argv0) {
    std::printf(
        "UltraNetApiStatus — public API status report for the UltraNet module\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "  --format=text|markdown|json  Report format (default: text)\n"
        "  --area=<name>                Only probe one area (Core, URL, HTTP,\n"
        "                               Session, SSE, WebSocket, DNS, Socket,\n"
        "                               TLS, FTP, MIME, Plugins)\n"
        "  --output=<path>              Write the report to a file instead of stdout\n"
        "  --network                    Also run probes that need the public internet\n"
        "  --strict                     Exit non-zero unless every entry is WORKING\n"
        "  --serve[=<seconds>]          Diagnostic: start only the loopback HTTP +\n"
        "                               WebSocket origin, print its port, and hold it\n"
        "                               open (default 30s) so an external client can\n"
        "                               poke at it. Runs no probes.\n"
        "  -h, --help                   This message\n"
        "\n"
        "Statuses:\n"
        "  WORKING          the probe drove the real code path and the observable\n"
        "                   result matched what the API promises\n"
        "  IMPLEMENTED      the implementation exists and was reached, but this\n"
        "                   environment cannot confirm the behaviour\n"
        "  NOT IMPLEMENTED  a documented stub / no-op, or a backend this build lacks\n"
        "  BROKEN           the probe ran and the result contradicted the API\n"
        "\n"
        "Environment overrides:\n"
        "  ULTRANET_PROBE_NETWORK=1     same as --network\n"
        "  ULTRANET_PROBE_HTTPS_URL     HTTPS endpoint for the TLS-info probe\n"
        "  ULTRANET_PROBE_DNS_DOMAIN    domain used by the record-type DNS probes\n"
        "  ULTRANET_PROBE_FTP_URL       writable FTP directory, e.g.\n"
        "                               ftp://user:pass@host/probe/\n"
        "  ULTRANET_PROBE_PLUGIN_DIR    directory of plug-in DSOs to load\n"
        "\n"
        "Exit code: 0 when nothing is BROKEN (and, with --strict, when every\n"
        "entry is WORKING); 1 otherwise; 2 on a usage error.\n",
        argv0);
}

bool StartsWith(const char* s, const char* prefix) {
    return std::strncmp(s, prefix, std::strlen(prefix)) == 0;
}

// Every probe target is a loopback peer this tool starts itself. libcurl falls
// back to the *_proxy environment variables whenever no proxy is configured
// through UltraNetConfig (setting UltraNetProxyType::None means "not
// configured", not "bypass"), so on a machine whose no_proxy list omits
// loopback, every HTTP probe would be routed at a proxy that cannot reach it.
// Make sure loopback is exempt, without disturbing the proxy itself — the
// --network probes still need it.
void ExemptLoopbackFromProxy() {
    const char* existing = std::getenv("no_proxy");
    if (!existing || !*existing) existing = std::getenv("NO_PROXY");
    const std::string current = existing ? existing : "";
    if (current.find("127.0.0.1") != std::string::npos) return;

    const std::string updated =
        current.empty() ? "127.0.0.1,localhost,::1"
                        : "127.0.0.1,localhost,::1," + current;
#if defined(_WIN32) || defined(_WIN64)
    _putenv_s("no_proxy", updated.c_str());
    _putenv_s("NO_PROXY", updated.c_str());
#else
    ::setenv("no_proxy", updated.c_str(), 1);
    ::setenv("NO_PROXY", updated.c_str(), 1);
#endif
}

} // namespace

int main(int argc, char** argv) {
    ultranet_apistatus::RunOptions options;
    int serveSeconds = 0;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            PrintUsage(argv[0]);
            return 0;
        }
        if (std::strcmp(arg, "--serve") == 0) { serveSeconds = 30; continue; }
        if (StartsWith(arg, "--serve=")) {
            serveSeconds = std::atoi(arg + 8);
            if (serveSeconds <= 0) {
                std::fprintf(stderr, "--serve needs a positive number of seconds\n");
                return 2;
            }
            continue;
        }
        if (std::strcmp(arg, "--strict") == 0)  { options.strict = true; continue; }
        if (std::strcmp(arg, "--network") == 0) { options.allowNetwork = true; continue; }
        if (StartsWith(arg, "--format="))  { options.format     = arg + 9;  continue; }
        if (StartsWith(arg, "--area="))    { options.areaFilter = arg + 7;  continue; }
        if (StartsWith(arg, "--output="))  { options.outputPath = arg + 9;  continue; }
        std::fprintf(stderr, "unknown option: %s\n\n", arg);
        PrintUsage(argv[0]);
        return 2;
    }

    if (options.format != "text" && options.format != "markdown" &&
        options.format != "json") {
        std::fprintf(stderr, "unknown format: %s\n", options.format.c_str());
        return 2;
    }

    ExemptLoopbackFromProxy();

    UltraNetConfig config = UltraNetConfig::Default();
    config.userAgent = "UltraNetApiStatus/0.1";
    const UltraNetResult init = UltraNet_Initialize(config);
    if (!init) {
        std::fprintf(stderr, "UltraNet_Initialize failed: %s\n", init.message.c_str());
        return 2;
    }

    if (serveSeconds > 0) {
        ultranet_apistatus::LoopbackServer& server =
            ultranet_apistatus::LoopbackServer::Instance();
        if (!server.Start()) {
            std::fprintf(stderr, "loopback origin failed to start: %s\n",
                         server.Error().c_str());
            UltraNet_Shutdown();
            return 2;
        }
        std::printf("%d\n", server.Port());
        std::fflush(stdout);
        std::fprintf(stderr, "loopback origin on http://127.0.0.1:%d "
                             "(ws://127.0.0.1:%d/ws) for %ds\n",
                     server.Port(), server.Port(), serveSeconds);
        std::this_thread::sleep_for(std::chrono::seconds(serveSeconds));
        server.Stop();
        UltraNet_Shutdown();
        return 0;
    }

    const int rc = ultranet_apistatus::Run(options);

    ultranet_apistatus::LoopbackServer::Instance().Stop();
    UltraNet_Shutdown();
    return rc;
}

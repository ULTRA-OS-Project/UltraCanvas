// Tests/UltraRTC/ApiStatus/ApiStatusMain.cpp
// Entry point of UltraRtcApiStatus: walks the public UltraRTC surface and
// prints, per function, whether it is WORKING here, merely IMPLEMENTED, NOT
// IMPLEMENTED in this build, or BROKEN.
// Version: 0.1.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraRtcApiStatus.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

void PrintUsage(const char* argv0) {
    std::printf(
        "UltraRtcApiStatus - public API status report for the UltraRTC module\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "  --format=text|markdown|json  Report format (default: text)\n"
        "  --area=<name>                Only probe one area (Lifecycle, Peers,\n"
        "                               DataChannels, Audio, Video)\n"
        "  --output=<path>              Write the report to a file instead of stdout\n"
        "  --strict                     Exit non-zero unless every entry is WORKING\n"
        "  -h, --help                   This message\n"
        "\n"
        "Statuses:\n"
        "  WORKING          the probe drove the real code path between two peers of\n"
        "                   this process and the result matched what the API promises\n"
        "  IMPLEMENTED      the implementation exists and was reached, but this\n"
        "                   machine cannot confirm the behaviour (no microphone,\n"
        "                   no camera, nothing to hear a speaker with)\n"
        "  NOT IMPLEMENTED  this build lacks what the function needs: the engine\n"
        "                   (OpenSSL), Opus, or VP8\n"
        "  BROKEN           the probe ran and the result contradicted the API\n"
        "\n"
        "Needs no network service: the peers connect over host candidates.\n"
        "\n"
        "Exit code: 0 when nothing is BROKEN (and, with --strict, when every\n"
        "entry is WORKING); 1 otherwise; 2 on a usage error.\n",
        argv0);
}

bool StartsWith(const char* s, const char* prefix) {
    return std::strncmp(s, prefix, std::strlen(prefix)) == 0;
}

} // namespace

int main(int argc, char** argv) {
    ultrartc_apistatus::RunOptions options;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            PrintUsage(argv[0]);
            return 0;
        }
        if (std::strcmp(arg, "--strict") == 0) { options.strict = true; continue; }
        if (StartsWith(arg, "--format=")) { options.format = arg + 9; continue; }
        if (StartsWith(arg, "--area="))   { options.areaFilter = arg + 7; continue; }
        if (StartsWith(arg, "--output=")) { options.outputPath = arg + 9; continue; }
        std::fprintf(stderr, "unknown option: %s\n\n", arg);
        PrintUsage(argv[0]);
        return 2;
    }
    if (options.format != "text" && options.format != "markdown" && options.format != "json") {
        std::fprintf(stderr, "unknown format: %s\n", options.format.c_str());
        return 2;
    }

    ultrartc_apistatus::ReportIdentity identity;
    identity.title = "UltraRTC API status";
    identity.areaOrder = {"Lifecycle", "Peers", "DataChannels", "Audio", "Video"};
    identity.networkEnvVar = "ULTRARTC_PROBE_NETWORK";      // no probe needs the internet yet
    ultrartc_apistatus::SetReportIdentity(identity);

    // What is being probed, on stderr so --format=json > file stays JSON.
    const UltraRtcCapabilities caps = UltraRtc_GetCapabilities();
    std::fprintf(stderr, "UltraRTC API %s, engine: %s\n", UltraRtc_GetVersion().c_str(),
                 caps.available ? caps.engine.c_str() : "none in this build");

    const int rc = ultrartc_apistatus::Run(options);

    ultrartc_apistatus::ClosePairs();
    UltraRtc_Shutdown();
    return rc;
}

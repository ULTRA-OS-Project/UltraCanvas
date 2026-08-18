// core/UltraWin/UltraWinComponents.cpp
// Component installation — VC++ runtimes, fonts, .NET, DXVK — for
// environments, wrapped around a spawned winetricks (never linked, exactly
// like Wine itself). Component names are winetricks verbs; installed
// components are read back from the winetricks log in the prefix.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWinInternal.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <unistd.h>

namespace fs = std::filesystem;

namespace ultrawin_internal {

bool IsValidComponentName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    unsigned char first = static_cast<unsigned char>(name.front());
    if (!std::islower(first) && !std::isdigit(first)) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::islower(c) || std::isdigit(c) || c == '.' || c == '_' ||
               c == '+' || c == '=' || c == '-';
    });
}

static bool IsElfFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    char magic[4] = {};
    f.read(magic, 4);
    return f.gcount() == 4 && magic[0] == 0x7f && magic[1] == 'E' &&
           magic[2] == 'L' && magic[3] == 'F';
}

std::string ResolveWineElfBinary(const std::string& winePath) {
    std::error_code ec;
    fs::path real = fs::canonical(winePath, ec);
    if (ec) return winePath;
    if (IsElfFile(real.string())) return real.string();

    // A script wrapper: try the loader locations wrappers point at,
    // strictly relative to the wrapper so a custom-configured wine never
    // resolves to a different install (Debian/Ubuntu: /usr/bin wrapper,
    // loader at ../lib/wine/wine64).
    const fs::path dir = real.parent_path();
    for (const fs::path candidate :
         {dir / "wine64", dir / ".." / "lib" / "wine" / "wine64",
          dir / ".." / "lib" / "wine" / "wine"}) {
        fs::path resolved = fs::canonical(candidate, ec);
        if (!ec && IsElfFile(resolved.string())) return resolved.string();
    }
    return winePath;
}

std::string FindWinetricksBinary() {
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (!g_config.winetricksPath.empty()) {
            return access(g_config.winetricksPath.c_str(), X_OK) == 0
                       ? g_config.winetricksPath
                       : std::string();
        }
    }
    const char* pathEnv = std::getenv("PATH");
    std::istringstream dirs(pathEnv ? pathEnv : "");
    std::string dir;
    while (std::getline(dirs, dir, ':')) {
        if (dir.empty()) continue;
        std::string full = dir + "/winetricks";
        if (access(full.c_str(), X_OK) == 0) return full;
    }
    return {};
}

}  // namespace ultrawin_internal

// ===========================================================================
// Public entry points
// ===========================================================================

using namespace ultrawin_internal;

UltraWinResult UltraWin_InstallComponent(const std::string& environment,
                                         const std::string& component) {
    if (!UltraWin_IsInitialized())
        return UltraWinResult::Error(UltraWinResultCode::NotInitialized,
                                     "call UltraWin_Initialize first");
    if (!IsValidEnvironmentName(environment))
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     "invalid environment name: " +
                                         environment);
    if (!IsValidComponentName(component))
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     "invalid component name: " + component);

    std::string wine = FindWineBinary();
    if (wine.empty())
        return UltraWinResult::Error(UltraWinResultCode::WineNotFound,
                                     "no usable wine binary found");
    std::string winetricks = FindWinetricksBinary();
    if (winetricks.empty())
        return UltraWinResult::Error(
            UltraWinResultCode::WinetricksNotFound,
            "winetricks is not installed (e.g. 'sudo apt install "
            "winetricks')");

    if (!UltraWin_EnvironmentExists(environment)) {
        auto created = UltraWin_CreateEnvironment(environment);
        if (!created) return created;
    }
    std::string prefix = PrefixPath(environment);

    // WINE points winetricks at the configured binary rather than whatever
    // is first in PATH — resolved to the real ELF loader, because
    // winetricks' arch detection rejects distro script wrappers. Prompts
    // stay enabled (suppressPrompts=false): winetricks drives its
    // installers unattended itself via --unattended, and some verbs
    // (dotnet…) need the real mscoree path.
    UltraWinConfig cfg = UltraWin_GetConfig();
    const std::string installLog = prefix + "/ultrawin-install.log";
    int rc = RunWineCommand(winetricks, {"--unattended", component}, prefix,
                            /*suppressPrompts=*/false,
                            cfg.componentInstallTimeoutSeconds,
                            {{"WINE", ResolveWineElfBinary(wine)}},
                            installLog);
    if (rc != 0) {
        return UltraWinResult::Error(
            UltraWinResultCode::ComponentInstallFailed,
            (rc == -1 ? component + ": winetricks timed out or could not run"
                      : component + ": winetricks exited with code " +
                            std::to_string(rc)) +
                " (details: " + installLog + ")");
    }
    return UltraWinResult::Ok();
}

std::vector<std::string> UltraWin_ListComponents(
    const std::string& environment) {
    std::vector<std::string> out;
    if (!UltraWin_IsInitialized() || !IsValidEnvironmentName(environment))
        return out;
    std::ifstream log(PrefixPath(environment) + "/winetricks.log");
    if (!log) return out;
    std::string line;
    while (std::getline(log, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        // The log is append-only and may repeat a verb; keep the first
        // occurrence so the list reads in installation order.
        if (std::find(out.begin(), out.end(), line) == out.end())
            out.push_back(line);
    }
    return out;
}

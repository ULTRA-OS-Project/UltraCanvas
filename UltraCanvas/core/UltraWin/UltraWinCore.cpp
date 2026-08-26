// core/UltraWin/UltraWinCore.cpp
// Module lifecycle, configuration, host-capability probing, and the shared
// pure helpers (name validation, mapping manifest, path resolution).
// Linux / ULTRA OS only.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWinInternal.h"
#include "UltraWinRdp.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ultrawin_internal {

std::mutex g_mutex;
bool g_initialized = false;
UltraWinConfig g_config;
std::unordered_map<UltraWinHandle, AppInstance> g_apps;

static std::string g_cachedWineVersion;   // guarded by g_mutex

// ---------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------

bool IsValidEnvironmentName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    if (name.front() == '.') return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '_' || c == '-';
    });
}

bool IsValidDriveLetter(char letter) {
    unsigned char c = static_cast<unsigned char>(letter);
    return std::isalpha(c) != 0;
}

char CanonicalDriveLetter(char letter) {
    return static_cast<char>(
        std::toupper(static_cast<unsigned char>(letter)));
}

std::vector<UltraWinFolderMapping> ParseMappingManifest(
    const std::string& text) {
    std::vector<UltraWinFolderMapping> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        // "L=/abs/path"
        if (line.size() < 3 || line[1] != '=') continue;
        if (!IsValidDriveLetter(line[0])) continue;
        std::string path = line.substr(2);
        if (path.empty() || path[0] != '/') continue;
        UltraWinFolderMapping m;
        m.driveLetter = CanonicalDriveLetter(line[0]);
        m.hostPath = path;
        // Later lines win over earlier ones for the same letter.
        auto dup = std::find_if(out.begin(), out.end(), [&](const auto& e) {
            return e.driveLetter == m.driveLetter;
        });
        if (dup != out.end())
            *dup = m;
        else
            out.push_back(m);
    }
    return out;
}

std::string SerializeMappingManifest(
    const std::vector<UltraWinFolderMapping>& mappings) {
    std::ostringstream out;
    out << "# UltraWin drive mappings — one DRIVELETTER=/absolute/host/path"
           " per line.\n";
    for (const auto& m : mappings) {
        if (!IsValidDriveLetter(m.driveLetter)) continue;
        if (m.hostPath.empty() || m.hostPath[0] != '/') continue;
        out << CanonicalDriveLetter(m.driveLetter) << '=' << m.hostPath
            << '\n';
    }
    return out.str();
}

std::string DefaultEnvironmentsRoot(const std::string& xdgDataHome,
                                    const std::string& home) {
    std::string base = !xdgDataHome.empty()
                           ? xdgDataHome
                           : (home.empty() ? "/tmp" : home + "/.local/share");
    return base + "/ultrawin/environments";
}

// ---------------------------------------------------------------------------
// State-aware helpers
// ---------------------------------------------------------------------------

static std::string EnvOrEmpty(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

std::string EnvironmentsRoot() {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_config.environmentsRoot.empty()) return g_config.environmentsRoot;
    return DefaultEnvironmentsRoot(EnvOrEmpty("XDG_DATA_HOME"),
                                   EnvOrEmpty("HOME"));
}

std::string PrefixPath(const std::string& name) {
    return EnvironmentsRoot() + "/" + name;
}

std::string FindWineBinary() {
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (!g_config.winePath.empty()) {
            return access(g_config.winePath.c_str(), X_OK) == 0
                       ? g_config.winePath
                       : std::string();
        }
    }
    std::string path = EnvOrEmpty("PATH");
    for (const char* candidate : {"wine", "wine64"}) {
        std::istringstream dirs(path);
        std::string dir;
        while (std::getline(dirs, dir, ':')) {
            if (dir.empty()) continue;
            std::string full = dir + "/" + candidate;
            if (access(full.c_str(), X_OK) == 0) return full;
        }
    }
    return {};
}

std::string ProbeWineVersion(const std::string& winePath) {
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (!g_cachedWineVersion.empty()) return g_cachedWineVersion;
    }
    if (winePath.empty()) return {};

    int fds[2];
    if (pipe(fds) != 0) return {};
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return {};
    }
    if (pid == 0) {
        // Child: wine --version prints a single line and exits; silence
        // stderr so a broken install doesn't spam the host app's console.
        dup2(fds[1], STDOUT_FILENO);
        close(fds[0]);
        close(fds[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, STDERR_FILENO);
        execl(winePath.c_str(), winePath.c_str(), "--version",
              static_cast<char*>(nullptr));
        _exit(127);
    }
    close(fds[1]);

    std::string out;
    char buf[256];
    // Bounded read: --version answers instantly; 5s covers cold caches.
    struct pollfd pfd{fds[0], POLLIN, 0};
    for (;;) {
        int pr = poll(&pfd, 1, 5000);
        if (pr <= 0) break;
        ssize_t n = read(fds[0], buf, sizeof buf);
        if (n <= 0) break;
        out.append(buf, static_cast<size_t>(n));
        if (out.size() > 4096) break;
    }
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return {};

    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    if (!out.empty()) {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_cachedWineVersion = out;
    }
    return out;
}

}  // namespace ultrawin_internal

// ===========================================================================
// Public entry points
// ===========================================================================

using namespace ultrawin_internal;

UltraWinResult UltraWin_Initialize(const UltraWinConfig& config) {
    if (config.homeDriveLetter != 0 &&
        !IsValidDriveLetter(config.homeDriveLetter)) {
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     "homeDriveLetter must be A..Z or 0");
    }
    std::lock_guard<std::mutex> lk(g_mutex);
    g_config = config;
    g_config.homeDriveLetter =
        config.homeDriveLetter == 0
            ? 0
            : CanonicalDriveLetter(config.homeDriveLetter);
    g_initialized = true;
    return UltraWinResult::Ok();
}

void UltraWin_Shutdown() {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_initialized) return;
    // Running apps are ordinary children of this process; they keep running
    // (documented behaviour) but their handles die with the registry.
    g_apps.clear();
    g_initialized = false;
}

bool UltraWin_IsInitialized() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_initialized;
}

UltraWinConfig UltraWin_GetConfig() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_config;
}

void UltraWin_SetConfig(const UltraWinConfig& config) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_config = config;
}

UltraWinCapabilities UltraWin_GetCapabilities() {
    UltraWinCapabilities caps;

    caps.winePath = FindWineBinary();
    caps.wineAvailable = !caps.winePath.empty();
    if (caps.wineAvailable)
        caps.wineVersion = ProbeWineVersion(caps.winePath);
    caps.wineTierAvailable = caps.wineAvailable;

    caps.winetricksPath = FindWinetricksBinary();
    caps.winetricksAvailable = !caps.winetricksPath.empty();

    caps.ntsyncAvailable = access("/dev/ntsync", F_OK) == 0;
    caps.kvmAvailable = access("/dev/kvm", R_OK | W_OK) == 0;

    caps.qemuPath = FindQemuBinary();
    caps.qemuAvailable =
        !caps.qemuPath.empty() && !FindQemuImgBinary().empty();
    caps.virtiofsdAvailable = !FindVirtiofsdBinary().empty();
    caps.remoteAppSupported = RdpBuiltIn();
    // Host-capable: QEMU present and hardware acceleration usable. Whether
    // the machine is provisioned/installed is UltraWin_VmGetInfo's business.
    caps.vmTierAvailable = caps.qemuAvailable && caps.kvmAvailable;

    struct utsname un{};
    if (uname(&un) == 0) caps.hostArchitecture = un.machine;
    return caps;
}

std::string UltraWin_GetVersion() { return "0.1.0"; }

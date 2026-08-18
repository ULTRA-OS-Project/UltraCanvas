// core/UltraWin/UltraWinEnvironment.cpp
// Environments = isolated Wine prefixes under the environments root, each
// with a persisted drive-mapping manifest. Mappings are re-asserted before
// every launch because Wine prefix updates recreate default dosdevices
// entries (notably z: -> /).
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWinInternal.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace ultrawin_internal {

static const char* kManifestName = "ultrawin-mappings.conf";

static std::string ManifestPath(const std::string& prefixPath) {
    return prefixPath + "/" + kManifestName;
}

static std::vector<UltraWinFolderMapping> LoadManifest(
    const std::string& prefixPath) {
    std::ifstream in(ManifestPath(prefixPath));
    if (!in) return {};
    std::stringstream buf;
    buf << in.rdbuf();
    return ParseMappingManifest(buf.str());
}

static bool SaveManifest(const std::string& prefixPath,
                         const std::vector<UltraWinFolderMapping>& mappings) {
    std::ofstream out(ManifestPath(prefixPath), std::ios::trunc);
    if (!out) return false;
    out << SerializeMappingManifest(mappings);
    return static_cast<bool>(out);
}

// A prefix is initialized once wineboot has written its registry.
static bool PrefixInitialized(const std::string& prefixPath) {
    return fs::exists(prefixPath + "/system.reg");
}

int RunWineCommand(const std::string& binaryPath,
                   const std::vector<std::string>& args,
                   const std::string& prefixPath,
                   bool suppressPrompts,
                   int timeoutSeconds,
                   const std::vector<std::pair<std::string, std::string>>&
                       extraEnv,
                   const std::string& outputFile) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        setenv("WINEPREFIX", prefixPath.c_str(), 1);
        if (suppressPrompts) {
            // No mono/gecko download dialogs during unattended prefix work.
            setenv("WINEDLLOVERRIDES", "mscoree,mshtml=", 1);
        }
        for (const auto& [key, value] : extraEnv)
            setenv(key.c_str(), value.c_str(), 1);
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) dup2(devnull, STDIN_FILENO);
        int out = outputFile.empty()
                      ? devnull
                      : open(outputFile.c_str(),
                             O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out >= 0) {
            dup2(out, STDOUT_FILENO);
            dup2(out, STDERR_FILENO);
        }
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(binaryPath.c_str()));
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execv(binaryPath.c_str(), argv.data());
        _exit(127);
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(timeoutSeconds);
    for (;;) {
        int status = 0;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }
        if (r < 0) return -1;
        if (timeoutSeconds > 0 &&
            std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return -1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

UltraWinResult ApplyMappings(const std::string& prefixPath) {
    UltraWinConfig cfg;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        cfg = g_config;
    }
    fs::path dosdevices = fs::path(prefixPath) / "dosdevices";
    std::error_code ec;
    fs::create_directories(dosdevices, ec);
    if (ec) {
        return UltraWinResult::Error(UltraWinResultCode::DriveMappingFailed,
                                     "cannot create dosdevices: " +
                                         ec.message());
    }

    auto assertLink = [&](char letter, const std::string& target)
        -> UltraWinResult {
        std::string link =
            std::string(1, static_cast<char>(
                                std::tolower(static_cast<unsigned char>(
                                    letter)))) +
            ":";
        fs::path linkPath = dosdevices / link;
        std::error_code lec;
        if (fs::is_symlink(linkPath, lec)) fs::remove(linkPath, lec);
        fs::create_directory_symlink(target, linkPath, lec);
        if (lec) {
            return UltraWinResult::Error(
                UltraWinResultCode::DriveMappingFailed,
                "mapping " + link + " -> " + target + ": " + lec.message());
        }
        return UltraWinResult::Ok();
    };

    // 1. Configured home mapping (the unified U: drive).
    if (cfg.homeDriveLetter != 0) {
        const char* home = std::getenv("HOME");
        if (home && *home) {
            auto r = assertLink(cfg.homeDriveLetter, home);
            if (!r) return r;
        }
    }

    // 2. Per-environment manifest mappings.
    for (const auto& m : LoadManifest(prefixPath)) {
        if (CanonicalDriveLetter(m.driveLetter) == 'C') continue;  // drive_c
        auto r = assertLink(m.driveLetter, m.hostPath);
        if (!r) return r;
    }

    // 3. Root-drive policy: Wine's default z: -> / exposes the whole host
    //    filesystem; drop it unless explicitly requested.
    if (!cfg.exposeRootDrive) {
        std::error_code lec;
        fs::path z = dosdevices / "z:";
        if (fs::is_symlink(z, lec)) fs::remove(z, lec);
    }
    return UltraWinResult::Ok();
}

// Environments with running apps cannot be deleted.
static bool EnvironmentBusyLocked(const std::string& name) {
    for (const auto& [h, inst] : g_apps) {
        (void)h;
        if (inst.info.environment != name) continue;
        if (inst.info.state == UltraWinAppState::Starting ||
            inst.info.state == UltraWinAppState::Running)
            return true;
    }
    return false;
}

}  // namespace ultrawin_internal

// ===========================================================================
// Public entry points
// ===========================================================================

using namespace ultrawin_internal;

UltraWinResult UltraWin_CreateEnvironment(const std::string& name) {
    if (!UltraWin_IsInitialized())
        return UltraWinResult::Error(UltraWinResultCode::NotInitialized,
                                     "call UltraWin_Initialize first");
    if (!IsValidEnvironmentName(name))
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     "invalid environment name: " + name);

    std::string wine = FindWineBinary();
    if (wine.empty())
        return UltraWinResult::Error(UltraWinResultCode::WineNotFound,
                                     "no usable wine binary found");

    std::string prefix = PrefixPath(name);
    if (fs::exists(prefix))
        return UltraWinResult::Error(UltraWinResultCode::EnvironmentExists,
                                     name + " already exists");

    std::error_code ec;
    fs::create_directories(prefix, ec);
    if (ec)
        return UltraWinResult::Error(UltraWinResultCode::EnvironmentCreateFailed,
                                     "mkdir failed: " + ec.message());

    UltraWinConfig cfg = UltraWin_GetConfig();
    int rc = RunWineCommand(wine, {"wineboot", "--init"}, prefix,
                            cfg.suppressWinePrompts,
                            cfg.environmentCreateTimeoutSeconds);
    if (rc != 0) {
        fs::remove_all(prefix, ec);
        return UltraWinResult::Error(
            UltraWinResultCode::EnvironmentCreateFailed,
            rc == -1 ? "wineboot --init timed out or could not run"
                     : "wineboot --init exited with code " +
                           std::to_string(rc));
    }

    // wineboot returns before wineserver has flushed the prefix registry to
    // disk (it lingers briefly after the last client exits and saves
    // system.reg on shutdown). Wait for it: `wineserver -w` when the binary
    // sits next to wine, then a bounded poll for the registry file.
    std::string wineserver =
        (fs::path(wine).parent_path() / "wineserver").string();
    if (access(wineserver.c_str(), X_OK) == 0) {
        RunWineCommand(wineserver, {"-w"}, prefix, false,
                       cfg.environmentCreateTimeoutSeconds);
    }
    for (int i = 0; i < 100 && !PrefixInitialized(prefix); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!PrefixInitialized(prefix)) {
        fs::remove_all(prefix, ec);
        return UltraWinResult::Error(
            UltraWinResultCode::EnvironmentCreateFailed,
            "prefix registry was not created by wineboot");
    }

    SaveManifest(prefix, {});
    return ApplyMappings(prefix);
}

UltraWinResult UltraWin_DeleteEnvironment(const std::string& name) {
    if (!UltraWin_IsInitialized())
        return UltraWinResult::Error(UltraWinResultCode::NotInitialized,
                                     "call UltraWin_Initialize first");
    if (!IsValidEnvironmentName(name))
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     "invalid environment name: " + name);
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (EnvironmentBusyLocked(name))
            return UltraWinResult::Error(
                UltraWinResultCode::EnvironmentBusy,
                name + " has running applications");
    }
    std::string prefix = PrefixPath(name);
    if (!fs::exists(prefix))
        return UltraWinResult::Error(UltraWinResultCode::EnvironmentNotFound,
                                     name);
    std::error_code ec;
    fs::remove_all(prefix, ec);
    if (ec)
        return UltraWinResult::Error(UltraWinResultCode::EnvironmentDeleteFailed,
                                     ec.message());
    return UltraWinResult::Ok();
}

std::vector<UltraWinEnvironmentInfo> UltraWin_ListEnvironments() {
    std::vector<UltraWinEnvironmentInfo> out;
    if (!UltraWin_IsInitialized()) return out;
    std::error_code ec;
    fs::directory_iterator it(EnvironmentsRoot(), ec);
    if (ec) return out;
    for (const auto& entry : it) {
        if (!entry.is_directory(ec)) continue;
        std::string name = entry.path().filename().string();
        if (!IsValidEnvironmentName(name)) continue;
        UltraWinEnvironmentInfo info;
        info.name = name;
        info.prefixPath = entry.path().string();
        info.initialized = PrefixInitialized(info.prefixPath);
        out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });
    return out;
}

bool UltraWin_EnvironmentExists(const std::string& name) {
    if (!UltraWin_IsInitialized() || !IsValidEnvironmentName(name))
        return false;
    return fs::exists(PrefixPath(name));
}

UltraWinResult UltraWin_MapFolder(const std::string& environment,
                                  char driveLetter,
                                  const std::string& hostPath) {
    if (!UltraWin_IsInitialized())
        return UltraWinResult::Error(UltraWinResultCode::NotInitialized,
                                     "call UltraWin_Initialize first");
    if (!IsValidEnvironmentName(environment))
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     "invalid environment name");
    if (!IsValidDriveLetter(driveLetter))
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     "drive letter must be A..Z");
    char letter = CanonicalDriveLetter(driveLetter);
    UltraWinConfig cfg = UltraWin_GetConfig();
    if (letter == 'C' || letter == cfg.homeDriveLetter)
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     std::string("drive ") + letter +
                                         ": is reserved");
    if (hostPath.empty() || hostPath[0] != '/')
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     "hostPath must be absolute");
    if (!fs::is_directory(hostPath))
        return UltraWinResult::Error(UltraWinResultCode::FileNotFound,
                                     hostPath + " is not a directory");
    std::string prefix = PrefixPath(environment);
    if (!fs::exists(prefix))
        return UltraWinResult::Error(UltraWinResultCode::EnvironmentNotFound,
                                     environment);

    auto mappings = LoadManifest(prefix);
    auto existing =
        std::find_if(mappings.begin(), mappings.end(), [&](const auto& m) {
            return m.driveLetter == letter;
        });
    if (existing != mappings.end())
        existing->hostPath = hostPath;
    else
        mappings.push_back({letter, hostPath});
    if (!SaveManifest(prefix, mappings))
        return UltraWinResult::Error(UltraWinResultCode::IoError,
                                     "cannot write mapping manifest");
    return ApplyMappings(prefix);
}

UltraWinResult UltraWin_UnmapFolder(const std::string& environment,
                                    char driveLetter) {
    if (!UltraWin_IsInitialized())
        return UltraWinResult::Error(UltraWinResultCode::NotInitialized,
                                     "call UltraWin_Initialize first");
    if (!IsValidEnvironmentName(environment))
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     "invalid environment name");
    if (!IsValidDriveLetter(driveLetter))
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     "drive letter must be A..Z");
    char letter = CanonicalDriveLetter(driveLetter);
    std::string prefix = PrefixPath(environment);
    if (!fs::exists(prefix))
        return UltraWinResult::Error(UltraWinResultCode::EnvironmentNotFound,
                                     environment);

    auto mappings = LoadManifest(prefix);
    auto n = mappings.size();
    mappings.erase(std::remove_if(mappings.begin(), mappings.end(),
                                  [&](const auto& m) {
                                      return m.driveLetter == letter;
                                  }),
                   mappings.end());
    if (mappings.size() == n)
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     std::string("drive ") + letter +
                                         ": is not mapped");
    if (!SaveManifest(prefix, mappings))
        return UltraWinResult::Error(UltraWinResultCode::IoError,
                                     "cannot write mapping manifest");
    std::error_code ec;
    fs::remove(fs::path(prefix) / "dosdevices" /
                   (std::string(1, static_cast<char>(std::tolower(
                                       static_cast<unsigned char>(letter)))) +
                    ":"),
               ec);
    return UltraWinResult::Ok();
}

std::vector<UltraWinFolderMapping> UltraWin_ListMappings(
    const std::string& environment) {
    if (!UltraWin_IsInitialized() || !IsValidEnvironmentName(environment))
        return {};
    std::string prefix = PrefixPath(environment);
    if (!fs::exists(prefix)) return {};
    return LoadManifest(prefix);
}

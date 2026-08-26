// core/UltraWin/UltraWinApp.cpp
// Launching and supervising Windows applications through the Wine tier.
// Each launch is a fork/exec of `wine <exe> [args...]` in its own process
// group; state is refreshed with non-blocking waitpid so no SIGCHLD
// handler is installed (the host application owns its signal setup).
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWinInternal.h"
#include "UltraWinRdp.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace ultrawin_internal;

namespace {

std::atomic<uint64_t> g_nextHandle{1};

// Non-blocking exit-status collection. Caller holds g_mutex. Wine-tier
// instances are child processes (waitpid); VM-tier instances live and die
// with their RemoteApp session.
void RefreshLocked(AppInstance& inst) {
    auto& info = inst.info;
    if (info.state != UltraWinAppState::Starting &&
        info.state != UltraWinAppState::Running)
        return;
    if (info.tier == UltraWinTier::Vm) {
        if (!inst.session) {
            info.state = UltraWinAppState::Failed;
            return;
        }
        switch (inst.session->State()) {
            case RdpSessionState::Connected:
                info.state = UltraWinAppState::Running;
                break;
            case RdpSessionState::Failed:
                info.state = UltraWinAppState::Failed;
                break;
            case RdpSessionState::Idle:
            case RdpSessionState::Disconnected:
                info.state = inst.closeRequested
                                 ? UltraWinAppState::Terminated
                                 : UltraWinAppState::Exited;
                break;
        }
        return;
    }
    if (inst.reaped) return;
    int status = 0;
    pid_t r = waitpid(static_cast<pid_t>(info.processId), &status, WNOHANG);
    if (r == 0) {
        info.state = UltraWinAppState::Running;
        return;
    }
    inst.reaped = true;
    if (r < 0) {
        // Child already gone (or reaped elsewhere) — the best remaining
        // classification is a plain exit with unknown code.
        info.state = UltraWinAppState::Exited;
        return;
    }
    if (WIFEXITED(status)) {
        info.exitCode = WEXITSTATUS(status);
        // 127 is our exec-failure sentinel from the fork child.
        info.state = info.exitCode == 127 ? UltraWinAppState::Failed
                                          : UltraWinAppState::Exited;
    } else if (WIFSIGNALED(status)) {
        info.state = UltraWinAppState::Terminated;
    }
}

UltraWinResult SignalApp(UltraWinHandle app, int sig) {
    std::shared_ptr<RdpSession> sessionToEnd;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto it = g_apps.find(app);
        if (it == g_apps.end())
            return UltraWinResult::Error(UltraWinResultCode::InvalidHandle,
                                         "unknown application handle");
        RefreshLocked(it->second);
        auto& info = it->second.info;
        if (info.state != UltraWinAppState::Starting &&
            info.state != UltraWinAppState::Running)
            return UltraWinResult::Ok();  // already ended — nothing to do
        if (info.tier == UltraWinTier::Vm) {
            // Ending the RemoteApp session ends the app's presence here;
            // Disconnect joins the pump thread, so run it off the lock.
            it->second.closeRequested = true;
            sessionToEnd = it->second.session;
        } else {
            // Whole process group: wine + the app's own child processes.
            if (kill(static_cast<pid_t>(-info.processId), sig) != 0 &&
                kill(static_cast<pid_t>(info.processId), sig) != 0)
                return UltraWinResult::Error(UltraWinResultCode::ProcessError,
                                             "signal delivery failed");
        }
    }
    if (sessionToEnd) sessionToEnd->Disconnect();
    return UltraWinResult::Ok();
}

// VM-tier launch: one RemoteApp (RAIL) session per application against the
// guest's forwarded RDP port. Stage 2b accepts GUEST paths ("C:\...", or a
// "||alias" the guest's RemoteApp table defines) — host paths need the
// shared-folder mapping that arrives with virtiofs integration.
UltraWinResult RunAppVmTier(const std::string& executablePath,
                            const UltraWinRunOptions& options,
                            UltraWinHandle* outHandle) {
    if (!RdpBuiltIn())
        return UltraWinResult::Error(
            UltraWinResultCode::NotSupported,
            "this build has no FreeRDP RemoteApp client");
    const bool guestPath =
        executablePath.size() >= 3 &&
        std::isalpha(static_cast<unsigned char>(executablePath[0])) &&
        executablePath[1] == ':' && executablePath[2] == '\\';
    const bool alias = executablePath.rfind("||", 0) == 0;
    std::string program = executablePath;
    if (!guestPath && !alias) {
        // Host path: reachable in the guest only through the shared home
        // (virtiofs, mounted as the unified home drive).
        UltraWinConfig pathCfg = UltraWin_GetConfig();
        const char* home = std::getenv("HOME");
        program = HostToGuestPath(executablePath, home ? home : "",
                                  pathCfg.homeDriveLetter);
        if (program.empty())
            return UltraWinResult::Error(
                UltraWinResultCode::InvalidArgument,
                "VM tier accepts guest paths (C:\\...), ||aliases, or host "
                "paths under your home directory (shared into the guest)");
    }
    if (UltraWin_VmGetState() != UltraWinVmState::Running)
        return UltraWinResult::Error(UltraWinResultCode::VmNotRunning,
                                     "start the machine first "
                                     "(UltraWin_VmStart)");

    UltraWinConfig cfg = UltraWin_GetConfig();
    RdpSessionOptions rdp;
    rdp.host = "127.0.0.1";
    rdp.port = cfg.vmRdpHostPort > 0 ? cfg.vmRdpHostPort : 13389;
    rdp.username = cfg.vmGuestUsername;
    rdp.password = cfg.vmGuestPassword;
    rdp.remoteApp = true;
    rdp.remoteAppProgram = program;
    for (const auto& a : options.arguments) {
        if (!rdp.remoteAppArgs.empty()) rdp.remoteAppArgs += ' ';
        rdp.remoteAppArgs += a;
    }
    auto session = std::make_shared<RdpSession>();
    if (!session->Connect(rdp))
        return UltraWinResult::Error(UltraWinResultCode::LaunchFailed,
                                     session->LastError());

    UltraWinHandle handle = g_nextHandle.fetch_add(1);
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        AppInstance inst;
        inst.info.handle = handle;
        inst.info.executablePath = executablePath;
        inst.info.tier = UltraWinTier::Vm;
        inst.info.state = UltraWinAppState::Running;
        inst.session = std::move(session);
        g_apps.emplace(handle, std::move(inst));
    }
    *outHandle = handle;
    return UltraWinResult::Ok();
}

}  // namespace

UltraWinResult UltraWin_RunApp(const std::string& executablePath,
                               const UltraWinRunOptions& options,
                               UltraWinHandle* outHandle) {
    if (outHandle) *outHandle = UltraWinInvalidHandle;
    if (!UltraWin_IsInitialized())
        return UltraWinResult::Error(UltraWinResultCode::NotInitialized,
                                     "call UltraWin_Initialize first");
    if (!outHandle)
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     "outHandle is required");
    if (options.forceTier == UltraWinTier::Vm)
        return RunAppVmTier(executablePath, options, outHandle);
    if (executablePath.empty() || executablePath[0] != '/')
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     "executablePath must be absolute");
    if (access(executablePath.c_str(), F_OK) != 0)
        return UltraWinResult::Error(UltraWinResultCode::FileNotFound,
                                     executablePath);

    std::string wine = FindWineBinary();
    if (wine.empty())
        return UltraWinResult::Error(UltraWinResultCode::WineNotFound,
                                     "no usable wine binary found");

    std::string envName =
        options.environment.empty() ? "Default" : options.environment;
    if (!IsValidEnvironmentName(envName))
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     "invalid environment name: " + envName);
    if (!UltraWin_EnvironmentExists(envName)) {
        auto created = UltraWin_CreateEnvironment(envName);
        if (!created) return created;
    }
    std::string prefix = PrefixPath(envName);

    // Mappings first — Wine updates recreate default dosdevices entries.
    auto mapped = ApplyMappings(prefix);
    if (!mapped) return mapped;

    std::string workdir = options.workingDirectory.empty()
                              ? fs::path(executablePath).parent_path().string()
                              : options.workingDirectory;

    // Extension routing: installers and Start-Menu shortcuts need a Wine
    // helper in front of the file; plain executables run directly.
    std::string ext = fs::path(executablePath).extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(
                              static_cast<unsigned char>(c)));
    std::vector<std::string> wineArgs;
    if (ext == ".msi")
        wineArgs = {"msiexec", "/i", executablePath};
    else if (ext == ".lnk")
        wineArgs = {"start", "/wait", "/unix", executablePath};
    else
        wineArgs = {executablePath};

    pid_t pid = fork();
    if (pid < 0)
        return UltraWinResult::Error(UltraWinResultCode::LaunchFailed,
                                     "fork failed");
    if (pid == 0) {
        setpgid(0, 0);  // own group, so Close/Kill reach the app's children
        setenv("WINEPREFIX", prefix.c_str(), 1);
        for (const auto& [k, v] : options.environmentVariables)
            setenv(k.c_str(), v.c_str(), 1);
        if (!workdir.empty() && chdir(workdir.c_str()) != 0) {
            _exit(127);
        }
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) dup2(devnull, STDIN_FILENO);
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(wine.c_str()));
        for (const auto& a : wineArgs)
            argv.push_back(const_cast<char*>(a.c_str()));
        for (const auto& a : options.arguments)
            argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execv(wine.c_str(), argv.data());
        _exit(127);
    }

    UltraWinHandle handle = g_nextHandle.fetch_add(1);
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        AppInstance inst;
        inst.info.handle = handle;
        inst.info.executablePath = executablePath;
        inst.info.environment = envName;
        inst.info.tier = UltraWinTier::Wine;
        inst.info.state = UltraWinAppState::Running;
        inst.info.processId = pid;
        g_apps.emplace(handle, std::move(inst));
    }
    *outHandle = handle;
    return UltraWinResult::Ok();
}

UltraWinResult UltraWin_CloseApp(UltraWinHandle app) {
    return SignalApp(app, SIGTERM);
}

UltraWinResult UltraWin_KillApp(UltraWinHandle app) {
    return SignalApp(app, SIGKILL);
}

UltraWinResult UltraWin_GetAppInfo(UltraWinHandle app, UltraWinAppInfo* out) {
    if (!out)
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     "out is required");
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_apps.find(app);
    if (it == g_apps.end())
        return UltraWinResult::Error(UltraWinResultCode::InvalidHandle,
                                     "unknown application handle");
    RefreshLocked(it->second);
    *out = it->second.info;
    return UltraWinResult::Ok();
}

UltraWinAppState UltraWin_GetAppState(UltraWinHandle app) {
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_apps.find(app);
    if (it == g_apps.end()) return UltraWinAppState::Failed;
    RefreshLocked(it->second);
    return it->second.info.state;
}

std::vector<UltraWinAppInfo> UltraWin_ListApps() {
    std::vector<UltraWinAppInfo> out;
    std::lock_guard<std::mutex> lk(g_mutex);
    out.reserve(g_apps.size());
    for (auto& [h, inst] : g_apps) {
        (void)h;
        RefreshLocked(inst);
        out.push_back(inst.info);
    }
    return out;
}

UltraWinResult UltraWin_WaitApp(UltraWinHandle app, int timeoutMilliseconds,
                                int* outExitCode) {
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeoutMilliseconds);
    for (;;) {
        {
            std::lock_guard<std::mutex> lk(g_mutex);
            auto it = g_apps.find(app);
            if (it == g_apps.end())
                return UltraWinResult::Error(UltraWinResultCode::InvalidHandle,
                                             "unknown application handle");
            RefreshLocked(it->second);
            const auto& info = it->second.info;
            if (info.state != UltraWinAppState::Starting &&
                info.state != UltraWinAppState::Running) {
                if (outExitCode) *outExitCode = info.exitCode;
                return UltraWinResult::Ok();
            }
        }
        if (timeoutMilliseconds > 0 &&
            std::chrono::steady_clock::now() >= deadline)
            return UltraWinResult::Error(UltraWinResultCode::Timeout,
                                         "application still running");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

UltraWinResult UltraWin_ReleaseApp(UltraWinHandle app) {
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_apps.find(app);
    if (it == g_apps.end())
        return UltraWinResult::Error(UltraWinResultCode::InvalidHandle,
                                     "unknown application handle");
    RefreshLocked(it->second);
    const auto& state = it->second.info.state;
    if (state == UltraWinAppState::Starting ||
        state == UltraWinAppState::Running)
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     "application is still running");
    g_apps.erase(it);
    return UltraWinResult::Ok();
}

std::vector<UltraWinProgramInfo> UltraWin_ListPrograms(
    const std::string& environment) {
    std::vector<UltraWinProgramInfo> out;
    if (!UltraWin_IsInitialized() || !IsValidEnvironmentName(environment))
        return out;
    const fs::path driveC = fs::path(PrefixPath(environment)) / "drive_c";
    const fs::path menuTail =
        fs::path("Microsoft") / "Windows" / "Start Menu" / "Programs";

    // The all-users menu plus every profile's per-user menu.
    std::vector<fs::path> roots = {driveC / "ProgramData" / menuTail};
    std::error_code ec;
    for (fs::directory_iterator user(driveC / "users", ec), end;
         !ec && user != end; user.increment(ec)) {
        roots.push_back(user->path() / "AppData" / "Roaming" / menuTail);
    }

    for (const auto& root : roots) {
        std::error_code rec;
        fs::recursive_directory_iterator it(root, rec), end;
        for (; !rec && it != end; it.increment(rec)) {
            if (!it->is_regular_file(rec)) continue;
            std::string ext = it->path().extension().string();
            for (char& c : ext)
                c = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".lnk") continue;
            UltraWinProgramInfo info;
            info.name = it->path().stem().string();
            info.category =
                fs::relative(it->path().parent_path(), root, rec).string();
            if (info.category == ".") info.category.clear();
            info.shortcutPath = it->path().string();
            info.environment = environment;
            out.push_back(std::move(info));
        }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });
    return out;
}

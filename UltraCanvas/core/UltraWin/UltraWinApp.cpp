// core/UltraWin/UltraWinApp.cpp
// Launching and supervising Windows applications through the Wine tier.
// Each launch is a fork/exec of `wine <exe> [args...]` in its own process
// group; state is refreshed with non-blocking waitpid so no SIGCHLD
// handler is installed (the host application owns its signal setup).
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWinInternal.h"

#include <atomic>
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

// Non-blocking exit-status collection. Caller holds g_mutex.
void RefreshLocked(AppInstance& inst) {
    auto& info = inst.info;
    if (inst.reaped) return;
    if (info.state != UltraWinAppState::Starting &&
        info.state != UltraWinAppState::Running)
        return;
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
    // Whole process group: wine + the app's own child processes.
    if (kill(static_cast<pid_t>(-info.processId), sig) != 0 &&
        kill(static_cast<pid_t>(info.processId), sig) != 0)
        return UltraWinResult::Error(UltraWinResultCode::ProcessError,
                                     "signal delivery failed");
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
        return UltraWinResult::Error(UltraWinResultCode::NotSupported,
                                     "VM tier is not available yet (Stage 2)");
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
        argv.push_back(const_cast<char*>(executablePath.c_str()));
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

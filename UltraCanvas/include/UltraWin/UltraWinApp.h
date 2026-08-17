// include/UltraWin/UltraWinApp.h
// Running Windows applications through UltraWin. Stage 1 launches through
// the Wine tier only; UltraWinRunOptions::forceTier keeps the surface
// stable for the Stage 2 VM tier. Windows apps appear as native windows —
// UltraWin never shows a Windows desktop.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraWinCore.h"

#include <map>

enum class UltraWinAppState {
    Starting,    // process spawned, not yet confirmed running
    Running,
    Exited,      // exited on its own; see exitCode
    Terminated,  // ended by CloseApp/KillApp
    Failed       // could not start (exec failure, missing binary, ...)
};

struct UltraWinRunOptions {
    // Environment (Wine prefix) to run in. Empty = "Default", created on
    // first use. Per-app environments are recommended for isolation.
    std::string environment;

    // Command-line arguments passed to the application.
    std::vector<std::string> arguments;

    // Working directory for the application (host path). Empty = the
    // directory containing the executable.
    std::string workingDirectory;

    // Extra environment variables for the launched process (e.g. a DXVK
    // switch). Applied on top of the standard WINEPREFIX/... variables.
    std::map<std::string, std::string> environmentVariables;

    // Tier selection. Stage 1 supports Auto (== Wine) and Wine; Vm returns
    // NotSupported until Stage 2.
    UltraWinTier forceTier = UltraWinTier::Auto;
};

struct UltraWinAppInfo {
    UltraWinHandle handle = UltraWinInvalidHandle;
    std::string executablePath;   // host path of the .exe
    std::string environment;
    UltraWinTier tier = UltraWinTier::Wine;
    UltraWinAppState state = UltraWinAppState::Starting;
    int exitCode = -1;            // valid once state == Exited
    int64_t processId = 0;        // host pid of the wine process
};

// Launch a Windows executable (host path to the .exe). Returns immediately
// after the spawn; use UltraWin_GetAppState / UltraWin_WaitApp to follow the
// application. On success *outHandle identifies the instance.
UltraWinResult UltraWin_RunApp(const std::string& executablePath,
                               const UltraWinRunOptions& options,
                               UltraWinHandle* outHandle);

// Ask the application to end (SIGTERM to the wine process — Wine forwards
// this as a graceful shutdown to the app). KillApp is immediate (SIGKILL).
UltraWinResult UltraWin_CloseApp(UltraWinHandle app);
UltraWinResult UltraWin_KillApp(UltraWinHandle app);

// Snapshot of one instance / all instances launched in this session.
// Instances that have ended stay listed (state Exited/Terminated/Failed)
// until UltraWin_ReleaseApp or UltraWin_Shutdown.
UltraWinResult UltraWin_GetAppInfo(UltraWinHandle app, UltraWinAppInfo* out);
UltraWinAppState UltraWin_GetAppState(UltraWinHandle app);
std::vector<UltraWinAppInfo> UltraWin_ListApps();

// Block until the application ends or timeoutMilliseconds elapses
// (0 = wait forever). Returns Timeout if still running.
UltraWinResult UltraWin_WaitApp(UltraWinHandle app, int timeoutMilliseconds,
                                int* outExitCode);

// Forget an ended instance (frees its slot). Fails with InvalidArgument
// while the application is still running.
UltraWinResult UltraWin_ReleaseApp(UltraWinHandle app);

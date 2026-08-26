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

// Launch a Windows program (host path). Routed by extension:
//   .exe (and anything else) — run directly
//   .msi                     — installed via `msiexec /i`
//   .lnk                     — resolved via `start /wait /unix` (Start-Menu
//                              shortcuts, see UltraWin_ListPrograms)
// Returns immediately after the spawn; use UltraWin_GetAppState /
// UltraWin_WaitApp to follow the application. On success *outHandle
// identifies the instance.
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

// ============================================================================
// Installed programs — the Start-Menu shortcuts an installer created inside
// an environment. This is what an ULTRA OS launcher shows so installed
// Windows programs are startable like native ones: pass shortcutPath to
// UltraWin_RunApp (routed through the .lnk path above).
// ============================================================================
struct UltraWinProgramInfo {
    std::string name;          // menu name (shortcut file name, no .lnk)
    std::string category;      // Start-Menu subfolder chain, "" at top level
    std::string shortcutPath;  // host path of the .lnk inside the prefix
    std::string environment;
};

// Start-Menu programs of one environment (both the all-users and per-user
// menus), sorted by name. Uninstaller shortcuts are included — filter by
// name where a launcher does not want them.
std::vector<UltraWinProgramInfo> UltraWin_ListPrograms(
    const std::string& environment);

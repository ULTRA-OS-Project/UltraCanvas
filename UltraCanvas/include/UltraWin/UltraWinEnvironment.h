// include/UltraWin/UltraWinEnvironment.h
// UltraWin environments — isolated Wine prefixes (Bottles-style), one per
// application by default. Each environment lives under the configured
// environments root and carries its own drive mappings, asserted before
// every launch (Wine prefix updates may recreate default mappings).
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraWinCore.h"

// A named environment. Names are restricted to [A-Za-z0-9._-]+ (no path
// separators — they become directory names under the environments root).
struct UltraWinEnvironmentInfo {
    std::string name;
    std::string prefixPath;   // absolute path of the Wine prefix directory
    bool initialized = false; // wineboot has completed for this prefix
};

// A drive-letter mapping inside an environment. Host paths must be absolute.
struct UltraWinFolderMapping {
    char driveLetter = 0;     // 'A'..'Z'
    std::string hostPath;
};

// Create a new environment: makes the prefix directory, runs
// `wineboot --init` (bounded by environmentCreateTimeoutSeconds), applies
// the standard mappings (home -> homeDriveLetter, Z: removed unless
// exposeRootDrive). Fails with EnvironmentExists if the name is taken and
// WineNotFound when no wine binary is available.
UltraWinResult UltraWin_CreateEnvironment(const std::string& name);

// Delete an environment and its prefix directory. Fails with
// EnvironmentBusy while applications launched from it are still running.
UltraWinResult UltraWin_DeleteEnvironment(const std::string& name);

// All environments under the environments root, sorted by name.
std::vector<UltraWinEnvironmentInfo> UltraWin_ListEnvironments();

bool UltraWin_EnvironmentExists(const std::string& name);

// Map / unmap a host folder as a drive letter inside an environment.
// Mappings persist in the environment's manifest and are re-asserted
// before every launch. The configured home mapping cannot be unmapped —
// change UltraWinConfig::homeDriveLetter instead.
UltraWinResult UltraWin_MapFolder(const std::string& environment,
                                  char driveLetter,
                                  const std::string& hostPath);
UltraWinResult UltraWin_UnmapFolder(const std::string& environment,
                                    char driveLetter);
std::vector<UltraWinFolderMapping> UltraWin_ListMappings(
    const std::string& environment);

// ============================================================================
// Components — runtime dependencies a Windows application may need inside
// its environment: VC++ runtimes ("vcrun2019"), core fonts ("corefonts"),
// .NET ("dotnet48"), Direct3D-to-Vulkan ("dxvk"), … Names are winetricks
// verbs; the installer is a spawned winetricks (probed like Wine —
// UltraWinCapabilities::winetricksAvailable).
// ============================================================================

// Install a component into an environment (created on first use, like
// UltraWin_RunApp). BLOCKING and potentially slow — components download
// from upstream mirrors (componentInstallTimeoutSeconds bounds one call);
// run it off the UI thread. Installing an already-installed component
// succeeds without re-downloading.
UltraWinResult UltraWin_InstallComponent(const std::string& environment,
                                         const std::string& component);

// Components recorded as installed in the environment (winetricks log),
// in installation order. Empty when none or the environment is unknown.
std::vector<std::string> UltraWin_ListComponents(
    const std::string& environment);

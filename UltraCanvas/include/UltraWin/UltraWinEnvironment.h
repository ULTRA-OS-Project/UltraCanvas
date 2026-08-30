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
// Associations — the remembered environment for programs living OUTSIDE
// any environment (portable apps, downloaded installers). Launchers ask
// once (a picker), store the answer here, and UltraWin_RunApp consults it
// automatically on later launches: explicit option → owning prefix →
// association → Default. Keyed by the program's absolute host path;
// persisted under the UltraWin data directory, shared by all launchers.
// ============================================================================

// The environment whose prefix physically contains hostPath (an installed
// program, its Start-Menu shortcut); "" when the path lies outside every
// environment. Launchers use it to know a program's environment is already
// decided — no need to ask.
std::string UltraWin_EnvironmentForPath(const std::string& hostPath);

// The stored environment for a program path; "" when none is stored.
std::string UltraWin_GetAssociation(const std::string& programPath);

// Remember / forget the environment for a program path. Setting validates
// the environment name but not its existence (it is created on first
// launch like everywhere else).
UltraWinResult UltraWin_SetAssociation(const std::string& programPath,
                                       const std::string& environment);
UltraWinResult UltraWin_RemoveAssociation(const std::string& programPath);

// What a picker should pre-select for a program: its own association,
// else the association of a sibling program in the same directory (the
// multi-exe application case), else the parent folder's name, else the
// file name — the last two sanitized into a valid environment name.
// Never empty for an absolute path.
std::string UltraWin_SuggestEnvironment(const std::string& programPath);

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

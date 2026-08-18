// core/UltraWin/UltraWinInternal.h
// Shared internals of the UltraWin Wine backend: module state, environment
// path resolution, name validation, mapping-manifest parsing, and process
// helpers. Not installed; include only from core/UltraWin/*.cpp and the
// UltraWin test suite.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraWin/UltraWin.h"

#include <mutex>
#include <unordered_map>
#include <utility>

namespace ultrawin_internal {

// ---------------------------------------------------------------------------
// Module state (defined in UltraWinCore.cpp). g_mutex guards everything
// below; process-wait bookkeeping in UltraWinApp.cpp takes it too.
// ---------------------------------------------------------------------------
extern std::mutex g_mutex;
extern bool g_initialized;
extern UltraWinConfig g_config;

struct AppInstance {
    UltraWinAppInfo info;   // info.processId is the wine child's pid
    bool reaped = false;    // waitpid() has collected the exit status
};

extern std::unordered_map<UltraWinHandle, AppInstance> g_apps;

// ---------------------------------------------------------------------------
// Pure helpers (no module state, no filesystem side effects) — unit-tested
// directly by Tests/UltraWin.
// ---------------------------------------------------------------------------

// Environment names become directory names: [A-Za-z0-9._-]+, no leading
// dot, at most 64 chars.
bool IsValidEnvironmentName(const std::string& name);

// 'A'..'Z' (case-insensitive input accepted, canonicalised to upper).
bool IsValidDriveLetter(char letter);
char CanonicalDriveLetter(char letter);

// One "L=/abs/path" mapping per line; '#' comments and blank lines skipped.
// Invalid lines are dropped (never fatal — the manifest is ours).
std::vector<UltraWinFolderMapping> ParseMappingManifest(
    const std::string& text);
std::string SerializeMappingManifest(
    const std::vector<UltraWinFolderMapping>& mappings);

// Default environments root for a given HOME/XDG_DATA_HOME pair (pure;
// the config-aware variant below applies the live process environment).
std::string DefaultEnvironmentsRoot(const std::string& xdgDataHome,
                                    const std::string& home);

// ---------------------------------------------------------------------------
// State-aware helpers (take g_mutex where noted)
// ---------------------------------------------------------------------------

// Effective environments root from g_config / process env. Never empty.
std::string EnvironmentsRoot();               // takes g_mutex

// Absolute prefix path for a validated environment name.
std::string PrefixPath(const std::string& name);  // takes g_mutex

// Locate the wine binary per config/PATH. Returns empty string when none.
std::string FindWineBinary();                 // takes g_mutex (config read)

// Locate the winetricks script per config/PATH. Empty string when none.
std::string FindWinetricksBinary();           // takes g_mutex (config read)

// The wine ELF loader behind a possibly-scripted wine entry point.
// Distro `wine` commands are often shell wrappers (Ubuntu: /usr/bin/wine ->
// alternatives -> script calling /usr/lib/wine/wine64); winetricks' arch
// detection needs the real ELF. Returns winePath itself when it already is
// one, the resolved loader when a wrapper is recognised, else winePath.
std::string ResolveWineElfBinary(const std::string& winePath);

// Winetricks verb charset: [a-z0-9][a-z0-9._+=-]*, max 64. (Pure.)
bool IsValidComponentName(const std::string& name);

// Best-effort `wine --version` (cached after first success).
std::string ProbeWineVersion(const std::string& winePath);

// Re-create the drive-letter symlinks in <prefix>/dosdevices from the
// manifest + config (home mapping, root-drive policy). Prefix must exist.
UltraWinResult ApplyMappings(const std::string& prefixPath);  // takes g_mutex

// Run a command to completion with WINEPREFIX set (plus any extraEnv
// variables). stdout+stderr go to outputFile when given (truncated first),
// else to /dev/null. Returns the exit code, or -1 on spawn failure /
// timeout (timeoutSeconds 0 = no limit).
int RunWineCommand(const std::string& binaryPath,
                   const std::vector<std::string>& args,
                   const std::string& prefixPath,
                   bool suppressPrompts,
                   int timeoutSeconds,
                   const std::vector<std::pair<std::string, std::string>>&
                       extraEnv = {},
                   const std::string& outputFile = {});

}  // namespace ultrawin_internal

// Apps/UltraCleaner/engine/UltraCleanerPaths.h
// Platform path resolution for the rule tables. Rules are written with
// tokens ({HOME}, {CACHE}, {LOCALAPPDATA}, …) instead of literal paths, so
// one rule row reads the same on all three platforms and the per-OS
// differences live here. A token that has no meaning on the running system
// resolves to the empty string and its rule root is skipped.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>
#include <vector>

namespace UltraCleaner {

// ===== WELL-KNOWN DIRECTORIES =====
// Each returns "" when the platform has no such location or the environment
// does not say where it is. None of them create anything.
std::string HomeDir();
std::string UserTempDir();      // TMPDIR / %TEMP% / /tmp
std::string SystemTempDir();    // /tmp, /private/var/tmp, C:\Windows\Temp
std::string UserCacheDir();     // ~/.cache, ~/Library/Caches, %LOCALAPPDATA%
std::string UserDataDir();      // ~/.local/share, ~/Library/Application Support, %APPDATA%
std::string UserStateDir();     // ~/.local/state, ~/Library/Logs, %LOCALAPPDATA%
std::string LocalAppDataDir();  // Windows only
std::string RoamingAppDataDir();// Windows only
std::string DownloadsDir();
std::string TrashDir();         // ~/.local/share/Trash, ~/.Trash, "" on Windows
std::string WindowsDir();       // %SystemRoot% ("C:/Windows"); "" elsewhere

// ===== TOKEN EXPANSION =====
// Replaces {TOKEN} occurrences in `pattern` and normalizes separators.
// Returns "" when any token in the pattern is unknown or unresolved, which
// the scanner treats as "this rule does not apply here".
std::string ExpandTokens(const std::string& pattern);

// Expands a path whose segments contain '*' wildcards by listing the parent
// directory — "{HOME}/Library/Containers/*/Data/Library/Caches" becomes one
// entry per container. A pattern with no wildcard yields the path itself
// unchanged (existing or not); a pattern with one yields only directories
// that exist.
std::vector<std::string> ExpandWildcardDirectories(const std::string& pattern);

// Shell-style match of a single name against a pattern with '*' (any run of
// characters), '?' (one character) and '[...]' character classes (ranges and
// '!'/'^' negation). Case-insensitive on Windows and macOS, matching those
// filesystems. Used for both wildcard path segments and the rule tables'
// file-name patterns.
bool GlobMatch(const std::string& name, const std::string& pattern);

// ===== PATH HELPERS =====
// Lower-cased on Windows and macOS (case-insensitive filesystems), and with
// separators normalized to '/', so path comparisons behave the same as the
// filesystem the user is on.
std::string NormalizeForCompare(const std::string& path);

// True when `path` is `ancestor` itself or lies underneath it. Purely
// lexical — the caller resolves symlinks first when that matters.
bool IsPathInside(const std::string& path, const std::string& ancestor);

} // namespace UltraCleaner

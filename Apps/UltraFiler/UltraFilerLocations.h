// Apps/UltraFiler/UltraFilerLocations.h
// "Extras > Open location": the operating system's own places a user is sent
// to and cannot easily find - where the crash dumps are, the application data
// folders, the temporary files, the logs, the startup programs - and the
// "Enter location…" field that opens a location typed the way the system
// writes it (%LOCALAPPDATA%\UltraCanvas\CrashDumps, $XDG_CONFIG_HOME/autostart,
// ~/Library/Logs).
//
//   FilerOsLocationsFor     the list, per platform, from the environment it is
//                           handed - pure, so every platform's list is testable
//                           anywhere
//   FilerOsLocations        that list on this machine
//   ExpandFilerLocationText %VAR% (Windows), $VAR / ${VAR} (elsewhere) and a
//                           leading ~ in a typed location
//   ResolveFilerLocation    a typed location made absolute against the folder
//                           on screen
//   RememberFilerLocation   the "recent locations" list the submenu ends with
//
// The UltraCanvas entries come from the rules the framework itself uses
// (UltraCanvasCrashDumpFolder.h, UltraCanvasSettingsFolder.h, the disk
// cache's root), so the menu opens the folder the framework writes to, not a
// copy of its rule. Header-only and free of widgets, like
// UltraFilerVolumeSpace.h: the test links nothing. All strings are UTF-8.
//
// Version: 1.0.0 - the location list, the expansion of a typed location, the recent list
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCrashDumpFolder.h"
#include "UltraCanvasDataFolder.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSettingsFolder.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <string>
#include <system_error>
#include <vector>

namespace UltraCanvas {

    // The two blocks of the submenu: the framework's own folders, then the
    // operating system's.
    enum class FilerLocationGroup { UltraCanvas, System };

    struct FilerLocation {
        FilerLocationGroup group = FilerLocationGroup::System;
        std::string label;   // the menu entry
        std::string path;    // absolute, UTF-8; need not exist
    };

    // An environment variable's value as UTF-8, empty when it is unset.
    using FilerEnvLookup = std::function<std::string(const std::string& name)>;

    // What the list is built from. The two folders the environment alone
    // does not decide are handed in: the disk cache's root
    // (UltraCanvasDiskCache::Root) and the temporary folder
    // (std::filesystem::temp_directory_path).
    struct FilerLocationEnvironment {
        DataFolderPlatform platform = DataFolderPlatform::Unix;
        FilerEnvLookup env;
        std::string cacheRoot;
        std::string tempDir;
        std::string filerSettingsDir;   // UltraFilerSettings::GetConfigDirectory
    };

    // The home folder: HOME, and on Windows, where HOME is normally unset,
    // USERPROFILE (as DataFolderHome does for this machine).
    inline std::string FilerLocationHome(DataFolderPlatform platform, const FilerEnvLookup& env) {
        if (!env) return {};
        std::string home = env("HOME");
        if (home.empty() && platform == DataFolderPlatform::Windows) home = env("USERPROFILE");
        return home;
    }

    // The locations for the platform and environment `in` describes, in menu
    // order: the UltraCanvas group first, then the system's. An entry whose
    // folder the environment cannot name (an unset variable) is left out; one
    // whose folder does not exist is kept - the menu shows it disabled, so the
    // user sees that there is, say, no crash dump yet. Pure.
    inline std::vector<FilerLocation> FilerOsLocationsFor(const FilerLocationEnvironment& in) {
        namespace fs = std::filesystem;
        const auto env = [&in](const char* name) { return in.env ? in.env(name) : std::string(); };
        std::vector<FilerLocation> out;
        const auto add = [&out](FilerLocationGroup group, const char* label, const fs::path& path) {
            if (path.empty()) return;
            out.push_back({group, label, PathToUtf8(path.lexically_normal())});
        };
        // `base` / parts, or nothing when the base is unknown.
        const auto under = [](const std::string& base, std::initializer_list<const char*> parts) {
            if (base.empty()) return fs::path();
            fs::path path = PathFromUtf8(base);
            for (const char* part : parts) path /= part;
            return path;
        };
        const auto fixed = [](const char* path) { return PathFromUtf8(path); };

        const DataFolderPlatform platform = in.platform;
        const std::string home = FilerLocationHome(platform, in.env);
        constexpr FilerLocationGroup kOwn = FilerLocationGroup::UltraCanvas;
        constexpr FilerLocationGroup kSystem = FilerLocationGroup::System;

        // ===== ULTRACANVAS =====
        add(kOwn, "Crash dumps", CrashDumpFolderFor(platform, env("ULTRACANVAS_CRASH_DUMP_DIR"),
                                                    env("LOCALAPPDATA")));
        const fs::path settingsRoot = SettingsFolderRootFor(platform, env("APPDATA"),
                                                            env("XDG_CONFIG_HOME"), home);
        if (!settingsRoot.empty()) add(kOwn, "Settings", settingsRoot / "UltraCanvas");
        if (!in.filerSettingsDir.empty())
            add(kOwn, "UltraFiler settings", PathFromUtf8(in.filerSettingsDir));
        if (!in.cacheRoot.empty()) add(kOwn, "Cache", PathFromUtf8(in.cacheRoot));

        // ===== THE SYSTEM =====
        if (platform == DataFolderPlatform::Windows) {
            const std::string appData = env("APPDATA");
            const std::string localAppData = env("LOCALAPPDATA");
            const std::string programData = env("ProgramData");
            const std::string systemRoot = env("SystemRoot");
            add(kSystem, "Application data (roaming)", under(appData, {}));
            add(kSystem, "Application data (local)", under(localAppData, {}));
            add(kSystem, "Program data", under(programData, {}));
            add(kSystem, "Programs", under(env("ProgramFiles"), {}));
            add(kSystem, "Programs (x86)", under(env("ProgramFiles(x86)"), {}));
            add(kSystem, "Start menu",
                under(appData, {"Microsoft", "Windows", "Start Menu", "Programs"}));
            add(kSystem, "Startup",
                under(appData, {"Microsoft", "Windows", "Start Menu", "Programs", "Startup"}));
            add(kSystem, "Temporary files",
                in.tempDir.empty() ? under(env("TEMP"), {}) : PathFromUtf8(in.tempDir));
            // Where Windows Error Reporting keeps the dumps of crashed programs
            // (LocalDumps' default folder) and the reports it has filed.
            add(kSystem, "Crash dumps (Windows)", under(localAppData, {"CrashDumps"}));
            add(kSystem, "Error reports",
                under(programData, {"Microsoft", "Windows", "WER", "ReportArchive"}));
            add(kSystem, "Logs", under(systemRoot, {"Logs"}));
            add(kSystem, "Fonts", under(systemRoot, {"Fonts"}));
            add(kSystem, "User fonts", under(localAppData, {"Microsoft", "Windows", "Fonts"}));
            add(kSystem, "Network configuration (hosts)",
                under(systemRoot, {"System32", "drivers", "etc"}));
            return out;
        }

        if (platform == DataFolderPlatform::MacOS) {
            add(kSystem, "Application data", under(home, {"Library", "Application Support"}));
            add(kSystem, "Preferences", under(home, {"Library", "Preferences"}));
            add(kSystem, "Caches", under(home, {"Library", "Caches"}));
            add(kSystem, "Programs", fixed("/Applications"));
            add(kSystem, "Startup (launch agents)", under(home, {"Library", "LaunchAgents"}));
            add(kSystem, "Temporary files",
                in.tempDir.empty() ? under(env("TMPDIR"), {}) : PathFromUtf8(in.tempDir));
            add(kSystem, "Crash reports", under(home, {"Library", "Logs", "DiagnosticReports"}));
            add(kSystem, "Logs", under(home, {"Library", "Logs"}));
            add(kSystem, "Fonts", fixed("/Library/Fonts"));
            add(kSystem, "User fonts", under(home, {"Library", "Fonts"}));
            add(kSystem, "System configuration", fixed("/etc"));
            return out;
        }

        // Linux and the other XDG desktops: each base directory is its
        // variable when set, else its default under the home folder.
        const auto xdg = [&](const char* name, std::initializer_list<const char*> fallback) {
            const std::string value = env(name);
            return value.empty() ? under(home, fallback) : PathFromUtf8(value);
        };
        const fs::path data = xdg("XDG_DATA_HOME", {".local", "share"});
        const fs::path config = xdg("XDG_CONFIG_HOME", {".config"});
        add(kSystem, "Application data", data);
        add(kSystem, "Configuration", config);
        add(kSystem, "Caches", xdg("XDG_CACHE_HOME", {".cache"}));
        add(kSystem, "Application launchers", fixed("/usr/share/applications"));
        if (!config.empty()) add(kSystem, "Startup (autostart)", config / "autostart");
        add(kSystem, "Temporary files", in.tempDir.empty() ? fixed("/tmp") : PathFromUtf8(in.tempDir));
        add(kSystem, "Crash reports", fixed("/var/crash"));
        add(kSystem, "Core dumps (systemd)", fixed("/var/lib/systemd/coredump"));
        add(kSystem, "Logs", fixed("/var/log"));
        add(kSystem, "Fonts", fixed("/usr/share/fonts"));
        if (!data.empty()) add(kSystem, "User fonts", data / "fonts");
        add(kSystem, "System configuration", fixed("/etc"));
        return out;
    }

    // A typed location with its variables filled in. Surrounding blanks and
    // one pair of surrounding quotes (as "Copy as path" puts them) go first;
    // then a leading ~ - alone, or before a separator - becomes the home
    // folder, and the variables their values: %NAME% on Windows, $NAME and
    // ${NAME} elsewhere. Each convention only where it is the system's own:
    // '$' is an ordinary character in a Windows name (\\server\C$), '%' in a
    // Linux one. A variable that is not set stays as it was typed, so the
    // "not found" message shows which one it was. Pure.
    inline std::string ExpandFilerLocationText(const std::string& text, DataFolderPlatform platform,
                                               const FilerEnvLookup& env) {
        const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
        size_t first = 0, last = text.size();
        while (first < last && blank(text[first])) ++first;
        while (last > first && blank(text[last - 1])) --last;
        std::string in = text.substr(first, last - first);
        if (in.size() >= 2 && (in.front() == '"' || in.front() == '\'') && in.back() == in.front())
            in = in.substr(1, in.size() - 2);

        const bool windows = platform == DataFolderPlatform::Windows;
        const auto lookup = [&env](const std::string& name) {
            return env ? env(name) : std::string();
        };

        std::string out;
        size_t i = 0;
        if (!in.empty() && in[0] == '~' &&
            (in.size() == 1 || in[1] == '/' || (windows && in[1] == '\\'))) {
            const std::string home = FilerLocationHome(platform, env);
            if (!home.empty()) {
                out = home;
                i = 1;
            }
        }

        const auto nameChar = [](char c, bool firstChar) {
            const unsigned char u = static_cast<unsigned char>(c);
            return std::isalpha(u) || c == '_' || (!firstChar && std::isdigit(u));
        };
        while (i < in.size()) {
            const char c = in[i];
            if (windows && c == '%') {
                const size_t close = in.find('%', i + 1);
                if (close != std::string::npos && close > i + 1) {
                    const std::string value = lookup(in.substr(i + 1, close - i - 1));
                    if (!value.empty()) {
                        out += value;
                        i = close + 1;
                        continue;
                    }
                }
                // Unset, or no closing '%': the '%' is text, and the one that
                // would have closed it may open the next name.
                out += c;
                ++i;
                continue;
            }
            if (!windows && c == '$' && i + 1 < in.size()) {
                if (in[i + 1] == '{') {
                    const size_t close = in.find('}', i + 2);
                    if (close != std::string::npos && close > i + 2) {
                        const std::string value = lookup(in.substr(i + 2, close - i - 2));
                        if (!value.empty()) {
                            out += value;
                            i = close + 1;
                            continue;
                        }
                        out += in.substr(i, close - i + 1);
                        i = close + 1;
                        continue;
                    }
                } else if (nameChar(in[i + 1], true)) {
                    size_t end = i + 2;
                    while (end < in.size() && nameChar(in[end], false)) ++end;
                    const std::string value = lookup(in.substr(i + 1, end - i - 1));
                    out += value.empty() ? in.substr(i, end - i) : value;
                    i = end;
                    continue;
                }
            }
            out += c;
            ++i;
        }
        return out;
    }

    // An expanded location as an absolute, normalised UTF-8 path: a relative
    // one is taken from `baseFolder` (the folder on screen, as a shell would),
    // a bare drive ("D:") means its root, and a trailing separator is dropped
    // so the result compares equal to the path a display shows. Empty for an
    // empty location, or a relative one without a base. Pure.
    inline std::string ResolveFilerLocation(const std::string& expanded, const std::string& baseFolder) {
        namespace fs = std::filesystem;
        if (expanded.empty()) return {};
        fs::path path = PathFromUtf8(expanded);
        if (path.has_root_name() && !path.has_root_directory() && path.relative_path().empty()) {
            path += fs::path::preferred_separator;
        } else if (path.is_relative()) {
            if (baseFolder.empty()) return {};
            path = PathFromUtf8(baseFolder) / path;
        }
        path = path.lexically_normal();
        if (!path.has_filename() && path != path.root_path()) path = path.parent_path();
        return PathToUtf8(path);
    }

    // Puts `location` (as the user typed it) at the front of `recent`, removing
    // an earlier copy, and keeps at most `maxEntries`. A blank entry changes
    // nothing. Pure.
    inline void RememberFilerLocation(std::vector<std::string>& recent, const std::string& location,
                                      size_t maxEntries) {
        if (location.find_first_not_of(" \t\r\n") == std::string::npos) return;
        recent.erase(std::remove(recent.begin(), recent.end(), location), recent.end());
        recent.insert(recent.begin(), location);
        if (recent.size() > maxEntries) recent.resize(maxEntries);
    }

    // FilerOsLocationsFor on this machine: its platform and environment (read
    // as UTF-8, GetEnvUtf8), with the cache root, the temporary folder and
    // UltraFiler's settings folder the caller knows.
    inline std::vector<FilerLocation> FilerOsLocations(const std::string& cacheRoot,
                                                       const std::string& filerSettingsDir) {
        FilerLocationEnvironment in;
        in.platform = CurrentDataFolderPlatform();
        in.env = [](const std::string& name) { return GetEnvUtf8(name.c_str()); };
        in.cacheRoot = cacheRoot;
        std::error_code ec;
        const std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
        if (!ec) in.tempDir = PathToUtf8(temp);
        in.filerSettingsDir = filerSettingsDir;
        return FilerOsLocationsFor(in);
    }

}  // namespace UltraCanvas

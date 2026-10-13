// UltraCanvas/include/UltraCanvasDataFolder.h
// The per-user application data folder every UltraCanvas application keeps
// its data in - the mailbox, the vault, the accounts, the analysis database:
// what belongs to the person and follows their profile, as opposed to the
// settings (UltraCanvasSettingsFolder.h) and to caches.
//
//   Windows  %APPDATA%\<App>   (without %APPDATA%: HOME\AppData\Roaming\<App>)
//   macOS    ~/Library/Application Support/<App>
//   others   $XDG_DATA_HOME/<App>, else ~/.local/share/<App>
//
// $XDG_DATA_HOME, when set, wins on every platform, so a test or a portable
// setup can point an application elsewhere.
//
// Until 2026-10 every application worked this out for itself, and they
// disagreed: UltraMail, UltraPassword and UltraAuthenticator treated macOS
// like Linux and kept their data in ~/.local/share there, UltraAuthenticator
// preferred HOME to %APPDATA% on Windows, EmailCleaner knew neither, while
// UltraSocial and the desktop's clipboard history used Application Support.
// The rule lives here now; an application adds only its folder name, and
// where an earlier version of it kept the data (its own LegacyDataDirs),
// which AdoptLegacyDataFolder moves into place once.
//
// One deliberate exception: the desktop's clipboard history
// (UltraCanvasClipboardHistory::DefaultDirectory) keeps %LOCALAPPDATA% on
// Windows. It is a machine-local store of copied blobs, not data that should
// roam with the profile, and moving an existing history would gain nothing.
//
// Header-only on purpose, like UltraCanvasPathUtf8.h: the applications'
// headless engines (UltraMailEngine, UltraPasswordCore, ...) use it without
// linking the UltraCanvas core. All strings are UTF-8.
//
// doc-check: std::string error;
// doc-check: example
//   // Where UltraMail keeps its data on this machine.
//   const std::string dataDir = UltraCanvas::PathToUtf8(UltraCanvas::UltraCanvasDataFolder("UltraMail"));
//   // Each platform's rule, testable anywhere.
//   const std::filesystem::path onMac = UltraCanvas::DataFolderRootFor(
//       UltraCanvas::DataFolderPlatform::MacOS, "", "", "/Users/erika") / "UltraMail";
//   // The first start after the folder moved brings an old folder along, whole.
//   const std::string movedFrom = UltraCanvas::AdoptLegacyDataFolder(
//       dataDir, {"/Users/erika/.local/share/UltraMail"}, {"mail.db"}, error);
//
// Version: 1.0.0 - the per-user data folder, and moving an old one into place
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8 / GetEnvUtf8

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace UltraCanvas {

    enum class DataFolderPlatform { Windows, MacOS, Unix };

    // The platform this build runs on.
    inline DataFolderPlatform CurrentDataFolderPlatform() {
#if defined(_WIN32)
        return DataFolderPlatform::Windows;
#elif defined(__APPLE__)
        return DataFolderPlatform::MacOS;
#else
        return DataFolderPlatform::Unix;
#endif
    }

    // The application data root for the given platform and environment values
    // (each may be empty). Pure, so every platform's rule is testable
    // anywhere. Empty when the environment names no place for it.
    inline std::filesystem::path DataFolderRootFor(DataFolderPlatform platform,
                                                   const std::string& xdgDataHome,
                                                   const std::string& appData,
                                                   const std::string& home) {
        if (!xdgDataHome.empty()) return PathFromUtf8(xdgDataHome);
        if (platform == DataFolderPlatform::Windows) {
            if (!appData.empty()) return PathFromUtf8(appData);
            // Without %APPDATA% (never on a real desktop), the folder it would
            // have named.
            if (!home.empty()) return PathFromUtf8(home) / "AppData" / "Roaming";
            return {};
        }
        if (home.empty()) return {};
        return platform == DataFolderPlatform::MacOS
                   ? PathFromUtf8(home) / "Library" / "Application Support"
                   : PathFromUtf8(home) / ".local" / "share";
    }

    // DataFolderRootFor on this platform, reading XDG_DATA_HOME, APPDATA and
    // HOME as UTF-8 (GetEnvUtf8).
    inline std::filesystem::path UltraCanvasDataFolderRoot() {
        return DataFolderRootFor(CurrentDataFolderPlatform(), GetEnvUtf8("XDG_DATA_HOME"),
                                 GetEnvUtf8("APPDATA"), GetEnvUtf8("HOME"));
    }

    // <root>/<appName>, which need not exist yet. Empty when the environment
    // names no root.
    inline std::filesystem::path UltraCanvasDataFolder(const std::string& appName) {
        const std::filesystem::path root = UltraCanvasDataFolderRoot();
        if (root.empty()) return {};
        return root / PathFromUtf8(appName);
    }

    // The folder an application used before this rule, on the platforms where
    // the rule moved it: ~/.local/share/<appName> on macOS and on Windows
    // (when HOME is set there). Empty on Linux, whose folder never moved, and
    // without a home. Pure, like DataFolderRootFor.
    inline std::vector<std::string> LegacyUnixDataFolders(DataFolderPlatform platform,
                                                          const std::string& home,
                                                          const std::string& appName) {
        std::vector<std::string> dirs;
        if (platform == DataFolderPlatform::Unix || home.empty()) return dirs;
        dirs.push_back(PathToUtf8(PathFromUtf8(home) / ".local" / "share" / PathFromUtf8(appName)));
        return dirs;
    }

    // When `dataDir` holds none of `markerFiles` (the files an application
    // always has in its folder: its database, its vault), move the first of
    // `candidates` that holds one - the whole folder - to `dataDir`: renamed
    // when it can be, copied and then removed when the two are on different
    // drives. Returns the folder the data came from; empty when nothing was
    // moved. A failure leaves the old folder where it was and says why in
    // `error`. A candidate that is empty, missing, the target itself or
    // without a marker is skipped.
    inline std::string AdoptLegacyDataFolder(const std::string& dataDir,
                                             const std::vector<std::string>& candidates,
                                             const std::vector<std::string>& markerFiles,
                                             std::string& error) {
        namespace fs = std::filesystem;
        error.clear();
        if (dataDir.empty()) return std::string();

        const auto holdsData = [&markerFiles](const fs::path& dir) {
            std::error_code ec;
            for (const auto& marker : markerFiles)
                if (fs::is_regular_file(dir / PathFromUtf8(marker), ec)) return true;
            return false;
        };
        // The same folder, however it is spelt (a symlinked home, "./" components).
        const auto sameFolder = [](const fs::path& a, const fs::path& b) {
            std::error_code ec;
            if (fs::exists(a, ec) && fs::exists(b, ec) && fs::equivalent(a, b, ec)) return true;
            return a.lexically_normal() == b.lexically_normal();
        };

        const fs::path target = PathFromUtf8(dataDir);
        if (holdsData(target)) return std::string();   // already in place

        for (const auto& candidate : candidates) {
            const fs::path source = PathFromUtf8(candidate);
            if (candidate.empty() || sameFolder(source, target) || !holdsData(source))
                continue;

            std::error_code ec;
            fs::create_directories(target.parent_path(), ec);
            // An empty folder in the way (made by hand, or by an installer) is
            // replaced; anything else in it is kept and the data copied beside.
            if (fs::is_directory(target, ec) && fs::is_empty(target, ec)) fs::remove(target, ec);

            ec.clear();
            if (!fs::exists(target, ec)) {
                fs::rename(source, target, ec);
                if (!ec) return candidate;
            }

            // Another drive, or the target already has files: copy, then remove.
            ec.clear();
            fs::create_directories(target, ec);
            fs::copy(source, target,
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec) {
                error = "could not move " + candidate + " to " + dataDir + ": " + ec.message();
                return std::string();
            }
            fs::remove_all(source, ec);   // the copy is complete; a leftover is harmless
            return candidate;
        }
        return std::string();
    }

}  // namespace UltraCanvas

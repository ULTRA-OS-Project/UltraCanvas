// include/UltraCanvasSettingsFolder.h
// The folder every UltraCanvas application keeps per-user settings in, shared
// between applications:
//   Windows  %APPDATA%\UltraCanvas
//   macOS    ~/Library/Application Support/UltraCanvas
//   others   $XDG_CONFIG_HOME/UltraCanvas, else ~/.config/UltraCanvas
//
// The root above it (%APPDATA%, Application Support, $XDG_CONFIG_HOME or
// ~/.config) is UltraCanvasSettingsFolderRoot(), for an application that keeps
// its settings in a folder of its own name there (UltraTexter, UltraAI). It is
// header-only, like UltraCanvasDataFolder.h for the data folder, so a test
// or a headless build uses it without linking the core; the pure
// SettingsFolderRootFor makes every platform's rule testable anywhere.
//
// The ULTRA OS desktop's own settings folder - <root>/ultraos, holding
// desktop.json (UltraDesktop, UOSSettings) and the clipboard history's key -
// is UltraCanvasDesktopSettingsFolder(). Up to 2026-10 it was ~/.config/ultraos
// on macOS too, while every other per-user folder there is in Application
// Support; the first call moves that folder into place once, whole.
//
// doc-check: example
//   const std::filesystem::path shared  = UltraCanvas::UltraCanvasSettingsFolder();          // .../UltraCanvas
//   const std::filesystem::path own     = UltraCanvas::UltraCanvasSettingsFolderRoot() / "UltraTexter";
//   const std::filesystem::path desktop = UltraCanvas::UltraCanvasDesktopSettingsFolder() / "desktop.json";
//
// Version: 1.2.0 - the desktop's settings folder (UltraCanvasDesktopSettingsFolder), moved on macOS once
// Version: 1.1.0 - the root above the folder, header-only (UltraCanvasSettingsFolderRoot)
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasDataFolder.h"   // DataFolderPlatform / CurrentDataFolderPlatform
#include "UltraCanvasPathUtf8.h"     // PathFromUtf8 / GetEnvUtf8

#include <filesystem>
#include <string>
#include <vector>

namespace UltraCanvas {

    // The settings root for the given platform and environment values (each
    // may be empty): %APPDATA% on Windows, ~/Library/Application Support on
    // macOS, $XDG_CONFIG_HOME else ~/.config elsewhere. Pure. Empty when the
    // environment names no place for it.
    inline std::filesystem::path SettingsFolderRootFor(DataFolderPlatform platform,
                                                       const std::string& appData,
                                                       const std::string& xdgConfigHome,
                                                       const std::string& home) {
        if (platform == DataFolderPlatform::Windows)
            return appData.empty() ? std::filesystem::path() : PathFromUtf8(appData);
        if (platform == DataFolderPlatform::MacOS)
            return home.empty() ? std::filesystem::path()
                                : PathFromUtf8(home) / "Library" / "Application Support";
        if (!xdgConfigHome.empty()) return PathFromUtf8(xdgConfigHome);
        if (!home.empty()) return PathFromUtf8(home) / ".config";
        return {};
    }

    // SettingsFolderRootFor on this platform, every variable read as UTF-8
    // (GetEnvUtf8: on Windows the narrow getenv answers in the ANSI code page
    // and misses a profile folder named outside it).
    inline std::filesystem::path UltraCanvasSettingsFolderRoot() {
        return SettingsFolderRootFor(CurrentDataFolderPlatform(), GetEnvUtf8("APPDATA"),
                                     GetEnvUtf8("XDG_CONFIG_HOME"), GetEnvUtf8("HOME"));
    }

    // The ULTRA OS desktop's settings folder, <root>/ultraos, which need not
    // exist yet. Empty when the environment names no root. On macOS a folder
    // an earlier version left at $XDG_CONFIG_HOME/ultraos or ~/.config/ultraos
    // (holding desktop.json or clipboard.key) is moved here once, whole; a
    // failed move leaves it where it is and the new folder is used.
    inline std::filesystem::path UltraCanvasDesktopSettingsFolder() {
        const std::filesystem::path root = UltraCanvasSettingsFolderRoot();
        if (root.empty()) return {};
        const std::filesystem::path folder = root / "ultraos";
        if (CurrentDataFolderPlatform() == DataFolderPlatform::MacOS) {
            // Where earlier versions kept it there: $XDG_CONFIG_HOME/ultraos when
            // that was set, else ~/.config/ultraos.
            std::vector<std::string> legacy;
            if (const std::string xdg = GetEnvUtf8("XDG_CONFIG_HOME"); !xdg.empty())
                legacy.push_back(PathToUtf8(PathFromUtf8(xdg) / "ultraos"));
            if (const std::string home = DataFolderHome(); !home.empty())
                legacy.push_back(PathToUtf8(PathFromUtf8(home) / ".config" / "ultraos"));
            std::string error;
            AdoptLegacyDataFolder(PathToUtf8(folder), legacy, {"desktop.json", "clipboard.key"}, error);
        }
        return folder;
    }

    // The shared folder above, which need not exist yet. Empty when the
    // environment names no home to put it in.
    std::filesystem::path UltraCanvasSettingsFolder();

}  // namespace UltraCanvas

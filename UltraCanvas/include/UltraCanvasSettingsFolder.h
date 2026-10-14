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
// doc-check: example
//   const std::filesystem::path shared = UltraCanvas::UltraCanvasSettingsFolder();          // .../UltraCanvas
//   const std::filesystem::path own    = UltraCanvas::UltraCanvasSettingsFolderRoot() / "UltraTexter";
//
// Version: 1.1.0 - the root above the folder, header-only (UltraCanvasSettingsFolderRoot)
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasDataFolder.h"   // DataFolderPlatform / CurrentDataFolderPlatform
#include "UltraCanvasPathUtf8.h"     // PathFromUtf8 / GetEnvUtf8

#include <filesystem>
#include <string>

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

    // The shared folder above, which need not exist yet. Empty when the
    // environment names no home to put it in.
    std::filesystem::path UltraCanvasSettingsFolder();

}  // namespace UltraCanvas

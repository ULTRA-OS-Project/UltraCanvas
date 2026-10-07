// include/UltraCanvasSettingsFolder.h
// The folder every UltraCanvas application keeps per-user settings in, shared
// between applications:
//   Windows  %APPDATA%\UltraCanvas
//   macOS    ~/Library/Application Support/UltraCanvas
//   others   $XDG_CONFIG_HOME/UltraCanvas, else ~/.config/UltraCanvas
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include <filesystem>

namespace UltraCanvas {

    // The folder above, which need not exist yet. Empty when the environment
    // names no home to put it in.
    std::filesystem::path UltraCanvasSettingsFolder();

}  // namespace UltraCanvas

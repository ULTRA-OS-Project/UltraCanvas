// include/UltraCanvasConfig.h
// Platform-specific default configuration for UltraCanvas Framework
// Version: 1.0.0
// Last Modified: 2026-03-11
// Author: UltraCanvas Framework

#pragma once
#include "UltraCanvasUtils.h"

namespace UltraCanvas {

    // Default directories relative to the executable
#if defined(_WIN32) || defined(_WIN64)
    constexpr const char* UC_DEFAULT_RESOURCES_DIR = "Resources\\"; // exe/Resources/
#elif defined(__APPLE__)
    constexpr const char* UC_DEFAULT_RESOURCES_DIR = "../Resources/"; // .app/Contents/Resources/
#else // Linux / Unix
    constexpr const char* UC_DEFAULT_RESOURCES_DIR = "../share/UltraCanvas/"; // app/../share/UltraCanvas/
#endif
    // Default application icon path relative to resources directory
    constexpr const char* UC_DEFAULT_ICON_SUBPATH = "media/lib/icons/UltraCanvas-logo.png";

    std::string GetResourcesDir();
} // namespace UltraCanvas

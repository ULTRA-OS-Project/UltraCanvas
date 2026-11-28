// include/UltraCanvasConfig.h
// Platform-specific default configuration for UltraCanvas Framework
// Version: 1.1.0 - kRuntimeDirVariable: the runtime an application runs on (UltraStore)
// Version: 1.0.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework

#pragma once
#include "UltraCanvasUtils.h"

namespace UltraCanvas {
    // The environment variable naming the folder of the UltraCanvas runtime an
    // application runs on, laid out like the suite package: lib/ (the shared
    // libraries), share/media and bin/Plugins/<family> on Linux; the DLLs,
    // Resources/media and Plugins/<family> on Windows. UltraStore sets it
    // when it starts an application installed with a runtime component, and
    // puts the runtime's libraries on the library path. The application's
    // own folders are looked in first: the resources (GetResourcesDir) and
    // UltraNet's plug-ins (UltraNet_GetPluginDirectory) fall back to the
    // runtime's only where the application carries none.
    inline constexpr const char* kRuntimeDirVariable = "ULTRACANVAS_RUNTIME_DIR";

    // Default application icon path relative to resources directory
    std::string GetDefaultIcon();
    void SetDefaultIcon(const std::string& relativePath);
    // The folder holding media/ (absolute, ending in a separator): the first
    // of the application's packaged place, the build tree's share/, share/
    // above a bin/, the legacy AppImage folder, and the runtime's
    // (kRuntimeDirVariable) that has a media/ folder.
    std::string GetResourcesDir();
    void SetResourcesDir(const std::string& relativePath);
} // namespace UltraCanvas

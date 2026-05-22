// include/UltraCanvasConfig.h
// Platform-specific default configuration for UltraCanvas Framework
// Version: 1.0.0
// Last Modified: 2026-03-11
// Author: UltraCanvas Framework

#pragma once
#include "UltraCanvasUtils.h"

namespace UltraCanvas {
    // Default application icon path relative to resources directory
    std::string GetDefaultIcon();
    void SetDefaultIcon(const std::string& relativePath);
    std::string GetResourcesDir();
    void SetResourcesDir(const std::string& relativePath);
} // namespace UltraCanvas

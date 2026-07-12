// core/UltraCanvasConfig.cpp
// Utils
// Version: 1.0.0
// Last Modified: 2025-09-14
// Author: UltraCanvas Framework

#include "UltraCanvasConfig.h"
#include <sstream>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <string>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {
    std::string resourcesDir;
    std::string defaultIcon = "media/lib/icons/UltraCanvas-logo.png";

    std::string GetResourcesDir() {
        if (resourcesDir.empty()) {
            SetResourcesDir("UltraCanvas");
            debugOutput << "GetResourcesDir dir=" << NormalizePath(resourcesDir) << std::endl;
        }
        return resourcesDir;
    }

    void SetResourcesDir(const std::string& subpath) {
#if defined(_WIN32) || defined(_WIN64)
        std::string UC_DEFAULT_RESOURCES_DIR = "Resources/"; // exe/Resources/
#elif defined(__APPLE__)
        std::string UC_DEFAULT_RESOURCES_DIR = "../Resources/"; // .app/Contents/Resources/
#else // Linux / Unix
        std::string UC_DEFAULT_RESOURCES_DIR = "../share/"+ subpath + "/"; // app/../share/UltraCanvas/
#endif
        resourcesDir = GetExecutableDir() + "/" + UC_DEFAULT_RESOURCES_DIR;
        debugOutput << "SetResourcesDir dir=" << NormalizePath(resourcesDir) << std::endl;
    }

    std::string GetDefaultIcon() {
        return NormalizePath(GetResourcesDir() + defaultIcon);
    }
    void SetDefaultIcon(const std::string& subpath) {
        defaultIcon = subpath;
    }
}

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
#include <string>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {
    std::string resourcesDir;
    std::string GetResourcesDir() {
        if (resourcesDir.empty()) {
#if defined(_WIN32) || defined(_WIN64)
            resourcesDir = GetExecutableDir() + "\\" + UC_DEFAULT_RESOURCES_DIR;
#else
            resourcesDir = GetExecutableDir() + "/" + UC_DEFAULT_RESOURCES_DIR;
#endif
            debugOutput << "GetResourcesDir dir=" << resourcesDir << std::endl;
        }
        return resourcesDir;
    }
}
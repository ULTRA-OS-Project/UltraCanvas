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

namespace UltraCanvas {
    std::string resourcesDir;
    std::string GetResourcesDir() {
        if (resourcesDir.empty()) {
            resourcesDir = GetExecutableDir() + "/" + UC_DEFAULT_RESOURCES_DIR;
            std::cerr << "GetResourcesDir dir=" << resourcesDir << std::endl;
        }
        return resourcesDir;
    }
}
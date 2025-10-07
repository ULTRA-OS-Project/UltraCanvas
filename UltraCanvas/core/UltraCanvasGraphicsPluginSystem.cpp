// include/UltraCanvasGraphicsPluginSystem.cpp
// Complete graphics plugin system with all required components
// Version: 1.2.4
// Last Modified: 2025-08-17
// Author: UltraCanvas Framework

#include "UltraCanvasGraphicsPluginSystem.h"

namespace UltraCanvas {
    std::vector<std::shared_ptr<IGraphicsPlugin>> UltraCanvasGraphicsPluginRegistry::plugins;
    std::map<std::string, std::shared_ptr<IGraphicsPlugin>> UltraCanvasGraphicsPluginRegistry::extensionMap;
    bool UltraCanvasGraphicsPluginRegistry::initialized = false;

}
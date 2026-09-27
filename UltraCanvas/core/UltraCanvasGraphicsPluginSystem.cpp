// core/UltraCanvasGraphicsPluginSystem.cpp
// The graphics plugin registry's storage. Each accessor holds a
// function-local static, so it is built on first use (a plugin registering
// from a static initialiser cannot reach a registry that does not exist
// yet), and lives here rather than inline in the header, so there is one
// registry per process: on Windows an inline function's statics are
// duplicated in every module, and the core DLL and the executables that
// link the format plugins each had their own (see the header).
// Version: 2.1.0
// Last Modified: 2026-09-26
// Author: UltraCanvas Framework

#include "UltraCanvasGraphicsPluginSystem.h"

namespace UltraCanvas {

    std::vector<std::shared_ptr<IGraphicsPlugin>>& UltraCanvasGraphicsPluginRegistry::Plugins() {
        static std::vector<std::shared_ptr<IGraphicsPlugin>> instance;
        return instance;
    }

    std::map<std::string, std::shared_ptr<IGraphicsPlugin>>& UltraCanvasGraphicsPluginRegistry::ExtensionMap() {
        static std::map<std::string, std::shared_ptr<IGraphicsPlugin>> instance;
        return instance;
    }

    bool& UltraCanvasGraphicsPluginRegistry::Initialized() {
        static bool instance = false;
        return instance;
    }

} // namespace UltraCanvas

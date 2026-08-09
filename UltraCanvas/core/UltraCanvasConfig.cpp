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
#if !defined(_WIN32) && !defined(_WIN64)
#include <sys/stat.h>
#endif
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
        resourcesDir = GetExecutableDir() + "/" + UC_DEFAULT_RESOURCES_DIR;
#elif defined(__APPLE__)
        std::string UC_DEFAULT_RESOURCES_DIR = "../Resources/"; // .app/Contents/Resources/
        resourcesDir = GetExecutableDir() + "/" + UC_DEFAULT_RESOURCES_DIR;
#else // Linux / Unix
        // The resources directory sits at a different offset from the executable
        // depending on how the app is deployed, so probe a set of candidates and
        // pick the first one that actually contains a media/ subdir:
        //   1. exeDir/share/               dev build (exe at build root)
        //   2. exeDir/../share/            portable package (real exe in bin/)
        //   3. exeDir/../share/UltraCanvas/ legacy AppImage layout
        const std::string exeDir = GetExecutableDir();
        const std::string candidates[] = {
            exeDir + "/share/",
            exeDir + "/../share/",
            exeDir + "/../share/" + subpath + "/",
        };
        resourcesDir = candidates[0]; // fallback if none exist
        for (const std::string& candidate : candidates) {
            struct stat st{};
            if (stat((candidate + "media").c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                resourcesDir = candidate;
                break;
            }
        }
#endif
        debugOutput << "SetResourcesDir dir=" << NormalizePath(resourcesDir) << std::endl;
    }

    std::string GetDefaultIcon() {
        return NormalizePath(GetResourcesDir() + defaultIcon);
    }
    void SetDefaultIcon(const std::string& subpath) {
        defaultIcon = subpath;
    }
}

// core/UltraCanvasConfig.cpp
// Utils
// Version: 1.1.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework

#include "UltraCanvasConfig.h"
#include "UltraCanvasPathUtf8.h"
#include <sstream>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
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
#if defined(__ANDROID__)
        // APK assets are not filesystem paths; the android_main glue extracts
        // them into the app-private files dir and exports it as HOME, so
        // path-based resource loading keeps working unchanged.
        const char* home = std::getenv("HOME");
        resourcesDir = std::string(home && *home ? home : ".") + "/share/";
#else
        // The resources sit at a different offset from the executable depending
        // on how the app is deployed, so probe a set of candidates and take the
        // first that actually contains a media/ subdir. The platform's packaged
        // layout comes first; then the build tree, where the executables sit
        // at the build root (or in bin/) and CMake links share/ to the
        // repository's media/ and Docs/ at configure time, so an application
        // started straight from the build tree finds its resources on every
        // desktop platform; then the portable Linux package and the legacy
        // AppImage layout, which use the same share/ offsets.
        const std::string exeDir = GetExecutableDir();
        const std::vector<std::string> candidates = {
#if defined(_WIN32) || defined(_WIN64)
            exeDir + "/Resources/",                   // the Windows package: exe/Resources/
#elif defined(__APPLE__)
            exeDir + "/../Resources/",                // the bundle: .app/Contents/Resources/
#endif
            exeDir + "/share/",                       // build tree (exe at the build root), portable package
            exeDir + "/../share/",                    // real exe in bin/
            exeDir + "/../share/" + subpath + "/",    // legacy AppImage layout
        };
        resourcesDir = candidates[0]; // fallback if none exist
        for (const std::string& candidate : candidates) {
            std::error_code ec;
            if (std::filesystem::is_directory(PathFromUtf8(candidate + "media"), ec)) {
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

// core/UltraCanvasSettingsFolder.cpp
// See UltraCanvasSettingsFolder.h.
// Version: 1.0.1 - every variable read through GetEnvUtf8, HOME and XDG_CONFIG_HOME too
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasSettingsFolder.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / GetEnvUtf8

namespace UltraCanvas {

    std::filesystem::path UltraCanvasSettingsFolder() {
        std::filesystem::path base;
#if defined(_WIN32) || defined(_WIN64)
        // UTF-8 from the process's UTF-16 environment (GetEnvUtf8), like
        // every path PathFromUtf8 takes.
        if (const std::string appData = GetEnvUtf8("APPDATA"); !appData.empty())
            base = PathFromUtf8(appData);
#elif defined(__APPLE__)
        // GetEnvUtf8 for every variable, not only APPDATA: on these
        // platforms it is the plain environment, so the file reads the same
        // way on all three and nothing narrow is left for the UTF-8 check
        // to tell from a Windows read.
        if (const std::string home = GetEnvUtf8("HOME"); !home.empty())
            base = PathFromUtf8(home) / "Library" / "Application Support";
#else
        if (const std::string xdg = GetEnvUtf8("XDG_CONFIG_HOME"); !xdg.empty()) {
            base = PathFromUtf8(xdg);
        } else if (const std::string home = GetEnvUtf8("HOME"); !home.empty()) {
            base = PathFromUtf8(home) / ".config";
        }
#endif
        if (base.empty()) return {};
        return base / "UltraCanvas";
    }

}  // namespace UltraCanvas

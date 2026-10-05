// core/UltraCanvasSettingsFolder.cpp
// See UltraCanvasSettingsFolder.h.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasSettingsFolder.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8

#include <cstdlib>

namespace UltraCanvas {

    std::filesystem::path UltraCanvasSettingsFolder() {
        std::filesystem::path base;
#if defined(_WIN32) || defined(_WIN64)
        if (const wchar_t* appData = _wgetenv(L"APPDATA"))
            base = std::filesystem::path(appData);   // path-string-ok: wide
#elif defined(__APPLE__)
        if (const char* home = std::getenv("HOME"))
            base = PathFromUtf8(home) / "Library" / "Application Support";
#else
        if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
            base = PathFromUtf8(xdg);
        } else if (const char* home = std::getenv("HOME")) {
            base = PathFromUtf8(home) / ".config";
        }
#endif
        if (base.empty()) return {};
        return base / "UltraCanvas";
    }

}  // namespace UltraCanvas

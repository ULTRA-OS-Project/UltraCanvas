// Apps/SmartHomeCenter/ui/SmartHomeCenterPaths.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "SmartHomeCenterPaths.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8 / GetEnvUtf8

#include <filesystem>
#include <system_error>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace SmartHomeCenter {

std::string DefaultStorePath(bool demoHome) {
    namespace fs = std::filesystem;
    fs::path root;
    // Every variable through GetEnvUtf8, as UltraNetMonitor does: on Windows
    // getenv answers in the ANSI code page.
    const std::string home = UltraCanvas::GetEnvUtf8("HOME");
#if defined(_WIN32)
    if (const std::string local = UltraCanvas::GetEnvUtf8("LOCALAPPDATA"); !local.empty())
        root = PathFromUtf8(local);
#elif defined(__APPLE__)
    if (!home.empty()) root = PathFromUtf8(home) / "Library" / "Application Support";
#else
    if (const std::string xdg = UltraCanvas::GetEnvUtf8("XDG_DATA_HOME"); !xdg.empty()) root = PathFromUtf8(xdg);
    else if (!home.empty()) root = PathFromUtf8(home) / ".local" / "share";
#endif
    if (root.empty()) return std::string();
    const fs::path directory = root / "ULTRA" / "SmartHome";
    std::error_code error;
    fs::create_directories(directory, error);
    if (error) return std::string();
#if !defined(_WIN32)
    // A home's log says when its people come and go: the directory is
    // theirs alone, whatever umask created it with.
    ::chmod(directory.c_str(), S_IRWXU);
#endif
    return PathToUtf8(directory / (demoHome ? "smarthome-demo.db" : "smarthome.db"));
}

} // namespace SmartHomeCenter

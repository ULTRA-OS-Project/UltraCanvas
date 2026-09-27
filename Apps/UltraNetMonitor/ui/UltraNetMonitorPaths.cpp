// Apps/UltraNetMonitor/ui/UltraNetMonitorPaths.cpp
// Version: 0.5.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraNetMonitorPaths.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8

#include <cstdlib>
#include <filesystem>
#include <system_error>

using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace UltraNetMonitor {

std::string DefaultStorePath() {
    namespace fs = std::filesystem;
    fs::path root;
    const char* home = std::getenv("HOME");
#if defined(_WIN32)
    if (const char* local = std::getenv("LOCALAPPDATA"); local && *local) root = local;
#elif defined(__APPLE__)
    if (home && *home) root = PathFromUtf8(home) / "Library" / "Application Support";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) root = xdg;
    else if (home && *home) root = PathFromUtf8(home) / ".local" / "share";
#endif
    if (root.empty()) return std::string();
    const fs::path directory = root / "UltraNetMonitor";
    std::error_code error;
    fs::create_directories(directory, error);
    if (error) return std::string();
#if !defined(_WIN32)
    // The store is a record of a person's activity: the directory is
    // theirs alone, whatever umask created it with. %LOCALAPPDATA% is
    // per-user already.
    ::chmod(directory.c_str(), S_IRWXU);
#endif
    return PathToUtf8(directory / "activity.db");
}

} // namespace UltraNetMonitor

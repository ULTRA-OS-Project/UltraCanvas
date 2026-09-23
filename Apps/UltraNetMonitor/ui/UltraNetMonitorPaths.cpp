// Apps/UltraNetMonitor/ui/UltraNetMonitorPaths.cpp
// Version: 0.5.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraNetMonitorPaths.h"

#include <cstdlib>
#include <filesystem>
#include <system_error>

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
    if (home && *home) root = fs::path(home) / "Library" / "Application Support";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) root = xdg;
    else if (home && *home) root = fs::path(home) / ".local" / "share";
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
    return (directory / "activity.db").string();
}

} // namespace UltraNetMonitor

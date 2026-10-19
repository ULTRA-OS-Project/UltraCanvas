// Apps/UltraNetMonitor/ui/UltraNetMonitorPaths.cpp
// Version: 0.1.1 - HOME and XDG_DATA_HOME read as UTF-8 too (GetEnvUtf8)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraNetMonitorPaths.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8 / GetEnvUtf8

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
    // Every variable through GetEnvUtf8: on Windows getenv answers in the
    // ANSI code page, which turns a profile folder named outside it into '?',
    // and reading HOME and XDG_DATA_HOME the same way keeps the file on one
    // rule rather than one per platform.
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

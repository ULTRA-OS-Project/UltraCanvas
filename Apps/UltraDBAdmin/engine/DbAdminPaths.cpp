// Apps/UltraDBAdmin/engine/DbAdminPaths.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminPaths.h"

#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8 / GetEnvUtf8

#include <filesystem>
#include <system_error>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace UltraDBAdmin {

std::string DataDirectory() {
    namespace fs = std::filesystem;
    fs::path root;
    // Every variable through GetEnvUtf8: on Windows getenv answers in the
    // ANSI code page, which turns a profile folder named outside it into '?'.
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
    const fs::path directory = root / "UltraDBAdmin";
    std::error_code error;
    fs::create_directories(directory, error);
    if (error) return std::string();
#if !defined(_WIN32)
    // Connection details and the vault of their passwords: the directory is
    // the user's alone, whatever umask created it with.
    ::chmod(directory.c_str(), S_IRWXU);
#endif
    return PathToUtf8(directory);
}

} // namespace UltraDBAdmin

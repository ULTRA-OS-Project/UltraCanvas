// core/UltraCanvasFileTimes.cpp
// Platform-independent half of CopyFileTimes: the public entry points and the
// std::filesystem fallback (the modification date only) for a platform with
// no backend. Every operating-system call lives in OS/<Platform>/
// (UltraCanvasWindowsFileTimes.cpp, UltraCanvasMacOSFileTimes.cpp,
// UltraCanvasLinuxFileTimes.cpp).
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraCanvasFileTimes.h"
#include "UltraCanvasPathUtf8.h"

#include <filesystem>
#include <system_error>

namespace UltraCanvas {

#ifndef ULTRACANVAS_HAS_NATIVE_FILE_TIMES
    bool NativeCopyFileTimes(const std::string& fromUtf8, const std::string& toUtf8) {
        namespace fs = std::filesystem;
        std::error_code ec;
        const auto stamp = fs::last_write_time(PathFromUtf8(fromUtf8), ec);
        if (ec) return false;
        fs::last_write_time(PathFromUtf8(toUtf8), stamp, ec);
        return !ec;
    }
#endif

    bool CopyFileTimes(const std::string& fromUtf8, const std::string& toUtf8) {
        if (fromUtf8.empty() || toUtf8.empty()) return false;
        return NativeCopyFileTimes(fromUtf8, toUtf8);
    }

    bool CanCopyCreationTime() {
#if defined(ULTRACANVAS_HAS_NATIVE_FILE_TIMES) && (defined(_WIN32) || defined(__APPLE__))
        return true;
#else
        return false;
#endif
    }

} // namespace UltraCanvas

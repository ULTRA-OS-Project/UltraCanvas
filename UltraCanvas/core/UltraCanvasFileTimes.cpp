// core/UltraCanvasFileTimes.cpp
// Platform-independent half of UltraCanvasFileTimes: the public entry points,
// CopyFileTimes as a read and a set, and the std::filesystem fallback (the
// modification date only) for a platform with no backend. Every
// operating-system call lives in OS/<Platform>/
// (UltraCanvasWindowsFileTimes.cpp, UltraCanvasMacOSFileTimes.cpp,
// UltraCanvasLinuxFileTimes.cpp).
// Version: 1.1.0 - GetFileTimes / SetFileTimes; CopyFileTimes is the two
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraCanvasFileTimes.h"
#include "UltraCanvasPathUtf8.h"

#include <chrono>
#include <filesystem>
#include <system_error>

namespace UltraCanvas {

#ifndef ULTRACANVAS_HAS_NATIVE_FILE_TIMES
    // std::filesystem's clock and the Unix epoch: libc++ (Android, WebAssembly
    // - the platforms that use this fallback) counts the file clock from the
    // Unix epoch itself; libstdc++ and MSVC convert through the system clock.
    namespace {
        namespace fs = std::filesystem;
        int64_t ToUnixNanos(fs::file_time_type stamp) {
            using namespace std::chrono;
#if defined(_LIBCPP_VERSION)
            return duration_cast<nanoseconds>(stamp.time_since_epoch()).count();
#elif defined(_MSC_VER)
            return duration_cast<nanoseconds>(clock_cast<system_clock>(stamp).time_since_epoch()).count();
#else
            return duration_cast<nanoseconds>(file_clock::to_sys(stamp).time_since_epoch()).count();
#endif
        }
        fs::file_time_type FromUnixNanos(int64_t nanos) {
            using namespace std::chrono;
#if defined(_LIBCPP_VERSION)
            return fs::file_time_type(duration_cast<fs::file_time_type::duration>(nanoseconds(nanos)));
#else
            const sys_time<nanoseconds> sys{nanoseconds(nanos)};
#if defined(_MSC_VER)
            return clock_cast<file_clock>(sys);
#else
            return time_point_cast<fs::file_time_type::duration>(file_clock::from_sys(sys));
#endif
#endif
        }
    } // namespace

    bool NativeGetFileTimes(const std::string& pathUtf8, FileTimes& out) {
        std::error_code ec;
        const auto stamp = fs::last_write_time(PathFromUtf8(pathUtf8), ec);
        if (ec) return false;
        out.modified = ToUnixNanos(stamp);
        return true;
    }

    bool NativeSetFileTimes(const std::string& pathUtf8, const FileTimes& times) {
        if (!times.modified) return true;
        std::error_code ec;
        fs::last_write_time(PathFromUtf8(pathUtf8), FromUnixNanos(times.modified), ec);
        return !ec;
    }
#endif

    bool GetFileTimes(const std::string& pathUtf8, FileTimes& out) {
        out = FileTimes{};
        if (pathUtf8.empty()) return false;
        return NativeGetFileTimes(pathUtf8, out);
    }

    bool SetFileTimes(const std::string& pathUtf8, const FileTimes& times) {
        if (pathUtf8.empty()) return false;
        if (!times.created && !times.modified && !times.accessed) return true;
        return NativeSetFileTimes(pathUtf8, times);
    }

    bool CopyFileTimes(const std::string& fromUtf8, const std::string& toUtf8) {
        if (fromUtf8.empty() || toUtf8.empty()) return false;
        FileTimes times;
        if (!GetFileTimes(fromUtf8, times) || !times.modified) return false;
        // The target must exist: SetFileTimes alone cannot tell "set" from
        // "nothing there" on every backend.
        std::error_code ec;
        if (!std::filesystem::exists(PathFromUtf8(toUtf8), ec)) return false;
        return SetFileTimes(toUtf8, times);
    }

    bool CanCopyCreationTime() {
#if defined(ULTRACANVAS_HAS_NATIVE_FILE_TIMES) && (defined(_WIN32) || defined(__APPLE__))
        return true;
#else
        return false;
#endif
    }

} // namespace UltraCanvas

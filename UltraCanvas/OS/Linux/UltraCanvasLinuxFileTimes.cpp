// OS/Linux/UltraCanvasLinuxFileTimes.cpp
// UltraCanvasFileTimes on Linux and the BSDs. The modification and access
// dates are set to the nanosecond through utimensat. The creation ("birth")
// date is read where the system reports it (statx on Linux, st_birthtim on
// the BSDs) but cannot be set by a program here, so a copy's is the moment it
// was made, as with cp -p.
// Version: 1.1.0 - NativeGetFileTimes / NativeSetFileTimes
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraCanvasFileTimes.h"
#include "UltraCanvasPathUtf8.h"

#include <fcntl.h>
#include <sys/stat.h>

namespace UltraCanvas {

    namespace {
        int64_t Nanos(const struct timespec& t) {
            return static_cast<int64_t>(t.tv_sec) * FileTimes::kNanosPerSecond + t.tv_nsec;
        }
        struct timespec Spec(int64_t nanos) {
            struct timespec t {};
            t.tv_sec = static_cast<time_t>(nanos / FileTimes::kNanosPerSecond);
            t.tv_nsec = static_cast<long>(nanos % FileTimes::kNanosPerSecond);
            if (t.tv_nsec < 0) { t.tv_nsec += FileTimes::kNanosPerSecond; --t.tv_sec; }
            return t;
        }
    } // namespace

    bool NativeGetFileTimes(const std::string& pathUtf8, FileTimes& out) {
        // Paths are UTF-8 bytes on these systems; PathFromUtf8 is the identity.
        const std::string path = PathFromUtf8(pathUtf8).native();
#if defined(__linux__) && defined(STATX_BTIME)
        struct statx sx {};
        if (::statx(AT_FDCWD, path.c_str(), 0, STATX_BASIC_STATS | STATX_BTIME, &sx) == 0) {
            auto nanos = [](const struct statx_timestamp& t) {
                return static_cast<int64_t>(t.tv_sec) * FileTimes::kNanosPerSecond + t.tv_nsec;
            };
            out.modified = nanos(sx.stx_mtime);
            out.accessed = nanos(sx.stx_atime);
            if (sx.stx_mask & STATX_BTIME) out.created = nanos(sx.stx_btime);
            return true;
        }
#endif
        struct stat st {};
        if (::stat(path.c_str(), &st) != 0) return false;
        out.modified = Nanos(st.st_mtim);
        out.accessed = Nanos(st.st_atim);
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
        out.created = Nanos(st.st_birthtim);
#endif
        return true;
    }

    bool NativeSetFileTimes(const std::string& pathUtf8, const FileTimes& times) {
        if (!times.modified && !times.accessed) return true;   // a creation date alone: nothing settable
        const std::string path = PathFromUtf8(pathUtf8).native();
        struct timespec spec[2];
        spec[0] = times.accessed ? Spec(times.accessed) : timespec{0, UTIME_OMIT};
        spec[1] = times.modified ? Spec(times.modified) : timespec{0, UTIME_OMIT};
        return ::utimensat(AT_FDCWD, path.c_str(), spec, 0) == 0;
    }

} // namespace UltraCanvas

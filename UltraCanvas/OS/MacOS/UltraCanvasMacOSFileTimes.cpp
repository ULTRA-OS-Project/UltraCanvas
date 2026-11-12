// OS/MacOS/UltraCanvasMacOSFileTimes.cpp
// UltraCanvasFileTimes on macOS: the dates read with stat (st_birthtimespec is
// the creation date), the access and modification dates set through
// utimensat, then the creation date through setattrlist (ATTR_CMN_CRTIME). The
// creation date goes last: APFS and HFS+ pull it back to a modification date
// set earlier than it, so setting it first could be undone by the second call.
// Version: 1.1.0 - NativeGetFileTimes / NativeSetFileTimes
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraCanvasFileTimes.h"
#include "UltraCanvasPathUtf8.h"

#include <fcntl.h>
#include <sys/attr.h>
#include <sys/stat.h>
#include <unistd.h>

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
        const std::string path = PathFromUtf8(pathUtf8).native();
        struct stat st {};
        if (::stat(path.c_str(), &st) != 0) return false;
        out.modified = Nanos(st.st_mtimespec);
        out.accessed = Nanos(st.st_atimespec);
        out.created = Nanos(st.st_birthtimespec);
        return true;
    }

    bool NativeSetFileTimes(const std::string& pathUtf8, const FileTimes& times) {
        const std::string path = PathFromUtf8(pathUtf8).native();
        bool ok = true;
        if (times.modified || times.accessed) {
            struct timespec spec[2];
            spec[0] = times.accessed ? Spec(times.accessed) : timespec{0, UTIME_OMIT};
            spec[1] = times.modified ? Spec(times.modified) : timespec{0, UTIME_OMIT};
            ok = ::utimensat(AT_FDCWD, path.c_str(), spec, 0) == 0;
        }
        if (times.created) {
            struct attrlist attributes {};
            attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
            attributes.commonattr = ATTR_CMN_CRTIME;
            struct timespec created = Spec(times.created);
            ok = ::setattrlist(path.c_str(), &attributes, &created, sizeof(created), 0) == 0 && ok;
        }
        return ok;
    }

} // namespace UltraCanvas

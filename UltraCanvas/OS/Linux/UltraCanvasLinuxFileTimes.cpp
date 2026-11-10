// OS/Linux/UltraCanvasLinuxFileTimes.cpp
// CopyFileTimes on Linux and the BSDs: the modification and access dates, to
// the nanosecond, through utimensat. The creation ("birth") date cannot be
// set by a program here - statx reports it, nothing writes it - so a copy's
// is the moment it was made, as with cp -p.
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraCanvasFileTimes.h"
#include "UltraCanvasPathUtf8.h"

#include <fcntl.h>
#include <sys/stat.h>

namespace UltraCanvas {

    bool NativeCopyFileTimes(const std::string& fromUtf8, const std::string& toUtf8) {
        // Paths are UTF-8 bytes on these systems; PathFromUtf8 is the identity.
        const std::string from = PathFromUtf8(fromUtf8).native();
        const std::string to = PathFromUtf8(toUtf8).native();
        struct stat st {};
        if (::stat(from.c_str(), &st) != 0) return false;
        const struct timespec times[2] = {st.st_atim, st.st_mtim};
        return ::utimensat(AT_FDCWD, to.c_str(), times, 0) == 0;
    }

} // namespace UltraCanvas

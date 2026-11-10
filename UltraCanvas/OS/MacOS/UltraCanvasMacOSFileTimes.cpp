// OS/MacOS/UltraCanvasMacOSFileTimes.cpp
// CopyFileTimes on macOS: the access and modification dates through utimensat,
// then the creation date through setattrlist (ATTR_CMN_CRTIME). The creation
// date goes last: APFS and HFS+ pull it back to a modification date set
// earlier than it, so setting it first could be undone by the second call.
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

    bool NativeCopyFileTimes(const std::string& fromUtf8, const std::string& toUtf8) {
        const std::string from = PathFromUtf8(fromUtf8).native();
        const std::string to = PathFromUtf8(toUtf8).native();
        struct stat st {};
        if (::stat(from.c_str(), &st) != 0) return false;
        const struct timespec times[2] = {st.st_atimespec, st.st_mtimespec};
        const bool modified = ::utimensat(AT_FDCWD, to.c_str(), times, 0) == 0;

        struct attrlist attributes {};
        attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
        attributes.commonattr = ATTR_CMN_CRTIME;
        struct timespec created = st.st_birthtimespec;
        ::setattrlist(to.c_str(), &attributes, &created, sizeof(created), 0);
        return modified;
    }

} // namespace UltraCanvas

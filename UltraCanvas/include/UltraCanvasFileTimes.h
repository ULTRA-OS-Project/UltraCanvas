// include/UltraCanvasFileTimes.h
// A file's or folder's dates - when it was created, last modified and last
// opened - read, set, and carried over to a copy. A copy made with
// std::filesystem gets none of them back (or, on Windows, the modification
// date only): a photo copied to another drive then claims to have been taken
// today. The Filer calls CopyFileTimes after every file and folder it copies;
// a cloud drive's download sets the dates the server reports with
// SetFileTimes, and its upload reads the ones to send with GetFileTimes.
//
// What each system lets a program set:
//   Windows        created, modified, accessed   (SetFileTime)
//   macOS          created, modified, accessed   (setattrlist, utimensat)
//   Linux, BSDs    modified, accessed            (utimensat - the kernel keeps
//                  the creation, "birth", time to itself; statx reads it)
//   elsewhere      modified                      (std::filesystem)
// The dates are carried to the nanosecond where the file system keeps them.
//
// Version: 1.1.0 - FileTimes, GetFileTimes, SetFileTimes: dates read and set
//                  on their own, for a date that comes from elsewhere (a server)
// Version: 1.0.0 - CopyFileTimes, CanCopyCreationTime
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <string>

namespace UltraCanvas {

    // A file's dates as nanoseconds since 1970-01-01 UTC. 0 is "not known":
    // GetFileTimes leaves a date the system does not keep at 0, and
    // SetFileTimes leaves a date given as 0 as it is on the file.
    struct FileTimes {
        int64_t created  = 0;
        int64_t modified = 0;
        int64_t accessed = 0;

        static constexpr int64_t kNanosPerSecond = 1000000000LL;
        static int64_t FromSeconds(int64_t seconds) { return seconds * kNanosPerSecond; }
        static int64_t ToSeconds(int64_t nanos) { return nanos / kNanosPerSecond; }
    };

    // Reads `pathUtf8`'s dates (a link is followed). False when the file
    // cannot be read; `out` is reset first.
    bool GetFileTimes(const std::string& pathUtf8, FileTimes& out);

    // Sets the non-zero dates of `times` on `pathUtf8` (a link is followed).
    // Best effort, like CopyFileTimes: true when every date this system can set
    // and was given a value was set; a creation date where none can be set
    // (Linux) is not a failure.
    bool SetFileTimes(const std::string& pathUtf8, const FileTimes& times);

    // Gives `toUtf8` the dates of `fromUtf8` (both files, or both folders;
    // a link is followed, as a copy follows it). Best effort: true when at
    // least the modification date was set. A file system that keeps no such
    // date, or refuses (a FAT stick's creation date is rounded, some network
    // shares refuse outright), leaves the copy's own - the copy is not undone.
    bool CopyFileTimes(const std::string& fromUtf8, const std::string& toUtf8);

    // True where SetFileTimes / CopyFileTimes can set the creation date
    // (Windows, macOS). A program saying "creation dates are kept" asks this.
    bool CanCopyCreationTime();

    // ===== BACKEND (OS/<Platform>/UltraCanvas<Platform>FileTimes.cpp) =====
    // Not part of the public surface. Defined by a platform backend when the
    // build sets ULTRACANVAS_HAS_NATIVE_FILE_TIMES, otherwise by the core
    // file's std::filesystem fallback.
    bool NativeGetFileTimes(const std::string& pathUtf8, FileTimes& out);
    bool NativeSetFileTimes(const std::string& pathUtf8, const FileTimes& times);

} // namespace UltraCanvas

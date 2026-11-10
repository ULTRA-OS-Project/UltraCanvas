// include/UltraCanvasFileTimes.h
// Carries a file's or folder's dates over to its copy: when it was created,
// last modified and last opened. A copy made with std::filesystem gets none of
// them back (or, on Windows, the modification date only): a photo copied to
// another drive then claims to have been taken today. The Filer calls it after
// every file and folder it copies, and after a move between drives, which is a
// copy and a delete.
//
// What each system lets a program set:
//   Windows        created, modified, accessed   (SetFileTime)
//   macOS          created, modified, accessed   (setattrlist, utimensat)
//   Linux, BSDs    modified, accessed            (utimensat - the kernel keeps
//                  the creation, "birth", time to itself)
//   elsewhere      modified                      (std::filesystem)
// The dates are copied to the nanosecond where the file system keeps them.
//
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework
#pragma once

#include <string>

namespace UltraCanvas {

    // Gives `toUtf8` the dates of `fromUtf8` (both files, or both folders;
    // a link is followed, as a copy follows it). Best effort: true when at
    // least the modification date was set. A file system that keeps no such
    // date, or refuses (a FAT stick's creation date is rounded, some network
    // shares refuse outright), leaves the copy's own - the copy is not undone.
    bool CopyFileTimes(const std::string& fromUtf8, const std::string& toUtf8);

    // True where CopyFileTimes can carry the creation date too (Windows,
    // macOS). A program saying "creation dates are kept" asks this first.
    bool CanCopyCreationTime();

    // ===== BACKEND (OS/<Platform>/UltraCanvas<Platform>FileTimes.cpp) =====
    // Not part of the public surface. Defined by a platform backend when the
    // build sets ULTRACANVAS_HAS_NATIVE_FILE_TIMES, otherwise by the core
    // file's std::filesystem fallback.
    bool NativeCopyFileTimes(const std::string& fromUtf8, const std::string& toUtf8);

} // namespace UltraCanvas

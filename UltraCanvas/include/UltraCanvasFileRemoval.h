// include/UltraCanvasFileRemoval.h
// Deleting files and folders for good, the way Explorer does it, and saying
// WHY when it cannot be done in terms of what the user can do about it.
//
// doc-check: std::filesystem::path folder;
// doc-check: example
//   std::error_code ec;
//   FileRemovalProblem problem = FileRemovalProblem::NoProblem;
//   std::filesystem::path stoppedAt;
//   if (!RemoveFileSystemTree(folder, ec, &problem, &stoppedAt)) {
//       if (problem == FileRemovalProblem::InUse) { /* close the program, try again */ }
//       if (problem == FileRemovalProblem::NotPermitted) { /* an administrator may */ }
//   }
//
// std::filesystem::remove answers "access denied" on Windows for four
// different things, and only one of them is about permission:
//
//   - the read-only attribute, on a file or on a FOLDER (a folder with a
//     desktop.ini, one copied off a CD or a camera's card). Explorer lifts it
//     without asking; so does this.
//   - "delete pending": the entry was deleted, but a program still holds it
//     open (a virus scanner, the search indexer, a thumbnail extractor), so
//     Windows keeps listing it until that program lets go - and refuses every
//     attempt to look at it in the meantime. It is gone as far as a delete is
//     concerned, and its folder empties within moments; this counts it as
//     removed and waits that moment for the folder.
//   - a file a program has mapped into memory - a running program's
//     executable or library, or a picture an image library memory-maps (as
//     libvips does for a JPEG it has read). The delete is refused while the
//     mapping lasts, to everybody, administrators included. That is
//     FileRemovalProblem::InUse: close the program and it goes.
//   - an access list that does not let this user delete it. Only that one is
//     FileRemovalProblem::NotPermitted, where retrying as administrator
//     (UltraCanvasElevatedFileOperations) can help.
//
// Telling them apart is what keeps a file manager from asking for
// administrator rights that cannot help - the UAC prompt, the elevated retry,
// and the same "access denied" again.
//
// Where the system lets it (Windows 10 1809 and later, on NTFS), an entry is
// removed with POSIX semantics: its name goes at once, even while another
// program still holds it, so its folder can be removed straight after. FAT,
// exFAT, network shares and older Windows get the classic delete.
//
// Backends: Windows (OS/MSWindows/UltraCanvasWindowsFileRemoval.cpp). Every
// other platform removes with std::filesystem::remove, which has none of
// these cases - unlinking an open file is allowed there - and classifies the
// errno.
// Version: 1.0.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework
#pragma once

#include <filesystem>
#include <system_error>

namespace UltraCanvas {

    // ===== WHY A REMOVAL FAILED =====
    // In terms of the user's next step, not of the error code.
    enum class FileRemovalProblem {
        NoProblem,      // removed, already gone, or on its way out
        InUse,          // a program holds it (open without delete sharing,
                        // mapped, running) - closing that program helps,
                        // administrator rights do not
        NotPermitted,   // this user may not delete it - an administrator may
        NotEmpty,       // a folder that still has entries in it
        Other           // anything else; the error code says what
    };

    // ===== REMOVAL =====
    // Removes one file, one symbolic link or junction (the link itself, never
    // what it points at) or one EMPTY folder. True when it is gone: removed
    // now, not there to begin with, or already deleted and only waiting for a
    // program to let go of it (Windows "delete pending"). A read-only
    // attribute is no obstacle; when the removal fails anyway the attribute
    // is put back. On Windows a folder whose remaining entries are all delete
    // pending is waited for, about two seconds at most. On failure `ec` is
    // the system's error and `problem` (when given) says what it means.
    bool RemoveFileSystemEntry(const std::filesystem::path& path,
                               std::error_code& ec,
                               FileRemovalProblem* problem = nullptr);

    // Removes `path` and everything under it: post-order, every entry through
    // RemoveFileSystemEntry, links removed as links and never followed (the
    // rule of std::filesystem::remove_all, which this replaces where the
    // reason for a failure matters). `stoppedAt` (when given) names the entry
    // that refused - inside a folder that is rarely the folder itself.
    bool RemoveFileSystemTree(const std::filesystem::path& path,
                              std::error_code& ec,
                              FileRemovalProblem* problem = nullptr,
                              std::filesystem::path* stoppedAt = nullptr);

    // Windows: whether `path` has already been deleted and is listed only
    // because a program still holds it open. Every attempt to read it then
    // fails with "access denied", and it vanishes by itself once that
    // program lets go - so a delete counts it as removed. Always false
    // elsewhere, where a deleted entry is never listed.
    bool FileSystemEntryIsDeletePending(const std::filesystem::path& path);

    // What a failed std::filesystem call's error means in the terms above:
    // for a failure that did not come from RemoveFileSystemEntry, such as
    // listing a folder or reading an entry's type on the way to removing it.
    FileRemovalProblem ClassifyFileRemovalError(const std::error_code& ec);

    // ===== BACKEND INTERFACE (implemented per platform under OS/<Platform>/) =====
    // Not part of the public surface. Windows implements them in
    // OS/MSWindows/UltraCanvasWindowsFileRemoval.cpp; the core file provides
    // the std::filesystem versions on every other platform. `problem` is set
    // on failure only.
    bool NativeRemoveFileSystemEntry(const std::filesystem::path& path,
                                     std::error_code& ec,
                                     FileRemovalProblem& problem);
    bool NativeFileSystemEntryIsDeletePending(const std::filesystem::path& path);
    FileRemovalProblem NativeClassifyFileRemovalError(const std::error_code& ec);

} // namespace UltraCanvas
